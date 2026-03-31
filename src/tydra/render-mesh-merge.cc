// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-mesh-merge.cc
/// @brief Mesh merging optimization for RenderSceneConverter
///

#include "render-data.hh"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>

#include "common-utils.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "value-types.hh"

#include "common-macros.inc"

namespace tinyusdz {
namespace tydra {

namespace {

// Helper function to transform a vec3 point by a matrix4d
vec3 TransformPoint(const value::matrix4d &m, const vec3 &p) {
  // Apply full 4x4 transform (position)
  double x = m.m[0][0] * double(p[0]) + m.m[1][0] * double(p[1]) + m.m[2][0] * double(p[2]) + m.m[3][0];
  double y = m.m[0][1] * double(p[0]) + m.m[1][1] * double(p[1]) + m.m[2][1] * double(p[2]) + m.m[3][1];
  double z = m.m[0][2] * double(p[0]) + m.m[1][2] * double(p[1]) + m.m[2][2] * double(p[2]) + m.m[3][2];
  double w = m.m[0][3] * double(p[0]) + m.m[1][3] * double(p[1]) + m.m[2][3] * double(p[2]) + m.m[3][3];

  if (std::abs(w) > 1e-10) {
    x /= w;
    y /= w;
    z /= w;
  }

  return vec3{float(x), float(y), float(z)};
}

// Helper function to transform a vec3 direction (normal) by a matrix4d
// Uses the upper-left 3x3 of the inverse-transpose for correct normal transformation
vec3 TransformNormal(const value::matrix4d &m, const vec3 &n) {
  // For normals, we need the inverse transpose of the upper-left 3x3
  // For now, we use the upper-left 3x3 directly (correct for uniform scale and rotation only)
  // TODO: Proper inverse-transpose for non-uniform scale
  double x = m.m[0][0] * double(n[0]) + m.m[1][0] * double(n[1]) + m.m[2][0] * double(n[2]);
  double y = m.m[0][1] * double(n[0]) + m.m[1][1] * double(n[1]) + m.m[2][1] * double(n[2]);
  double z = m.m[0][2] * double(n[0]) + m.m[1][2] * double(n[1]) + m.m[2][2] * double(n[2]);

  // Normalize the result
  double len = std::sqrt(x*x + y*y + z*z);
  if (len > 1e-10) {
    x /= len;
    y /= len;
    z /= len;
  }

  return vec3{float(x), float(y), float(z)};
}

}  // namespace

bool RenderSceneConverter::IsMeshMergeable(const RenderMesh &mesh) const {
  // Mesh cannot be merged if:
  // 1. Has skeletal animation
  if (mesh.skel_id >= 0) {
    return false;
  }

  // 2. Has blend shapes
  if (!mesh.targets.empty()) {
    return false;
  }

  // 3. Has per-face materials (GeomSubset)
  if (!mesh.material_subsetMap.empty()) {
    return false;
  }

  // 4. Is an area light (special rendering)
  if (mesh.is_area_light) {
    return false;
  }

  return true;
}

bool RenderSceneConverter::MergeMeshData(const RenderMesh &src,
                                          const value::matrix4d &src_transform,
                                          RenderMesh &dst) {
  // Check if transform is identity using tinyusdz::is_identity function
  bool transform_is_identity = tinyusdz::is_identity(src_transform);

  // Get the vertex offset for index adjustment
  uint32_t vertex_offset = static_cast<uint32_t>(dst.points.size());

  // Merge points (with transform if needed)
  if (transform_is_identity) {
    dst.points.insert(dst.points.end(), src.points.begin(), src.points.end());
  } else {
    for (const auto &p : src.points) {
      dst.points.push_back(TransformPoint(src_transform, p));
    }
  }

  // Merge face vertex indices (adjust by vertex offset)
  for (uint32_t idx : src.usdFaceVertexIndices) {
    dst.usdFaceVertexIndices.push_back(idx + vertex_offset);
  }

  // Merge face vertex counts
  dst.usdFaceVertexCounts.insert(dst.usdFaceVertexCounts.end(),
                                  src.usdFaceVertexCounts.begin(),
                                  src.usdFaceVertexCounts.end());

  // Merge triangulated indices if present
  if (!src.triangulatedFaceVertexIndices.empty()) {
    for (uint32_t idx : src.triangulatedFaceVertexIndices) {
      dst.triangulatedFaceVertexIndices.push_back(idx + vertex_offset);
    }
    dst.triangulatedFaceVertexCounts.insert(dst.triangulatedFaceVertexCounts.end(),
                                             src.triangulatedFaceVertexCounts.begin(),
                                             src.triangulatedFaceVertexCounts.end());
  }

  // Merge normals (transform direction if needed)
  if (!src.normals.empty()) {
    size_t src_normal_count = src.normals.vertex_count();

    // Ensure dst normals has same format
    if (dst.normals.empty()) {
      dst.normals = src.normals;
      if (!transform_is_identity) {
        // Transform the normals we just copied
        vec3 *normals_data = reinterpret_cast<vec3*>(dst.normals.data.data());
        for (size_t i = 0; i < src_normal_count; i++) {
          normals_data[i] = TransformNormal(src_transform, normals_data[i]);
        }
      }
    } else {
      // Append normals
      size_t old_size = dst.normals.data.size();
      dst.normals.data.resize(old_size + src.normals.data.size());

      if (transform_is_identity) {
        memcpy(dst.normals.data.data() + old_size, src.normals.data.data(), src.normals.data.size());
      } else {
        const vec3 *src_normals = reinterpret_cast<const vec3*>(src.normals.data.data());
        vec3 *dst_normals = reinterpret_cast<vec3*>(dst.normals.data.data() + old_size);
        for (size_t i = 0; i < src_normal_count; i++) {
          dst_normals[i] = TransformNormal(src_transform, src_normals[i]);
        }
      }
    }
  }

  // Merge texcoords (no transform needed)
  for (const auto &src_tc : src.texcoords) {
    uint32_t slot = src_tc.first;
    const auto &src_attr = src_tc.second;

    if (dst.texcoords.count(slot) == 0) {
      dst.texcoords[slot] = src_attr;
    } else {
      auto &dst_attr = dst.texcoords[slot];
      size_t old_size = dst_attr.data.size();
      dst_attr.data.resize(old_size + src_attr.data.size());
      memcpy(dst_attr.data.data() + old_size, src_attr.data.data(), src_attr.data.size());
    }
  }

  // Merge tangents (transform direction if needed)
  if (!src.tangents.empty()) {
    if (dst.tangents.empty()) {
      dst.tangents = src.tangents;
      if (!transform_is_identity) {
        vec3 *tangents_data = reinterpret_cast<vec3*>(dst.tangents.data.data());
        size_t count = dst.tangents.vertex_count();
        for (size_t i = 0; i < count; i++) {
          tangents_data[i] = TransformNormal(src_transform, tangents_data[i]);
        }
      }
    } else {
      size_t old_size = dst.tangents.data.size();
      size_t src_count = src.tangents.vertex_count();
      dst.tangents.data.resize(old_size + src.tangents.data.size());

      if (transform_is_identity) {
        memcpy(dst.tangents.data.data() + old_size, src.tangents.data.data(), src.tangents.data.size());
      } else {
        const vec3 *src_tangents = reinterpret_cast<const vec3*>(src.tangents.data.data());
        vec3 *dst_tangents = reinterpret_cast<vec3*>(dst.tangents.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_tangents[i] = TransformNormal(src_transform, src_tangents[i]);
        }
      }
    }
  }

  // Merge binormals (transform direction if needed)
  if (!src.binormals.empty()) {
    if (dst.binormals.empty()) {
      dst.binormals = src.binormals;
      if (!transform_is_identity) {
        vec3 *binormals_data = reinterpret_cast<vec3*>(dst.binormals.data.data());
        size_t count = dst.binormals.vertex_count();
        for (size_t i = 0; i < count; i++) {
          binormals_data[i] = TransformNormal(src_transform, binormals_data[i]);
        }
      }
    } else {
      size_t old_size = dst.binormals.data.size();
      size_t src_count = src.binormals.vertex_count();
      dst.binormals.data.resize(old_size + src.binormals.data.size());

      if (transform_is_identity) {
        memcpy(dst.binormals.data.data() + old_size, src.binormals.data.data(), src.binormals.data.size());
      } else {
        const vec3 *src_binormals = reinterpret_cast<const vec3*>(src.binormals.data.data());
        vec3 *dst_binormals = reinterpret_cast<vec3*>(dst.binormals.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_binormals[i] = TransformNormal(src_transform, src_binormals[i]);
        }
      }
    }
  }

  // Merge vertex colors
  if (!src.vertex_colors.empty()) {
    if (dst.vertex_colors.empty()) {
      dst.vertex_colors = src.vertex_colors;
    } else {
      size_t old_size = dst.vertex_colors.data.size();
      dst.vertex_colors.data.resize(old_size + src.vertex_colors.data.size());
      memcpy(dst.vertex_colors.data.data() + old_size, src.vertex_colors.data.data(), src.vertex_colors.data.size());
    }
  }

  // Merge vertex opacities
  if (!src.vertex_opacities.empty()) {
    if (dst.vertex_opacities.empty()) {
      dst.vertex_opacities = src.vertex_opacities;
    } else {
      size_t old_size = dst.vertex_opacities.data.size();
      dst.vertex_opacities.data.resize(old_size + src.vertex_opacities.data.size());
      memcpy(dst.vertex_opacities.data.data() + old_size, src.vertex_opacities.data.data(), src.vertex_opacities.data.size());
    }
  }

  return true;
}

bool RenderSceneConverter::MergeMeshesImpl(const RenderSceneConverterEnv &env) {
  if (!env.scene_config.merge_meshes) {
    return true;  // Merging disabled, nothing to do
  }

  DCOUT("MergeMeshesImpl: Starting mesh merge...");

  // Build a map from mesh to its node and global transform
  struct MeshNodeInfo {
    Node *node{nullptr};
    value::matrix4d global_matrix;
    size_t mesh_index{0};
  };

  std::vector<MeshNodeInfo> mesh_node_infos;
  mesh_node_infos.resize(meshes.size());

  // Helper to traverse nodes and collect mesh info
  std::function<void(Node &)> collectMeshNodes = [&](Node &node) {
    if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
        size_t(node.id) < meshes.size()) {
      mesh_node_infos[size_t(node.id)].node = &node;
      mesh_node_infos[size_t(node.id)].global_matrix = node.global_matrix;
      mesh_node_infos[size_t(node.id)].mesh_index = size_t(node.id);
    }
    for (auto &child : node.children) {
      collectMeshNodes(child);
    }
  };

