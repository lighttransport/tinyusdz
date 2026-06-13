// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: per-element value kernels (one child vertex at a time).
//
// The bulk refinement kernels (CatmarkRefineValues / LoopRefineValues /
// BilinearRefineValues) and the streaming refinement (tsd-stream.cc) share
// these helpers so the math lives in exactly one place and is bit-identical
// between the bulk and streaming paths. Each helper computes one child value
// (a vertex-child, edge-child, or face-child tuple) purely from the parent
// topology + parent values + sharpness context -- scatter-free, so callers may
// invoke them per element in any order or in parallel.
//
// Catmull-Clark edge/vertex children consume face-children (centroids). The
// bulk path precomputes a full face_child array and passes an accessor that
// indexes it; the streaming path passes an accessor that recomputes the
// centroid into a scratch tuple on demand (centroids are consumed sequentially,
// so a single reused scratch tuple is safe). Both produce identical bits.

#ifndef TINYUSDZ_TSD_KERNEL_HH_
#define TINYUSDZ_TSD_KERNEL_HH_

#include <cmath>
#include <vector>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

// Vertex-vertex subdivision rule, matching Sdc::Crease semantics.
enum class VRule : uint8_t { Smooth, Crease, Corner };

inline VRule DetermineVertexRule(float vert_sharpness,
                                 uint32_t sharp_edge_count) {
  if (IsSharp(vert_sharpness)) {
    return VRule::Corner;
  }
  if (sharp_edge_count > 2) {
    return VRule::Corner;
  }
  // 0 or 1 sharp edges => smooth (1 == dart, same mask).
  return (sharp_edge_count == 2) ? VRule::Crease : VRule::Smooth;
}

// Small fixed-capacity accumulator (stride <= 4).
struct AccumF {
  float v[4];
  void Zero(uint32_t stride) {
    for (uint32_t c = 0; c < stride; c++) {
      v[c] = 0.0f;
    }
  }
  void AddScaled(const float *src, float w, uint32_t stride) {
    for (uint32_t c = 0; c < stride; c++) {
      v[c] += w * src[c];
    }
  }
  void Store(float *dst, uint32_t stride) const {
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = v[c];
    }
  }
};

// --- Shared face child (centroid) -------------------------------------------

inline void ComputeFaceChild(const Topology &topo, const uint32_t *parent_fvi,
                             const float *values, uint32_t stride, uint32_t f,
                             float *out) {
  const uint32_t begin = topo.face_offsets[f];
  const uint32_t n = topo.face_offsets[f + 1] - begin;
  const float w = 1.0f / float(n);
  AccumF acc;
  acc.Zero(stride);
  for (uint32_t k = 0; k < n; k++) {
    acc.AddScaled(&values[size_t(parent_fvi[begin + k]) * stride], w, stride);
  }
  acc.Store(out, stride);
}

// --- Bilinear ----------------------------------------------------------------

inline void ComputeBilinearVertexChild(const float *values, uint32_t stride,
                                       uint32_t v, float *out) {
  const float *src = &values[size_t(v) * stride];
  for (uint32_t c = 0; c < stride; c++) {
    out[c] = src[c];
  }
}

inline void ComputeBilinearEdgeChild(const Topology &topo, const float *values,
                                     uint32_t stride, uint32_t e, float *out) {
  const float *p0 = &values[size_t(topo.edge_verts[2 * e]) * stride];
  const float *p1 = &values[size_t(topo.edge_verts[2 * e + 1]) * stride];
  for (uint32_t c = 0; c < stride; c++) {
    out[c] = 0.5f * (p0[c] + p1[c]);
  }
}

// --- Catmull-Clark -----------------------------------------------------------

