//
// Primvar interpolation implementation for subdivision surfaces
//
// Copyright (c) 2025, Lightweight USD contributors
// Licensed under MIT license
//

#ifndef TINYUSDZ_PRIMVAR_INTERPOLATION_HH_
#define TINYUSDZ_PRIMVAR_INTERPOLATION_HH_

#include "subdivision.hh"
#include <map>
#include <set>

namespace tinyusdz {
namespace subdiv {

///
/// Primvar interpolation utilities
///

// Helper to interpolate between values with weights (for vector types)
// Note: This template is not used in the current implementation,
// kept for future extensibility
template<typename T>
T InterpolateWeighted(const std::vector<T>& values,
                      const std::vector<uint32_t>& indices,
                      const std::vector<float>& weights) {
  T result{};

  for (size_t i = 0; i < indices.size() && i < weights.size(); ++i) {
    if (indices[i] < values.size()) {
      result = result + values[indices[i]] * weights[i];
    }
  }

  return result;
}

// Specializations for common types
template<>
inline float InterpolateWeighted<float>(const std::vector<float>& values,
                                        const std::vector<uint32_t>& indices,
                                        const std::vector<float>& weights) {
  float result = 0.0f;
  for (size_t i = 0; i < indices.size() && i < weights.size(); ++i) {
    if (indices[i] < values.size()) {
      result += values[indices[i]] * weights[i];
    }
  }
  return result;
}

// Vector types (stored as flat arrays)
struct Vec2 {
  float x, y;
  Vec2() : x(0), y(0) {}
  Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
  Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
};

struct Vec3 {
  float x, y, z;
  Vec3() : x(0), y(0), z(0) {}
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
  Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
};

struct Vec4 {
  float x, y, z, w;
  Vec4() : x(0), y(0), z(0), w(0) {}
  Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  Vec4 operator+(const Vec4& other) const { return Vec4(x + other.x, y + other.y, z + other.z, w + other.w); }
  Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
};

///
/// Catmull-Clark primvar interpolation
///
template<typename T>
SubdivResult CatmullClarkSubdivider::SubdividePrimvar(
    const PrimvarData<T>& input_primvar,
    const HalfEdgeMesh& input_mesh,
    const HalfEdgeMesh& output_mesh,
    PrimvarData<T>& output_primvar) {

  output_primvar.interpolation = input_primvar.interpolation;

  switch (input_primvar.interpolation) {
    case InterpolationType::Constant: {
      // Constants don't subdivide, just copy
      output_primvar.values = input_primvar.values;
      return SubdivResult(true);
    }

    case InterpolationType::Uniform: {
      // Per-face data
      // Each input face with n vertices produces n output faces
      output_primvar.values.clear();

      for (uint32_t face_idx = 0; face_idx < input_mesh.GetNumFaces(); ++face_idx) {
        if (face_idx >= input_primvar.values.size()) {
          return SubdivResult(false, "Uniform primvar size mismatch");
        }

        uint32_t count = input_mesh.face_vertex_counts[face_idx];

        // Replicate the value for each new quad
        for (uint32_t i = 0; i < count; ++i) {
          output_primvar.values.push_back(input_primvar.values[face_idx]);
        }
      }
      return SubdivResult(true);
    }

    case InterpolationType::Vertex:
    case InterpolationType::Varying: {
      // Per-vertex data - smooth interpolation
      // Output has: old vertices (updated), edge points, face points

      if (input_primvar.values.size() != input_mesh.GetNumVertices()) {
        return SubdivResult(false, "Vertex primvar size mismatch");
      }

      output_primvar.values.clear();
      output_primvar.values.reserve(output_mesh.GetNumVertices());

      // Build topology for interpolation
      std::vector<VertexInfo> vertex_info;
      BuildTopology(input_mesh, vertex_info);

      uint32_t num_input_verts = input_mesh.GetNumVertices();
      uint32_t num_input_faces = input_mesh.GetNumFaces();

      // Step 1: Update old vertices (same formula as positions)
      for (uint32_t v = 0; v < num_input_verts; ++v) {
        T value = input_primvar.values[v];

        if (vertex_info[v].is_boundary) {
          // Boundary: average with boundary neighbors
          T neighbor_avg{};
          uint32_t boundary_count = 0;

          for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
            if (vertex_info[adj_v].is_boundary) {
              neighbor_avg = neighbor_avg + input_primvar.values[adj_v];
              boundary_count++;
            }
          }

          if (boundary_count > 0) {
            neighbor_avg = neighbor_avg * (1.0f / static_cast<float>(boundary_count));
            value = value * 0.5f + neighbor_avg * 0.5f;
          }
        } else {
          // Interior: Catmull-Clark formula
          uint32_t n = vertex_info[v].valence;

          if (n > 0) {
            // Q = average of adjacent face values
            T q{};
            for (uint32_t face_idx : vertex_info[v].adjacent_faces) {
              // Face value = average of face vertices
              T face_val{};
              uint32_t face_count = input_mesh.face_vertex_counts[face_idx];

              uint32_t idx_offset = 0;
              for (uint32_t fi = 0; fi < face_idx; ++fi) {
                idx_offset += input_mesh.face_vertex_counts[fi];
              }

              for (uint32_t i = 0; i < face_count; ++i) {
                uint32_t fv = input_mesh.face_vertex_indices[idx_offset + i];
                face_val = face_val + input_primvar.values[fv];
              }
              face_val = face_val * (1.0f / static_cast<float>(face_count));
              q = q + face_val;
            }
            q = q * (1.0f / static_cast<float>(vertex_info[v].adjacent_faces.size()));

            // R = average of edge midpoints
            T r{};
            for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
              r = r + input_primvar.values[adj_v];
            }
            r = r * (1.0f / static_cast<float>(n));

            // Apply formula: (Q/n) + (2R/n) + (S(n-3)/n)
            float inv_n = 1.0f / static_cast<float>(n);
            value = q * inv_n + r * (2.0f * inv_n) + value * ((static_cast<float>(n) - 3.0f) * inv_n);
          }
        }

        output_primvar.values.push_back(value);
      }

      // Step 2: Compute edge point primvars
      std::map<EdgeKey, T> edge_values;
      std::map<EdgeKey, std::vector<uint32_t>> edge_faces;

      uint32_t idx_offset = 0;
      for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
        uint32_t count = input_mesh.face_vertex_counts[face_idx];

        for (uint32_t i = 0; i < count; ++i) {
          uint32_t v0 = input_mesh.face_vertex_indices[idx_offset + i];
          uint32_t v1 = input_mesh.face_vertex_indices[idx_offset + (i + 1) % count];

          EdgeKey edge(v0, v1);
          edge_faces[edge].push_back(face_idx);
        }
        idx_offset += count;
      }