  for (auto &root : root_nodes) {
    collectMeshNodes(root);
  }

  // Group meshes by material_id
  // Only include meshes that are mergeable
  std::map<int, std::vector<size_t>> material_to_meshes;

  for (size_t i = 0; i < meshes.size(); i++) {
    const auto &mesh = meshes[i];
    if (!IsMeshMergeable(mesh)) {
      continue;
    }

    // Skip meshes that don't have a node (shouldn't happen but be safe)
    if (!mesh_node_infos[i].node) {
      continue;
    }

    material_to_meshes[mesh.material_id].push_back(i);
  }

  // For each material group with 2+ meshes, merge them
  std::vector<RenderMesh> merged_meshes;
  std::map<size_t, int32_t> old_to_new_mesh_id;  // old mesh index -> new merged mesh index
  std::set<size_t> meshes_to_remove;

  for (auto &kv : material_to_meshes) {
    int material_id = kv.first;
    auto &mesh_indices = kv.second;

    if (mesh_indices.size() < 2) {
      // Only one mesh with this material, no merging needed
      continue;
    }

    DCOUT("Merging " << mesh_indices.size() << " meshes with material_id=" << material_id);

    // Check if all meshes have the same global transform (when bake_transform is false)
    bool can_merge = true;
    if (!env.scene_config.merge_meshes_bake_transform) {
      const auto &first_matrix = mesh_node_infos[mesh_indices[0]].global_matrix;
      for (size_t i = 1; i < mesh_indices.size(); i++) {
        const auto &matrix = mesh_node_infos[mesh_indices[i]].global_matrix;
        // Compare matrices (with epsilon)
        bool same_transform = true;
        for (int r = 0; r < 4 && same_transform; r++) {
          for (int c = 0; c < 4 && same_transform; c++) {
            if (std::abs(first_matrix.m[r][c] - matrix.m[r][c]) > 1e-6) {
              same_transform = false;
            }
          }
        }
        if (!same_transform) {
          can_merge = false;
          break;
        }
      }
    }

    if (!can_merge) {
      DCOUT("Cannot merge meshes with material_id=" << material_id << " - different transforms");
      continue;
    }

    // Create merged mesh
    RenderMesh merged;
    merged.prim_name = "merged_material_" + std::to_string(material_id);
    merged.abs_path = "/merged/" + merged.prim_name;
    merged.display_name = "Merged mesh (material " + std::to_string(material_id) + ")";
    merged.material_id = material_id;

    // Copy properties from first mesh
    const auto &first_mesh = meshes[mesh_indices[0]];
    merged.doubleSided = first_mesh.doubleSided;
    merged.displayColor = first_mesh.displayColor;
    merged.displayOpacity = first_mesh.displayOpacity;
    merged.is_rightHanded = first_mesh.is_rightHanded;

    // If baking transforms, we transform all vertices to world space
    // The merged mesh will have identity transform

    for (size_t idx : mesh_indices) {
      const auto &src_mesh = meshes[idx];
      const auto &node_info = mesh_node_infos[idx];

      value::matrix4d relative_transform;
      if (env.scene_config.merge_meshes_bake_transform) {
        // Use world space transform
        relative_transform = node_info.global_matrix;
      } else {
        // All transforms should be the same (checked above)
        relative_transform = value::matrix4d::identity();
      }

      if (!MergeMeshData(src_mesh, relative_transform, merged)) {
        PUSH_WARN("Failed to merge mesh " + src_mesh.abs_path);
        continue;
      }

      meshes_to_remove.insert(idx);
    }

    // The merged mesh is either in world space (if bake_transform) or
    // shares the transform of the first mesh
    merged.is_single_indexable = first_mesh.is_single_indexable;

    // Add merged mesh
    size_t new_mesh_index = meshes.size() + merged_meshes.size();
    merged_meshes.push_back(std::move(merged));

    // Map old mesh indices to new merged mesh index
    for (size_t idx : mesh_indices) {
      old_to_new_mesh_id[idx] = static_cast<int32_t>(new_mesh_index);
    }
  }

