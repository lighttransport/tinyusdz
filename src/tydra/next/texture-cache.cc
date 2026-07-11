// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#include "texture-cache.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <utility>

#include "image-loader.hh"
#include "next/reader/usdz-reader.hh"
#include "tydra/texture-util.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

namespace {

// Match a USD asset path against a .usdz entry name. Entries are archive-
// relative and may carry a directory prefix the authored path omits, so accept
// an exact match, a path-suffix match on a directory boundary, or a basename
// match as the last resort.
bool UsdzEntryMatches(const std::string& entry, const std::string& asset) {
  std::string a = asset;
  if (a.rfind("./", 0) == 0) a = a.substr(2);
  if (entry == a) return true;
  if (entry.size() > a.size() &&
      entry.compare(entry.size() - a.size(), a.size(), a) == 0 &&
      entry[entry.size() - a.size() - 1] == '/') {
    return true;
  }
  auto base = [](const std::string& s) {
    const size_t p = s.find_last_of('/');
    return p == std::string::npos ? s : s.substr(p + 1);
  };
  return base(entry) == base(a);
}

bool LoadSourceImage(const TextureDecodeOptions& opt, const std::string& asset,
                     ::tinyusdz::Image* out) {
  if (asset.empty()) return false;
  if (opt.usdz) {
    for (size_t i = 0; i < opt.usdz->NumEntries(); ++i) {
      if (!UsdzEntryMatches(opt.usdz->EntryName(i), asset)) continue;
      auto res = ::tinyusdz::image::LoadImageFromMemory(
          opt.usdz->EntryData(i), opt.usdz->EntrySize(i),
          opt.usdz->EntryName(i));
      if (!res) return false;
      *out = std::move(res.value().image);
      return true;
    }
  }
  std::string path = asset;
  if (path[0] != '/' && !opt.base_dir.empty()) {
    path = opt.base_dir + "/" + path;
  }
  auto res = ::tinyusdz::image::LoadImageFromFile(path);
  if (!res) return false;
  *out = std::move(res.value().image);
  return true;
}

// Raw bytes of an asset, resolved exactly like LoadSourceImage (usdz entry first,
// then base_dir-relative on disk).
bool ReadSourceBytes(const TextureDecodeOptions& opt, const std::string& asset,
                     std::vector<uint8_t>* out) {
  if (asset.empty() || !out) return false;
  if (opt.usdz) {
    for (size_t i = 0; i < opt.usdz->NumEntries(); ++i) {
      if (!UsdzEntryMatches(opt.usdz->EntryName(i), asset)) continue;
      const uint8_t* p = opt.usdz->EntryData(i);
      const size_t n = opt.usdz->EntrySize(i);
      if (!p || !n) return false;
      out->assign(p, p + n);
      return true;
    }
  }
  std::string path = asset;
  if (path[0] != '/' && !opt.base_dir.empty()) {
    path = opt.base_dir + "/" + path;
  }
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  const std::streamoff n = f.tellg();
  if (n <= 0) return false;
  f.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(n));
  return bool(f.read(reinterpret_cast<char*>(out->data()),
                     static_cast<std::streamsize>(n)));
}

// Narrow a 16-bit UInt image to 8-bit in place. Loaders hand back 16-bit PNGs
// as-is, but every consumer downstream wants 8-bit RGBA.
bool NarrowTo8Bit(::tinyusdz::Image* img) {
  if (img->bpp != 16 ||
      img->format != ::tinyusdz::Image::PixelFormat::UInt) {
    return true;
  }
  const size_t samples = size_t(img->width) * size_t(img->height) *
                         size_t(img->channels);
  if (img->data.size() < samples * sizeof(uint16_t)) return false;
  std::vector<uint8_t> narrowed(samples);
  for (size_t i = 0; i < samples; ++i) {
    uint16_t v = 0;
    std::memcpy(&v, img->data.data() + i * sizeof(uint16_t), sizeof(v));
    narrowed[i] = static_cast<uint8_t>((uint32_t(v) + 128u) / 257u);
  }
  img->data = std::move(narrowed);
  img->bpp = 8;
  return true;
}