      for (const auto& entry : edge_faces) {
        const EdgeKey& edge = entry.first;
        const std::vector<uint32_t>& adj_faces = entry.second;

        T edge_val = input_primvar.values[edge.v0] + input_primvar.values[edge.v1];

        // Add adjacent face values
        for (uint32_t face_idx : adj_faces) {
          T face_val{};
          uint32_t face_count = input_mesh.face_vertex_counts[face_idx];

          idx_offset = 0;
          for (uint32_t fi = 0; fi < face_idx; ++fi) {
            idx_offset += input_mesh.face_vertex_counts[fi];
          }

          for (uint32_t i = 0; i < face_count; ++i) {
            uint32_t fv = input_mesh.face_vertex_indices[idx_offset + i];
            face_val = face_val + input_primvar.values[fv];
          }
          face_val = face_val * (1.0f / static_cast<float>(face_count));
          edge_val = edge_val + face_val;
        }

        edge_val = edge_val * (1.0f / static_cast<float>(2 + adj_faces.size()));
        edge_values[edge] = edge_val;
        output_primvar.values.push_back(edge_val);
      }

      // Step 3: Compute face point primvars
      idx_offset = 0;
      for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
        uint32_t count = input_mesh.face_vertex_counts[face_idx];

        T face_val{};
        for (uint32_t i = 0; i < count; ++i) {
          uint32_t v = input_mesh.face_vertex_indices[idx_offset + i];
          face_val = face_val + input_primvar.values[v];
        }
        face_val = face_val * (1.0f / static_cast<float>(count));

        output_primvar.values.push_back(face_val);
        idx_offset += count;
      }

      return SubdivResult(true);
    }

    case InterpolationType::FaceVarying: {
      // Per-face-vertex data (like UVs)
      // This requires maintaining the face-vertex correspondence

      if (input_primvar.values.size() != input_mesh.face_vertex_indices.size()) {
        return SubdivResult(false, "FaceVarying primvar size mismatch");
      }

      // For now, use simple linear interpolation
      // TODO: Implement proper discontinuous interpolation
      output_primvar.values.clear();

      // Similar to vertex interpolation but per face-vertex
      // This is a simplified implementation
      return SubdivResult(false, "FaceVarying interpolation not yet fully implemented");
    }

    default:
      return SubdivResult(false, "Unknown interpolation type");
  }
}

