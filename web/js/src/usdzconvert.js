// Shared usdzconvert orchestration used by both the browser module
// (web/js/usdzconvert.js) and the Node CLI (web/js/cli/usdzconvert.js).
//
// Flow: load the root USD as a layer -> for every referenced texture, optionally
// resize / re-encode it (via the WASM `convertImage` binding, fpng in WASM) and
// register it in the asset cache under its USD-relative name -> exportAsUSDZ()
// (which flattens the stage and packs the cached image assets).

const USD_RE = /\.(usd|usda|usdc|usdz)$/i;
const IMG_RE = /\.(png|jpg|jpeg|exr|avif)$/i;

export function isImageName(name) {
  return IMG_RE.test(name);
}

export function isUsdName(name) {
  return USD_RE.test(name);
}

// Format string for the WASM convertImage() encoder, or null if we should not
// re-encode (e.g. EXR/AVIF — left untouched).
export function imageFormatFromName(name) {
  const ext = (name.toLowerCase().split('.').pop() || '');
  if (ext === 'jpg' || ext === 'jpeg') return 'jpeg';
  if (ext === 'png') return 'png';
  return null;
}

// Parse a human byte size: "100MB", "50mb", "1.5g", "1048576" -> bytes (0 on fail).
export function parseByteSize(input) {
  if (typeof input === 'number') return Math.max(0, Math.floor(input));
  const s = String(input || '').trim();
  const m = s.match(/^([0-9]*\.?[0-9]+)\s*([kmgt]?b?)?$/i);
  if (!m) return 0;
  const num = parseFloat(m[1]);
  const unit = (m[2] || '').toLowerCase();
  const mult = unit.startsWith('k') ? 1024
    : unit.startsWith('m') ? 1024 ** 2
    : unit.startsWith('g') ? 1024 ** 3
    : unit.startsWith('t') ? 1024 ** 4
    : 1;
  return Math.floor(num * mult);
}

// Replace a path's extension (keeping directories): "a/b.png","jpg" -> "a/b.jpg".
export function replaceExt(name, ext) {
  const dot = name.lastIndexOf('.');
  const slash = name.lastIndexOf('/');
  if (dot > slash) return name.slice(0, dot + 1) + ext;
  return name + '.' + ext;
}

// Instantiate the Emscripten module. `importer` returns the dynamic import of
// the compiled glue (e.g. () => import('./src/tinyusdz/tinyusdz.js')).
export async function loadWasm(importer) {
  const mod = await importer();
  return await mod.default();
}

// Pick the root USD layer from an asset map (Map<path, bytes>).
// Prefers shallow paths and non-.usdz layers (a .usdz is self-contained).
export function rootUsdFromMap(assetMap, preferred) {
  if (preferred && assetMap.has(preferred)) return preferred;
  const usds = [...assetMap.keys()].filter(isUsdName);
  usds.sort((a, b) => {
    const za = /\.usdz$/i.test(a) ? 1 : 0;
    const zb = /\.usdz$/i.test(b) ? 1 : 0;
    if (za !== zb) return za - zb; // non-usdz first
    return a.split('/').length - b.split('/').length; // shallower first
  });
  return usds[0] || null;
}

