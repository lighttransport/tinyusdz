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

#include "tsd-internal.hh"
#include "tsd-kernel.hh"

namespace tinyusdz {
namespace tsd {

void LoopRefineValues(const Topology &topo, const uint32_t *parent_fvi,
                      const float *values, uint32_t stride,
                      const SharpnessCtx &sharp, const Options &opts,
                      float *child_values) {
  const uint32_t V = topo.num_points;
  const uint32_t E = topo.num_edges;

  float *vert_child = child_values;
  float *edge_child = child_values + size_t(V) * stride;

  ParallelFor(opts, E, [&](uint32_t e) {
    ComputeLoopEdgeChild(topo, parent_fvi, values, stride, sharp, e,
                         &edge_child[size_t(e) * stride]);
  });

  ParallelFor(opts, V, [&](uint32_t v) {
    ComputeLoopVertexChild(topo, values, stride, sharp, opts, v,
                           &vert_child[size_t(v) * stride]);
  });
}

}  // namespace tsd
}  // namespace tinyusdz
