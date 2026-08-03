// SPDX-License-Identifier: Apache-2.0
#include "ql_scene.hh"

#include <algorithm>
#include <cmath>

namespace tusdql {

void QlAabb::Expand(const float p[3]) {
  if (!valid) {
    lo[0] = hi[0] = p[0];
    lo[1] = hi[1] = p[1];
    lo[2] = hi[2] = p[2];
    valid = true;
    return;
  }
  for (int i = 0; i < 3; i++) {
    lo[i] = std::min(lo[i], p[i]);
    hi[i] = std::max(hi[i], p[i]);
  }
}

void QlAabb::Expand(const QlAabb& other) {
  if (!other.valid) return;
  Expand(other.lo);
  Expand(other.hi);
}

void QlAabb::Center(float out[3]) const {
  for (int i = 0; i < 3; i++) {
    out[i] = valid ? 0.5f * (lo[i] + hi[i]) : 0.0f;
  }
}

float QlAabb::Radius() const {
  if (!valid) return 0.0f;
  const float dx = hi[0] - lo[0];
  const float dy = hi[1] - lo[1];
  const float dz = hi[2] - lo[2];
  return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
}

size_t QlMesh::byte_size() const {
  return positions.size() * sizeof(float) + normals.size() * sizeof(float) +
         uvs.size() * sizeof(float) + tangents.size() * sizeof(float) +
         indices.size() * sizeof(uint32_t);
}

void QlScene::Clear() {
  meshes.clear();
  materials.clear();
  textures.clear();
  lights.clear();
  cameras.clear();
  bounds = QlAabb{};
  stats = QlSceneStats{};
  degraded = QlDegradation{};
  y_up = true;
  meters_per_unit = 1.0f;
}

void QlScene::RecomputeBounds() {
  bounds = QlAabb{};
  for (const QlMesh& m : meshes) bounds.Expand(m.bounds);
}

uint64_t QlScene::ByteSize() const {
  uint64_t total = 0;
  for (const QlMesh& m : meshes) total += m.byte_size();
  for (const QlTexture& t : textures) total += t.rgba.size();
  return total;
}

}  // namespace tusdql
