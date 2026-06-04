// Shared usdzconvert orchestration used by both the browser module
// (web/js/usdzconvert.js) and the Node CLI (web/js/cli/usdzconvert.js).
//
// Flow: load the root USD as a layer -> for every referenced texture, optionally
// resize / re-encode it (via an injected browser texture processor, or the WASM
// `convertImage` binding as fallback) and register it in the asset cache under
// its USD-relative name -> exportAsUSDZ() (which flattens the stage and packs
// the cached image assets).

const USD_RE = /\.(usd|usda|usdc|usdz)$/i;
const IMG_RE = /\.(png|jpg|jpeg|exr|avif)$/i;
// Audio formats referenced by UsdMediaSpatialAudio (filePath asset). USD itself
// is codec-agnostic; these are the formats commonly embedded in USDZ.
const AUDIO_RE = /\.(m4a|mp3|wav|aac|ogg|flac|aiff|aif)$/i;

export function isImageName(name) {
  return IMG_RE.test(name);
}

export function isUsdName(name) {
  return USD_RE.test(name);
}

export function isAudioName(name) {
  return AUDIO_RE.test(name);
}

// Format string for the WASM convertImage() encoder, or null if we should not
// re-encode (e.g. AVIF — left untouched). EXR is encodable now (recent USDZ
// allows EXR textures): the WASM build can resize and re-encode it, or transcode
// it to PNG/JPEG.
export function imageFormatFromName(name) {
  const ext = (name.toLowerCase().split('.').pop() || '');
  if (ext === 'jpg' || ext === 'jpeg') return 'jpeg';
  if (ext === 'png') return 'png';
  if (ext === 'exr') return 'exr';
  return null;
}

export function normalizedTextureFormat(format) {
  const f = String(format || 'keep').toLowerCase();
  if (f === 'jpg') return 'jpeg';
  if (f === 'jpeg' || f === 'png' || f === 'exr') return f;
  return 'keep';
}

