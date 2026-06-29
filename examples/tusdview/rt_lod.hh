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
  bool lodEnabled{false};
  bool frustumCull{true};
  std::uint32_t ditherSeed{0};  // stable for a settled pose (stochastic crossfade)
};

// A prototype's data the selector reads (a non-owning view onto a VkMeshGPU).
struct RtLodProto {
  const float* instanceXforms{nullptr};  // 12 floats/inst (3x4 row-major), or null
  std::uint32_t instanceCount{0};        // 0 => single placement from `world`
  const float* world{nullptr};           // column-major 4x4 (non-instanced)
  const float* instanceColors{nullptr};  // 3 floats/inst, or null
  const float* flatColor{nullptr};       // 3 floats fallback tint
  const float* protoAabbMin{nullptr};    // object space
  const float* protoAabbMax{nullptr};
  std::uint32_t meshId{0};               // index into the renderer mesh/desc arrays
};

// One emitted TLAS instance, ready to memcpy into the instance + instInfo arrays.
struct RtLodInstance {
  float xform[12];          // 3x4 row-major (raw o2w for Full, BoxFit for Proxy)
  float tint[3];
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

// Classify and emit every visible placement across all prototypes. Appends to
// `out` (caller may reserve). `boxMeshId` is the descriptor index of the shared
// box prototype used for Proxy instances.
RtLodStats SelectInstanceLOD(const RtLodProto* protos, std::uint32_t protoCount,
                             const RtLodCamera& cam, std::uint32_t boxMeshId,
                             std::vector<RtLodInstance>* out);

}  // namespace tusdview
