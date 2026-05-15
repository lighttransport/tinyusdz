// Benchmark for position-bucketed vertex deduplication.
//
// Generates synthetic triangle-grid meshes with facevarying attributes and
// measures the dedup algorithm used by BuildVertexIndicesImpl and
// ComputeTangentsAndBinormals.
//
// Usage:
//   make && ./bench_mesh_build [--sizes 1000,10000,...] [--tangent-only] [--vertex-only]
//
// The mesh generator creates a flat grid of R×C vertices (≈ num_points),
// triangulated into 2·(R-1)·(C-1) triangles → 6·(R-1)·(C-1) face-vertices.
//
// Two shading modes are tested:
//   Smooth — all face-vertices at a position share the same normal → high merge rate
//   Flat   — each triangle gets its own face normal → minimal merging
//
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Lightweight types matching value::float2 / value::float3
// ---------------------------------------------------------------------------
using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;

// ---------------------------------------------------------------------------
// is_close: matches math::is_close from TinyUSDZ (math-util.inc).
// Relative + absolute epsilon: |a-b| <= eps  OR  |a-b| <= eps * max(|a|,|b|)
// ---------------------------------------------------------------------------
static inline bool is_close(float a, float b, float eps) {
  float d = a - b;
  if (std::fabs(d) <= eps) return true;
  return std::fabs(d) <= (eps * std::fmax(std::fabs(a), std::fabs(b)));
}

static inline bool is_close_f2(const float2 &a, const float2 &b, float eps) {
  return is_close(a[0], b[0], eps) && is_close(a[1], b[1], eps);
}

static inline bool is_close_f3(const float3 &a, const float3 &b, float eps) {
  return is_close(a[0], b[0], eps) && is_close(a[1], b[1], eps) &&
         is_close(a[2], b[2], eps);
}

// ---------------------------------------------------------------------------
// Synthetic mesh generator
// ---------------------------------------------------------------------------
struct SyntheticMesh {
  size_t rows;
  size_t cols;

  // Vertex-varying
  std::vector<float3> points;  // rows * cols

  // Per-face-vertex (facevarying)
  std::vector<uint32_t> faceVertexIndices;  // 6 * (rows-1) * (cols-1)
  std::vector<float3> normals;
  std::vector<float2> uv0s;
  // Optional extra attributes for the full-vertex benchmark
  std::vector<float2> uv1s;
  std::vector<float3> tangents;
  std::vector<float3> binormals;
  std::vector<float3> colors;
  std::vector<float> opacities;

  // Compute total memory footprint of input data
  size_t input_bytes() const {
    size_t n = 0;
    n += points.capacity() * sizeof(float3);
    n += faceVertexIndices.capacity() * sizeof(uint32_t);
    n += normals.capacity() * sizeof(float3);
    n += uv0s.capacity() * sizeof(float2);
    n += uv1s.capacity() * sizeof(float2);
    n += tangents.capacity() * sizeof(float3);
    n += binormals.capacity() * sizeof(float3);
    n += colors.capacity() * sizeof(float3);
    n += opacities.capacity() * sizeof(float);
    return n;
  }
};

static float3 cross(const float3 &a, const float3 &b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

static float3 normalize(const float3 &v) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len < 1e-12f) return {0.0f, 0.0f, 1.0f};
  return {v[0] / len, v[1] / len, v[2] / len};
}

