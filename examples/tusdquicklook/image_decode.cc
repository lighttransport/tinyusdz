// SPDX-License-Identifier: Apache-2.0
#include "image_decode.hh"

#include <algorithm>
#include <limits>

extern "C" {
#include <lightui/image.h>
#include <lightvg/surface.h>
}

namespace tusdql {

namespace {

uint32_t U16LE(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8);
}

uint32_t U32BE(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t U32LE(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint64_t DecodedBytes(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) return 0;
  const uint64_t pixels = uint64_t(width) * uint64_t(height);
  if (pixels > std::numeric_limits<uint64_t>::max() / 4) {
    return std::numeric_limits<uint64_t>::max();
  }
  return pixels * 4;
}

// Return the uncompressed RGBA footprint when the small image headers expose
// dimensions without decoding. Unknown formats return 0 and are checked again
// after the decoder reports its actual surface size.
uint64_t HeaderDecodedBytes(const uint8_t* data, size_t len) {
  if (!data) return 0;
  if (len >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G' && data[4] == '\r' && data[5] == '\n' &&
      data[6] == 0x1a && data[7] == '\n' && U32BE(data + 12) == 0x49484452u) {
    return DecodedBytes(U32BE(data + 16), U32BE(data + 20));
  }
  if (len >= 10 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
    return DecodedBytes(U16LE(data + 6), U16LE(data + 8));
  }
  if (len >= 26 && data[0] == 'B' && data[1] == 'M') {
    const uint32_t width = U32LE(data + 18);
    const int32_t signed_height = static_cast<int32_t>(U32LE(data + 22));
    const uint32_t height = signed_height < 0
                                ? uint32_t(-int64_t(signed_height))
                                : uint32_t(signed_height);
    return DecodedBytes(width, height);
  }
  if (len >= 18 && (data[1] == 0 || data[1] == 1 || data[1] == 2) &&
      (data[2] == 1 || data[2] == 2 || data[2] == 3 || data[2] == 9 ||
       data[2] == 10 || data[2] == 11)) {
    return DecodedBytes(U16LE(data + 12), U16LE(data + 14));
  }
  if (len < 4 || data[0] != 0xff || data[1] != 0xd8) return 0;

  // JPEG dimensions are in a SOF marker. Skip APP/comment and other variable
  // length segments with bounds checks; entropy data is irrelevant here.
  size_t p = 2;
  while (p + 3 < len) {
    while (p < len && data[p] != 0xff) p++;
    while (p < len && data[p] == 0xff) p++;
    if (p >= len) break;
    const uint8_t marker = data[p++];
    if (marker == 0xd9 || marker == 0xda) break;
    if (marker >= 0xd0 && marker <= 0xd7) continue;
    if (p + 2 > len) break;
    const uint32_t segment_len = (uint32_t(data[p]) << 8) | data[p + 1];
    if (segment_len < 2 || p + segment_len > len) break;
    const bool sof = (marker >= 0xc0 && marker <= 0xc3) ||
                     (marker >= 0xc5 && marker <= 0xc7) ||
                     (marker >= 0xc9 && marker <= 0xcb) ||
                     (marker >= 0xcd && marker <= 0xcf);
    if (sof && segment_len >= 7) {
      const uint32_t height = (uint32_t(data[p + 3]) << 8) | data[p + 4];
      const uint32_t width = (uint32_t(data[p + 5]) << 8) | data[p + 6];
      return DecodedBytes(width, height);
    }
    p += segment_len;
  }
  return 0;
}

}  // namespace

bool DecodeImageToRgba(const void* data, size_t len, uint32_t max_dim,
                       DecodedImage* out, uint64_t max_decoded_bytes) {
  if (!data || len == 0 || !out) return false;
  if (len > size_t(INT32_MAX)) return false;
  out->width = 0;
  out->height = 0;
  out->rgba.clear();

  const auto* bytes = static_cast<const uint8_t*>(data);
  if (max_decoded_bytes > 0 &&
      HeaderDecodedBytes(bytes, len) > max_decoded_bytes) {
    return false;
  }

  lvg_surface_t* src = lui_image_load_mem(data, static_cast<int>(len));
  if (!src) return false;

  const uint32_t sw = static_cast<uint32_t>(src->width);
  const uint32_t sh = static_cast<uint32_t>(src->height);
  if (sw == 0 || sh == 0) {
    lvg_surface_destroy(src);
    return false;
  }
  if (max_decoded_bytes > 0 && DecodedBytes(sw, sh) > max_decoded_bytes) {
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

  const uint64_t output_bytes = DecodedBytes(dw, dh);
  if (output_bytes == std::numeric_limits<uint64_t>::max() ||
      output_bytes > std::numeric_limits<size_t>::max() ||
      (max_decoded_bytes > 0 && output_bytes > max_decoded_bytes)) {
    lvg_surface_destroy(src);
    return false;
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
      uint64_t acc[4] = {0, 0, 0, 0};
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
