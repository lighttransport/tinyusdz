//
// Dependency-free subdivision surface library for USD
//
// Copyright (c) 2025, Lightweight USD contributors
// Licensed under MIT license
//

#include "subdivision.hh"

#include <algorithm>
#include <map>
#include <set>
#include <cstring>

namespace tinyusdz {
namespace subdiv {

// ============================================================================
// HalfEdgeMesh Implementation
// ============================================================================

bool HalfEdgeMesh::IsValid() const {
  // Check for null data
  if (face_vertex_counts.empty() || face_vertex_indices.empty() || points.empty()) {
    return false;
  }

  // Check points are vec3
  if (points.size() % 3 != 0) {
    return false;
  }

  // Security checks
  if (GetNumVertices() > kMaxVertices || GetNumFaces() > kMaxFaces) {
    return false;
  }

  // Validate face vertex counts sum
  uint64_t sum = 0;
  for (uint32_t count : face_vertex_counts) {
    if (count < 3 || count > kMaxFaceVertices) {
      return false;  // Face must have at least 3 vertices
    }
    sum += count;
  }

  if (sum != face_vertex_indices.size()) {
    return false;  // Mismatch between counts and indices
  }

  // Validate all indices are in range
  uint32_t num_verts = GetNumVertices();
  for (uint32_t idx : face_vertex_indices) {
    if (idx >= num_verts) {
      return false;  // Out of range index
    }
  }

  return true;
}

// ============================================================================
// Utility Functions
// ============================================================================

SubdivResult ConvertToHalfEdgeMesh(
    const std::vector<uint32_t>& face_vertex_counts,
    const std::vector<uint32_t>& face_vertex_indices,
    const std::vector<float>& points,
    HalfEdgeMesh& mesh) {

  mesh.face_vertex_counts = face_vertex_counts;
  mesh.face_vertex_indices = face_vertex_indices;
  mesh.points = points;

  if (!mesh.IsValid()) {
    return SubdivResult(false, "Invalid mesh data");
  }

  // Detect boundary vertices and edges
  uint32_t num_verts = mesh.GetNumVertices();
  mesh.vertex_on_boundary.resize(num_verts, false);

  // Build edge map to detect boundaries (edges used by only one face)
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> edge_usage;

  uint32_t idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < mesh.GetNumFaces(); ++face_idx) {
    uint32_t count = face_vertex_counts[face_idx];

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v0 = face_vertex_indices[idx_offset + i];
      uint32_t v1 = face_vertex_indices[idx_offset + (i + 1) % count];

      auto edge_key = std::make_pair(std::min(v0, v1), std::max(v0, v1));
      edge_usage[edge_key]++;
    }
    idx_offset += count;
  }

  // Mark boundary edges and vertices
  for (const auto& entry : edge_usage) {
    if (entry.second == 1) {  // Boundary edge
      mesh.vertex_on_boundary[entry.first.first] = true;
      mesh.vertex_on_boundary[entry.first.second] = true;
    }
  }

  return SubdivResult(true);
}

SubdivResult ConvertFromHalfEdgeMesh(
    const HalfEdgeMesh& mesh,
    std::vector<uint32_t>& face_vertex_counts,
    std::vector<uint32_t>& face_vertex_indices,
    std::vector<float>& points) {

  if (!mesh.IsValid()) {
    return SubdivResult(false, "Invalid mesh");
  }

  face_vertex_counts = mesh.face_vertex_counts;
  face_vertex_indices = mesh.face_vertex_indices;
  points = mesh.points;

  return SubdivResult(true);
}

// ============================================================================
// Catmull-Clark Subdivision Implementation
// ============================================================================

