// Simple image loader
// supported file format: PNG(use fpng), JPEG(use stb_image), OpenEXR(use tinyexr), TIFF(use tinydng)  
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "tinyusdz.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace image {

nonstd::expected<Image, std::string> LoadImage(const std::string &filename);

} // namespace image
} // namespace tinyusdz
