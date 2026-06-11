// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: faceVarying primvar refinement.
//
// Mode "all" (fully linear) interpolates per-face-corner values within each
// face independently -- no cross-face smoothing, seams stay sharp. The
// remaining five modes refine a seam-split mesh with the scheme kernels
// (M3).

#include <algorithm>
#include <cstring>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

namespace {

// Union-find over face corners.
struct UnionFind {
  std::vector<uint32_t> parent;
  void Init(uint32_t n) {
    parent.resize(n);
    for (uint32_t i = 0; i < n; i++) {
      parent[i] = i;
    }
  }
  uint32_t Find(uint32_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  }
  void Union(uint32_t a, uint32_t b) {
    a = Find(a);
    b = Find(b);
    if (a != b) {
      parent[b] = a;
    }
  }
};

// Local corner index of vertex `v` within face `f`, or kInvalidIndex.
inline uint32_t CornerOfVertexInFace(const Topology &topo,
                                     const uint32_t *fvi, uint32_t f,
                                     uint32_t v) {
  const uint32_t begin = topo.face_offsets[f];
  const uint32_t end = topo.face_offsets[f + 1];
  for (uint32_t k = begin; k < end; k++) {
    if (fvi[k] == v) {
      return k;  // global corner index
    }
  }
  return kInvalidIndex;
}

}  // namespace