SubdivResult CatmullClarkSubdivider::BuildTopology(
    const HalfEdgeMesh& mesh,
    std::vector<VertexInfo>& vertex_info) {

  uint32_t num_verts = mesh.GetNumVertices();
  vertex_info.clear();
  vertex_info.resize(num_verts);

  // Build adjacency information
  uint32_t idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < mesh.GetNumFaces(); ++face_idx) {
    uint32_t count = mesh.face_vertex_counts[face_idx];

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v_curr = mesh.face_vertex_indices[idx_offset + i];
      uint32_t v_prev = mesh.face_vertex_indices[idx_offset + (i + count - 1) % count];
      uint32_t v_next = mesh.face_vertex_indices[idx_offset + (i + 1) % count];

      // Add face adjacency
      if (std::find(vertex_info[v_curr].adjacent_faces.begin(),
                    vertex_info[v_curr].adjacent_faces.end(),
                    face_idx) == vertex_info[v_curr].adjacent_faces.end()) {
        vertex_info[v_curr].adjacent_faces.push_back(face_idx);
      }

      // Add vertex adjacency
      if (std::find(vertex_info[v_curr].adjacent_vertices.begin(),
                    vertex_info[v_curr].adjacent_vertices.end(),
                    v_prev) == vertex_info[v_curr].adjacent_vertices.end()) {
        vertex_info[v_curr].adjacent_vertices.push_back(v_prev);
      }
      if (std::find(vertex_info[v_curr].adjacent_vertices.begin(),
                    vertex_info[v_curr].adjacent_vertices.end(),
                    v_next) == vertex_info[v_curr].adjacent_vertices.end()) {
        vertex_info[v_curr].adjacent_vertices.push_back(v_next);
      }

      // Add edge adjacency
      EdgeKey edge_prev(v_curr, v_prev);
      EdgeKey edge_next(v_curr, v_next);

      if (std::find(vertex_info[v_curr].adjacent_edges.begin(),
                    vertex_info[v_curr].adjacent_edges.end(),
                    edge_prev) == vertex_info[v_curr].adjacent_edges.end()) {
        vertex_info[v_curr].adjacent_edges.push_back(edge_prev);
      }
      if (std::find(vertex_info[v_curr].adjacent_edges.begin(),
                    vertex_info[v_curr].adjacent_edges.end(),
                    edge_next) == vertex_info[v_curr].adjacent_edges.end()) {
        vertex_info[v_curr].adjacent_edges.push_back(edge_next);
      }
    }
    idx_offset += count;
  }

  // Compute valence and mark boundaries
  for (uint32_t v = 0; v < num_verts; ++v) {
    vertex_info[v].valence = static_cast<uint32_t>(vertex_info[v].adjacent_vertices.size());
    vertex_info[v].is_boundary = mesh.vertex_on_boundary[v];
  }

  return SubdivResult(true);
}

