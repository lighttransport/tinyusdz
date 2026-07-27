// SPDX-License-Identifier: Apache-2.0
// tusdrender -- view-dependent per-instance LOD selection for the two-level
// (TLAS) render path. Native port of examples/tusdview/rt_lod.{hh,cc}: given the
// render camera and each TLAS instance's object-to-world + its prototype's
// object-space AABB, classify the placement as Full (trace the real BLAS), Proxy
// (trace a shared unit-box BLAS via a box-fit transform), or Cull (drop it from
// the TLAS). Pure math in tusdr's own Vec3 type (no light3d), unit-testable.
//
// OFFLINE CAVEAT: unlike the interactive viewer, this feeds a path tracer, where
// off-screen geometry still casts shadows / appears in reflections / contributes
// GI. Frustum culling is therefore OFF by default (opt-in via frustum_cull) and
// every knob is a deliberate speed/approximation trade. Proxy keeps a coarse
// occluder; sub-pixel Cull drops only placements below cull_px.
#pragma once

#include <cstdint>

#include "tusdr_types.hh"  // tusdr::Vec3, CameraFrame, Bounds

namespace tusdr {

enum class RtLod : std::uint8_t { Cull = 0, Proxy = 1, Full = 2 };

// LOD knobs (from Options). Disabled by default -> the render path is untouched.
struct RtLodConfig {
  bool enabled = false;
  bool proxy = true;          // distant prototypes -> shared box; false = Full-or-Cull
  bool frustum_cull = false;  // OFF by default (GI-unsafe); opt-in speed flag
  float full_px = 64.0f;      // >= full_px projected radius -> Full
  float cull_px = 2.0f;       // <  cull_px projected radius -> Cull
};

// Camera snapshot the selector reads, derived once from a CameraFrame + image
// height. Holds the 6 frustum planes (inward normal a,b,c + offset d; a point is
// inside plane i when a*x+b*y+c*z+d >= 0) and the pixel focal length.
struct RtLodView {
  Vec3 eye{0, 0, 0};
  Vec3 forward{0, 0, -1};
  float planes[6][4]{};   // near, far, left, right, bottom, top
  float focal_px = 1000.f; // (height * 0.5) / tan(0.5 * yfov)
  float near_plane = 1e-3f;
  bool ortho = false;
};

// Stats for one selection pass.
struct RtLodStats {
  std::uint32_t full = 0, proxy = 0, culled = 0;
};

// Build the view from the resolved render camera and image height.
RtLodView MakeRtLodView(const CameraFrame& cam, int image_height);

// Projected pixel radius of a world sphere (center c, radius r): the visible
// half-extent given the focal length, using view-space depth (so an off-axis
// instance is not over-promoted by its larger Euclidean distance).
float ProjectedRadiusPx(const Vec3& c, float r, const RtLodView& view);

// Classify one placement. `local` is the prototype's object-space AABB; `o2w` is
// the 3x4 row-major object->world. A degenerate (zero-volume) AABB carries no
// size info and is always kept Full (never culled/proxied).
RtLod ClassifyInstance(const RtLodView& view, const RtLodConfig& cfg,
                       const float o2w[12], const Bounds& local);

// Compose a 3x4 o2w with the box-fit that maps the unit cube [0,1]^3 onto the
// prototype AABB [mn,mx] -> the proxy box BLAS lands exactly on that AABB. Mirrors
// examples/tusdview/lod_math.hh BoxFitXform.
void BoxFitO2W(const float o2w[12], const Vec3& mn, const Vec3& mx,
               float out[12]);

}  // namespace tusdr
