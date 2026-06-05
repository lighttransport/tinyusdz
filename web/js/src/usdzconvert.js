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

const ZIP_LOCAL_HEADER_SIZE = 30;
const ZIP_CENTRAL_DIR_HEADER_SIZE = 46;
const USDZ_ALIGNMENT = 64;

function crc32Table() {
  if (crc32Table.cache) return crc32Table.cache;
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[i] = c >>> 0;
  }
  crc32Table.cache = table;
  return table;
}

function crc32(bytes) {
  const table = crc32Table();
  let c = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) {
    c = table[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

function zipPaddingForDataOffset(offset, nameLength) {
  const headerSize = ZIP_LOCAL_HEADER_SIZE + nameLength;
  const remainder = (offset + headerSize) % USDZ_ALIGNMENT;
  return remainder === 0 ? 0 : USDZ_ALIGNMENT - remainder;
}

function assertZip32Size(name, size) {
  if (size > 0xffffffff) {
    throw new Error(`USDZ entry "${name}" exceeds ZIP32 size limit.`);
  }
}

function writeAsciiName(out, offset, nameBytes) {
  out.set(nameBytes, offset);
  return offset + nameBytes.length;
}

function writeLocalEntry(out, offset, entry) {
  const dv = new DataView(out.buffer, out.byteOffset, out.byteLength);
  const nameBytes = entry.nameBytes;
  const data = entry.data;
  const padding = zipPaddingForDataOffset(offset, nameBytes.length);
  if (padding > 0xffff) {
    throw new Error(`USDZ alignment padding is too large for "${entry.name}".`);
  }
  entry.localHeaderOffset = offset;

  dv.setUint32(offset + 0, 0x04034b50, true);
  dv.setUint16(offset + 4, 20, true);
  dv.setUint16(offset + 6, 0, true);
  dv.setUint16(offset + 8, 0, true);
  dv.setUint16(offset + 10, 0, true);
  dv.setUint16(offset + 12, 0, true);
  dv.setUint32(offset + 14, entry.crc32 >>> 0, true);
  dv.setUint32(offset + 18, data.length >>> 0, true);
  dv.setUint32(offset + 22, data.length >>> 0, true);
  dv.setUint16(offset + 26, nameBytes.length, true);
  dv.setUint16(offset + 28, padding, true);
  offset += ZIP_LOCAL_HEADER_SIZE;
  offset = writeAsciiName(out, offset, nameBytes);
  offset += padding;
  if (offset % USDZ_ALIGNMENT !== 0) {
    throw new Error(`Internal error: USDZ alignment failed for "${entry.name}".`);
  }
  out.set(data, offset);
  return offset + data.length;
}

function writeCentralDirectory(out, offset, entries) {
  const dv = new DataView(out.buffer, out.byteOffset, out.byteLength);
  const cdOffset = offset;
  for (const entry of entries) {
    const nameBytes = entry.nameBytes;
    dv.setUint32(offset + 0, 0x02014b50, true);
    dv.setUint16(offset + 4, 20, true);
    dv.setUint16(offset + 6, 20, true);
    dv.setUint16(offset + 8, 0, true);
    dv.setUint16(offset + 10, 0, true);
    dv.setUint16(offset + 12, 0, true);
    dv.setUint16(offset + 14, 0, true);
    dv.setUint32(offset + 16, entry.crc32 >>> 0, true);
    dv.setUint32(offset + 20, entry.data.length >>> 0, true);
    dv.setUint32(offset + 24, entry.data.length >>> 0, true);
    dv.setUint16(offset + 28, nameBytes.length, true);
    dv.setUint16(offset + 30, 0, true);
    dv.setUint16(offset + 32, 0, true);
    dv.setUint16(offset + 34, 0, true);
    dv.setUint16(offset + 36, 0, true);
    dv.setUint32(offset + 38, 0, true);
    dv.setUint32(offset + 42, entry.localHeaderOffset >>> 0, true);
    offset += ZIP_CENTRAL_DIR_HEADER_SIZE;
    offset = writeAsciiName(out, offset, nameBytes);
  }
  const cdSize = offset - cdOffset;
  if (entries.length > 0xffff || cdOffset > 0xffffffff || cdSize > 0xffffffff) {
    throw new Error('USDZ central directory exceeds ZIP32 limits.');
  }

  dv.setUint32(offset + 0, 0x06054b50, true);
  dv.setUint16(offset + 4, 0, true);
  dv.setUint16(offset + 6, 0, true);
  dv.setUint16(offset + 8, entries.length, true);
  dv.setUint16(offset + 10, entries.length, true);
  dv.setUint32(offset + 12, cdSize >>> 0, true);
  dv.setUint32(offset + 16, cdOffset >>> 0, true);
  dv.setUint16(offset + 20, 0, true);
  return offset + 22;
}

// Parse a STORE-only USDZ/ZIP archive into ordered entry descriptors. Entry
// `data` fields are subarray views into the original archive bytes.
export function parseUSDZEntries(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (u8.length < 22) throw new Error('Not a valid USDZ/ZIP (too small).');
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const td = new TextDecoder();

  let eocd = -1;
  const minStart = Math.max(0, u8.length - 22 - 0xffff);
  for (let i = u8.length - 22; i >= minStart; i--) {
    if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error('Not a valid USDZ/ZIP (no EOCD record).');

  const count = dv.getUint16(eocd + 10, true);
  let off = dv.getUint32(eocd + 16, true);
  const entries = [];
  for (let n = 0; n < count; n++) {
    if (off + ZIP_CENTRAL_DIR_HEADER_SIZE > u8.length ||
        dv.getUint32(off, true) !== 0x02014b50) {
      throw new Error('Corrupt USDZ: bad central directory header.');
    }
    const method = dv.getUint16(off + 10, true);
    const crc = dv.getUint32(off + 16, true);
    const compSize = dv.getUint32(off + 20, true);
    const uncompSize = dv.getUint32(off + 24, true);
    const nameLen = dv.getUint16(off + 28, true);
    const extraLen = dv.getUint16(off + 30, true);
    const commentLen = dv.getUint16(off + 32, true);
    const lho = dv.getUint32(off + 42, true);
    if (off + ZIP_CENTRAL_DIR_HEADER_SIZE + nameLen + extraLen + commentLen >
        u8.length) {
      throw new Error('Corrupt USDZ: truncated central directory entry.');
    }
    const name = td.decode(u8.subarray(off + 46, off + 46 + nameLen));

    if (!name.endsWith('/')) {
      if (method !== 0) {
        throw new Error(`USDZ entry "${name}" is compressed (method ${method}); ` +
          'only STORE is supported.');
      }
      if (compSize !== uncompSize) {
        throw new Error(`USDZ entry "${name}" has mismatched compressed/uncompressed size.`);
      }
      if (lho + ZIP_LOCAL_HEADER_SIZE > u8.length ||
          dv.getUint32(lho, true) !== 0x04034b50) {
        throw new Error(`Corrupt USDZ: bad local header for "${name}".`);
      }
      const lNameLen = dv.getUint16(lho + 26, true);
      const lExtraLen = dv.getUint16(lho + 28, true);
      const dataStart = lho + ZIP_LOCAL_HEADER_SIZE + lNameLen + lExtraLen;
      const dataEnd = dataStart + compSize;
      if (dataEnd > u8.length) {
        throw new Error(`Corrupt USDZ: truncated data for "${name}".`);
      }
      if (dataStart % USDZ_ALIGNMENT !== 0) {
        throw new Error(`USDZ entry "${name}" is not 64-byte aligned.`);
      }
      entries.push({
        name,
        data: u8.subarray(dataStart, dataEnd),
        size: compSize,
        crc32: crc >>> 0,
        index: n,
      });
    }
    off += ZIP_CENTRAL_DIR_HEADER_SIZE + nameLen + extraLen + commentLen;
  }
  return entries;
}

export function buildUSDZWithNewRoot(rootName, rootData, passthroughEntries) {
  const encoder = new TextEncoder();
  const entries = [{
    name: rootName,
    nameBytes: encoder.encode(rootName),
    data: rootData instanceof Uint8Array ? rootData : new Uint8Array(rootData),
    crc32: crc32(rootData instanceof Uint8Array ? rootData : new Uint8Array(rootData)),
    localHeaderOffset: 0,
  }];
  for (const src of passthroughEntries) {
    const data = src.data instanceof Uint8Array ? src.data : new Uint8Array(src.data);
    entries.push({
      name: src.name,
      nameBytes: encoder.encode(src.name),
      data,
      crc32: src.crc32 >>> 0,
      localHeaderOffset: 0,
    });
  }

  let offset = 0;
  for (const entry of entries) {
    assertZip32Size(entry.name, entry.data.length);
    if (entry.nameBytes.length > 0xffff) {
      throw new Error(`USDZ entry name is too long: "${entry.name}".`);
    }
    const padding = zipPaddingForDataOffset(offset, entry.nameBytes.length);
    offset += ZIP_LOCAL_HEADER_SIZE + entry.nameBytes.length + padding +
      entry.data.length;
    if (offset > 0xffffffff) {
      throw new Error('USDZ archive exceeds ZIP32 size limit.');
    }
  }
  const centralDirOffset = offset;
  for (const entry of entries) {
    offset += ZIP_CENTRAL_DIR_HEADER_SIZE + entry.nameBytes.length;
  }
  const centralDirSize = offset - centralDirOffset;
  if (entries.length > 0xffff || centralDirOffset > 0xffffffff ||
      centralDirSize > 0xffffffff || offset + 22 > 0xffffffff) {
    throw new Error('USDZ central directory exceeds ZIP32 limits.');
  }
  offset += 22;

  const out = new Uint8Array(offset);
  let writeOffset = 0;
  for (const entry of entries) {
    writeOffset = writeLocalEntry(out, writeOffset, entry);
  }
  writeOffset = writeCentralDirectory(out, writeOffset, entries);
  if (writeOffset !== out.length) {
    throw new Error('Internal error: USDZ output size mismatch.');
  }
  return out;
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
export async function loadWasm(importer, options = {}) {
  const mod = await importer();
  return await mod.default(options);
}

// Unpack a USDZ archive (Uint8Array) into { entries: Map<name, Uint8Array>,
// order: string[] }. USDZ mandates STORE (no compression) and 64-byte data
// alignment, so we only walk the central directory — no inflate needed.
// Throws on a compressed entry or a malformed archive.
export function unpackUSDZ(bytes) {
  const entries = new Map();
  const order = [];
  for (const entry of parseUSDZEntries(bytes)) {
    entries.set(entry.name, entry.data);
    order.push(entry.name);
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

function shouldUseLowHeapFlattenedUSDZ(rootPath, assetMap, opts, textureFormat) {
  const rootLayerFormat =
    String(opts.rootLayerFormat || 'usdc').toLowerCase() === 'usda' ? 'usda' : 'usdc';
  return /\.usdz$/i.test(rootPath) &&
    assetMap.size === 1 &&
    assetMap.has(rootPath) &&
    opts.lowHeapFlattenUsdz !== false &&
    opts.repackUsdz !== false &&
    opts.flatten !== false &&
    opts.reencode === false &&
    textureFormat === 'keep' &&
    rootLayerFormat === 'usdc' &&
    (opts.maxTextureSize || 0) <= 0 &&
    (opts.targetTextureBytes || 0) <= 0 &&
    !opts.arkitCompatible &&
    typeof opts.textureProcessor !== 'function' &&
    typeof opts.audioProcessor !== 'function';
}

// Like shouldUseLowHeapFlattenedUSDZ, but for the STAGE (typed-Prim,
// ARKit-style flatten) path: a single self-contained .usdz, ARKit-compatible,
// textures kept (no re-encode). Streams the typed-flattened root USDC into a JS
// buffer and repacks the zip in JS, so the in-heap stage path's 2 GB wasm32 OOM
// on large scenes (texture decode + whole crate + zip in heap) is avoided.
function shouldUseLowHeapStageFlattenedUSDZ(rootPath, assetMap, opts, textureFormat) {
  return /\.usdz$/i.test(rootPath) &&
    assetMap.size === 1 &&
    assetMap.has(rootPath) &&
    !!opts.arkitCompatible &&
    opts.lowHeapStageUsdz !== false &&
    opts.lowHeapFlattenUsdz !== false &&
    opts.repackUsdz !== false &&
    opts.flatten !== false &&
    opts.reencode === false &&
    textureFormat === 'keep' &&
    (opts.maxTextureSize || 0) <= 0 &&
    (opts.targetTextureBytes || 0) <= 0 &&
    typeof opts.textureProcessor !== 'function' &&
    typeof opts.audioProcessor !== 'function';
}

function maxUSDCExportBytes(opts) {
  const configuredMb = Number(opts.maxUsdcMb || 0);
  if (configuredMb > 0) return Math.floor(configuredMb * 1024 * 1024);
  return 1024 * 1024 * 1024;
}

function initialLowHeapRootCapacity(inputBytes, opts) {
  const multiplier = Number(opts.lowHeapRootSizeMultiplier || 3);
  const minBytes = 64 * 1024 * 1024;
  const wanted = Math.max(minBytes, Math.ceil(inputBytes * Math.max(1, multiplier)));
  return Math.min(maxUSDCExportBytes(opts), wanted);
}

function isBufferTooSmallError(error) {
  return /buffer|capacity|too small|exceeds/i.test(String(error || ''));
}

// Export the composed root USDC straight into a JS buffer (outside the wasm
// heap). kind='layer' writes the composed Layer's PrimSpecs as-is (faithful);
// kind='stage' writes the typed-Prim-reconstructed Stage (the arkit/flatten
// path) — same streaming, so large typed-flatten scenes stay off the wasm heap.
function exportUSDCOutsideWasmHeap(usd, inputBytes, opts, log, kind = 'layer') {
  const isStage = kind === 'stage';
  const bufMethod = isStage
    ? 'exportStageAsUSDCToBufferWithOptions'
    : 'exportLayerAsUSDCToBufferWithOptions';

  if (typeof usd[bufMethod] !== 'function') {
    // Fallback: in-heap export (defeats the low-heap intent, but keeps working
    // on older glue builds).
    log(`WARN: WASM module lacks ${bufMethod}; falling back to heap-copy export.`);
    const exported = isStage
      ? usd.exportAsUSDC()
      : (typeof usd.exportLayerAsUSDCWithOptions === 'function'
          ? usd.exportLayerAsUSDCWithOptions({ rootLayerFormat: 'usdc' })
          : null);
    if (!exported) {
      throw new Error(`USDC ${kind} export failed: ` + usd.error());
    }
    return new Uint8Array(exported);
  }

  const maxBytes = maxUSDCExportBytes(opts);
  let capacity = initialLowHeapRootCapacity(inputBytes, opts);
  if (capacity <= 0) {
    throw new Error('USDC export buffer capacity is zero.');
  }

  for (let attempt = 0; attempt < 4; attempt++) {
    const rootBuffer = new Uint8Array(capacity);
    const result = usd[bufMethod](rootBuffer, { rootLayerFormat: 'usdc' });
    if (result && result.success) {
      const size = Number(result.size || 0);
      if (size <= 0 || size > rootBuffer.length) {
        throw new Error(`USDC ${kind} export returned invalid size ${size}.`);
      }
      return rootBuffer.subarray(0, size);
    }

    const err = (result && result.error) ? result.error : usd.error();
    if (!isBufferTooSmallError(err) || capacity >= maxBytes) {
      throw new Error(`USDC ${kind} export failed: ` + err);
    }

    const nextCapacity = Math.min(maxBytes, Math.max(capacity + 1, capacity * 2));
    log(`USDC export buffer too small (${capacity} bytes); retrying with ${nextCapacity} bytes`);
    capacity = nextCapacity;
  }

  throw new Error(`USDC ${kind} export failed after repeated buffer growth.`);
}

// Back-compat alias for the layer-only callers.
function exportLayerAsUSDCOutsideWasmHeap(usd, inputBytes, opts, log) {
  return exportUSDCOutsideWasmHeap(usd, inputBytes, opts, log, 'layer');
}

async function convertSingleUSDZToLowHeapFlattenedUSDZ(native, rootPath, bytes,
                                                       opts, log, useStage = false) {
  const archiveEntries = parseUSDZEntries(bytes);
  const rootEntry = archiveEntries.find((entry) => isUsdName(entry.name));
  if (!rootEntry) {
    throw new Error('USDZ archive contained no USD layer.');
  }

  const rootDir = rootEntry.name.includes('/')
    ? rootEntry.name.slice(0, rootEntry.name.lastIndexOf('/') + 1)
    : '';

  // The low-heap path rewrites the inner root as a top-level `root.usdc` and
  // strips the root's directory prefix when registering sublayer assets. If the
  // inner root lives in a subdirectory, that rename would re-anchor the
  // flattened root's relative asset references (textures, any remaining
  // sublayers) to the wrong archive paths. Bail out so the caller falls through
  // to the standard unpack+repack path, which keeps every entry's original full
  // path. Top-level roots (the common USDZ layout) take the low-heap path.
  if (rootDir) {
    log('Low-heap flatten skipped: inner root layer is in a subdirectory ' +
        `("${rootEntry.name}"); using standard repack to preserve asset paths.`);
    return null;
  }

  const assetNameFor = (path) =>
    (rootDir && path.startsWith(rootDir)) ? path.slice(rootDir.length) : path;

  const stats = {
    textures: archiveEntries.filter((entry) => isImageName(entry.name)).length,
    resized: 0,
    reencoded: 0,
    audio: archiveEntries.filter((entry) => isAudioName(entry.name)).length,
    otherAssets: archiveEntries.filter((entry) =>
      entry !== rootEntry && !isUsdName(entry.name) && !isImageName(entry.name) &&
      !isAudioName(entry.name)).length,
    rootPath: rootEntry.name,
    rootLayerFormat: 'usdc',
    flatten: true,
    // Stage mode is the typed-Prim reconstruction path used for ARKit packaging.
    arkitCompatible: useStage,
    lowHeapFlatten: true,
    lowHeapStage: useStage,
  };

  let rootUSDC = null;
  const usd = new native.TinyUSDZLoaderNative();
  try {
    if ((opts.maxUsdcMb > 0 || opts.maxMemMb > 0) &&
        typeof usd.setUSDCExportLimitMB === 'function') {
      usd.setUSDCExportLimitMB(opts.maxUsdcMb || 0, opts.maxMemMb || 0);
    }

    for (const entry of archiveEntries) {
      if (entry === rootEntry || !isUsdName(entry.name) || /\.usdz$/i.test(entry.name)) {
        continue;
      }
      usd.setAsset(assetNameFor(entry.name), entry.data);
    }

    log(`Low-heap ${useStage ? 'stage(typed)' : 'layer'} flatten; inner root layer: ${rootEntry.name}`);
    if (!usd.loadAsLayerFromBinary(rootEntry.data, rootEntry.name.split('/').pop())) {
      throw new Error('Failed to load USD: ' + usd.error());
    }
    if (typeof usd.warn === 'function' && usd.warn()) log('WARN: ' + usd.warn());
    composeToFixedPoint(usd);

    rootUSDC = exportUSDCOutsideWasmHeap(usd, bytes.length, opts, log,
                                         useStage ? 'stage' : 'layer');
  } finally {
    usd.delete();
  }

  const passthroughEntries = archiveEntries.filter((entry) => entry !== rootEntry);
  const usdz = buildUSDZWithNewRoot('root.usdc', rootUSDC, passthroughEntries);
  log(`Low-heap flattened USDZ wrote root.usdc (${rootUSDC.length} bytes) ` +
      `and copied ${passthroughEntries.length} entr${passthroughEntries.length === 1 ? 'y' : 'ies'}`);
  return { usdz, stats };
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
    opts.flatten === false &&
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

  if (shouldUseLowHeapFlattenedUSDZ(rootPath, assetMap, opts, textureFormat)) {
    const lowHeap = await convertSingleUSDZToLowHeapFlattenedUSDZ(
      native, rootPath, assetMap.get(rootPath), opts, log);
    if (lowHeap) {
      return lowHeap;
    }
    // Nested-root archive: fall through to the standard unpack+repack path,
    // which preserves the full asset directory layout.
  }

  // Low-heap STAGE (typed-Prim / ARKit-style flatten) path: keeps textures
  // (no re-encode), streams the typed-flattened root USDC into a JS buffer and
  // repacks the zip in JS, so large scenes that OOM the in-heap arkit path fit.
  if (shouldUseLowHeapStageFlattenedUSDZ(rootPath, assetMap, opts, textureFormat)) {
    const lowHeap = await convertSingleUSDZToLowHeapFlattenedUSDZ(
      native, rootPath, assetMap.get(rootPath), opts, log, /* useStage */ true);
    if (lowHeap) {
      return lowHeap;
    }
    // Nested-root archive: fall through to the standard (in-heap) path.
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