// Take 1/2/3/4-channel 8-bit texels. With `force_rgba`, grayscale replicates
// into RGB and a missing alpha channel becomes opaque; otherwise the source
// channel count is kept as-is.
bool ToDecoded(const ::tinyusdz::Image& src, bool force_rgba,
               DecodedImage* out) {
  if (src.width <= 0 || src.height <= 0 || src.bpp != 8 ||
      src.format != ::tinyusdz::Image::PixelFormat::UInt) {
    return false;
  }
  const size_t ch = static_cast<size_t>(src.channels);
  if (ch < 1 || ch > 4) return false;
  const size_t npix = size_t(src.width) * size_t(src.height);
  if (src.data.size() < npix * ch) return false;

  out->width = static_cast<uint32_t>(src.width);
  out->height = static_cast<uint32_t>(src.height);

  if (!force_rgba) {
    out->channels = static_cast<uint8_t>(ch);
    out->pixels.assign(src.data.begin(), src.data.begin() + npix * ch);
    return true;
  }

  out->channels = 4;
  out->pixels.assign(npix * 4, 255);
  for (size_t i = 0; i < npix; ++i) {
    const uint8_t* s = src.data.data() + i * ch;
    uint8_t* d = out->pixels.data() + i * 4;
    switch (ch) {
      case 1:
        d[0] = d[1] = d[2] = s[0];
        break;
      case 2:
        d[0] = d[1] = d[2] = s[0];
        d[3] = s[1];
        break;
      case 3:
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        break;
      default:
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        break;
    }
  }
  return true;
}

bool ResizeDecoded(DecodedImage* img, uint32_t w, uint32_t h, bool srgb) {
  if (w == 0 || h == 0 || img->width == 0 || img->height == 0) return false;
  if (img->width == w && img->height == h) return true;

  ::tinyusdz::Image src;
  src.width = static_cast<int>(img->width);
  src.height = static_cast<int>(img->height);
  src.channels = static_cast<int>(img->channels);
  src.bpp = 8;
  src.format = ::tinyusdz::Image::PixelFormat::UInt;
  src.data = img->pixels;

  ::tinyusdz::Image dst;
  const ResizeFilter filter = srgb ? ResizeFilter::SRGB : ResizeFilter::Linear;
  std::string err;
  if (!ResizeImage(src, static_cast<int>(w), static_cast<int>(h), &dst, filter,
                   &err)) {
    return false;
  }
  img->width = static_cast<uint32_t>(dst.width);
  img->height = static_cast<uint32_t>(dst.height);
  img->channels = static_cast<uint8_t>(dst.channels);
  img->pixels = std::move(dst.data);
  return true;
}

// Scale both edges by `ratio`, never below 1 texel.
void ScaledExtent(const DecodedImage& img, double ratio, uint32_t* w,
                  uint32_t* h) {
  *w = std::max(1u, static_cast<uint32_t>(std::floor(img.width * ratio)));
  *h = std::max(1u, static_cast<uint32_t>(std::floor(img.height * ratio)));
}

}  // namespace

bool TextureDecoder::ReadAssetBytes(const std::string& asset,
                                   std::vector<uint8_t>* out) const {
  return ReadSourceBytes(options_, asset, out);
}

bool TextureDecoder::Decode(const std::string& asset, bool srgb,
                            DecodedImage* out) {
  if (!out) return false;

  ::tinyusdz::Image src;
  if (!LoadSourceImage(options_, asset, &src)) return false;
  if (!NarrowTo8Bit(&src)) return false;

  DecodedImage img;
  if (!ToDecoded(src, options_.force_rgba, &img)) return false;
  src.data.clear();
  src.data.shrink_to_fit();

  // Size cap first: it is the cheap, predictable lever, and shrinking here also
  // shrinks what the budget below has to account for.
  if (options_.max_edge > 0) {
    const uint32_t longest = std::max(img.width, img.height);
    if (longest > options_.max_edge) {
      const double ratio = double(options_.max_edge) / double(longest);
      uint32_t w = 0, h = 0;
      ScaledExtent(img, ratio, &w, &h);
      if (ResizeDecoded(&img, w, h, srgb)) ++downscaled_;
    }
  }

  // Byte budget: best-effort. Shrink whatever is left of the allowance across
  // both edges (hence sqrt); when the budget is already spent, fall back to a
  // hard 1/8 area shrink rather than dropping the texture entirely.
  if (options_.budget_bytes > 0) {
    const uint64_t bytes = img.byte_size();
    if (decoded_bytes_ + bytes > options_.budget_bytes) {
      const double remain = decoded_bytes_ < options_.budget_bytes
                                ? double(options_.budget_bytes - decoded_bytes_)
                                : 0.0;
      const double ratio =
          remain > 0.0 ? std::sqrt(remain / double(bytes)) : 0.125;
      uint32_t w = 0, h = 0;
      ScaledExtent(img, ratio, &w, &h);
      if (ResizeDecoded(&img, w, h, srgb)) ++downscaled_;
    }
  }

  decoded_bytes_ += img.byte_size();
  *out = std::move(img);
  return true;
}

std::string ReplaceUdimToken(const std::string& asset, int udim) {
  std::string out = asset;
  const size_t pos = out.find("<UDIM>");
  if (pos != std::string::npos) {
    out.replace(pos, 6, std::to_string(udim));
  }
  return out;
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
