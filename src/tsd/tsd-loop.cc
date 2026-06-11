// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: one-level Loop refinement kernel (all-triangle input).
//
// OpenSubdiv Sdc-compatible Loop rules:
//   - edge child: smooth 3/8 (v0+v1) + 1/8 (opposite vertices), crease
//     (midpoint), or a fractional blend for sharpness in (0, 1)
//   - vertex child: smooth with beta weights (regular valence 6: 5/8 vertex,
//     1/16 per neighbor; else beta = 1/4 cos(2pi/n) + 3/8,
//     edge weight = (5/8 - beta^2)/n), crease (3/4 V + 1/8 (ea+eb)),
//     corner (V), with the same transitional sharpness blending as
//     Catmull-Clark.
// Boundary modes come baked into the sharpness context, exactly as for the
// Catmull-Clark kernel.

#include <cmath>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

namespace {

enum class VRule : uint8_t { Smooth, Crease, Corner };

inline VRule DetermineVertexRule(float vert_sharpness,
                                 uint32_t sharp_edge_count) {
  if (IsSharp(vert_sharpness)) {
    return VRule::Corner;
  }
  if (sharp_edge_count > 2) {
    return VRule::Corner;
  }
  return (sharp_edge_count == 2) ? VRule::Crease : VRule::Smooth;
}

// Third vertex of triangle `f` that is not `a` or `b`.
inline uint32_t OppositeVertex(const Topology &topo, const uint32_t *fvi,
                               uint32_t f, uint32_t a, uint32_t b) {
  const uint32_t begin = topo.face_offsets[f];
  for (uint32_t k = 0; k < 3; k++) {
    const uint32_t v = fvi[begin + k];
    if (v != a && v != b) {
      return v;
    }
  }
  return fvi[begin];  // degenerate; unreachable for valid input
}

}  // namespace

void LoopRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                      const float *values, uint32_t stride,
                      const SharpnessCtx &sharp, const Options &opts,
                      float *child_values) {
  const uint32_t V = topo.num_points;
  const uint32_t E = topo.num_edges;

  float *vert_child = child_values;
  float *edge_child = child_values + size_t(V) * stride;

  // --- 1. Edge children -------------------------------------------------------
  ParallelFor(opts, E, [&](uint32_t e) {
    const uint32_t v0 = topo.edge_verts[2 * e];
    const uint32_t v1 = topo.edge_verts[2 * e + 1];
    const float *p0 = &values[size_t(v0) * stride];
    const float *p1 = &values[size_t(v1) * stride];
    float *dst = &edge_child[size_t(e) * stride];

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
  });

  // --- 2. Vertex children -------------------------------------------------------
  const bool chaikin = (opts.creasing == CreasingMethod::Chaikin);
  ParallelFor(opts, V, [&](uint32_t v) {
    const uint32_t ebegin = topo.vert_edge_offsets[v];
    const uint32_t eend = topo.vert_edge_offsets[v + 1];
    const uint32_t valence = eend - ebegin;

    const float *pv = &values[size_t(v) * stride];
    float *dst = &vert_child[size_t(v) * stride];

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
  });
}

}  // namespace tsd
}  // namespace tinyusdz
