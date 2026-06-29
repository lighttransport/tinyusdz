// SPDX-License-Identifier: Apache-2.0
// tusdview — shared host-side scene build for the CUDA/HIP screenshot tracers.
// Flattens the DrawScene into world/local-space triangle SoA + per-prototype
// BLAS + a TLAS over instances, ready to upload to the device. The per-mesh
// geometry build (flatten + per-prototype BLAS) is parallelized across meshes;
// the result is byte-identical to a serial build. Both tracers call this and
// then just upload the arrays (the only per-backend difference).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_scene.hh"  // DrawScene
#include "rt_bvh.hh"     // Node

namespace tusdview {

// Per-instance record (must match `Inst` in the trace kernel: all 4-byte fields).
struct Inst {
  float w2o[12];   // world->object (affine inverse of o2w)
  float o2w[12];   // object->world (row-major 3x4)
  float tint[3];   // per-instance color
  int blasRoot;    // global node index of this instance's BLAS root
  int instId;      // stable instance id (instance-id AOV)
};

// Per-volume params (must match `VolParam` in the trace kernel).
struct HostVolParam {
  float invModel[16];
  float bmin[4];
  float bmax[4];
  int dim[4];  // .xyz dims, .w = float offset into volDens
  float albedo[4];
  float emission[4];
};

// Fully-built host scene, device-upload ready. Arrays mirror the kernel inputs.
struct HostScene {
  std::vector<float> tris, nrms, cols, uv, uv1, infl, domw;
  std::vector<uint8_t> geo;
  std::vector<int> mat, face, domj;
  std::vector<Node> blas;       // BLAS nodes, rebased to the global arrays
  std::vector<Node> tlas;       // TLAS nodes (root at 0)
  std::vector<Inst> instances;  // leaf-order (matches the TLAS)
  std::vector<float> matPbr;
  int numMats = 0;
  std::vector<float> volDens;
  std::vector<HostVolParam> volParams;
  int numVols = 0;
  size_t triCount = 0, instCount = 0, blasNodeCount = 0, tlasNodeCount = 0;
  bool truncated = false;
};

// Build `out` from `scene`. `maxTris` caps unique prototype triangles, `maxInstances`
// caps the instance count (0 = unlimited). `displacementScale` bakes coarse
// UsdPreviewSurface displacement into the traced geometry (0 = none). Returns
// false (with *err) only when the scene has no triangles/instances.
bool BuildHostScene(const DrawScene& scene, size_t maxTris, size_t maxInstances,
                    float displacementScale, HostScene* out, std::string* err);

}  // namespace tusdview