// `get_face_child(f)` returns a pointer to face f's centroid tuple.
template <typename FaceChildFn>
inline void ComputeCatmarkEdgeChild(const Topology &topo, const float *values,
                                    uint32_t stride, const SharpnessCtx &sharp,
                                    const Options &opts, uint32_t e,
                                    FaceChildFn get_face_child, float *dst) {
  const bool tri_smooth =
      (opts.triangle_subdivision == TriangleSubdivision::Smooth);
  const uint32_t v0 = topo.edge_verts[2 * e];
  const uint32_t v1 = topo.edge_verts[2 * e + 1];
  const float *p0 = &values[size_t(v0) * stride];
  const float *p1 = &values[size_t(v1) * stride];

  const float s = sharp.EdgeSharpness(e);
  const uint32_t f0 = topo.edge_faces[2 * e];
  const uint32_t f1 = topo.edge_faces[2 * e + 1];
  const bool interior = (f1 != kInvalidIndex);

  if (IsSharp(s) && (s >= 1.0f || !interior)) {
    // Full crease: midpoint. (Boundary edges are always infinite.)
    AccumF acc;
    acc.Zero(stride);
    acc.AddScaled(p0, 0.5f, stride);
    acc.AddScaled(p1, 0.5f, stride);
    acc.Store(dst, stride);
    return;
  }

  float vw = 0.25f;
  float fw = 0.25f;
  if (interior && tri_smooth) {
    const uint32_t n0 = topo.face_offsets[f0 + 1] - topo.face_offsets[f0];
    const uint32_t n1 = topo.face_offsets[f1 + 1] - topo.face_offsets[f1];
    if (n0 == 3 || n1 == 3) {
      const float kTriWeight = 0.470f;
      const float f0w = (n0 == 3) ? kTriWeight : 0.25f;
      const float f1w = (n1 == 3) ? kTriWeight : 0.25f;
      fw = 0.5f * (f0w + f1w);
      vw = 0.5f * (1.0f - 2.0f * fw);
    }
  }

  AccumF acc;
  acc.Zero(stride);
  acc.AddScaled(p0, vw, stride);
  acc.AddScaled(p1, vw, stride);
  if (interior) {
    acc.AddScaled(get_face_child(f0), fw, stride);
    acc.AddScaled(get_face_child(f1), fw, stride);
  } else {
    acc.AddScaled(get_face_child(f0), 2.0f * fw, stride);
  }

  if (IsSharp(s)) {
    // Fractional crease in (0,1): blend crease (midpoint) into smooth.
    const float pw = s;
    const float cw = 1.0f - s;
    for (uint32_t c = 0; c < stride; c++) {
      acc.v[c] = cw * acc.v[c] + pw * 0.5f * (p0[c] + p1[c]);
    }
  }
  acc.Store(dst, stride);
}

