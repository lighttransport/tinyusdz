#include "tinysubdiv.hh"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tinysubdiv {

SubdivisionMesh::SubdivisionMesh() = default;
SubdivisionMesh::~SubdivisionMesh() = default;

void SubdivisionMesh::set_vertices(const float* positions, size_t count) {
  vertices_.clear();
  vertices_.reserve(count);
  vertex_data_.clear();
  vertex_data_.resize(count);

  for (size_t i = 0; i < count; ++i) {
    vertices_.push_back(Vec3f(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]));
  }

  vertex_faces_.clear();
  vertex_faces_.resize(count);
  vertex_edges_.clear();
  vertex_edges_.resize(count);
}

void SubdivisionMesh::set_faces(const uint32_t* indices, const uint32_t* face_counts, size_t num_faces) {
  faces_.clear();
  faces_.reserve(num_faces);

  size_t idx = 0;
  for (size_t i = 0; i < num_faces; ++i) {
    Face face;
    face.vertices.reserve(face_counts[i]);
    for (uint32_t j = 0; j < face_counts[i]; ++j) {
      face.vertices.push_back(indices[idx++]);
    }
    faces_.push_back(std::move(face));
  }

  build_topology();
}

void SubdivisionMesh::set_creases(const uint32_t* crease_indices, const float* crease_sharpness, size_t num_creases) {
  for (size_t i = 0; i < num_creases; ++i) {
    uint32_t v0 = crease_indices[i * 2];
    uint32_t v1 = crease_indices[i * 2 + 1];
    uint64_t key = edge_key(v0, v1);

    auto it = edge_map_.find(key);
    if (it != edge_map_.end()) {
      edges_[it->second].is_crease = true;
      edges_[it->second].sharpness = crease_sharpness[i];
    }
  }
}

void SubdivisionMesh::set_corners(const uint32_t* corner_indices, const float* corner_sharpness, size_t num_corners) {
  for (size_t i = 0; i < num_corners; ++i) {
    uint32_t vid = corner_indices[i];
    if (vid < vertex_data_.size()) {
      vertex_data_[vid].is_corner = true;
      vertex_data_[vid].sharpness = corner_sharpness[i];
    }
  }
}

void SubdivisionMesh::build_topology() {
  build_edges();
  build_half_edges();
  find_boundary_vertices();
}

void SubdivisionMesh::build_edges() {
  edges_.clear();
  edge_map_.clear();

  // Clear vertex-face relationships
  for (auto& vf : vertex_faces_) {
    vf.clear();
  }
  for (auto& ve : vertex_edges_) {
    ve.clear();
  }

  for (size_t fi = 0; fi < faces_.size(); ++fi) {
    const Face& face = faces_[fi];
    const size_t n = face.vertices.size();

    // Add face to each vertex's face list (only once per face)
    for (size_t i = 0; i < n; ++i) {
      uint32_t v = face.vertices[i];
      vertex_faces_[v].push_back(static_cast<uint32_t>(fi));
    }

    for (size_t i = 0; i < n; ++i) {
      uint32_t v0 = face.vertices[i];
      uint32_t v1 = face.vertices[(i + 1) % n];

      uint64_t key = edge_key(v0, v1);
      auto it = edge_map_.find(key);

      if (it == edge_map_.end()) {
        Edge edge;
        edge.v0 = std::min(v0, v1);
        edge.v1 = std::max(v0, v1);
        edge.f0 = static_cast<uint32_t>(fi);
        edge.f1 = Edge::INVALID_FACE;
        edge.is_crease = false;
        edge.sharpness = 0.0f;

        uint32_t edge_idx = static_cast<uint32_t>(edges_.size());
        edges_.push_back(edge);
        edge_map_[key] = edge_idx;

        if (vertex_edges_[v0].empty() ||
            std::find(vertex_edges_[v0].begin(), vertex_edges_[v0].end(), edge_idx) == vertex_edges_[v0].end()) {
          vertex_edges_[v0].push_back(edge_idx);
        }
        if (vertex_edges_[v1].empty() ||
            std::find(vertex_edges_[v1].begin(), vertex_edges_[v1].end(), edge_idx) == vertex_edges_[v1].end()) {
          vertex_edges_[v1].push_back(edge_idx);
        }
      } else {
        edges_[it->second].f1 = static_cast<uint32_t>(fi);
      }
    }
  }

  for (size_t i = 0; i < vertex_data_.size(); ++i) {
    vertex_data_[i].valence = static_cast<uint32_t>(vertex_edges_[i].size());
  }
}

