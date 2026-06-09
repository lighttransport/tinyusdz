// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-present Light Transport Entertainment, Inc.
//
// http-asset-resolver.js — HTTP(fetch)-based USD asset resolution demo.
//
// Shows two flows for resolving a USD scene's external assets (sublayers,
// references, payloads, textures) over the network with fetch():
//
//   Demo 1 — Local USDA, HTTP asset path:
//     A USDA loaded from the same origin whose texture is authored as an
//     absolute https:// URL. The unresolved texture is fetched over HTTP and
//     fed back into the converter, then rendered.
//
//   Demo 2 — Root USD over HTTP, rewrite every reference to HTTP:
//     A root .usd/.usda/.usdc/.usdz loaded from a remote host (e.g. the
//     usd-wg/assets test_assets on raw.githubusercontent.com). ALL relative
//     asset paths it references are rewritten onto that host's directory and
//     pulled over HTTP — composition arcs first (references/payloads/sublayers),
//     then textures — so the whole scene resolves from the remote location.
//
// How resolution works (reusing the existing pieces):
//   - HttpAssetResolver rewrites a USD asset path (relative, or already
//     absolute http(s)/data/blob) to an absolute URL against a configurable
//     base, fetches it, and caches it under the path AS WRITTEN in the USD.
//   - TinyUSDZComposer.progressiveComposition() runs the LIVRPS loop:
//     extract*AssetPaths() -> resolver.resolveAsync() (fetch) -> layer.setAsset()
//     -> compose*(). We inject HttpAssetResolver via composer.setAssetResolver().
//   - Textures are resolved in a second pass: layerToRenderScene() leaves remote
//     textures unresolved (image.bufferId == -1); we read each unresolved image's
//     authored path via getImagePtr().uri, fetch it over HTTP, setAsset(), and
//     re-convert. tydra resolves MaterialX and UsdPreviewSurface into the SAME
//     material record, so "simple MaterialX shading" needs no JS network eval.
//
// Memory: rendering goes through StreamingUSDRenderer.renderComposedNative(),
// which keeps textures ENCODED in the WASM heap (decoded per-texture in JS via
// createImageBitmap, off-heap), uploads geometry through zero-copy heap views,
// then reset()s the WASM scene — the low-memory streaming technique, applied to
// an HTTP-composed scene. (The RenderStream incremental path can't compose
// external references, so the composition demos use this eager+encoded path.)
//
// Usage (module): include http-asset-resolver.html, which calls
// mountHttpAssetResolverDemo().

import { StreamingUSDRenderer } from './streaming.js';
import { TinyUSDZComposer } from './src/tinyusdz/TinyUSDZComposer.js';
import { parseUSDZEntries } from './src/usdzconvert.js';

// ---------------------------------------------------------------------------
// HttpAssetResolver — fetch + base-URL rewrite, keyed by the authored path.
// ---------------------------------------------------------------------------

// Implements the resolver interface TinyUSDZComposer expects:
//   resolveAsync(assetPath) -> Promise<[assetPath, ArrayBuffer]>
//   getAsset / hasAsset / setAsset / clearCache
// plus rewrite(): relative paths resolve against `baseUrl`, absolute ones
// (http(s)/data/blob) pass through unchanged. resolveAsync RETURNS THE ORIGINAL
// authored path as the key, so layer.setAsset(path, bytes) stores it under the
// name the native composer/converter looks it up by (a direct cache hit).
export class HttpAssetResolver {
  constructor({ baseUrl = '' } = {}) {
    this.baseUrl = baseUrl;
    this.assetCache = new Map();   // authored path -> ArrayBuffer
    this.fetchLog = [];            // [{ path, url, bytes, ok, error? }]
    this.bytesFetched = 0;
  }

  setBaseUrl(url) { this.baseUrl = url || ''; }

  static isAbsolute(p) { return /^(https?:|data:|blob:)/i.test(String(p || '')); }