// Convert an asset map (Map<path, Uint8Array>) into a USDZ Uint8Array.
//
// opts: { rootPath?, maxTextureSize?, reencode?, pngEncoder?, jpegQuality?, log? }
// returns { usdz: Uint8Array, stats: { textures, resized, reencoded, rootPath } }
export async function convertFolderToUSDZ(native, assetMap, opts = {}) {
  const log = opts.log || (() => {});
  const rootPath = opts.rootPath || rootUsdFromMap(assetMap);
  if (!rootPath) throw new Error('No USD file (.usd/.usda/.usdc/.usdz) found in the input.');

  const images = [...assetMap.keys()].filter(isImageName);
  if (/\.usdz$/i.test(rootPath) && images.length === 0) {
    log('WARN: root is a self-contained .usdz with no separate texture files; ' +
        'internal textures are not repacked in the web version (use the native tusdzconvert CLI).');
  }

  // Directory of the root USD; texture references are relative to it.
  const rootDir = rootPath.includes('/') ? rootPath.slice(0, rootPath.lastIndexOf('/') + 1) : '';
  const stats = { textures: 0, resized: 0, reencoded: 0, rootPath };

  // USD-relative asset name for a given uploaded path.
  const assetNameFor = (path) =>
    (rootDir && path.startsWith(rootDir)) ? path.slice(rootDir.length) : path;

  const usd = new native.TinyUSDZLoaderNative();
  try {
    // --- Budget-fit path: shrink all textures to a total byte budget. ---
    const budget = opts.targetTextureBytes || 0;
    if (budget > 0 && images.length > 0) {
      const entries = images.map((path) => ({
        path, name: assetNameFor(path), data: assetMap.get(path),
      }));
      const fit = native.fitTextures({
        images: entries.map((e) => ({ data: e.data, name: e.name })),
        targetBytes: budget,
        strategy: opts.fitStrategy === 'quality' ? 'quality' : 'size',
        startMaxSize: opts.maxTextureSize || 0,
        minTextureSize: opts.fitMinTextureSize || 64,
        minQuality: opts.fitMinQuality || 30,
        jpegQuality: opts.jpegQuality || 90,
        pngEncoder: opts.pngEncoder || 'auto',
      });
      if (!fit || !fit.success) throw new Error('fitTextures failed: ' + (fit && fit.error));
      if (fit.warn) log('WARN: ' + fit.warn);

      const remap = {};
      for (let i = 0; i < fit.results.length; i++) {
        const r = fit.results[i];
        const oldName = entries[i].name;
        const newName = replaceExt(oldName, r.ext);
        usd.setAsset(newName, new Uint8Array(r.data));
        stats.textures++;
        stats.reencoded++;
        if (opts.fitStrategy === 'size') stats.resized++;
        if (newName !== oldName) remap[oldName] = newName;
        log(`  ${oldName} -> ${newName} [${r.width}x${r.height}, ${r.data.length} bytes]`);
      }

      const usdBytes = assetMap.get(rootPath);
      const ok = usd.loadAsLayerFromBinary(usdBytes, rootPath.split('/').pop());
      if (!ok) throw new Error('Failed to load USD: ' + usd.error());
      if (typeof usd.warn === 'function' && usd.warn()) log('WARN: ' + usd.warn());

      const data = Object.keys(remap).length > 0
        ? usd.exportAsUSDZWithRemap(remap)
        : usd.exportAsUSDZ();
      if (!data) throw new Error('USDZ export failed: ' + usd.error());
      stats.fitTotalBytes = fit.totalBytes;
      return { usdz: new Uint8Array(data), stats };
    }

    // --- Default path: per-texture resize/re-encode (preserve names). ---
    for (const path of images) {
      const bytes = assetMap.get(path);
      // Name as the USD most likely references it (relative to the USD's dir).
      let assetName = (rootDir && path.startsWith(rootDir)) ? path.slice(rootDir.length) : path;

      stats.textures++;
      let outBytes = bytes;

      const fmt = imageFormatFromName(path);
      const wantResize = (opts.maxTextureSize || 0) > 0;
      if (fmt && (wantResize || opts.reencode)) {
        const res = native.convertImage(bytes, {
          maxSize: opts.maxTextureSize || 0,
          format: fmt,
          pngEncoder: opts.pngEncoder || 'auto',
          jpegQuality: opts.jpegQuality || 90,
        });
        if (res && res.success) {
          outBytes = new Uint8Array(res.data); // copy out of wasm heap
          if (res.resized) stats.resized++;
          stats.reencoded++;
          log(`  ${assetName}: ${bytes.length} -> ${outBytes.length} bytes` +
              (res.resized ? ` [resized ${res.width}x${res.height}]` : ' [reencoded]'));
        } else {
          log(`  ${assetName}: convertImage failed (${res && res.error}); using original`);
        }
      } else {
        log(`  ${assetName}: ${bytes.length} bytes [passthrough]`);
      }

      usd.setAsset(assetName, outBytes);
    }

    const usdBytes = assetMap.get(rootPath);
    const ok = usd.loadAsLayerFromBinary(usdBytes, rootPath.split('/').pop());
    if (!ok) throw new Error('Failed to load USD: ' + usd.error());
    if (typeof usd.warn === 'function' && usd.warn()) log('WARN: ' + usd.warn());

    const data = usd.exportAsUSDZ();
    if (!data) throw new Error('USDZ export failed: ' + usd.error());
    const usdz = new Uint8Array(data); // copy out of wasm heap

    return { usdz, stats };
  } finally {
    usd.delete();
  }
}