void SubdivisionMesh::build_half_edges() {
  half_edges_.clear();

  for (size_t fi = 0; fi < faces_.size(); ++fi) {
    const Face& face = faces_[fi];
    const size_t n = face.vertices.size();

    for (size_t i = 0; i < n; ++i) {
      HalfEdge he;
      he.vertex = face.vertices[(i + 1) % n];
      he.face = static_cast<uint32_t>(fi);
      he.next = static_cast<uint32_t>(half_edges_.size() + ((i + 1) % n));
      he.twin = HalfEdge::INVALID;

      uint32_t v0 = face.vertices[i];
      uint32_t v1 = face.vertices[(i + 1) % n];
      uint64_t key = edge_key(v0, v1);
      auto it = edge_map_.find(key);
      if (it != edge_map_.end()) {
        he.edge = it->second;
      }

      half_edges_.push_back(he);
    }
  }
}

void SubdivisionMesh::find_boundary_vertices() {
  for (size_t i = 0; i < vertex_data_.size(); ++i) {
    vertex_data_[i].is_boundary = false;

    for (uint32_t edge_idx : vertex_edges_[i]) {
      if (edges_[edge_idx].is_boundary()) {
        vertex_data_[i].is_boundary = true;
        break;
      }
    }
  }
}

void SubdivisionMesh::compute_face_points(ChunkedTypedArray<Vec3f>& face_points) {
  face_points.clear();
  face_points.reserve(faces_.size());

  for (size_t fi = 0; fi < faces_.size(); ++fi) {
    const Face& face = faces_[fi];
    Vec3f center(0, 0, 0);

    for (uint32_t vi : face.vertices) {
      center += vertices_[vi];
    }

    center = center / static_cast<float>(face.vertices.size());
    face_points.push_back(center);
  }
}

void SubdivisionMesh::compute_edge_points(ChunkedTypedArray<Vec3f>& edge_points,
                                         const ChunkedTypedArray<Vec3f>& face_points) {
  edge_points.clear();
  edge_points.reserve(edges_.size());

  for (size_t i = 0; i < edges_.size(); ++i) {
    const Edge& edge = edges_[i];
    Vec3f edge_point;

    if (edge.is_crease || edge.is_boundary()) {
      edge_point = (vertices_[edge.v0] + vertices_[edge.v1]) * 0.5f;
    } else {
      Vec3f edge_center = (vertices_[edge.v0] + vertices_[edge.v1]) * 0.5f;
      Vec3f face_center = face_points[edge.f0];

      if (edge.f1 != Edge::INVALID_FACE) {
        face_center = (face_points[edge.f0] + face_points[edge.f1]) * 0.5f;
      }

      edge_point = (edge_center + face_center) * 0.5f;
    }

    edge_points.push_back(edge_point);
  }
}