static float3 sub(const float3 &a, const float3 &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

// Attribute flags for fine-grained control of which attributes to generate.
enum AttribFlags : uint32_t {
  A_NORMAL   = 1 << 0,
  A_UV0      = 1 << 1,
  A_UV1      = 1 << 2,
  A_TANGENT  = 1 << 3,
  A_BINORMAL = 1 << 4,
  A_COLOR    = 1 << 5,
  A_OPACITY  = 1 << 6,
};

// Preset combinations matching real production scenarios.
static constexpr uint32_t ATTRIB_BASIC     = A_NORMAL | A_UV0;
static constexpr uint32_t ATTRIB_MULTI_UV  = A_NORMAL | A_UV0 | A_UV1;
static constexpr uint32_t ATTRIB_TANGENT   = A_NORMAL | A_UV0 | A_TANGENT | A_BINORMAL;
static constexpr uint32_t ATTRIB_MULTI_UV_TAN = A_NORMAL | A_UV0 | A_UV1 | A_TANGENT | A_BINORMAL;
static constexpr uint32_t ATTRIB_COLOR     = A_NORMAL | A_UV0 | A_COLOR | A_OPACITY;
static constexpr uint32_t ATTRIB_FULL      = A_NORMAL | A_UV0 | A_UV1 | A_TANGENT | A_BINORMAL | A_COLOR | A_OPACITY;

static const char *attrib_label(uint32_t flags) {
  if (flags == ATTRIB_BASIC)          return "nrm+uv0";
  if (flags == ATTRIB_MULTI_UV)       return "nrm+uv01";
  if (flags == ATTRIB_TANGENT)        return "nrm+uv0+tan";
  if (flags == ATTRIB_MULTI_UV_TAN)   return "nrm+uv01+tan";
  if (flags == ATTRIB_COLOR)          return "nrm+uv0+col";
  if (flags == ATTRIB_FULL)           return "all7";
  return "custom";
}

// Count bytes compared per face-vertex for a given attribute set.
static size_t attrib_cmp_bytes(uint32_t flags) {
  size_t n = 0;
  if (flags & A_NORMAL)   n += sizeof(float3);
  if (flags & A_UV0)      n += sizeof(float2);
  if (flags & A_UV1)      n += sizeof(float2);
  if (flags & A_TANGENT)  n += sizeof(float3);
  if (flags & A_BINORMAL) n += sizeof(float3);
  if (flags & A_COLOR)    n += sizeof(float3);
  if (flags & A_OPACITY)  n += sizeof(float);
  return n;
}

// Generate a grid mesh with slight height variation for non-trivial normals.
// flat_shading: if true, each triangle gets its own face normal (poor merge).
//               if false, all face-verts at a position share the smooth normal.
static SyntheticMesh generate_grid(size_t approx_points, bool flat_shading,
                                   uint32_t attrib_flags = ATTRIB_BASIC) {
  SyntheticMesh mesh;

  size_t side =
      static_cast<size_t>(std::ceil(std::sqrt(double(approx_points))));
  if (side < 2) side = 2;
  mesh.rows = side;
  mesh.cols = side;
  size_t num_points = side * side;
  size_t num_tris = 2 * (side - 1) * (side - 1);
  size_t num_fvs = 3 * num_tris;

  // -- Points on a gently curved surface --
  mesh.points.resize(num_points);
  for (size_t r = 0; r < side; r++) {
    for (size_t c = 0; c < side; c++) {
      float x = float(c) / float(side - 1);
      float y = float(r) / float(side - 1);
      float z = 0.05f * std::sin(x * 6.2831853f) * std::cos(y * 6.2831853f);
      mesh.points[r * side + c] = {x, y, z};
    }
  }

  // -- Face-vertex indices (two triangles per quad) --
  mesh.faceVertexIndices.reserve(num_fvs);
  for (size_t r = 0; r < side - 1; r++) {
    for (size_t c = 0; c < side - 1; c++) {
      uint32_t v00 = uint32_t(r * side + c);
      uint32_t v10 = uint32_t((r + 1) * side + c);
      uint32_t v01 = uint32_t(r * side + c + 1);
      uint32_t v11 = uint32_t((r + 1) * side + c + 1);
      mesh.faceVertexIndices.push_back(v00);
      mesh.faceVertexIndices.push_back(v10);
      mesh.faceVertexIndices.push_back(v01);
      mesh.faceVertexIndices.push_back(v01);
      mesh.faceVertexIndices.push_back(v10);
      mesh.faceVertexIndices.push_back(v11);
    }
  }

  // -- Normals (facevarying) --
  if (attrib_flags & A_NORMAL) {
    mesh.normals.resize(num_fvs);
    if (flat_shading) {
      for (size_t t = 0; t < num_tris; t++) {
        uint32_t i0 = mesh.faceVertexIndices[3 * t + 0];
        uint32_t i1 = mesh.faceVertexIndices[3 * t + 1];
        uint32_t i2 = mesh.faceVertexIndices[3 * t + 2];
        float3 fn = normalize(cross(sub(mesh.points[i1], mesh.points[i0]),
                                    sub(mesh.points[i2], mesh.points[i0])));
        mesh.normals[3 * t + 0] = fn;
        mesh.normals[3 * t + 1] = fn;
        mesh.normals[3 * t + 2] = fn;
      }
    } else {
      std::vector<float3> vertex_normals(num_points, {0.0f, 0.0f, 0.0f});
      for (size_t t = 0; t < num_tris; t++) {
        uint32_t i0 = mesh.faceVertexIndices[3 * t + 0];
        uint32_t i1 = mesh.faceVertexIndices[3 * t + 1];
        uint32_t i2 = mesh.faceVertexIndices[3 * t + 2];
        float3 fn = cross(sub(mesh.points[i1], mesh.points[i0]),
                           sub(mesh.points[i2], mesh.points[i0]));
        for (int k = 0; k < 3; k++) {
          vertex_normals[i0][k] += fn[k];
          vertex_normals[i1][k] += fn[k];
          vertex_normals[i2][k] += fn[k];
        }
      }
      for (auto &n : vertex_normals) n = normalize(n);
      for (size_t i = 0; i < num_fvs; i++) {
        mesh.normals[i] = vertex_normals[mesh.faceVertexIndices[i]];
      }
    }
  }

  // -- UV0 (facevarying, derived from position) --
  if (attrib_flags & A_UV0) {
    mesh.uv0s.resize(num_fvs);
    for (size_t i = 0; i < num_fvs; i++) {
      const float3 &p = mesh.points[mesh.faceVertexIndices[i]];
      mesh.uv0s[i] = {p[0], p[1]};
    }
  }

  // -- UV1 (second UV set, scaled/offset from UV0) --
  if (attrib_flags & A_UV1) {
    mesh.uv1s.resize(num_fvs);
    for (size_t i = 0; i < num_fvs; i++) {
      const float3 &p = mesh.points[mesh.faceVertexIndices[i]];
      mesh.uv1s[i] = {p[0] * 0.5f + 0.25f, p[1] * 0.5f + 0.25f};
    }
  }

  // -- Tangent / Binormal (user-supplied, derived from surface) --
  if (attrib_flags & A_TANGENT) {
    mesh.tangents.resize(num_fvs);
    for (size_t i = 0; i < num_fvs; i++) {
      // Approximate tangent along U direction
      const float3 &p = mesh.points[mesh.faceVertexIndices[i]];
      float dx = -0.05f * 6.2831853f * std::cos(p[0] * 6.2831853f) *
                 std::cos(p[1] * 6.2831853f);
      mesh.tangents[i] = normalize({1.0f, 0.0f, dx});
    }
  }
  if (attrib_flags & A_BINORMAL) {
    mesh.binormals.resize(num_fvs);
    for (size_t i = 0; i < num_fvs; i++) {
      const float3 &p = mesh.points[mesh.faceVertexIndices[i]];
      float dy = 0.05f * 6.2831853f * std::sin(p[0] * 6.2831853f) *
                 std::sin(p[1] * 6.2831853f);
      mesh.binormals[i] = normalize({0.0f, 1.0f, dy});
    }
  }

  // -- Vertex color + opacity --
  if (attrib_flags & A_COLOR) {
    mesh.colors.resize(num_fvs);
    for (size_t i = 0; i < num_fvs; i++) {
      const float3 &p = mesh.points[mesh.faceVertexIndices[i]];
      mesh.colors[i] = {p[0], p[1], 0.5f};
    }
  }
  if (attrib_flags & A_OPACITY) {
    mesh.opacities.resize(num_fvs);
    for (size_t i = 0; i < num_fvs; i++) {
      mesh.opacities[i] = 1.0f;
    }
  }

  return mesh;
}

// Backward-compat wrapper for callers using bool full_attribs.
static SyntheticMesh generate_grid(size_t approx_points, bool flat_shading,
                                   bool full_attribs) {
  return generate_grid(approx_points, flat_shading,
                       full_attribs ? ATTRIB_FULL : ATTRIB_BASIC);
}

// ---------------------------------------------------------------------------
// Position-bucketed vertex dedup (same algorithm as BuildVertexIndicesImpl)
// ---------------------------------------------------------------------------

struct BucketEntry {
  uint32_t fv_index;
  uint32_t out_vertex_id;
};

// Measure actual memory of bucket storage (inner vector allocations + outer
// vector).
static size_t measure_bucket_mem(
    const std::vector<std::vector<BucketEntry>> &buckets) {
  // Outer vector storage for the vector-of-vectors headers
  size_t mem = buckets.capacity() * sizeof(std::vector<BucketEntry>);
  // Each inner vector's heap allocation
  for (const auto &b : buckets) {
    mem += b.capacity() * sizeof(BucketEntry);
  }
  return mem;
}

struct MemStats {
  size_t input_bytes;    // input mesh data
  size_t bucket_bytes;   // bucket storage during dedup
  size_t output_bytes;   // output index arrays
  size_t total_bytes;    // input + bucket + output (peak working set)
};

struct VertexDedupResult {
  std::vector<uint32_t> out_indices;
  std::vector<uint32_t> out_point_indices;
  uint32_t num_unique;
  MemStats mem;
};

static VertexDedupResult run_vertex_dedup(const SyntheticMesh &mesh) {
  size_t num_fvs = mesh.faceVertexIndices.size();
  size_t num_verts = mesh.points.size();

  const float3 *normals_ptr =
      mesh.normals.empty() ? nullptr : mesh.normals.data();
  const float2 *uv0_ptr = mesh.uv0s.empty() ? nullptr : mesh.uv0s.data();
  const float2 *uv1_ptr = mesh.uv1s.empty() ? nullptr : mesh.uv1s.data();
  const float3 *tangents_ptr =
      mesh.tangents.empty() ? nullptr : mesh.tangents.data();
  const float3 *binormals_ptr =
      mesh.binormals.empty() ? nullptr : mesh.binormals.data();
  const float3 *colors_ptr =
      mesh.colors.empty() ? nullptr : mesh.colors.data();
  const float *opacities_ptr =
      mesh.opacities.empty() ? nullptr : mesh.opacities.data();

  auto attribs_match = [&](size_t a, size_t b) -> bool {
    if (normals_ptr &&
        memcmp(&normals_ptr[a], &normals_ptr[b], sizeof(float3)) != 0)
      return false;
    if (uv0_ptr && memcmp(&uv0_ptr[a], &uv0_ptr[b], sizeof(float2)) != 0)
      return false;
    if (uv1_ptr && memcmp(&uv1_ptr[a], &uv1_ptr[b], sizeof(float2)) != 0)
      return false;
    if (tangents_ptr &&
        memcmp(&tangents_ptr[a], &tangents_ptr[b], sizeof(float3)) != 0)
      return false;
    if (binormals_ptr &&
        memcmp(&binormals_ptr[a], &binormals_ptr[b], sizeof(float3)) != 0)
      return false;
    if (colors_ptr &&
        memcmp(&colors_ptr[a], &colors_ptr[b], sizeof(float3)) != 0)
      return false;
    if (opacities_ptr &&
        memcmp(&opacities_ptr[a], &opacities_ptr[b], sizeof(float)) != 0)
      return false;
    return true;
  };

  std::vector<std::vector<BucketEntry>> buckets(num_verts);
  VertexDedupResult result;
  result.out_indices.resize(num_fvs);
  result.out_point_indices.resize(num_fvs);
  uint32_t next_vertex_id = 0;

  for (size_t i = 0; i < num_fvs; i++) {
    uint32_t pid = mesh.faceVertexIndices[i];
    auto &bucket = buckets[pid];
    uint32_t matched_id = ~0u;
    for (const auto &entry : bucket) {
      if (attribs_match(i, entry.fv_index)) {
        matched_id = entry.out_vertex_id;
        break;
      }
    }
    if (matched_id == ~0u) {
      matched_id = next_vertex_id++;
      bucket.push_back({uint32_t(i), matched_id});
    }
    result.out_indices[i] = matched_id;
    result.out_point_indices[i] = pid;
  }

  result.num_unique = next_vertex_id;

  // Measure memory
  result.mem.input_bytes = mesh.input_bytes();
  result.mem.bucket_bytes = measure_bucket_mem(buckets);
  result.mem.output_bytes =
      result.out_indices.capacity() * sizeof(uint32_t) +
      result.out_point_indices.capacity() * sizeof(uint32_t);
  result.mem.total_bytes =
      result.mem.input_bytes + result.mem.bucket_bytes +
      result.mem.output_bytes;

  return result;
}

// ---------------------------------------------------------------------------
// Tangent dedup (same algorithm as ComputeTangentsAndBinormals dedup step)
// Only compares normal + uv within position buckets.
// ---------------------------------------------------------------------------

struct TangentDedupResult {
  std::vector<uint32_t> vertex_indices;
  uint32_t num_unique;
  MemStats mem;
};

static TangentDedupResult run_tangent_dedup(const SyntheticMesh &mesh) {
  size_t num_fvs = mesh.faceVertexIndices.size();
  uint32_t num_points =
      *std::max_element(mesh.faceVertexIndices.begin(),
                        mesh.faceVertexIndices.end()) +
      1;

  const float3 *nrm_ptr = mesh.normals.data();
  const float2 *uv_ptr = mesh.uv0s.data();

  auto attribs_match = [&](size_t a, size_t b) -> bool {
    if (memcmp(&nrm_ptr[a], &nrm_ptr[b], sizeof(float3)) != 0) return false;
    if (memcmp(&uv_ptr[a], &uv_ptr[b], sizeof(float2)) != 0) return false;
    return true;
  };

  std::vector<std::vector<BucketEntry>> buckets(num_points);
  TangentDedupResult result;
  result.vertex_indices.resize(num_fvs);
  uint32_t next_vertex_id = 0;

  for (size_t i = 0; i < num_fvs; i++) {
    uint32_t pid = mesh.faceVertexIndices[i];
    auto &bucket = buckets[pid];
    uint32_t matched_id = ~0u;
    for (const auto &entry : bucket) {
      if (attribs_match(i, entry.fv_index)) {
        matched_id = entry.out_vertex_id;
        break;
      }
    }
    if (matched_id == ~0u) {
      matched_id = next_vertex_id++;
      bucket.push_back({uint32_t(i), matched_id});
    }
    result.vertex_indices[i] = matched_id;
  }

  result.num_unique = next_vertex_id;

  // Measure memory
  result.mem.input_bytes = mesh.input_bytes();
  result.mem.bucket_bytes = measure_bucket_mem(buckets);
  result.mem.output_bytes =
      result.vertex_indices.capacity() * sizeof(uint32_t);
  result.mem.total_bytes =
      result.mem.input_bytes + result.mem.bucket_bytes +
      result.mem.output_bytes;

  return result;
}

// ---------------------------------------------------------------------------
// FNV-1a hashed variant: hash attributes within each position bucket, then
// probe a per-bucket unordered_map instead of linear scan.
// This tests whether hashing helps when buckets are small (valence 4-8).
// ---------------------------------------------------------------------------

#include <unordered_map>

static inline uint32_t fnv1a_bytes(const void *data, size_t len,
                                   uint32_t hash = 0x811c9dc5u) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < len; i++) {
    hash ^= p[i];
    hash *= 0x01000193u;
  }
  return hash;
}