SubdivResult CatmullClarkSubdivider::SubdivideOnce(
    const HalfEdgeMesh& input,
    HalfEdgeMesh& output) {

  if (!input.IsValid()) {
    return SubdivResult(false, "Invalid input mesh");
  }

  // Build topology
  std::vector<VertexInfo> vertex_info;
  SubdivResult topo_result = BuildTopology(input, vertex_info);
  if (!topo_result.success) {
    return SubdivResult(false, topo_result.error);
  }

  uint32_t num_input_verts = input.GetNumVertices();
  uint32_t num_input_faces = input.GetNumFaces();

  // ========================================================================
  // Step 1: Compute face points (average of face vertices)
  // ========================================================================
  std::vector<float> face_points;
  face_points.reserve(num_input_faces * 3);

  uint32_t idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t count = input.face_vertex_counts[face_idx];

    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v = input.face_vertex_indices[idx_offset + i];
      fx += input.points[v * 3 + 0];
      fy += input.points[v * 3 + 1];
      fz += input.points[v * 3 + 2];
    }

    float inv_count = 1.0f / static_cast<float>(count);
    face_points.push_back(fx * inv_count);
    face_points.push_back(fy * inv_count);
    face_points.push_back(fz * inv_count);

    idx_offset += count;
  }

  // ========================================================================
  // Step 2: Compute edge points
  // ========================================================================
  // Edge points = average of (edge endpoints + adjacent face points)
  std::map<EdgeKey, uint32_t> edge_to_index;
  std::vector<float> edge_points;

  // Build edge map with adjacent faces
  std::map<EdgeKey, std::vector<uint32_t>> edge_faces;
  idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t count = input.face_vertex_counts[face_idx];

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v0 = input.face_vertex_indices[idx_offset + i];
      uint32_t v1 = input.face_vertex_indices[idx_offset + (i + 1) % count];

      EdgeKey edge(v0, v1);
      edge_faces[edge].push_back(face_idx);
    }
    idx_offset += count;
  }

  // Compute edge point positions
  for (const auto& entry : edge_faces) {
    const EdgeKey& edge = entry.first;
    const std::vector<uint32_t>& adj_faces = entry.second;

    float ex = 0.0f, ey = 0.0f, ez = 0.0f;

    // Add edge endpoints
    ex += input.points[edge.v0 * 3 + 0];
    ey += input.points[edge.v0 * 3 + 1];
    ez += input.points[edge.v0 * 3 + 2];
    ex += input.points[edge.v1 * 3 + 0];
    ey += input.points[edge.v1 * 3 + 1];
    ez += input.points[edge.v1 * 3 + 2];

    // Add adjacent face points
    for (uint32_t face_idx : adj_faces) {
      ex += face_points[face_idx * 3 + 0];
      ey += face_points[face_idx * 3 + 1];
      ez += face_points[face_idx * 3 + 2];
    }

    // Average
    float inv_count = 1.0f / static_cast<float>(2 + adj_faces.size());

    uint32_t edge_idx = static_cast<uint32_t>(edge_points.size() / 3);
    edge_to_index[edge] = edge_idx;

    edge_points.push_back(ex * inv_count);
    edge_points.push_back(ey * inv_count);
    edge_points.push_back(ez * inv_count);
  }

  // ========================================================================
  // Step 3: Compute new vertex points
  // ========================================================================
  // Formula: (Q/n) + (2R/n) + (S(n-3)/n)
  // Q = average of adjacent face points
  // R = average of adjacent edge midpoints
  // S = original vertex position
  // n = valence
  std::vector<float> new_vertex_points;
  new_vertex_points.reserve(num_input_verts * 3);

  for (uint32_t v = 0; v < num_input_verts; ++v) {
    float vx = input.points[v * 3 + 0];
    float vy = input.points[v * 3 + 1];
    float vz = input.points[v * 3 + 2];

    // Handle boundary vertices
    if (vertex_info[v].is_boundary) {
      if (boundary_mode_ == BoundaryInterpolation::EdgeAndCorner ||
          boundary_mode_ == BoundaryInterpolation::EdgeOnly) {
        // Keep boundary vertices at edge midpoint with adjacent boundary vertices
        float qx = 0.0f, qy = 0.0f, qz = 0.0f;
        uint32_t boundary_neighbor_count = 0;

        for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
          if (vertex_info[adj_v].is_boundary) {
            qx += input.points[adj_v * 3 + 0];
            qy += input.points[adj_v * 3 + 1];
            qz += input.points[adj_v * 3 + 2];
            boundary_neighbor_count++;
          }
        }

        if (boundary_neighbor_count > 0) {
          float inv_count = 1.0f / static_cast<float>(boundary_neighbor_count);
          qx *= inv_count;
          qy *= inv_count;
          qz *= inv_count;

          // Interpolate: 0.5 * original + 0.5 * average of boundary neighbors
          vx = 0.5f * vx + 0.5f * qx;
          vy = 0.5f * vy + 0.5f * qy;
          vz = 0.5f * vz + 0.5f * qz;
        }
      }
      // else: boundary_mode_ == None, keep original position
    } else {
      // Interior vertex: apply Catmull-Clark formula
      uint32_t n = vertex_info[v].valence;

      if (n == 0) {
        // Isolated vertex, keep original
      } else {
        // Q = average of adjacent face points
        float qx = 0.0f, qy = 0.0f, qz = 0.0f;
        for (uint32_t face_idx : vertex_info[v].adjacent_faces) {
          qx += face_points[face_idx * 3 + 0];
          qy += face_points[face_idx * 3 + 1];
          qz += face_points[face_idx * 3 + 2];
        }
        float inv_n_faces = 1.0f / static_cast<float>(vertex_info[v].adjacent_faces.size());
        qx *= inv_n_faces;
        qy *= inv_n_faces;
        qz *= inv_n_faces;

        // R = average of adjacent edge midpoints
        float rx = 0.0f, ry = 0.0f, rz = 0.0f;
        for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
          rx += input.points[adj_v * 3 + 0];
          ry += input.points[adj_v * 3 + 1];
          rz += input.points[adj_v * 3 + 2];
        }
        float inv_n_edges = 1.0f / static_cast<float>(n);
        rx *= inv_n_edges;
        ry *= inv_n_edges;
        rz *= inv_n_edges;

        // Apply formula: (Q/n) + (2R/n) + (S(n-3)/n)
        float inv_n = 1.0f / static_cast<float>(n);
        vx = qx * inv_n + rx * 2.0f * inv_n + vx * (static_cast<float>(n) - 3.0f) * inv_n;
        vy = qy * inv_n + ry * 2.0f * inv_n + vy * (static_cast<float>(n) - 3.0f) * inv_n;
        vz = qz * inv_n + rz * 2.0f * inv_n + vz * (static_cast<float>(n) - 3.0f) * inv_n;
      }
    }

    new_vertex_points.push_back(vx);
    new_vertex_points.push_back(vy);
    new_vertex_points.push_back(vz);
  }

  // ========================================================================
  // Step 4: Build output mesh topology
  // ========================================================================
  // Each input face with n vertices becomes n quads
  output.points.clear();
  output.face_vertex_counts.clear();
  output.face_vertex_indices.clear();

  // Add all vertices to output
  // Order: new vertex points, edge points, face points
  output.points = new_vertex_points;
  output.points.insert(output.points.end(), edge_points.begin(), edge_points.end());
  output.points.insert(output.points.end(), face_points.begin(), face_points.end());

  uint32_t num_edge_points = static_cast<uint32_t>(edge_points.size() / 3);
  uint32_t edge_point_offset = num_input_verts;
  uint32_t face_point_offset = num_input_verts + num_edge_points;

  // Build new faces
  idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t count = input.face_vertex_counts[face_idx];

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v_curr = input.face_vertex_indices[idx_offset + i];
      uint32_t v_next = input.face_vertex_indices[idx_offset + (i + 1) % count];

      EdgeKey edge_curr(v_curr, v_next);
      EdgeKey edge_prev(v_curr, input.face_vertex_indices[idx_offset + (i + count - 1) % count]);

      // Create quad: (v_curr, edge_curr, face_point, edge_prev)
      uint32_t idx_v_curr = v_curr;
      uint32_t idx_edge_curr = edge_point_offset + edge_to_index[edge_curr];
      uint32_t idx_face = face_point_offset + face_idx;
      uint32_t idx_edge_prev = edge_point_offset + edge_to_index[edge_prev];

      output.face_vertex_counts.push_back(4);
      output.face_vertex_indices.push_back(idx_v_curr);
      output.face_vertex_indices.push_back(idx_edge_curr);
      output.face_vertex_indices.push_back(idx_face);
      output.face_vertex_indices.push_back(idx_edge_prev);
    }
    idx_offset += count;
  }

  // Update boundary information for output mesh
  SubdivResult convert_result = ConvertToHalfEdgeMesh(
      output.face_vertex_counts,
      output.face_vertex_indices,
      output.points,
      output);

  return convert_result;
}

