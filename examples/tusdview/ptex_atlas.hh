// SPDX-License-Identifier: Apache-2.0
// tusdview - bounded coarse Ptex atlas construction.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "gpu_scene.hh"
#include "ptx-loader.hh"

namespace tusdview {

struct PtexAtlasOptions {
  uint32_t maxFaceEdge{512};
  uint32_t maxAtlasEdge{16384};
  uint32_t gutter{2};
  size_t maxDecodedFaceBytes{64ull * 1024ull * 1024ull};
  // Decoded face pages retained while constructing an atlas. Zero disables
  // retention while preserving the same bounded one-page-at-a-time behavior.
  size_t maxDecodedCacheBytes{0};
  size_t maxAtlasBytes{0};  // 0 = derive only from maxAtlasEdge.
};

struct PtexPageCacheStats {
  uint64_t hits{0};
  uint64_t misses{0};
  uint64_t evictions{0};
  uint64_t decodedBytes{0};
  uint64_t residentBytes{0};
  uint64_t peakResidentBytes{0};
};

// Byte-bounded decoded-face cache. One cache belongs to one Reader/source;
// entries are keyed by (face,mip), promoted on access, and evicted least-
// recently-used. Oversized pages are returned through a transient slot and are
// never retained, so residentBytes never exceeds the configured limit.
class PtexFacePageCache {
 public:
  explicit PtexFacePageCache(size_t maxBytes) : maxBytes_(maxBytes) {}

  const tinyusdz::ptx::FaceImage* Fetch(
      const tinyusdz::ptx::Reader& reader, uint32_t face, uint32_t mip,
      size_t maxDecodedFaceBytes, std::string* err);
  void Clear();

  const PtexPageCacheStats& stats() const { return stats_; }
  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    uint64_t key{0};
    tinyusdz::ptx::FaceImage image;
  };
  using List = std::list<Entry>;

  size_t maxBytes_{0};
  List entries_;
  std::unordered_map<uint64_t, List::iterator> byKey_;
  tinyusdz::ptx::FaceImage transient_;
  PtexPageCacheStats stats_;
};

struct PtexAtlasBuildStats {
  uint64_t decodedFaces{0};
  uint64_t decodedBytes{0};
  uint64_t atlasBytes{0};
  uint32_t downsampledFaces{0};
  uint32_t rectTexelOffset{0};
  PtexPageCacheStats pageCache;
};

// Builds an RGBA8 atlas without stretching rectangular faces. Face rectangles
// are retained in `faceRects` and include only the inner texels; `image`
// includes the surrounding gutters. Ptex's bottom-left data convention is
// converted to tusdview's top-row-first image convention during the copy.
bool BuildPtexAtlas(const tinyusdz::ptx::Reader& reader,
                    const PtexAtlasOptions& options, bool srgb,
                    light3d::Image* image,
                    std::vector<DrawPtexFaceRectCPU>* faceRects,
                    PtexAtlasBuildStats* stats, std::string* err);

}  // namespace tusdview
