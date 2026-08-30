// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpu_scene.hh"

namespace tusdview {

struct PrototypeLodBuildInput {
  std::size_t meshIndex{0};
  std::uint64_t sceneGeneration{0};
  std::vector<DrawVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<DrawSubmesh> submeshes;
};

struct PrototypeLodBuildResult {
  std::size_t meshIndex{0};
  std::uint64_t sceneGeneration{0};
  std::vector<DrawPrototypeLodCPU> levels;
};

bool PrototypeNeedsGeneratedLods(const DrawMeshCPU& mesh);
PrototypeLodBuildResult BuildPrototypeLods(PrototypeLodBuildInput input);

}  // namespace tusdview