Result BuildFVarSplitLevel0(const Topology &geo_topo, const uint32_t *geo_fvc,
                            const uint32_t *geo_fvi,
                            const FVarChannelView &channel,
                            const std::vector<float> &geo_edge_sharp,
                            const std::vector<float> &geo_vert_sharp,
                            FVarSplitState *state, std::string *err) {
  const uint32_t num_corners = geo_topo.face_offsets[geo_topo.num_faces];
  const uint32_t stride = channel.stride;
  const FVarLinearInterpolation mode = channel.interpolation;

  // --- Per-corner value ids -------------------------------------------------
  // Continuity is decided by value-id equality: the authored indices when
  // present (matching OpenSubdiv, which compares fvar value indices), else
  // bitwise tuple equality.
  std::vector<uint32_t> vid(num_corners);
  if (channel.indices) {
    for (uint32_t i = 0; i < num_corners; i++) {
      vid[i] = channel.indices[i];
    }
  } else {
    // Hash bitwise tuples to ids.
    std::vector<uint32_t> order(num_corners);
    for (uint32_t i = 0; i < num_corners; i++) {
      order[i] = i;
    }
    const float *vals = channel.values;
    std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
      return memcmp(&vals[size_t(x) * stride], &vals[size_t(y) * stride],
                    sizeof(float) * stride) < 0;
    });
    uint32_t next_id = 0;
    for (uint32_t i = 0; i < num_corners; i++) {
      if (i && memcmp(&vals[size_t(order[i]) * stride],
                      &vals[size_t(order[i - 1]) * stride],
                      sizeof(float) * stride) != 0) {
        next_id++;
      }
      vid[order[i]] = next_id;
    }
  }

  // --- Merge corners into spans across continuous edge ends ------------------
  // Two corners at the same vertex on the two faces sharing an edge merge
  // iff the value ids at that vertex agree across the edge.
  UnionFind uf;
  uf.Init(num_corners);
  for (uint32_t e = 0; e < geo_topo.num_edges; e++) {
    const uint32_t f1 = geo_topo.edge_faces[2 * e + 1];
    if (f1 == kInvalidIndex) {
      continue;  // boundary edge
    }
    const uint32_t f0 = geo_topo.edge_faces[2 * e];
    for (int end = 0; end < 2; end++) {
      const uint32_t v = geo_topo.edge_verts[2 * e + end];
      const uint32_t c0 = CornerOfVertexInFace(geo_topo, geo_fvi, f0, v);
      const uint32_t c1 = CornerOfVertexInFace(geo_topo, geo_fvi, f1, v);
      if (c0 == kInvalidIndex || c1 == kInvalidIndex) {
        continue;
      }
      if (vid[c0] == vid[c1]) {
        uf.Union(c0, c1);
      }
    }
  }

  // --- Compact classes into split vertex ids ---------------------------------
  std::vector<uint32_t> class_id(num_corners, kInvalidIndex);
  uint32_t num_split = 0;
  for (uint32_t i = 0; i < num_corners; i++) {
    const uint32_t root = uf.Find(i);
    if (class_id[root] == kInvalidIndex) {
      class_id[root] = num_split++;
    }
    class_id[i] = class_id[root];
  }

  state->stride = stride;
  state->num_values = num_split;
  state->fvi.resize(num_corners);
  for (uint32_t i = 0; i < num_corners; i++) {
    state->fvi[i] = class_id[i];
  }

  // Values + per-split-vertex info (geometry vertex, value id, span size).
  state->values.assign(size_t(num_split) * stride, 0.0f);
  std::vector<uint32_t> split_geo_vert(num_split, kInvalidIndex);
  std::vector<uint32_t> split_vid(num_split, 0);
  std::vector<uint32_t> span_size(num_split, 0);
  for (uint32_t i = 0; i < num_corners; i++) {
    const uint32_t s = class_id[i];
    span_size[s]++;
    if (split_geo_vert[s] != kInvalidIndex) {
      continue;
    }
    split_geo_vert[s] = geo_fvi[i];
    split_vid[s] = vid[i];
    const uint32_t src = channel.indices ? channel.indices[i] : i;
    memcpy(&state->values[size_t(s) * stride],
           &channel.values[size_t(src) * stride], sizeof(float) * stride);
  }

  // --- Split topology ----------------------------------------------------------
  Result r = BuildTopology(geo_fvc, geo_topo.num_faces, state->fvi.data(),
                           num_corners, num_split, &state->topo, err);
  if (r != Result::Success) {
    return Fail(r, err, "fvar split-mesh topology is invalid.");
  }
  const Topology &st = state->topo;

  // --- Split edge sharpness -------------------------------------------------------
  // Interior split edges inherit the geometry edge's sharpness (creases);
  // split-boundary edges (seams + geometry boundary) are infinitely sharp.
  state->edge_sharp.assign(st.num_edges, 0.0f);
  for (uint32_t k = 0; k < num_corners; k++) {
    const float gs = geo_edge_sharp[geo_topo.face_edges[k]];
    float &ss = state->edge_sharp[st.face_edges[k]];
    if (ss < gs) {
      ss = gs;
    }
  }
  for (uint32_t e = 0; e < st.num_edges; e++) {
    if (st.IsBoundaryEdge(e)) {
      state->edge_sharp[e] = kInfiniteSharpness;
    }
  }

  // --- Per-vertex bookkeeping for the mode rules ----------------------------------
  // For each geometry vertex: number of split classes and number of distinct
  // value ids; detect "disjoint" values (same id in several classes).
  const uint32_t num_geo_verts = geo_topo.num_points;
  std::vector<uint32_t> classes_at(num_geo_verts, 0);
  for (uint32_t s = 0; s < num_split; s++) {
    classes_at[split_geo_vert[s]]++;
  }
  std::vector<uint32_t> class_off(num_geo_verts + 1, 0);
  for (uint32_t v = 0; v < num_geo_verts; v++) {
    class_off[v + 1] = class_off[v] + classes_at[v];
  }
  std::vector<uint32_t> classes(num_split);
  {
    std::vector<uint32_t> cursor(class_off.begin(), class_off.end() - 1);
    for (uint32_t s = 0; s < num_split; s++) {
      classes[cursor[split_geo_vert[s]]++] = s;
    }
  }

  // Per split vertex: interior-edge sharpness inventory.
  std::vector<uint32_t> inf_interior(num_split, 0);
  std::vector<float> max_semi_interior(num_split, 0.0f);
  for (uint32_t e = 0; e < st.num_edges; e++) {
    if (st.IsBoundaryEdge(e)) {
      continue;
    }
    const float s = state->edge_sharp[e];
    if (!IsSharp(s)) {
      continue;
    }
    for (int end = 0; end < 2; end++) {
      const uint32_t sv = st.edge_verts[2 * e + end];
      if (IsInfinitelySharp(s)) {
        inf_interior[sv]++;
      } else if (max_semi_interior[sv] < s) {
        max_semi_interior[sv] = s;
      }
    }
  }

  // --- Mode-dependent vertex sharpening ----------------------------------------
  const bool corners_sharp = (mode != FVarLinearInterpolation::None);
  const bool linear_boundaries = (mode == FVarLinearInterpolation::Boundaries);
  const bool dependent = (mode == FVarLinearInterpolation::CornersPlus1) ||
                         (mode == FVarLinearInterpolation::CornersPlus2);
  const bool plus2 = (mode == FVarLinearInterpolation::CornersPlus2);
  const bool sharpen_darts = plus2 || linear_boundaries;

  state->vert_sharp.assign(num_split, 0.0f);
  for (uint32_t v = 0; v < num_geo_verts; v++) {
    const uint32_t begin = class_off[v];
    const uint32_t end = class_off[v + 1];
    const uint32_t nclasses = end - begin;
    if (nclasses == 0) {
      continue;
    }

    // Distinct value ids and disjoint (duplicated) value detection.
    uint32_t nvals = nclasses;
    bool any_disjoint = false;
    for (uint32_t i = begin; i < end; i++) {
      for (uint32_t j = begin; j < i; j++) {
        if (split_vid[classes[i]] == split_vid[classes[j]]) {
          nvals--;
          any_disjoint = true;
          break;
        }
      }
    }

    const float gvs = geo_vert_sharp[v];
    const bool geo_boundary = geo_topo.vert_is_boundary[v] != 0;

    for (uint32_t i = begin; i < end; i++) {
      const uint32_t sv = classes[i];
      float s = gvs;  // geometry corner/pin sharpness applies to all values

      const bool split_boundary = st.vert_is_boundary[sv] != 0;
      bool inf = false;

      if (linear_boundaries && split_boundary) {
        inf = true;  // all boundary values piecewise linear
      }
      if (dependent && nvals > 2) {
        inf = true;  // 3+ distinct values at a vertex
      }
      if (sharpen_darts && nclasses == 1 && !geo_boundary && split_boundary) {
        inf = true;  // dart: interior vertex the seam passes through
      }
      if (corners_sharp && span_size[sv] == 1) {
        inf = true;  // chart corner (single-face span)
      }
      if (inf_interior[sv] > 0) {
        inf = true;  // span crosses an infinitely sharp crease
      }
      if (any_disjoint) {
        inf = true;  // same value in disconnected spans: sharpen
      }
      if (dependent && nclasses == 2 && nvals == 2) {
        const uint32_t other = classes[begin + (i - begin == 0 ? 1 : 0)];
        if (inf_interior[other] > 0) {
          inf = true;  // sharp crease through the other span sharpens both
        }
        if (plus2 && (span_size[other] == 1 || span_size[sv] == 1)) {
          inf = true;  // a corner on either side sharpens both
        }
        // Dependent semi-sharpness: if only the other span is semi-sharp,
        // this value follows its (decaying) sharpness.
        if (IsSharp(max_semi_interior[other]) &&
            !IsSharp(max_semi_interior[sv]) && s < max_semi_interior[other]) {
          s = max_semi_interior[other];
        }
      }

      if (inf) {
        s = kInfiniteSharpness;
      }
      state->vert_sharp[sv] = s;
    }
  }

  return Result::Success;
}

