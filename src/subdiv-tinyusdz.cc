//
// TinyUSDZ subdivision surface implementation
// Using dependency-free subdivision library
//
// SPDX-License-Identifier: Apache 2.0 OR MIT
// Copyright 2025 Light Transport Entertainment Inc.
//

#include <cmath>
#include <unordered_map>
#include "subdiv.hh"
#include "subdivision.hh"

namespace tinyusdz {

using namespace subdiv;

// Convert ControlQuadMesh to our HalfEdgeMesh format
static bool ConvertToHalfEdge(const ControlQuadMesh &control_mesh,
                             HalfEdgeMesh &half_edge_mesh,
                             std::string *err) {
  // Convert vertices (float array with stride 3)
  std::vector<float> points;
  points.reserve(control_mesh.vertices.size());
  for (float v : control_mesh.vertices) {
    points.push_back(v);
  }

  // Convert face vertex counts and indices
  std::vector<uint32_t> face_vertex_counts;
  std::vector<uint32_t> face_vertex_indices;

  face_vertex_counts.reserve(control_mesh.verts_per_faces.size());
  for (int count : control_mesh.verts_per_faces) {
    if (count < 3) {
      if (err) {
        *err = "Face must have at least 3 vertices";
      }
      return false;
    }
    face_vertex_counts.push_back(static_cast<uint32_t>(count));
  }

  face_vertex_indices.reserve(control_mesh.indices.size());
  for (int idx : control_mesh.indices) {
    if (idx < 0) {
      if (err) {
        *err = "Negative vertex index not allowed";
      }
      return false;
    }
    face_vertex_indices.push_back(static_cast<uint32_t>(idx));
  }

  // Use conversion function
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, half_edge_mesh);

  if (!result.success) {
    if (err) {
      *err = result.error;
    }
    return false;
  }

  return true;
}

// Triangulate a subdivided mesh
static void TriangulateMesh(const HalfEdgeMesh &mesh, SubdividedMesh *out) {
  out->vertices.clear();
  out->triangulated_indices.clear();
  out->face_num_verts.clear();
  out->face_indices.clear();
  out->face_index_offsets.clear();
  out->face_ids.clear();
  out->face_triangle_ids.clear();

  // Copy vertices
  out->vertices = mesh.points;

  // Triangulate faces
  uint32_t idx_offset = 0;

  for (uint32_t face_idx = 0; face_idx < mesh.GetNumFaces(); ++face_idx) {
    uint32_t count = mesh.face_vertex_counts[face_idx];

    // Store original face info
    out->face_num_verts.push_back(static_cast<uint8_t>(count));
    out->face_index_offsets.push_back(static_cast<uint32_t>(out->face_indices.size()));

    for (uint32_t i = 0; i < count; ++i) {
      out->face_indices.push_back(mesh.face_vertex_indices[idx_offset + i]);
    }

    // Triangulate: fan triangulation from first vertex
    if (count == 3) {
      // Already a triangle
      out->triangulated_indices.push_back(mesh.face_vertex_indices[idx_offset + 0]);
      out->triangulated_indices.push_back(mesh.face_vertex_indices[idx_offset + 1]);
      out->triangulated_indices.push_back(mesh.face_vertex_indices[idx_offset + 2]);
      out->face_ids.push_back(face_idx);
      out->face_triangle_ids.push_back(0);
    } else {
      // Fan triangulation for quads and n-gons
      for (uint32_t i = 1; i + 1 < count; ++i) {
        out->triangulated_indices.push_back(mesh.face_vertex_indices[idx_offset + 0]);
        out->triangulated_indices.push_back(mesh.face_vertex_indices[idx_offset + i]);
        out->triangulated_indices.push_back(mesh.face_vertex_indices[idx_offset + i + 1]);
        out->face_ids.push_back(face_idx);
        out->face_triangle_ids.push_back(static_cast<uint8_t>(i - 1));
      }
    }

    idx_offset += count;
  }
}

