// Support files
//
// - OpenEXR(through TinyEXR). 16bit and 32bit
// - HDR/RGBE (Radiance) through stb_image. float32
// - TIFF/DNG(through TinyDNG). 8bit, 16bit and 32bit
// - PNG(8bit, 16bit), Jpeg, bmp, tga, ...(through stb_image or wuffs).
//
// - [ ] Use fpng for 8bit PNG when `stb_image` is used
// - [ ] 10bit, 12bit and 14bit DNG image
// - [ ] Support LoD tile, multi-channel for TIFF image
//

#include "safe-arithmetic.hh"

#if defined(TINYUSDZ_WITH_NANOIMAGE)
extern "C" {
#include "external/nanoimage/nanoimage.h"
#include "external/nanoimage/nanoimage_jpeg.h"
#include "external/nanoimage/nanoimage_png.h"
#include "external/nanoimage/nanoimage_bmp.h"
#include "external/nanoimage/nanoimage_tga.h"
}
#endif

#if defined(TINYUSDZ_WITH_EXR)
#if defined(TINYUSDZ_EXR_V3)
// Pure-C11 tinyexr v3 C backend (default).
#include "external/tinyexr/include/exr.h"
#else
// Legacy v1 backend (deprecated; TINYUSDZ_USE_TINYEXR_V3=OFF).
#include "external/tinyexr.h"
#endif
#endif

#if defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)

#ifndef TINYUSDZ_NO_WUFFS_IMPLEMENTATION
#define WUFFS_IMPLEMENTATION

#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__BMP
//#define WUFFS_CONFIG__MODULE__GIF
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__JPEG
//#define WUFFS_CONFIG__MODULE__WBMP
#endif

#else

#if !defined( TINYUSDZ_NO_BUILTIN_IMAGE_LOADER)

// stb_image
#ifndef TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif

#endif

#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif


#if defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)

#include "external/wuffs-unsupported-snapshot.c"

#else

#if !defined( TINYUSDZ_NO_BUILTIN_IMAGE_LOADER)

// fpng, stb_image
#include "external/fpng.h"

// avoid duplicated symbols when tinyusdz is linked to an app/library whose also use stb_image.
#define STB_IMAGE_STATIC
#include "external/stb_image.h"

#endif

#endif

#if defined(TINYUSDZ_WITH_TIFF)
#ifndef TINYUSDZ_NO_TINY_DNG_LOADER_IMPLEMENTATION
#define TINY_DNG_LOADER_IMPLEMENTATION
#endif

#ifndef TINY_DNG_NO_EXCEPTION
#define TINY_DNG_NO_EXCEPTION
#endif

#ifndef TINY_DNG_LOADER_NO_STDIO
#define TINY_DNG_LOADER_NO_STDIO
#endif

// Prevent including `stb_image.h` inside of tiny_dng_loader.h
#ifndef TINY_DNG_LOADER_NO_STB_IMAGE_INCLUDE
#define TINY_DNG_LOADER_NO_STB_IMAGE_INCLUDE
#endif

#include "external/tiny_dng_loader.h"
#endif


#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstring>  // for std::memcpy
#include <cstdint>  // for SIZE_MAX

#include "image-loader.hh"
#include "io-util.hh"
#if defined(TINYUSDZ_WITH_EXR) && defined(TINYUSDZ_EXR_V3)
#include "memory-budget.hh"  // bound v3 C EXR decode allocations
#endif

