// Simple image loader
// supported file format: PNG(use fpng), JPEG(use stb_image), OpenEXR(use tinyexr), TIFF(use tinydng)  
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "tinyusdz.hh"

#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace image {

struct ImageResult {
  Image image;
  std::string warning;
};

///
/// @param[in] filename Input filename(or URI)
/// @return ImageResult or error message(std::string)
///
nonstd::expected<ImageResult, std::string> LoadImageFromFile(const std::string &filename);

///
/// @param[in] addr Memory address
/// @param[in] datasize Data size(in bytes)
/// @param[in] uri Input URI(or filename)
/// @return ImageResult or error message(std::string)
///
nonstd::expected<ImageResult, std::string> LoadImageFromMemory(const uint8_t *addr, const size_t datasize, const std::string &uri);

} // namespace image
} // namespace tinyusdz