struct HashedBucketEntry {
  uint32_t attr_hash;
  uint32_t fv_index;
  uint32_t out_vertex_id;
};

static size_t measure_hashed_bucket_mem(
    const std::vector<std::vector<HashedBucketEntry>> &buckets) {
  size_t mem = buckets.capacity() * sizeof(std::vector<HashedBucketEntry>);
  for (const auto &b : buckets) {
    mem += b.capacity() * sizeof(HashedBucketEntry);
  }
  return mem;
}

static VertexDedupResult run_vertex_dedup_hashed(const SyntheticMesh &mesh) {
  size_t num_fvs = mesh.faceVertexIndices.size();
  size_t num_verts = mesh.points.size();

  const float3 *normals_ptr =
      mesh.normals.empty() ? nullptr : mesh.normals.data();
  const float2 *uv0_ptr = mesh.uv0s.empty() ? nullptr : mesh.uv0s.data();
  const float2 *uv1_ptr = mesh.uv1s.empty() ? nullptr : mesh.uv1s.data();
  const float3 *tangents_ptr =
      mesh.tangents.empty() ? nullptr : mesh.tangents.data();
  const float3 *binormals_ptr =
      mesh.binormals.empty() ? nullptr : mesh.binormals.data();
  const float3 *colors_ptr =
      mesh.colors.empty() ? nullptr : mesh.colors.data();
  const float *opacities_ptr =
      mesh.opacities.empty() ? nullptr : mesh.opacities.data();

  // Hash the attributes at face-vertex index i
  auto hash_attribs = [&](size_t i) -> uint32_t {
    uint32_t h = 0x811c9dc5u;
    if (normals_ptr) h = fnv1a_bytes(&normals_ptr[i], sizeof(float3), h);
    if (uv0_ptr) h = fnv1a_bytes(&uv0_ptr[i], sizeof(float2), h);
    if (uv1_ptr) h = fnv1a_bytes(&uv1_ptr[i], sizeof(float2), h);
    if (tangents_ptr) h = fnv1a_bytes(&tangents_ptr[i], sizeof(float3), h);
    if (binormals_ptr) h = fnv1a_bytes(&binormals_ptr[i], sizeof(float3), h);
    if (colors_ptr) h = fnv1a_bytes(&colors_ptr[i], sizeof(float3), h);
    if (opacities_ptr) h = fnv1a_bytes(&opacities_ptr[i], sizeof(float), h);
    return h;
  };

  auto attribs_match = [&](size_t a, size_t b) -> bool {
    if (normals_ptr &&
        memcmp(&normals_ptr[a], &normals_ptr[b], sizeof(float3)) != 0)
      return false;
    if (uv0_ptr && memcmp(&uv0_ptr[a], &uv0_ptr[b], sizeof(float2)) != 0)
      return false;
    if (uv1_ptr && memcmp(&uv1_ptr[a], &uv1_ptr[b], sizeof(float2)) != 0)
      return false;
    if (tangents_ptr &&
        memcmp(&tangents_ptr[a], &tangents_ptr[b], sizeof(float3)) != 0)
      return false;
    if (binormals_ptr &&
        memcmp(&binormals_ptr[a], &binormals_ptr[b], sizeof(float3)) != 0)
      return false;
    if (colors_ptr &&
        memcmp(&colors_ptr[a], &colors_ptr[b], sizeof(float3)) != 0)
      return false;
    if (opacities_ptr &&
        memcmp(&opacities_ptr[a], &opacities_ptr[b], sizeof(float)) != 0)
      return false;
    return true;
  };

  // Each position bucket stores entries keyed by attribute hash.
  // On collision, linear probe within same-hash entries (rare).
  std::vector<std::vector<HashedBucketEntry>> buckets(num_verts);
  VertexDedupResult result;
  result.out_indices.resize(num_fvs);
  result.out_point_indices.resize(num_fvs);
  uint32_t next_vertex_id = 0;

  for (size_t i = 0; i < num_fvs; i++) {
    uint32_t pid = mesh.faceVertexIndices[i];
    auto &bucket = buckets[pid];
    uint32_t h = hash_attribs(i);
    uint32_t matched_id = ~0u;
    for (const auto &entry : bucket) {
      if (entry.attr_hash == h && attribs_match(i, entry.fv_index)) {
        matched_id = entry.out_vertex_id;
        break;
      }
    }
    if (matched_id == ~0u) {
      matched_id = next_vertex_id++;
      bucket.push_back({h, uint32_t(i), matched_id});
    }
    result.out_indices[i] = matched_id;
    result.out_point_indices[i] = pid;
  }

  result.num_unique = next_vertex_id;

  result.mem.input_bytes = mesh.input_bytes();
  result.mem.bucket_bytes = measure_hashed_bucket_mem(buckets);
  result.mem.output_bytes =
      result.out_indices.capacity() * sizeof(uint32_t) +
      result.out_point_indices.capacity() * sizeof(uint32_t);
  result.mem.total_bytes =
      result.mem.input_bytes + result.mem.bucket_bytes +
      result.mem.output_bytes;

  return result;
}