namespace tinyusdz {
namespace image {

namespace {

// Safety net against image "decompression bombs": a tiny compressed file can
// declare enormous dimensions that pass each decoder's per-axis limit (e.g.
// stb's STBI_MAX_DIMENSIONS = 1<<24) yet expand to a multi-GB buffer. The
// per-decoder size math below already guards integer overflow with safe::mul*,
// but the resulting resize() would still attempt a huge allocation — and under
// -fno-exceptions an allocation failure aborts the process rather than throwing.
// We reject anything above this ceiling with a clean error instead. This is a
// ceiling, not a policy limit: legitimate textures are far smaller (a 16K RGBA8
// image is 1 GiB; fp32 would be 4 GiB).
static constexpr size_t kMaxDecodedImageBytes = size_t(2048) * 1024 * 1024;  // 2 GiB

#if defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)

bool DecodeImageWUFF(const uint8_t *bytes, const size_t size,
                    const std::string &uri, Image *image, std::string *warn,
                    std::string *err) {

  if (err) {
    (*err) = "TODO: WUFF image loader.\n";
  }

  return false;

}

bool GetImageInfoWUFF(const uint8_t *bytes, const size_t size,
                    const std::string &uri, uint32_t *width, uint32_t *height, uint32_t *channels, std::string *warn,
                    std::string *err) {
  if (err) {
    (*err) = "TODO: WUFF image loader.\n";
  }

  return false;
}


#else

#if !defined( TINYUSDZ_NO_BUILTIN_IMAGE_LOADER)

// Decode image(png, jpg, ...) using STB
// 16bit PNG is supported.
bool DecodeImageSTB(const uint8_t *bytes, const size_t size,
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
  if (!data) {
    data = stbi_load_from_memory(bytes, int(size), &w, &h, &comp, req_comp);
  }

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
  image->format = Image::PixelFormat::UInt;
  size_t count;
  if (!safe::mul3(w, h, req_comp, &count)) {
    return false;
  }
  size_t total_size;
  if (!safe::mul(count, size_t(bits / 8), &total_size)) {
    return false;
  }
  if (total_size > kMaxDecodedImageBytes) {
    stbi_image_free(data);
    if (err) {
      (*err) += "Decoded image exceeds the maximum allowed size for: " + uri + "\n";
    }
    return false;
  }
  // assign() copy-constructs directly from the source range, avoiding the
  // redundant zero-fill that resize() would do before the copy overwrites it
  // (meaningful for large decoded textures).
  image->data.assign(data, data + total_size);
  stbi_image_free(data);

  return true;
}

bool GetImageInfoSTB(const uint8_t *bytes, const size_t size,
                    const std::string &uri, uint32_t *width, uint32_t *height, uint32_t *channels, std::string *warn,
                    std::string *err) {
  (void)warn;
  (void)uri;
  (void)err;

  int w = 0, h = 0, comp = 0;

  int ret = stbi_info_from_memory(bytes, int(size), &w, &h, &comp);

  if (w < 0) w = 0;
  if (h < 0) h = 0;
  if (comp < 0) comp = 0;

  if (ret == 1) {
    if (width) { (*width) = uint32_t(w); }
    if (height) { (*height) = uint32_t(h); }
    if (channels) { (*channels) = uint32_t(comp); }
    return true;
  }

  return false;
}

// Check if the image is HDR (Radiance RGBE format)
bool IsHDRFromMemory(const uint8_t *bytes, const size_t size) {
  return stbi_is_hdr_from_memory(bytes, int(size)) != 0;
}

// Decode HDR (Radiance RGBE) image using stbi_loadf_from_memory
// Returns float32 RGBA data
bool DecodeImageHDR(const uint8_t *bytes, const size_t size,
                    const std::string &uri, Image *image, std::string *warn,
                    std::string *err) {
  (void)warn;

  int w = 0, h = 0, comp = 0;

  // Request 4 channels (RGBA) for consistency with other loaders
  float *data = stbi_loadf_from_memory(bytes, int(size), &w, &h, &comp, 4);

  if (!data) {
    if (err) {
      (*err) += "Failed to decode HDR image: " + uri + " - " + stbi_failure_reason() + "\n";
    }
    return false;
  }

  if ((w < 1) || (h < 1)) {
    stbi_image_free(data);
    if (err) {
      (*err) += "Invalid HDR image data for: " + uri + "\n";
    }
    return false;
  }

  image->width = w;
  image->height = h;
  image->channels = 4;  // Always RGBA
  image->bpp = 32;      // 32-bit float per channel
  image->format = Image::PixelFormat::Float;

  // Copy float data to image buffer
  size_t dataSize;
  if (!safe::mul3(size_t(w), size_t(h), size_t(4 * sizeof(float)), &dataSize)) {
    stbi_image_free(data);
    if (err) {
      (*err) += "Integer overflow computing HDR image data size for: " + uri + "\n";
    }
    return false;
  }
  if (dataSize > kMaxDecodedImageBytes) {
    stbi_image_free(data);
    if (err) {
      (*err) += "Decoded HDR image exceeds the maximum allowed size for: " + uri + "\n";
    }
    return false;
  }
  // assign() avoids the redundant zero-fill of resize() before the copy.
  {
    const uint8_t *src = reinterpret_cast<const uint8_t *>(data);
    image->data.assign(src, src + dataSize);
  }

  stbi_image_free(data);

  return true;
}

bool GetImageInfoHDR(const uint8_t *bytes, const size_t size,
                    const std::string &uri, uint32_t *width, uint32_t *height, uint32_t *channels, std::string *warn,
                    std::string *err) {
  (void)warn;
  (void)uri;
  (void)err;

  int w = 0, h = 0, comp = 0;

  // Use stbi_info to get HDR image dimensions
  int ret = stbi_info_from_memory(bytes, int(size), &w, &h, &comp);

  if (w < 0) w = 0;
  if (h < 0) h = 0;

  if (ret == 1) {
    if (width) { (*width) = uint32_t(w); }
    if (height) { (*height) = uint32_t(h); }
    if (channels) { (*channels) = 4; }  // Always return RGBA for HDR
    return true;
  }

  return false;
}
#endif
#endif

#if defined(TINYUSDZ_WITH_NANOIMAGE)

// Decode image (jpg, png, bmp, tga) using nanoimage.
// nanoimage is a fuzz-tested, memory-safe C image decoder.
bool DecodeImageNanoimage(const uint8_t *bytes, const size_t size,
                          const std::string &uri, Image *image,
                          std::string *warn, std::string *err) {
  (void)warn;

  char errbuf[256];
  errbuf[0] = 0;

  // JPEG: FF D8 FF
  // PNG:  89 50 4E 47
  // BMP:  42 4D
  // TGA:  no reliable magic; try last
  //
  // Try decoders in order of likelihood. Each reports its own error on mismatch.
  ni_image ni_img;
  memset(&ni_img, 0, sizeof(ni_img));
  int ok = 0;

  if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
    ok = ni_load_jpeg_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
  } else if (size >= 4 && bytes[0] == 0x89 && bytes[1] == 0x50 &&
             bytes[2] == 0x4E && bytes[3] == 0x47) {
    ok = ni_load_png_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
  } else if (size >= 2 && bytes[0] == 0x42 && bytes[1] == 0x4D) {
    ok = ni_load_bmp_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
  } else {
    // Fallback: try TGA first (no magic), then jpeg/png/bmp as last resort
    ok = ni_load_tga_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
    if (!ok) {
      ok = ni_load_png_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
    }
    if (!ok) {
      ok = ni_load_jpeg_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
    }
    if (!ok) {
      ok = ni_load_bmp_from_memory(bytes, size, &ni_img, errbuf, sizeof(errbuf));
    }
  }

