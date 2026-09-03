// SPDX-License-Identifier: Apache-2.0
// lusdview - GPU-budget LOD for the realtime raster preview.
//
// A huge assembled scene (e.g. Moana island: ~84k meshes / ~43M instances) does
// not exceed VRAM after dedup (~5 GiB), but the per-mesh-buffer raster upload
// path creates tens of thousands of Vulkan buffers and stalls for minutes before
// a single frame. This pass bounds the *mesh/draw count* (and VRAM): it keeps the
// N most prominent meshes at full geometry and MERGES every other mesh into a
// single bounding-box "proxy soup" -- one instanced unit-cube mesh whose per-
// instance transforms place each original prototype's AABB at each instance. The
// scene then uploads as ~N+1 GPU buffers, fits the VRAM budget, and is rendered
// with the existing per-instance frustum culling. Simple preview only (the proxy
// boxes shade flat; no shadows/GI).
#pragma once

#include <cstddef>
#include <string>

namespace lusdview {

struct DrawScene;

// Transform `draw` in place. Keep the most prominent meshes full (ranked by
// prototype size) within both budgets, merge the rest into one bbox-proxy mesh.
//   vramBudgetBytes : stop admitting full meshes past this many bytes (0 = no cap)
//   maxFullMeshes   : keep at most this many meshes full (0 = no count cap)
// At least one budget should be > 0; with both 0 this is a no-op. `report` (if
// non-null) receives a one-line human summary. No-op when nothing would merge.
void ApplyGpuBudgetLOD(DrawScene* draw, std::size_t vramBudgetBytes,
                       std::size_t maxFullMeshes, std::string* report);

// Robust scene bounds for auto-framing huge scenes that contain a few
// horizon-scale outlier meshes (e.g. Moana island's `osOcean` plane spans to the
// horizon, so fit-all frames the island down to a speck). Per axis it trims
// `trimFrac` of the extreme mesh-AABB endpoints, dropping the handful of outlier
// endpoints while the bulk of geometry defines the range. Writes outMin/outMax
// and returns true ONLY when the trimmed bounds are materially tighter than the
// full bounds (a real outlier exists); otherwise returns false and the caller
// should frame the full bounds unchanged. Needs enough meshes to be meaningful.
//   trimFrac : fraction trimmed from each end per axis (e.g. 0.01 = 1%)
bool ComputeRobustSceneBounds(const DrawScene* draw, float trimFrac,
                              float outMin[3], float outMax[3],
                              std::string* report);

}  // namespace lusdview