// Compute face-varying normals for triangulated mesh
static void ComputeFaceVaryingNormals(SubdividedMesh *mesh) {
  mesh->facevarying_normals.clear();

  size_t num_triangles = mesh->triangulated_indices.size() / 3;
  mesh->facevarying_normals.reserve(num_triangles * 9);  // 3 vertices * 3 components

  for (size_t tri = 0; tri < num_triangles; ++tri) {
    uint32_t i0 = mesh->triangulated_indices[tri * 3 + 0];
    uint32_t i1 = mesh->triangulated_indices[tri * 3 + 1];
    uint32_t i2 = mesh->triangulated_indices[tri * 3 + 2];

    float v0[3] = {
      mesh->vertices[i0 * 3 + 0],
      mesh->vertices[i0 * 3 + 1],
      mesh->vertices[i0 * 3 + 2]
    };
    float v1[3] = {
      mesh->vertices[i1 * 3 + 0],
      mesh->vertices[i1 * 3 + 1],
      mesh->vertices[i1 * 3 + 2]
    };
    float v2[3] = {
      mesh->vertices[i2 * 3 + 0],
      mesh->vertices[i2 * 3 + 1],
      mesh->vertices[i2 * 3 + 2]
    };

    // Compute edges
    float e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
    float e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};

    // Cross product
    float n[3] = {
      e1[1] * e2[2] - e1[2] * e2[1],
      e1[2] * e2[0] - e1[0] * e2[2],
      e1[0] * e2[1] - e1[1] * e2[0]
    };

    // Normalize
    float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (length > 1e-8f) {
      n[0] /= length;
      n[1] /= length;
      n[2] /= length;
    }

    // Store same normal for all 3 vertices (flat shading)
    for (int v = 0; v < 3; ++v) {
      mesh->facevarying_normals.push_back(n[0]);
      mesh->facevarying_normals.push_back(n[1]);
      mesh->facevarying_normals.push_back(n[2]);
    }
  }
}

bool subdivide(int subd_level, const ControlQuadMesh &in_mesh,
               SubdividedMesh *out_mesh, std::string *err,
               subdiv::SubdivisionScheme scheme, bool dump) {
  if (!out_mesh) {
    if (err) {
      *err = "Output mesh pointer is null";
    }
    return false;
  }

  if (subd_level < 0) {
    subd_level = 0;
  }

  if (subd_level > 8) {
    subd_level = 8;  // Security limit
  }

  // Convert input mesh to our format
  HalfEdgeMesh input_half_edge;
  if (!ConvertToHalfEdge(in_mesh, input_half_edge, err)) {
    return false;
  }

  HalfEdgeMesh subdivided_mesh;
  if (subd_level == 0) {
    subdivided_mesh = input_half_edge;
  } else {
    SubdivResult result(false, "Unknown subdivision scheme");

    switch (scheme) {
      case subdiv::SubdivisionScheme::CatmullClark: {
        CatmullClarkSubdivider subdivider;
        subdivider.SetBoundaryInterpolation(BoundaryInterpolation::EdgeOnly);
        result = subdivider.Subdivide(input_half_edge, subdivided_mesh, subd_level);
        break;
      }
      case subdiv::SubdivisionScheme::Loop: {
        LoopSubdivider subdivider;
        subdivider.SetBoundaryInterpolation(BoundaryInterpolation::EdgeOnly);
        result = subdivider.Subdivide(input_half_edge, subdivided_mesh, subd_level);
        break;
      }
      case subdiv::SubdivisionScheme::Bilinear: {
        BilinearSubdivider subdivider;
        result = subdivider.Subdivide(input_half_edge, subdivided_mesh, subd_level);
        break;
      }
    }

    if (!result.success) {
      if (err) {
        *err = "Subdivision failed: " + result.error;
      }
      return false;
    }
  }

  // Triangulate the subdivided mesh
  TriangulateMesh(subdivided_mesh, out_mesh);

  // Compute normals
  ComputeFaceVaryingNormals(out_mesh);

  // Handle UVs if present
  if (!in_mesh.faevarying_uvs.empty() &&
      !in_mesh.facevarying_uv_indices.empty()) {
    // TODO: Implement UV subdivision using primvar interpolation
    // For now, skip UV interpolation
  }

  // Debug output
  if (dump) {
    // Optional: Write .obj for debugging
  }

  return true;
}

}  // namespace tinyusdz
