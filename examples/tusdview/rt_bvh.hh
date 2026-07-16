// SPDX-License-Identifier: Apache-2.0
// tusdview — shared CPU BVH builders for the CUDA/HIP screenshot ray tracers.
// `Node` must stay layout-compatible with the `Node` struct in the trace kernel
// (raytracer_kernel.inc): count>0 => leaf (left = first prim), count==0 =>
// interior (left/right = child node indices).
#pragma once

#include <vector>

namespace tusdview {

struct Node {
  float bmin[3];
  float bmax[3];
  int left;
  int right;
  int count;
};

// Median-split BVH over triangle centroids (per-prototype BLAS). Appends nodes to
// `nodes`, reorders `idx` so leaves reference contiguous ranges, returns the root
// node index. `tris` = 9 floats/triangle. Serial (prototypes are small).
int BuildBvh(std::vector<Node>& nodes, std::vector<int>& idx, int first, int count,
             const std::vector<float>& cent, const std::vector<float>& tris);

// Median-split BVH over instance WORLD-AABB centroids (the TLAS). `aabb` = 6
// floats/instance (lo.xyz, hi.xyz); `cent` = 3 floats/instance. Reorders `idx`
// to leaf order and returns the node vector with the root at index 0. Parallelized
// across the top levels (std::async) so it scales to tens of millions of
// instances; the result is identical to a serial median-split build.
std::vector<Node> BuildTlas(std::vector<int>& idx, int count,
                            const std::vector<float>& cent,
                            const std::vector<float>& aabb);

}  // namespace tusdview
