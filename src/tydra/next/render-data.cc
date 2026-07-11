// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Data Implementation

#include "render-data.hh"
#include <cmath>

namespace tinyusdz {
namespace tydra {
namespace next {
namespace {

template <typename T>
size_t VectorBytes(const std::vector<T>& v) {
  return v.capacity() * sizeof(T);
}

size_t StringVectorBytes(const std::vector<std::string>& v) {
  size_t total = v.capacity() * sizeof(std::string);
  for (const std::string& s : v) {
    total += s.capacity();
  }
  return total;
}

}  // namespace

//
// RenderMesh
//

void RenderMesh::compact() {
  face_vertex_counts.shrink_to_fit();
  face_vertex_indices.shrink_to_fit();
  points.shrink_to_fit();
  normals.shrink_to_fit();
  tangents.shrink_to_fit();
  texcoords_0.shrink_to_fit();
  texcoords_1.shrink_to_fit();
  colors.shrink_to_fit();
  opacities.shrink_to_fit();
  triangulated_indices.shrink_to_fit();
  triangulated_face_vertex_indices.shrink_to_fit();

  for (auto& pv : primvars) {
    pv.float_data.shrink_to_fit();
    pv.int_data.shrink_to_fit();
    pv.uint_data.shrink_to_fit();
    pv.indices.shrink_to_fit();
  }

  if (skin) {
    skin->joint_indices.shrink_to_fit();
    skin->joint_weights.shrink_to_fit();
  }

  for (auto& bs : blend_shapes) {
    bs.point_offsets.shrink_to_fit();
    bs.normal_offsets.shrink_to_fit();
    for (auto& inbetween : bs.inbetweens) {
      inbetween.point_offsets.shrink_to_fit();
    }
  }
}

bool RenderMesh::has_alloc_failure() const {
  if (face_vertex_counts.alloc_failed() || face_vertex_indices.alloc_failed() ||
      points.alloc_failed() || normals.alloc_failed() ||
      tangents.alloc_failed() || texcoords_0.alloc_failed() ||
      texcoords_1.alloc_failed() || colors.alloc_failed() ||
      opacities.alloc_failed() ||
      triangulated_indices.alloc_failed() ||
      triangulated_face_vertex_indices.alloc_failed()) {
    return true;
  }
  for (const auto& pv : primvars) {
    if (pv.float_data.alloc_failed() || pv.int_data.alloc_failed() ||
        pv.uint_data.alloc_failed() || pv.indices.alloc_failed()) {
      return true;
    }
  }
  if (skin && (skin->joint_indices.alloc_failed() ||
               skin->joint_weights.alloc_failed())) {
    return true;
  }
  for (const auto& bs : blend_shapes) {
    if (bs.point_offsets.alloc_failed() || bs.normal_offsets.alloc_failed()) {
      return true;
    }
    for (const auto& inbetween : bs.inbetweens) {
      if (inbetween.point_offsets.alloc_failed()) return true;
    }
  }
  return false;
}

size_t RenderMesh::memory_usage() const {
  size_t total = sizeof(*this);
  total += face_vertex_counts.memory_usage();
  total += face_vertex_indices.memory_usage();
  total += points.memory_usage();
  total += normals.memory_usage();
  total += tangents.memory_usage();
  total += texcoords_0.memory_usage();
  total += texcoords_1.memory_usage();
  total += colors.memory_usage();
  total += opacities.memory_usage();
  total += triangulated_indices.memory_usage();

  for (const auto& pv : primvars) {
    total += pv.memory_usage();
  }

  if (skin) {
    total += skin->joint_indices.memory_usage();
    total += skin->joint_weights.memory_usage();
  }

  for (const auto& bs : blend_shapes) {
    total += bs.point_offsets.memory_usage();
    total += bs.normal_offsets.memory_usage();
    for (const auto& inbetween : bs.inbetweens) {
      total += inbetween.name.capacity();
      total += inbetween.point_offsets.memory_usage();
    }
  }

  return total;
}

//
// RenderPointInstancer
//

size_t RenderPoints::memory_usage() const {
  size_t total = sizeof(*this);
  total += name.capacity();
  total += prim_path.capacity();
  total += points.memory_usage();
  total += widths.memory_usage();
  total += colors.memory_usage();
  return total;
}

//
// RenderCurves
//

size_t RenderCurves::memory_usage() const {
  size_t total = sizeof(*this);
  total += name.capacity();
  total += prim_path.capacity();
  total += VectorBytes(curve_vertex_counts);
  total += points.memory_usage();
  total += widths.memory_usage();
  total += colors.memory_usage();
  total += VectorBytes(tessellated_vertex_counts);
  total += tessellated_points.memory_usage();
  total += tessellated_widths.memory_usage();
  total += tessellated_colors.memory_usage();
  return total;
}

//
// RenderPointInstancer
//

size_t RenderPointInstancer::memory_usage() const {
  size_t total = sizeof(*this);
  total += StringVectorBytes(prototype_paths);
  total += VectorBytes(prototype_node_ids);
  total += VectorBytes(prototype_mesh_offsets);
  total += VectorBytes(prototype_mesh_ids);
  total += VectorBytes(prototype_mesh_transforms);
  total += VectorBytes(proto_indices);
  total += VectorBytes(positions);
  total += VectorBytes(orientations);
  total += VectorBytes(scales);
  total += VectorBytes(velocities);
  total += VectorBytes(angular_velocities);
  total += VectorBytes(ids);
  total += VectorBytes(invisible_ids);
  total += VectorBytes(inactive_ids);
  total += VectorBytes(transforms);
  total += VectorBytes(instance_visible);
  total += name.capacity();
  total += prim_path.capacity();
  total += validation_error.capacity();
  return total;
}

size_t RenderPointInstancer::prototype_mesh_count(size_t prototype_index) const {
  if (prototype_index + 1 >= prototype_mesh_offsets.size()) return 0;
  return prototype_mesh_offsets[prototype_index + 1] -
         prototype_mesh_offsets[prototype_index];
}

bool RenderPointInstancer::has_valid_prototype_mesh_bindings() const {
  if (prototype_mesh_offsets.size() != prototype_paths.size() + 1) return false;
  if (prototype_node_ids.size() != prototype_paths.size()) return false;
  if (prototype_mesh_transforms.size() != prototype_mesh_ids.size()) return false;
  if (prototype_mesh_offsets.empty() || prototype_mesh_offsets[0] != 0) return false;
  uint32_t prev = 0;
  for (uint32_t offset : prototype_mesh_offsets) {
    if (offset < prev) return false;
    prev = offset;
  }
  return prototype_mesh_offsets.back() == prototype_mesh_ids.size();
}

size_t RenderPointInstancer::visible_instance_count() const {
  if (instance_visible.empty()) return instance_count();
  size_t total = 0;
  for (uint8_t visible : instance_visible) {
    if (visible) ++total;
  }
  return total;
}

bool RenderPointInstancer::has_valid_draw_range(size_t total_draw_count) const {
  const size_t start = draw_start;
  const size_t count = draw_count;
  return start <= total_draw_count && count <= total_draw_count - start;
}

//
// RenderCamera
//

float RenderCamera::fov_y() const {
  if (type == CameraType::Orthographic) {
    return 0.0f;
  }
  // fov_y = 2 * atan(vertical_aperture / (2 * focal_length))
  return 2.0f * std::atan(vertical_aperture / (2.0f * focal_length));
}

float RenderCamera::fov_x() const {
  if (type == CameraType::Orthographic) {
    return 0.0f;
  }
  return 2.0f * std::atan(horizontal_aperture / (2.0f * focal_length));
}

float RenderCamera::aspect_ratio() const {
  if (vertical_aperture <= 0.0f) return 1.0f;
  return horizontal_aperture / vertical_aperture;
}

//
// RenderScene
//

size_t RenderScene::memory_usage() const {
  size_t total = sizeof(*this);

  for (const auto& mesh : meshes) {
    total += mesh.memory_usage();
  }

  for (const auto& point_cloud : points) {
    total += point_cloud.memory_usage();
  }

  for (const auto& curve : curves) {
    total += curve.memory_usage();
  }

  for (const auto& instancer : point_instancers) {
    total += instancer.memory_usage();
  }

  for (const auto& img : images) {
    total += img.memory_usage();
  }

  // Estimate for other containers
  total += nodes.size() * sizeof(SceneNode);
  total += point_instance_draws.size() * sizeof(RenderPointInstanceDraw);
  total += materials.size() * sizeof(RenderMaterial);
  total += textures.size() * sizeof(RenderTexture);
  total += lights.size() * sizeof(RenderLight);
  total += cameras.size() * sizeof(RenderCamera);
  total += animations.size() * sizeof(AnimationClip);
  total += skeletons.size() * sizeof(Skeleton);
  total += unsupported_renderables.size() * sizeof(UnsupportedRenderable);

  return total;
}

const RenderMesh* RenderScene::get_mesh(int32_t mesh_id) const {
  if (mesh_id < 0 || static_cast<size_t>(mesh_id) >= meshes.size()) return nullptr;
  return &meshes[static_cast<size_t>(mesh_id)];
}

const RenderPoints* RenderScene::get_points(int32_t points_id) const {
  if (points_id < 0 || static_cast<size_t>(points_id) >= points.size()) return nullptr;
  return &points[static_cast<size_t>(points_id)];
}

const RenderCurves* RenderScene::get_curves(int32_t curves_id) const {
  if (curves_id < 0 || static_cast<size_t>(curves_id) >= curves.size()) {
    return nullptr;
  }
  return &curves[static_cast<size_t>(curves_id)];
}

const RenderMaterial* RenderScene::get_material(int32_t material_id) const {
  if (material_id < 0 ||
      static_cast<size_t>(material_id) >= materials.size()) {
    return nullptr;
  }
  return &materials[static_cast<size_t>(material_id)];
}

const RenderPointInstancer* RenderScene::get_point_instancer(
    int32_t instancer_id) const {
  if (instancer_id < 0 ||
      static_cast<size_t>(instancer_id) >= point_instancers.size()) {
    return nullptr;
  }
  return &point_instancers[static_cast<size_t>(instancer_id)];
}

const RenderPointInstanceDraw* RenderScene::get_point_instance_draw(
    size_t draw_id) const {
  if (draw_id >= point_instance_draws.size()) return nullptr;
  return &point_instance_draws[draw_id];
}

RenderPointInstanceDrawView RenderScene::get_point_instance_draw_view(
    size_t draw_id) const {
  RenderPointInstanceDrawView view;
  view.draw = get_point_instance_draw(draw_id);
  if (!view.draw) return view;
  view.instancer = get_point_instancer(view.draw->point_instancer_id);
  view.mesh = get_mesh(view.draw->mesh_id);
  view.expanded_mesh = get_mesh(view.draw->expanded_mesh_id);
  view.material = get_material(view.draw->material_id);
  return view;
}

RenderPointInstanceDrawRange RenderScene::get_point_instancer_draws(
    int32_t instancer_id) const {
  RenderPointInstanceDrawRange range;
  const RenderPointInstancer* instancer = get_point_instancer(instancer_id);
  if (!instancer || instancer->draw_count == 0) return range;
  const size_t start = instancer->draw_start;
  const size_t count = instancer->draw_count;
  if (start > point_instance_draws.size() ||
      count > point_instance_draws.size() - start) {
    return range;
  }
  range.data = point_instance_draws.data() + start;
  range.size = count;
  return range;
}

const SceneNode* RenderScene::get_node(int32_t node_id) const {
  if (node_id < 0 || static_cast<size_t>(node_id) >= nodes.size()) return nullptr;
  return &nodes[static_cast<size_t>(node_id)];
}

bool RenderScene::has_valid_point_instance_draw_ranges() const {
  for (size_t instancer_id = 0; instancer_id < point_instancers.size();
       ++instancer_id) {
    const RenderPointInstancer& instancer = point_instancers[instancer_id];
    if (!instancer.has_valid_draw_range(point_instance_draws.size())) {
      return false;
    }
    for (size_t draw_i = 0; draw_i < instancer.draw_count; ++draw_i) {
      const size_t draw_id = static_cast<size_t>(instancer.draw_start) + draw_i;
      if (draw_id >= point_instance_draws.size()) return false;
      if (point_instance_draws[draw_id].point_instancer_id !=
          static_cast<int32_t>(instancer_id)) {
        return false;
      }
    }
  }
  return true;
}

RenderScene::Stats RenderScene::get_stats() const {
  Stats s = {};
  s.node_count = nodes.size();
  s.mesh_count = meshes.size();
  s.points_count = points.size();
  s.point_instancer_count = point_instancers.size();
  s.point_instance_draw_count = point_instance_draws.size();
  s.material_count = materials.size();
  s.texture_count = textures.size();
  s.image_count = images.size();
  s.light_count = lights.size();
  s.camera_count = cameras.size();
  s.animation_count = animations.size();
  s.skeleton_count = skeletons.size();

  for (const auto& mesh : meshes) {
    s.total_vertices += mesh.point_count();

    // Count triangles
    if (mesh.is_triangulated) {
      s.total_triangles += mesh.triangulated_indices.size() / 3;
    } else {
      // Estimate from face counts
      for (size_t i = 0; i < mesh.face_vertex_counts.size(); ++i) {
        uint32_t nverts = mesh.face_vertex_counts[i];
        if (nverts >= 3) {
          s.total_triangles += nverts - 2;  // Triangle fan decomposition
        }
      }
    }
  }

  for (const auto& instancer : point_instancers) {
    s.point_instance_count += instancer.instance_count();
    s.visible_point_instance_count += instancer.visible_instance_count();
  }

  for (const auto& point_cloud : points) {
    s.point_cloud_point_count += point_cloud.point_count();
  }

  s.curves_count = curves.size();
  for (const auto& curve : curves) {
    s.curve_count += curve.curve_count();
    s.curve_tessellated_point_count += curve.tessellated_point_count();
  }

  s.memory_bytes = memory_usage();
  return s;
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
