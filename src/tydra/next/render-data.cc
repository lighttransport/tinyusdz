// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Data Implementation

#include "render-data.hh"
#include <cmath>

namespace tinyusdz {
namespace tydra {
namespace next {

//
// RenderMesh
//

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
  }

  return total;
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

  for (const auto& img : images) {
    total += img.memory_usage();
  }

  // Estimate for other containers
  total += nodes.size() * sizeof(SceneNode);
  total += materials.size() * sizeof(RenderMaterial);
  total += textures.size() * sizeof(RenderTexture);
  total += lights.size() * sizeof(RenderLight);
  total += cameras.size() * sizeof(RenderCamera);
  total += animations.size() * sizeof(AnimationClip);
  total += skeletons.size() * sizeof(Skeleton);

  return total;
}

RenderScene::Stats RenderScene::get_stats() const {
  Stats s = {};
  s.node_count = nodes.size();
  s.mesh_count = meshes.size();
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

  s.memory_bytes = memory_usage();
  return s;
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