template <typename FaceChildFn>
inline void ComputeCatmarkVertexChild(const Topology &topo,
                                      const float *values, uint32_t stride,
                                      const SharpnessCtx &sharp,
                                      const Options &opts, uint32_t v,
                                      FaceChildFn get_face_child, float *dst) {
  const bool chaikin = (opts.creasing == CreasingMethod::Chaikin);
  const uint32_t ebegin = topo.vert_edge_offsets[v];
  const uint32_t eend = topo.vert_edge_offsets[v + 1];
  const uint32_t valence = eend - ebegin;
  const uint32_t fbegin = topo.vert_face_offsets[v];
  const uint32_t fend = topo.vert_face_offsets[v + 1];
  const uint32_t face_count = fend - fbegin;

  const float *pv = &values[size_t(v) * stride];

  if (valence == 0) {
    // Unreferenced point: pass through.
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = pv[c];
    }
    return;
  }

  // Gather parent and child (decayed) sharpness around the vertex. valence is
  // unbounded, so use a small stack buffer with heap fallback.
  float sharp_stack[32];
  std::vector<float> sharp_heap;
  float *es = sharp_stack;
  if (valence > 16) {
    sharp_heap.resize(size_t(valence) * 2);
    es = sharp_heap.data();
  }
  float *ces = es + valence;  // child edge sharpness

  const float vs = sharp.VertSharpness(v);
  const float cvs = DecrementSharpness(vs);

  uint32_t sharp_edges = 0;
  uint32_t child_sharp_edges = 0;
  for (uint32_t i = 0; i < valence; i++) {
    const float s = sharp.EdgeSharpness(topo.vert_edges[ebegin + i]);
    es[i] = s;
    sharp_edges += IsSharp(s) ? 1u : 0u;
  }
  for (uint32_t i = 0; i < valence; i++) {
    ces[i] = chaikin ? ChaikinChildEdgeSharpness(es[i], valence, es)
                     : DecrementSharpness(es[i]);
    child_sharp_edges += IsSharp(ces[i]) ? 1u : 0u;
  }

  const VRule prule = DetermineVertexRule(vs, sharp_edges);
  const VRule crule = DetermineVertexRule(cvs, child_sharp_edges);

  // Accumulates one rule's contribution scaled by `w`.
  auto accumulate_rule = [&](VRule rule, const float *rule_es, float w,
                             AccumF *acc) {
    if (rule == VRule::Corner) {
      acc->AddScaled(pv, w, stride);
      return;
    }
    if (rule == VRule::Crease) {
      // 3/4 V + 1/8 (other endpoint of each of the two sharp edges).
      acc->AddScaled(pv, w * 0.75f, stride);
      uint32_t found = 0;
      for (uint32_t i = 0; i < valence && found < 2; i++) {
        if (IsSharp(rule_es[i])) {
          const uint32_t e = topo.vert_edges[ebegin + i];
          const uint32_t other = (topo.edge_verts[2 * e] == v)
                                     ? topo.edge_verts[2 * e + 1]
                                     : topo.edge_verts[2 * e];
          acc->AddScaled(&values[size_t(other) * stride], w * 0.125f, stride);
          found++;
        }
      }
      return;
    }
    // Smooth: (n-2)/n * V + 1/n^2 * sum(edge other endpoints)
    //                     + 1/n^2 * sum(child face points).
    const float n = float(valence);
    const float vw = (n - 2.0f) / n;
    const float ew = 1.0f / (n * n);
    acc->AddScaled(pv, w * vw, stride);
    for (uint32_t i = 0; i < valence; i++) {
      const uint32_t e = topo.vert_edges[ebegin + i];
      const uint32_t other = (topo.edge_verts[2 * e] == v)
                                 ? topo.edge_verts[2 * e + 1]
                                 : topo.edge_verts[2 * e];
      acc->AddScaled(&values[size_t(other) * stride], w * ew, stride);
    }
    for (uint32_t i = 0; i < face_count; i++) {
      const uint32_t f = topo.vert_faces[fbegin + i];
      acc->AddScaled(get_face_child(f), w * ew, stride);
    }
  };

  AccumF acc;
  acc.Zero(stride);

  if (prule == crule) {
    // Note: parent rule uses parent sharpness to pick crease ends.
    accumulate_rule(prule, es, 1.0f, &acc);
    acc.Store(dst, stride);
    return;
  }

  // Transitional: blend parent rule and child rule by the fractional weight
  // (Sdc::Crease::ComputeFractionalWeightAtVertex).
  float transition_sum = 0.0f;
  uint32_t transition_count = 0;
  if (IsSharp(vs) && !IsSharp(cvs)) {
    transition_count = 1;
    transition_sum = vs;
  }
  if (!chaikin) {
    for (uint32_t i = 0; i < valence; i++) {
      if (IsSharp(es[i]) && es[i] <= 1.0f) {
        transition_sum += es[i];
        transition_count++;
      }
    }
  } else {
    for (uint32_t i = 0; i < valence; i++) {
      if (IsSharp(es[i]) && !IsSharp(ces[i])) {
        transition_sum += es[i];
        transition_count++;
      }
    }
  }
  float pw = 0.0f;
  if (transition_count) {
    pw = transition_sum / float(transition_count);
    if (pw > 1.0f) {
      pw = 1.0f;
    }
  }

  accumulate_rule(prule, es, pw, &acc);
  accumulate_rule(crule, ces, 1.0f - pw, &acc);
  acc.Store(dst, stride);
}

