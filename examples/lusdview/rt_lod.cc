// SPDX-License-Identifier: Apache-2.0
#include "rt_lod.hh"

#include <algorithm>
#include <cmath>

#include "lod_math.hh"  // World3x4, BoxFitXform

namespace lusdview {
namespace {

// PCG-ish integer hash, mirrors raytrace.comp:hashU so the CPU LOD dither and any
// future GPU dither agree.
inline std::uint32_t HashU(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// World AABB of a prototype AABB [mn,mx] under a 3x4 row-major o2w: transform the
// 8 corners. Returns center + bounding-sphere radius and the world min/max.
void ProtoWorldBounds(const float o2w[12], const float mn[3], const float mx[3],
                      float center[3], float* radius, float wmn[3], float wmx[3]) {
  for (int r = 0; r < 3; ++r) {
    wmn[r] = 1e30f;
    wmx[r] = -1e30f;
  }
  for (int c = 0; c < 8; ++c) {
    const float p[3] = {(c & 1) ? mx[0] : mn[0], (c & 2) ? mx[1] : mn[1],
                        (c & 4) ? mx[2] : mn[2]};
    for (int r = 0; r < 3; ++r) {
      const float w = o2w[r * 4 + 0] * p[0] + o2w[r * 4 + 1] * p[1] +
                      o2w[r * 4 + 2] * p[2] + o2w[r * 4 + 3];
      wmn[r] = std::min(wmn[r], w);
      wmx[r] = std::max(wmx[r], w);
    }
  }
  for (int r = 0; r < 3; ++r) center[r] = 0.5f * (wmn[r] + wmx[r]);
  const float dx = wmx[0] - wmn[0], dy = wmx[1] - wmn[1], dz = wmx[2] - wmn[2];
  *radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
}

// World 3x4 o2w of placement k (instanced or the single non-instanced matrix).
const float* PlacementXform(const RtLodProto& proto, std::uint32_t k,
                            const float singleO2w[12]) {
  return proto.instanceCount ? &proto.instanceXforms[k * 12] : singleO2w;
}

}  // namespace

float ProjectedRadiusPx(const float c[3], float r, const RtLodCamera& cam) {
  const float dx = c[0] - cam.eye.x, dy = c[1] - cam.eye.y, dz = c[2] - cam.eye.z;
  float depth = dx * cam.forward.x + dy * cam.forward.y + dz * cam.forward.z;
  depth = std::max(depth, cam.nearPlane);
  return cam.focalPx * r / depth;
}

bool IsSubpixelAggregateCell(const RtLodGridCell& cell,
                             const RtLodCamera& cam) {
  if (cell.maxInstanceRadius <= 0.0f) return false;
  float nearDepth = 1e30f;
  for (int ci = 0; ci < 8; ++ci) {
    const float corner[3] = {
        (ci & 1) ? cell.wmx[0] : cell.wmn[0],
        (ci & 2) ? cell.wmx[1] : cell.wmn[1],
        (ci & 4) ? cell.wmx[2] : cell.wmn[2]};
    const float depth = (corner[0] - cam.eye.x) * cam.forward.x +
                        (corner[1] - cam.eye.y) * cam.forward.y +
                        (corner[2] - cam.eye.z) * cam.forward.z;
    nearDepth = std::min(nearDepth, depth);
  }
  const float maxInstancePx = cam.focalPx * cell.maxInstanceRadius /
                              std::max(nearDepth, cam.nearPlane);
  const float center[3] = {0.5f * (cell.wmn[0] + cell.wmx[0]),
                           0.5f * (cell.wmn[1] + cell.wmx[1]),
                           0.5f * (cell.wmn[2] + cell.wmx[2])};
  const float dx = cell.wmx[0] - cell.wmn[0];
  const float dy = cell.wmx[1] - cell.wmn[1];
  const float dz = cell.wmx[2] - cell.wmn[2];
  const float cellRadius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
  const float cellPx = ProjectedRadiusPx(center, cellRadius, cam);
  // A population proxy is a density hint, not a replacement for a visibly
  // large district. Cap it at eight pixels to avoid foreground megaboxes even
  // when the ordinary per-instance proxy threshold is intentionally generous.
  const float aggregatePx = std::max(cam.cullPx,
                                     std::min(cam.fullPx, 8.0f));
  return maxInstancePx < cam.cullPx && cellPx < aggregatePx;
}

void BuildRtLodGrid(const RtLodProto& proto, std::uint32_t minInstances,
                    RtLodGrid* grid) {
  grid->order.clear();
  grid->cells.clear();
  grid->valid = false;
  const std::uint32_t n = proto.instanceCount;
  if (n < minInstances || n < 2 || !proto.instanceXforms || !proto.protoAabbMin ||
      !proto.protoAabbMax)
    return;
  const float* mn = proto.protoAabbMin;
  const float* mx = proto.protoAabbMax;
  if (!(mx[0] > mn[0] || mx[1] > mn[1] || mx[2] > mn[2])) return;  // degenerate

  // Per-instance world AABBs + overall center bounds. The world AABB is
  // computed once per instance and reused: the counting-sort cellOf() derives
  // centers from it, and the per-cell min/max/radius accumulation reads it back,
  // so the cell pass never re-transforms the proto corners (one O(n) transform
  // pass instead of two on huge scenes). Center is an exact affine image of the
  // box center, so 0.5*(wmn+wmx) reproduces the pass-1 center bit-for-bit.
  std::vector<float> wmn(n * 3), wmx(n * 3);
  float gmn[3] = {1e30f, 1e30f, 1e30f}, gmx[3] = {-1e30f, -1e30f, -1e30f};
  for (std::uint32_t k = 0; k < n; ++k) {
    float center[3], radius, mn3[3], mx3[3];
    ProtoWorldBounds(&proto.instanceXforms[k * 12], mn, mx, center, &radius, mn3, mx3);
    for (int r = 0; r < 3; ++r) {
      wmn[k * 3 + r] = mn3[r];
      wmx[k * 3 + r] = mx3[r];
      gmn[r] = std::min(gmn[r], center[r]);
      gmx[r] = std::max(gmx[r], center[r]);
    }
  }
  const float ext[3] = {gmx[0] - gmn[0], gmx[1] - gmn[1], gmx[2] - gmn[2]};
  const float maxext = std::max({ext[0], ext[1], ext[2]});
  if (maxext <= 0.0f) return;  // all centers coincide

  // Aim for ~256 instances/cell. Size cells using only the *populated* axes (an
  // axis whose extent is negligible vs the largest gets one cell), so a flat layout
  // (e.g. an ocean plane: ~zero Y extent) does not blow the cell count up. cellSize
  // is the geometric mean cell edge over the D effective dimensions.
  const float flatEps = 1e-4f * maxext;
  float prodExt = 1.0f;
  int effDim = 0;
  for (int r = 0; r < 3; ++r)
    if (ext[r] > flatEps) { prodExt *= ext[r]; ++effDim; }
  const float target = std::max(1.0f, float(n) / 256.0f);
  const float cellSize =
      std::pow(prodExt / target, 1.0f / float(std::max(effDim, 1)));
  int dim[3];
  for (int r = 0; r < 3; ++r) {
    int d = (ext[r] > flatEps && cellSize > 0.0f)
                ? int(std::ceil(ext[r] / cellSize)) : 1;
    dim[r] = std::min(std::max(d, 1), 256);
  }
  if (dim[0] == 1 && dim[1] == 1 && dim[2] == 1) return;  // no benefit
  // inv maps a center coord into [0,dim) along each axis (flat axes -> bin 0).
  const std::uint32_t ncell =
      std::uint32_t(dim[0]) * std::uint32_t(dim[1]) * std::uint32_t(dim[2]);
  const float inv[3] = {ext[0] > flatEps ? static_cast<float>(dim[0]) / ext[0] : 0.0f,
                        ext[1] > flatEps ? static_cast<float>(dim[1]) / ext[1] : 0.0f,
                        ext[2] > flatEps ? static_cast<float>(dim[2]) / ext[2] : 0.0f};

  auto cellOf = [&](std::uint32_t k) -> std::uint32_t {
    const float cx = 0.5f * (wmn[k * 3] + wmx[k * 3]);
    const float cy = 0.5f * (wmn[k * 3 + 1] + wmx[k * 3 + 1]);
    const float cz = 0.5f * (wmn[k * 3 + 2] + wmx[k * 3 + 2]);
    int ix = std::min(int((cx - gmn[0]) * inv[0]), dim[0] - 1);
    int iy = std::min(int((cy - gmn[1]) * inv[1]), dim[1] - 1);
    int iz = std::min(int((cz - gmn[2]) * inv[2]), dim[2] - 1);
    ix = std::max(ix, 0); iy = std::max(iy, 0); iz = std::max(iz, 0);
    return std::uint32_t((iz * dim[1] + iy) * dim[0] + ix);
  };

  // Counting sort instances into cells (one O(n) pass each).
  std::vector<std::uint32_t> head(ncell + 1, 0);
  for (std::uint32_t k = 0; k < n; ++k) ++head[cellOf(k) + 1];
  for (std::uint32_t c = 0; c < ncell; ++c) head[c + 1] += head[c];
  grid->order.resize(n);
  std::vector<std::uint32_t> cursor(head.begin(), head.end() - 1);
  for (std::uint32_t k = 0; k < n; ++k) grid->order[cursor[cellOf(k)]++] = k;

  // Emit non-empty cells with their combined world AABB.
  grid->cells.reserve(64);
  for (std::uint32_t c = 0; c < ncell; ++c) {
    const std::uint32_t b = head[c], e = head[c + 1];
    if (e == b) continue;
    RtLodGridCell cell;
    cell.begin = b;
    cell.count = e - b;
    cell.maxInstanceRadius = 0.0f;
    for (int r = 0; r < 3; ++r) { cell.wmn[r] = 1e30f; cell.wmx[r] = -1e30f; }
    for (std::uint32_t i = b; i < e; ++i) {
      const std::uint32_t k = grid->order[i];
      const float dx = wmx[k * 3] - wmn[k * 3];
      const float dy = wmx[k * 3 + 1] - wmn[k * 3 + 1];
      const float dz = wmx[k * 3 + 2] - wmn[k * 3 + 2];
      const float radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
      cell.maxInstanceRadius = std::max(cell.maxInstanceRadius, radius);
      for (int r = 0; r < 3; ++r) {
        cell.wmn[r] = std::min(cell.wmn[r], wmn[k * 3 + r]);
        cell.wmx[r] = std::max(cell.wmx[r], wmx[k * 3 + r]);
      }
    }
    grid->cells.push_back(cell);
  }
  grid->valid = !grid->cells.empty();
}

RtLodStats SelectInstanceLOD(const RtLodProto* protos, std::uint32_t protoCount,
                             const RtLodCamera& cam, std::uint32_t boxMeshId,
                             std::vector<RtLodInstance>* out) {
  RtLodStats stats;
  light3d::Frustum frustum = light3d::Frustum::fromViewProjection(cam.viewProj);
  const float fullLo = cam.fullPx * (1.0f - cam.bandFrac);
  const float fullHi = cam.fullPx * (1.0f + cam.bandFrac);
  const float bandSpan = std::max(1e-3f, fullHi - fullLo);

  for (std::uint32_t p = 0; p < protoCount; ++p) {
    const RtLodProto& proto = protos[p];
    if (!proto.protoAabbMin || !proto.protoAabbMax) continue;
    const float* mn = proto.protoAabbMin;
    const float* mx = proto.protoAabbMax;
    // A degenerate (unset / zero-volume) prototype AABB carries no size info, so
    // it cannot be projected -> always keep such instances Full (never cull). This
    // covers non-instanced meshes, whose protoAabb the loader does not compute.
    const bool degenerate =
        !(mx[0] > mn[0] || mx[1] > mn[1] || mx[2] > mn[2]);

    const std::uint32_t ninst = proto.instanceCount;
    const std::uint32_t count = ninst ? ninst : 1u;
    float singleO2w[12];
    if (!ninst) {
      // Non-instanced: derive the 3x4 o2w from the column-major world matrix.
      if (proto.world)
        World3x4(proto.world, singleO2w);
      else
        for (int k = 0; k < 12; ++k) singleO2w[k] = (k % 5 == 0) ? 1.f : 0.f;
    }

    // Classify + emit one placement k. Identical decision regardless of whether k
    // was reached by the flat loop or via a grid cell -> grid acceleration is a
    // pure early-out, the emitted set is order-independent.
    auto classify = [&](std::uint32_t k) {
      const float* o2w = PlacementXform(proto, k, singleO2w);

      float center[3], radius, wmn[3], wmx[3];
      ProtoWorldBounds(o2w, mn, mx, center, &radius, wmn, wmx);

      RtLod level = RtLod::Full;
      if (cam.lodEnabled && !degenerate) {
        if (cam.frustumCull &&
            frustum.testAABB({wmn[0], wmn[1], wmn[2]}, {wmx[0], wmx[1], wmx[2]}) ==
                light3d::CullResult::Outside) {
          ++stats.culled;
          return;
        }
        const float px = ProjectedRadiusPx(center, radius, cam);
        if (px < cam.cullPx) {
          ++stats.culled;
          return;
        }
        if (!cam.proxyEnabled) {
          level = RtLod::Full;  // P1: cull-only, distant-but-visible stays Full
        } else if (px >= fullHi) {
          level = RtLod::Full;
        } else if (px <= fullLo) {
          level = RtLod::Proxy;
        } else {
          // Stochastic crossfade band: t in [0,1] from all-Proxy to all-Full.
          const float t = (px - fullLo) / bandSpan;
          const std::uint32_t h =
              HashU(proto.meshId * 0x9e3779b9u ^ (k + 1u) ^ cam.ditherSeed);
          const float d = float(h >> 8) * (1.0f / 16777216.0f);  // [0,1)
          level = (d < t) ? RtLod::Full : RtLod::Proxy;
        }
      }

      RtLodInstance inst;
      const float* col = (proto.instanceColors && ninst)
                             ? &proto.instanceColors[k * 3]
                             : proto.flatColor;
      inst.tint[0] = col ? col[0] : 0.6f;
      inst.tint[1] = col ? col[1] : 0.6f;
      inst.tint[2] = col ? col[2] : 0.6f;
      inst.opacity = proto.instanceOpacities ? proto.instanceOpacities[k]
                                             : proto.flatOpacity;
      inst.opacity = std::max(0.0f, std::min(1.0f, inst.opacity));
      inst.sourceMeshId = proto.meshId;
      if (level == RtLod::Proxy) {
        BoxFitXform(o2w, mn, mx, inst.xform);
        inst.meshId = boxMeshId;
        inst.level = RtLod::Proxy;
        inst.instanced = true;  // proxy uses the tint path (useMaterial=0)
        ++stats.proxy;
      } else {
        for (int j = 0; j < 12; ++j) inst.xform[j] = o2w[j];
        inst.meshId = proto.meshId;
        inst.level = RtLod::Full;
        inst.instanced = (ninst != 0);
        ++stats.full;
      }
      out->push_back(inst);
    };

    // Coarse grid path (P5): reject whole cells before any per-instance work. A
    // cell's AABB contains every instance AABB inside it, and its near-depth +
    // bounding-sphere radius over-estimate every contained instance's projected
    // radius, so both rejections are conservative -> identical kept set, but the
    // off-screen / sub-pixel tail never iterates. Only when LOD is actually active.
    if (cam.lodEnabled && !degenerate && ninst && proto.grid && proto.grid->valid) {
      const RtLodGrid& g = *proto.grid;
      for (const RtLodGridCell& cell : g.cells) {
        if (cam.frustumCull &&
            frustum.testAABB({cell.wmn[0], cell.wmn[1], cell.wmn[2]},
                             {cell.wmx[0], cell.wmx[1], cell.wmx[2]}) ==
                light3d::CullResult::Outside) {
          stats.culled += cell.count;
          continue;
        }
        // Conservative projected radius: nearest cell depth, full-cell radius.
        float crad, nearDepth = 1e30f;
        const float cdx = cell.wmx[0] - cell.wmn[0], cdy = cell.wmx[1] - cell.wmn[1],
                    cdz = cell.wmx[2] - cell.wmn[2];
        crad = 0.5f * std::sqrt(cdx * cdx + cdy * cdy + cdz * cdz);
        for (int ci = 0; ci < 8; ++ci) {
          const float corner[3] = {(ci & 1) ? cell.wmx[0] : cell.wmn[0],
                              (ci & 2) ? cell.wmx[1] : cell.wmn[1],
                              (ci & 4) ? cell.wmx[2] : cell.wmn[2]};
          const float depth = (corner[0] - cam.eye.x) * cam.forward.x +
                              (corner[1] - cam.eye.y) * cam.forward.y +
                              (corner[2] - cam.eye.z) * cam.forward.z;
          nearDepth = std::min(nearDepth, depth);
        }
        const float cellPx =
            cam.focalPx * crad / std::max(nearDepth, cam.nearPlane);
        if (cellPx < cam.cullPx) {
          stats.culled += cell.count;
          continue;
        }
        for (std::uint32_t i = cell.begin; i < cell.begin + cell.count; ++i)
          classify(g.order[i]);
      }
    } else {
      for (std::uint32_t k = 0; k < count; ++k) classify(k);
    }
  }
  return stats;
}

}  // namespace lusdview
