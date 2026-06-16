// Worker for the Node-side JS texture pipeline (see texture-processor-node.mjs).
// Handles PNG decode -> resize -> PNG encode entirely in JS + node:zlib (via
// pngjs), so textures can be processed on worker threads in parallel instead of
// serializing through the single WASM instance. Non-PNG inputs and non-PNG
// output formats are reported back as `skip` so the caller can fall back to the
// WASM convertImage path.
import { parentPort } from 'node:worker_threads';
import { PNG } from 'pngjs';

// 8-bit sRGB <-> linear LUT/curve for gamma-aware resampling of color
// textures (matches the intent of the stbir srgb path used by the WASM/native
// pipelines; normal/roughness/etc. resample linearly).
const SRGB_TO_LINEAR = new Float32Array(256);
for (let i = 0; i < 256; i++) {
  const c = i / 255;
  SRGB_TO_LINEAR[i] = (c <= 0.04045) ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}
function linearToSrgb8(v) {
  const c = (v <= 0.0031308) ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
  const x = Math.round(c * 255);
  return x < 0 ? 0 : (x > 255 ? 255 : x);
}

// Box-filter (area average) downscale of RGBA8. `srgb` selects gamma-aware
// averaging for RGB (alpha is always linear).
function resizeRGBA8(src, sw, sh, dw, dh, srgb) {
  const dst = Buffer.allocUnsafe(dw * dh * 4);
  const xr = sw / dw;
  const yr = sh / dh;
  for (let dy = 0; dy < dh; dy++) {
    const sy0 = Math.floor(dy * yr);
    const sy1 = Math.min(sh, Math.max(sy0 + 1, Math.floor((dy + 1) * yr)));
    for (let dx = 0; dx < dw; dx++) {
      const sx0 = Math.floor(dx * xr);
      const sx1 = Math.min(sw, Math.max(sx0 + 1, Math.floor((dx + 1) * xr)));
      let r = 0, g = 0, b = 0, a = 0;
      let n = 0;
      for (let sy = sy0; sy < sy1; sy++) {
        let o = (sy * sw + sx0) * 4;
        for (let sx = sx0; sx < sx1; sx++, o += 4) {
          if (srgb) {
            r += SRGB_TO_LINEAR[src[o]];
            g += SRGB_TO_LINEAR[src[o + 1]];
            b += SRGB_TO_LINEAR[src[o + 2]];
          } else {
            r += src[o];
            g += src[o + 1];
            b += src[o + 2];
          }
          a += src[o + 3];
          n++;
        }
      }
      const inv = 1 / n;
      const oo = (dy * dw + dx) * 4;
      if (srgb) {
        dst[oo] = linearToSrgb8(r * inv);
        dst[oo + 1] = linearToSrgb8(g * inv);
        dst[oo + 2] = linearToSrgb8(b * inv);
      } else {
        dst[oo] = Math.round(r * inv);
        dst[oo + 1] = Math.round(g * inv);
        dst[oo + 2] = Math.round(b * inv);
      }
      dst[oo + 3] = Math.round(a * inv);
    }
  }
  return dst;
}

function isPNG(bytes) {
  return bytes.length >= 8 &&
      bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e &&
      bytes[3] === 0x47;
}

parentPort.on('message', (msg) => {
  const { id, data, maxTextureSize, textureFormat, reencode, colorspace } = msg;
  try {
    const bytes = Buffer.from(data);
    // This JS path only covers PNG -> PNG; everything else falls back to WASM.
    const wantPng = (textureFormat === 'png' || textureFormat === 'keep');
    if (!wantPng || !isPNG(bytes)) {
      parentPort.postMessage({ id, skip: true });
      return;
    }

    const png = PNG.sync.read(bytes);  // -> RGBA8
    const maxEdge = Math.max(png.width, png.height);
    const needResize = (maxTextureSize || 0) > 0 && maxEdge > maxTextureSize;
    if (!needResize && !reencode) {
      parentPort.postMessage({ id, skip: true });  // passthrough upstream
      return;
    }

    let outW = png.width, outH = png.height, pixels = png.data;
    if (needResize) {
      const scale = maxTextureSize / maxEdge;
      outW = Math.max(1, Math.round(png.width * scale));
      outH = Math.max(1, Math.round(png.height * scale));
      pixels = resizeRGBA8(png.data, png.width, png.height, outW, outH,
                           colorspace === 'srgb');
    }

    const out = new PNG({ width: outW, height: outH });
    out.data = pixels;
    // deflateLevel 6: node's native zlib at the default compression/speed
    // trade-off; filterType 4 (Paeth) avoids the try-all-filters heuristic.
    const encoded = PNG.sync.write(out, { deflateLevel: 6, filterType: 4 });
    const buf = encoded.buffer.slice(encoded.byteOffset,
                                     encoded.byteOffset + encoded.byteLength);
    parentPort.postMessage(
        { id, data: buf, width: outW, height: outH, resized: needResize },
        [buf]);
  } catch (err) {
    parentPort.postMessage({ id, error: String(err && err.message ? err.message : err) });
  }
});
