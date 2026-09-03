// SPDX-License-Identifier: Apache-2.0
// lusdview — shared CPU BVH builders (see rt_bvh.hh).
#include "rt_bvh.hh"

#include <algorithm>
#include <future>

namespace lusdview {

int BuildBvh(std::vector<Node>& nodes, std::vector<int>& idx, int first, int count,
             const std::vector<float>& cent, const std::vector<float>& tris) {
  int self = static_cast<int>(nodes.size());
  nodes.push_back({});
  Node nd{};
  float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i) {
    const float* tv = &tris[idx[first + i] * 9];
    for (int v = 0; v < 3; ++v)
      for (int k = 0; k < 3; ++k) {
        bmin[k] = std::min(bmin[k], tv[v * 3 + k]);
        bmax[k] = std::max(bmax[k], tv[v * 3 + k]);
      }
  }
  for (int k = 0; k < 3; ++k) { nd.bmin[k] = bmin[k]; nd.bmax[k] = bmax[k]; }
  if (count <= 4) {
    nd.left = first; nd.right = 0; nd.count = count;
    nodes[self] = nd;
    return self;
  }
  float cmin[3] = {1e30f, 1e30f, 1e30f}, cmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i)
    for (int k = 0; k < 3; ++k) {
      float c = cent[idx[first + i] * 3 + k];
      cmin[k] = std::min(cmin[k], c);
      cmax[k] = std::max(cmax[k], c);
    }
  int axis = 0;
  if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis]) axis = 1;
  if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis]) axis = 2;
  int mid = first + count / 2;
  std::nth_element(idx.begin() + first, idx.begin() + mid, idx.begin() + first + count,
                   [&](int a, int b) { return cent[a * 3 + axis] < cent[b * 3 + axis]; });
  int left = BuildBvh(nodes, idx, first, count / 2, cent, tris);
  int right = BuildBvh(nodes, idx, mid, count - count / 2, cent, tris);
  nd.left = left; nd.right = right; nd.count = 0;
  nodes[self] = nd;
  return self;
}

namespace {

// Serial in-place TLAS builder over instance world-AABB centroids; appends to
// `nodes`, returns the root index. (The original median-split build.)
int BuildTlasSerial(std::vector<Node>& nodes, std::vector<int>& idx, int first,
                    int count, const std::vector<float>& cent,
                    const std::vector<float>& aabb) {
  int self = static_cast<int>(nodes.size());
  nodes.push_back({});
  Node nd{};
  float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i) {
    const float* a = &aabb[idx[first + i] * 6];
    for (int k = 0; k < 3; ++k) {
      bmin[k] = std::min(bmin[k], a[k]);
      bmax[k] = std::max(bmax[k], a[3 + k]);
    }
  }
  for (int k = 0; k < 3; ++k) { nd.bmin[k] = bmin[k]; nd.bmax[k] = bmax[k]; }
  if (count <= 4) {
    nd.left = first; nd.right = 0; nd.count = count;
    nodes[self] = nd;
    return self;
  }
  float cmin[3] = {1e30f, 1e30f, 1e30f}, cmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i)
    for (int k = 0; k < 3; ++k) {
      float c = cent[idx[first + i] * 3 + k];
      cmin[k] = std::min(cmin[k], c);
      cmax[k] = std::max(cmax[k], c);
    }
  int axis = 0;
  if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis]) axis = 1;
  if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis]) axis = 2;
  int mid = first + count / 2;
  std::nth_element(idx.begin() + first, idx.begin() + mid, idx.begin() + first + count,
                   [&](int a, int b) { return cent[a * 3 + axis] < cent[b * 3 + axis]; });
  int left = BuildTlasSerial(nodes, idx, first, count / 2, cent, aabb);
  int right = BuildTlasSerial(nodes, idx, mid, count - count / 2, cent, aabb);
  nd.left = left; nd.right = right; nd.count = 0;
  nodes[self] = nd;
  return self;
}

// Compute an interior node's bbox (over a leaf-order range) without building.
Node RangeNode(const std::vector<int>& idx, int first, int count,
               const std::vector<float>& aabb) {
  Node nd{};
  float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i) {
    const float* a = &aabb[idx[first + i] * 6];
    for (int k = 0; k < 3; ++k) {
      bmin[k] = std::min(bmin[k], a[k]);
      bmax[k] = std::max(bmax[k], a[3 + k]);
    }
  }
  for (int k = 0; k < 3; ++k) { nd.bmin[k] = bmin[k]; nd.bmax[k] = bmax[k]; }
  return nd;
}

constexpr int kMaxParDepth = 4;          // up to ~2^4 subtree threads
constexpr int kParThreshold = 200000;    // only parallelize big subtrees

// Build the subtree for idx[first, first+count) into a fresh node vector with the
// root at index 0; interior child refs are local to the returned vector, leaves
// keep their absolute `first`. The top `kMaxParDepth` levels split the work
// across std::async threads; below the threshold it falls back to the in-place
// serial builder (so no per-level node copying for the bulk of the tree).
std::vector<Node> BuildTlasPar(std::vector<int>& idx, int first, int count,
                               const std::vector<float>& cent,
                               const std::vector<float>& aabb, int depth) {
  if (depth >= kMaxParDepth || count < kParThreshold || count <= 4) {
    std::vector<Node> nodes;
    nodes.reserve(static_cast<size_t>(count) * 2 + 1);
    BuildTlasSerial(nodes, idx, first, count, cent, aabb);
    return nodes;
  }
  Node nd = RangeNode(idx, first, count, aabb);
  float cmin[3] = {1e30f, 1e30f, 1e30f}, cmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i)
    for (int k = 0; k < 3; ++k) {
      float c = cent[idx[first + i] * 3 + k];
      cmin[k] = std::min(cmin[k], c);
      cmax[k] = std::max(cmax[k], c);
    }
  int axis = 0;
  if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis]) axis = 1;
  if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis]) axis = 2;
  int mid = first + count / 2;
  std::nth_element(idx.begin() + first, idx.begin() + mid, idx.begin() + first + count,
                   [&](int a, int b) { return cent[a * 3 + axis] < cent[b * 3 + axis]; });

  // Left and right operate on disjoint idx subranges, so they run concurrently.
  auto fut = std::async(std::launch::async, [&idx, &cent, &aabb, first, count, depth] {
    return BuildTlasPar(idx, first, count / 2, cent, aabb, depth + 1);
  });
  std::vector<Node> R = BuildTlasPar(idx, mid, count - count / 2, cent, aabb, depth + 1);
  std::vector<Node> L = fut.get();

  std::vector<Node> out;
  out.reserve(1 + L.size() + R.size());
  const int loff = 1;
  const int roff = 1 + static_cast<int>(L.size());
  nd.left = loff;
  nd.right = roff;
  nd.count = 0;
  out.push_back(nd);
  for (Node n : L) {
    if (n.count == 0) { n.left += loff; n.right += loff; }
    out.push_back(n);
  }
  for (Node n : R) {
    if (n.count == 0) { n.left += roff; n.right += roff; }
    out.push_back(n);
  }
  return out;
}

}  // namespace

std::vector<Node> BuildTlas(std::vector<int>& idx, int count,
                            const std::vector<float>& cent,
                            const std::vector<float>& aabb) {
  if (count <= 0) return {};
  return BuildTlasPar(idx, 0, count, cent, aabb, 0);
}

}  // namespace lusdview
