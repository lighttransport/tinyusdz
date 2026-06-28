// SPDX-License-Identifier: Apache-2.0
#include "gpu_budget_lod.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gpu_scene.hh"

namespace tusdview {
namespace {

// Full-geometry GPU cost of a mesh in bytes: vertex + index + per-instance
// transform/color buffers (the dominant raster-path allocations).
std::size_t MeshVramBytes(const DrawMeshCPU& m) {
  std::size_t b = m.vertices.size() * sizeof(DrawVertex);
  b += m.indices.size() * sizeof(std::uint32_t);
  const std::size_t ninst = m.instanceCount();
  if (ninst) {
    b += ninst * 12 * sizeof(float);  // instanceXforms (3x4 o2w)
    b += m.instanceColors.size() * sizeof(float);
  }
  b += m.vertexColors.size() * sizeof(float);
  return b;
}

// Prototype (object-space) diagonal length -- "how big is this object".
float ProtoDiag(const DrawMeshCPU& m) {
  const float dx = m.protoAabbMax[0] - m.protoAabbMin[0];
  const float dy = m.protoAabbMax[1] - m.protoAabbMin[1];
  const float dz = m.protoAabbMax[2] - m.protoAabbMin[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// 3x4 row-major object->world from a column-major Mat4 world[16].
void World3x4(const float w[16], float o[12]) {
  for (int r = 0; r < 3; ++r) {
    o[r * 4 + 0] = w[0 * 4 + r];
    o[r * 4 + 1] = w[1 * 4 + r];
    o[r * 4 + 2] = w[2 * 4 + r];
    o[r * 4 + 3] = w[3 * 4 + r];
  }
}

// Compose a 3x4 o2w with the box-fit that maps the unit cube [0,1]^3 onto the
// prototype AABB [mn,mx]: world = o2w * (mn + unit * (mx - mn)).
void BoxFitXform(const float o2w[12], const float mn[3], const float mx[3],
                 float out[12]) {
  const float s[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
  for (int r = 0; r < 3; ++r) {
    out[r * 4 + 0] = o2w[r * 4 + 0] * s[0];
    out[r * 4 + 1] = o2w[r * 4 + 1] * s[1];
    out[r * 4 + 2] = o2w[r * 4 + 2] * s[2];
    out[r * 4 + 3] = o2w[r * 4 + 0] * mn[0] + o2w[r * 4 + 1] * mn[1] +
                     o2w[r * 4 + 2] * mn[2] + o2w[r * 4 + 3];
  }
}

}  // namespace

void ApplyGpuBudgetLOD(DrawScene* draw, std::size_t vramBudgetBytes,
                       std::size_t maxFullMeshes, std::string* report) {
  if (!draw) return;
  const std::size_t n = draw->meshes.size();
  if (n == 0 || (vramBudgetBytes == 0 && maxFullMeshes == 0)) return;

  // Rank by prototype size: biggest objects stay full, the long tail of small
  // meshes becomes the proxy soup.
  std::vector<std::size_t> order(n);
  for (std::size_t i = 0; i < n; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
                   [&](std::size_t a, std::size_t b) {
                     return ProtoDiag(draw->meshes[a]) >
                            ProtoDiag(draw->meshes[b]);
                   });

  // Greedily admit full meshes within the count and VRAM budgets. Always keep
  // at least the single most prominent mesh.
  std::vector<char> keepFull(n, 0);
  std::size_t fullCount = 0, fullBytes = 0;
  for (std::size_t k = 0; k < n; ++k) {
    const std::size_t i = order[k];
    if (maxFullMeshes > 0 && fullCount >= maxFullMeshes) break;
    const std::size_t cost = MeshVramBytes(draw->meshes[i]);
    if (vramBudgetBytes > 0 && fullCount > 0 &&
        fullBytes + cost > vramBudgetBytes) {
      continue;  // skip this one (too big), maybe a later smaller mesh fits
    }
    keepFull[i] = 1;
    ++fullCount;
    fullBytes += cost;
  }

  if (fullCount == n) {
    if (report) *report = "gpu-budget LOD: all meshes fit, no proxy needed";
    return;  // nothing to merge
  }

  // Count overflow instances to reserve the proxy buffers.
  std::size_t proxyInst = 0, mergedMeshes = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (keepFull[i]) continue;
    ++mergedMeshes;
    const std::size_t ninst = draw->meshes[i].instanceCount();
    proxyInst += ninst ? ninst : 1;  // non-instanced mesh = one placement
  }

  // Build the proxy: a unit-cube prototype instanced once per overflow instance,
  // each instance transform placing that original prototype's AABB box.
  DrawMeshCPU proxy;
  proxy.name = "__gpu_budget_proxy__";
  proxy.purpose = "default";
  proxy.geometricNormal = true;  // boxes shade by screen-derivative normal
  proxy.doubleSided = true;      // winding-agnostic
  // 8 corners of [0,1]^3, indexed by (x + 2y + 4z).
  proxy.vertices.resize(8);
  for (int c = 0; c < 8; ++c) {
    DrawVertex& v = proxy.vertices[c];
    v.px = (c & 1) ? 1.f : 0.f;
    v.py = (c & 2) ? 1.f : 0.f;
    v.pz = (c & 4) ? 1.f : 0.f;
    v.nx = 0.f; v.ny = 1.f; v.nz = 0.f;  // ignored (geometricNormal)
    v.u = 0.f; v.v = 0.f;
  }
  // 12 triangles (6 quad faces). Winding irrelevant (doubleSided).
  static const std::uint32_t kBox[36] = {
      0, 1, 3, 0, 3, 2,  // z=0
      4, 5, 7, 4, 7, 6,  // z=1
      0, 1, 5, 0, 5, 4,  // y=0
      2, 3, 7, 2, 7, 6,  // y=1
      0, 2, 6, 0, 6, 4,  // x=0
      1, 3, 7, 1, 7, 5,  // x=1
  };
  proxy.indices.assign(kBox, kBox + 36);
  proxy.submeshes.push_back(DrawSubmesh{0, 36, -1});
  proxy.instanceXforms.reserve(proxyInst * 12);
  proxy.instanceColors.reserve(proxyInst * 3);

  float pmn[3] = {1e30f, 1e30f, 1e30f}, pmx[3] = {-1e30f, -1e30f, -1e30f};
  for (std::size_t i = 0; i < n; ++i) {
    if (keepFull[i]) continue;
    const DrawMeshCPU& m = draw->meshes[i];
    const std::size_t ninst = m.instanceCount();
    const float col[3] = {m.flatColor[0], m.flatColor[1], m.flatColor[2]};
    auto emit = [&](const float o2w[12]) {
      float xf[12];
      BoxFitXform(o2w, m.protoAabbMin, m.protoAabbMax, xf);
      proxy.instanceXforms.insert(proxy.instanceXforms.end(), xf, xf + 12);
      proxy.instanceColors.insert(proxy.instanceColors.end(), col, col + 3);
      // expand proxy world AABB by this box's 8 corners
      for (int c = 0; c < 8; ++c) {
        const float ux = (c & 1) ? 1.f : 0.f, uy = (c & 2) ? 1.f : 0.f,
                    uz = (c & 4) ? 1.f : 0.f;
        for (int r = 0; r < 3; ++r) {
          const float w = xf[r * 4 + 0] * ux + xf[r * 4 + 1] * uy +
                          xf[r * 4 + 2] * uz + xf[r * 4 + 3];
          pmn[r] = std::min(pmn[r], w);
          pmx[r] = std::max(pmx[r], w);
        }
      }
    };
    if (ninst) {
      for (std::size_t j = 0; j < ninst; ++j) emit(&m.instanceXforms[j * 12]);
    } else {
      float o2w[12];
      World3x4(m.world, o2w);
      emit(o2w);
    }
  }
  proxy.flatColor[0] = 0.6f; proxy.flatColor[1] = 0.6f; proxy.flatColor[2] = 0.6f;
  for (int k = 0; k < 3; ++k) {
    proxy.protoAabbMin[k] = 0.f;
    proxy.protoAabbMax[k] = 1.f;
    proxy.aabbMin[k] = pmn[k];
    proxy.aabbMax[k] = pmx[k];
  }
  // Identity world (instanced draw ignores it, but keep it sane).
  for (int k = 0; k < 16; ++k) proxy.world[k] = (k % 5 == 0) ? 1.f : 0.f;

  // Rebuild the mesh list: kept-full meshes (original order) + the proxy.
  std::vector<DrawMeshCPU> kept;
  kept.reserve(fullCount + 1);
  for (std::size_t i = 0; i < n; ++i)
    if (keepFull[i]) kept.push_back(std::move(draw->meshes[i]));
  kept.push_back(std::move(proxy));
  draw->meshes = std::move(kept);

  // Recompute the unique triangle count for logging/stats.
  std::size_t tris = 0;
  for (const DrawMeshCPU& m : draw->meshes) tris += m.indices.size() / 3;
  draw->triangleCount = tris;

  if (report) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "gpu-budget LOD: %zu meshes full (%.2f GiB), merged %zu meshes "
                  "/ %zu instances into bbox proxy (now %zu draws)",
                  fullCount, double(fullBytes) / (1024.0 * 1024.0 * 1024.0),
                  mergedMeshes, proxyInst, draw->meshes.size());
    *report = buf;
  }
}

bool ComputeRobustSceneBounds(const DrawScene* draw, float trimFrac,
                              float outMin[3], float outMax[3],
                              std::string* report) {
  if (!draw) return false;
  const std::size_t n = draw->meshes.size();
  // Too few meshes: a 1% trim is meaningless and risks clipping real geometry.
  if (n < 16) return false;
  if (!(trimFrac > 0.f) || trimFrac >= 0.5f) trimFrac = 0.01f;

  // Per axis, gather every mesh world-AABB endpoint (its min and its max), each
  // carrying the mesh's geometry "mass" = instance count (x tri count). A sparse
  // horizon shell or a few far-flung elements weigh almost nothing, so their
  // extreme endpoints fall in the trimmed tail; the dense island geometry, which
  // dominates the mass, defines the kept range. Mass-weighting (not mesh count)
  // is the key: the island is a small dense cluster inside a huge sparse bbox.
  struct WSample {
    float v;
    double w;
  };
  std::vector<WSample> samp[3];
  for (int a = 0; a < 3; ++a) samp[a].reserve(2 * n);
  float fullMin[3] = {1e30f, 1e30f, 1e30f}, fullMax[3] = {-1e30f, -1e30f, -1e30f};
  double totalW = 0.0;
  std::size_t valid = 0;
  for (const DrawMeshCPU& m : draw->meshes) {
    bool ok = true;
    for (int a = 0; a < 3; ++a) {
      const float mn = m.aabbMin[a], mx = m.aabbMax[a];
      if (!(mx >= mn) || !std::isfinite(mn) || !std::isfinite(mx)) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    ++valid;
    const std::size_t ninst = m.instanceCount();
    const std::size_t tris = m.indices.size() / 3;
    const double w =
        double(ninst ? ninst : 1) * double(tris ? tris : 1);
    totalW += w;
    for (int a = 0; a < 3; ++a) {
      samp[a].push_back({m.aabbMin[a], w});
      samp[a].push_back({m.aabbMax[a], w});
      fullMin[a] = std::min(fullMin[a], m.aabbMin[a]);
      fullMax[a] = std::max(fullMax[a], m.aabbMax[a]);
    }
  }
  if (valid < 16 || !(totalW > 0.0)) return false;

  // Mass-weighted percentiles: walk endpoints in ascending value, accumulating
  // mass; robustMin = value where cumulative mass first exceeds trimFrac of the
  // total (per endpoint, total mass = 2*totalW), robustMax = the symmetric tail.
  const double cut = trimFrac * 2.0 * totalW;
  float rMin[3], rMax[3];
  for (int a = 0; a < 3; ++a) {
    std::sort(samp[a].begin(), samp[a].end(),
              [](const WSample& x, const WSample& y) { return x.v < y.v; });
    double acc = 0.0;
    rMin[a] = samp[a].front().v;
    for (const WSample& s : samp[a]) {
      acc += s.w;
      if (acc >= cut) {
        rMin[a] = s.v;
        break;
      }
    }
    acc = 0.0;
    rMax[a] = samp[a].back().v;
    for (auto it = samp[a].rbegin(); it != samp[a].rend(); ++it) {
      acc += it->w;
      if (acc >= cut) {
        rMax[a] = it->v;
        break;
      }
    }
    if (rMax[a] < rMin[a]) std::swap(rMax[a], rMin[a]);
  }

  auto diag = [](const float mn[3], const float mx[3]) {
    const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  const float fullDiag = diag(fullMin, fullMax);
  const float robustDiag = diag(rMin, rMax);
  // Only override when the mass-dense region is materially tighter than the full
  // bounds (robust bounds < 70% of the full size). Otherwise the geometry fills
  // its bbox and we must not shrink its legitimate framing.
  if (!(fullDiag > 0.f) || robustDiag >= 0.7f * fullDiag) return false;

  for (int a = 0; a < 3; ++a) {
    outMin[a] = rMin[a];
    outMax[a] = rMax[a];
  }
  if (report) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "robust auto-frame: trimmed %.0f%% outliers, scene diag %.1f "
                  "-> %.1f (%.0f%% smaller)",
                  trimFrac * 100.f, fullDiag, robustDiag,
                  100.f * (1.f - robustDiag / fullDiag));
    *report = buf;
  }
  return true;
}

}  // namespace tusdview
