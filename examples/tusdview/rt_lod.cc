// SPDX-License-Identifier: Apache-2.0
#include "rt_lod.hh"

#include <algorithm>
#include <cmath>

#include "lod_math.hh"  // World3x4, BoxFitXform

namespace tusdview {
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

}  // namespace

float ProjectedRadiusPx(const float c[3], float r, const RtLodCamera& cam) {
  const float dx = c[0] - cam.eye.x, dy = c[1] - cam.eye.y, dz = c[2] - cam.eye.z;
  float depth = dx * cam.forward.x + dy * cam.forward.y + dz * cam.forward.z;
  depth = std::max(depth, cam.nearPlane);
  return cam.focalPx * r / depth;
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

    for (std::uint32_t k = 0; k < count; ++k) {
      const float* o2w = ninst ? &proto.instanceXforms[k * 12] : singleO2w;

      float center[3], radius, wmn[3], wmx[3];
      ProtoWorldBounds(o2w, mn, mx, center, &radius, wmn, wmx);

      RtLod level = RtLod::Full;
      if (cam.lodEnabled && !degenerate) {
        if (cam.frustumCull &&
            frustum.testAABB({wmn[0], wmn[1], wmn[2]}, {wmx[0], wmx[1], wmx[2]}) ==
                light3d::CullResult::Outside) {
          ++stats.culled;
          continue;
        }
        const float px = ProjectedRadiusPx(center, radius, cam);
        if (px < cam.cullPx) {
          ++stats.culled;
          continue;
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
    }
  }
  return stats;
}

}  // namespace tusdview
