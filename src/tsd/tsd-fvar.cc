// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: faceVarying primvar refinement.
//
// Mode "all" (fully linear) interpolates per-face-corner values within each
// face independently -- no cross-face smoothing, seams stay sharp. The
// remaining five modes refine a seam-split mesh with the scheme kernels
// (M3).

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

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

}  // namespace tsd
}  // namespace tinyusdz
