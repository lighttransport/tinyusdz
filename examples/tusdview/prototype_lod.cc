// SPDX-License-Identifier: Apache-2.0
#include "prototype_lod.hh"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

#include "external/meshoptimizer/meshoptimizer.h"

namespace tusdview {

bool PrototypeNeedsGeneratedLods(const DrawMeshCPU& mesh) {
  if (mesh.instanceCount() == 0 || !mesh.morphs.empty() ||
      !mesh.jointIdx.empty()) {
    return false;
  }
  const std::size_t tris = mesh.indices.size() / 3;
  return tris >= 2000 && tris * mesh.instanceCount() >= 1000000;
}

namespace {

struct LevelSpec {
  float ratio;
  float maxError;
  bool allowSloppy;
};

DrawPrototypeLodCPU BuildLevel(const PrototypeLodBuildInput& input,
                               const LevelSpec& spec) {
  DrawPrototypeLodCPU out;
  if (input.vertices.empty() || input.indices.empty()) return out;
  out.indices.reserve(static_cast<std::size_t>(
      static_cast<double>(input.indices.size()) * spec.ratio));
  float worstError = 0.0f;

  for (const DrawSubmesh& sub : input.submeshes) {
    if (sub.indexCount < 6 ||
        std::size_t(sub.indexOffset) + sub.indexCount > input.indices.size()) {
      continue;
    }
    const std::uint32_t* src = input.indices.data() + sub.indexOffset;
    std::size_t target = static_cast<std::size_t>(
        static_cast<double>(sub.indexCount) * spec.ratio);
    target = std::max<std::size_t>(3, target - target % 3);
    std::vector<std::uint32_t> simplified(sub.indexCount);
    float error = 0.0f;
    const unsigned int options = meshopt_SimplifyPermissive;
    std::size_t count = meshopt_simplify(
        simplified.data(), src, sub.indexCount, &input.vertices[0].px,
        input.vertices.size(), sizeof(DrawVertex), target, spec.maxError,
        options, &error);
    // Disconnected vegetation and heavily split face-varying meshes can stop
    // well above the requested ratio. The coarse level is explicitly distant,
    // so use the spatial fallback rather than retaining millions of tiny leaves.
    if (spec.allowSloppy && count > target + target / 4) {
      float sloppyError = 0.0f;
      const std::size_t sloppy = meshopt_simplifySloppy(
          simplified.data(), src, sub.indexCount, &input.vertices[0].px,
          input.vertices.size(), sizeof(DrawVertex), target, spec.maxError,
          &sloppyError);
      if (sloppy >= 3 && sloppy < count) {
        count = sloppy;
        error = sloppyError;
      }
    }
    count -= count % 3;
    if (count < 3) continue;
    meshopt_optimizeVertexCache(simplified.data(), simplified.data(), count,
                                input.vertices.size());
    DrawSubmesh lodSub = sub;
    lodSub.indexOffset = static_cast<std::uint32_t>(out.indices.size());
    lodSub.indexCount = static_cast<std::uint32_t>(count);
    out.indices.insert(out.indices.end(), simplified.begin(),
                       simplified.begin() + static_cast<std::ptrdiff_t>(count));
    out.submeshes.push_back(lodSub);
    worstError = std::max(worstError, error);
  }
  out.objectError = worstError;
  out.triangleRatio = input.indices.empty()
                          ? 1.0f
                          : static_cast<float>(out.indices.size()) /
                                static_cast<float>(input.indices.size());
  return out;
}

}  // namespace

PrototypeLodBuildResult BuildPrototypeLods(PrototypeLodBuildInput input) {
  PrototypeLodBuildResult result;
  result.meshIndex = input.meshIndex;
  result.sceneGeneration = input.sceneGeneration;
  static constexpr std::array<LevelSpec, 3> kSpecs{{
      {0.50f, 0.005f, false},
      {0.15f, 0.020f, true},
      {0.05f, 0.050f, true},
  }};
  std::size_t previous = input.indices.size();
  uint32_t logicalLevel = 1;
  for (const LevelSpec& spec : kSpecs) {
    DrawPrototypeLodCPU level = BuildLevel(input, spec);
    level.level = logicalLevel++;
    if (level.indices.empty() || level.indices.size() * 5 > previous * 4) {
      continue;  // less than 20% reduction: it is not a useful distinct level
    }
    previous = level.indices.size();
    result.levels.push_back(std::move(level));
  }
  return result;
}

}  // namespace tusdview