export function outputFormatForImage(name, textureFormat = 'keep') {
  const requested = normalizedTextureFormat(textureFormat);
  const fmt = requested === 'keep' ? imageFormatFromName(name) : requested;
  if (fmt === 'jpeg') {
    const originalExt = (name.toLowerCase().split('.').pop() || '');
    const ext = requested === 'keep' && originalExt === 'jpeg' ? 'jpeg' : 'jpg';
    return { format: 'jpeg', ext };
  }
  if (fmt === 'png') return { format: 'png', ext: 'png' };
  // Keep EXR as EXR (resize-only), or force EXR output for any input.
  if (fmt === 'exr') return { format: 'exr', ext: 'exr' };
  return { format: null, ext: null };
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

// Unpack a USDZ archive (Uint8Array) into { entries: Map<name, Uint8Array>,
// order: string[] }. USDZ mandates STORE (no compression) and 64-byte data
// alignment, so we only walk the central directory — no inflate needed.
// Throws on a compressed entry or a malformed archive.
export function unpackUSDZ(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (u8.length < 22) throw new Error('Not a valid USDZ/ZIP (too small).');
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const td = new TextDecoder();

  // Locate the End Of Central Directory record (sig 0x06054b50), scanning back
  // over the (≤64KB) trailing comment.
  let eocd = -1;
  const minStart = Math.max(0, u8.length - 22 - 0xffff);
  for (let i = u8.length - 22; i >= minStart; i--) {
    if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error('Not a valid USDZ/ZIP (no EOCD record).');

  const count = dv.getUint16(eocd + 10, true);
  let off = dv.getUint32(eocd + 16, true); // central directory offset
  const entries = new Map();
  const order = [];
  for (let n = 0; n < count; n++) {
    if (dv.getUint32(off, true) !== 0x02014b50) {
      throw new Error('Corrupt USDZ: bad central directory header.');
    }
    const method = dv.getUint16(off + 10, true);
    const compSize = dv.getUint32(off + 20, true);
    const uncompSize = dv.getUint32(off + 24, true);
    const nameLen = dv.getUint16(off + 28, true);
    const extraLen = dv.getUint16(off + 30, true);
    const commentLen = dv.getUint16(off + 32, true);
    const lho = dv.getUint32(off + 42, true); // local header offset
    const name = td.decode(u8.subarray(off + 46, off + 46 + nameLen));

    if (!name.endsWith('/')) {
      if (method !== 0) {
        throw new Error(`USDZ entry "${name}" is compressed (method ${method}); ` +
          'only STORE is supported.');
      }
      // Local header is 30 bytes + its own (possibly different) name/extra lens.
      const lNameLen = dv.getUint16(lho + 26, true);
      const lExtraLen = dv.getUint16(lho + 28, true);
      const dataStart = lho + 30 + lNameLen + lExtraLen;
      const size = compSize || uncompSize;
      entries.set(name, u8.subarray(dataStart, dataStart + size));
      order.push(name);
    }
    off += 46 + nameLen + extraLen + commentLen;
  }
  return { entries, order };
}

// Expand any .usdz archives in an asset map into their contents, so internal
// textures can be repacked. Returns { assetMap, innerRoot } where innerRoot is
// the first USD layer found inside a .usdz (USDZ guarantees the first archive
// entry is the default root layer). Separately-supplied (non-.usdz) files are
// overlaid on top so they can override archive contents.
export function expandUsdzInputs(assetMap, opts = {}) {
  const log = opts.log || (() => {});
  const out = new Map();
  let innerRoot = null;
  // Pass 1: unpack archives.
  for (const [path, data] of assetMap) {
    if (!/\.usdz$/i.test(path)) continue;
    let unpacked;
    try {
      unpacked = unpackUSDZ(data);
    } catch (err) {
      log(`WARN: could not unpack ${path}: ${err && err.message ? err.message : err}`);
      out.set(path, data);
      continue;
    }
    for (const name of unpacked.order) out.set(name, unpacked.entries.get(name));
    const root = unpacked.order.find(isUsdName);
    if (root && !innerRoot) innerRoot = root;
    log(`Unpacked ${path}: ${unpacked.order.length} entr${unpacked.order.length === 1 ? 'y' : 'ies'}` +
        (root ? ` (root layer: ${root})` : ''));
  }
  // Pass 2: overlay non-archive inputs (override archive contents).
  for (const [path, data] of assetMap) {
    if (/\.usdz$/i.test(path)) continue;
    out.set(path, data);
  }
  return { assetMap: out, innerRoot };
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
function exportUSDZ(usd, remap, opts) {
  const rootLayerFormat = String(opts.rootLayerFormat || 'usdc').toLowerCase() === 'usda' ? 'usda' : 'usdc';
  const exportOpts = {
    rootLayerFormat: opts.arkitCompatible ? 'usdc' : rootLayerFormat,
    arkitCompatible: !!opts.arkitCompatible,
  };
  if (opts.flatten === false && !opts.arkitCompatible &&
      typeof usd.exportLayerAsUSDZWithOptions === 'function') {
    return usd.exportLayerAsUSDZWithOptions({ rootLayerFormat: 'usda' });
  }
  const hasRemap = remap && Object.keys(remap).length > 0;
  // Faithful path: when no texture path remapping is needed (passthrough /
  // USDZ->USDZ roundtrip), write the composed LAYER directly. This preserves all
  // PrimSpec data and matches the native tusdzconvert output. The Stage-based
  // export below reconstructs typed Prims and drops typed shader inputs that lack
  // a USDC serializer (e.g. OpenPBR surface inputs), so only use it when a remap
  // or ARKit metadata rewrite is required.
  if (!hasRemap && !opts.arkitCompatible &&
      typeof usd.exportLayerAsUSDZWithOptions === 'function') {
    return usd.exportLayerAsUSDZWithOptions({ rootLayerFormat });
  }
  const hasOptions = exportOpts.rootLayerFormat !== 'usdc' || exportOpts.arkitCompatible;
  if (typeof usd.exportAsUSDZWithOptions === 'function' && (hasRemap || hasOptions)) {
    return usd.exportAsUSDZWithOptions(remap || {}, exportOpts);
  }
  if (hasRemap) return usd.exportAsUSDZWithRemap(remap);
  return usd.exportAsUSDZ();
}

function composeToFixedPoint(usd) {
  const steps = [
    { has: 'hasSublayers', compose: 'composeSublayers', name: 'sublayers' },
    { has: 'hasReferences', compose: 'composeReferences', name: 'references' },
    { has: 'hasPayload', compose: 'composePayload', name: 'payloads' },
    { has: 'hasInherits', compose: 'composeInherits', name: 'inherits' },
    { has: 'hasVariants', compose: 'composeVariants', name: 'variants' },
  ];
  for (let iter = 0; iter < 64; iter++) {
    let didCompose = false;
    for (const step of steps) {
      if (typeof usd[step.has] !== 'function' ||
          typeof usd[step.compose] !== 'function') {
        continue;
      }
      if (!usd[step.has]()) {
        continue;
      }
      if (!usd[step.compose]()) {
        throw new Error(`Failed to compose ${step.name}: ${usd.error()}`);
      }
      didCompose = true;
    }
    if (!didCompose) {
      return;
    }
  }
  throw new Error('Composition did not converge before the iteration limit.');
}

// opts: {
//   rootPath?, maxTextureSize?, reencode?, pngEncoder?, jpegQuality?, log?,
//   textureFormat?: "keep"|"png"|"jpeg", rootLayerFormat?: "usdc"|"usda",
//   arkitCompatible?, flatten?, textureProcessor?
// }
// returns { usdz: Uint8Array, stats: { textures, resized, reencoded, rootPath } }
export async function convertFolderToUSDZ(native, assetMap, opts = {}) {
  const log = opts.log || (() => {});
  let rootPath = opts.rootPath || rootUsdFromMap(assetMap);
  if (!rootPath) throw new Error('No USD file (.usd/.usda/.usdc/.usdz) found in the input.');

  const textureFormat = normalizedTextureFormat(opts.textureFormat);
  const hasSingleUsdzInput = /\.usdz$/i.test(rootPath) && assetMap.size === 1 &&
    assetMap.has(rootPath);
  const canPassthroughUsdz = hasSingleUsdzInput &&
    opts.passthroughUsdz !== false &&
    opts.reencode === false &&
    textureFormat === 'keep' &&
    (opts.maxTextureSize || 0) <= 0 &&
    (opts.targetTextureBytes || 0) <= 0 &&
    !opts.arkitCompatible;
  if (canPassthroughUsdz) {
    const data = assetMap.get(rootPath);
    log(`Passing through USDZ unchanged: ${rootPath}`);
    return {
      usdz: data instanceof Uint8Array ? data : new Uint8Array(data),
      stats: {
        textures: 0,
        resized: 0,
        reencoded: 0,
        audio: 0,
        otherAssets: 0,
        rootPath,
        rootLayerFormat: null,
        flatten: false,
        arkitCompatible: false,
        passthrough: true,
      },
    };
  }

  // If the root is a self-contained .usdz, unpack it so its internal textures
  // can be repacked (passthrough by default). Disable with repackUsdz: false.
  if (/\.usdz$/i.test(rootPath) && opts.repackUsdz !== false) {
    const expanded = expandUsdzInputs(assetMap, { log });
    if (expanded.innerRoot || [...expanded.assetMap.keys()].some(isUsdName)) {
      assetMap = expanded.assetMap;
      rootPath = expanded.innerRoot || rootUsdFromMap(assetMap);
      if (!rootPath) throw new Error('USDZ archive contained no USD layer.');
      log(`Repacking USDZ; inner root layer: ${rootPath}`);
    }
  }

  const rootLayerFormat = String(opts.rootLayerFormat || 'usdc').toLowerCase() === 'usda' ? 'usda' : 'usdc';
  const flatten = opts.flatten !== false || !!opts.arkitCompatible;

  const images = [...assetMap.keys()].filter(isImageName);
  if (/\.usdz$/i.test(rootPath)) {
    log('WARN: root is still a .usdz (could not be unpacked for texture repack); ' +
        'passing it through as an opaque layer.');
  }

  // Directory of the root USD; texture references are relative to it.
  const rootDir = rootPath.includes('/') ? rootPath.slice(0, rootPath.lastIndexOf('/') + 1) : '';
  const stats = {
    textures: 0,
    resized: 0,
    reencoded: 0,
    audio: 0,
    otherAssets: 0,
    rootPath,
    rootLayerFormat: flatten ? (opts.arkitCompatible ? 'usdc' : rootLayerFormat) : 'usda',
    flatten,
    arkitCompatible: !!opts.arkitCompatible,
  };
  if (opts.flatten === false) {
    log('WARN: non-flattened USDZ output preserves composition arcs and writes a USDA root layer.');
  }

  // USD-relative asset name for a given uploaded path.
  const assetNameFor = (path) =>
    (rootDir && path.startsWith(rootDir)) ? path.slice(rootDir.length) : path;

  const registerDependencyLayers = (usd) => {
    for (const path of assetMap.keys()) {
      if (path === rootPath || !isUsdName(path)) {
        continue;
      }
      if (/\.usdz$/i.test(path)) {
        continue;
      }
      usd.setAsset(assetNameFor(path), assetMap.get(path));
    }
  };

  // Carry through non-USD, non-image assets (audio, etc.) so they survive into
  // the repacked USDZ. Images are handled by the texture pipeline; USD layers by
  // registerDependencyLayers. Everything else is copied as-is here.
  //
  // Audio is a placeholder for a future transcode step: opts.audioProcessor, if
  // provided, may return { data, name } to replace the bytes; by default audio
  // passes through unchanged (no audio codecs are bundled in the web build).
  const registerPassthroughAssets = async (usd) => {
    for (const path of assetMap.keys()) {
      if (path === rootPath || isUsdName(path) || isImageName(path)) {
        continue;
      }
      const name = assetNameFor(path);
      let bytes = assetMap.get(path);
      const audio = isAudioName(path);
      if (audio && typeof opts.audioProcessor === 'function') {
        try {
          const processed = await opts.audioProcessor({ path, name, data: bytes, log });
          if (processed && processed.data) bytes = new Uint8Array(processed.data);
        } catch (err) {
          log(`  ${name}: audio processing failed (${err && err.message ? err.message : err}); passing through`);
        }
      }
      usd.setAsset(name, bytes);
      if (audio) { stats.audio++; log(`  ${name}: ${bytes.length} bytes [audio passthrough]`); }
      else { stats.otherAssets++; log(`  ${name}: ${bytes.length} bytes [asset passthrough]`); }
    }
  };

  const loadRootLayer = (usd) => {
    const usdBytes = assetMap.get(rootPath);
    const ok = usd.loadAsLayerFromBinary(usdBytes, rootPath.split('/').pop());
    if (!ok) throw new Error('Failed to load USD: ' + usd.error());
    if (typeof usd.warn === 'function' && usd.warn()) log('WARN: ' + usd.warn());
    if (flatten) {
      composeToFixedPoint(usd);
    }
  };

  const usd = new native.TinyUSDZLoaderNative();
  try {
    // Raise the USDC writer resource limits when requested (0 = keep the
    // conservative WASM default). Needed to export large scenes (dense meshes /
    // big timesample data) that exceed the built-in 100 MB / 256 MB caps.
    if ((opts.maxUsdcMb > 0 || opts.maxMemMb > 0) &&
        typeof usd.setUSDCExportLimitMB === 'function') {
      usd.setUSDCExportLimitMB(opts.maxUsdcMb || 0, opts.maxMemMb || 0);
    }

    registerDependencyLayers(usd);

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

      await registerPassthroughAssets(usd);
      loadRootLayer(usd);

      const data = exportUSDZ(usd, remap, opts);
      if (!data) throw new Error('USDZ export failed: ' + usd.error());
      stats.fitTotalBytes = fit.totalBytes;
      return { usdz: new Uint8Array(data), stats };
    }

    // --- Default path: per-texture resize/re-encode (preserve names unless
    // the requested output format changes the extension).
    const textureRemap = {};
    for (const path of images) {
      const bytes = assetMap.get(path);
      // Name as the USD most likely references it (relative to the USD's dir).
      let assetName = (rootDir && path.startsWith(rootDir)) ? path.slice(rootDir.length) : path;

      stats.textures++;
      let outBytes = bytes;

      const fmtInfo = outputFormatForImage(path, textureFormat);
      const wantResize = (opts.maxTextureSize || 0) > 0;
      let processed = null;
      if (typeof opts.textureProcessor === 'function') {
        try {
          processed = await opts.textureProcessor({
            path,
            name: assetName,
            data: bytes,
            maxTextureSize: opts.maxTextureSize || 0,
            reencode: opts.reencode,
            textureFormat,
            jpegQuality: opts.jpegQuality || 90,
            log,
          });
        } catch (err) {
          log(`  ${assetName}: browser texture processing failed (${err && err.message ? err.message : err}); trying WASM`);
        }
      }

      if (processed && processed.data) {
        outBytes = new Uint8Array(processed.data);
        if (processed.name && processed.name !== assetName) {
          textureRemap[assetName] = processed.name;
          assetName = processed.name;
        } else if (processed.ext) {
          const newName = replaceExt(assetName, processed.ext);
          if (newName !== assetName) textureRemap[assetName] = newName;
          assetName = newName;
        }
        if (processed.resized) stats.resized++;
        if (processed.reencoded || outBytes !== bytes) stats.reencoded++;
        log(`  ${assetName}: ${bytes.length} -> ${outBytes.length} bytes [browser]`);
      } else if (fmtInfo.format && (wantResize || opts.reencode || textureFormat !== 'keep')) {
        const res = native.convertImage(bytes, {
          maxSize: opts.maxTextureSize || 0,
          format: fmtInfo.format,
          pngEncoder: opts.pngEncoder || 'auto',
          jpegQuality: opts.jpegQuality || 90,
        });
        if (res && res.success) {
          outBytes = new Uint8Array(res.data); // copy out of wasm heap
          if (fmtInfo.ext) {
            const newName = replaceExt(assetName, fmtInfo.ext);
            if (newName !== assetName) textureRemap[assetName] = newName;
            assetName = newName;
          }
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

    await registerPassthroughAssets(usd);
    loadRootLayer(usd);

    const data = exportUSDZ(usd, textureRemap, opts);
    if (!data) throw new Error('USDZ export failed: ' + usd.error());
    const usdz = new Uint8Array(data); // copy out of wasm heap

    return { usdz, stats };
  } finally {
    usd.delete();
  }
}