SubdivResult CatmullClarkSubdivider::Subdivide(
    const HalfEdgeMesh& input,
    HalfEdgeMesh& output,
    int levels) {

  if (levels < 1) {
    return SubdivResult(false, "Subdivision levels must be >= 1");
  }

  if (levels > 10) {
    return SubdivResult(false, "Subdivision levels too high (max 10 for security)");
  }

  HalfEdgeMesh current = input;
  HalfEdgeMesh next;

  for (int level = 0; level < levels; ++level) {
    SubdivResult result = SubdivideOnce(current, next);
    if (!result.success) {
      return SubdivResult(false, result.error);
    }

    // Security check: ensure we're not creating too many vertices
    if (next.GetNumVertices() > HalfEdgeMesh::kMaxVertices) {
      return SubdivResult(false, "Subdivision would exceed maximum vertex count");
    }

    current = next;
  }

  output = current;
  return SubdivResult(true);
}

// ============================================================================
// Loop Subdivision Implementation
// ============================================================================

float LoopSubdivider::ComputeBeta(uint32_t valence) const {
  // Loop's formula: β = (1/n)[5/8 - (3/8 + 1/4 cos(2π/n))²]
  // Warren's simplified formula: β = 3/(8n) for n > 3, β = 3/16 for n = 3

  if (valence == 3) {
    return 3.0f / 16.0f;
  } else if (valence > 3) {
    return 3.0f / (8.0f * static_cast<float>(valence));
  } else {
    // Degenerate case
    return 0.125f;
  }
}

SubdivResult LoopSubdivider::ValidateTriangleMesh(const HalfEdgeMesh& mesh) {
  for (uint32_t count : mesh.face_vertex_counts) {
    if (count != 3) {
      return SubdivResult(false, "Loop subdivision requires all triangles");
    }
  }
  return SubdivResult(true);
}

