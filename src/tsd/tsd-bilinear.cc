// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: one-level bilinear refinement kernel.
//
// Bilinear shares the quad-split topology with Catmull-Clark but uses
// linear weights everywhere (Sdc trivializes its masks: vertex children
// pass through, edge children are midpoints, face children centroids).
// Sharpness has no effect. This kernel doubles as the RefineMode::Linear
// path for "varying" primvars under any quad scheme.

#include "tsd-internal.hh"
#include "tsd-kernel.hh"

namespace tinyusdz {
namespace tsd {

void BilinearRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                          const float *values, uint32_t stride,
                          const Options &opts, float *child_values) {
  const uint32_t V = topo.num_points;
  const uint32_t E = topo.num_edges;
  const uint32_t F = topo.num_faces;

  float *vert_child = child_values;
  float *edge_child = child_values + size_t(V) * stride;
  float *face_child = child_values + (size_t(V) + E) * stride;

  ParallelFor(opts, V, [&](uint32_t v) {
    ComputeBilinearVertexChild(values, stride, v, &vert_child[size_t(v) * stride]);
  });

  ParallelFor(opts, E, [&](uint32_t e) {
    ComputeBilinearEdgeChild(topo, values, stride, e,
                             &edge_child[size_t(e) * stride]);
  });

  ParallelFor(opts, F, [&](uint32_t f) {
    ComputeFaceChild(topo, parent_fvi, values, stride, f,
                     &face_child[size_t(f) * stride]);
  });
}

}  // namespace tsd
}  // namespace tinyusdz