void SubdivisionMesh::compute_vertex_points(ChunkedTypedArray<Vec3f>& new_vertices,
                                           const ChunkedTypedArray<Vec3f>& face_points,
                                           const ChunkedTypedArray<Vec3f>& edge_points) {
  new_vertices.clear();
  new_vertices.reserve(vertices_.size());

  for (size_t vi = 0; vi < vertices_.size(); ++vi) {
    const Vec3f& old_pos = vertices_[vi];
    const Vertex& vdata = vertex_data_[vi];

    if (vdata.is_corner) {
      new_vertices.push_back(old_pos);
      continue;
    }

    if (vdata.is_boundary) {
      Vec3f boundary_sum(0, 0, 0);
      int boundary_count = 0;

      for (uint32_t edge_idx : vertex_edges_[vi]) {
        if (edges_[edge_idx].is_boundary()) {
          uint32_t other_v = (edges_[edge_idx].v0 == vi) ? edges_[edge_idx].v1 : edges_[edge_idx].v0;
          boundary_sum += vertices_[other_v];
          boundary_count++;
        }
      }

      if (boundary_count == 2) {
        Vec3f new_pos = old_pos * 0.75f + boundary_sum * 0.125f;
        new_vertices.push_back(new_pos);
      } else {
        new_vertices.push_back(old_pos);
      }
    } else {
      const uint32_t n = vdata.valence;

      if (n < 3) {
        new_vertices.push_back(old_pos);
        continue;
      }

      Vec3f face_avg(0, 0, 0);
      for (uint32_t fi : vertex_faces_[vi]) {
        face_avg += face_points[fi];
      }
      face_avg = face_avg / static_cast<float>(vertex_faces_[vi].size());

      Vec3f edge_avg(0, 0, 0);
      for (uint32_t edge_idx : vertex_edges_[vi]) {
        const Edge& edge = edges_[edge_idx];
        uint32_t other_v = (edge.v0 == vi) ? edge.v1 : edge.v0;
        edge_avg += (old_pos + vertices_[other_v]) * 0.5f;
      }
      edge_avg = edge_avg / static_cast<float>(n);

      const float k1 = static_cast<float>(n - 2) / static_cast<float>(n);
      const float k2 = 1.0f / static_cast<float>(n * n);
      const float k3 = 2.0f / static_cast<float>(n * n);

      Vec3f new_pos = old_pos * k1 + edge_avg * k3 + face_avg * k2;
      new_vertices.push_back(new_pos);
    }
  }
}

void SubdivisionMesh::create_subdivided_faces(const ChunkedTypedArray<Vec3f>& face_points,
                                             const ChunkedTypedArray<Vec3f>& edge_points,
                                             const ChunkedTypedArray<Vec3f>& new_vertices) {
  ChunkedTypedArray<Vec3f> new_verts_array;
  ChunkedTypedArray<Face> new_faces;
  std::vector<bool> new_hole_flags;

  new_verts_array.reserve(new_vertices.size() + face_points.size() + edge_points.size());

  for (size_t i = 0; i < new_vertices.size(); ++i) {
    new_verts_array.push_back(new_vertices[i]);
  }

  uint32_t face_point_start = static_cast<uint32_t>(new_verts_array.size());
  for (size_t i = 0; i < face_points.size(); ++i) {
    new_verts_array.push_back(face_points[i]);
  }

  uint32_t edge_point_start = static_cast<uint32_t>(new_verts_array.size());
  for (size_t i = 0; i < edge_points.size(); ++i) {
    new_verts_array.push_back(edge_points[i]);
  }

  for (size_t fi = 0; fi < faces_.size(); ++fi) {
    // Skip hole faces - they are not subdivided
    bool is_hole = (fi < hole_face_flags_.size()) && hole_face_flags_[fi];
    if (is_hole) {
      // Keep the hole face as-is (or optionally skip it entirely)
      continue;
    }
    const Face& old_face = faces_[fi];
    const size_t n = old_face.vertices.size();
    uint32_t face_point_idx = face_point_start + static_cast<uint32_t>(fi);

    for (size_t i = 0; i < n; ++i) {
      uint32_t v = old_face.vertices[i];
      uint32_t v_next = old_face.vertices[(i + 1) % n];
      uint32_t v_prev = old_face.vertices[(i + n - 1) % n];

      uint64_t edge_curr_key = edge_key(v, v_next);
      uint64_t edge_prev_key = edge_key(v_prev, v);

      auto edge_curr_it = edge_map_.find(edge_curr_key);
      auto edge_prev_it = edge_map_.find(edge_prev_key);

      if (edge_curr_it != edge_map_.end() && edge_prev_it != edge_map_.end()) {
        uint32_t edge_curr_idx = edge_point_start + edge_curr_it->second;
        uint32_t edge_prev_idx = edge_point_start + edge_prev_it->second;

        Face new_face;
        new_face.vertices.push_back(v);
        new_face.vertices.push_back(edge_curr_idx);
        new_face.vertices.push_back(face_point_idx);
        new_face.vertices.push_back(edge_prev_idx);

        new_faces.push_back(std::move(new_face));
      }
    }
  }

  vertices_ = std::move(new_verts_array);
  faces_ = std::move(new_faces);

  // Clear hole flags as subdivided faces are not holes
  hole_face_flags_.clear();

  // Resize vertex data structures for the new mesh
  vertex_data_.clear();
  vertex_data_.resize(vertices_.size());
  vertex_faces_.clear();
  vertex_faces_.resize(vertices_.size());
  vertex_edges_.clear();
  vertex_edges_.resize(vertices_.size());

  build_topology();
}

