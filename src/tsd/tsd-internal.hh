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

// --- Sharpness (Sdc-compatible semantics) ------------------------------------
//
// IsSharp(s): s > 0. IsInfinite(s): s >= kInfiniteSharpness.
// Uniform child sharpness: smooth stays smooth, infinite stays infinite,
// otherwise decrement by 1 (clamped at smooth).

inline bool IsSharp(float s) { return s > 0.0f; }
inline bool IsInfinitelySharp(float s) { return s >= kInfiniteSharpness; }
inline bool IsSemiSharp(float s) {
  return (s > 0.0f) && (s < kInfiniteSharpness);
}

inline float DecrementSharpness(float s) {
  if (!(s > 0.0f)) {
    return 0.0f;
  }
  if (s >= kInfiniteSharpness) {
    return kInfiniteSharpness;
  }
  return (s > 1.0f) ? (s - 1.0f) : 0.0f;
}

// Child sharpness of edge `edge_sharpness` at its end vertex whose incident
// edge sharpness values are given (Chaikin rule; matches OpenSubdiv
// Sdc::Crease::SubdivideEdgeSharpnessAtVertex).
float ChaikinChildEdgeSharpness(float edge_sharpness,
                                uint32_t incident_edge_count,
                                const float *incident_edge_sharpness);

// Effective (boundary-baked) sharpness state for one level.
struct SharpnessCtx {
  const float *edge_sharpness = nullptr;  // per edge; may be null (all 0)
  const float *vert_sharpness = nullptr;  // per vertex; may be null (all 0)

  float EdgeSharpness(uint32_t e) const {
    return edge_sharpness ? edge_sharpness[e] : 0.0f;
  }
  float VertSharpness(uint32_t v) const {
    return vert_sharpness ? vert_sharpness[v] : 0.0f;
  }
};

// Value-interpolation mode: Smooth applies the scheme rules (geometry and
// "vertex" primvars), Linear applies bilinear rules ("varying" primvars).
enum class RefineMode : uint8_t { Smooth, Linear };

// --- One-level child topology -------------------------------------------------
//
// Quad split (Catmull-Clark & Bilinear): each parent n-gon face becomes n
// quads. Child vertex order: [0, V) children of parent vertices, [V, V + E)
// children of edges, [V + E, V + E + F) children of faces. Child quad k of
// parent face f (corner k): { vchild(corner k), echild(edge k),
// fchild(f), echild(edge k-1) }.

struct ChildTopo {
  uint32_t num_points = 0;
  std::vector<uint32_t> fvc;
  std::vector<uint32_t> fvi;
  std::vector<uint32_t> face_parent;  // parent face per child face
};

Result BuildChildTopologyQuad(const Topology &topo, const uint32_t *parent_fvi,
                              ChildTopo *out, std::string *err);

// Tri split (Loop): each parent triangle becomes 4 triangles. Child vertex
// order: [0, V) children of parent vertices, [V, V + E) edge children.
// Child face order per parent face: corner 0, corner 1, corner 2, center.
Result BuildChildTopologyTri(const Topology &topo, const uint32_t *parent_fvi,
                             ChildTopo *out, std::string *err);

// --- Per-scheme value kernels ---------------------------------------------------
//
// All kernels write child values for the quad-split layout above:
// child_values must hold (V + E + F) * stride floats. `values` holds one
// tuple of `stride` floats per parent vertex. Results are scatter-free per
// output element (parallel_for-safe).

// Bilinear: vertex children copy, edge children midpoint, face centroid.
// (Also used for RefineMode::Linear by other quad schemes.)
void BilinearRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                          const float *values, uint32_t stride,
                          const Options &opts, float *child_values);

// Catmull-Clark with Sdc-compatible crease/corner/boundary rules and
// fractional-sharpness transitional blending. Boundary behavior comes
// entirely from `sharp` (boundary edges/corners pre-baked to infinite).
void CatmarkRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                         const float *values, uint32_t stride,
                         const SharpnessCtx &sharp, const Options &opts,
                         float *child_values);