// ---------------------------------------------------------------------------
// Global hash-map variant: unordered_multimap<uint64_t, {fv_index, vertex_id}>
// keyed by FNV-1a hash of (position_index + attributes), like the old
// BuildIndices approach. On hash collision, verify with full memcmp.
// ---------------------------------------------------------------------------

static VertexDedupResult run_vertex_dedup_global_hashmap(
    const SyntheticMesh &mesh) {
  size_t num_fvs = mesh.faceVertexIndices.size();

  const float3 *normals_ptr =
      mesh.normals.empty() ? nullptr : mesh.normals.data();
  const float2 *uv0_ptr = mesh.uv0s.empty() ? nullptr : mesh.uv0s.data();
  const float2 *uv1_ptr = mesh.uv1s.empty() ? nullptr : mesh.uv1s.data();
  const float3 *tangents_ptr =
      mesh.tangents.empty() ? nullptr : mesh.tangents.data();
  const float3 *binormals_ptr =
      mesh.binormals.empty() ? nullptr : mesh.binormals.data();
  const float3 *colors_ptr =
      mesh.colors.empty() ? nullptr : mesh.colors.data();
  const float *opacities_ptr =
      mesh.opacities.empty() ? nullptr : mesh.opacities.data();

  auto hash_fv = [&](size_t i) -> uint64_t {
    uint32_t pid = mesh.faceVertexIndices[i];
    uint32_t h = fnv1a_bytes(&pid, sizeof(pid));
    if (normals_ptr) h = fnv1a_bytes(&normals_ptr[i], sizeof(float3), h);
    if (uv0_ptr) h = fnv1a_bytes(&uv0_ptr[i], sizeof(float2), h);
    if (uv1_ptr) h = fnv1a_bytes(&uv1_ptr[i], sizeof(float2), h);
    if (tangents_ptr) h = fnv1a_bytes(&tangents_ptr[i], sizeof(float3), h);
    if (binormals_ptr) h = fnv1a_bytes(&binormals_ptr[i], sizeof(float3), h);
    if (colors_ptr) h = fnv1a_bytes(&colors_ptr[i], sizeof(float3), h);
    if (opacities_ptr) h = fnv1a_bytes(&opacities_ptr[i], sizeof(float), h);
    return uint64_t(pid) << 32 | h;
  };

  auto attribs_match_full = [&](size_t a, size_t b) -> bool {
    if (mesh.faceVertexIndices[a] != mesh.faceVertexIndices[b]) return false;
    if (normals_ptr &&
        memcmp(&normals_ptr[a], &normals_ptr[b], sizeof(float3)) != 0)
      return false;
    if (uv0_ptr && memcmp(&uv0_ptr[a], &uv0_ptr[b], sizeof(float2)) != 0)
      return false;
    if (uv1_ptr && memcmp(&uv1_ptr[a], &uv1_ptr[b], sizeof(float2)) != 0)
      return false;
    if (tangents_ptr &&
        memcmp(&tangents_ptr[a], &tangents_ptr[b], sizeof(float3)) != 0)
      return false;
    if (binormals_ptr &&
        memcmp(&binormals_ptr[a], &binormals_ptr[b], sizeof(float3)) != 0)
      return false;
    if (colors_ptr &&
        memcmp(&colors_ptr[a], &colors_ptr[b], sizeof(float3)) != 0)
      return false;
    if (opacities_ptr &&
        memcmp(&opacities_ptr[a], &opacities_ptr[b], sizeof(float)) != 0)
      return false;
    return true;
  };

  struct HashMapEntry {
    uint32_t fv_index;
    uint32_t out_vertex_id;
  };

  std::unordered_multimap<uint64_t, HashMapEntry> index_map;

  VertexDedupResult result;
  result.out_indices.resize(num_fvs);
  result.out_point_indices.resize(num_fvs);
  uint32_t next_vertex_id = 0;

  for (size_t i = 0; i < num_fvs; i++) {
    uint64_t h = hash_fv(i);
    uint32_t matched_id = ~0u;
    auto range = index_map.equal_range(h);
    for (auto it = range.first; it != range.second; ++it) {
      if (attribs_match_full(i, it->second.fv_index)) {
        matched_id = it->second.out_vertex_id;
        break;
      }
    }
    if (matched_id == ~0u) {
      matched_id = next_vertex_id++;
      index_map.insert({h, {uint32_t(i), matched_id}});
    }
    result.out_indices[i] = matched_id;
    result.out_point_indices[i] = mesh.faceVertexIndices[i];
  }

  result.num_unique = next_vertex_id;

  // Measure hash map memory from bucket_count + element count.
  // Each node: key(8) + value(8) + next_ptr(8) + hash(8) ≈ 32-48 bytes typical.
  size_t hashmap_bucket_array = index_map.bucket_count() * sizeof(void *);
  size_t hashmap_nodes = index_map.size() * 48;  // typical libstdc++ node size
  result.mem.input_bytes = mesh.input_bytes();
  result.mem.bucket_bytes = hashmap_bucket_array + hashmap_nodes;
  result.mem.output_bytes =
      result.out_indices.capacity() * sizeof(uint32_t) +
      result.out_point_indices.capacity() * sizeof(uint32_t);
  result.mem.total_bytes =
      result.mem.input_bytes + result.mem.bucket_bytes +
      result.mem.output_bytes;

  return result;
}

// ---------------------------------------------------------------------------
// Position-bucketed dedup WITH valence safeguard (matches production code)
// ---------------------------------------------------------------------------

static VertexDedupResult run_vertex_dedup_guarded(const SyntheticMesh &mesh,
                                                   uint32_t max_valence) {
  size_t num_fvs = mesh.faceVertexIndices.size();
  size_t num_verts = mesh.points.size();

  const float3 *normals_ptr =
      mesh.normals.empty() ? nullptr : mesh.normals.data();
  const float2 *uv0_ptr = mesh.uv0s.empty() ? nullptr : mesh.uv0s.data();
  const float2 *uv1_ptr = mesh.uv1s.empty() ? nullptr : mesh.uv1s.data();
  const float3 *tangents_ptr =
      mesh.tangents.empty() ? nullptr : mesh.tangents.data();
  const float3 *binormals_ptr =
      mesh.binormals.empty() ? nullptr : mesh.binormals.data();
  const float3 *colors_ptr =
      mesh.colors.empty() ? nullptr : mesh.colors.data();
  const float *opacities_ptr =
      mesh.opacities.empty() ? nullptr : mesh.opacities.data();

  VertexDedupResult result;
  result.out_indices.resize(num_fvs);
  result.out_point_indices.resize(num_fvs);
  uint32_t next_vertex_id = 0;

  // Pre-count degree per position.
  bool flatten = false;
  uint32_t max_deg = 0;
  if (max_valence > 0) {
    std::vector<uint32_t> degree(num_verts, 0);
    for (size_t i = 0; i < num_fvs; i++) {
      degree[mesh.faceVertexIndices[i]]++;
    }
    max_deg = *std::max_element(degree.begin(), degree.end());
    if (max_deg > max_valence) {
      flatten = true;
    }
  }

  size_t bucket_mem = 0;

  if (flatten) {
    // Flatten: each face-vertex is its own unique vertex.
    for (size_t i = 0; i < num_fvs; i++) {
      result.out_indices[i] = uint32_t(i);
      result.out_point_indices[i] = mesh.faceVertexIndices[i];
    }
    next_vertex_id = uint32_t(num_fvs);
    bucket_mem = 0;
  } else {
    auto attribs_match = [&](size_t a, size_t b) -> bool {
      if (normals_ptr &&
          memcmp(&normals_ptr[a], &normals_ptr[b], sizeof(float3)) != 0)
        return false;
      if (uv0_ptr && memcmp(&uv0_ptr[a], &uv0_ptr[b], sizeof(float2)) != 0)
        return false;
      if (uv1_ptr && memcmp(&uv1_ptr[a], &uv1_ptr[b], sizeof(float2)) != 0)
        return false;
      if (tangents_ptr &&
          memcmp(&tangents_ptr[a], &tangents_ptr[b], sizeof(float3)) != 0)
        return false;
      if (binormals_ptr &&
          memcmp(&binormals_ptr[a], &binormals_ptr[b], sizeof(float3)) != 0)
        return false;
      if (colors_ptr &&
          memcmp(&colors_ptr[a], &colors_ptr[b], sizeof(float3)) != 0)
        return false;
      if (opacities_ptr &&
          memcmp(&opacities_ptr[a], &opacities_ptr[b], sizeof(float)) != 0)
        return false;
      return true;
    };

    std::vector<std::vector<BucketEntry>> buckets(num_verts);
    for (size_t i = 0; i < num_fvs; i++) {
      uint32_t pid = mesh.faceVertexIndices[i];
      auto &bucket = buckets[pid];
      uint32_t matched_id = ~0u;
      for (const auto &entry : bucket) {
        if (attribs_match(i, entry.fv_index)) {
          matched_id = entry.out_vertex_id;
          break;
        }
      }
      if (matched_id == ~0u) {
        matched_id = next_vertex_id++;
        bucket.push_back({uint32_t(i), matched_id});
      }
      result.out_indices[i] = matched_id;
      result.out_point_indices[i] = pid;
    }
    bucket_mem = measure_bucket_mem(buckets);
  }

  result.num_unique = next_vertex_id;
  result.mem.input_bytes = mesh.input_bytes();
  result.mem.bucket_bytes = bucket_mem;
  result.mem.output_bytes =
      result.out_indices.capacity() * sizeof(uint32_t) +
      result.out_point_indices.capacity() * sizeof(uint32_t);
  result.mem.total_bytes =
      result.mem.input_bytes + result.mem.bucket_bytes +
      result.mem.output_bytes;

  return result;
}

