// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — image decoding seam.
//
// One tiny wrapper so the loader never includes the UI library, and so the
// decoder can be swapped without touching the loader. Currently backed by
// lightui's vendored wuffs decoder (PNG/JPEG/GIF/BMP/TGA), which is already
// linked for the UI and needs no extra dependency.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tusdql {

struct DecodedImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4, tightly packed
  bool valid() const {
    return width > 0 && height > 0 &&
           rgba.size() == size_t(width) * height * 4;
  }
};

// Decode `len` bytes and box-downsample so neither edge exceeds `max_dim`
// (0 = no limit). Downsampling happens inside the decoder wrapper so a 4K map
// never becomes a 64 MB allocation the caller has to hold.
bool DecodeImageToRgba(const void* data, size_t len, uint32_t max_dim,
                       DecodedImage* out);

}  // namespace tusdql
