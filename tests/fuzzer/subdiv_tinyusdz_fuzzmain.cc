//
// Fuzz target for TinySubdiv (dependency-free subdivision library).
//
// Interprets fuzzer input as a serialized HalfEdgeMesh and exercises all
// three subdivision schemes (CatmullClark, Loop, Bilinear) at levels 1-3.
//
// Input format (all multi-byte values little-endian):
//   [0..1]   : scheme selector (uint16_t, modulo 3)
//   [2..5]   : subdiv level (uint32_t, clamped to 1..3)
//   [6..9]   : num_vertices (uint32_t, capped at 10000)
//   [10..13] : num_faces (uint32_t, capped at 10000)
//   remainder : packed vertex positions (num_vertices × 3 float),
//               then face_vertex_counts (num_faces × uint32_t),
//               then face_vertex_indices (uint32_t × sum of counts)
//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "subdivision.hh"

using tinyusdz::subdiv::BilinearSubdivider;
using tinyusdz::subdiv::BoundaryInterpolation;
using tinyusdz::subdiv::CatmullClarkSubdivider;
using tinyusdz::subdiv::HalfEdgeMesh;
using tinyusdz::subdiv::LoopSubdivider;
using tinyusdz::subdiv::SubdivisionScheme;

static constexpr uint32_t kMaxVertices = 10000;
static constexpr uint32_t kMaxFaces = 10000;
static constexpr uint32_t kMaxInputBytes = 1024 * 1024;  // 1 MB

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 14) return 0;                   // header too small
  if (size > kMaxInputBytes) return 0;        // size cap

  // --- Parse header ---
  uint16_t scheme_raw;
  uint32_t level, num_verts, num_faces;
  std::memcpy(&scheme_raw, data, 2);
  std::memcpy(&level, data + 2, 4);
  std::memcpy(&num_verts, data + 6, 4);
  std::memcpy(&num_faces, data + 10, 4);

  int scheme = static_cast<int>(scheme_raw) % 3;
  if (level < 1 || level > 3) level = 1;

  // Clamp mesh sizes for safety.
  if (num_verts > kMaxVertices) num_verts = kMaxVertices;
  if (num_faces > kMaxFaces) num_faces = kMaxFaces;
  if (num_verts < 3) return 0;   // need at least a triangle
  if (num_faces < 1) return 0;

  // --- Compute required bytes ---
  // Points: num_verts * 3 floats (4 bytes each)
  // Face counts: num_faces uint32_t
  // Face indices: up to num_faces * kMaxFaceVertices uint32_t
  // We'll parse what's available beyond the header.

  size_t header_bytes = 14;
  size_t points_bytes = static_cast<size_t>(num_verts) * 3 * 4;
  size_t counts_bytes = static_cast<size_t>(num_faces) * 4;

  if (header_bytes + points_bytes + counts_bytes > size) return 0;

  const uint8_t *ptr = data + header_bytes;

  // --- Parse vertex positions ---
  std::vector<float> points(num_verts * 3);
  for (size_t i = 0; i < points.size(); ++i) {
    uint32_t u;
    std::memcpy(&u, ptr + i * 4, 4);
    // Mask exponent bits to avoid NaN/Inf that could slow the fuzzer.
    u = (u & 0x807fffffu) | 0x3f000000u;  // clamp to ~[0.5, 2.0)
    float f;
    std::memcpy(&f, &u, 4);
    points[i] = f;
  }
  ptr += points_bytes;

  // --- Parse face vertex counts ---
  std::vector<uint32_t> fvc(num_faces);
  uint32_t total_indices = 0;
  for (size_t i = 0; i < num_faces; ++i) {
    std::memcpy(&fvc[i], ptr + i * 4, 4);
    if (fvc[i] < 3) fvc[i] = 3;
    if (fvc[i] > HalfEdgeMesh::kMaxFaceVertices) fvc[i] = HalfEdgeMesh::kMaxFaceVertices;
    total_indices += fvc[i];
  }
  ptr += counts_bytes;

  // --- Parse face vertex indices ---
  size_t indices_bytes = static_cast<size_t>(total_indices) * 4;
  size_t remaining = size - static_cast<size_t>(ptr - data);
  if (indices_bytes > remaining) {
    total_indices = static_cast<uint32_t>(remaining / 4);
    if (total_indices == 0) return 0;
    // Adjust fvc to fit available indices.
    fvc.resize(num_faces);
    uint32_t consumed = 0;
    for (size_t i = 0; i < fvc.size(); ++i) {
      if (consumed + fvc[i] > total_indices) {
        if (consumed == total_indices) { fvc.resize(i); break; }
        fvc[i] = total_indices - consumed;
      }
      consumed += fvc[i];
    }
    num_faces = static_cast<uint32_t>(fvc.size());
    total_indices = consumed;
  }

  std::vector<uint32_t> fvi(total_indices);
  for (size_t i = 0; i < total_indices; ++i) {
    uint32_t idx;
    std::memcpy(&idx, ptr + i * 4, 4);
    // Clamp indices to valid range.
    if (num_verts > 0) idx = idx % num_verts;
    fvi[i] = idx;
  }

  // --- Build HalfEdgeMesh ---
  HalfEdgeMesh hem;
  auto conv = tinyusdz::subdiv::ConvertToHalfEdgeMesh(fvc, fvi, points, hem);
  if (!conv.success) return 0;

  // --- Subdivide ---
  HalfEdgeMesh out;

  switch (scheme) {
    case 0: {
      CatmullClarkSubdivider sub;
      sub.SetBoundaryInterpolation(BoundaryInterpolation::EdgeOnly);
      auto r = sub.Subdivide(hem, out, static_cast<int>(level));
      (void)r;
      break;
    }
    case 1: {
      LoopSubdivider sub;
      sub.SetBoundaryInterpolation(BoundaryInterpolation::EdgeOnly);
      auto r = sub.Subdivide(hem, out, static_cast<int>(level));
      (void)r;
      break;
    }
    case 2: {
      BilinearSubdivider sub;
      auto r = sub.Subdivide(hem, out, static_cast<int>(level));
      (void)r;
      break;
    }
  }

  return 0;
}
