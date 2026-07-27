// SPDX-License-Identifier: Apache-2.0
#include "ptex_atlas.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace tusdview {

PtexPhysicalPageCache::PtexPhysicalPageCache(uint32_t slotCount)
    : slots_(slotCount) {}

bool PtexPhysicalPageCache::Request(
    uint32_t face, PtexPhysicalPageAssignment* assignment) {
  if (!assignment || slots_.empty()) return false;
  const auto found = byFace_.find(face);
  if (found != byFace_.end()) {
    Slot& slot = slots_[found->second];
    slot.stamp = ++clock_;
    assignment->slot = found->second;
    assignment->evictedFace = ~uint32_t{0};
    assignment->hit = true;
    ++hits_;
    return true;
  }

  ++misses_;
  uint32_t selected = 0;
  if (residentCount_ < slots_.size()) {
    // Slots are populated in index order, which keeps tests and upload traces
    // reproducible across standard-library implementations.
    selected = residentCount_++;
  } else {
    for (uint32_t i = 1; i < slots_.size(); ++i) {
      if (slots_[i].stamp < slots_[selected].stamp) selected = i;
    }
  }

  Slot& slot = slots_[selected];
  const uint32_t evicted = slot.face;
  if (evicted != ~uint32_t{0}) {
    byFace_.erase(evicted);
    ++evictions_;
  }
  slot.face = face;
  slot.stamp = ++clock_;
  byFace_[face] = selected;
  assignment->slot = selected;
  assignment->evictedFace = evicted;
  assignment->hit = false;
  return true;
}

