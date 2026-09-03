// SPDX-License-Identifier: Apache-2.0
#include "render/pick.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include "lightrt_c_tri.h"
}

namespace lusdql {

PickAccel::~PickAccel() { Release(); }

void PickAccel::Release() {
  if (tlas_) {
    lrt_tlas_free(tlas_);
    tlas_ = nullptr;
  }
  for (Entry& e : blas_) {
    if (e.scene) lrt_tri_scene_free(e.scene);
  }
  blas_.clear();
  blas_ptrs_.clear();
}

void PickAccel::SetScene(const QlScene* scene) {
  scene_ = scene;
  Release();
  synced_mesh_count_ = 0;
}

bool PickAccel::Sync(int threads) {
  if (!scene_) return false;
  if (scene_->meshes.size() == synced_mesh_count_) return false;

  lrt_tri_build_options opts{};
  // LBVH: a previewer wants the image on screen, not the last 10% of traversal
  // speed, and the build is the user-visible latency here.
  opts.quality = LRT_TRI_BUILD_FAST;
  opts.layout = LRT_TRI_LAYOUT_AUTO;
  opts.max_leaf_size = 0;
  opts.num_threads = static_cast<unsigned>(std::max(1, threads));

  if (scene_->meshes.size() < synced_mesh_count_) {
    // The loader only appends, but keep the invariant safe if a caller reuses
    // a scene object in an unexpected way.
    Release();
    synced_mesh_count_ = 0;
  }

  blas_.reserve(scene_->meshes.size());
  blas_ptrs_.reserve(scene_->meshes.size());
  std::vector<lrt_instance> instances;
  instances.reserve(scene_->meshes.size());

  // BLASes are immutable once their mesh arrives. Keep them and only build the
  // newly appended meshes; the TLAS below is the part that must be rebuilt to
  // expose the new instances.
  for (size_t i = synced_mesh_count_; i < scene_->meshes.size(); i++) {
    const QlMesh& m = scene_->meshes[i];
    if (m.triangle_count() == 0) continue;

    lrt_result err = LRT_RESULT_OK;
    lrt_tri_scene* bs = lrt_tri_scene_build_indexed(
        m.positions.data(), m.vertex_count(), m.indices.data(),
        m.triangle_count(), &opts, &err);
    if (!bs) continue;

    Entry entry;
    entry.scene = bs;
    entry.mesh_index = i;
    blas_.push_back(entry);
    blas_ptrs_.push_back(bs);

  }

  if (tlas_) {
    lrt_tlas_free(tlas_);
    tlas_ = nullptr;
  }
  for (size_t i = 0; i < blas_ptrs_.size(); i++) {
    lrt_instance inst{};
    inst.blas_id = static_cast<uint32_t>(i);
    // Identity 3x4: positions were already baked into world space by the
    // loader, so the TLAS exists purely to key hits back to a mesh.
    inst.obj2world[0] = 1.0f;
    inst.obj2world[5] = 1.0f;
    inst.obj2world[10] = 1.0f;
    inst.instance_id = static_cast<uint32_t>(i);
    inst.mask = 0xFFFFFFFFu;
    instances.push_back(inst);
  }

  if (!instances.empty()) {
    lrt_result err = LRT_RESULT_OK;
    tlas_ = lrt_tlas_build(blas_ptrs_.data(), blas_ptrs_.size(),
                           instances.data(), instances.size(), &opts, &err);
  }

  synced_mesh_count_ = scene_->meshes.size();
  return true;
}

bool PickAccel::Trace(const OrbitCamera& camera, int px, int py, int width,
                      int height, PickHit* out) const {
  if (!tlas_ || !scene_) return false;

  float origin[3], direction[3];
  // Pixel centre: picking should hit what is under the cursor, with none of
  // the sub-pixel jitter the progressive tracer uses.
  camera.GenerateRay(px, py, width, height, 0.5f, 0.5f, origin, direction);

  lrt_ray ray{};
  std::memcpy(ray.org, origin, sizeof(ray.org));
  std::memcpy(ray.dir, direction, sizeof(ray.dir));
  ray.tmin = 0.0f;
  ray.tmax = 1e30f;

  lrt_tlas_hit hit{};
  if (!lrt_tlas_intersect1(tlas_, &ray, 0xFFFFFFFFu, &hit)) return false;
  if (hit.inst_id >= blas_.size()) return false;

  const size_t mesh_index = blas_[hit.inst_id].mesh_index;
  if (mesh_index >= scene_->meshes.size()) return false;
  const QlMesh& mesh = scene_->meshes[mesh_index];

  const size_t tri = hit.prim_id;
  if (tri * 3 + 2 >= mesh.indices.size()) return false;

  out->mesh_index = mesh_index;
  out->distance = hit.t;
  for (int i = 0; i < 3; i++) {
    out->position[i] = origin[i] + direction[i] * hit.t;
  }

  const uint32_t i0 = mesh.indices[tri * 3 + 0];
  const uint32_t i1 = mesh.indices[tri * 3 + 1];
  const uint32_t i2 = mesh.indices[tri * 3 + 2];
  const float* p0 = &mesh.positions[size_t(i0) * 3];
  const float* p1 = &mesh.positions[size_t(i1) * 3];
  const float* p2 = &mesh.positions[size_t(i2) * 3];
  const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
  const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
  float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0]};
  const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (len > 1e-20f) {
    for (int i = 0; i < 3; i++) n[i] /= len;
  }
  std::memcpy(out->normal, n, sizeof(n));
  return true;
}

}  // namespace lusdql
