#if defined(TINYUSDZ_SUPPORT_EXR)
#include "external/tinyexr.h"
#endif

#if defined(TINYUSDZ_SUPPORT_TIFF)
#include "external/tinydng.h"
#endif

#ifndef TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/stb_image_write.h"
#include "external/fpng.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "image-writer.hh"

namespace tinyusdz {
namespace image {

namespace {

}

nonstd::expected<bool, std::string> WriteImageToFile(
    const std::string &filename, const Image &image,
    WriteOption option) {

  (void)filename;
  (void)image;
  (void)option;

  return nonstd::make_unexpected("TODO: Implement WriteImageToFile");
}

///
/// @param[in] image Image data
/// @param[in] option Image write option(optional)
/// @return Serialized image data
///
nonstd::expected<std::vector<uint8_t>, std::string> WriteImageToMemory(
    const Image &image, const WriteOption option)
{
  (void)image;
  (void)option;
  
  return nonstd::make_unexpected("TODO: Implement WriteImageToFile");
}
 
} // namespace image
} // namespace tinyusdz
