// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

#include "gpu_scene.hh"

namespace tusdview {

// Hash/equality deliberately describe rendered output, not USD identity. Names,
// paths and diagnostic parameter spelling are excluded; every scalar and texture
// sampling lane consumed by raster or RT is packed by the shared bridge first.
uint64_t DrawMaterialRenderHash(const DrawMaterialCPU& material);
bool DrawMaterialsRenderEquivalent(const DrawMaterialCPU& a,
                                   const DrawMaterialCPU& b);

// Deduplicate a complete, non-streaming DrawScene and remap all live bindings.
// Returns the number of removed material records.
size_t DeduplicateDrawMaterials(DrawScene* scene);

}  // namespace tusdview