  if (!ok) {
    if (err) {
      (*err) += "nanoimage cannot decode image data for: " + uri;
      if (errbuf[0]) {
        (*err) += " (" + std::string(errbuf) + ")";
      }
      (*err) += "\n";
    }
    return false;
  }

  if (ni_img.width < 1 || ni_img.height < 1) {
    ni_image_free(&ni_img);
    if (err) {
      (*err) += "Invalid image dimensions for: " + uri + "\n";
    }
    return false;
  }

  image->width = int(ni_img.width);
  image->height = int(ni_img.height);
  image->channels = int(ni_img.channels);
  image->bpp = int(ni_img.bit_depth);
  image->format = Image::PixelFormat::UInt;
  if (ni_img.data_size > kMaxDecodedImageBytes) {
    ni_image_free(&ni_img);
    if (err) {
      (*err) += "Decoded image exceeds the maximum allowed size for: " + uri + "\n";
    }
    return false;
  }
  // assign() avoids the redundant zero-fill of resize() before the copy.
  {
    const uint8_t *src = reinterpret_cast<const uint8_t *>(ni_img.data);
    image->data.assign(src, src + ni_img.data_size);
  }
  ni_image_free(&ni_img);

  return true;
}

bool GetImageInfoNanoimage(const uint8_t *bytes, const size_t size,
                           const std::string &uri, uint32_t *width,
                           uint32_t *height, uint32_t *channels,
                           std::string *warn, std::string *err) {
  // Reuse the decode path for info extraction — nanoimage has no info-only API.
  Image img;
  if (!DecodeImageNanoimage(bytes, size, uri, &img, warn, err)) {
    return false;
  }
  if (width) *width = uint32_t(img.width);
  if (height) *height = uint32_t(img.height);
  if (channels) *channels = uint32_t(img.channels);
  return true;
}

#endif

#if defined(TINYUSDZ_WITH_EXR)

#if defined(TINYUSDZ_EXR_V3)

// exr_allocator backed by a MemoryBudgetManager: bounds the total memory the
// v3 C decoder allocates (reader scratch + planar channel buffers) for a single
// decode, so a crafted EXR fails cleanly (NULL -> EXR_ERROR_OUT_OF_MEMORY)
// rather than OOM-aborting. Each block carries a 16-byte size header (the free
// callback only receives the pointer); the 16-byte offset preserves the
// 16-byte alignment malloc already provides.
struct ExrBudgetAllocator {
  tinyusdz::MemoryBudgetManager mgr;
  explicit ExrBudgetAllocator(uint64_t budget) : mgr(budget) {}

