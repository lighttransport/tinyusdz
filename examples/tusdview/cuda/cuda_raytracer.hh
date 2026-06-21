// SPDX-License-Identifier: Apache-2.0
// tusdview - CUDA ray tracing backend.
//
// A self-contained BVH path tracer (primary + one shadow ray) that runs on the
// CUDA driver API loaded *at runtime* via cuew (no build-time CUDA link) and
// compiles its trace kernel at runtime via NVRTC. If CUDA / NVRTC / a device is
// unavailable the tracer reports `available() == false` and the caller falls back
// to another backend.
//
// Geometry is flattened to world-space triangles (instances expanded) and stored
// in a single BVH -- simple and correct; full two-level instancing is future work.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_scene.hh"

namespace tusdview {

class CudaRayTracer {
 public:
  CudaRayTracer() = default;
  ~CudaRayTracer();

  CudaRayTracer(const CudaRayTracer&) = delete;
  CudaRayTracer& operator=(const CudaRayTracer&) = delete;

  // Runtime detection: load CUDA + NVRTC via cuew, create a context, compile the
  // kernel. Returns false (with *err set) if anything is missing. Safe to call
  // once; subsequent calls are no-ops returning the cached result.
  bool init(std::string* err);
  bool initialized() const { return ctx_ != nullptr; }

  // Flatten `scene` into world-space triangles, build a BVH, and upload everything
  // to the device. `maxTris` caps the flattened triangle count (instances are
  // expanded, so heavily-instanced scenes can explode); 0 = no cap. Returns false
  // on OOM / build failure. Replaces any previously built scene.
  bool build(const DrawScene& scene, size_t maxTris, std::string* err);
  size_t triangleCount() const { return triCount_; }
  bool truncated() const { return truncated_; }

  // Trace one frame. `invViewProj` is the column-major inverse(proj*view) (16
  // floats), `camPos`/`lightDir` are 3-float world vectors (lightDir points toward
  // the light), `clearColor` is the 3-float background. Writes a top-down RGBA8
  // image (row 0 = top) of size w*h*4 into *rgba. Returns false on launch failure.
  // renderMode mirrors RenderMode (0=shaded, 1=wireframe, 2=normals, 3=material-id,
  // 4=geom normal, 5=uv, 6=depth). depthScale normalizes the depth AOV.
  bool trace(const float invViewProj[16], const float camPos[3],
             const float lightDir[3], const float clearColor[3], int renderMode,
             float depthScale, const float sceneMin[3], const float sceneExtent[3],
             int w, int h, std::vector<uint8_t>* rgba, std::string* err);

  const char* deviceName() const { return deviceName_.c_str(); }

 private:
  void freeScene();

  // Opaque CUDA handles (void* to avoid leaking cuew/driver types into the header).
  void* ctx_{nullptr};       // CUcontext
  void* module_{nullptr};    // CUmodule
  void* kernel_{nullptr};    // CUfunction (trace)
  int device_{0};

  // Device buffers (CUdeviceptr stored as uintptr_t).
  uintptr_t dTris_{0};       // float[9] positions per tri
  uintptr_t dNrms_{0};       // float[9] vertex normals per tri
  uintptr_t dCols_{0};       // float[9] per-vertex color per tri (base*displayColor)
  uintptr_t dGeo_{0};        // uint8 geometricNormal flag per tri
  uintptr_t dMat_{0};        // int material id per tri (material-id viz)
  uintptr_t dUV_{0};         // float[6] per-vertex uv per tri (uv viz)
  uintptr_t dNodes_{0};      // BVH nodes
  uintptr_t dOut_{0};        // RGBA8 output image
  size_t outCap_{0};         // bytes currently allocated for dOut_

  size_t triCount_{0};
  size_t nodeCount_{0};
  bool truncated_{false};
  std::string deviceName_;
};

}  // namespace tusdview