void SubdivisionMesh::subdivide_catmull_clark(int level) {
  for (int l = 0; l < level; ++l) {
    ChunkedTypedArray<Vec3f> face_points;
    compute_face_points(face_points);

    ChunkedTypedArray<Vec3f> edge_points;
    compute_edge_points(edge_points, face_points);

    ChunkedTypedArray<Vec3f> new_vertices;
    compute_vertex_points(new_vertices, face_points, edge_points);

    create_subdivided_faces(face_points, edge_points, new_vertices);
  }
}

// Loop subdivision implementation
float SubdivisionMesh::loop_beta(int valence) {
  // Warren's improved beta formula
  if (valence == 3) {
    return 3.0f / 16.0f;
  } else {
    return 3.0f / (8.0f * static_cast<float>(valence));
  }
}

void SubdivisionMesh::compute_loop_edge_points(ChunkedTypedArray<Vec3f>& edge_points) {
  edge_points.clear();
  if (edges_.empty()) return;

  edge_points.reserve(edges_.size());

  for (size_t i = 0; i < edges_.size(); ++i) {
    const Edge& edge = edges_[i];

    if (edge.is_crease || edge.is_boundary()) {
      // Boundary/crease edge: use midpoint rule
      Vec3f midpoint = (vertices_[edge.v0] + vertices_[edge.v1]) * 0.5f;
      edge_points.push_back(midpoint);
    } else {
      // Interior edge: use Loop's 3/8 - 1/8 rule
      Vec3f v0 = vertices_[edge.v0];
      Vec3f v1 = vertices_[edge.v1];

      // Find opposite vertices in the two triangles sharing this edge
      Vec3f opposite_sum(0, 0, 0);
      int opposite_count = 0;

      // For each face adjacent to this edge
      if (edge.f0 != Edge::INVALID_FACE) {
        const Face& face0 = faces_[edge.f0];
        for (uint32_t vid : face0.vertices) {
          if (vid != edge.v0 && vid != edge.v1) {
            opposite_sum += vertices_[vid];
            opposite_count++;
          }
        }
      }

      if (edge.f1 != Edge::INVALID_FACE) {
        const Face& face1 = faces_[edge.f1];
        for (uint32_t vid : face1.vertices) {
          if (vid != edge.v0 && vid != edge.v1) {
            opposite_sum += vertices_[vid];
            opposite_count++;
          }
        }
      }

      // Loop subdivision edge point formula: 3/8 * (v0 + v1) + 1/8 * (opposite vertices)
      Vec3f edge_point = (v0 + v1) * (3.0f / 8.0f);
      if (opposite_count > 0) {
        edge_point += opposite_sum * (1.0f / (8.0f * opposite_count));
      }

      edge_points.push_back(edge_point);
    }
  }
}

