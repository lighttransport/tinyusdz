#include "tinysubdiv.hh"
#include <cstring>

#ifdef TINYSUBDIV_USE_SSE
#include <emmintrin.h>
#include <xmmintrin.h>
#include <smmintrin.h>
#endif

#ifdef TINYSUBDIV_USE_AVX
#include <immintrin.h>
#endif

#ifdef TINYSUBDIV_USE_NEON
#include <arm_neon.h>
#endif

namespace tinysubdiv {

#ifdef TINYSUBDIV_USE_SSE

inline __m128 vec3_load(const Vec3f& v) {
  return _mm_set_ps(0.0f, v.z, v.y, v.x);
}

inline void vec3_store(Vec3f& v, __m128 val) {
  alignas(16) float tmp[4];
  _mm_store_ps(tmp, val);
  v.x = tmp[0];
  v.y = tmp[1];
  v.z = tmp[2];
}

inline __m128 vec3_add(__m128 a, __m128 b) {
  return _mm_add_ps(a, b);
}

inline __m128 vec3_mul(__m128 a, float scalar) {
  return _mm_mul_ps(a, _mm_set1_ps(scalar));
}

static void compute_face_points_sse(const ChunkedTypedArray<Vec3f>& vertices,
                                   const ChunkedTypedArray<Face>& faces,
                                   ChunkedTypedArray<Vec3f>& face_points) {
  face_points.clear();
  face_points.reserve(faces.size());

  for (size_t fi = 0; fi < faces.size(); ++fi) {
    const Face& face = faces[fi];
    __m128 sum = _mm_setzero_ps();

    for (uint32_t vi : face.vertices) {
      __m128 v = vec3_load(vertices[vi]);
      sum = vec3_add(sum, v);
    }

    const float inv_count = 1.0f / static_cast<float>(face.vertices.size());
    __m128 center = vec3_mul(sum, inv_count);

    Vec3f result;
    vec3_store(result, center);
    face_points.push_back(result);
  }
}

static void compute_edge_points_sse(const ChunkedTypedArray<Vec3f>& vertices,
                                   const ChunkedTypedArray<Edge>& edges,
                                   const ChunkedTypedArray<Vec3f>& face_points,
                                   ChunkedTypedArray<Vec3f>& edge_points) {
  edge_points.clear();
  edge_points.reserve(edges.size());

  for (size_t i = 0; i < edges.size(); ++i) {
    const Edge& edge = edges[i];

    __m128 v0 = vec3_load(vertices[edge.v0]);
    __m128 v1 = vec3_load(vertices[edge.v1]);

    if (edge.is_crease || edge.is_boundary()) {
      __m128 mid = vec3_mul(vec3_add(v0, v1), 0.5f);
      Vec3f result;
      vec3_store(result, mid);
      edge_points.push_back(result);
    } else {
      __m128 edge_center = vec3_mul(vec3_add(v0, v1), 0.5f);
      __m128 face_center = vec3_load(face_points[edge.f0]);

      if (edge.f1 != Edge::INVALID_FACE) {
        __m128 f1 = vec3_load(face_points[edge.f1]);
        face_center = vec3_mul(vec3_add(face_center, f1), 0.5f);
      }

      __m128 edge_point = vec3_mul(vec3_add(edge_center, face_center), 0.5f);
      Vec3f result;
      vec3_store(result, edge_point);
      edge_points.push_back(result);
    }
  }
}

void catmull_clark_sse(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full SSE-optimized Catmull-Clark
  // For now, delegate to scalar version
  mesh.subdivide_catmull_clark(level);
}

#endif // TINYSUBDIV_USE_SSE

#ifdef TINYSUBDIV_USE_AVX

inline __m256 vec3x2_load(const Vec3f& v1, const Vec3f& v2) {
  return _mm256_set_ps(0.0f, v2.z, v2.y, v2.x, 0.0f, v1.z, v1.y, v1.x);
}

inline void vec3x2_store(Vec3f& v1, Vec3f& v2, __m256 val) {
  alignas(32) float tmp[8];
  _mm256_store_ps(tmp, val);
  v1.x = tmp[0];
  v1.y = tmp[1];
  v1.z = tmp[2];
  v2.x = tmp[4];
  v2.y = tmp[5];
  v2.z = tmp[6];
}

static void compute_face_points_avx(const ChunkedTypedArray<Vec3f>& vertices,
                                   const ChunkedTypedArray<Face>& faces,
                                   ChunkedTypedArray<Vec3f>& face_points) {
  face_points.clear();
  face_points.reserve(faces.size());

  size_t fi = 0;
  for (; fi + 1 < faces.size(); fi += 2) {
    const Face& face1 = faces[fi];
    const Face& face2 = faces[fi + 1];

    __m256 sum1_2 = _mm256_setzero_ps();

    size_t max_verts = std::max(face1.vertices.size(), face2.vertices.size());
    for (size_t i = 0; i < max_verts; ++i) {
      Vec3f v1(0, 0, 0), v2(0, 0, 0);
      if (i < face1.vertices.size()) v1 = vertices[face1.vertices[i]];
      if (i < face2.vertices.size()) v2 = vertices[face2.vertices[i]];

      __m256 verts = vec3x2_load(v1, v2);
      sum1_2 = _mm256_add_ps(sum1_2, verts);
    }

    const float inv1 = 1.0f / static_cast<float>(face1.vertices.size());
    const float inv2 = 1.0f / static_cast<float>(face2.vertices.size());

    __m256 multipliers = _mm256_set_ps(0.0f, inv2, inv2, inv2, 0.0f, inv1, inv1, inv1);
    __m256 centers = _mm256_mul_ps(sum1_2, multipliers);

    Vec3f result1, result2;
    vec3x2_store(result1, result2, centers);
    face_points.push_back(result1);
    face_points.push_back(result2);
  }

  if (fi < faces.size()) {
    const Face& face = faces[fi];
    Vec3f center(0, 0, 0);
    for (uint32_t vi : face.vertices) {
      center += vertices[vi];
    }
    center = center / static_cast<float>(face.vertices.size());
    face_points.push_back(center);
  }
}

void catmull_clark_avx(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full AVX-optimized Catmull-Clark
  // For now, delegate to scalar version
  mesh.subdivide_catmull_clark(level);
}

#endif // TINYSUBDIV_USE_AVX

#ifdef TINYSUBDIV_USE_AVX512

static void compute_face_points_avx512(const ChunkedTypedArray<Vec3f>& vertices,
                                      const ChunkedTypedArray<Face>& faces,
                                      ChunkedTypedArray<Vec3f>& face_points) {
  face_points.clear();
  face_points.reserve(faces.size());

  // Process 4 faces at a time using AVX512
  size_t fi = 0;
  for (; fi + 3 < faces.size(); fi += 4) {
    __m512 sum = _mm512_setzero_ps();

    // Accumulate vertices for 4 faces
    for (int f = 0; f < 4; ++f) {
      const Face& face = faces[fi + f];
      for (uint32_t vi : face.vertices) {
        const Vec3f& v = vertices[vi];
        __m512 vert = _mm512_set_ps(
          0, 0, 0, 0,
          (f == 3) ? v.z : 0, (f == 3) ? v.y : 0, (f == 3) ? v.x : 0, 0,
          (f == 2) ? v.z : 0, (f == 2) ? v.y : 0, (f == 2) ? v.x : 0, 0,
          (f == 1) ? v.z : 0, (f == 1) ? v.y : 0, (f == 1) ? v.x : 0, 0,
          (f == 0) ? v.z : 0, (f == 0) ? v.y : 0, (f == 0) ? v.x : 0, 0
        );
        sum = _mm512_add_ps(sum, vert);
      }
    }

    // Divide by face vertex counts
    alignas(64) float result[16];
    _mm512_store_ps(result, sum);

    for (int f = 0; f < 4; ++f) {
      const Face& face = faces[fi + f];
      float inv = 1.0f / static_cast<float>(face.vertices.size());
      Vec3f center(
        result[f * 4] * inv,
        result[f * 4 + 1] * inv,
        result[f * 4 + 2] * inv
      );
      face_points.push_back(center);
    }
  }

  // Handle remaining faces
  for (; fi < faces.size(); ++fi) {
    const Face& face = faces[fi];
    Vec3f center(0, 0, 0);
    for (uint32_t vi : face.vertices) {
      center += vertices[vi];
    }
    center = center / static_cast<float>(face.vertices.size());
    face_points.push_back(center);
  }
}

void catmull_clark_avx512(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full AVX512-optimized Catmull-Clark
  // For now, delegate to scalar version
  mesh.subdivide_catmull_clark(level);
}

#endif // TINYSUBDIV_USE_AVX512

#ifdef TINYSUBDIV_USE_NEON

inline float32x4_t vec3_load_neon(const Vec3f& v) {
  float32x4_t result = vdupq_n_f32(0.0f);
  result = vsetq_lane_f32(v.x, result, 0);
  result = vsetq_lane_f32(v.y, result, 1);
  result = vsetq_lane_f32(v.z, result, 2);
  return result;
}

inline void vec3_store_neon(Vec3f& v, float32x4_t val) {
  v.x = vgetq_lane_f32(val, 0);
  v.y = vgetq_lane_f32(val, 1);
  v.z = vgetq_lane_f32(val, 2);
}

static void compute_face_points_neon(const ChunkedTypedArray<Vec3f>& vertices,
                                    const ChunkedTypedArray<Face>& faces,
                                    ChunkedTypedArray<Vec3f>& face_points) {
  face_points.clear();
  face_points.reserve(faces.size());

  for (size_t fi = 0; fi < faces.size(); ++fi) {
    const Face& face = faces[fi];
    float32x4_t sum = vdupq_n_f32(0.0f);

    for (uint32_t vi : face.vertices) {
      float32x4_t v = vec3_load_neon(vertices[vi]);
      sum = vaddq_f32(sum, v);
    }

    const float inv_count = 1.0f / static_cast<float>(face.vertices.size());
    float32x4_t center = vmulq_n_f32(sum, inv_count);

    Vec3f result;
    vec3_store_neon(result, center);
    face_points.push_back(result);
  }
}

static void compute_edge_points_neon(const ChunkedTypedArray<Vec3f>& vertices,
                                    const ChunkedTypedArray<Edge>& edges,
                                    const ChunkedTypedArray<Vec3f>& face_points,
                                    ChunkedTypedArray<Vec3f>& edge_points) {
  edge_points.clear();
  edge_points.reserve(edges.size());

  for (size_t i = 0; i < edges.size(); ++i) {
    const Edge& edge = edges[i];

    float32x4_t v0 = vec3_load_neon(vertices[edge.v0]);
    float32x4_t v1 = vec3_load_neon(vertices[edge.v1]);

    if (edge.is_crease || edge.is_boundary()) {
      float32x4_t mid = vmulq_n_f32(vaddq_f32(v0, v1), 0.5f);
      Vec3f result;
      vec3_store_neon(result, mid);
      edge_points.push_back(result);
    } else {
      float32x4_t edge_center = vmulq_n_f32(vaddq_f32(v0, v1), 0.5f);
      float32x4_t face_center = vec3_load_neon(face_points[edge.f0]);

      if (edge.f1 != Edge::INVALID_FACE) {
        float32x4_t f1 = vec3_load_neon(face_points[edge.f1]);
        face_center = vmulq_n_f32(vaddq_f32(face_center, f1), 0.5f);
      }

      float32x4_t edge_point = vmulq_n_f32(vaddq_f32(edge_center, face_center), 0.5f);
      Vec3f result;
      vec3_store_neon(result, edge_point);
      edge_points.push_back(result);
    }
  }
}

void catmull_clark_neon(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full NEON-optimized Catmull-Clark
  // For now, delegate to scalar version
  mesh.subdivide_catmull_clark(level);
}

#endif // TINYSUBDIV_USE_NEON

// Loop subdivision SIMD implementations

#ifdef TINYSUBDIV_USE_SSE

static void compute_loop_edge_points_sse(const ChunkedTypedArray<Vec3f>& vertices,
                                        const ChunkedTypedArray<Edge>& edges,
                                        const ChunkedTypedArray<Face>& faces,
                                        ChunkedTypedArray<Vec3f>& edge_points) {
  edge_points.clear();
  edge_points.reserve(edges.size());

  const __m128 three_eighths = _mm_set1_ps(3.0f / 8.0f);
  const __m128 one_eighth = _mm_set1_ps(1.0f / 8.0f);
  const __m128 half = _mm_set1_ps(0.5f);

  for (size_t i = 0; i < edges.size(); ++i) {
    const Edge& edge = edges[i];

    if (edge.is_crease || edge.is_boundary()) {
      // Boundary edge: midpoint
      __m128 v0 = vec3_load(vertices[edge.v0]);
      __m128 v1 = vec3_load(vertices[edge.v1]);
      __m128 midpoint = vec3_mul(vec3_add(v0, v1), 0.5f);

      Vec3f result;
      vec3_store(result, midpoint);
      edge_points.push_back(result);
    } else {
      // Interior edge: Loop formula
      __m128 v0 = vec3_load(vertices[edge.v0]);
      __m128 v1 = vec3_load(vertices[edge.v1]);
      __m128 edge_contrib = _mm_mul_ps(vec3_add(v0, v1), three_eighths);

      // Find opposite vertices
      __m128 opposite_sum = _mm_setzero_ps();
      int opposite_count = 0;

      if (edge.f0 != Edge::INVALID_FACE) {
        const Face& face0 = faces[edge.f0];
        for (uint32_t vid : face0.vertices) {
          if (vid != edge.v0 && vid != edge.v1) {
            opposite_sum = vec3_add(opposite_sum, vec3_load(vertices[vid]));
            opposite_count++;
          }
        }
      }

      if (edge.f1 != Edge::INVALID_FACE) {
        const Face& face1 = faces[edge.f1];
        for (uint32_t vid : face1.vertices) {
          if (vid != edge.v0 && vid != edge.v1) {
            opposite_sum = vec3_add(opposite_sum, vec3_load(vertices[vid]));
            opposite_count++;
          }
        }
      }

      __m128 edge_point = edge_contrib;
      if (opposite_count > 0) {
        float scale = 1.0f / (8.0f * opposite_count);
        edge_point = vec3_add(edge_point, vec3_mul(opposite_sum, scale));
      }

      Vec3f result;
      vec3_store(result, edge_point);
      edge_points.push_back(result);
    }
  }
}

void loop_subdivision_sse(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full SSE-optimized Loop subdivision
  mesh.subdivide_loop(level);
}

#endif // TINYSUBDIV_USE_SSE

#ifdef TINYSUBDIV_USE_AVX

static void compute_loop_edge_points_avx(const ChunkedTypedArray<Vec3f>& vertices,
                                        const ChunkedTypedArray<Edge>& edges,
                                        const ChunkedTypedArray<Face>& faces,
                                        ChunkedTypedArray<Vec3f>& edge_points) {
  edge_points.clear();
  edge_points.reserve(edges.size());

  // Process two edges at a time with AVX
  size_t i = 0;
  for (; i + 1 < edges.size(); i += 2) {
    const Edge& edge1 = edges[i];
    const Edge& edge2 = edges[i + 1];

    if ((edge1.is_crease || edge1.is_boundary()) &&
        (edge2.is_crease || edge2.is_boundary())) {
      // Both boundary edges
      Vec3f v1_0 = vertices[edge1.v0];
      Vec3f v1_1 = vertices[edge1.v1];
      Vec3f v2_0 = vertices[edge2.v0];
      Vec3f v2_1 = vertices[edge2.v1];

      __m256 verts1 = vec3x2_load(v1_0, v2_0);
      __m256 verts2 = vec3x2_load(v1_1, v2_1);
      __m256 midpoints = _mm256_mul_ps(_mm256_add_ps(verts1, verts2), _mm256_set1_ps(0.5f));

      Vec3f result1, result2;
      vec3x2_store(result1, result2, midpoints);
      edge_points.push_back(result1);
      edge_points.push_back(result2);
    } else {
      // Process edges individually if they have different characteristics
      for (int j = 0; j < 2; ++j) {
        const Edge& edge = edges[i + j];
        Vec3f edge_point;

        if (edge.is_crease || edge.is_boundary()) {
          edge_point = (vertices[edge.v0] + vertices[edge.v1]) * 0.5f;
        } else {
          Vec3f v0 = vertices[edge.v0];
          Vec3f v1 = vertices[edge.v1];
          edge_point = (v0 + v1) * (3.0f / 8.0f);

          // Add opposite vertex contributions
          Vec3f opposite_sum(0, 0, 0);
          int opposite_count = 0;

          if (edge.f0 != Edge::INVALID_FACE) {
            const Face& face = faces[edge.f0];
            for (uint32_t vid : face.vertices) {
              if (vid != edge.v0 && vid != edge.v1) {
                opposite_sum += vertices[vid];
                opposite_count++;
              }
            }
          }

          if (edge.f1 != Edge::INVALID_FACE) {
            const Face& face = faces[edge.f1];
            for (uint32_t vid : face.vertices) {
              if (vid != edge.v0 && vid != edge.v1) {
                opposite_sum += vertices[vid];
                opposite_count++;
              }
            }
          }

          if (opposite_count > 0) {
            edge_point += opposite_sum * (1.0f / (8.0f * opposite_count));
          }
        }

        edge_points.push_back(edge_point);
      }
    }
  }

  // Handle remaining edge
  if (i < edges.size()) {
    const Edge& edge = edges[i];
    Vec3f edge_point;

    if (edge.is_crease || edge.is_boundary()) {
      edge_point = (vertices[edge.v0] + vertices[edge.v1]) * 0.5f;
    } else {
      edge_point = (vertices[edge.v0] + vertices[edge.v1]) * (3.0f / 8.0f);
      // Add opposite vertices contribution (simplified)
    }

    edge_points.push_back(edge_point);
  }
}

void loop_subdivision_avx(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full AVX-optimized Loop subdivision
  mesh.subdivide_loop(level);
}

#endif // TINYSUBDIV_USE_AVX

#ifdef TINYSUBDIV_USE_NEON

static void compute_loop_edge_points_neon(const ChunkedTypedArray<Vec3f>& vertices,
                                         const ChunkedTypedArray<Edge>& edges,
                                         const ChunkedTypedArray<Face>& faces,
                                         ChunkedTypedArray<Vec3f>& edge_points) {
  edge_points.clear();
  edge_points.reserve(edges.size());

  const float32x4_t three_eighths = vdupq_n_f32(3.0f / 8.0f);
  const float32x4_t half = vdupq_n_f32(0.5f);

  for (size_t i = 0; i < edges.size(); ++i) {
    const Edge& edge = edges[i];

    if (edge.is_crease || edge.is_boundary()) {
      float32x4_t v0 = vec3_load_neon(vertices[edge.v0]);
      float32x4_t v1 = vec3_load_neon(vertices[edge.v1]);
      float32x4_t midpoint = vmulq_f32(vaddq_f32(v0, v1), half);

      Vec3f result;
      vec3_store_neon(result, midpoint);
      edge_points.push_back(result);
    } else {
      float32x4_t v0 = vec3_load_neon(vertices[edge.v0]);
      float32x4_t v1 = vec3_load_neon(vertices[edge.v1]);
      float32x4_t edge_contrib = vmulq_f32(vaddq_f32(v0, v1), three_eighths);

      // Find opposite vertices
      float32x4_t opposite_sum = vdupq_n_f32(0.0f);
      int opposite_count = 0;

      if (edge.f0 != Edge::INVALID_FACE) {
        const Face& face0 = faces[edge.f0];
        for (uint32_t vid : face0.vertices) {
          if (vid != edge.v0 && vid != edge.v1) {
            opposite_sum = vaddq_f32(opposite_sum, vec3_load_neon(vertices[vid]));
            opposite_count++;
          }
        }
      }

      if (edge.f1 != Edge::INVALID_FACE) {
        const Face& face1 = faces[edge.f1];
        for (uint32_t vid : face1.vertices) {
          if (vid != edge.v0 && vid != edge.v1) {
            opposite_sum = vaddq_f32(opposite_sum, vec3_load_neon(vertices[vid]));
            opposite_count++;
          }
        }
      }

      float32x4_t edge_point = edge_contrib;
      if (opposite_count > 0) {
        float scale = 1.0f / (8.0f * opposite_count);
        edge_point = vaddq_f32(edge_point, vmulq_n_f32(opposite_sum, scale));
      }

      Vec3f result;
      vec3_store_neon(result, edge_point);
      edge_points.push_back(result);
    }
  }
}

void loop_subdivision_neon(SubdivisionMesh& mesh, int level) {
  // TODO: Implement full NEON-optimized Loop subdivision
  mesh.subdivide_loop(level);
}

#endif // TINYSUBDIV_USE_NEON

}  // namespace tinysubdiv