SubdivResult LoopSubdivider::BuildTopology(
    const HalfEdgeMesh& mesh,
    std::vector<VertexInfo>& vertex_info) {

  uint32_t num_verts = mesh.GetNumVertices();
  vertex_info.clear();
  vertex_info.resize(num_verts);

  // Build adjacency
  uint32_t idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < mesh.GetNumFaces(); ++face_idx) {
    uint32_t count = mesh.face_vertex_counts[face_idx];  // Should be 3

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v_curr = mesh.face_vertex_indices[idx_offset + i];
      uint32_t v_prev = mesh.face_vertex_indices[idx_offset + (i + count - 1) % count];
      uint32_t v_next = mesh.face_vertex_indices[idx_offset + (i + 1) % count];

      // Add vertex adjacency
      if (std::find(vertex_info[v_curr].adjacent_vertices.begin(),
                    vertex_info[v_curr].adjacent_vertices.end(),
                    v_prev) == vertex_info[v_curr].adjacent_vertices.end()) {
        vertex_info[v_curr].adjacent_vertices.push_back(v_prev);
      }
      if (std::find(vertex_info[v_curr].adjacent_vertices.begin(),
                    vertex_info[v_curr].adjacent_vertices.end(),
                    v_next) == vertex_info[v_curr].adjacent_vertices.end()) {
        vertex_info[v_curr].adjacent_vertices.push_back(v_next);
      }
    }
    idx_offset += count;
  }

  // Compute valence and mark boundaries
  for (uint32_t v = 0; v < num_verts; ++v) {
    vertex_info[v].valence = static_cast<uint32_t>(vertex_info[v].adjacent_vertices.size());
    vertex_info[v].is_boundary = mesh.vertex_on_boundary[v];
  }

  return SubdivResult(true);
}

