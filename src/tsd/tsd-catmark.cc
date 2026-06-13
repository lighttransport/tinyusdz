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
#include "tsd-kernel.hh"

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

void DeriveChildSharpness(const Topology &parent_topo,
                          const std::vector<float> &parent_edge_sharp,
                          const std::vector<float> &parent_vert_sharp,
                          const Topology &child_topo, CreasingMethod method,
                          std::vector<float> *child_edge_sharp,
                          std::vector<float> *child_vert_sharp) {
  const uint32_t pV = parent_topo.num_points;
  const uint32_t pE = parent_topo.num_edges;
  const bool chaikin = (method == CreasingMethod::Chaikin);

  child_edge_sharp->assign(child_topo.num_edges, 0.0f);
  for (uint32_t e = 0; e < child_topo.num_edges; e++) {
    const uint32_t a = child_topo.edge_verts[2 * e];  // a < b
    const uint32_t b = child_topo.edge_verts[2 * e + 1];
    if (a >= pV || b < pV || b >= pV + pE) {
      continue;  // not a half of a parent edge
    }
    const uint32_t pe = b - pV;
    const float ps = parent_edge_sharp[pe];
    if (!IsSharp(ps)) {
      continue;
    }
    if (!chaikin) {
      (*child_edge_sharp)[e] = DecrementSharpness(ps);
    } else {
      const uint32_t begin = parent_topo.vert_edge_offsets[a];
      const uint32_t end = parent_topo.vert_edge_offsets[a + 1];
      const uint32_t valence = end - begin;
      float stack[32];
      std::vector<float> heap;
      float *incident = stack;
      if (valence > 32) {
        heap.resize(valence);
        incident = heap.data();
      }
      for (uint32_t i = 0; i < valence; i++) {
        incident[i] = parent_edge_sharp[parent_topo.vert_edges[begin + i]];
      }
      (*child_edge_sharp)[e] = ChaikinChildEdgeSharpness(ps, valence, incident);
    }
  }

  child_vert_sharp->assign(child_topo.num_points, 0.0f);
  for (uint32_t v = 0; v < pV; v++) {
    (*child_vert_sharp)[v] = DecrementSharpness(parent_vert_sharp[v]);
  }
}

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

  // Face-child accessor: bulk path indexes the precomputed centroid array.
  auto get_face_child = [&](uint32_t f) -> const float * {
    return &face_child[size_t(f) * stride];
  };

  // --- 1. Face children: centroids -----------------------------------------
  ParallelFor(opts, F, [&](uint32_t f) {
    ComputeFaceChild(topo, parent_fvi, values, stride, f,
                     &face_child[size_t(f) * stride]);
  });

  // --- 2. Edge children -----------------------------------------------------
  ParallelFor(opts, E, [&](uint32_t e) {
    ComputeCatmarkEdgeChild(topo, values, stride, sharp, opts, e,
                            get_face_child, &edge_child[size_t(e) * stride]);
  });

  // --- 3. Vertex children ----------------------------------------------------
  ParallelFor(opts, V, [&](uint32_t v) {
    ComputeCatmarkVertexChild(topo, values, stride, sharp, opts, v,
                              get_face_child, &vert_child[size_t(v) * stride]);
  });
}

}  // namespace tsd
}  // namespace tinyusdz