// Loop (all-triangle input) with the same Sdc-compatible crease semantics.
// Child layout matches BuildChildTopologyTri ((V + E) * stride floats).
void LoopRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                      const float *values, uint32_t stride,
                      const SharpnessCtx &sharp, const Options &opts,
                      float *child_values);

// FaceVarying "all" (fully linear) per-corner refinement for the quad
// split: 4 child corner tuples per parent corner, in BuildChildTopologyQuad
// child-face order.
void LinearFVarRefineQuad(const Topology &topo, const float *corner_values,
                          uint32_t stride, const Options &opts,
                          float *child_corner_values);

// Same for the tri split: 12 child corner tuples per parent face (4 tris),
// in BuildChildTopologyTri child-face order.
void LinearFVarRefineTri(const Topology &topo, const float *corner_values,
                         uint32_t stride, const Options &opts,
                         float *child_corner_values);

// Bakes the level-0 sharpness state (matching OpenSubdiv's
// TopologyRefinerFactory): authored creases and corners, boundary edges to
// infinite, "edgeAndCorner" corner pinning, and -- for catmark/loop with
// boundary "none" -- tagging boundary-incident faces as holes.
// `hole`/`any_hole` may be null when hole tagging is not needed.
void BakeLevel0Sharpness(const MeshView &mesh, const Options &options,
                         const Topology &topo, const CreaseEdges &creases,
                         std::vector<float> *edge_sharp,
                         std::vector<float> *vert_sharp,
                         std::vector<uint8_t> *hole, bool *any_hole);

// Derives child-level sharpness from a parent level: a child edge is a half
// of parent edge pe iff it connects a vertex child (< parentV) to pe's edge
// child; halves decay (uniform decrement or Chaikin at the shared parent
// vertex), everything else is smooth. Parent vertex children keep decayed
// vertex sharpness.
void DeriveChildSharpness(const Topology &parent_topo,
                          const std::vector<float> &parent_edge_sharp,
                          const std::vector<float> &parent_vert_sharp,
                          const Topology &child_topo, CreasingMethod method,
                          std::vector<float> *child_edge_sharp,
                          std::vector<float> *child_vert_sharp);

// --- FaceVarying seam-split refinement (modes other than "all") --------------
//
// The five smooth-capable fvar modes refine a "split mesh": its vertices are
// equivalence classes of face corners at the same mesh vertex whose values
// are continuous across the shared edges (the spans of OpenSubdiv's
// FVarLevel). The split mesh is refined with the same scheme kernels as
// geometry; seams and mesh boundaries become split-mesh boundary edges
// (infinitely sharp) and the per-mode rules become extra vertex sharpness.

struct FVarSplitState {
  uint32_t stride = 0;
  uint32_t num_values = 0;           // split vertex count (current level)
  std::vector<float> values;         // per split vertex, stride floats
  std::vector<uint32_t> fvi;         // per corner -> split vertex id
  std::vector<float> edge_sharp;     // per split edge (current level)
  std::vector<float> vert_sharp;     // per split vertex
  Topology topo;                     // split topology (current level)
};

// Builds the level-0 split mesh + sharpness for one smooth fvar channel.
// `geo_edge_sharp`/`geo_vert_sharp` are the geometry's baked level-0 arrays
// (boundary edges infinite, corner pinning applied).
Result BuildFVarSplitLevel0(const Topology &geo_topo, const uint32_t *geo_fvc,
                            const uint32_t *geo_fvi,
                            const FVarChannelView &channel,
                            const std::vector<float> &geo_edge_sharp,
                            const std::vector<float> &geo_vert_sharp,
                            FVarSplitState *state, std::string *err);

// Refines one smooth fvar channel one level. When `more_levels` is false the
// child topology/sharpness are not rebuilt (only values + fvi advance).
Result RefineFVarSplitOnce(FVarSplitState *state, const Options &opts,
                           bool more_levels, std::string *err);

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