void SubdivisionMesh::compute_loop_vertex_points(ChunkedTypedArray<Vec3f>& new_vertices,
                                                const ChunkedTypedArray<Vec3f>& edge_points) {
  new_vertices.clear();
  if (vertices_.empty()) return;

  new_vertices.reserve(vertices_.size());

  // Ensure vertex_data_ is properly sized
  if (vertex_data_.size() != vertices_.size()) {
    vertex_data_.resize(vertices_.size());
  }

  for (size_t vi = 0; vi < vertices_.size(); ++vi) {
    const Vec3f& old_pos = vertices_[vi];
    const Vertex& vdata = vertex_data_[vi];

    if (vdata.is_corner) {
      // Corner vertices don't move
      new_vertices.push_back(old_pos);
      continue;
    }

    if (vdata.is_boundary) {
      // Boundary vertex: use 1/8 - 3/4 - 1/8 rule
      Vec3f boundary_sum(0, 0, 0);
      int boundary_count = 0;

      for (uint32_t edge_idx : vertex_edges_[vi]) {
        if (edges_[edge_idx].is_boundary()) {
          uint32_t other_v = (edges_[edge_idx].v0 == vi) ? edges_[edge_idx].v1 : edges_[edge_idx].v0;
          boundary_sum += vertices_[other_v];
          boundary_count++;
        }
      }

      if (boundary_count == 2) {
        Vec3f new_pos = old_pos * 0.75f + boundary_sum * 0.125f;
        new_vertices.push_back(new_pos);
      } else {
        new_vertices.push_back(old_pos);
      }
    } else {
      // Interior vertex: use Loop's vertex update rule
      const int n = vdata.valence;
      float beta = loop_beta(n);

      // Sum of neighboring vertices
      Vec3f neighbor_sum(0, 0, 0);
      for (uint32_t edge_idx : vertex_edges_[vi]) {
        const Edge& edge = edges_[edge_idx];
        uint32_t other_v = (edge.v0 == vi) ? edge.v1 : edge.v0;
        neighbor_sum += vertices_[other_v];
      }

      // Loop vertex formula: (1 - n*beta) * old_vertex + beta * sum(neighbors)
      float alpha = 1.0f - n * beta;
      Vec3f new_pos = old_pos * alpha + neighbor_sum * beta;
      new_vertices.push_back(new_pos);
    }
  }
}

