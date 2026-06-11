// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv internals. Not a public header.

#ifndef TINYUSDZ_TSD_INTERNAL_HH_
#define TINYUSDZ_TSD_INTERNAL_HH_

#include <cstdint>
#include <string>
#include <vector>

#include "tinysubdiv.hh"

namespace tinyusdz {
namespace tsd {

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;
constexpr uint32_t kMaxFaceDegree = 256;

// Set `*err` (if non-null) and return `r`.
inline Result Fail(Result r, std::string *err, const char *msg) {
  if (err) {
    (*err) += msg;
    (*err) += "\n";
  }
  return r;
}

// --- Topology (built once per level) ---------------------------------------
//
// Edges are interned with v0 < v1 ordering. Per-edge incident faces are
// limited to 2 (non-manifold input is rejected during BuildTopology).

struct Topology {
  uint32_t num_points = 0;
  uint32_t num_faces = 0;
  uint32_t num_edges = 0;

  // Prefix sum of face_vertex_counts; size num_faces + 1.
  std::vector<uint32_t> face_offsets;

  // Edge id per face-corner (edge from corner i to corner i+1 of the face);
  // parallel to face_vertex_indices.
  std::vector<uint32_t> face_edges;

  // Per edge: 2 endpoint vertex ids (v0 < v1).
  std::vector<uint32_t> edge_verts;
  // Per edge: 2 incident face ids (second is kInvalidIndex for boundary).
  std::vector<uint32_t> edge_faces;

  // CSR: vertex -> incident faces.
  std::vector<uint32_t> vert_face_offsets;
  std::vector<uint32_t> vert_faces;
  // CSR: vertex -> incident edges.
  std::vector<uint32_t> vert_edge_offsets;
  std::vector<uint32_t> vert_edges;

  std::vector<uint8_t> vert_is_boundary;

  bool IsBoundaryEdge(uint32_t e) const {
    return edge_faces[2 * e + 1] == kInvalidIndex;
  }
};

// Builds edge table + CSR adjacency. Rejects:
// - indices out of range
// - face degree outside [3, kMaxFaceDegree]
// - non-manifold edges (>2 incident faces, or inconsistent winding where
//   two faces traverse an edge in the same direction)
// - degenerate edges (v == v)
Result BuildTopology(const uint32_t *face_vertex_counts, uint32_t num_faces,
                     const uint32_t *face_vertex_indices,
                     uint32_t num_face_vertex_indices, uint32_t num_points,
                     Topology *out, std::string *err);

// --- Validation / canonicalization ------------------------------------------

// Canonical creases: one entry per unique mesh edge with sharpness > 0.
struct CreaseEdges {
  std::vector<uint32_t> edge_verts;  // pairs (v0 < v1), 2 per crease edge
  std::vector<float> sharpnesses;    // clamped to [0, kInfiniteSharpness]
};

// Validates MeshView counts/indices and the Options. Does not build topology.
Result ValidateInput(const MeshView &mesh,
                     const FVarChannelView *fvar_channels,
                     uint32_t num_fvar_channels,
                     const VertexPrimvarView *vertex_primvars,
                     uint32_t num_vertex_primvars, const Options &options,
                     std::string *err);

// Flattens USD crease vertex chains into per-edge sharpness pairs.
// Duplicate edges keep the max sharpness. Pairs that are not actual mesh
// edges are skipped (matches OpenSubdiv's warn-and-ignore behavior); the
// edge-existence filtering happens later when edges are interned.
Result CanonicalizeCreases(const MeshView &mesh, CreaseEdges *out,
                           std::string *err);

// Applies canonical crease sharpness onto level-0 edges; non-edges are
// skipped. `edge_sharpness` must be sized topo.num_edges (zero-filled).
void MapCreasesToEdges(const Topology &topo, const CreaseEdges &creases,
                       std::vector<float> *edge_sharpness);

// --- Overflow-safe sizing ----------------------------------------------------

// a + b with overflow check against 2^32-1; returns false on overflow.
inline bool CheckedAddU32(uint64_t a, uint64_t b, uint32_t *out) {
  uint64_t s = a + b;  // cannot overflow uint64 for inputs < 2^33
  if (s > 0xFFFFFFFFull) {
    return false;
  }
  *out = uint32_t(s);
  return true;
}

// --- Parallel-for dispatch ---------------------------------------------------

template <typename Body>
inline void ParallelFor(const Options &opts, uint32_t count, const Body &body) {
  if (opts.parallel_for && count > 1) {
    struct Ctx {
      const Body *body;
    } ctx{&body};
    opts.parallel_for(
        opts.parallel_for_user, count,
        [](void *user, uint32_t i) { (*static_cast<Ctx *>(user)->body)(i); },
        &ctx);
  } else {
    for (uint32_t i = 0; i < count; i++) {
      body(i);
    }
  }
}

}  // namespace tsd
}  // namespace tinyusdz

#endif  // TINYUSDZ_TSD_INTERNAL_HH_
