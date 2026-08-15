// SPDX-License-Identifier: Apache-2.0
// tusdview - CPU ray tracing backend.
//
// Reuses BuildHostScene() (rt_scene_build.hh), the same DrawScene -> flat
// world-space triangle-soup flattener the CUDA/HIP tracers already use (which
// expands instances into unique world-space triangles -- "simple and
// correct", per CudaRayTracer's header comment), then builds a single BVH
// over that soup with the vendored lightrt_c library (src/external/lightrt,
// shared with tools/tusdrender via the lightrt_c CMake target) instead of
// uploading to a device. No refit: unlike HipRayTracer, a re-pose always
// re-flattens + rebuilds (lightrt_c_tri has no per-vertex BVH refit API,
// only lrt_tlas_refit for instance-transform-only TLASs, which this single-
// BLAS design does not use) -- the same simplification CudaRayTracer already
// accepts.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_scene.hh"
#include "rt_camera.hh"
#include "rt_scene_build.hh"  // HostScene, BuildHostScene, BuildProgress

typedef struct lrt_tri_scene lrt_tri_scene;  // src/external/lightrt/lightrt_c_tri.h

namespace tusdview {

class CpuRayTracer {
 public:
  CpuRayTracer() = default;
  ~CpuRayTracer();

  CpuRayTracer(const CpuRayTracer&) = delete;
  CpuRayTracer& operator=(const CpuRayTracer&) = delete;

  // CPU tracing has no device to detect; always succeeds. Kept for API parity
  // with CudaRayTracer/HipRayTracer so App's dispatch code is uniform.
  bool init(std::string* /*err*/) { return true; }
  void setTextureBudgetBytes(size_t bytes) { textureBudgetBytes_ = bytes; }

  // Flatten `scene` (BuildHostScene) and build a BVH over the result. Replaces
  // any previously built scene. See CudaRayTracer::build for the parameter
  // meanings (identical contract).
  bool build(const DrawScene& scene, size_t maxTris, size_t maxInstances,
             std::string* err, float displacementScale = 0.0f,
             BuildProgress* progress = nullptr);
  bool canRefit() const { return false; }  // no vertex-level BVH refit in lightrt_c_tri
  size_t triangleCount() const { return triCount_; }
  bool truncated() const { return truncated_; }

  // Trace one frame on CPU worker threads (std::thread::hardware_concurrency(),
  // one sample/pixel -- no accumulation/progressive refinement in this first
  // cut). Same signature/contract as CudaRayTracer::trace: writes a top-down
  // RGBA8 image into *rgba. renderMode: 0=shaded, 2=normals, 3=material id,
  // 6=depth (others fall back to shaded). `lens` (depth of field) and
  // `spp` > 1 (supersampling) are accepted but not yet implemented.
  bool trace(const float invViewProj[16], const float viewProj[16],
             const float camPos[3], const float lightDir[3],
             const float clearColor[3], float exposure, int renderMode,
             float depthScale, const float sceneMin[3], const float sceneExtent[3],
             int w, int h, std::vector<uint8_t>* rgba, std::string* err,
             int spp = 1, const RtCameraLens* lens = nullptr);

  const char* deviceName() const { return "CPU"; }

 private:
  void freeScene();

  HostScene hs_;             // retained (unlike CUDA/HIP, which discard it after device upload)
  lrt_tri_scene* scene_{nullptr};
  size_t triCount_{0};
  bool truncated_{false};
  size_t textureBudgetBytes_{0};
};

}  // namespace tusdview
