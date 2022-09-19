// Simple image loader
// supported file format: PNG(use fpng), JPEG(use stb_image), OpenEXR(use tinyexr), TIFF(use tinydng)  
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "image-types.hh"

#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace image {

struct ImageResult {
  Image image;
  std::string warning;
};

struct ImageInfoResult {
  uint32_t width;
  uint32_t height;
  uint32_t channels;
  std::string warning;
};

///
/// Load image from a file.
/// 
/// @param[in] filename Input filename(or URI)
/// @return ImageResult or error message(std::string)
///
nonstd::expected<ImageResult, std::string> LoadImageFromFile(const std::string &filename);

///
/// Get Image info from file.
/// 
/// @param[in] filename Input filename(or URI)
/// @return ImageInfoResult or error message(std::string)
///
nonstd::expected<ImageResult, std::string> GetImageInfoFromFile(const std::string &filename);

///
/// Load image from memory
///
/// @param[in] addr Memory address
/// @param[in] datasize Data size(in bytes)
/// @param[in] uri Input URI(or filename) as a hint. This is used only in error message.
/// @return ImageResult or error message(std::string)
///
nonstd::expected<ImageResult, std::string> LoadImageFromMemory(const uint8_t *addr, const size_t datasize, const std::string &uri);

///
/// Get Image info from a file.
///
/// @param[in] addr Memory address
/// @param[in] datasize Data size(in bytes)
/// @param[in] uri Input URI(or filename) as a hint. This is used only in error message.
/// @return ImageResult or error message(std::string)
///
nonstd::expected<ImageInfoResult, std::string> GetImageInfoFromMemory(const uint8_t *addr, const size_t datasize, const std::string &uri);

} // namespace image
} // namespace tinyusdz