///
/// Loop primvar interpolation
///
template<typename T>
SubdivResult LoopSubdivider::SubdividePrimvar(
    const PrimvarData<T>& input_primvar,
    const HalfEdgeMesh& input_mesh,
    const HalfEdgeMesh& output_mesh,
    PrimvarData<T>& output_primvar) {

  output_primvar.interpolation = input_primvar.interpolation;

  switch (input_primvar.interpolation) {
    case InterpolationType::Constant: {
      output_primvar.values = input_primvar.values;
      return SubdivResult(true);
    }

    case InterpolationType::Uniform: {
      // Per-face: each triangle becomes 4 triangles
      output_primvar.values.clear();

      for (uint32_t face_idx = 0; face_idx < input_mesh.GetNumFaces(); ++face_idx) {
        if (face_idx >= input_primvar.values.size()) {
          return SubdivResult(false, "Uniform primvar size mismatch");
        }

        // Each triangle produces 4 triangles
        for (uint32_t i = 0; i < 4; ++i) {
          output_primvar.values.push_back(input_primvar.values[face_idx]);
        }
      }
      return SubdivResult(true);
    }

    case InterpolationType::Vertex:
    case InterpolationType::Varying: {
      if (input_primvar.values.size() != input_mesh.GetNumVertices()) {
        return SubdivResult(false, "Vertex primvar size mismatch");
      }

      output_primvar.values.clear();

      // Build topology
      std::vector<VertexInfo> vertex_info;
      BuildTopology(input_mesh, vertex_info);

      uint32_t num_input_verts = input_mesh.GetNumVertices();
      uint32_t num_input_faces = input_mesh.GetNumFaces();

      // Step 1: Update old vertices
      for (uint32_t v = 0; v < num_input_verts; ++v) {
        T value = input_primvar.values[v];

        if (vertex_info[v].is_boundary) {
          // Boundary
          T neighbor_avg{};
          uint32_t boundary_count = 0;

          for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
            if (vertex_info[adj_v].is_boundary) {
              neighbor_avg = neighbor_avg + input_primvar.values[adj_v];
              boundary_count++;
            }
          }

          if (boundary_count > 0) {
            neighbor_avg = neighbor_avg * (1.0f / static_cast<float>(boundary_count));
            value = value * 0.5f + neighbor_avg * 0.5f;
          }
        } else {
          // Interior: Loop formula
          uint32_t n = vertex_info[v].valence;

          if (n > 0) {
            float beta = ComputeBeta(n);
            float alpha = 1.0f - static_cast<float>(n) * beta;

            T neighbor_sum{};
            for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
              neighbor_sum = neighbor_sum + input_primvar.values[adj_v];
            }

            value = value * alpha + neighbor_sum * beta;
          }
        }

        output_primvar.values.push_back(value);
      }

      // Step 2: Compute edge point primvars
      std::map<EdgeKey, std::vector<uint32_t>> edge_faces;
      std::map<EdgeKey, std::pair<uint32_t, uint32_t>> edge_endpoints;
      std::map<EdgeKey, std::vector<uint32_t>> edge_opposite_verts;

      uint32_t idx_offset = 0;
      for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
        for (uint32_t i = 0; i < 3; ++i) {
          uint32_t v0 = input_mesh.face_vertex_indices[idx_offset + i];
          uint32_t v1 = input_mesh.face_vertex_indices[idx_offset + (i + 1) % 3];
          uint32_t v_opposite = input_mesh.face_vertex_indices[idx_offset + (i + 2) % 3];

          EdgeKey edge(v0, v1);
          edge_faces[edge].push_back(face_idx);
          edge_endpoints[edge] = std::make_pair(v0, v1);
          edge_opposite_verts[edge].push_back(v_opposite);
        }
        idx_offset += 3;
      }

      for (const auto& entry : edge_endpoints) {
        const EdgeKey& edge = entry.first;
        uint32_t v0 = entry.second.first;
        uint32_t v1 = entry.second.second;

        T edge_val;

        bool is_boundary = (edge_faces[edge].size() == 1);

        if (is_boundary) {
          // Boundary: simple average
          edge_val = (input_primvar.values[v0] + input_primvar.values[v1]) * 0.5f;
        } else {
          // Interior: (3/8)(A+B) + (1/8)(C+D)
          const std::vector<uint32_t>& opposite = edge_opposite_verts[edge];

          edge_val = (input_primvar.values[v0] + input_primvar.values[v1]) * 0.375f;

          for (uint32_t v_opp : opposite) {
            edge_val = edge_val + input_primvar.values[v_opp] * 0.125f;
          }
        }

        output_primvar.values.push_back(edge_val);
      }

      return SubdivResult(true);
    }

    case InterpolationType::FaceVarying: {
      return SubdivResult(false, "FaceVarying interpolation not yet fully implemented");
    }

    default:
      return SubdivResult(false, "Unknown interpolation type");
  }
}

