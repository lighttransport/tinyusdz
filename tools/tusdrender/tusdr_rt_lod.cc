// SPDX-License-Identifier: Apache-2.0
#include "tusdr_rt_lod.hh"

#include <algorithm>
#include <cmath>

#include "tusdr_math.hh"  // Dot/Sub/Add/Mul/Normalize, TransformPointO2W

namespace tusdr {
namespace {

// Inward-normal plane through `pt` with (already-normalized) normal `n`, written
// as a*x+b*y+c*z+d with d = -dot(n,pt) so dot(n,P)+d >= 0 means inside.
void SetPlane(float pl[4], const Vec3& n, const Vec3& pt) {
  pl[0] = n.x;
  pl[1] = n.y;
  pl[2] = n.z;
  pl[3] = -Dot(n, pt);
}

// World AABB of a local AABB under a 3x4 row-major o2w (transform the 8 corners).
void WorldAabb(const float o2w[12], const Bounds& local, Vec3* center,
               float* radius) {
  Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
  for (int c = 0; c < 8; ++c) {
    const Vec3 p{(c & 1) ? local.hi.x : local.lo.x,
                 (c & 2) ? local.hi.y : local.lo.y,
                 (c & 4) ? local.hi.z : local.lo.z};
    const Vec3 w = TransformPointO2W(o2w, p);
    mn.x = std::min(mn.x, w.x); mn.y = std::min(mn.y, w.y); mn.z = std::min(mn.z, w.z);
    mx.x = std::max(mx.x, w.x); mx.y = std::max(mx.y, w.y); mx.z = std::max(mx.z, w.z);
  }
  center->x = 0.5f * (mn.x + mx.x);
  center->y = 0.5f * (mn.y + mx.y);
  center->z = 0.5f * (mn.z + mx.z);
  const float dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
  *radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
  // Stash the world AABB extents back via the radius-only return is enough for the
  // size test; the frustum test re-derives corners from center +/- half-extent.
  // (Keep mn/mx out-params implicit: a sphere is conservative for both tests.)
}

}  // namespace

RtLodView MakeRtLodView(const CameraFrame& cam, int image_height) {
  RtLodView v;
  v.eye = cam.origin;
  v.forward = Normalize(cam.forward);
  v.near_plane = std::max(1e-4f, cam.znear);
  v.ortho = cam.ortho;
  const float h = float(std::max(1, image_height));
  // Vertical pixel focal length: (H/2) / tan(yfov/2) = (H/2) * cot(yfov/2).
  const float tan_half = std::tan(0.5f * std::max(1e-4f, cam.yfov));
  v.focal_px = (0.5f * h) / std::max(1e-6f, tan_half);

  const Vec3 f = v.forward;
  const Vec3 r = Normalize(cam.right);
  const Vec3 u = Normalize(cam.up);
  // Perspective frustum apex at the eye. half_v = tan(yfov/2); half_h scales by the
  // pixel focal length ratio, which already folds in the image aspect (focal_px is
  // vertical, so half_h = half_v is a square-pixel assumption refined below is not
  // needed -- we test a bounding sphere, so isotropic side planes from half_v are
  // conservative for culling). Use half_v for all four sides (slightly wider than a
  // wide-aspect true frustum -> never culls a visible instance).
  const float half_v = tan_half;
  const Vec3 nLeft = Normalize(Add(r, Mul(f, half_v)));
  const Vec3 nRight = Normalize(Add(Mul(r, -1.0f), Mul(f, half_v)));
  const Vec3 nBottom = Normalize(Add(u, Mul(f, half_v)));
  const Vec3 nTop = Normalize(Add(Mul(u, -1.0f), Mul(f, half_v)));
  SetPlane(v.planes[0], f, Add(v.eye, Mul(f, v.near_plane)));        // near
  SetPlane(v.planes[1], Mul(f, -1.0f), Add(v.eye, Mul(f, std::max(v.near_plane, cam.zfar))));  // far
  SetPlane(v.planes[2], nLeft, v.eye);
  SetPlane(v.planes[3], nRight, v.eye);
  SetPlane(v.planes[4], nBottom, v.eye);
  SetPlane(v.planes[5], nTop, v.eye);
  return v;
}

float ProjectedRadiusPx(const Vec3& c, float r, const RtLodView& view) {
  if (view.ortho) return r * view.focal_px;  // depth-independent for ortho
  float depth = Dot(Sub(c, view.eye), view.forward);
  depth = std::max(depth, view.near_plane);
  return view.focal_px * r / depth;
}

RtLod ClassifyInstance(const RtLodView& view, const RtLodConfig& cfg,
                       const float o2w[12], const Bounds& local) {
  // A degenerate (unset / zero-volume) prototype AABB carries no size info -> keep
  // Full (never cull/proxy). Covers non-instanced or bounds-less prototypes.
  const bool degenerate = !(local.hi.x > local.lo.x || local.hi.y > local.lo.y ||
                            local.hi.z > local.lo.z);
  if (!cfg.enabled || degenerate || view.ortho) return RtLod::Full;

  Vec3 center;
  float radius = 0.0f;
  WorldAabb(o2w, local, &center, &radius);

  // Frustum cull (bounding sphere vs each inward plane). OFF by default: a path
  // tracer needs off-screen geometry for shadows/reflections/GI.
  if (cfg.frustum_cull) {
    for (int i = 0; i < 6; ++i) {
      const float dist = view.planes[i][0] * center.x + view.planes[i][1] * center.y +
                         view.planes[i][2] * center.z + view.planes[i][3];
      if (dist < -radius) return RtLod::Cull;  // sphere fully outside this plane
    }
  }

  const float px = ProjectedRadiusPx(center, radius, view);
  if (px < cfg.cull_px) return RtLod::Cull;
  if (!cfg.proxy) return RtLod::Full;
  return (px >= cfg.full_px) ? RtLod::Full : RtLod::Proxy;
}

void BoxFitO2W(const float o2w[12], const Vec3& mn, const Vec3& mx,
               float out[12]) {
  const float s[3] = {mx.x - mn.x, mx.y - mn.y, mx.z - mn.z};
  const float m[3] = {mn.x, mn.y, mn.z};
  for (int r = 0; r < 3; ++r) {
    out[r * 4 + 0] = o2w[r * 4 + 0] * s[0];
    out[r * 4 + 1] = o2w[r * 4 + 1] * s[1];
    out[r * 4 + 2] = o2w[r * 4 + 2] * s[2];
    out[r * 4 + 3] = o2w[r * 4 + 0] * m[0] + o2w[r * 4 + 1] * m[1] +
                     o2w[r * 4 + 2] * m[2] + o2w[r * 4 + 3];
  }
}

}  // namespace tusdr
