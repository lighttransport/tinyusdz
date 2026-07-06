// SPDX-License-Identifier: Apache-2.0
// tusdview - view-dependent LOD selection for the interactive Vulkan ray-query
// renderer. Pure math (Vulkan-free, light3d only) so it is unit-testable and can
// be reused by tusdrender. Given the camera and each prototype's instances, it
// classifies every placement as Full (trace the real mesh BLAS), Proxy (trace the
// shared unit-box BLAS via a box-fit transform), or Cull (off-screen / sub-pixel,
// dropped from the TLAS), and emits the ready-to-upload instance rows.
#pragma once

#include <cstdint>
#include <vector>

#include "light3d/camera.h"  // light3d::Mat4, Vec3, Frustum

namespace tusdview {

enum class RtLod : std::uint8_t { Cull = 0, Proxy = 1, Full = 2 };

// Camera + viewport snapshot for one selection pass (filled in renderFrame).
struct RtLodCamera {
  light3d::Mat4 viewProj;     // proj*view, column-major, OpenGL Z (frustum source)
  light3d::Vec3 eye{0, 0, 0};
  light3d::Vec3 forward{0, 0, -1};  // normalize(target - eye)
  float nearPlane{0.05f};
  float focalPx{1000.f};      // (viewportH * 0.5) / tan(0.5 * fovYRad)
  // Projected-radius thresholds (pixels): promote to Full at/above fullPx, drop
  // below cullPx. bandFrac is the stochastic transition half-width as a fraction
  // of fullPx (0 = hard switch). When lodEnabled is false every visible instance
  // is Full and nothing is frustum/size culled (exact parity with the no-LOD TLAS).
  float fullPx{64.f};
  float cullPx{2.f};
  float bandFrac{0.25f};
  bool lodEnabled{false};   // false => every visible instance Full, no cull (parity)
  bool proxyEnabled{false}; // false => below-fullPx instances stay Full (P1 cull-only)
  bool frustumCull{true};
  std::uint32_t ditherSeed{0};  // stable for a settled pose (stochastic crossfade)
};

// One coarse cell of an RtLodGrid: a combined world AABB over a contiguous run of
// instance indices in RtLodGrid::order.
struct RtLodGridCell {
  float wmn[3];
  float wmx[3];
  std::uint32_t begin{0};  // offset into RtLodGrid::order
  std::uint32_t count{0};
};

// A coarse spatial grid over a prototype's instance world AABBs. At selection time
// a whole cell can be frustum- or size-rejected before any of its instances are
// touched, so reselect cost scales with the *visible* cell set rather than the
// total instance count (the mechanism that keeps tens-of-millions-of-instances
// scenes interactive). Built once per prototype; instance transforms are static.
struct RtLodGrid {
  std::vector<std::uint32_t> order;  // instance indices grouped cell-by-cell
  std::vector<RtLodGridCell> cells;  // non-empty cells only
  bool valid{false};                 // false => fall back to the flat per-instance loop
};

// A prototype's data the selector reads (a non-owning view onto a VkMeshGPU).
struct RtLodProto {
  const float* instanceXforms{nullptr};  // 12 floats/inst (3x4 row-major), or null
  std::uint32_t instanceCount{0};        // 0 => single placement from `world`
  const float* world{nullptr};           // column-major 4x4 (non-instanced)
  const float* instanceColors{nullptr};  // 3 floats/inst, or null
  const float* instanceOpacities{nullptr};  // 1 float/inst, or null
  const float* flatColor{nullptr};       // 3 floats fallback tint
  float flatOpacity{1.0f};
  const float* protoAabbMin{nullptr};    // object space
  const float* protoAabbMax{nullptr};
  std::uint32_t meshId{0};               // index into the renderer mesh/desc arrays
  const RtLodGrid* grid{nullptr};        // optional coarse cell grid (P5), or null
};

// One emitted TLAS instance, ready to memcpy into the instance + instInfo arrays.
struct RtLodInstance {
  float xform[12];          // 3x4 row-major (raw o2w for Full, BoxFit for Proxy)
  float tint[3];
  float opacity{1.0f};
  std::uint32_t meshId;     // Full: real mesh; Proxy: boxMeshId
  RtLod level;
  bool instanced;          // Full + non-instanced => shader useMaterial=1
};

struct RtLodStats {
  std::uint32_t full{0}, proxy{0}, culled{0};
};

// Projected radius (px) of a world-space sphere (center c, radius r): the visible
// pixel half-extent given the camera focal length, using view-space depth so an
// off-axis instance is not over-promoted by its larger Euclidean distance.
float ProjectedRadiusPx(const float c[3], float r, const RtLodCamera& cam);

// Build a coarse spatial grid over `proto`'s instance world AABBs for fast cell
// rejection in SelectInstanceLOD. No-op (grid->valid = false) when the prototype is
// non-instanced, degenerate, or has fewer than `minInstances` placements (the flat
// loop is already cheap there). Deterministic; call once per prototype.
void BuildRtLodGrid(const RtLodProto& proto, std::uint32_t minInstances,
                    RtLodGrid* grid);

// Classify and emit every visible placement across all prototypes. Appends to
// `out` (caller may reserve). `boxMeshId` is the descriptor index of the shared
// box prototype used for Proxy instances.
RtLodStats SelectInstanceLOD(const RtLodProto* protos, std::uint32_t protoCount,
                             const RtLodCamera& cam, std::uint32_t boxMeshId,
                             std::vector<RtLodInstance>* out);

}  // namespace tusdview
