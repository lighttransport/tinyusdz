// SPDX-License-Identifier: Apache-2.0
#include "gpu_budget_lod.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gpu_scene.hh"
#include "lod_math.hh"  // World3x4, BoxFitXform, UnitCubeCorner, kBoxIndices

namespace lusdview {
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
    b += m.instanceOpacities.size() * sizeof(float);
  }
  b += m.vertexColors.size() * sizeof(float);
  for (const DrawGeomPropCPU& prop : m.geomProps)
    b += prop.values.size() * sizeof(float) + prop.name.capacity();
  return b;
}

// Prototype (object-space) diagonal length -- "how big is this object".
float ProtoDiag(const DrawMeshCPU& m) {
  const float dx = m.protoAabbMax[0] - m.protoAabbMin[0];
  const float dy = m.protoAabbMax[1] - m.protoAabbMin[1];
  const float dz = m.protoAabbMax[2] - m.protoAabbMin[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
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
  // Unit cube [0,1]^3 corners (shared with the RT box proxy via lod_math.hh).
  proxy.vertices.resize(8);
  for (int c = 0; c < 8; ++c) {
    DrawVertex& v = proxy.vertices[c];
    float p[3];
    UnitCubeCorner(c, p);
    v.px = p[0]; v.py = p[1]; v.pz = p[2];
    v.nx = 0.f; v.ny = 1.f; v.nz = 0.f;  // ignored (geometricNormal)
    v.u = 0.f; v.v = 0.f;
  }
  // 12 triangles (6 quad faces). Winding irrelevant (doubleSided).
  proxy.indices.assign(kBoxIndices, kBoxIndices + 36);
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
    // Guide geometry (breadcrumbs / endpoints) spans the whole map and is hidden
    // by default -- never let it drive framing.
    if (m.purpose == "guide") continue;
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

}  // namespace lusdview