  // Resolve an authored asset path to an absolute URL to fetch.
  rewrite(assetPath) {
    const p = String(assetPath || '');
    if (HttpAssetResolver.isAbsolute(p)) return p;
    const base = this.baseUrl || (typeof location !== 'undefined' ? location.href : '');
    if (!base) return p;
    // `new URL` collapses ./ and ../ relative to the base directory.
    return new URL(p, base).href;
  }

  async resolveAsync(assetPath) {
    if (this.assetCache.has(assetPath)) return [assetPath, this.assetCache.get(assetPath)];
    const url = this.rewrite(assetPath);
    let bytes;
    try {
      const resp = await fetch(url, { cache: 'no-store', headers: { Accept: '*/*' } });
      if (!resp.ok && resp.status !== 206) {
        throw new Error(`HTTP ${resp.status} ${resp.statusText}`);
      }
      bytes = await resp.arrayBuffer();
    } catch (e) {
      this.fetchLog.push({ path: assetPath, url, bytes: 0, ok: false, error: e.message });
      throw new Error(`Failed to fetch '${assetPath}' (${url}): ${e.message}`);
    }
    this.assetCache.set(assetPath, bytes);
    this.bytesFetched += bytes.byteLength;
    this.fetchLog.push({ path: assetPath, url, bytes: bytes.byteLength, ok: true });
    return [assetPath, bytes];
  }

  getAsset(uri) { return this.assetCache.has(uri) ? this.assetCache.get(uri) : null; }
  hasAsset(uri) { return this.assetCache.has(uri); }
  setAsset(uri, data) { this.assetCache.set(uri, data); }
  clearCache() { this.assetCache.clear(); this.fetchLog = []; this.bytesFetched = 0; }
}

// ---------------------------------------------------------------------------
// Texture resolution over HTTP (two-pass).
// ---------------------------------------------------------------------------

// Convert the composed layer to a render scene, then fetch — in JS, off the
// WASM heap — the encoded bytes for every texture the WASM side left unresolved
// (image.bufferId < 0). We deliberately do NOT round-trip texture bytes back
// through the native asset resolver: SanitizeAssetPath() collapses URL-ish and
// `..` paths (e.g. `https://` -> `https:/`), so a setAsset(path)/re-convert
// would never match. Instead we hand the encoded bytes to the renderer keyed by
// image id; it decodes them with createImageBitmap (off-heap) and binds them to
// the materials that already reference that image. Returns
// { textureBytesById: Map<imageId, Uint8Array>, fetched, missing }.
export async function resolveTexturesOverHttp(usd, resolver, { onStatus } = {}) {
  if (!usd.layerToRenderScene()) {
    throw new Error('layerToRenderScene failed: ' + (usd.error ? usd.error() : 'unknown'));
  }
  const textureBytesById = new Map();
  let fetched = 0;
  let missing = 0;

  let n = 0;
  try { n = usd.numImages(); } catch (_) { n = 0; }
  for (let i = 0; i < n; i++) {
    let m;
    try { m = usd.getImagePtr(i); } catch (_) { continue; }
    // Already resolved into the WASM heap (e.g. a .usdz-embedded texture) — the
    // renderer reads it straight from the heap; nothing to fetch.
    if (!m || (typeof m.byteLength === 'number' && m.byteLength > 0)) continue;
    const uri = m.uri;
    if (!uri) { missing++; continue; }
    try {
      const [, bytes] = await resolver.resolveAsync(uri); // rewritten onto the host
      textureBytesById.set(i, new Uint8Array(bytes));
      fetched++;
      onStatus && onStatus(`Fetched texture: ${uri}`);
    } catch (e) {
      missing++;
      onStatus && onStatus(`Texture missing (skipped): ${uri}`);
    }
  }
  return { textureBytesById, fetched, missing };
}

// ---------------------------------------------------------------------------
// Compose a (possibly remote) USD over HTTP and hand it to the renderer.
// ---------------------------------------------------------------------------