///
/// Bilinear primvar interpolation
///
template<typename T>
SubdivResult BilinearSubdivider::SubdividePrimvar(
    const PrimvarData<T>& input_primvar,
    const HalfEdgeMesh& input_mesh,
    const HalfEdgeMesh& output_mesh,
    PrimvarData<T>& output_primvar) {

  output_primvar.interpolation = input_primvar.interpolation;

  switch (input_primvar.interpolation) {
    case InterpolationType::Constant: {
      output_primvar.values = input_primvar.values;
      return SubdivResult(true);
    }

    case InterpolationType::Uniform: {
      // Per-face: replicate for each sub-face
      output_primvar.values.clear();

      for (uint32_t face_idx = 0; face_idx < input_mesh.GetNumFaces(); ++face_idx) {
        if (face_idx >= input_primvar.values.size()) {
          return SubdivResult(false, "Uniform primvar size mismatch");
        }

        uint32_t count = input_mesh.face_vertex_counts[face_idx];

        // Each face produces 'count' sub-faces (plus 1 center for triangles)
        for (uint32_t i = 0; i < count; ++i) {
          output_primvar.values.push_back(input_primvar.values[face_idx]);
        }

        // Center triangle for triangular faces
        if (count == 3) {
          output_primvar.values.push_back(input_primvar.values[face_idx]);
        }
      }
      return SubdivResult(true);
    }

    case InterpolationType::Vertex:
    case InterpolationType::Varying: {
      if (input_primvar.values.size() != input_mesh.GetNumVertices()) {
        return SubdivResult(false, "Vertex primvar size mismatch");
      }

      output_primvar.values.clear();

      uint32_t num_input_verts = input_mesh.GetNumVertices();
      uint32_t num_input_faces = input_mesh.GetNumFaces();

      // Step 1: Original vertices (unchanged)
      for (uint32_t v = 0; v < num_input_verts; ++v) {
        output_primvar.values.push_back(input_primvar.values[v]);
      }

      // Step 2: Edge midpoints (linear interpolation)
      std::map<EdgeKey, T> edge_values;
      std::set<EdgeKey> processed_edges;

      uint32_t idx_offset = 0;
      for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
        uint32_t count = input_mesh.face_vertex_counts[face_idx];

        for (uint32_t i = 0; i < count; ++i) {
          uint32_t v0 = input_mesh.face_vertex_indices[idx_offset + i];
          uint32_t v1 = input_mesh.face_vertex_indices[idx_offset + (i + 1) % count];

          EdgeKey edge(v0, v1);

          if (processed_edges.find(edge) == processed_edges.end()) {
            // Linear interpolation: (v0 + v1) / 2
            T edge_val = (input_primvar.values[v0] + input_primvar.values[v1]) * 0.5f;
            edge_values[edge] = edge_val;
            output_primvar.values.push_back(edge_val);
            processed_edges.insert(edge);
          }
        }
        idx_offset += count;
      }

      // Step 3: Face centers (average of face vertices)
      idx_offset = 0;
      for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
        uint32_t count = input_mesh.face_vertex_counts[face_idx];

        T face_val{};
        for (uint32_t i = 0; i < count; ++i) {
          uint32_t v = input_mesh.face_vertex_indices[idx_offset + i];
          face_val = face_val + input_primvar.values[v];
        }
        face_val = face_val * (1.0f / static_cast<float>(count));

        output_primvar.values.push_back(face_val);
        idx_offset += count;
      }

      return SubdivResult(true);
    }

    case InterpolationType::FaceVarying: {
      return SubdivResult(false, "FaceVarying interpolation not yet fully implemented");
    }

    default:
      return SubdivResult(false, "Unknown interpolation type");
  }
}

}  // namespace subdiv
}  // namespace tinyusdz

#endif  // TINYUSDZ_PRIMVAR_INTERPOLATION_HH_