// ---------------------------------------------------------------------------
// Generate a "star" mesh with one center vertex connected to N outer vertices.
// This creates a single position with valence = N, useful for testing the
// valence safeguard.
// ---------------------------------------------------------------------------
static SyntheticMesh generate_star(size_t num_outer_verts) {
  SyntheticMesh mesh;
  // Center vertex + outer ring
  size_t num_points = 1 + num_outer_verts;
  size_t num_tris = num_outer_verts;
  size_t num_fvs = 3 * num_tris;

  mesh.rows = 1;
  mesh.cols = num_points;
  mesh.points.resize(num_points);
  mesh.points[0] = {0.0f, 0.0f, 0.0f};
  for (size_t i = 0; i < num_outer_verts; i++) {
    float angle = float(i) / float(num_outer_verts) * 6.2831853f;
    mesh.points[1 + i] = {std::cos(angle), std::sin(angle), 0.0f};
  }

  // Fan triangulation: center(0) → outer[i] → outer[(i+1) % N]
  mesh.faceVertexIndices.resize(num_fvs);
  for (size_t i = 0; i < num_outer_verts; i++) {
    mesh.faceVertexIndices[3 * i + 0] = 0;
    mesh.faceVertexIndices[3 * i + 1] = uint32_t(1 + i);
    mesh.faceVertexIndices[3 * i + 2] = uint32_t(1 + ((i + 1) % num_outer_verts));
  }

  // Flat normals
  mesh.normals.resize(num_fvs, {0.0f, 0.0f, 1.0f});

  // UVs from position
  mesh.uv0s.resize(num_fvs);
  for (size_t i = 0; i < num_fvs; i++) {
    const float3 &p = mesh.points[mesh.faceVertexIndices[i]];
    mesh.uv0s[i] = {p[0] * 0.5f + 0.5f, p[1] * 0.5f + 0.5f};
  }

  return mesh;
}

// ---------------------------------------------------------------------------
// Add small noise to attributes — simulates DCC rounding / interpolation
// artifacts in production scenes. Returns a copy with noise added.
// noise_scale: max perturbation per component (e.g. 1e-6 for ~8 ULP at 1.0).
// ---------------------------------------------------------------------------
static SyntheticMesh add_noise(const SyntheticMesh &src, float noise_scale,
                                uint32_t seed = 12345) {
  SyntheticMesh dst = src;  // deep copy
  // Simple LCG for reproducible noise
  uint32_t rng = seed;
  auto next_float = [&]() -> float {
    rng = rng * 1664525u + 1013904223u;
    // Map to [-noise_scale, +noise_scale]
    return (float(rng) / float(0xFFFFFFFFu) * 2.0f - 1.0f) * noise_scale;
  };
  for (auto &n : dst.normals) {
    n[0] += next_float(); n[1] += next_float(); n[2] += next_float();
  }
  for (auto &u : dst.uv0s) {
    u[0] += next_float(); u[1] += next_float();
  }
  for (auto &u : dst.uv1s) {
    u[0] += next_float(); u[1] += next_float();
  }
  for (auto &t : dst.tangents) {
    t[0] += next_float(); t[1] += next_float(); t[2] += next_float();
  }
  for (auto &b : dst.binormals) {
    b[0] += next_float(); b[1] += next_float(); b[2] += next_float();
  }
  for (auto &c : dst.colors) {
    c[0] += next_float(); c[1] += next_float(); c[2] += next_float();
  }
  for (auto &o : dst.opacities) {
    o += next_float();
  }
  return dst;
}

// ---------------------------------------------------------------------------
// Position-bucketed dedup with close-match (is_close) comparison.
// dedup_eps: epsilon for is_close. 0 = exact memcmp.
// ---------------------------------------------------------------------------
static VertexDedupResult run_vertex_dedup_close(const SyntheticMesh &mesh,
                                                 float dedup_eps,
                                                 uint32_t max_valence = 0) {
  size_t num_fvs = mesh.faceVertexIndices.size();
  size_t num_verts = mesh.points.size();

  const float3 *normals_ptr =
      mesh.normals.empty() ? nullptr : mesh.normals.data();
  const float2 *uv0_ptr = mesh.uv0s.empty() ? nullptr : mesh.uv0s.data();
  const float2 *uv1_ptr = mesh.uv1s.empty() ? nullptr : mesh.uv1s.data();
  const float3 *tangents_ptr =
      mesh.tangents.empty() ? nullptr : mesh.tangents.data();
  const float3 *binormals_ptr =
      mesh.binormals.empty() ? nullptr : mesh.binormals.data();
  const float3 *colors_ptr =
      mesh.colors.empty() ? nullptr : mesh.colors.data();
  const float *opacities_ptr =
      mesh.opacities.empty() ? nullptr : mesh.opacities.data();

  VertexDedupResult result;
  result.out_indices.resize(num_fvs);
  result.out_point_indices.resize(num_fvs);
  uint32_t next_vertex_id = 0;

  // Valence safeguard
  bool flatten = false;
  if (max_valence > 0) {
    std::vector<uint32_t> degree(num_verts, 0);
    for (size_t i = 0; i < num_fvs; i++)
      degree[mesh.faceVertexIndices[i]]++;
    uint32_t max_deg = *std::max_element(degree.begin(), degree.end());
    if (max_deg > max_valence) flatten = true;
  }

  size_t bucket_mem = 0;

  if (flatten) {
    for (size_t i = 0; i < num_fvs; i++) {
      result.out_indices[i] = uint32_t(i);
      result.out_point_indices[i] = mesh.faceVertexIndices[i];
    }
    next_vertex_id = uint32_t(num_fvs);
  } else {
    auto attribs_match = [&](size_t a, size_t b) -> bool {
      if (dedup_eps > 0.0f) {
        if (normals_ptr   && !is_close_f3(normals_ptr[a],   normals_ptr[b],   dedup_eps)) return false;
        if (uv0_ptr       && !is_close_f2(uv0_ptr[a],       uv0_ptr[b],       dedup_eps)) return false;
        if (uv1_ptr       && !is_close_f2(uv1_ptr[a],       uv1_ptr[b],       dedup_eps)) return false;
        if (tangents_ptr  && !is_close_f3(tangents_ptr[a],   tangents_ptr[b],  dedup_eps)) return false;
        if (binormals_ptr && !is_close_f3(binormals_ptr[a],  binormals_ptr[b], dedup_eps)) return false;
        if (colors_ptr    && !is_close_f3(colors_ptr[a],     colors_ptr[b],    dedup_eps)) return false;
        if (opacities_ptr && !is_close(opacities_ptr[a],     opacities_ptr[b], dedup_eps)) return false;
      } else {
        if (normals_ptr   && memcmp(&normals_ptr[a],   &normals_ptr[b],   sizeof(float3)) != 0) return false;
        if (uv0_ptr       && memcmp(&uv0_ptr[a],       &uv0_ptr[b],       sizeof(float2)) != 0) return false;
        if (uv1_ptr       && memcmp(&uv1_ptr[a],       &uv1_ptr[b],       sizeof(float2)) != 0) return false;
        if (tangents_ptr  && memcmp(&tangents_ptr[a],   &tangents_ptr[b],  sizeof(float3)) != 0) return false;
        if (binormals_ptr && memcmp(&binormals_ptr[a],  &binormals_ptr[b], sizeof(float3)) != 0) return false;
        if (colors_ptr    && memcmp(&colors_ptr[a],     &colors_ptr[b],    sizeof(float3)) != 0) return false;
        if (opacities_ptr && memcmp(&opacities_ptr[a],  &opacities_ptr[b], sizeof(float))  != 0) return false;
      }
      return true;
    };

    std::vector<std::vector<BucketEntry>> buckets(num_verts);
    for (size_t i = 0; i < num_fvs; i++) {
      uint32_t pid = mesh.faceVertexIndices[i];
      auto &bucket = buckets[pid];
      uint32_t matched_id = ~0u;
      for (const auto &entry : bucket) {
        if (attribs_match(i, entry.fv_index)) {
          matched_id = entry.out_vertex_id;
          break;
        }
      }
      if (matched_id == ~0u) {
        matched_id = next_vertex_id++;
        bucket.push_back({uint32_t(i), matched_id});
      }
      result.out_indices[i] = matched_id;
      result.out_point_indices[i] = pid;
    }
    bucket_mem = measure_bucket_mem(buckets);
  }

  result.num_unique = next_vertex_id;
  result.mem.input_bytes = mesh.input_bytes();
  result.mem.bucket_bytes = bucket_mem;
  result.mem.output_bytes =
      result.out_indices.capacity() * sizeof(uint32_t) +
      result.out_point_indices.capacity() * sizeof(uint32_t);
  result.mem.total_bytes =
      result.mem.input_bytes + result.mem.bucket_bytes +
      result.mem.output_bytes;

  return result;
}