// Heuristic: does this root reference a MaterialX material? Used only to label
// the shading mode — tydra unifies MaterialX and UsdPreviewSurface into one
// material record, so both render through the same path.
function detectMaterialX(bytes) {
  // Sniff the first chunk as text (works for .usda; harmless for binary crates).
  const head = new TextDecoder('utf-8', { fatal: false }).decode(
    bytes.subarray(0, Math.min(bytes.length, 1 << 16)));
  return /\.mtlx\b/i.test(head) ||
    /info:implementationSource\s*=\s*"sourceAsset"/.test(head) ||
    /\bND_[a-zA-Z0-9_]+/.test(head) ||
    /MaterialX/i.test(head) ||
    /info:mtlx:/i.test(head);
}

function isUsdz(name) { return /\.usdz$/i.test(name || ''); }

// Build a composed native Layer instance from root bytes, fetching every
// external arc + texture over HTTP via `resolver`. Returns
// { usd, textureBytesById } — the native instance (already through
// layerToRenderScene; caller renders then owns/frees it) plus the JS-held
// encoded bytes for HTTP textures keyed by image id.
async function composeOverHttp({ renderer, rootBytes, filename, resolver, onStatus }) {
  const native = renderer.native;
  const u8 = rootBytes instanceof Uint8Array ? rootBytes : new Uint8Array(rootBytes);

  const layer = new native.TinyUSDZLoaderNative();
  // Keep textures ENCODED in the heap (JS decodes them off-heap) — low memory.
  if (typeof layer.setLoadTextureInNative === 'function') layer.setLoadTextureInNative(false);
  // Allow USD `../` parent-dir references during composition (resolved by the
  // sandboxed in-memory resolver). On by default in recent WASM builds; opt in
  // explicitly so older builds that default off still get it where available.
  if (typeof layer.setAllowParentRelativeAssetPaths === 'function') {
    layer.setAllowParentRelativeAssetPaths(true);
  }

  // Seed the in-archive assets for a .usdz root, then load the root crate.
  let rootForLoad = u8;
  if (isUsdz(filename)) {
    let entries;
    try { entries = parseUSDZEntries(u8); } catch (e) { entries = null; }
    if (entries && entries.length) {
      const root = entries.find((e) => /\.usd[ac]?$/i.test(e.name)) || entries[0];
      for (const e of entries) layer.setAsset(e.name, e.data); // in-archive textures/layers
      rootForLoad = root.data;
      filename = root.name;
      onStatus && onStatus(`USDZ: seeded ${entries.length} in-archive assets`);
    }
  }

  if (!layer.loadAsLayerFromBinary(rootForLoad, filename)) {
    const err = layer.error ? layer.error() : 'unknown';
    if (typeof layer.delete === 'function') layer.delete();
    throw new Error(`loadAsLayerFromBinary failed for ${filename}: ${err}`);
  }

  // Composition arcs (references/payloads/sublayers) over HTTP, reusing the
  // proven LIVRPS loop with our HTTP resolver injected.
  const composer = new TinyUSDZComposer();
  composer.setLayer(layer);
  composer.setUSDLoader(renderer.loader);
  composer.setAssetResolver(resolver);
  composer.setBaseWorkingPath('./');
  composer.setAssetSearchPaths(['./']);
  try {
    await composer.progressiveComposition();
  } catch (e) {
    onStatus && onStatus(`Composition note: ${e.message}`);
  }

  // Textures over HTTP: convert once, then fetch each unresolved image's bytes
  // in JS (off the WASM heap) keyed by image id for the renderer to decode.
  const tex = await resolveTexturesOverHttp(layer, resolver, { onStatus });
  onStatus && onStatus(
    `Resolved ${tex.fetched} texture(s) over HTTP` +
    (tex.missing ? `, ${tex.missing} unresolved` : ''));

  return { usd: layer, textureBytesById: tex.textureBytesById };
}

// ---------------------------------------------------------------------------
// Demo bootstrap.
// ---------------------------------------------------------------------------

const LOCAL_HTTP_TEXTURE_USDA = './assets/http-cat-plane.usda';

