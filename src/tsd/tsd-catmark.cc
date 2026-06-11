// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: one-level Catmull-Clark refinement kernel.
//
// Implements the Catmull-Clark subdivision rules with OpenSubdiv
// Sdc-compatible semi-sharp crease semantics:
//   - edge child: smooth (v0+v1+f0+f1)/4, crease (midpoint), or a
//     fractional blend for sharpness in (0, 1)
//   - vertex child: smooth ((n-2)/n*V + sum(E)/n^2 + sum(F)/n^2), crease
//     (3/4*V + 1/8*(ea+eb)), corner (V), with transitional blending between
//     the parent rule and the (sharpness-decayed) child rule
//   - triangleSubdivisionRule "smooth": alternate edge-child weights when an
//     incident face is a triangle
// Boundary modes are realized purely through sharpness: the driver bakes
// boundary edges (and corner vertices for edgeAndCorner) to infinite
// sharpness before calling this kernel.

#include <cstring>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

float ChaikinChildEdgeSharpness(float edge_sharpness,
                                uint32_t incident_edge_count,
                                const float *incident_edge_sharpness) {
  if (incident_edge_count < 2) {
    return DecrementSharpness(edge_sharpness);
  }
  if (!IsSharp(edge_sharpness)) {
    return 0.0f;
  }
  if (IsInfinitelySharp(edge_sharpness)) {
    return kInfiniteSharpness;
  }

  float sharp_sum = 0.0f;
  uint32_t sharp_count = 0;
  for (uint32_t i = 0; i < incident_edge_count; i++) {
    if (IsSemiSharp(incident_edge_sharpness[i])) {
      sharp_count++;
      sharp_sum += incident_edge_sharpness[i];
    }
  }
  float s = edge_sharpness;
  if (sharp_count > 1) {
    // Chaikin: 3/4 own sharpness + 1/4 average of the other sharp edges.
    const float avg_others =
        (sharp_sum - edge_sharpness) / float(sharp_count - 1);
    s = 0.75f * edge_sharpness + 0.25f * avg_others;
  }
  s -= 1.0f;
  return IsSharp(s) ? s : 0.0f;
}

namespace {

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

}  // namespace

void CatmarkRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                         const float *values, uint32_t stride,
                         const SharpnessCtx &sharp, const Options &opts,
                         float *child_values) {
  const uint32_t V = topo.num_points;
  const uint32_t E = topo.num_edges;
  const uint32_t F = topo.num_faces;

  float *vert_child = child_values;
  float *edge_child = child_values + size_t(V) * stride;
  float *face_child = child_values + (size_t(V) + E) * stride;

  // --- 1. Face children: centroids -----------------------------------------
  ParallelFor(opts, F, [&](uint32_t f) {
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;
    AccumF acc;
    acc.Zero(stride);
    const float w = 1.0f / float(n);
    for (uint32_t k = 0; k < n; k++) {
      acc.AddScaled(&values[size_t(parent_fvi[begin + k]) * stride], w,
                    stride);
    }
    acc.Store(&face_child[size_t(f) * stride], stride);
  });

  // --- 2. Edge children -----------------------------------------------------
  const bool tri_smooth =
      (opts.triangle_subdivision == TriangleSubdivision::Smooth);
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
      // Full crease: midpoint. (Boundary edges are always infinite.)
      AccumF acc;
      acc.Zero(stride);
      acc.AddScaled(p0, 0.5f, stride);
      acc.AddScaled(p1, 0.5f, stride);
      acc.Store(dst, stride);
      return;
    }

    // Smooth mask. For two incident faces the face weights are 1/4 each
    // (or the alternate triangle weights); face weights apply to the child
    // face points.
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
      acc.AddScaled(&face_child[size_t(f0) * stride], fw, stride);
      acc.AddScaled(&face_child[size_t(f1) * stride], fw, stride);
    } else {
      // Single incident face (only reachable if a boundary edge were not
      // sharpened): fw = 0.5 / faceCount per Sdc.
      acc.AddScaled(&face_child[size_t(f0) * stride], 2.0f * fw, stride);
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
  });

  // --- 3. Vertex children ----------------------------------------------------
  const bool chaikin = (opts.creasing == CreasingMethod::Chaikin);
  ParallelFor(opts, V, [&](uint32_t v) {
    const uint32_t ebegin = topo.vert_edge_offsets[v];
    const uint32_t eend = topo.vert_edge_offsets[v + 1];
    const uint32_t valence = eend - ebegin;
    const uint32_t fbegin = topo.vert_face_offsets[v];
    const uint32_t fend = topo.vert_face_offsets[v + 1];
    const uint32_t face_count = fend - fbegin;

    const float *pv = &values[size_t(v) * stride];
    float *dst = &vert_child[size_t(v) * stride];

    // Gather parent and child (decayed) sharpness around the vertex.
    // kMaxFaceDegree bounds nothing here -- valence is unbounded -- so use
    // a small stack buffer with heap fallback.
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
        // Pick the two sharp edges from the sharpness set that defined the
        // rule (parent or child).
        uint32_t found = 0;
        for (uint32_t i = 0; i < valence && found < 2; i++) {
          if (IsSharp(rule_es[i])) {
            const uint32_t e = topo.vert_edges[ebegin + i];
            const uint32_t other = (topo.edge_verts[2 * e] == v)
                                       ? topo.edge_verts[2 * e + 1]
                                       : topo.edge_verts[2 * e];
            acc->AddScaled(&values[size_t(other) * stride], w * 0.125f,
                           stride);
            found++;
          }
        }
        return;
      }
      // Smooth: (n-2)/n * V + 1/n^2 * sum(edge other endpoints)
      //                     + 1/n^2 * sum(child face points).
      // Only valid for interior manifold vertices (faces == edges); the
      // sharpness baking guarantees boundary vertices never reach here.
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
        acc->AddScaled(&face_child[size_t(f) * stride], w * ew, stride);
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

    // Transitional: blend parent rule and child rule by the fractional
    // weight (Sdc::Crease::ComputeFractionalWeightAtVertex).
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
  });
}

}  // namespace tsd
}  // namespace tinyusdz