// ---------------------------------------------------------------------------
// Timing helper
// ---------------------------------------------------------------------------
using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

// ---------------------------------------------------------------------------
// Human-readable formatting
// ---------------------------------------------------------------------------
static std::string fmt_count(size_t n) {
  if (n >= 1000000) return std::to_string(n / 1000000) + "M";
  if (n >= 1000) return std::to_string(n / 1000) + "K";
  return std::to_string(n);
}

static std::string fmt_bytes(size_t n) {
  char buf[32];
  if (n >= size_t(1024) * 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.2f GB", double(n) / (1024.0 * 1024 * 1024));
  } else if (n >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", double(n) / (1024 * 1024));
  } else if (n >= 1024) {
    snprintf(buf, sizeof(buf), "%.1f KB", double(n) / 1024);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", n);
  }
  return buf;
}

// ---------------------------------------------------------------------------
// Parse --sizes argument
// ---------------------------------------------------------------------------
static std::vector<size_t> parse_sizes(const char *arg) {
  std::vector<size_t> sizes;
  std::string s(arg);
  size_t pos = 0;
  while (pos < s.size()) {
    size_t end = s.find(',', pos);
    if (end == std::string::npos) end = s.size();
    std::string tok = s.substr(pos, end - pos);
    size_t mult = 1;
    if (!tok.empty() && (tok.back() == 'K' || tok.back() == 'k')) {
      mult = 1000;
      tok.pop_back();
    } else if (!tok.empty() && (tok.back() == 'M' || tok.back() == 'm')) {
      mult = 1000000;
      tok.pop_back();
    }
    size_t val = size_t(std::stoul(tok)) * mult;
    if (val > 0) sizes.push_back(val);
    pos = end + 1;
  }
  return sizes;
}

// ---------------------------------------------------------------------------
// Print table header/separator helpers
// ---------------------------------------------------------------------------

// Column layout for vertex dedup tables:
//   Points FaceVerts Shading Unique Ratio Time Throughput | Input Buckets Output Total | Attribs
static void print_vertex_header(const char *title) {
  printf("\n--- %s ---\n", title);
  printf("%-8s  %-9s  %-6s  %-9s  %-6s  %8s  %10s"
         "  | %9s  %9s  %9s  %9s"
         "  | %s\n",
         "Points", "FaceVerts", "Shade", "Unique", "Ratio",
         "Time(ms)", "Tput(M/s)",
         "Input", "Buckets", "Output", "Total",
         "Attribs");
  printf("%-8s  %-9s  %-6s  %-9s  %-6s  %8s  %10s"
         "  | %9s  %9s  %9s  %9s"
         "  | %s\n",
         "--------", "---------", "------", "---------", "------",
         "--------", "----------",
         "---------", "---------", "---------", "---------",
         "-------");
}

static void print_vertex_row(size_t pts, size_t fvs, const char *shade,
                              uint32_t unique, double ms, double tput,
                              const MemStats &mem, const char *attribs) {
  printf("%-8s  %-9s  %-6s  %-9u  %5.1f%%  %8.1f  %10.1f"
         "  | %9s  %9s  %9s  %9s"
         "  | %s\n",
         fmt_count(pts).c_str(), fmt_count(fvs).c_str(), shade,
         unique, 100.0 * double(unique) / double(fvs), ms, tput,
         fmt_bytes(mem.input_bytes).c_str(),
         fmt_bytes(mem.bucket_bytes).c_str(),
         fmt_bytes(mem.output_bytes).c_str(),
         fmt_bytes(mem.total_bytes).c_str(),
         attribs);
}

// Column layout for tangent dedup tables:
//   Points FaceVerts Shading Unique Ratio Time Throughput | Input Buckets Output Total
static void print_tangent_header() {
  printf("\n--- Tangent Dedup (normal + uv only) ---\n");
  printf("%-8s  %-9s  %-6s  %-9s  %-6s  %8s  %10s"
         "  | %9s  %9s  %9s  %9s\n",
         "Points", "FaceVerts", "Shade", "Unique", "Ratio",
         "Time(ms)", "Tput(M/s)",
         "Input", "Buckets", "Output", "Total");
  printf("%-8s  %-9s  %-6s  %-9s  %-6s  %8s  %10s"
         "  | %9s  %9s  %9s  %9s\n",
         "--------", "---------", "------", "---------", "------",
         "--------", "----------",
         "---------", "---------", "---------", "---------");
}