// --- Loop --------------------------------------------------------------------

// Third vertex of triangle `f` that is not `a` or `b`.
inline uint32_t OppositeVertex(const Topology &topo, const uint32_t *fvi,
                               uint32_t f, uint32_t a, uint32_t b) {
  const uint32_t begin = topo.face_offsets[f];
  for (uint32_t k = 0; k < 3; k++) {
    const uint32_t vv = fvi[begin + k];
    if (vv != a && vv != b) {
      return vv;
    }
  }
  return fvi[begin];  // degenerate; unreachable for valid input
}

inline void ComputeLoopEdgeChild(const Topology &topo, const uint32_t *parent_fvi,
                                 const float *values, uint32_t stride,
                                 const SharpnessCtx &sharp, uint32_t e,
                                 float *dst) {
  const uint32_t v0 = topo.edge_verts[2 * e];
  const uint32_t v1 = topo.edge_verts[2 * e + 1];
  const float *p0 = &values[size_t(v0) * stride];
  const float *p1 = &values[size_t(v1) * stride];

  const float s = sharp.EdgeSharpness(e);
  const uint32_t f0 = topo.edge_faces[2 * e];
  const uint32_t f1 = topo.edge_faces[2 * e + 1];
  const bool interior = (f1 != kInvalidIndex);

  if (IsSharp(s) && (s >= 1.0f || !interior)) {
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = 0.5f * (p0[c] + p1[c]);
    }
    return;
  }

  // Smooth: 3/8 endpoints + 1/8 opposite vertices.
  const uint32_t o0 = OppositeVertex(topo, parent_fvi, f0, v0, v1);
  const float *q0 = &values[size_t(o0) * stride];
  const float *q1 = q0;
  if (interior) {
    const uint32_t o1 = OppositeVertex(topo, parent_fvi, f1, v0, v1);
    q1 = &values[size_t(o1) * stride];
  }
  for (uint32_t c = 0; c < stride; c++) {
    float smooth = 0.375f * (p0[c] + p1[c]);
    smooth += interior ? (0.125f * (q0[c] + q1[c])) : (0.25f * q0[c]);
    if (IsSharp(s)) {
      dst[c] = (1.0f - s) * smooth + s * 0.5f * (p0[c] + p1[c]);
    } else {
      dst[c] = smooth;
    }
  }
}

