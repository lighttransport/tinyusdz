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
    // Use the stored size: entry.data may have been freed after its local entry
    // was written (consume mode) to bound peak memory.
    const entrySize = (entry.size != null ? entry.size : entry.data.length) >>> 0;
    dv.setUint32(offset + 20, entrySize, true);
    dv.setUint32(offset + 24, entrySize, true);
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

export function buildUSDZWithNewRoot(rootName, rootData, passthroughEntries,
                                     opts = {}) {
  // consume: free each entry's bytes (and the caller's source reference) right
  // after writing its local entry, so the source arrays and the growing output
  // buffer do not both fully coexist. Lets texture-streaming repack a large
  // .usdz without holding 2× the output in memory. Safe only when the caller no
  // longer needs the passed-in entry/root bytes afterward.
  const consume = !!opts.consume;
  const encoder = new TextEncoder();
  const rootBytes = rootData instanceof Uint8Array ? rootData : new Uint8Array(rootData);
  const entries = [{
    name: rootName,
    nameBytes: encoder.encode(rootName),
    data: rootBytes,
    size: rootBytes.length,
    crc32: crc32(rootBytes),
    localHeaderOffset: 0,
    _src: null,
  }];
  for (const src of passthroughEntries) {
    const data = src.data instanceof Uint8Array ? src.data : new Uint8Array(src.data);
    entries.push({
      name: src.name,
      nameBytes: encoder.encode(src.name),
      data,
      size: data.length,
      crc32: src.crc32 >>> 0,
      localHeaderOffset: 0,
      _src: consume ? src : null,
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
    if (consume) {
      // Bytes are now copied into `out`; drop the source references so the
      // input/re-encoded buffers can be reclaimed while the rest is written.
      entry.data = null;
      if (entry._src) { entry._src.data = null; entry._src = null; }
    }
  }
  writeOffset = writeCentralDirectory(out, writeOffset, entries);
  if (writeOffset !== out.length) {
    throw new Error('Internal error: USDZ output size mismatch.');
  }
  return out;
}

// Incremental STORE-only USDZ writer. addEntry() emits one 64-byte-aligned local
// entry to `sink` immediately (so the caller can release the bytes right after),
// and finalize() appends the central directory + EOCD. Only one entry's bytes are
// held at a time, so a large texture-streamed archive can be written to disk
// without the whole output (and all its source entries) coexisting in memory.
export class ZipStreamWriter {
  constructor(sink) {
    // sink is either a function (bytes)=>void that appends to the output, or an
    // object { write(bytes), patch(pos, bytes) }. A patch-capable (seekable)
    // sink additionally enables addEntryStreaming() — the local header's CRC and
    // sizes are written as placeholders, then patched after the streamed data.
    if (typeof sink === 'function') { this._write = sink; this._patch = null; }
    else { this._write = sink.write; this._patch = sink.patch || null; }
    this.offset = 0;
    this.entries = [];  // { nameBytes, crc32, size, localHeaderOffset }
    this._enc = new TextEncoder();
  }
  _emit(bytes) { this._write(bytes); this.offset += bytes.length; }
  addEntry(name, data) {
    const u8 = data instanceof Uint8Array ? data : new Uint8Array(data);
    assertZip32Size(name, u8.length);
    const nameBytes = this._enc.encode(name);
    if (nameBytes.length > 0xffff) {
      throw new Error(`USDZ entry name is too long: "${name}".`);
    }
    const padding = zipPaddingForDataOffset(this.offset, nameBytes.length);
    if (padding > 0xffff) {
      throw new Error(`USDZ alignment padding is too large for "${name}".`);
    }
    const localHeaderOffset = this.offset;
    if (localHeaderOffset > 0xffffffff) {
      throw new Error('USDZ archive exceeds ZIP32 size limit.');
    }
    const crc = crc32(u8) >>> 0;
    const head = new Uint8Array(ZIP_LOCAL_HEADER_SIZE + nameBytes.length + padding);
    const dv = new DataView(head.buffer);
    dv.setUint32(0, 0x04034b50, true);
    dv.setUint16(4, 20, true);
    dv.setUint32(14, crc, true);
    dv.setUint32(18, u8.length >>> 0, true);
    dv.setUint32(22, u8.length >>> 0, true);
    dv.setUint16(26, nameBytes.length, true);
    dv.setUint16(28, padding, true);
    head.set(nameBytes, ZIP_LOCAL_HEADER_SIZE);
    this._emit(head);
    this._emit(u8);
    if (this.offset > 0xffffffff) {
      throw new Error('USDZ archive exceeds ZIP32 size limit.');
    }
    this.entries.push({ nameBytes, crc32: crc, size: u8.length, localHeaderOffset });
  }
  // Stream one entry whose bytes are produced incrementally by streamFn(emit),
  // where emit(chunk) appends a Uint8Array. Used to write a large root layer
  // straight from the WASM writer without ever holding it in JS. The local
  // header's CRC/size are emitted as placeholders then patched after the data,
  // so this requires a patch-capable (seekable) sink. Output is byte-identical
  // to addEntry() with the same bytes. `emit`-supplied chunks are consumed
  // synchronously (copied into the CRC + the sink), so transient WASM-heap views
  // are safe.
  addEntryStreaming(name, streamFn) {
    if (!this._patch) {
      throw new Error('addEntryStreaming requires a patch-capable sink');
    }
    const nameBytes = this._enc.encode(name);
    if (nameBytes.length > 0xffff) {
      throw new Error(`USDZ entry name is too long: "${name}".`);
    }
    const padding = zipPaddingForDataOffset(this.offset, nameBytes.length);
    if (padding > 0xffff) {
      throw new Error(`USDZ alignment padding is too large for "${name}".`);
    }
    const localHeaderOffset = this.offset;
    if (localHeaderOffset > 0xffffffff) {
      throw new Error('USDZ archive exceeds ZIP32 size limit.');
    }
    // Local header with CRC/sizes left as 0 (patched after the data is streamed).
    const head = new Uint8Array(ZIP_LOCAL_HEADER_SIZE + nameBytes.length + padding);
    const dv = new DataView(head.buffer);
    dv.setUint32(0, 0x04034b50, true);
    dv.setUint16(4, 20, true);
    dv.setUint16(26, nameBytes.length, true);
    dv.setUint16(28, padding, true);
    head.set(nameBytes, ZIP_LOCAL_HEADER_SIZE);
    this._emit(head);

    const table = crc32Table();
    let crc = 0xffffffff;
    let size = 0;
    streamFn((chunk) => {
      const c = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);
      for (let i = 0; i < c.length; i++) {
        crc = table[(crc ^ c[i]) & 0xff] ^ (crc >>> 8);
      }
      size += c.length;
      this._emit(c);  // copies synchronously (fs write / append)
    });
    crc = (crc ^ 0xffffffff) >>> 0;
    assertZip32Size(name, size);
    if (this.offset > 0xffffffff) {
      throw new Error('USDZ archive exceeds ZIP32 size limit.');
    }
    // Patch CRC32 (+14), compressed size (+18), uncompressed size (+22).
    const patch = new Uint8Array(12);
    const pdv = new DataView(patch.buffer);
    pdv.setUint32(0, crc, true);
    pdv.setUint32(4, size >>> 0, true);
    pdv.setUint32(8, size >>> 0, true);
    this._patch(localHeaderOffset + 14, patch);

    this.entries.push({ nameBytes, crc32: crc, size, localHeaderOffset });
  }
  finalize() {
    const cdOffset = this.offset;
    for (const e of this.entries) {
      const hdr = new Uint8Array(ZIP_CENTRAL_DIR_HEADER_SIZE + e.nameBytes.length);
      const dv = new DataView(hdr.buffer);
      dv.setUint32(0, 0x02014b50, true);
      dv.setUint16(4, 20, true);
      dv.setUint16(6, 20, true);
      dv.setUint32(16, e.crc32 >>> 0, true);
      dv.setUint32(20, e.size >>> 0, true);
      dv.setUint32(24, e.size >>> 0, true);
      dv.setUint16(28, e.nameBytes.length, true);
      dv.setUint32(42, e.localHeaderOffset >>> 0, true);
      hdr.set(e.nameBytes, ZIP_CENTRAL_DIR_HEADER_SIZE);
      this._emit(hdr);
    }
    const cdSize = this.offset - cdOffset;
    if (this.entries.length > 0xffff || cdOffset > 0xffffffff ||
        cdSize > 0xffffffff) {
      throw new Error('USDZ central directory exceeds ZIP32 limits.');
    }
    const eocd = new Uint8Array(22);
    const dv = new DataView(eocd.buffer);
    dv.setUint32(0, 0x06054b50, true);
    dv.setUint16(8, this.entries.length, true);
    dv.setUint16(10, this.entries.length, true);
    dv.setUint32(12, cdSize >>> 0, true);
    dv.setUint32(16, cdOffset >>> 0, true);
    this._emit(eocd);
  }
}

// Repack a flattened root + the archive's non-root entries into a USDZ. Images
// are re-encoded/resized one at a time (keep format only) when requested, else
// passed through. If opts.zipSink is given the archive is streamed straight to
// it (one entry held at a time — the lowest-peak path); otherwise it is built in
// memory and returned. Shared by the legacy texture-streaming path and the next
// low-memory pipeline. Returns { usdz, streamedToSink, textures, resized,
// reencoded, audio, otherAssets }.
// Build a per-texture resize-colorspace picker(name)->('srgb'|'linear'|'').
// With resizeColorspace:'auto' it reads each UsdUVTexture's authored
// sourceColorSpace from the root layer (basename-keyed): 'sRGB' -> 'srgb'
// (linear-light resample), everything else -> 'linear' (gamma-space, safe for
// data maps). Non-'auto' returns the global value for every texture.
function makeResizeCsPicker(native, rootBytes, opts) {
  const want = String((opts && opts.resizeColorspace) || '');
  if (want.toLowerCase() !== 'auto') return () => want;
  let csMap = null;
  if ((opts.maxTextureSize || 0) > 0 && rootBytes &&
      typeof native.getTextureColorspaceMap === 'function') {
    try { csMap = native.getTextureColorspaceMap(rootBytes); } catch (e) { csMap = null; }
  }
  const base = (n) => { const s = n.lastIndexOf('/'); return s < 0 ? n : n.slice(s + 1); };
  return (name) => {
    const cs = csMap ? csMap[base(name)] : undefined;
    return (cs === 'sRGB' || cs === 'srgb') ? 'srgb' : 'linear';
  };
}

function repackUSDZEntries(native, rootName, rootData, archiveEntries, rootEntry,
                           opts, log) {
  const wantResize = (opts.maxTextureSize || 0) > 0;
  const keepFmt = normalizedTextureFormat(opts.textureFormat) === 'keep';
  // Re-encode only for keep format (format conversion would need in-layer asset
  // remap, which this path does not do — those textures pass through unchanged).
  const doReencode = (wantResize || opts.reencode === true) && keepFmt;
  // Re-compressing a JPEG (decode->encode) is LOSSY and would decode the whole
  // image into the WASM heap; with no resize and no explicit quality reduction
  // it gains nothing, so JPEGs pass through losslessly. (PNG re-encodes via the
  // streaming transcoder in convertImage.) Only a resize or an explicit lower
  // --jpeg-quality re-encodes a JPEG.
  const jpegRecompress = (opts.jpegQuality || 90) < 90;
  // Role-aware resize colorspace: with resizeColorspace:'auto', read each
  // texture's authored UsdUVTexture sourceColorSpace from the root layer once.
  // 'sRGB' textures resample in linear light; everything else (raw/auto/unknown)
  // stays gamma-space — safe for linear data maps (normal/ORM/height).
  const roleAware = String(opts.resizeColorspace || '').toLowerCase() === 'auto';
  const resizeCsFor = makeResizeCsPicker(native, rootData, opts);
  let reencoded = 0, resized = 0, textures = 0, audio = 0, otherAssets = 0;
  const tally = (name) => {
    if (isImageName(name)) textures++;
    else if (isAudioName(name)) audio++;
    else if (!isUsdName(name)) otherAssets++;
  };
  const produce = (e) => {
    if (isImageName(e.name) && doReencode) {
      const fmtInfo = outputFormatForImage(e.name, 'keep');
      if (fmtInfo.format === 'jpeg' && !wantResize && !jpegRecompress) {
        log(`  ${e.name}: ${e.data.length} bytes [jpeg passthrough — lossless]`);
        return e.data;
      }
      if (fmtInfo.format) {
        const res = native.convertImage(e.data, {
          maxSize: opts.maxTextureSize || 0,
          format: fmtInfo.format,
          pngEncoder: opts.pngEncoder || 'auto',
          jpegQuality: opts.jpegQuality || 90,
          resizeColorspace: resizeCsFor(e.name),
        });
        if (res && res.success) {
          reencoded++;
          if (res.resized) resized++;
          log(`  ${e.name}: ${e.data.length} -> ${res.data.length} bytes` +
              (res.resized ? ` [resized ${res.width}x${res.height}` +
                 (roleAware ? ` ${resizeCsFor(e.name)}]` : ']') : ' [reencoded]'));
          return new Uint8Array(res.data);
        }
        log(`  ${e.name}: convertImage failed (${res && res.error}); passthrough`);
      }
    }
    return e.data;
  };

  // opts.zipSink may be a function (append-only) or an object { write, patch }.
  // rootData may be a Uint8Array, or { stream: fn } to stream the root entry
  // straight from the WASM writer (requires a patch-capable sink).
  const sink = opts.zipSink || null;
  let usdz = null;
  if (sink) {
    const zw = new ZipStreamWriter(sink);
    if (rootData && typeof rootData === 'object' && typeof rootData.stream === 'function') {
      zw.addEntryStreaming(rootName, rootData.stream);
    } else {
      zw.addEntry(rootName, rootData);
    }
    rootData = null;
    for (const e of archiveEntries) {
      if (e === rootEntry) continue;
      let data = produce(e);
      zw.addEntry(e.name, data);
      tally(e.name);
      data = null;
      e.data = null;  // release the input slice reference as we go
    }
    zw.finalize();
  } else {
    const outEntries = [];
    for (const e of archiveEntries) {
      if (e === rootEntry) continue;
      outEntries.push({ name: e.name, data: produce(e) });
      tally(e.name);
    }
    usdz = buildUSDZWithNewRoot(rootName, rootData, outEntries, { consume: true });
  }
  return { usdz, streamedToSink: !!sink, textures, resized, reencoded, audio,
           otherAssets };
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
  // The keep-textures root crate is geometry-only (textures pass through), so it
  // is usually <= the input .usdz. Start at 1.5x and let the too-small retry
  // (exportUSDCOutsideWasmHeap doubles) handle the rare inflating scene — much
  // tighter than the old 3x, which over-allocated ~600 MB for big scenes.
  const multiplier = Number(opts.lowHeapRootSizeMultiplier || 1.5);
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
  // 'layer': write the composed Layer's PrimSpecs (faithful; used for both the
  //          composeToFixedPoint and the C++ flattenLayer() flatten).
  // 'stage': typed-Prim Stage reconstruction (ARKit-style; heaviest).
  const bufMethod = kind === 'stage'
    ? 'exportStageAsUSDCToBufferWithOptions'
    : 'exportLayerAsUSDCToBufferWithOptions';

  if (typeof usd[bufMethod] !== 'function') {
    // Fallback: in-heap export (defeats the low-heap intent, but keeps working
    // on older glue builds).
    log(`WARN: WASM module lacks ${bufMethod}; falling back to heap-copy export.`);
    const exported = kind === 'stage'
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

// mode: 'layer' (composed PrimSpecs), 'stage' (typed-Prim, heaviest), or
// 'flatten-layer' (C++ flatten then write Layer — lightest faithful path).
async function convertSingleUSDZToLowHeapFlattenedUSDZ(native, rootPath, bytes,
                                                       opts, log, mode = 'layer') {
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
    // 'stage'/'flatten-layer' are the ARKit-style flatten paths.
    arkitCompatible: mode !== 'layer',
    lowHeapFlatten: true,
    lowHeapStage: mode === 'stage',
    lowHeapFlattenLayer: mode === 'flatten-layer',
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

    log(`Low-heap ${mode} flatten; inner root layer: ${rootEntry.name}`);
    if (!usd.loadAsLayerFromBinary(rootEntry.data, rootEntry.name.split('/').pop())) {
      throw new Error('Failed to load USD: ' + usd.error());
    }
    if (typeof usd.warn === 'function' && usd.warn()) log('WARN: ' + usd.warn());
    // Flatten step. 'flatten-layer' uses the single C++ flattenLayer() call
    // (lightest); the others compose at the layer level via composeToFixedPoint.
    if (mode === 'flatten-layer' && typeof usd.flattenLayer === 'function') {
      if (!usd.flattenLayer()) throw new Error('Layer flatten failed: ' + usd.error());
    } else {
      composeToFixedPoint(usd);
    }

    // Both 'layer' and 'flatten-layer' write the composed Layer (retriable);
    // only 'stage' writes the typed Stage.
    const writeKind = mode === 'stage' ? 'stage' : 'layer';
    rootUSDC = exportUSDCOutsideWasmHeap(usd, bytes.length, opts, log, writeKind);
  } finally {
    usd.delete();
  }

  const passthroughEntries = archiveEntries.filter((entry) => entry !== rootEntry);
  const usdz = buildUSDZWithNewRoot('root.usdc', rootUSDC, passthroughEntries);
  log(`Low-heap flattened USDZ wrote root.usdc (${rootUSDC.length} bytes) ` +
      `and copied ${passthroughEntries.length} entr${passthroughEntries.length === 1 ? 'y' : 'ies'}`);
  return { usdz, stats };
}

// Allocate a zero-copy input buffer and stream `usdcBytes` into the WASM heap
// (chunked, single copy). Returns the buffer uuid, or null if the allocation
// declined (e.g. exceeds the single-buffer cap) so the caller can fall back.
function nextAllocAndFill(native, usd, usdcBytes, maxBufferBytes, log) {
  const size = usdcBytes.length;
  // 0 -> wasm-side 512 MiB default; a geometry-heavy root USDC larger than that
  // raises the cap via --max-mem-mb so it streams instead of falling back.
  const info = usd.allocateZeroCopyBuffer('__next_input__', size, maxBufferBytes || 0);
  if (!info || !info.success) {
    log(`next: zero-copy alloc declined (${info && info.error}); falling back.`);
    return null;
  }
  // Re-grab HEAPU8 AFTER the allocation (it may have grown/detached the view).
  const ptr = Number(info.bufferPtr);
  const CHUNK = 16 * 1024 * 1024;
  for (let off = 0; off < size; off += CHUNK) {
    const end = Math.min(off + CHUNK, size);
    native.HEAPU8.set(usdcBytes.subarray(off, end), ptr + off);
  }
  return info.uuid;
}

// Stream a USDC buffer into the WASM heap (chunked, single copy) and flatten it
// with the next low-memory lazy-ValueRep pipeline. Returns
// { data: Uint8Array, stats } or null if the next pipeline is unavailable or
// declines (so the caller can fall back to the legacy path). `native` is the
// Emscripten Module (exposes HEAPU8); `usd` is a TinyUSDZLoaderNative instance.
export function nextFlattenViaStreaming(native, usd, usdcBytes, log = () => {}, lazy = true,
                                        maxBufferBytes = 0) {
  if (typeof usd.nextFlattenBuffer !== 'function' ||
      typeof usd.allocateZeroCopyBuffer !== 'function') {
    return null;  // old wasm without the next pipeline
  }
  const uuid = nextAllocAndFill(native, usd, usdcBytes, maxBufferBytes, log);
  if (uuid === null) return null;
  const res = usd.nextFlattenBuffer(uuid, lazy);
  if (!res || !res.success) {
    log('next flatten failed: ' + (res && res.error));
    return null;
  }
  return { data: new Uint8Array(res.data), stats: res };
}

// Gated conversion path: flatten a single-.usdz (USDC root) with the next
// low-memory pipeline, then repack the flattened root with the original
// (unchanged) texture/audio entries. Returns { usdz, stats } or null to fall
// back to the legacy path. The next reader handles USDC only and flattens the
// single root layer (no sublayer/reference expansion yet), so non-USDC or
// nested/sublayered roots decline.
async function convertSingleUSDZToNextLowMemUSDZ(native, bytes, opts, log) {
  const archiveEntries = parseUSDZEntries(bytes);
  const rootEntry = archiveEntries.find((entry) => isUsdName(entry.name));
  if (!rootEntry) throw new Error('USDZ archive contained no USD layer.');

  if (!/\.usdc$/i.test(rootEntry.name)) {
    log(`next pipeline: root "${rootEntry.name}" is not USDC; falling back.`);
    return null;
  }
  if (rootEntry.name.includes('/')) {
    log(`next pipeline: inner root "${rootEntry.name}" is in a subdirectory; falling back.`);
    return null;
  }

  const maxBufferBytes = Number(opts.maxMemMb || 0) > 0
    ? Number(opts.maxMemMb) * 1024 * 1024 : 0;  // 0 -> wasm 512 MiB default
  const lazy = opts.nextEager !== true;

  // Streaming-write path: the default for the next pipeline. When a patch-capable
  // (seekable) sink is available, stream the flattened root crate straight from
  // the WASM writer into the .usdz — the full output crate is never materialized
  // in the WASM heap nor copied to JS. Disable with opts.streamWrite === false
  // (CLI --no-stream-write). Without a seekable sink it transparently falls back
  // to buffering the root and repacking it. Both produce a byte-identical archive.
  const sink = opts.zipSink;
  const canStreamWrite = opts.streamWrite !== false && sink && typeof sink === 'object' &&
    typeof sink.patch === 'function' && typeof sink.write === 'function';

  const usd = new native.TinyUSDZLoaderNative();
  let r = null;
  let stats = null;
  try {
    if (canStreamWrite && typeof usd.nextFlattenBufferToSink === 'function') {
      // Allocate + fill BEFORE touching the sink so a declined alloc (cap) falls
      // back cleanly instead of corrupting a half-written archive.
      const uuid = nextAllocAndFill(native, usd, rootEntry.data, maxBufferBytes, log);
      if (uuid === null) return null;  // declined -> caller falls back to legacy
      r = repackUSDZEntries(native, 'root.usdc',
        { stream: (emit) => {
            const s = usd.nextFlattenBufferToSink(uuid, lazy, (view) => { emit(view); return true; });
            if (!s || !s.success) {
              throw new Error('next stream flatten failed: ' + (s && s.error));
            }
            stats = s;
          } },
        archiveEntries, rootEntry, opts, log);
    } else {
      const flat = nextFlattenViaStreaming(native, usd, rootEntry.data, log, lazy, maxBufferBytes);
      if (!flat) return null;
      stats = flat.stats;
      // Repack: next-flattened root + textures (re-encoded one-at-a-time when
      // requested, else passthrough), streamed to opts.zipSink when given.
      r = repackUSDZEntries(native, 'root.usdc', flat.data, archiveEntries,
                            rootEntry, opts, log);
    }
  } finally {
    usd.delete();
  }
  if (!r || !stats) return null;

  log(`next low-mem flatten: root.usdc ${stats.inputBytes} -> ${stats.outputBytes} bytes ` +
      `(passthrough=${stats.arraysPassedThrough}, reencoded=${stats.arraysReencoded})` +
      `${canStreamWrite ? ' [stream-write]' : ''}; ` +
      `repacked ${r.textures} texture(s), re-encoded ${r.reencoded}${r.streamedToSink ? ' [streamed to sink]' : ''}`);
  return {
    usdz: r.usdz,
    streamedToSink: r.streamedToSink,
    stats: {
      textures: r.textures, resized: r.resized, reencoded: r.reencoded,
      audio: r.audio, otherAssets: r.otherAssets,
      rootPath: rootEntry.name,
      rootLayerFormat: 'usdc',
      flatten: true,
      pipeline: 'next',
      arraysPassedThrough: stats.arraysPassedThrough,
      arraysReencoded: stats.arraysReencoded,
    },
  };
}

// Track B: stream textures one at a time so re-encoded images never accumulate
// in the WASM heap. Flattens the root (legacy loader/writer — robust on complex
// scenes), keeping textures OUT of WASM, then re-encodes each image singly via
// native.convertImage (keeping its format, so no asset-path remap is needed) and
// repacks in JS. Peak WASM stays ~= root layer + one image, instead of all
// re-encoded textures. Returns { usdz, stats } or null to fall back to legacy.
// Applies only to a single .usdz with a top-level USD root and textureFormat
// 'keep'; format conversions (png<->jpeg) still use the legacy path (which
// rewrites the renamed asset references inside the layer).
async function convertSingleUSDZStreamTextures(native, bytes, opts, log) {
  const archiveEntries = parseUSDZEntries(bytes);
  const rootEntry = archiveEntries.find((e) => isUsdName(e.name));
  if (!rootEntry) throw new Error('USDZ archive contained no USD layer.');
  if (rootEntry.name.includes('/')) {
    log(`stream-textures: inner root "${rootEntry.name}" in subdir; falling back.`);
    return null;
  }
  if (normalizedTextureFormat(opts.textureFormat) !== 'keep') {
    log('stream-textures: only textureFormat=keep is supported; falling back.');
    return null;
  }

  // 1) Flatten the root layer low-heap. Textures are NOT registered in WASM.
  let rootUSDC = null;
  const usd = new native.TinyUSDZLoaderNative();
  try {
    if ((opts.maxUsdcMb > 0 || opts.maxMemMb > 0) &&
        typeof usd.setUSDCExportLimitMB === 'function') {
      usd.setUSDCExportLimitMB(opts.maxUsdcMb || 0, opts.maxMemMb || 0);
    }
    // Register only dependency USD layers (sublayers/refs), never images.
    for (const e of archiveEntries) {
      if (e === rootEntry || !isUsdName(e.name) || /\.usdz$/i.test(e.name)) continue;
      usd.setAsset(e.name, e.data);
    }
    if (!usd.loadAsLayerFromBinary(rootEntry.data, rootEntry.name.split('/').pop())) {
      throw new Error('Failed to load USD: ' + usd.error());
    }
    const flatten = opts.flatten !== false || !!opts.arkitCompatible;
    if (flatten) {
      if (typeof usd.flattenLayer === 'function') {
        if (!usd.flattenLayer()) throw new Error('Layer flatten failed: ' + usd.error());
      } else {
        composeToFixedPoint(usd);
      }
    }
    rootUSDC = exportUSDCOutsideWasmHeap(usd, bytes.length, opts, log, 'layer');
  } finally {
    usd.delete();  // free the WASM-side layer before re-encoding textures
  }

  // 2) Repack: re-encode images one at a time (WASM holds one at a time) and
  //    stream straight to opts.zipSink when given, else build in memory.
  const rootLen = rootUSDC.length;
  const r = repackUSDZEntries(native, 'root.usdc', rootUSDC, archiveEntries,
                              rootEntry, opts, log);
  rootUSDC = null;
  log(`stream-textures: root.usdc ${rootLen} bytes, re-encoded ${r.reencoded}/${r.textures} textures (one at a time)${r.streamedToSink ? ' [streamed to sink]' : ''}`);
  return {
    usdz: r.usdz,
    streamedToSink: r.streamedToSink,
    stats: {
      textures: r.textures, resized: r.resized, reencoded: r.reencoded,
      audio: r.audio, otherAssets: r.otherAssets,
      rootPath: rootEntry.name, rootLayerFormat: 'usdc',
      flatten: true, streamTextures: true,
    },
  };
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

  // Experimental: next low-memory lazy-ValueRep flatten pipeline. Opt-in via
  // opts.pipeline === 'next'. Only applies to a single .usdz with a top-level
  // USDC root (keeping textures as JS passthrough); declines (and falls back to
  // the legacy paths below) otherwise. Output fidelity is still limited (the
  // next writer drops some property types), so this is not the default.
  if (opts.pipeline === 'next' && hasSingleUsdzInput) {
    try {
      const next = await convertSingleUSDZToNextLowMemUSDZ(
        native, assetMap.get(rootPath), opts, log);
      if (next) return next;
      log('next pipeline declined; falling back to the legacy flatten path.');
    } catch (e) {
      log('next pipeline error (' + (e && e.message) + '); falling back to legacy.');
    }
  }

  // Stream textures one at a time to bound WASM heap when re-encoding/resizing a
  // single .usdz (textureFormat 'keep'). This is the LOW-MEMORY default for that
  // case (the in-heap batch path holds every decoded image at once and OOMs the
  // wasm32 2 GB ceiling on large texture-heavy scenes). It is enabled
  // automatically; set opts.streamTextures === false (CLI: --no-stream-textures)
  // to force the in-heap path. Declines (falls back) for nested roots / non-keep
  // formats / custom texture processors.
  const wantTextureWork = (opts.maxTextureSize || 0) > 0 || opts.reencode === true;
  if (opts.streamTextures !== false && hasSingleUsdzInput &&
      textureFormat === 'keep' && wantTextureWork &&
      typeof opts.textureProcessor !== 'function') {
    try {
      const streamed = await convertSingleUSDZStreamTextures(
        native, assetMap.get(rootPath), opts, log);
      if (streamed) return streamed;
      log('stream-textures declined; falling back to the standard texture path.');
    } catch (e) {
      log('stream-textures error (' + (e && e.message) + '); falling back.');
    }
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

  // Low-heap ARKit-style flatten path: keeps textures (no re-encode), streams
  // the flattened root USDC into a JS buffer and repacks the zip in JS, so large
  // scenes that OOM the in-heap arkit path fit. Default to 'flatten-layer' (C++
  // Layer->Layer flatten then write Layer — no typed Stage, no layer copy:
  // lighter on the wasm heap and faithful). Set opts.lowHeapStageMode='stage'
  // to force the typed-Prim Stage reconstruction instead.
  if (shouldUseLowHeapStageFlattenedUSDZ(rootPath, assetMap, opts, textureFormat)) {
    const mode = opts.lowHeapStageMode === 'stage' ? 'stage' : 'flatten-layer';
    const lowHeap = await convertSingleUSDZToLowHeapFlattenedUSDZ(
      native, rootPath, assetMap.get(rootPath), opts, log, mode);
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
    const pickResizeCs = makeResizeCsPicker(native, assetMap.get(rootPath), opts);
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
          resizeColorspace: pickResizeCs(assetName),
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
