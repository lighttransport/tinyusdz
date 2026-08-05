// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — the shared ray-tracing acceleration structure.
//
// One BLAS per QlMesh plus a TLAS with identity transforms (the loader bakes
// world space into the positions), so a hit reports (mesh, triangle) directly
// and geometry appended during a progressive load only costs a TLAS rebuild.
//
// This is deliberately owned outside the renderer: the CPU tracer needs it to
// shade, and the app needs it to pick, but neither should build its own. That
// matters under a tight --max-mem, because lightrt allocates with plain malloc
// and those bytes are invisible to the budget.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "camera.hh"
#include "ql_scene.hh"

struct lrt_tlas;
struct lrt_tri_scene;

namespace tusdql {

struct PickHit {
  float position[3] = {0, 0, 0};
  float normal[3] = {0, 1, 0};
  size_t mesh_index = 0;
  float distance = 0.0f;
};

class PickAccel {
 public:
  PickAccel() = default;
  ~PickAccel();

  PickAccel(const PickAccel&) = delete;
  PickAccel& operator=(const PickAccel&) = delete;

  // Point at a scene. Does not build; call Sync().
  void SetScene(const QlScene* scene);

  // Rebuild for geometry appended since the last call. Cheap when nothing
  // changed, so it is safe to call every frame during a progressive load.
  // `threads` only affects build speed. Returns true if anything was rebuilt.
  bool Sync(int threads);

  // Closest hit along the primary ray through pixel (px, py).
  bool Trace(const OrbitCamera& camera, int px, int py, int width, int height,
             PickHit* out) const;

  // For the CPU renderer, which traces against the same structure.
  const lrt_tlas* tlas() const { return tlas_; }
  size_t mesh_index(uint32_t instance_id) const {
    return instance_id < blas_.size() ? blas_[instance_id].mesh_index : 0;
  }
  size_t instance_count() const { return blas_.size(); }
  const QlScene* scene() const { return scene_; }

 private:
  struct Entry {
    lrt_tri_scene* scene = nullptr;
    size_t mesh_index = 0;
  };

  void Release();

  const QlScene* scene_ = nullptr;
  std::vector<Entry> blas_;
  std::vector<lrt_tri_scene*> blas_ptrs_;
  lrt_tlas* tlas_ = nullptr;
  size_t synced_mesh_count_ = 0;
};

}  // namespace tusdql