inline void ComputeLoopVertexChild(const Topology &topo, const float *values,
                                   uint32_t stride, const SharpnessCtx &sharp,
                                   const Options &opts, uint32_t v, float *dst) {
  const bool chaikin = (opts.creasing == CreasingMethod::Chaikin);
  const uint32_t ebegin = topo.vert_edge_offsets[v];
  const uint32_t eend = topo.vert_edge_offsets[v + 1];
  const uint32_t valence = eend - ebegin;

  const float *pv = &values[size_t(v) * stride];

  if (valence == 0) {
    // Unreferenced point: pass through.
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = pv[c];
    }
    return;
  }

  float sharp_stack[32];
  std::vector<float> sharp_heap;
  float *es = sharp_stack;
  if (valence > 16) {
    sharp_heap.resize(size_t(valence) * 2);
    es = sharp_heap.data();
  }
  float *ces = es + valence;

  const float vs = sharp.VertSharpness(v);
  const float cvs = DecrementSharpness(vs);

  uint32_t sharp_edges = 0;
  uint32_t child_sharp_edges = 0;
  for (uint32_t i = 0; i < valence; i++) {
    const float s = sharp.EdgeSharpness(topo.vert_edges[ebegin + i]);
    es[i] = s;
    sharp_edges += IsSharp(s) ? 1u : 0u;
  }
  for (uint32_t i = 0; i < valence; i++) {
    ces[i] = chaikin ? ChaikinChildEdgeSharpness(es[i], valence, es)
                     : DecrementSharpness(es[i]);
    child_sharp_edges += IsSharp(ces[i]) ? 1u : 0u;
  }

  const VRule prule = DetermineVertexRule(vs, sharp_edges);
  const VRule crule = DetermineVertexRule(cvs, child_sharp_edges);

  auto accumulate_rule = [&](VRule rule, const float *rule_es, float w,
                             float *acc) {
    if (rule == VRule::Corner) {
      for (uint32_t c = 0; c < stride; c++) {
        acc[c] += w * pv[c];
      }
      return;
    }
    if (rule == VRule::Crease) {
      for (uint32_t c = 0; c < stride; c++) {
        acc[c] += w * 0.75f * pv[c];
      }
      uint32_t found = 0;
      for (uint32_t i = 0; i < valence && found < 2; i++) {
        if (IsSharp(rule_es[i])) {
          const uint32_t e = topo.vert_edges[ebegin + i];
          const uint32_t other = (topo.edge_verts[2 * e] == v)
                                     ? topo.edge_verts[2 * e + 1]
                                     : topo.edge_verts[2 * e];
          const float *q = &values[size_t(other) * stride];
          for (uint32_t c = 0; c < stride; c++) {
            acc[c] += w * 0.125f * q[c];
          }
          found++;
        }
      }
      return;
    }
    // Smooth: regular valence 6 uses 5/8 + 1/16 per neighbor; otherwise
    // beta = 1/4 cos(2pi/n) + 3/8, edge weight = (5/8 - beta^2)/n.
    float ew = 0.0625f;
    float vw = 0.625f;
    if (valence != 6) {
      const double n = double(valence);
      const double beta =
          0.25 * std::cos(2.0 * 3.14159265358979323846 / n) + 0.375;
      ew = float((0.625 - beta * beta) / n);
      vw = 1.0f - ew * float(valence);
    }
    for (uint32_t c = 0; c < stride; c++) {
      acc[c] += w * vw * pv[c];
    }
    for (uint32_t i = 0; i < valence; i++) {
      const uint32_t e = topo.vert_edges[ebegin + i];
      const uint32_t other = (topo.edge_verts[2 * e] == v)
                                 ? topo.edge_verts[2 * e + 1]
                                 : topo.edge_verts[2 * e];
      const float *q = &values[size_t(other) * stride];
      for (uint32_t c = 0; c < stride; c++) {
        acc[c] += w * ew * q[c];
      }
    }
  };

  float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  if (prule == crule) {
    accumulate_rule(prule, es, 1.0f, acc);
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = acc[c];
    }
    return;
  }

  float transition_sum = 0.0f;
  uint32_t transition_count = 0;
  if (IsSharp(vs) && !IsSharp(cvs)) {
    transition_count = 1;
    transition_sum = vs;
  }
  if (!chaikin) {
    for (uint32_t i = 0; i < valence; i++) {
      if (IsSharp(es[i]) && es[i] <= 1.0f) {
        transition_sum += es[i];
        transition_count++;
      }
    }
  } else {
    for (uint32_t i = 0; i < valence; i++) {
      if (IsSharp(es[i]) && !IsSharp(ces[i])) {
        transition_sum += es[i];
        transition_count++;
      }
    }
  }
  float pw = 0.0f;
  if (transition_count) {
    pw = transition_sum / float(transition_count);
    if (pw > 1.0f) {
      pw = 1.0f;
    }
  }

  accumulate_rule(prule, es, pw, acc);
  accumulate_rule(crule, ces, 1.0f - pw, acc);
  for (uint32_t c = 0; c < stride; c++) {
    dst[c] = acc[c];
  }
}

}  // namespace tsd
}  // namespace tinyusdz

#endif  // TINYUSDZ_TSD_KERNEL_HH_