SubdivResult LoopSubdivider::SubdivideOnce(
    const HalfEdgeMesh& input,
    HalfEdgeMesh& output) {

  if (!input.IsValid()) {
    return SubdivResult(false, "Invalid input mesh");
  }

  SubdivResult tri_check = ValidateTriangleMesh(input);
  if (!tri_check.success) {
    return SubdivResult(false, tri_check.error);
  }

  // Build topology
  std::vector<VertexInfo> vertex_info;
  SubdivResult topo_result = BuildTopology(input, vertex_info);
  if (!topo_result.success) {
    return SubdivResult(false, topo_result.error);
  }

  uint32_t num_input_verts = input.GetNumVertices();
  uint32_t num_input_faces = input.GetNumFaces();

  // ========================================================================
  // Step 1: Compute new edge vertices (odd vertices)
  // ========================================================================
  // For interior edges: (3/8)(A+B) + (1/8)(C+D)
  // where A,B are edge endpoints, C,D are opposite vertices
  // For boundary edges: (A+B)/2
  std::map<EdgeKey, uint32_t> edge_to_index;
  std::vector<float> edge_points;

  // Build edge map with adjacent triangles
  std::map<EdgeKey, std::vector<uint32_t>> edge_faces;
  std::map<EdgeKey, std::pair<uint32_t, uint32_t>> edge_endpoints;
  std::map<EdgeKey, std::vector<uint32_t>> edge_opposite_verts;

  uint32_t idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    for (uint32_t i = 0; i < 3; ++i) {
      uint32_t v0 = input.face_vertex_indices[idx_offset + i];
      uint32_t v1 = input.face_vertex_indices[idx_offset + (i + 1) % 3];
      uint32_t v_opposite = input.face_vertex_indices[idx_offset + (i + 2) % 3];

      EdgeKey edge(v0, v1);
      edge_faces[edge].push_back(face_idx);
      edge_endpoints[edge] = std::make_pair(v0, v1);
      edge_opposite_verts[edge].push_back(v_opposite);
    }
    idx_offset += 3;
  }

  // Compute edge point positions
  for (const auto& entry : edge_endpoints) {
    const EdgeKey& edge = entry.first;
    uint32_t v0 = entry.second.first;
    uint32_t v1 = entry.second.second;

    float ex, ey, ez;

    // Check if boundary edge
    bool is_boundary = (edge_faces[edge].size() == 1);

    if (is_boundary && boundary_mode_ != BoundaryInterpolation::None) {
      // Boundary edge: simple midpoint
      ex = 0.5f * (input.points[v0 * 3 + 0] + input.points[v1 * 3 + 0]);
      ey = 0.5f * (input.points[v0 * 3 + 1] + input.points[v1 * 3 + 1]);
      ez = 0.5f * (input.points[v0 * 3 + 2] + input.points[v1 * 3 + 2]);
    } else {
      // Interior edge: weighted average
      // (3/8)(A+B) + (1/8)(C+D)
      const std::vector<uint32_t>& opposite = edge_opposite_verts[edge];

      ex = 0.375f * (input.points[v0 * 3 + 0] + input.points[v1 * 3 + 0]);
      ey = 0.375f * (input.points[v0 * 3 + 1] + input.points[v1 * 3 + 1]);
      ez = 0.375f * (input.points[v0 * 3 + 2] + input.points[v1 * 3 + 2]);

      for (uint32_t v_opp : opposite) {
        ex += 0.125f * input.points[v_opp * 3 + 0];
        ey += 0.125f * input.points[v_opp * 3 + 1];
        ez += 0.125f * input.points[v_opp * 3 + 2];
      }
    }

    uint32_t edge_idx = static_cast<uint32_t>(edge_points.size() / 3);
    edge_to_index[edge] = edge_idx;

    edge_points.push_back(ex);
    edge_points.push_back(ey);
    edge_points.push_back(ez);
  }

  // ========================================================================
  // Step 2: Update existing vertices (even vertices)
  // ========================================================================
  // Interior: (1 - nβ)S + β * Σ(neighbors)
  // Boundary: 0.5*S + 0.5*average(boundary_neighbors)
  std::vector<float> new_vertex_points;
  new_vertex_points.reserve(num_input_verts * 3);

  for (uint32_t v = 0; v < num_input_verts; ++v) {
    float vx = input.points[v * 3 + 0];
    float vy = input.points[v * 3 + 1];
    float vz = input.points[v * 3 + 2];

    if (vertex_info[v].is_boundary) {
      if (boundary_mode_ == BoundaryInterpolation::EdgeAndCorner ||
          boundary_mode_ == BoundaryInterpolation::EdgeOnly) {
        // Boundary vertex: simple average with boundary neighbors
        float qx = 0.0f, qy = 0.0f, qz = 0.0f;
        uint32_t boundary_neighbor_count = 0;

        for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
          if (vertex_info[adj_v].is_boundary) {
            qx += input.points[adj_v * 3 + 0];
            qy += input.points[adj_v * 3 + 1];
            qz += input.points[adj_v * 3 + 2];
            boundary_neighbor_count++;
          }
        }

        if (boundary_neighbor_count > 0) {
          float inv_count = 1.0f / static_cast<float>(boundary_neighbor_count);
          qx *= inv_count;
          qy *= inv_count;
          qz *= inv_count;

          vx = 0.5f * vx + 0.5f * qx;
          vy = 0.5f * vy + 0.5f * qy;
          vz = 0.5f * vz + 0.5f * qz;
        }
      }
    } else {
      // Interior vertex: apply Loop formula
      uint32_t n = vertex_info[v].valence;

      if (n > 0) {
        float beta = ComputeBeta(n);
        float alpha = 1.0f - static_cast<float>(n) * beta;

        float neighbor_sum_x = 0.0f;
        float neighbor_sum_y = 0.0f;
        float neighbor_sum_z = 0.0f;

        for (uint32_t adj_v : vertex_info[v].adjacent_vertices) {
          neighbor_sum_x += input.points[adj_v * 3 + 0];
          neighbor_sum_y += input.points[adj_v * 3 + 1];
          neighbor_sum_z += input.points[adj_v * 3 + 2];
        }

        vx = alpha * vx + beta * neighbor_sum_x;
        vy = alpha * vy + beta * neighbor_sum_y;
        vz = alpha * vz + beta * neighbor_sum_z;
      }
    }

    new_vertex_points.push_back(vx);
    new_vertex_points.push_back(vy);
    new_vertex_points.push_back(vz);
  }

  // ========================================================================
  // Step 3: Build output mesh
  // ========================================================================
  // Each triangle becomes 4 triangles
  output.points.clear();
  output.face_vertex_counts.clear();
  output.face_vertex_indices.clear();

  // Add all vertices
  output.points = new_vertex_points;
  output.points.insert(output.points.end(), edge_points.begin(), edge_points.end());

  uint32_t edge_point_offset = num_input_verts;

  // Build new faces
  idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t v0 = input.face_vertex_indices[idx_offset + 0];
    uint32_t v1 = input.face_vertex_indices[idx_offset + 1];
    uint32_t v2 = input.face_vertex_indices[idx_offset + 2];

    EdgeKey e01(v0, v1);
    EdgeKey e12(v1, v2);
    EdgeKey e20(v2, v0);

    uint32_t e01_idx = edge_point_offset + edge_to_index[e01];
    uint32_t e12_idx = edge_point_offset + edge_to_index[e12];
    uint32_t e20_idx = edge_point_offset + edge_to_index[e20];

    // Create 4 triangles:
    // (v0, e01, e20)
    // (v1, e12, e01)
    // (v2, e20, e12)
    // (e01, e12, e20) - center triangle

    output.face_vertex_counts.push_back(3);
    output.face_vertex_indices.push_back(v0);
    output.face_vertex_indices.push_back(e01_idx);
    output.face_vertex_indices.push_back(e20_idx);

    output.face_vertex_counts.push_back(3);
    output.face_vertex_indices.push_back(v1);
    output.face_vertex_indices.push_back(e12_idx);
    output.face_vertex_indices.push_back(e01_idx);

    output.face_vertex_counts.push_back(3);
    output.face_vertex_indices.push_back(v2);
    output.face_vertex_indices.push_back(e20_idx);
    output.face_vertex_indices.push_back(e12_idx);

    output.face_vertex_counts.push_back(3);
    output.face_vertex_indices.push_back(e01_idx);
    output.face_vertex_indices.push_back(e12_idx);
    output.face_vertex_indices.push_back(e20_idx);

    idx_offset += 3;
  }

  // Update boundary information
  SubdivResult convert_result = ConvertToHalfEdgeMesh(
      output.face_vertex_counts,
      output.face_vertex_indices,
      output.points,
      output);

  return convert_result;
}

