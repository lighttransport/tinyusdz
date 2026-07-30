// SPDX-License-Identifier: Apache-2.0
#include "image_decode.hh"

#include <algorithm>

extern "C" {
#include <lightui/image.h>
#include <lightvg/surface.h>
}

namespace tusdql {

bool DecodeImageToRgba(const void* data, size_t len, uint32_t max_dim,
                       DecodedImage* out) {
  if (!data || len == 0 || !out) return false;
  if (len > size_t(INT32_MAX)) return false;

  lvg_surface_t* src = lui_image_load_mem(data, static_cast<int>(len));
  if (!src) return false;

  const uint32_t sw = static_cast<uint32_t>(src->width);
  const uint32_t sh = static_cast<uint32_t>(src->height);
  if (sw == 0 || sh == 0) {
    lvg_surface_destroy(src);
    return false;
  }

  uint32_t dw = sw;
  uint32_t dh = sh;
  if (max_dim > 0 && (sw > max_dim || sh > max_dim)) {
    const double scale = double(max_dim) / double(std::max(sw, sh));
    dw = std::max(1u, static_cast<uint32_t>(sw * scale));
    dh = std::max(1u, static_cast<uint32_t>(sh * scale));
  }

  out->width = dw;
  out->height = dh;
  out->rgba.assign(size_t(dw) * dh * 4, 0);

  // Box filter over the source footprint of each destination texel. Cheap, and
  // for a preview it beats point sampling badly on detailed maps.
  const uint32_t* px = src->pixels;
  const int stride = src->stride;
  for (uint32_t y = 0; y < dh; y++) {
    const uint32_t y0 = uint32_t(uint64_t(y) * sh / dh);
    const uint32_t y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * sh / dh));
    for (uint32_t x = 0; x < dw; x++) {
      const uint32_t x0 = uint32_t(uint64_t(x) * sw / dw);
      const uint32_t x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * sw / dw));
      uint32_t acc[4] = {0, 0, 0, 0};
      uint32_t n = 0;
      for (uint32_t sy = y0; sy < y1 && sy < sh; sy++) {
        for (uint32_t sx = x0; sx < x1 && sx < sw; sx++) {
          // lightvg surfaces are 0xAARRGGBB.
          const uint32_t c = px[size_t(sy) * size_t(stride) + sx];
          acc[0] += (c >> 16) & 0xFF;
          acc[1] += (c >> 8) & 0xFF;
          acc[2] += c & 0xFF;
          acc[3] += (c >> 24) & 0xFF;
          n++;
        }
      }
      if (n == 0) n = 1;
      uint8_t* d = out->rgba.data() + (size_t(y) * dw + x) * 4;
      d[0] = static_cast<uint8_t>(acc[0] / n);
      d[1] = static_cast<uint8_t>(acc[1] / n);
      d[2] = static_cast<uint8_t>(acc[2] / n);
      d[3] = static_cast<uint8_t>(acc[3] / n);
    }
  }

  lvg_surface_destroy(src);
  return true;
}

}  // namespace tusdql