  static void *Alloc(void *user, size_t size) {
    auto *self = static_cast<ExrBudgetAllocator *>(user);
    const size_t kHdr = 16;
    if (size > SIZE_MAX - kHdr) return nullptr;
    const size_t total = size + kHdr;
    if (!self->mgr.Reserve(total)) return nullptr;
    void *base = std::malloc(total);
    if (!base) {
      self->mgr.Release(total);
      return nullptr;
    }
    std::memcpy(base, &total, sizeof(size_t));
    return static_cast<uint8_t *>(base) + kHdr;
  }
  static void Free(void *user, void *ptr) {
    if (!ptr) return;
    auto *self = static_cast<ExrBudgetAllocator *>(user);
    const size_t kHdr = 16;
    void *base = static_cast<uint8_t *>(ptr) - kHdr;
    size_t total = 0;
    std::memcpy(&total, base, sizeof(size_t));
    self->mgr.Release(total);
    std::free(base);
  }
  exr_allocator handle() {
    exr_allocator a;
    a.user = this;
    a.alloc = &ExrBudgetAllocator::Alloc;
    a.free = &ExrBudgetAllocator::Free;
    return a;
  }
};

// Locate the standard color channels (R/G/B/A and luminance Y) by name in a
// v3 exr_part's name-sorted channel list. Returns false if none are present.
static bool ExrFindRGBAY(const exr_part *part, int *idxR, int *idxG, int *idxB,
                         int *idxA, int *idxY) {
  *idxR = *idxG = *idxB = *idxA = *idxY = -1;
  for (int c = 0; c < part->header.num_channels; c++) {
    const char *n = part->header.channels[c].name;
    if (std::strcmp(n, "R") == 0) *idxR = c;
    else if (std::strcmp(n, "G") == 0) *idxG = c;
    else if (std::strcmp(n, "B") == 0) *idxB = c;
    else if (std::strcmp(n, "A") == 0) *idxA = c;
    else if (std::strcmp(n, "Y") == 0) *idxY = c;
  }
  return (*idxR >= 0 || *idxG >= 0 || *idxB >= 0 || *idxY >= 0);
}

bool DecodeImageEXR(const uint8_t *bytes, const size_t size,
                    const std::string &uri, Image *image,
                    std::string *err) {
  exr_image img;
  std::memset(&img, 0, sizeof(img));
  ExrBudgetAllocator budget(kMaxDecodedImageBytes);
  exr_allocator alloc = budget.handle();
  exr_result r = exr_load_from_memory(bytes, size, &alloc, &img);
  if (!EXR_OK(r)) {
    (*err) += "Failed to load EXR image: " + uri + " (" +
              std::string(exr_result_string(r)) + ")\n";
    return false;
  }
  if (img.num_parts < 1 || img.parts == nullptr) {
    exr_image_free(&img);
    (*err) += "EXR has no image parts: " + uri + "\n";
    return false;
  }
  const exr_part *part = &img.parts[0];
  if (part->is_deep || part->images == nullptr || part->width <= 0 ||
      part->height <= 0) {
    exr_image_free(&img);
    (*err) += "EXR part is deep or empty: " + uri + "\n";
    return false;
  }

  int idxR, idxG, idxB, idxA, idxY;
  if (!ExrFindRGBAY(part, &idxR, &idxG, &idxB, &idxA, &idxY)) {
    exr_image_free(&img);
    (*err) += "EXR has no R/G/B/Y channel: " + uri + "\n";
    return false;
  }

  size_t npix, total_size;
  if (!safe::mul(size_t(part->width), size_t(part->height), &npix) ||
      !safe::mul(npix, size_t(4 * sizeof(float)), &total_size)) {
    exr_image_free(&img);
    return false;
  }
  if (total_size > kMaxDecodedImageBytes) {
    exr_image_free(&img);
    (*err) += "Decoded EXR image exceeds the maximum allowed size for: " + uri + "\n";
    return false;
  }

  // Read channel `idx` element `p` as float, honoring its native pixel type.
  auto getf = [&](int idx, size_t p) -> float {
    if (idx < 0) return 0.0f;
    const void *base = part->images[idx];
    if (!base) return 0.0f;
    switch (part->header.channels[idx].pixel_type) {
      case EXR_PIXEL_HALF: {
        uint16_t h = reinterpret_cast<const uint16_t *>(base)[p];
        float f;
        exr_half_to_float(&h, &f, 1);
        return f;
      }
      case EXR_PIXEL_FLOAT:
        return reinterpret_cast<const float *>(base)[p];
      case EXR_PIXEL_UINT:
        return float(reinterpret_cast<const uint32_t *>(base)[p]);
    }
    return 0.0f;
  };

  image->width = part->width;
  image->height = part->height;
  image->channels = 4;  // RGBA
  image->bpp = 32;      // fp32
  image->format = Image::PixelFormat::Float;
  image->data.resize(total_size);
  float *out = reinterpret_cast<float *>(image->data.data());
  for (size_t p = 0; p < npix; p++) {
    out[p * 4 + 0] = idxR >= 0 ? getf(idxR, p) : getf(idxY, p);
    out[p * 4 + 1] = idxG >= 0 ? getf(idxG, p) : getf(idxY, p);
    out[p * 4 + 2] = idxB >= 0 ? getf(idxB, p) : getf(idxY, p);
    out[p * 4 + 3] = idxA >= 0 ? getf(idxA, p) : 1.0f;
  }

  exr_image_free(&img);
  return true;
}

#else  // legacy v1 backend

bool DecodeImageEXR(const uint8_t *bytes, const size_t size,
                    const std::string &uri, Image *image,
                    std::string *err) {
  // TODO(syoyo):
  // - [ ] Read fp16 image as fp16
  // - [ ] Read int16 image as int16
  // - [ ] Read int32 image as int32
  // - [ ] Multi-channel EXR

  float *rgba = nullptr;
  int width;
  int height;
  const char *exrerr = nullptr;
  // LoadEXRFromMemory always load EXR image as fp32 x RGBA
  int ret = LoadEXRFromMemory(&rgba, &width, &height, bytes, size, &exrerr);

  if (exrerr) {
    (*err) += std::string(exrerr);

    FreeEXRErrorMessage(exrerr);
  }

  // LoadEXRFromMemory returns TINYEXR_SUCCESS (0) on success and a negative
  // error code otherwise, so test against TINYEXR_SUCCESS (the previous `!ret`
  // inverted this and reported failure on every successful load).
  if (ret != TINYEXR_SUCCESS) {
    (*err) += "Failed to load EXR image: " + uri + "\n";
    return false;
  }
  if (rgba == nullptr) {
    (*err) += "EXR decode returned no pixel data: " + uri + "\n";
    return false;
  }

  image->width = width;
  image->height = height;
  image->channels = 4;  // RGBA
  image->bpp = 32;      // fp32
  image->format = Image::PixelFormat::Float;
  size_t count;
  if (!safe::mul3(width, height, size_t(4), &count)) {
    return false;
  }
  size_t total_size;
  if (!safe::mul(count, sizeof(float), &total_size)) {
    return false;
  }
  if (total_size > kMaxDecodedImageBytes) {
    free(rgba);
    (*err) += "Decoded EXR image exceeds the maximum allowed size for: " + uri + "\n";
    return false;
  }
  image->data.resize(total_size);
  memcpy(image->data.data(), rgba, total_size);

  free(rgba);

  return true;
}

#endif  // TINYUSDZ_EXR_V3

#endif

#if defined(TINYUSDZ_WITH_TIFF)

bool DecodeImageTIFF(const uint8_t *bytes, const size_t size,
                    const std::string &uri, Image *image,
                    std::string *err) {


  std::vector<tinydng::FieldInfo> custom_fields; // no custom fields
  std::vector<tinydng::DNGImage> images;

  std::string warn;
  std::string dngerr;

  bool ret = tinydng::LoadDNGFromMemory(reinterpret_cast<const char *>(bytes), uint32_t(size), custom_fields, &images, &warn, &dngerr);

  if (!dngerr.empty()) {
    (*err) += dngerr;
  }

  if (!ret) {
    (*err) += "Failed to load TIFF/DNG image: " + uri + "\n";
    return false;
  }

  // TODO(syoyo): Multi-layer TIFF
  // Use the largest image(based on width pixels).
  size_t largest = 0;
  int largest_width = images[0].width;
  for (size_t i = 1; i < images.size(); i++) {
    if (largest_width < images[i].width) {
      largest = i;
      largest_width = images[i].width;
     }
  }

  size_t spp = size_t(images[largest].samples_per_pixel);
  size_t bps = size_t(images[largest].bits_per_sample);

  if (spp > 4) {
    (*err) += "Samples per pixel must be 0 ~ 4, but got " + std::to_string(spp) + " for image: " + uri + "\n";
    return false;
  }

  // TODO: Support 10, 12 and 14bit Image(e.g. Apple ProRAW 12bit)
  if ((bps == 8) || (bps == 16) || (bps == 32)) {
    // ok
  } else {
    (*err) += "Invalid or unsupported bits per sample " + std::to_string(bps) + " for image: " + uri + "\n";
    return false;
  }

  auto sample_format = images[largest].sample_format;
  if (sample_format == tinydng::SAMPLEFORMAT_UINT) {
    image->format = Image::PixelFormat::UInt;
  } else if (sample_format == tinydng::SAMPLEFORMAT_INT) {
    image->format = Image::PixelFormat::Int;
  } else if (sample_format == tinydng::SAMPLEFORMAT_IEEEFP) {
    image->format = Image::PixelFormat::Float;
  } else {
    (*err) += "Invalid Sample format for image: " + uri + "\n";
    return false;
  }

  image->width = images[largest].width;
  image->height = images[largest].height;
  image->channels = int(spp);
  image->bpp = int(bps);

  image->data.swap(images[largest].data);

  return true;
}

#endif

}  // namespace

#if defined(TINYUSDZ_WITH_EXR)
#if defined(TINYUSDZ_EXR_V3)
bool DecodeImageEXRHalf(const uint8_t *bytes, size_t size,
                        const std::string &uri, Image *image,
                        std::string *err) {
  (void)uri;
  (void)err;
  exr_image img;
  std::memset(&img, 0, sizeof(img));
  ExrBudgetAllocator budget(kMaxDecodedImageBytes);
  exr_allocator alloc = budget.handle();
  if (!EXR_OK(exr_load_from_memory(bytes, size, &alloc, &img))) {
    return false;  // not EXR / unparseable -> caller falls back
  }
  // Contract: single-part scanline only; multipart/tiled/deep -> fp32 path.
  if (img.num_parts != 1 || img.parts == nullptr) {
    exr_image_free(&img);
    return false;
  }
  const exr_part *part = &img.parts[0];
  if (part->is_deep || part->header.part_type != EXR_PART_SCANLINE ||
      part->images == nullptr || part->width <= 0 || part->height <= 0) {
    exr_image_free(&img);
    return false;
  }
  // Only take the half path when EVERY channel is stored HALF in the file.
  if (part->header.num_channels <= 0) {
    exr_image_free(&img);
    return false;
  }
  for (int c = 0; c < part->header.num_channels; c++) {
    if (part->header.channels[c].pixel_type != EXR_PIXEL_HALF) {
      exr_image_free(&img);
      return false;  // float/uint channels present -> fp32 path
    }
  }

  int idxR, idxG, idxB, idxA, idxY;
  if (!ExrFindRGBAY(part, &idxR, &idxG, &idxB, &idxA, &idxY)) {
    exr_image_free(&img);
    return false;  // no standard color channel -> fp32 path
  }

  size_t npix, total;
  if (!safe::mul(size_t(part->width), size_t(part->height), &npix) ||
      !safe::mul(npix, size_t(4 * 2), &total)) {  // RGBA * 2 bytes/half
    exr_image_free(&img);
    return false;
  }
  if (total > kMaxDecodedImageBytes) {
    // Oversized: bail so the caller falls back to the fp32 path, which emits a
    // clean over-size error of its own.
    exr_image_free(&img);
    return false;
  }

  image->width = part->width;
  image->height = part->height;
  image->channels = 4;
  image->bpp = 16;
  image->format = Image::PixelFormat::Float;
  image->data.resize(total);
  uint16_t *out = reinterpret_cast<uint16_t *>(image->data.data());

  auto chan = [&](int idx) -> const uint16_t * {
    return idx >= 0 ? reinterpret_cast<const uint16_t *>(part->images[idx])
                    : nullptr;
  };
  const uint16_t *R = chan(idxR), *G = chan(idxG), *B = chan(idxB),
                 *A = chan(idxA), *Y = chan(idxY);
  const uint16_t kOne = 0x3C00;  // half 1.0
  const uint16_t kZero = 0x0000;
  for (size_t p = 0; p < npix; p++) {
    out[p * 4 + 0] = R ? R[p] : (Y ? Y[p] : kZero);
    out[p * 4 + 1] = G ? G[p] : (Y ? Y[p] : kZero);
    out[p * 4 + 2] = B ? B[p] : (Y ? Y[p] : kZero);
    out[p * 4 + 3] = A ? A[p] : kOne;
  }

  exr_image_free(&img);
  return true;
}

#else  // legacy v1 backend

bool DecodeImageEXRHalf(const uint8_t *bytes, size_t size,
                        const std::string &uri, Image *image,
                        std::string *err) {
  (void)uri;
  (void)err;
  EXRVersion version;
  if (ParseEXRVersionFromMemory(&version, bytes, size) != TINYEXR_SUCCESS) {
    return false;  // not EXR / unparseable -> caller falls back
  }
  if (version.multipart || version.tiled || version.non_image) {
    return false;  // unsupported layout -> fp32 path
  }

  EXRHeader header;
  InitEXRHeader(&header);
  const char *exrerr = nullptr;
  if (ParseEXRHeaderFromMemory(&header, &version, bytes, size, &exrerr) !=
      TINYEXR_SUCCESS) {
    if (exrerr) FreeEXRErrorMessage(exrerr);
    // ParseEXRHeader may allocate the header (ConvertHeader) before failing;
    // free it. (LoadEXRImageFromMemory, by contrast, frees its image itself on
    // failure — calling FreeEXRImage there double-frees.)
    FreeEXRHeader(&header);
    return false;
  }

  // Only take the half path when EVERY channel is HALF in the file — tinyexr
  // cannot narrow a FLOAT channel to half on load.
  bool all_half = header.num_channels > 0;
  for (int c = 0; c < header.num_channels; c++) {
    if (header.pixel_types[c] != TINYEXR_PIXELTYPE_HALF) {
      all_half = false;
      break;
    }
    header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_HALF;  // load as-is
  }
  if (!all_half) {
    FreeEXRHeader(&header);
    return false;  // float channels present -> fp32 path
  }

  EXRImage exr;
  InitEXRImage(&exr);
  if (LoadEXRImageFromMemory(&exr, &header, bytes, size, &exrerr) !=
      TINYEXR_SUCCESS) {
    if (exrerr) FreeEXRErrorMessage(exrerr);
    FreeEXRHeader(&header);
    return false;
  }
  if (exr.images == nullptr || exr.width <= 0 || exr.height <= 0) {
    FreeEXRImage(&exr);  // load succeeded -> safe to free here
    FreeEXRHeader(&header);
    return false;
  }

  // Map channel names to RGBA (EXR usually stores (A)BGR order; Y = luminance).
  int idxR = -1, idxG = -1, idxB = -1, idxA = -1, idxY = -1;
  for (int c = 0; c < header.num_channels; c++) {
    const char *n = header.channels[c].name;
    if (std::strcmp(n, "R") == 0) idxR = c;
    else if (std::strcmp(n, "G") == 0) idxG = c;
    else if (std::strcmp(n, "B") == 0) idxB = c;
    else if (std::strcmp(n, "A") == 0) idxA = c;
    else if (std::strcmp(n, "Y") == 0) idxY = c;
  }
  // No standard color channel (e.g. custom/layered names) — bail so the caller
  // falls back to the fp32 path instead of emitting a silently-black image.
  if (idxR < 0 && idxG < 0 && idxB < 0 && idxY < 0) {
    FreeEXRImage(&exr);
    FreeEXRHeader(&header);
    return false;
  }

  size_t npix, total;
  if (!safe::mul(size_t(exr.width), size_t(exr.height), &npix) ||
      !safe::mul(npix, size_t(4 * 2), &total)) {  // RGBA * 2 bytes/half
    FreeEXRImage(&exr);
    FreeEXRHeader(&header);
    return false;
  }
  if (total > kMaxDecodedImageBytes) {
    // Oversized: bail so the caller falls back to the fp32 path, which emits a
    // clean over-size error of its own.
    FreeEXRImage(&exr);
    FreeEXRHeader(&header);
    return false;
  }

  image->width = exr.width;
  image->height = exr.height;
  image->channels = 4;
  image->bpp = 16;
  image->format = Image::PixelFormat::Float;
  image->data.resize(total);
  uint16_t *out = reinterpret_cast<uint16_t *>(image->data.data());

  auto chan = [&](int idx) -> const uint16_t * {
    return idx >= 0 ? reinterpret_cast<const uint16_t *>(exr.images[idx])
                    : nullptr;
  };
  const uint16_t *R = chan(idxR), *G = chan(idxG), *B = chan(idxB),
                 *A = chan(idxA), *Y = chan(idxY);
  const uint16_t kOne = 0x3C00;  // half 1.0
  const uint16_t kZero = 0x0000;
  for (size_t p = 0; p < npix; p++) {
    out[p * 4 + 0] = R ? R[p] : (Y ? Y[p] : kZero);
    out[p * 4 + 1] = G ? G[p] : (Y ? Y[p] : kZero);
    out[p * 4 + 2] = B ? B[p] : (Y ? Y[p] : kZero);
    out[p * 4 + 3] = A ? A[p] : kOne;
  }

  FreeEXRImage(&exr);
  FreeEXRHeader(&header);
  return true;
}
#endif  // TINYUSDZ_EXR_V3
#else
bool DecodeImageEXRHalf(const uint8_t *, size_t, const std::string &, Image *,
                        std::string *) {
  return false;  // built without EXR support
}
#endif

#if defined(TINYUSDZ_WITH_EXR)
// EXR magic-number detection, backend-agnostic.
static inline bool ExrIsEXR(const uint8_t *addr, size_t sz) {
#if defined(TINYUSDZ_EXR_V3)
  return exr_is_exr_memory(addr, sz) != 0;
#else
  return TINYEXR_SUCCESS == IsEXRFromMemory(addr, sz);
#endif
}
#endif

nonstd::expected<image::ImageResult, std::string> LoadImageFromMemory(
    const uint8_t *addr, size_t sz, const std::string &uri) {
  image::ImageResult ret;
  std::string err;

#if defined(TINYUSDZ_WITH_EXR)
  if (ExrIsEXR(addr, sz)) {

    bool ok = DecodeImageEXR(addr, sz, uri, &ret.image, &err);

    if (!ok) {
      return nonstd::make_unexpected(err);
    }

    return std::move(ret);
  }
#endif

#if defined(TINYUSDZ_WITH_TIFF)
  {
    std::string msg;
    if (tinydng::IsDNGFromMemory(reinterpret_cast<const char *>(addr), uint32_t(sz), &msg)) {

      bool ok = DecodeImageTIFF(addr, sz, uri, &ret.image, &err);

      if (!ok) {
        return nonstd::make_unexpected(err);
      }

      return std::move(ret);
    }
  }
#endif

#if defined(TINYUSDZ_WITH_NANOIMAGE)
  // Try nanoimage for common formats (jpg, png, bmp, tga).
  // Fuzz-tested, memory-safe C decoder. Falls through to STB/WUFFS on failure.
  {
    bool ok = DecodeImageNanoimage(addr, sz, uri, &ret.image, &ret.warning, &err);
    if (ok) {
      return std::move(ret);
    }
    // Clear error from nanoimage attempt; STB/WUFFS will provide the final error.
    err.clear();
  }
#endif

  // HDR (Radiance RGBE) detection - must be before generic STB fallback
  // to ensure we decode as float instead of uint8
#if !defined(TINYUSDZ_NO_BUILTIN_IMAGE_LOADER) && !defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)
  if (IsHDRFromMemory(addr, sz)) {
    bool ok = DecodeImageHDR(addr, sz, uri, &ret.image, &ret.warning, &err);

    if (!ok) {
      return nonstd::make_unexpected(err);
    }

    return std::move(ret);
  }
#endif

#if defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)
  bool ok = DecodeImageWUFF(addr, sz, uri, &ret.image, &ret.warning, &err);
#elif !defined(TINYUSDZ_NO_BUILTIN_IMAGE_LOADER)
  bool ok = DecodeImageSTB(addr, sz, uri, &ret.image, &ret.warning, &err);
#else
  // TODO: Use user-supplied image loader
  (void)addr;
  (void)sz;
  (void)uri;
  bool ok = false;
  err = "Image loading feature is disabled in this build. TODO: use user-supplied image loader\n";
#endif
  if (!ok) {
    return nonstd::make_unexpected(err);
  }

  return std::move(ret);
}

nonstd::expected<image::ImageInfoResult, std::string> GetImageInfoFromMemory(
    const uint8_t *addr, size_t sz, const std::string &uri) {
  image::ImageInfoResult ret;
  std::string err;

#if defined(TINYUSDZ_WITH_EXR)
  if (ExrIsEXR(addr, sz)) {
#if defined(TINYUSDZ_EXR_V3)
    // Parse headers + offset tables only (no pixel decode) for fast info query.
    ExrBudgetAllocator budget(kMaxDecodedImageBytes);
    exr_allocator alloc = budget.handle();
    exr_reader *rd = nullptr;
    if (!EXR_OK(exr_reader_open_memory(addr, sz, &alloc, &rd))) {
      return nonstd::make_unexpected("Failed to open EXR for info: " + uri + "\n");
    }
    if (!EXR_OK(exr_reader_parse_header(rd)) ||
        exr_reader_num_parts(rd) < 1) {
      exr_reader_close(rd);
      return nonstd::make_unexpected("Failed to parse EXR header: " + uri + "\n");
    }
    const exr_header *h = exr_reader_part_header(rd, 0);
    if (!h) {
      exr_reader_close(rd);
      return nonstd::make_unexpected("EXR has no part header: " + uri + "\n");
    }
    const int64_t w =
        int64_t(h->data_window.max_x) - int64_t(h->data_window.min_x) + 1;
    const int64_t hgt =
        int64_t(h->data_window.max_y) - int64_t(h->data_window.min_y) + 1;
    const int32_t nch = h->num_channels;  // read before closing (h is reader-owned)
    exr_reader_close(rd);
    if (w <= 0 || hgt <= 0) {
      return nonstd::make_unexpected("EXR has an invalid data window: " + uri + "\n");
    }
    ret.width = uint32_t(w);
    ret.height = uint32_t(hgt);
    ret.channels = uint32_t(nch);
    return std::move(ret);
#else
    return nonstd::make_unexpected("TODO: EXR format");
#endif
  }
#endif

#if defined(TINYUSDZ_WITH_TIFF)
  if (tinydng::IsDNGFromMemory(reinterpret_cast<const char *>(addr), uint32_t(sz), &err)) {

      return nonstd::make_unexpected("TODO: TIFF/DNG format");

  }
#endif

#if defined(TINYUSDZ_WITH_NANOIMAGE)
  {
    bool ok = GetImageInfoNanoimage(addr, sz, uri, &ret.width, &ret.height,
                                    &ret.channels, &ret.warning, &err);
    if (ok) {
      return std::move(ret);
    }
    err.clear();
  }
#endif

  // HDR (Radiance RGBE) detection
#if !defined(TINYUSDZ_NO_BUILTIN_IMAGE_LOADER) && !defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)
  if (IsHDRFromMemory(addr, sz)) {
    bool ok = GetImageInfoHDR(addr, sz, uri, &ret.width, &ret.height, &ret.channels, &ret.warning, &err);
    if (!ok) {
      return nonstd::make_unexpected(err);
    }
    return std::move(ret);
  }
#endif

#if defined(TINYUSDZ_USE_WUFFS_IMAGE_LOADER)
  bool ok = GetImageInfoWUFF(addr, sz, uri, &ret.width, &ret.height, &ret.channels, &ret.warning, &err);
#elif !defined(TINYUSDZ_NO_BUILTIN_IMAGE_LOADER)
  bool ok = GetImageInfoSTB(addr, sz, uri, &ret.width, &ret.height, &ret.channels, &ret.warning, &err);
#else
  (void)addr;
  (void)sz;
  (void)uri;
  bool ok = false;
  err = "Image loading feature is disabled in this build. TODO: use user-supplied image info function\n";
#endif
  if (!ok) {
    return nonstd::make_unexpected(err);
  }

  return std::move(ret);
}

nonstd::expected<image::ImageResult, std::string> LoadImageFromFile(
    const std::string &filename, const size_t max_memory_limit_in_mb) {

  // Assume filename is already resolved.
  std::string filepath = filename;

  std::vector<uint8_t> data;
  size_t max_bytes = size_t(1024 * 1024 * max_memory_limit_in_mb);
  std::string err;
  if (!io::ReadWholeFile(&data, &err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    return nonstd::make_unexpected("File not found or failed to read : \"" + filepath + "\"\n");
  }

  if (data.size() < 4) {
    return nonstd::make_unexpected("File size too short. Looks like this file is not an image file : \"" +
                filepath + "\"\n");
  }

  return LoadImageFromMemory(data.data(), data.size(), filename);
}

}  // namespace image
}  // namespace tinyusdz