void PtexPhysicalPageCache::Clear() {
  std::fill(slots_.begin(), slots_.end(), Slot{});
  byFace_.clear();
  residentCount_ = 0;
  clock_ = 0;
  hits_ = 0;
  misses_ = 0;
  evictions_ = 0;
}
namespace {

float HalfToFloat(uint16_t h) {
  const uint32_t sign = uint32_t(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1fu;
  uint32_t mantissa = h & 0x3ffu;
  uint32_t bits = 0;
  if (exp == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      exp = 127u - 15u + 1u;
      while ((mantissa & 0x400u) == 0) {
        mantissa <<= 1;
        --exp;
      }
      mantissa &= 0x3ffu;
      bits = sign | (exp << 23) | (mantissa << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else {
    bits = sign | ((exp + (127u - 15u)) << 23) | (mantissa << 13);
  }
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

uint16_t U16(const uint8_t* p) {
  return uint16_t(p[0]) | uint16_t(p[1]) << 8;
}

float Component(const tinyusdz::ptx::FaceImage& face, size_t pixel,
                uint16_t channel) {
  const size_t component = pixel * face.channels + channel;
  switch (face.dataType) {
    case tinyusdz::ptx::DataType::UInt8:
      return float(face.data[component]) / 255.0f;
    case tinyusdz::ptx::DataType::UInt16:
      return float(U16(face.data.data() + component * 2)) / 65535.0f;
    case tinyusdz::ptx::DataType::Half:
      return HalfToFloat(U16(face.data.data() + component * 2));
    case tinyusdz::ptx::DataType::Float: {
      float value = 0.0f;
      std::memcpy(&value, face.data.data() + component * 4, sizeof(value));
      return value;
    }
  }
  return 0.0f;
}

uint8_t Byte(float value) {
  if (!std::isfinite(value)) value = 0.0f;
  value = std::max(0.0f, std::min(1.0f, value));
  return static_cast<uint8_t>(std::lround(value * 255.0f));
}

light3d::Image ConvertFace(const tinyusdz::ptx::FaceImage& face) {
  light3d::Image out;
  out.width = static_cast<int>(face.width);
  out.height = static_cast<int>(face.height);
  out.channels = 4;
  const size_t pixels = size_t(face.width) * face.height;
  out.data.resize(pixels * 4);
  for (size_t pixel = 0; pixel < pixels; ++pixel) {
    const float r = Component(face, pixel, 0);
    const float g = face.channels > 1 ? Component(face, pixel, 1) : r;
    const float b = face.channels > 2 ? Component(face, pixel, 2) : r;
    const float a = face.channels == 2
                        ? g
                        : (face.channels > 3 ? Component(face, pixel, 3)
                                             : 1.0f);
    uint8_t* dst = out.data.data() + pixel * 4;
    dst[0] = Byte(r);
    dst[1] = Byte(face.channels == 2 ? r : g);
    dst[2] = Byte(face.channels == 2 ? r : b);
    dst[3] = Byte(a);
  }
  return out;
}

struct Placement {
  uint32_t face{0};
  uint32_t mip{0};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t x{0};
  uint32_t y{0};
};

bool Pack(std::vector<Placement>* placements, uint32_t gutter,
          uint32_t maxEdge, uint32_t* width, uint32_t* height) {
  if (!placements || !width || !height || maxEdge == 0) return false;
  std::vector<size_t> order(placements->size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const Placement& pa = (*placements)[a];
    const Placement& pb = (*placements)[b];
    if (pa.height != pb.height) return pa.height > pb.height;
    return pa.width > pb.width;
  });
  uint32_t x = 0, y = 0, rowHeight = 0, usedWidth = 0;
  for (size_t index : order) {
    Placement& p = (*placements)[index];
    const uint64_t outerW = uint64_t(p.width) + uint64_t(gutter) * 2u;
    const uint64_t outerH = uint64_t(p.height) + uint64_t(gutter) * 2u;
    if (outerW > maxEdge || outerH > maxEdge) return false;
    if (uint64_t(x) + outerW > maxEdge) {
      x = 0;
      y += rowHeight;
      rowHeight = 0;
    }
    if (uint64_t(y) + outerH > maxEdge) return false;
    p.x = x + gutter;
    p.y = y + gutter;
    x += static_cast<uint32_t>(outerW);
    rowHeight = std::max(rowHeight, static_cast<uint32_t>(outerH));
    usedWidth = std::max(usedWidth, x);
  }
  *width = std::max(1u, usedWidth);
  *height = std::max(1u, y + rowHeight);
  return true;
}

void CopyPixel(const light3d::Image& src, uint32_t sx, uint32_t sy,
               light3d::Image* dst, uint32_t dx, uint32_t dy) {
  if (!dst || sx >= static_cast<uint32_t>(src.width) ||
      sy >= static_cast<uint32_t>(src.height) ||
      dx >= static_cast<uint32_t>(dst->width) ||
      dy >= static_cast<uint32_t>(dst->height)) return;
  std::memcpy(dst->data.data() + (size_t(dy) * dst->width + dx) * 4,
              src.data.data() + (size_t(sy) * src.width + sx) * 4, 4);
}

void CopyWithClampGutter(const light3d::Image& face, const Placement& p,
                         uint32_t gutter, light3d::Image* atlas) {
  // Ptex rows begin at v=0 (bottom). Atlas images are top-row-first, so reverse
  // source Y while copying the inner rectangle.
  for (uint32_t y = 0; y < p.height; ++y) {
    for (uint32_t x = 0; x < p.width; ++x) {
      CopyPixel(face, x, p.height - 1u - y, atlas, p.x + x, p.y + y);
    }
  }
  for (uint32_t d = 1; d <= gutter; ++d) {
    for (uint32_t x = 0; x < p.width; ++x) {
      CopyPixel(*atlas, p.x + x, p.y, atlas, p.x + x, p.y - d);
      CopyPixel(*atlas, p.x + x, p.y + p.height - 1u, atlas, p.x + x,
                p.y + p.height - 1u + d);
    }
    for (uint32_t y = 0; y < p.height; ++y) {
      CopyPixel(*atlas, p.x, p.y + y, atlas, p.x - d, p.y + y);
      CopyPixel(*atlas, p.x + p.width - 1u, p.y + y, atlas,
                p.x + p.width - 1u + d, p.y + y);
    }
  }
  for (uint32_t gy = 0; gy < gutter; ++gy) {
    for (uint32_t gx = 0; gx < gutter; ++gx) {
      CopyPixel(*atlas, p.x, p.y, atlas, p.x - 1u - gx, p.y - 1u - gy);
      CopyPixel(*atlas, p.x + p.width - 1u, p.y, atlas,
                p.x + p.width + gx, p.y - 1u - gy);
      CopyPixel(*atlas, p.x, p.y + p.height - 1u, atlas,
                p.x - 1u - gx, p.y + p.height + gy);
      CopyPixel(*atlas, p.x + p.width - 1u, p.y + p.height - 1u, atlas,
                p.x + p.width + gx, p.y + p.height + gy);
    }
  }
}

}  // namespace

const tinyusdz::ptx::FaceImage* PtexFacePageCache::Fetch(
    const tinyusdz::ptx::Reader& reader, uint32_t face, uint32_t mip,
    size_t maxDecodedFaceBytes, std::string* err) {
  const uint64_t key = (uint64_t(face) << 32u) | uint64_t(mip);
  const auto found = byKey_.find(key);
  if (found != byKey_.end()) {
    ++stats_.hits;
    entries_.splice(entries_.begin(), entries_, found->second);
    return &entries_.front().image;
  }

  ++stats_.misses;
  tinyusdz::ptx::FaceImage decoded;
  if (!reader.ReadFace(face, mip, maxDecodedFaceBytes, &decoded, err)) {
    return nullptr;
  }
  stats_.decodedBytes += decoded.data.size();
  const size_t bytes = decoded.data.size();
  if (maxBytes_ == 0 || bytes > maxBytes_) {
    transient_ = std::move(decoded);
    return &transient_;
  }

  while (!entries_.empty() && stats_.residentBytes + bytes > maxBytes_) {
    auto last = std::prev(entries_.end());
    stats_.residentBytes -= last->image.data.size();
    byKey_.erase(last->key);
    entries_.erase(last);
    ++stats_.evictions;
  }
  entries_.push_front(Entry{key, std::move(decoded)});
  byKey_[key] = entries_.begin();
  stats_.residentBytes += bytes;
  stats_.peakResidentBytes =
      std::max(stats_.peakResidentBytes, stats_.residentBytes);
  return &entries_.front().image;
}

void PtexFacePageCache::Clear() {
  entries_.clear();
  byKey_.clear();
  transient_ = tinyusdz::ptx::FaceImage{};
  stats_.residentBytes = 0;
}

bool BuildPtexAtlas(const tinyusdz::ptx::Reader& reader,
                    const PtexAtlasOptions& options, bool /*srgb*/,
                    light3d::Image* image,
                    std::vector<DrawPtexFaceRectCPU>* faceRects,
                    PtexAtlasBuildStats* stats, std::string* err) {
  if (!image || !faceRects) {
    if (err) *err = "Ptex atlas output is null";
    return false;
  }
  const tinyusdz::ptx::Info& info = reader.info();
  if (info.meshType != 1 || info.faces == 0 || info.channels == 0 ||
      info.channels > 4) {
    if (err) *err = "Ptex atlas requires a non-empty quad texture with 1-4 channels";
    return false;
  }
  PtexAtlasBuildStats localStats;
  std::vector<Placement> placements(info.faces);
  for (uint32_t face = 0; face < info.faces; ++face) {
    Placement& p = placements[face];
    p.face = face;
    uint32_t w = info.faceInfo[face].width();
    uint32_t h = info.faceInfo[face].height();
    while (p.mip + 1u < info.levels && options.maxFaceEdge > 0 &&
           std::max(w, h) > options.maxFaceEdge) {
      ++p.mip;
      w = std::max(1u, w >> 1u);
      h = std::max(1u, h >> 1u);
    }
    p.width = w;
    p.height = h;
    if (p.mip > 0) ++localStats.downsampledFaces;
  }

  uint32_t atlasWidth = 0, atlasHeight = 0, imageHeight = 0;
  uint32_t rectTexelOffset = 0;
  for (;;) {
    if (Pack(&placements, options.gutter, options.maxAtlasEdge, &atlasWidth,
             &atlasHeight)) {
      const uint64_t rectTexels = uint64_t(info.faces) * 8u;
      const uint64_t rectRows =
          (rectTexels + uint64_t(atlasWidth) - 1u) / atlasWidth;
      const uint64_t totalHeight = uint64_t(atlasHeight) + rectRows;
      const uint64_t bytes = uint64_t(atlasWidth) * totalHeight * 4u;
      if (totalHeight <= options.maxAtlasEdge &&
          (options.maxAtlasBytes == 0 || bytes <= options.maxAtlasBytes)) {
        imageHeight = static_cast<uint32_t>(totalHeight);
        rectTexelOffset = atlasWidth * atlasHeight;
        break;
      }
    }
    size_t best = placements.size();
    uint64_t bestArea = 0;
    for (size_t i = 0; i < placements.size(); ++i) {
      const Placement& p = placements[i];
      if (p.mip + 1u >= info.levels || (p.width == 1 && p.height == 1)) continue;
      const uint64_t area = uint64_t(p.width) * p.height;
      if (area > bestArea) { bestArea = area; best = i; }
    }
    if (best == placements.size()) {
      if (err) *err = "Ptex atlas does not fit configured edge/byte budget";
      return false;
    }
    Placement& p = placements[best];
    ++p.mip;
    p.width = std::max(1u, p.width >> 1u);
    p.height = std::max(1u, p.height >> 1u);
    ++localStats.downsampledFaces;
  }

  image->width = static_cast<int>(atlasWidth);
  image->height = static_cast<int>(imageHeight);
  image->channels = 4;
  image->data.assign(size_t(atlasWidth) * imageHeight * 4, 0);
  faceRects->assign(info.faces, DrawPtexFaceRectCPU{});
  PtexFacePageCache pageCache(options.maxDecodedCacheBytes);
  for (const Placement& p : placements) {
    std::string readErr;
    const tinyusdz::ptx::FaceImage* decoded = pageCache.Fetch(
        reader, p.face, p.mip, options.maxDecodedFaceBytes, &readErr);
    if (!decoded) {
      if (err) *err = "Ptex face " + std::to_string(p.face) + ": " + readErr;
      return false;
    }
    if (decoded->width != p.width || decoded->height != p.height) {
      if (err) *err = "Ptex face dimensions do not match selected mip";
      return false;
    }
    localStats.decodedFaces++;
    localStats.decodedBytes += decoded->data.size();
    const light3d::Image rgba = ConvertFace(*decoded);
    CopyWithClampGutter(rgba, p, options.gutter, image);
    DrawPtexFaceRectCPU& rect = (*faceRects)[p.face];
    rect.x = p.x;
    rect.y = p.y;
    rect.width = p.width;
    rect.height = p.height;
    rect.mipLevel = static_cast<uint16_t>(p.mip);
  }
  // Store the rectangle table in alpha-only texels after the color atlas.
  // GL/Vulkan sRGB formats transform RGB reads but never alpha, so this compact
  // table can be decoded by every backend from the same sampled image.
  for (uint32_t face = 0; face < info.faces; ++face) {
    const DrawPtexFaceRectCPU& rect = (*faceRects)[face];
    const uint16_t values[4] = {
        static_cast<uint16_t>(rect.x), static_cast<uint16_t>(rect.y),
        static_cast<uint16_t>(rect.width), static_cast<uint16_t>(rect.height)};
    for (uint32_t byte = 0; byte < 8; ++byte) {
      const uint32_t linear = rectTexelOffset + face * 8u + byte;
      const uint8_t value = static_cast<uint8_t>(
          (values[byte / 2u] >> ((byte & 1u) * 8u)) & 0xffu);
      image->data[size_t(linear) * 4u + 3u] = value;
    }
  }
  localStats.rectTexelOffset = rectTexelOffset;
  localStats.atlasBytes = image->data.size();
  localStats.pageCache = pageCache.stats();
  if (stats) *stats = localStats;
  return true;
}

}  // namespace tusdview