void SubdivisionMesh::create_loop_subdivided_faces(const ChunkedTypedArray<Vec3f>& edge_points,
                                                  const ChunkedTypedArray<Vec3f>& new_vertices) {
  ChunkedTypedArray<Vec3f> new_verts_array;
  ChunkedTypedArray<Face> new_faces;

  // Copy updated vertex positions
  new_verts_array.reserve(new_vertices.size() + edge_points.size());
  for (size_t i = 0; i < new_vertices.size(); ++i) {
    new_verts_array.push_back(new_vertices[i]);
  }

  // Add edge points as new vertices
  uint32_t edge_point_start = static_cast<uint32_t>(new_verts_array.size());
  for (size_t i = 0; i < edge_points.size(); ++i) {
    new_verts_array.push_back(edge_points[i]);
  }

  // Create new faces (each triangle becomes 4 triangles)
  for (size_t fi = 0; fi < faces_.size(); ++fi) {
    // Skip hole faces
    bool is_hole = (fi < hole_face_flags_.size()) && hole_face_flags_[fi];
    if (is_hole) {
      continue;
    }

    const Face& old_face = faces_[fi];

    // Only process triangles for Loop subdivision
    if (!old_face.is_triangle()) {
      continue;
    }

    uint32_t v0 = old_face.vertices[0];
    uint32_t v1 = old_face.vertices[1];
    uint32_t v2 = old_face.vertices[2];

    // Find edge point indices
    uint64_t e01_key = edge_key(v0, v1);
    uint64_t e12_key = edge_key(v1, v2);
    uint64_t e20_key = edge_key(v2, v0);

    auto e01_it = edge_map_.find(e01_key);
    auto e12_it = edge_map_.find(e12_key);
    auto e20_it = edge_map_.find(e20_key);

    if (e01_it != edge_map_.end() && e12_it != edge_map_.end() && e20_it != edge_map_.end()) {
      uint32_t e01 = edge_point_start + e01_it->second;
      uint32_t e12 = edge_point_start + e12_it->second;
      uint32_t e20 = edge_point_start + e20_it->second;

      // Create 4 new triangles
      Face f1, f2, f3, f4;

      // Center triangle
      f1.vertices.push_back(e01);
      f1.vertices.push_back(e12);
      f1.vertices.push_back(e20);

      // Corner triangles
      f2.vertices.push_back(v0);
      f2.vertices.push_back(e01);
      f2.vertices.push_back(e20);

      f3.vertices.push_back(v1);
      f3.vertices.push_back(e12);
      f3.vertices.push_back(e01);

      f4.vertices.push_back(v2);
      f4.vertices.push_back(e20);
      f4.vertices.push_back(e12);

      new_faces.push_back(std::move(f1));
      new_faces.push_back(std::move(f2));
      new_faces.push_back(std::move(f3));
      new_faces.push_back(std::move(f4));
    }
  }

  vertices_ = std::move(new_verts_array);
  faces_ = std::move(new_faces);

  // Resize vertex data structures for the new mesh
  vertex_data_.clear();
  vertex_data_.resize(vertices_.size());
  vertex_faces_.clear();
  vertex_faces_.resize(vertices_.size());
  vertex_edges_.clear();
  vertex_edges_.resize(vertices_.size());

  // Rebuild topology for the new mesh
  build_topology();
}

void SubdivisionMesh::subdivide_loop(int level) {
  // Verify mesh contains only triangles
  for (size_t i = 0; i < faces_.size(); ++i) {
    if (!faces_[i].is_triangle()) {
      // Could throw an exception or convert to triangles first
      return;
    }
  }

  for (int l = 0; l < level; ++l) {
    ChunkedTypedArray<Vec3f> edge_points;
    compute_loop_edge_points(edge_points);

    ChunkedTypedArray<Vec3f> new_vertices;
    compute_loop_vertex_points(new_vertices, edge_points);

    create_loop_subdivided_faces(edge_points, new_vertices);
  }
}

void SubdivisionMesh::get_triangulated(std::vector<float>& vertices, std::vector<uint32_t>& indices) const {
  vertices.clear();
  vertices.reserve(vertices_.size() * 3);

  for (size_t i = 0; i < vertices_.size(); ++i) {
    const Vec3f& v = vertices_[i];
    vertices.push_back(v.x);
    vertices.push_back(v.y);
    vertices.push_back(v.z);
  }

  indices.clear();
  for (size_t fi = 0; fi < faces_.size(); ++fi) {
    const Face& face = faces_[fi];

    if (face.is_triangle()) {
      indices.push_back(face.vertices[0]);
      indices.push_back(face.vertices[1]);
      indices.push_back(face.vertices[2]);
    } else if (face.is_quad()) {
      indices.push_back(face.vertices[0]);
      indices.push_back(face.vertices[1]);
      indices.push_back(face.vertices[2]);

      indices.push_back(face.vertices[0]);
      indices.push_back(face.vertices[2]);
      indices.push_back(face.vertices[3]);
    } else {
      for (size_t i = 1; i < face.vertices.size() - 1; ++i) {
        indices.push_back(face.vertices[0]);
        indices.push_back(face.vertices[i]);
        indices.push_back(face.vertices[i + 1]);
      }
    }
  }
}

}  // namespace tinysubdiv