#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <vector>
#include <unordered_map>
#include <array>
#include <utility>

#include "chunked-array.hh"

namespace tinysubdiv {

struct Vec3f {
  float x, y, z;

  Vec3f() : x(0), y(0), z(0) {}
  Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  Vec3f operator+(const Vec3f& v) const { return Vec3f(x + v.x, y + v.y, z + v.z); }
  Vec3f operator-(const Vec3f& v) const { return Vec3f(x - v.x, y - v.y, z - v.z); }
  Vec3f operator*(float s) const { return Vec3f(x * s, y * s, z * s); }
  Vec3f operator/(float s) const { return Vec3f(x / s, y / s, z / s); }

  Vec3f& operator+=(const Vec3f& v) {
    x += v.x; y += v.y; z += v.z;
    return *this;
  }

  Vec3f& operator*=(float s) {
    x *= s; y *= s; z *= s;
    return *this;
  }

  float length() const { return std::sqrt(x * x + y * y + z * z); }
  Vec3f normalized() const {
    float len = length();
    return len > 0 ? (*this) / len : Vec3f();
  }
};

struct Vec4f {
  union {
    struct { float x, y, z, w; };
    float v[4];
  };

  Vec4f() : x(0), y(0), z(0), w(0) {}
  Vec4f(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct Face {
  std::vector<uint32_t> vertices;

  Face() = default;
  explicit Face(const std::vector<uint32_t>& verts) : vertices(verts) {}

  size_t size() const { return vertices.size(); }
  bool is_quad() const { return vertices.size() == 4; }
  bool is_triangle() const { return vertices.size() == 3; }
};

struct HalfEdge {
  uint32_t vertex;     // vertex at the end of this half-edge
  uint32_t face;       // face this half-edge belongs to
  uint32_t next;       // next half-edge in the same face
  uint32_t twin;       // opposite half-edge
  uint32_t edge;       // edge index this half-edge belongs to

  static constexpr uint32_t INVALID = 0xFFFFFFFF;
};

struct Edge {
  uint32_t v0, v1;     // vertex indices
  uint32_t f0, f1;     // adjacent face indices
  Vec3f midpoint;      // edge midpoint (computed during subdivision)
  bool is_crease;      // crease edge flag
  float sharpness;     // crease sharpness

  static constexpr uint32_t INVALID_FACE = 0xFFFFFFFF;

  bool is_boundary() const { return f1 == INVALID_FACE; }
};

struct Vertex {
  Vec3f position;
  Vec3f normal;
  uint32_t valence;    // number of edges connected to this vertex
  bool is_boundary;
  bool is_corner;      // corner vertex (sharp)
  float sharpness;     // corner sharpness

  Vertex() : valence(0), is_boundary(false), is_corner(false), sharpness(0.0f) {}
};

class SubdivisionMesh {
 public:
  SubdivisionMesh();
  ~SubdivisionMesh();

  // Input mesh setup
  void set_vertices(const float* positions, size_t count);
  void set_faces(const uint32_t* indices, const uint32_t* face_counts, size_t num_faces);
  void set_creases(const uint32_t* crease_indices, const float* crease_sharpness, size_t num_creases);
  void set_corners(const uint32_t* corner_indices, const float* corner_sharpness, size_t num_corners);

  // Subdivision schemes
  void subdivide_catmull_clark(int level);
  void subdivide_loop(int level);

  // Output accessors
  const ChunkedTypedArray<Vec3f>& get_vertices() const { return vertices_; }
  const ChunkedTypedArray<Face>& get_faces() const { return faces_; }

  // Get triangulated output
  void get_triangulated(std::vector<float>& vertices, std::vector<uint32_t>& indices) const;

  // Utility functions (public for testing)
  float loop_beta(int valence);  // Warren's beta weight calculation

 protected:
  // Topology building
  void build_topology();
  void build_half_edges();
  void build_edges();
  void find_boundary_vertices();

  // Catmull-Clark subdivision steps
  void compute_face_points(ChunkedTypedArray<Vec3f>& face_points);
  void compute_edge_points(ChunkedTypedArray<Vec3f>& edge_points,
                          const ChunkedTypedArray<Vec3f>& face_points);
  void compute_vertex_points(ChunkedTypedArray<Vec3f>& new_vertices,
                            const ChunkedTypedArray<Vec3f>& face_points,
                            const ChunkedTypedArray<Vec3f>& edge_points);
  void create_subdivided_faces(const ChunkedTypedArray<Vec3f>& face_points,
                              const ChunkedTypedArray<Vec3f>& edge_points,
                              const ChunkedTypedArray<Vec3f>& new_vertices);

  // Loop subdivision steps (for triangle meshes)
  void compute_loop_edge_points(ChunkedTypedArray<Vec3f>& edge_points);
  void compute_loop_vertex_points(ChunkedTypedArray<Vec3f>& new_vertices,
                                  const ChunkedTypedArray<Vec3f>& edge_points);
  void create_loop_subdivided_faces(const ChunkedTypedArray<Vec3f>& edge_points,
                                    const ChunkedTypedArray<Vec3f>& new_vertices);

  // Helpers
  uint64_t edge_key(uint32_t v0, uint32_t v1) const {
    if (v0 > v1) std::swap(v0, v1);
    return (static_cast<uint64_t>(v0) << 32) | v1;
  }

  // Mesh data
  ChunkedTypedArray<Vec3f> vertices_;
  ChunkedTypedArray<Face> faces_;
  ChunkedTypedArray<Edge> edges_;
  ChunkedTypedArray<HalfEdge> half_edges_;
  ChunkedTypedArray<Vertex> vertex_data_;

  // Edge lookup
  std::unordered_map<uint64_t, uint32_t> edge_map_;

  // Face neighbors for vertices
  std::vector<std::vector<uint32_t>> vertex_faces_;
  std::vector<std::vector<uint32_t>> vertex_edges_;

  // Hole faces - these faces are not subdivided or rendered
  std::vector<bool> hole_face_flags_;
};

// SIMD optimized versions
#ifdef TINYSUBDIV_USE_SSE
void catmull_clark_sse(SubdivisionMesh& mesh, int level);
void loop_subdivision_sse(SubdivisionMesh& mesh, int level);
#endif

#ifdef TINYSUBDIV_USE_AVX
void catmull_clark_avx(SubdivisionMesh& mesh, int level);
void loop_subdivision_avx(SubdivisionMesh& mesh, int level);
#endif

#ifdef TINYSUBDIV_USE_AVX512
void catmull_clark_avx512(SubdivisionMesh& mesh, int level);
#endif

#ifdef TINYSUBDIV_USE_NEON
void catmull_clark_neon(SubdivisionMesh& mesh, int level);
void loop_subdivision_neon(SubdivisionMesh& mesh, int level);
#endif

}  // namespace tinysubdiv