// A few usd-wg/assets test_assets (raw GitHub serves permissive CORS). These are
// editable in the UI; pick the one that exists / renders best for your build.
const TEST_ASSET_BASE = 'https://raw.githubusercontent.com/usd-wg/assets/main/test_assets/';
const DEMO2_PRESETS = [
  // UsdPreviewSurface; normal/bump textures referenced (./r_*.png) in the same
  // remote directory -> rewritten onto the host and pulled over HTTP.
  { label: 'NormalsTextureBiasAndScale', url: TEST_ASSET_BASE + 'NormalsTextureBiasAndScale/NormalsTextureBiasAndScale.usda' },
  // MaterialX (flattened): inline geometry + MaterialX materials; textures
  // (textures/brass_*.jpg) pulled over HTTP. Shows "simple MaterialX shading"
  // (tydra-unified).
  { label: 'MaterialXTest (flattened)', url: TEST_ASSET_BASE + 'MaterialXTest/basicTextured_flatten.usda' },
  // Textured quads with texture-transform; textures live under ./0 (subdir).
  { label: 'TextureTransformTest', url: TEST_ASSET_BASE + 'TextureTransformTest/TextureTransformTest.usd' },
];

function fmtMB(b) { return (b / 1048576).toFixed(1) + ' MB'; }

