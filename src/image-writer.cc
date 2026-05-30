// SPDX-License-Identifier: Apache 2.0
#if defined(TINYUSDZ_WITH_EXR)
#include "external/tinyexr.h"
#endif

#if defined(TINYUSDZ_WITH_TIFF)
#include "external/tiny_dng_writer.h"
#endif

#ifndef TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/stb_image_write.h"

#if defined(TINYUSDZ_HAVE_FPNG)
#include "external/fpng.h"
#endif

#if defined(TINYUSDZ_HAVE_FPNGE)
#include "external/fpnge/fpnge.h"
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "image-writer.hh"
#include "io-util.hh"
#include "str-util.hh"

#include <climits>

namespace tinyusdz {
namespace image {

namespace {

bool DetectFileFormatFromExtension(const std::string &_ext, tinyusdz::image::WriteImageFormat &format) {

  std::string ext = to_lower(_ext);

  if (ext == "bmp") {
    format = tinyusdz::image::WriteImageFormat::BMP;
    return true;
  } else if (ext == "png") {
    format = tinyusdz::image::WriteImageFormat::PNG;
    return true;
  } else if ((ext == "jpg") || (ext == "jpeg")) {
    format = tinyusdz::image::WriteImageFormat::JPEG;
    return true;
  } else if ((ext == "tiff") || (ext == "tif")) {
    format = tinyusdz::image::WriteImageFormat::TIFF;
    return true;
  } else if (ext == "dng") {
    format = tinyusdz::image::WriteImageFormat::DNG;
    return true;
  } else if (ext == "exr") {
    format = tinyusdz::image::WriteImageFormat::EXR;
    return true;
  }

  return false;
}

// stb_image_write callback that appends to a std::vector<uint8_t>.
void AppendToVector(void *context, void *data, int size) {
  if (size <= 0 || data == nullptr) {
    return;
  }
  auto *buf = reinterpret_cast<std::vector<uint8_t> *>(context);
  const uint8_t *p = reinterpret_cast<const uint8_t *>(data);
  buf->insert(buf->end(), p, p + size_t(size));
}

bool ValidDims(const Image &image, std::string *err) {
  if (image.width <= 0 || image.height <= 0 || image.channels <= 0) {
    if (err) {
      (*err) = "Invalid image dimensions/channels.";
    }
    return false;
  }
  if (image.channels > 4) {
    if (err) {
      (*err) = "Only 1-4 channel images are supported.";
    }
    return false;
  }
  // Enforce a maximum dimension to prevent integer overflow in stride calculations.
  constexpr int kMaxDimension = 65536;
  if (image.width > kMaxDimension || image.height > kMaxDimension) {
    if (err) {
      (*err) = "Image dimensions exceed maximum allowed size (65536).";
    }
    return false;
  }
  const int64_t npixels = int64_t(image.width) * int64_t(image.height);
  const int64_t bytes_per_channel = (image.bpp > 0) ? (image.bpp / 8) : 1;
  const int64_t expected =
      npixels * int64_t(image.channels) * bytes_per_channel;
  if (int64_t(image.data.size()) < expected) {
    if (err) {
      (*err) = "Image pixel buffer is smaller than width*height*channels*bytesPerChannel.";
    }
    return false;
  }
  return true;
}

#if defined(TINYUSDZ_HAVE_FPNGE)
bool EncodePNG_fpnge(const Image &image, std::vector<uint8_t> *out,
                     std::string *err) {
  const size_t bytes_per_channel = size_t((image.bpp > 0) ? image.bpp / 8 : 1);
  if (bytes_per_channel != 1 && bytes_per_channel != 2) {
    if (err) {
      (*err) = "fpnge: only 8-bit or 16-bit per channel is supported.";
    }
    return false;
  }
  const size_t num_channels = size_t(image.channels);
  if (num_channels < 1 || num_channels > 4) {
    if (err) {
      (*err) = "fpnge: only 1-4 channels are supported.";
    }
    return false;
  }

  const size_t width = size_t(image.width);
  const size_t height = size_t(image.height);
  const uint64_t row_stride_64 = uint64_t(width) * num_channels * bytes_per_channel;
  if (row_stride_64 > SIZE_MAX) {
    if (err) {
      (*err) = "fpnge: row stride too large.";
    }
    return false;
  }
  const size_t row_stride = static_cast<size_t>(row_stride_64);

  struct FPNGEOptions options;
  FPNGEFillOptions(&options, FPNGE_COMPRESS_LEVEL_DEFAULT, FPNGE_CICP_NONE);

  out->resize(
      FPNGEOutputAllocSize(bytes_per_channel, num_channels, width, height));

  const size_t n =
      FPNGEEncode(bytes_per_channel, num_channels, image.data.data(), width,
                  row_stride, height, out->data(), &options);
  if (n == 0) {
    if (err) {
      (*err) = "fpnge: encode failed.";
    }
    return false;
  }
  out->resize(n);
  return true;
}
#endif

#if defined(TINYUSDZ_HAVE_FPNG)
bool EncodePNG_fpng(const Image &image, std::vector<uint8_t> *out,
                    std::string *err) {
  // fpng only supports 8-bit, 3 or 4 channels.
  if (image.bpp != 8) {
    if (err) {
      (*err) = "fpng: only 8-bit per channel is supported.";
    }
    return false;
  }
  if (image.channels != 3 && image.channels != 4) {
    if (err) {
      (*err) = "fpng: only 3(RGB) or 4(RGBA) channels are supported.";
    }
    return false;
  }
  static bool s_fpng_inited = []() {
    fpng::fpng_init();
    return true;
  }();
  (void)s_fpng_inited;

  if (!fpng::fpng_encode_image_to_memory(
          image.data.data(), uint32_t(image.width), uint32_t(image.height),
          uint32_t(image.channels), *out)) {
    if (err) {
      (*err) = "fpng: encode failed.";
    }
    return false;
  }
  return true;
}
#endif  // TINYUSDZ_HAVE_FPNG

bool EncodePNG_stb(const Image &image, std::vector<uint8_t> *out,
                   std::string *err) {
  if (image.bpp != 8) {
    if (err) {
      (*err) = "stb png: only 8-bit per channel is supported.";
    }
    return false;
  }
  const int64_t stride64 = int64_t(image.width) * int64_t(image.channels);
  if (stride64 > static_cast<int64_t>(INT_MAX)) {
    if (err) {
      (*err) = "stb png: image row stride exceeds INT_MAX.";
    }
    return false;
  }
  const int stride = static_cast<int>(stride64);
  out->clear();
  int ret = stbi_write_png_to_func(AppendToVector, out, image.width,
                                   image.height, image.channels,
                                   image.data.data(), stride);
  if (ret == 0) {
    if (err) {
      (*err) = "stb png: encode failed.";
    }
    return false;
  }
  return true;
}

bool EncodePNG(const Image &image, PngEncoder encoder,
               std::vector<uint8_t> *out, std::string *err) {
  // Decide preference order based on the requested encoder.
  // Auto/Fpnge prefer fpnge(when compiled in), then fpng, then stb.
  // Fpng forces fpng(then stb fallback for unsupported channel/bitdepth).
  // When fpnge is not compiled in, a Fpnge request transparently falls
  // back to fpng/stb.
  (void)encoder;
  std::string local_err;

#if defined(TINYUSDZ_HAVE_FPNGE)
  if (encoder == PngEncoder::Auto || encoder == PngEncoder::Fpnge) {
    if (EncodePNG_fpnge(image, out, &local_err)) {
      return true;
    }
  }
#endif

#if defined(TINYUSDZ_HAVE_FPNG)
  if (EncodePNG_fpng(image, out, &local_err)) {
    return true;
  }
#endif

  // Final fallback: stb (handles 1/2-channel 8-bit that fpng rejects).
  if (EncodePNG_stb(image, out, &local_err)) {
    return true;
  }

  if (err) {
    (*err) = "PNG encode failed: " + local_err;
  }
  return false;
}

}  // namespace

nonstd::expected<std::vector<uint8_t>, std::string> WriteImageToMemory(
    const Image &image, const WriteOption option) {
  tinyusdz::image::WriteImageFormat format = option.format;

  if (format == tinyusdz::image::WriteImageFormat::Autodetect) {
    // Choose a sensible default from the pixel format:
    // float -> EXR, otherwise PNG.
    if (image.format == Image::PixelFormat::Float || image.bpp > 16) {
      format = tinyusdz::image::WriteImageFormat::EXR;
    } else {
      format = tinyusdz::image::WriteImageFormat::PNG;
    }
  }

  std::string err;
  if (!ValidDims(image, &err)) {
    return nonstd::make_unexpected(err);
  }

  std::vector<uint8_t> out;

  switch (format) {
    case tinyusdz::image::WriteImageFormat::PNG: {
      if (!EncodePNG(image, option.png_encoder, &out, &err)) {
        return nonstd::make_unexpected(err);
      }
      return out;
    }
    case tinyusdz::image::WriteImageFormat::JPEG: {
      if (image.bpp != 8) {
        return nonstd::make_unexpected("8bit only for JPEG output.");
      }
      int q = option.jpeg_quality;
      if (q < 1) q = 1;
      if (q > 100) q = 100;
      out.clear();
      int ret = stbi_write_jpg_to_func(AppendToVector, &out, image.width,
                                       image.height, image.channels,
                                       image.data.data(), q);
      if (ret == 0) {
        return nonstd::make_unexpected("JPEG encode failed.");
      }
      return out;
    }
    case tinyusdz::image::WriteImageFormat::BMP: {
      if (image.bpp != 8) {
        return nonstd::make_unexpected("8bit only for BMP output.");
      }
      out.clear();
      int ret = stbi_write_bmp_to_func(AppendToVector, &out, image.width,
                                       image.height, image.channels,
                                       image.data.data());
      if (ret == 0) {
        return nonstd::make_unexpected("BMP encode failed.");
      }
      return out;
    }
    case tinyusdz::image::WriteImageFormat::EXR:
    case tinyusdz::image::WriteImageFormat::TIFF:
    case tinyusdz::image::WriteImageFormat::DNG:
      return nonstd::make_unexpected(
          "WriteImageToMemory: EXR/TIFF/DNG output is not implemented yet.");
    case tinyusdz::image::WriteImageFormat::Autodetect:
      return nonstd::make_unexpected("Internal error in WriteImageToMemory.");
  }
  // Unreachable but needed for compilers that don't recognize exhaustive switches.
  return nonstd::make_unexpected("Internal error in WriteImageToMemory.");
}

nonstd::expected<bool, std::string> WriteImageToFile(
    const std::string &filename, const Image &image, WriteOption option) {
  if (option.format == tinyusdz::image::WriteImageFormat::Autodetect) {
    tinyusdz::image::WriteImageFormat detected;
    if (DetectFileFormatFromExtension(io::GetFileExtension(filename),
                                      detected)) {
      option.format = detected;
    }
    // If extension is unknown, leave Autodetect and let WriteImageToMemory pick.
  }

  auto ret = WriteImageToMemory(image, option);
  if (!ret) {
    return nonstd::make_unexpected(ret.error());
  }

  std::string err;
  if (!io::WriteWholeFile(filename, ret.value().data(), ret.value().size(),
                          &err)) {
    return nonstd::make_unexpected("Failed to write image file: " + filename +
                                   " : " + err);
  }

  return true;
}

}  // namespace image
}  // namespace tinyusdz