Result RefineFVarSplitOnce(FVarSplitState *state, const Options &opts,
                           bool more_levels, std::string *err) {
  const Topology &topo = state->topo;
  const uint32_t stride = state->stride;
  const bool loop = (opts.scheme == Scheme::Loop);

  ChildTopo child;
  Result r = loop
                 ? BuildChildTopologyTri(topo, state->fvi.data(), &child, err)
                 : BuildChildTopologyQuad(topo, state->fvi.data(), &child, err);
  if (r != Result::Success) {
    return r;
  }

  SharpnessCtx sharp;
  sharp.edge_sharpness = state->edge_sharp.data();
  sharp.vert_sharpness = state->vert_sharp.data();

  std::vector<float> child_values(size_t(child.num_points) * stride);
  if (loop) {
    LoopRefineValues(topo, state->fvi.data(), state->values.data(), stride,
                     sharp, opts, child_values.data());
  } else {
    CatmarkRefineValues(topo, state->fvi.data(), state->values.data(), stride,
                        sharp, opts, child_values.data());
  }

  state->values = std::move(child_values);
  state->num_values = child.num_points;

  if (more_levels) {
    Topology child_topo;
    r = BuildTopology(child.fvc.data(), uint32_t(child.fvc.size()),
                      child.fvi.data(), uint32_t(child.fvi.size()),
                      child.num_points, &child_topo, err);
    if (r != Result::Success) {
      return r;
    }
    std::vector<float> child_edge_sharp;
    std::vector<float> child_vert_sharp;
    DeriveChildSharpness(topo, state->edge_sharp, state->vert_sharp,
                         child_topo, opts.creasing, &child_edge_sharp,
                         &child_vert_sharp);
    state->topo = std::move(child_topo);
    state->edge_sharp = std::move(child_edge_sharp);
    state->vert_sharp = std::move(child_vert_sharp);
  }
  state->fvi = std::move(child.fvi);

  return Result::Success;
}

