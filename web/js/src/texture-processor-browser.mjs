// Browser-side JS texture pipeline for convertFolderToUSDZ's textureProcessor
// hook: decode via createImageBitmap, resize via OffscreenCanvas drawImage
// (engine codecs + GPU-accelerated raster when the browser has a GPU), encode
// via OffscreenCanvas.convertToBlob('image/png'|'image/jpeg'). PNG/JPEG/WebP
// inputs are handled; anything the engine can't decode returns null so the
// caller falls back to the WASM convertImage path.
//
// Caveats vs the WASM/native pipeline:
// - canvas 2d compositing is premultiplied-alpha; fully-transparent texels
//   lose their RGB (matters for some packed data maps with alpha).
// - drawImage resampling is engine bilinear/trilinear in canvas color space,
//   not the gamma-aware/linear split the native path does per colorspace.
//
//   const tp = createBrowserTextureProcessor({ concurrency: 8 });
//   await convertFolderToUSDZ(native, map, { textureProcessor: tp.processor,
//                                            textureConcurrency: tp.concurrency, ... });
//   tp.stats() -> { processed, decodeMs, rasterMs, encodeMs, inBytes, outBytes }

function sniffMime(bytes) {
  if (bytes.length >= 8 && bytes[0] === 0x89 && bytes[1] === 0x50) return 'image/png';
  if (bytes.length >= 3 && bytes[0] === 0xff && bytes[1] === 0xd8) return 'image/jpeg';
  if (bytes.length >= 12 && bytes[8] === 0x57 && bytes[9] === 0x45 &&
      bytes[10] === 0x42 && bytes[11] === 0x50) return 'image/webp';
  return null;
}

export function createBrowserTextureProcessor(opts = {}) {
  const concurrency = Math.max(1, opts.concurrency || 8);
  const stats = {
    processed: 0, skipped: 0,
    decodeMs: 0, rasterMs: 0, encodeMs: 0,
    inBytes: 0, outBytes: 0,
  };

  async function processor({ data, maxTextureSize, reencode, textureFormat,
                             jpegQuality }) {
    const srcMime = sniffMime(data);
    if (!srcMime) { stats.skipped++; return null; }

    const fmt = textureFormat || 'keep';
    let outMime;
    if (fmt === 'png') outMime = 'image/png';
    else if (fmt === 'jpeg') outMime = 'image/jpeg';
    else if (fmt === 'keep') outMime = (srcMime === 'image/webp') ? 'image/png' : srcMime;
    else { stats.skipped++; return null; }

    let t = performance.now();
    let bmp;
    try {
      bmp = await createImageBitmap(new Blob([data], { type: srcMime }), {
        premultiplyAlpha: 'none',
        colorSpaceConversion: 'none',
      });
    } catch (_) {
      stats.skipped++;
      return null;  // engine can't decode it -> WASM fallback
    }
    stats.decodeMs += performance.now() - t;

    const maxEdge = Math.max(bmp.width, bmp.height);
    const needResize = (maxTextureSize || 0) > 0 && maxEdge > maxTextureSize;
    if (!needResize && !reencode && outMime === srcMime) {
      bmp.close();
      stats.skipped++;
      return null;  // upstream passthrough
    }
    const scale = needResize ? maxTextureSize / maxEdge : 1;
    const w = Math.max(1, Math.round(bmp.width * scale));
    const h = Math.max(1, Math.round(bmp.height * scale));

    t = performance.now();
    const canvas = new OffscreenCanvas(w, h);
    const ctx = canvas.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(bmp, 0, 0, w, h);
    bmp.close();
    stats.rasterMs += performance.now() - t;

    t = performance.now();
    const blob = await canvas.convertToBlob(
        outMime === 'image/jpeg'
            ? { type: outMime, quality: (jpegQuality || 90) / 100 }
            : { type: outMime });
    const out = new Uint8Array(await blob.arrayBuffer());
    stats.encodeMs += performance.now() - t;

    stats.processed++;
    stats.inBytes += data.length;
    stats.outBytes += out.length;
    return {
      data: out,
      ext: outMime === 'image/jpeg' ? 'jpg' : 'png',
      resized: needResize,
      reencoded: true,
    };
  }

  return { processor, concurrency, stats: () => ({ ...stats }) };
}