SubdivResult LoopSubdivider::Subdivide(
    const HalfEdgeMesh& input,
    HalfEdgeMesh& output,
    int levels) {

  if (levels < 1) {
    return SubdivResult(false, "Subdivision levels must be >= 1");
  }

  if (levels > 10) {
    return SubdivResult(false, "Subdivision levels too high (max 10 for security)");
  }

  HalfEdgeMesh current = input;
  HalfEdgeMesh next;

  for (int level = 0; level < levels; ++level) {
    SubdivResult result = SubdivideOnce(current, next);
    if (!result.success) {
      return SubdivResult(false, result.error);
    }

    // Security check
    if (next.GetNumVertices() > HalfEdgeMesh::kMaxVertices) {
      return SubdivResult(false, "Subdivision would exceed maximum vertex count");
    }

    current = next;
  }

  output = current;
  return SubdivResult(true);
}

// ============================================================================
// Bilinear Subdivision Implementation
// ============================================================================

SubdivResult BilinearSubdivider::SubdivideOnce(
    const HalfEdgeMesh& input,
    HalfEdgeMesh& output) {

  if (!input.IsValid()) {
    return SubdivResult(false, "Invalid input mesh");
  }

  uint32_t num_input_verts = input.GetNumVertices();
  uint32_t num_input_faces = input.GetNumFaces();

  // ========================================================================
  // Step 1: Keep original vertices unchanged
  // ========================================================================
  output.points = input.points;

  // ========================================================================
  // Step 2: Compute edge midpoints (simple linear interpolation)
  // ========================================================================
  std::map<EdgeKey, uint32_t> edge_to_index;
  std::vector<float> edge_points;

  // Build edge list
  uint32_t idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t count = input.face_vertex_counts[face_idx];

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v0 = input.face_vertex_indices[idx_offset + i];
      uint32_t v1 = input.face_vertex_indices[idx_offset + (i + 1) % count];

      EdgeKey edge(v0, v1);

      // If edge not yet processed, add it
      if (edge_to_index.find(edge) == edge_to_index.end()) {
        uint32_t edge_idx = static_cast<uint32_t>(edge_points.size() / 3);
        edge_to_index[edge] = edge_idx;

        // Simple midpoint: (v0 + v1) / 2
        float ex = 0.5f * (input.points[v0 * 3 + 0] + input.points[v1 * 3 + 0]);
        float ey = 0.5f * (input.points[v0 * 3 + 1] + input.points[v1 * 3 + 1]);
        float ez = 0.5f * (input.points[v0 * 3 + 2] + input.points[v1 * 3 + 2]);

        edge_points.push_back(ex);
        edge_points.push_back(ey);
        edge_points.push_back(ez);
      }
    }
    idx_offset += count;
  }

  // ========================================================================
  // Step 3: Compute face centers (average of face vertices)
  // ========================================================================
  std::vector<float> face_points;
  face_points.reserve(num_input_faces * 3);

  idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t count = input.face_vertex_counts[face_idx];

    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v = input.face_vertex_indices[idx_offset + i];
      fx += input.points[v * 3 + 0];
      fy += input.points[v * 3 + 1];
      fz += input.points[v * 3 + 2];
    }

    float inv_count = 1.0f / static_cast<float>(count);
    face_points.push_back(fx * inv_count);
    face_points.push_back(fy * inv_count);
    face_points.push_back(fz * inv_count);

    idx_offset += count;
  }

  // ========================================================================
  // Step 4: Build output mesh
  // ========================================================================
  // Add all vertices: original, edge midpoints, face centers
  output.points.insert(output.points.end(), edge_points.begin(), edge_points.end());
  output.points.insert(output.points.end(), face_points.begin(), face_points.end());

  uint32_t num_edge_points = static_cast<uint32_t>(edge_points.size() / 3);
  uint32_t edge_point_offset = num_input_verts;
  uint32_t face_point_offset = num_input_verts + num_edge_points;

  // Build new faces
  output.face_vertex_counts.clear();
  output.face_vertex_indices.clear();

  idx_offset = 0;
  for (uint32_t face_idx = 0; face_idx < num_input_faces; ++face_idx) {
    uint32_t count = input.face_vertex_counts[face_idx];

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v_curr = input.face_vertex_indices[idx_offset + i];
      uint32_t v_next = input.face_vertex_indices[idx_offset + (i + 1) % count];

      EdgeKey edge_curr(v_curr, v_next);
      EdgeKey edge_prev(v_curr, input.face_vertex_indices[idx_offset + (i + count - 1) % count]);

      // Create sub-face: (v_curr, edge_curr, face_center, edge_prev)
      uint32_t idx_v_curr = v_curr;
      uint32_t idx_edge_curr = edge_point_offset + edge_to_index[edge_curr];
      uint32_t idx_face = face_point_offset + face_idx;
      uint32_t idx_edge_prev = edge_point_offset + edge_to_index[edge_prev];

      // Determine face type (triangle or quad)
      if (count == 3) {
        // For triangles, create triangular sub-faces
        output.face_vertex_counts.push_back(3);
        output.face_vertex_indices.push_back(idx_v_curr);
        output.face_vertex_indices.push_back(idx_edge_curr);
        output.face_vertex_indices.push_back(idx_edge_prev);
      } else {
        // For quads and n-gons, create quad sub-faces
        output.face_vertex_counts.push_back(4);
        output.face_vertex_indices.push_back(idx_v_curr);
        output.face_vertex_indices.push_back(idx_edge_curr);
        output.face_vertex_indices.push_back(idx_face);
        output.face_vertex_indices.push_back(idx_edge_prev);
      }
    }

    // Add center face for triangles
    if (input.face_vertex_counts[face_idx] == 3) {
      uint32_t v0 = input.face_vertex_indices[idx_offset + 0];
      uint32_t v1 = input.face_vertex_indices[idx_offset + 1];
      uint32_t v2 = input.face_vertex_indices[idx_offset + 2];

      EdgeKey e01(v0, v1);
      EdgeKey e12(v1, v2);
      EdgeKey e20(v2, v0);

      output.face_vertex_counts.push_back(3);
      output.face_vertex_indices.push_back(edge_point_offset + edge_to_index[e01]);
      output.face_vertex_indices.push_back(edge_point_offset + edge_to_index[e12]);
      output.face_vertex_indices.push_back(edge_point_offset + edge_to_index[e20]);
    }

    idx_offset += count;
  }

  // Update boundary information
  SubdivResult convert_result = ConvertToHalfEdgeMesh(
      output.face_vertex_counts,
      output.face_vertex_indices,
      output.points,
      output);

  return convert_result;
}

SubdivResult BilinearSubdivider::Subdivide(
    const HalfEdgeMesh& input,
    HalfEdgeMesh& output,
    int levels) {

  if (levels < 1) {
    return SubdivResult(false, "Subdivision levels must be >= 1");
  }

  if (levels > 10) {
    return SubdivResult(false, "Subdivision levels too high (max 10 for security)");
  }

  HalfEdgeMesh current = input;
  HalfEdgeMesh next;

  for (int level = 0; level < levels; ++level) {
    SubdivResult result = SubdivideOnce(current, next);
    if (!result.success) {
      return SubdivResult(false, result.error);
    }

    // Security check
    if (next.GetNumVertices() > HalfEdgeMesh::kMaxVertices) {
      return SubdivResult(false, "Subdivision would exceed maximum vertex count");
    }

    current = next;
  }

  output = current;
  return SubdivResult(true);
}

}  // namespace subdiv
}  // namespace tinyusdz
