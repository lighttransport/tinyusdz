#if defined(TINYUSDZ_SUPPORT_EXR)
#include "external/tinyexr.h"
#endif

#ifndef TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/stb_image.h"
#include "external/fpng.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "image-loader.hh"

namespace tinyusdz {
namespace image {

namespace {

// Decode image(png, jpg, ...)
static bool DecodeImage(const uint8_t *bytes, const size_t size,
                        const std::string &uri, Image *image, std::string *warn,
                        std::string *err) {
  (void)warn;

  int w = 0, h = 0, comp = 0, req_comp = 0;

  unsigned char *data = nullptr;

  // force 32-bit textures for common Vulkan compatibility. It appears that
  // some GPU drivers do not support 24-bit images for Vulkan
  req_comp = 4;
  int bits = 8;

  // It is possible that the image we want to load is a 16bit per channel image
  // We are going to attempt to load it as 16bit per channel, and if it worked,
  // set the image data accodingly. We are casting the returned pointer into
  // unsigned char, because we are representing "bytes". But we are updating
  // the Image metadata to signal that this image uses 2 bytes (16bits) per
  // channel:
  if (stbi_is_16_bit_from_memory(bytes, int(size))) {
    data = reinterpret_cast<unsigned char *>(
        stbi_load_16_from_memory(bytes, int(size), &w, &h, &comp, req_comp));
    if (data) {
      bits = 16;
    }
  }

  // at this point, if data is still NULL, it means that the image wasn't
  // 16bit per channel, we are going to load it as a normal 8bit per channel
  // mage as we used to do:
  // if image cannot be decoded, ignore parsing and keep it by its path
  // don't break in this case
  // FIXME we should only enter this function if the image is embedded. If
  // `uri` references an image file, it should be left as it is. Image loading
  // should not be mandatory (to support other formats)
  if (!data)
    data = stbi_load_from_memory(bytes, int(size), &w, &h, &comp, req_comp);
  if (!data) {
    // NOTE: you can use `warn` instead of `err`
    if (err) {
      (*err) +=
          "Unknown image format. STB cannot decode image data for image: " +
          uri + "\".\n";
    }
    return false;
  }

  if ((w < 1) || (h < 1)) {
    stbi_image_free(data);
    if (err) {
      (*err) += "Invalid image data for image: " + uri + "\"\n";
    }
    return false;
  }

  image->width = w;
  image->height = h;
  image->channels = req_comp;
  image->bpp = bits;
  image->data.resize(static_cast<size_t>(w * h * req_comp) * size_t(bits / 8));
  std::copy(data, data + w * h * req_comp * (bits / 8), image->data.begin());
  stbi_image_free(data);

  return true;
}


} // namespace


nonstd::expected<image::ImageResult, std::string> LoadImageFromMemory(const uint8_t *addr, size_t sz, const std::string &uri) {

  image::ImageResult ret;

  std::string err;
  bool ok = DecodeImage(addr, sz, uri, &ret.image, &ret.warning, &err);
  if (!ok) {
    return nonstd::make_unexpected(err);
  }

  return ret;
}
 
} // namespace image
} // namespace tinyusdz