  if (merged_meshes.empty()) {
    DCOUT("No meshes were merged");
    return true;
  }

  DCOUT("Created " << merged_meshes.size() << " merged meshes from " << meshes_to_remove.size() << " source meshes");

  // Add merged meshes to the mesh array
  for (auto &mm : merged_meshes) {
    meshes.push_back(std::move(mm));
  }

  // Update node references
  // For merged meshes, we keep only the first node pointing to the merged mesh
  // and invalidate the other nodes (set id = -1)
  std::set<int32_t> used_merged_ids;

  std::function<void(Node &)> updateNodeMeshRefs = [&](Node &node) {
    if (node.nodeType == NodeType::Mesh && node.id >= 0) {
      size_t old_id = size_t(node.id);
      auto it = old_to_new_mesh_id.find(old_id);
      if (it != old_to_new_mesh_id.end()) {
        int32_t new_id = it->second;
        if (used_merged_ids.count(new_id) == 0) {
          // First node for this merged mesh - update to point to merged mesh
          node.id = new_id;
          used_merged_ids.insert(new_id);

          // If we baked transforms, reset the node's transform to identity
          if (env.scene_config.merge_meshes_bake_transform) {
            node.local_matrix = value::matrix4d::identity();
            node.global_matrix = value::matrix4d::identity();
          }
        } else {
          // This mesh was merged and this is not the first node
          // Mark as invalid (mesh is now part of merged mesh)
          node.id = -1;
        }
      }
    }
    for (auto &child : node.children) {
      updateNodeMeshRefs(child);
    }
  };

  for (auto &root : root_nodes) {
    updateNodeMeshRefs(root);
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
