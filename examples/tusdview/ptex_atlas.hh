// SPDX-License-Identifier: Apache-2.0
// tusdview - bounded coarse Ptex atlas construction.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gpu_scene.hh"
#include "ptx-loader.hh"

namespace tusdview {

struct PtexAtlasOptions {
  uint32_t maxFaceEdge{512};
  uint32_t maxAtlasEdge{16384};
  uint32_t gutter{2};
  size_t maxDecodedFaceBytes{64ull * 1024ull * 1024ull};
  size_t maxAtlasBytes{0};  // 0 = derive only from maxAtlasEdge.
};

struct PtexAtlasBuildStats {
  uint64_t decodedFaces{0};
  uint64_t decodedBytes{0};
  uint64_t atlasBytes{0};
  uint32_t downsampledFaces{0};
  uint32_t rectTexelOffset{0};
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