// Linear per-corner refinement for the quad split. `corner_values` holds one
// tuple per parent face corner; `child_corner_values` receives 4 tuples per
// child face (4 * parent corner count), in the child-face order emitted by
// BuildChildTopologyQuad (per parent face, per corner).
void LinearFVarRefineQuad(const Topology &topo, const float *corner_values,
                          uint32_t stride, const Options &opts,
                          float *child_corner_values) {
  ParallelFor(opts, topo.num_faces, [&](uint32_t f) {
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;

    // Face average.
    float favg[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float w = 1.0f / float(n);
    for (uint32_t k = 0; k < n; k++) {
      const float *src = &corner_values[size_t(begin + k) * stride];
      for (uint32_t c = 0; c < stride; c++) {
        favg[c] += w * src[c];
      }
    }

    // Child quad k corners: [corner k, mid(k, k+1), face avg, mid(k-1, k)].
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t next = (k + 1 == n) ? 0 : (k + 1);
      const uint32_t prev = (k == 0) ? (n - 1) : (k - 1);
      const float *ck = &corner_values[size_t(begin + k) * stride];
      const float *cn = &corner_values[size_t(begin + next) * stride];
      const float *cp = &corner_values[size_t(begin + prev) * stride];

      float *dst = &child_corner_values[(size_t(begin) + k) * 4 * stride];
      for (uint32_t c = 0; c < stride; c++) {
        dst[0 * stride + c] = ck[c];
        dst[1 * stride + c] = 0.5f * (ck[c] + cn[c]);
        dst[2 * stride + c] = favg[c];
        dst[3 * stride + c] = 0.5f * (cp[c] + ck[c]);
      }
    }
  });
}

void LinearFVarRefineTri(const Topology &topo, const float *corner_values,
                         uint32_t stride, const Options &opts,
                         float *child_corner_values) {
  ParallelFor(opts, topo.num_faces, [&](uint32_t f) {
    const uint32_t begin = topo.face_offsets[f];
    const float *c0 = &corner_values[size_t(begin) * stride];
    const float *c1 = &corner_values[size_t(begin + 1) * stride];
    const float *c2 = &corner_values[size_t(begin + 2) * stride];

    // Child face order: corner 0, corner 1, corner 2, center; corner layouts
    // match BuildChildTopologyTri: (vk, m_k,k+1, m_k-1,k), center
    // (m01, m12, m20).
    float *dst = &child_corner_values[size_t(f) * 12 * stride];
    for (uint32_t c = 0; c < stride; c++) {
      const float m01 = 0.5f * (c0[c] + c1[c]);
      const float m12 = 0.5f * (c1[c] + c2[c]);
      const float m20 = 0.5f * (c2[c] + c0[c]);
      dst[0 * stride + c] = c0[c];
      dst[1 * stride + c] = m01;
      dst[2 * stride + c] = m20;
      dst[3 * stride + c] = c1[c];
      dst[4 * stride + c] = m12;
      dst[5 * stride + c] = m01;
      dst[6 * stride + c] = c2[c];
      dst[7 * stride + c] = m20;
      dst[8 * stride + c] = m12;
      dst[9 * stride + c] = m01;
      dst[10 * stride + c] = m12;
      dst[11 * stride + c] = m20;
    }
  });
}

}  // namespace tsd
}  // namespace tinyusdz