static void print_tangent_row(size_t pts, size_t fvs, const char *shade,
                               uint32_t unique, double ms, double tput,
                               const MemStats &mem) {
  printf("%-8s  %-9s  %-6s  %-9u  %5.1f%%  %8.1f  %10.1f"
         "  | %9s  %9s  %9s  %9s\n",
         fmt_count(pts).c_str(), fmt_count(fvs).c_str(), shade,
         unique, 100.0 * double(unique) / double(fvs), ms, tput,
         fmt_bytes(mem.input_bytes).c_str(),
         fmt_bytes(mem.bucket_bytes).c_str(),
         fmt_bytes(mem.output_bytes).c_str(),
         fmt_bytes(mem.total_bytes).c_str());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  std::vector<size_t> sizes = {1000, 10000, 100000, 1000000, 5000000, 10000000};
  bool run_vertex = true;
  bool run_tangent = true;

  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg == "--sizes" && i + 1 < argc) {
      sizes = parse_sizes(argv[++i]);
    } else if (arg == "--tangent-only") {
      run_vertex = false;
    } else if (arg == "--vertex-only") {
      run_tangent = false;
    } else if (arg == "--help" || arg == "-h") {
      printf(
          "Usage: %s [--sizes 1K,100K,1M,...] [--tangent-only] "
          "[--vertex-only]\n",
          argv[0]);
      return 0;
    }
  }

  printf("=== Position-Bucketed Vertex Dedup Benchmark ===\n");

  // -----------------------------------------------------------------------
  // Vertex dedup benchmark — multiple attribute combinations
  // -----------------------------------------------------------------------
  if (run_vertex) {
    // Attribute combos to test, matching real production scenarios:
    //   1. nrm+uv0            — simplest PBR mesh
    //   2. nrm+uv01           — lightmap / dual-UV
    //   3. nrm+uv0+tan        — normal-mapped PBR (user-supplied tangent frame)
    //   4. nrm+uv01+tan       — normal-mapped + lightmap (heaviest common case)
    //   5. nrm+uv0+col        — vertex-colored mesh with opacity
    //   6. all7                — kitchen-sink (all attributes)
    struct AttribCombo {
      uint32_t flags;
      const char *title;
    };
    AttribCombo combos[] = {
      {ATTRIB_BASIC,        "nrm+uv0 (basic PBR)"},
      {ATTRIB_MULTI_UV,     "nrm+uv01 (dual UV / lightmap)"},
      {ATTRIB_TANGENT,      "nrm+uv0+tan+bn (normal-mapped PBR)"},
      {ATTRIB_MULTI_UV_TAN, "nrm+uv01+tan+bn (normal-mapped + lightmap)"},
      {ATTRIB_COLOR,        "nrm+uv0+color+opacity (vertex color)"},
      {ATTRIB_FULL,         "all 7 attributes (kitchen-sink)"},
    };

    for (const auto &combo : combos) {
      char header[128];
      snprintf(header, sizeof(header), "Vertex Dedup — %s  [%zu B/cmp]",
               combo.title, attrib_cmp_bytes(combo.flags));
      print_vertex_header(header);

      for (size_t npts : sizes) {
        for (int flat = 0; flat <= 1; flat++) {
          bool is_flat = (flat == 1);
          SyntheticMesh mesh = generate_grid(npts, is_flat, combo.flags);
          size_t actual_pts = mesh.points.size();
          size_t num_fvs = mesh.faceVertexIndices.size();

          auto t0 = Clock::now();
          VertexDedupResult result = run_vertex_dedup(mesh);
          auto t1 = Clock::now();

          double ms = elapsed_ms(t0, t1);
          double tput = (ms > 0) ? (double(num_fvs) / ms / 1000.0) : 0;

          print_vertex_row(actual_pts, num_fvs,
                           is_flat ? "flat" : "smooth",
                           result.num_unique, ms, tput,
                           result.mem, attrib_label(combo.flags));
        }
      }
    }
  }

  // -----------------------------------------------------------------------
  // Tangent dedup benchmark (ComputeTangentsAndBinormals equivalent)
  // -----------------------------------------------------------------------
  if (run_tangent) {
    print_tangent_header();

    for (size_t npts : sizes) {
      for (int flat = 0; flat <= 1; flat++) {
        bool is_flat = (flat == 1);
        SyntheticMesh mesh = generate_grid(npts, is_flat, false);
        size_t actual_pts = mesh.points.size();
        size_t num_fvs = mesh.faceVertexIndices.size();

        auto t0 = Clock::now();
        TangentDedupResult result = run_tangent_dedup(mesh);
        auto t1 = Clock::now();

        double ms = elapsed_ms(t0, t1);
        double tput = (ms > 0) ? (double(num_fvs) / ms / 1000.0) : 0;

        print_tangent_row(actual_pts, num_fvs,
                          is_flat ? "flat" : "smooth",
                          result.num_unique, ms, tput,
                          result.mem);
      }
    }
  }

  // -----------------------------------------------------------------------
  // Algorithm comparison: linear-scan vs FNV-1a-hashed buckets vs global
  // hash-map.  Uses normals+uv0 only for fair comparison.
  // -----------------------------------------------------------------------
  if (run_vertex) {
    printf("\n--- Algorithm Comparison (normals + uv0) ---\n");
    printf("%-8s  %-9s  %-6s  | %23s  | %23s  | %23s\n",
           "", "", "",
           "  Linear Scan (current)", "  FNV-1a Hashed Bucket",
           "  Global unordered_map");
    printf("%-8s  %-9s  %-6s  | %10s  %11s  | %10s  %11s  | %10s  %11s\n",
           "Points", "FaceVerts", "Shade",
           "Time(ms)", "Buckets",
           "Time(ms)", "Buckets",
           "Time(ms)", "HashMap");
    printf("%-8s  %-9s  %-6s  | %10s  %11s  | %10s  %11s  | %10s  %11s\n",
           "--------", "---------", "------",
           "----------", "-----------",
           "----------", "-----------",
           "----------", "-----------");

    for (size_t npts : sizes) {
      for (int flat = 0; flat <= 1; flat++) {
        bool is_flat = (flat == 1);
        SyntheticMesh mesh = generate_grid(npts, is_flat, false);
        size_t actual_pts = mesh.points.size();
        size_t num_fvs = mesh.faceVertexIndices.size();

        // 1) Linear scan (current)
        auto t0 = Clock::now();
        VertexDedupResult r_linear = run_vertex_dedup(mesh);
        auto t1 = Clock::now();
        double ms_linear = elapsed_ms(t0, t1);

        // 2) FNV-1a hashed buckets
        auto t2 = Clock::now();
        VertexDedupResult r_hashed = run_vertex_dedup_hashed(mesh);
        auto t3 = Clock::now();
        double ms_hashed = elapsed_ms(t2, t3);

        // 3) Global hash-map
        auto t4 = Clock::now();
        VertexDedupResult r_global = run_vertex_dedup_global_hashmap(mesh);
        auto t5 = Clock::now();
        double ms_global = elapsed_ms(t4, t5);

        // Verify same result
        if (r_linear.num_unique != r_hashed.num_unique ||
            r_linear.num_unique != r_global.num_unique) {
          fprintf(stderr,
                  "ERROR: unique count mismatch: linear=%u hashed=%u "
                  "global=%u\n",
                  r_linear.num_unique, r_hashed.num_unique,
                  r_global.num_unique);
        }

        printf("%-8s  %-9s  %-6s  | %10.1f  %11s  | %10.1f  %11s  | %10.1f  %11s\n",
               fmt_count(actual_pts).c_str(), fmt_count(num_fvs).c_str(),
               is_flat ? "flat" : "smooth",
               ms_linear, fmt_bytes(r_linear.mem.bucket_bytes).c_str(),
               ms_hashed, fmt_bytes(r_hashed.mem.bucket_bytes).c_str(),
               ms_global, fmt_bytes(r_global.mem.bucket_bytes).c_str());
      }
    }

    printf("\n  Notes:\n");
    printf("  - Linear Scan: position-bucketed, ~4-8 memcmp per face-vert (current impl)\n");
    printf("  - FNV-1a Hashed Bucket: position-bucketed + FNV-1a pre-filter per entry\n");
    printf("  - Global unordered_map: single hash-map keyed by (pid + attrib hash), like old BuildIndices\n");
    printf("  - BucketEntry = %zu B, HashedBucketEntry = %zu B\n",
           sizeof(BucketEntry), sizeof(HashedBucketEntry));
  }

  // -----------------------------------------------------------------------
  // Valence safeguard benchmark: star meshes with high center-vertex valence.
  // Shows the dedup with/without safeguard on pathological topologies.
  // -----------------------------------------------------------------------
  if (run_vertex) {
    printf("\n--- Valence Safeguard Test ---\n");
    printf("Star meshes (one center vertex connected to N outer vertices, valence = N)\n");
    printf("Threshold = 16 (default). When center valence > 16, safeguard flattens.\n\n");
    printf("%-8s  %-9s  %-8s  | %-10s  %-9s  %-8s  | %-10s  %-9s  %-8s\n",
           "Valence", "FaceVerts", "MaxDeg",
           "No Guard", "Unique", "Time(ms)",
           "Guarded", "Unique", "Time(ms)");
    printf("%-8s  %-9s  %-8s  | %-10s  %-9s  %-8s  | %-10s  %-9s  %-8s\n",
           "--------", "---------", "--------",
           "----------", "---------", "--------",
           "----------", "---------", "--------");

    std::vector<size_t> valences = {4, 8, 12, 16, 20, 50, 100, 500, 1000, 10000};
    for (size_t val : valences) {
      SyntheticMesh mesh = generate_star(val);
      size_t num_fvs = mesh.faceVertexIndices.size();

      // Without safeguard (max_valence=0 disables)
      auto t0 = Clock::now();
      VertexDedupResult r_no_guard = run_vertex_dedup_guarded(mesh, 0);
      auto t1 = Clock::now();
      double ms_no_guard = elapsed_ms(t0, t1);

      // With safeguard (threshold=16)
      auto t2 = Clock::now();
      VertexDedupResult r_guarded = run_vertex_dedup_guarded(mesh, 16);
      auto t3 = Clock::now();
      double ms_guarded = elapsed_ms(t2, t3);

      const char *ng_mode = (val <= 16) ? "dedup" : "dedup";
      const char *g_mode = (val <= 16) ? "dedup" : "FLATTEN";

      printf("%-8zu  %-9s  %-8zu  | %-10s  %-9u  %8.3f  | %-10s  %-9u  %8.3f\n",
             val, fmt_count(num_fvs).c_str(), val,
             ng_mode, r_no_guard.num_unique, ms_no_guard,
             g_mode, r_guarded.num_unique, ms_guarded);
    }

    printf("\n  Notes:\n");
    printf("  - 'FLATTEN' = safeguard triggered, each face-vertex becomes unique (no dedup)\n");
    printf("  - For normal grids (valence 4-8), safeguard never triggers\n");
    printf("  - Flatten avoids O(valence^2) comparison cost at the expense of more vertices\n");

    // -------------------------------------------------------------------
    // Pathological: high-valence star + noise + close-match.
    // Without safeguard this would be O(N^2) and stall. With safeguard it
    // flattens instantly.
    // -------------------------------------------------------------------
    printf("\n--- Pathological: High-Valence Star + Noise + Close-Match ---\n");
    printf("Star mesh (single center vertex with valence=N), noise=1e-4, eps=1e-4.\n");
    printf("Without safeguard (threshold=0): dedup attempts O(valence) scan per face-vert.\n");
    printf("With safeguard (threshold=16): flattens instantly when valence > 16.\n\n");

    printf("%-8s  %-9s  | %28s  | %28s\n",
           "", "",
           "  No Safeguard (close eps=1e-4)",
           "  Safeguard=16 (close eps=1e-4)");
    printf("%-8s  %-9s  | %10s  %8s  %7s  | %10s  %8s  %7s\n",
           "Valence", "FaceVerts",
           "Time(ms)", "Unique", "Mode",
           "Time(ms)", "Unique", "Mode");
    printf("%-8s  %-9s  | %10s  %8s  %7s  | %10s  %8s  %7s\n",
           "--------", "---------",
           "----------", "--------", "-------",
           "----------", "--------", "-------");

    std::vector<size_t> patho_valences = {8, 16, 32, 100, 500, 1000, 5000, 10000, 50000, 100000, 500000};
    for (size_t val : patho_valences) {
      SyntheticMesh star = generate_star(val);
      SyntheticMesh noisy = add_noise(star, 1e-4f);
      size_t num_fvs = noisy.faceVertexIndices.size();

      // No safeguard, close-match
      auto t0 = Clock::now();
      VertexDedupResult r_no = run_vertex_dedup_close(noisy, 1e-4f, 0);
      auto t1 = Clock::now();
      double ms_no = elapsed_ms(t0, t1);

      // With safeguard, close-match
      auto t2 = Clock::now();
      VertexDedupResult r_yes = run_vertex_dedup_close(noisy, 1e-4f, 16);
      auto t3 = Clock::now();
      double ms_yes = elapsed_ms(t2, t3);

      const char *mode_no = "dedup";
      const char *mode_yes = (val > 16) ? "FLATTEN" : "dedup";

      printf("%-8zu  %-9s  | %10.3f  %8u  %7s  | %10.3f  %8u  %7s\n",
             val, fmt_count(num_fvs).c_str(),
             ms_no, r_no.num_unique, mode_no,
             ms_yes, r_yes.num_unique, mode_yes);
    }

    printf("\n  Notes:\n");
    printf("  - Without safeguard, time grows quadratically with valence\n");
    printf("  - With safeguard=16, high-valence cases flatten in O(N) time\n");
    printf("  - Flatten produces more vertices but avoids stalling\n");
    printf("  - close-match (is_close) within each bucket means even noisy\n");
    printf("    data at low valence merges correctly\n");
  }

  // -----------------------------------------------------------------------
  // Close-match (is_close) benchmark: exact memcmp vs approximate matching.
  // Simulates production scenes where DCC tools introduce small rounding.
  // -----------------------------------------------------------------------
  if (run_vertex) {
    printf("\n--- Close-Match Dedup (exact vs is_close) ---\n");
    printf("Smooth-shaded grids with noise added to attributes.\n");
    printf("is_close: |a-b| <= eps  OR  |a-b| <= eps * max(|a|, |b|)\n");

    struct CloseCombo {
      uint32_t flags;
      const char *label;
    };
    CloseCombo close_combos[] = {
      {ATTRIB_BASIC,        "nrm+uv0"},
      {ATTRIB_MULTI_UV_TAN, "nrm+uv01+tan+bn"},
      {ATTRIB_COLOR,        "nrm+uv0+col+opa"},
      {ATTRIB_FULL,         "all7"},
    };

    std::vector<size_t> close_sizes = {10000, 100000, 1000000, 5000000};
    float noise_levels[] = {0.0f, 1e-7f, 1e-6f, 1e-5f};
    const char *noise_labels[] = {"none", "1e-7", "1e-6", "1e-5"};

    for (const auto &cc : close_combos) {
      printf("\n  Attribs: %s  [%zu B compared per face-vert]\n\n", cc.label,
             attrib_cmp_bytes(cc.flags));

      printf("%-8s  %-9s  %-10s  | %23s  | %23s  | %23s\n",
             "", "", "",
             "   Exact (memcmp)", "   is_close eps=1e-5", "   is_close eps=1e-4");
      printf("%-8s  %-9s  %-10s  | %10s  %11s  | %10s  %11s  | %10s  %11s\n",
             "Points", "FaceVerts", "Noise",
             "Time(ms)", "Unique",
             "Time(ms)", "Unique",
             "Time(ms)", "Unique");
      printf("%-8s  %-9s  %-10s  | %10s  %11s  | %10s  %11s  | %10s  %11s\n",
             "--------", "---------", "----------",
             "----------", "-----------",
             "----------", "-----------",
             "----------", "-----------");

      for (size_t npts : close_sizes) {
        SyntheticMesh base = generate_grid(npts, false, cc.flags);

        for (int ni = 0; ni < 4; ni++) {
          SyntheticMesh mesh = (noise_levels[ni] > 0.0f)
                                   ? add_noise(base, noise_levels[ni])
                                   : base;
          size_t actual_pts = mesh.points.size();
          size_t num_fvs = mesh.faceVertexIndices.size();

          // 1) Exact memcmp
          auto t0 = Clock::now();
          VertexDedupResult r_exact = run_vertex_dedup_close(mesh, 0.0f);
          auto t1 = Clock::now();
          double ms_exact = elapsed_ms(t0, t1);

          // 2) is_close eps=1e-5
          auto t2 = Clock::now();
          VertexDedupResult r_close5 = run_vertex_dedup_close(mesh, 1e-5f);
          auto t3 = Clock::now();
          double ms_close5 = elapsed_ms(t2, t3);

          // 3) is_close eps=1e-4
          auto t4 = Clock::now();
          VertexDedupResult r_close4 = run_vertex_dedup_close(mesh, 1e-4f);
          auto t5 = Clock::now();
          double ms_close4 = elapsed_ms(t4, t5);

          printf("%-8s  %-9s  %-10s  | %10.1f  %11u  | %10.1f  %11u  | %10.1f  %11u\n",
                 fmt_count(actual_pts).c_str(), fmt_count(num_fvs).c_str(),
                 noise_labels[ni],
                 ms_exact, r_exact.num_unique,
                 ms_close5, r_close5.num_unique,
                 ms_close4, r_close4.num_unique);
        }
        printf("\n");
      }
    }

    printf("  Notes:\n");
    printf("  - 'noise=none': clean data, exact and close-match should give same unique count\n");
    printf("  - With noise, exact memcmp sees all face-verts as different (no merge)\n");
    printf("  - is_close with appropriate eps recovers the original merge rate\n");
    printf("  - Recommended: eps=1e-5 for production scenes (~20 ULP at unit scale)\n");
    printf("  - More attribs compared => is_close overhead grows, but stays small vs memory latency\n");
  }

  printf("\nDone.\n");
  return 0;
}
