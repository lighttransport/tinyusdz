// SPDX-License-Identifier: Apache-2.0
// tusdview - HIP/ROCm ray tracing backend.
//
// The AMD counterpart of CudaRayTracer: a self-contained BVH path tracer
// (primary + one shadow ray) that runs on the HIP runtime loaded *at runtime*
// via hipew (no build-time ROCm link) and compiles its trace kernel at runtime
// via hiprtc. If HIP / hiprtc / a device is unavailable the tracer reports
// `initialized() == false` and the caller falls back to another backend.
//
// It shares the exact trace kernel source (raytracer_kernel.inc) and the 2-level
// BVH build with the CUDA backend; only the GPU runtime/driver calls differ.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_scene.hh"

namespace tusdview {

class HipRayTracer {
 public:
  HipRayTracer() = default;
  ~HipRayTracer();

  HipRayTracer(const HipRayTracer&) = delete;
  HipRayTracer& operator=(const HipRayTracer&) = delete;

  // Runtime detection: load HIP + hiprtc via hipew, select a device, compile the
  // kernel. Returns false (with *err set) if anything is missing. Safe to call
  // once; subsequent calls are no-ops returning the cached result.
  bool init(std::string* err);
  bool initialized() const { return ready_; }

  // Flatten `scene` into world-space triangles, build a BVH, and upload to the
  // device. See CudaRayTracer::build for the full contract.
  bool build(const DrawScene& scene, size_t maxTris, std::string* err,
             float displacementScale = 0.0f);
  size_t triangleCount() const { return triCount_; }
  bool truncated() const { return truncated_; }

  // Trace one frame. See CudaRayTracer::trace for the argument contract.
  bool trace(const float invViewProj[16], const float camPos[3],
             const float lightDir[3], const float clearColor[3], int renderMode,
             float depthScale, const float sceneMin[3], const float sceneExtent[3],
             int w, int h, std::vector<uint8_t>* rgba, std::string* err);

  const char* deviceName() const { return deviceName_.c_str(); }

 private:
  void freeScene();

  // Opaque HIP handles (void* to avoid leaking hipew types into the header).
  bool ready_{false};        // device selected + kernel compiled
  void* module_{nullptr};    // hipModule_t
  void* kernel_{nullptr};    // hipFunction_t (trace)
  int device_{0};

  // Device buffers (hipDeviceptr_t stored as uintptr_t).
  uintptr_t dTris_{0};       // float[9] positions per tri
  uintptr_t dNrms_{0};       // float[9] vertex normals per tri
  uintptr_t dCols_{0};       // float[9] per-vertex color per tri (base*displayColor)
  uintptr_t dGeo_{0};        // uint8 geometricNormal flag per tri
  uintptr_t dMat_{0};        // int material id per tri (material-id viz)
  uintptr_t dMatPbr_{0};     // float[6] per material: metal,rough,emitRGB,alpha
  int numMats_{0};           // material count (matPbr index bound)
  uintptr_t dUV_{0};         // float[6] per-vertex uv per tri (uv viz)
  uintptr_t dUV1_{0};        // float[6] per-vertex uv set 1 per tri (multi-UV AOV)
  uintptr_t dInfl_{0};       // float[3] per-vertex blendshape influence per tri
  uintptr_t dFace_{0};       // int source USD face id per tri (source-face-id AOV)
  uintptr_t dDomW_{0};       // float[3] per-vertex dominant skin weight per tri
  uintptr_t dDomJoint_{0};   // int provoking-vertex dominant joint per tri (skin-weights)
  uintptr_t dBlasNodes_{0};  // concatenated BLAS nodes (local-space geometry)
  uintptr_t dTlasNodes_{0};  // TLAS nodes over instance world AABBs
  uintptr_t dInstances_{0};  // Inst[] table (w2o,o2w,tint,blasRoot,instId)
  uintptr_t dVolDens_{0};    // UsdVol: concatenated dense float density grids
  uintptr_t dVolParams_{0};  // UsdVol: VolParam[] (one per volume)
  int numVols_{0};           // UsdVol: volume count
  uintptr_t dOut_{0};        // RGBA8 output image
  size_t outCap_{0};         // bytes currently allocated for dOut_

  size_t triCount_{0};       // unique prototype triangles (geometry stored once)
  size_t instCount_{0};      // total instances (TLAS leaves)
  size_t blasNodeCount_{0};
  size_t tlasNodeCount_{0};
  size_t nodeCount_{0};
  bool truncated_{false};
  std::string deviceName_;
};

}  // namespace tusdview
