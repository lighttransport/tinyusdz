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
    const float *src = &values[size_t(v) * stride];
    float *dst = &vert_child[size_t(v) * stride];
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = src[c];
    }
  });

  ParallelFor(opts, E, [&](uint32_t e) {
    const float *p0 = &values[size_t(topo.edge_verts[2 * e]) * stride];
    const float *p1 = &values[size_t(topo.edge_verts[2 * e + 1]) * stride];
    float *dst = &edge_child[size_t(e) * stride];
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = 0.5f * (p0[c] + p1[c]);
    }
  });

  ParallelFor(opts, F, [&](uint32_t f) {
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;
    float *dst = &face_child[size_t(f) * stride];
    const float w = 1.0f / float(n);
    for (uint32_t c = 0; c < stride; c++) {
      dst[c] = 0.0f;
    }
    for (uint32_t k = 0; k < n; k++) {
      const float *src = &values[size_t(parent_fvi[begin + k]) * stride];
      for (uint32_t c = 0; c < stride; c++) {
        dst[c] += w * src[c];
      }
    }
  });
}

}  // namespace tsd
}  // namespace tinyusdz