export async function mountHttpAssetResolverDemo(opts = {}) {
  const canvas = opts.canvas || document.getElementById('gl');
  const statusEl = opts.status || document.getElementById('status');
  const logEl = opts.log || document.getElementById('log');
  const panel = opts.panel || document.getElementById('mem-panel');
  const urlInput = opts.urlInput || document.getElementById('asset-url');
  const btnLocal = opts.btnLocal || document.getElementById('btn-demo1');
  const btnHttp = opts.btnHttp || document.getElementById('btn-demo2');
  const presetSel = opts.presetSelect || document.getElementById('preset');

  const renderer = new StreamingUSDRenderer(canvas);
  await renderer.init();

  let busy = false;
  const setStatus = (s) => { if (statusEl) statusEl.textContent = s; };
  const appendLog = (s) => {
    if (!logEl) { console.log('[http-asset-resolver]', s); return; }
    const line = document.createElement('div');
    line.textContent = s;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
  };

  // Live + final memory panel (same shape StreamingUSDRenderer emits).
  const renderPanel = (m) => {
    if (!panel) return;
    const ratio = m.inputBytes ? (m.heapReserved / m.inputBytes).toFixed(2) : '–';
    const rows = [
      ['Input + fetched', fmtMB(m.inputBytes)],
      ['WASM heap (reserved peak)', fmtMB(m.heapReserved)],
      ['WASM render buffers (live)', fmtMB(m.renderBuffers)],
      ['Heap / input', ratio + '×'],
    ];
    let html = '<table>' + rows.map(([k, v]) =>
      `<tr><td>${k}</td><td style="text-align:right">${v}</td></tr>`).join('') + '</table>';
    if (m.last && m.last.phases) {
      html += '<div class="phases"><b>Load phases (heap / live buffers)</b><table>' +
        m.last.phases.map((p) =>
          `<tr><td>${p.label}</td><td style="text-align:right">${fmtMB(p.heapReserved)}</td>` +
          `<td style="text-align:right">${fmtMB(p.renderBuffers || 0)}</td></tr>`).join('') +
        '</table></div>';
    }
    panel.innerHTML = html;
  };
  renderer.onMemory(renderPanel);
  renderer.startMemoryPolling(500);

  // Shared loader for a root that's already bytes (local file path or remote URL).
  async function run({ rootBytes, filename, baseUrl, label }) {
    if (busy) return;
    busy = true;
    if (btnLocal) btnLocal.disabled = true;
    if (btnHttp) btnHttp.disabled = true;
    if (logEl) logEl.innerHTML = '';
    try {
      const t0 = performance.now();
      const mtlx = detectMaterialX(rootBytes);
      const shadeLabel = mtlx ? 'MaterialX shading (simple, via tydra)' : 'UsdPreviewSurface';
      setStatus(`Composing ${label} over HTTP… (${shadeLabel})`);

      const resolver = new HttpAssetResolver({ baseUrl });
      const { usd, textureBytesById } = await composeOverHttp({ renderer, rootBytes, filename, resolver, onStatus: appendLog });

      const inputBytes = rootBytes.byteLength + resolver.bytesFetched;
      const r = await renderer.renderComposedNative(usd, label, { inputBytes, textureBytesById });
      const dt = (performance.now() - t0).toFixed(0);

      const s = r.memory.summary;
      setStatus(`${label}: ${r.meshes} meshes, ${r.textures} textures, ${r.materials} materials ` +
        `in ${dt} ms — ${shadeLabel}; peak WASM heap ${s.peakHeapMB.toFixed(1)} MB ` +
        `(${s.ratio.toFixed(2)}× of ${fmtMB(inputBytes)} fetched).`);
      appendLog(`HTTP fetches: ${resolver.fetchLog.length} ` +
        `(${resolver.fetchLog.filter((f) => f.ok).length} ok), ${fmtMB(resolver.bytesFetched)} total.`);
    } catch (e) {
      console.error(e);
      setStatus('Error: ' + (e && e.message));
      appendLog('Error: ' + (e && e.message));
    } finally {
      busy = false;
      if (btnLocal) btnLocal.disabled = false;
      if (btnHttp) btnHttp.disabled = false;
    }
  }

  // Demo 1 — local USDA whose texture is an absolute https:// URL.
  async function loadLocalHttpTexture() {
    setStatus(`Fetching local ${LOCAL_HTTP_TEXTURE_USDA}…`);
    const resp = await fetch(LOCAL_HTTP_TEXTURE_USDA);
    if (!resp.ok) { setStatus(`Cannot load ${LOCAL_HTTP_TEXTURE_USDA}: HTTP ${resp.status}`); return; }
    const bytes = new Uint8Array(await resp.arrayBuffer());
    // baseUrl '' — the texture path is already absolute http, fetched directly.
    await run({ rootBytes: bytes, filename: 'http-cat-plane.usda', baseUrl: '', label: 'Local USDA (HTTP texture)' });
  }

  // Demo 2 — root USD over HTTP; rewrite all relative refs/textures onto its dir.
  async function loadOverHttp(url) {
    const target = url || (urlInput && urlInput.value) || DEMO2_PRESETS[0].url;
    setStatus(`Fetching root ${target}…`);
    let bytes;
    try {
      const resp = await fetch(target, { cache: 'no-store' });
      if (!resp.ok) throw new Error(`HTTP ${resp.status} ${resp.statusText}`);
      bytes = new Uint8Array(await resp.arrayBuffer());
    } catch (e) {
      setStatus(`Cannot fetch ${target}: ${e.message}`);
      return;
    }
    const filename = target.split('/').pop() || 'root.usd';
    // baseUrl = the root's directory, so relative refs/textures rewrite onto it.
    const baseUrl = target.slice(0, target.lastIndexOf('/') + 1);
    await run({ rootBytes: bytes, filename, baseUrl, label: filename });
  }

  // Wire UI.
  if (presetSel) {
    presetSel.innerHTML = DEMO2_PRESETS.map((p, i) =>
      `<option value="${p.url}"${i === 0 ? ' selected' : ''}>${p.label}</option>`).join('');
    presetSel.addEventListener('change', () => { if (urlInput) urlInput.value = presetSel.value; });
  }
  if (urlInput && !urlInput.value) urlInput.value = DEMO2_PRESETS[0].url;
  if (btnLocal) btnLocal.addEventListener('click', () => loadLocalHttpTexture());
  if (btnHttp) btnHttp.addEventListener('click', () => loadOverHttp());

  // Optional auto-run via ?demo=1|2&url=…
  if (opts.autoRun !== false && typeof location !== 'undefined') {
    const q = new URLSearchParams(location.search);
    if (q.get('demo') === '1') loadLocalHttpTexture();
    else if (q.get('demo') === '2') loadOverHttp(q.get('url') || undefined);
    else setStatus('Pick a demo: load the local USDA (HTTP texture), or a USD over HTTP.');
  }

  return { renderer, loadLocalHttpTexture, loadOverHttp };
}

export default mountHttpAssetResolverDemo;
