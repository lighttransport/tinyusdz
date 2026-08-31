// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpu_scene.hh"

namespace tusdview {

// Hash/equality deliberately describe rendered output, not USD identity. Names,
// paths and diagnostic parameter spelling are excluded; every scalar and texture
// sampling lane consumed by raster or RT is packed by the shared bridge first.
uint64_t DrawMaterialRenderHash(const DrawMaterialCPU& material);
bool DrawMaterialsRenderEquivalent(const DrawMaterialCPU& a,
                                   const DrawMaterialCPU& b);

struct DrawMaterialTable {
  // One entry per logical DrawScene::materials record.
  std::vector<int> logicalToCanonical;
  // Logical material id used as the payload source for each canonical entry.
  std::vector<int> canonicalRepresentatives;
};

// Build an identity-preserving table for renderer-side resource sharing.
DrawMaterialTable BuildDrawMaterialTable(
    const std::vector<DrawMaterialCPU>& materials);

// Refresh material optimization counters without changing materials or any
// authored binding. Returns the number of shared logical payloads.
size_t CanonicalizeDrawMaterials(DrawScene* scene);

// Compatibility spelling. This no longer removes authored records; it returns
// the number of logical records sharing canonical raster payloads.
size_t DeduplicateDrawMaterials(DrawScene* scene);

}  // namespace tusdview
