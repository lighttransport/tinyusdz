// SPDX-License-Identifier: Apache-2.0
// lusdview - shared bbox-proxy LOD math: the unit-cube box geometry and the
// box-fit transform that maps that unit cube onto a prototype's object-space
// AABB. Used by the raster GPU-budget proxy (gpu_budget_lod.cc) and the Vulkan
// ray-tracing view-dependent proxy (rt_lod / vk_renderer buildBoxBlas), so the
// two paths share one definition and can never drift.
#pragma once

#include <cstdint>

namespace lusdview {

// 8 corners of the unit cube [0,1]^3, indexed by (x + 2y + 4z). Fill an 8-entry
// position array (3 floats each) in this order.
inline void UnitCubeCorner(int c, float out[3]) {
  out[0] = (c & 1) ? 1.f : 0.f;
  out[1] = (c & 2) ? 1.f : 0.f;
  out[2] = (c & 4) ? 1.f : 0.f;
}

// 12 triangles (6 quad faces) over the 8 UnitCubeCorner() vertices. Winding is
// irrelevant for the proxy (it is rendered double-sided / geometric-normal).
inline constexpr std::uint32_t kBoxIndices[36] = {
    0, 1, 3, 0, 3, 2,  // z=0
    4, 5, 7, 4, 7, 6,  // z=1
    0, 1, 5, 0, 5, 4,  // y=0
    2, 3, 7, 2, 7, 6,  // y=1
    0, 2, 6, 0, 6, 4,  // x=0
    1, 3, 7, 1, 7, 5,  // x=1
};

// 3x4 row-major object->world from a column-major Mat4 world[16].
inline void World3x4(const float w[16], float o[12]) {
  for (int r = 0; r < 3; ++r) {
    o[r * 4 + 0] = w[0 * 4 + r];
    o[r * 4 + 1] = w[1 * 4 + r];
    o[r * 4 + 2] = w[2 * 4 + r];
    o[r * 4 + 3] = w[3 * 4 + r];
  }
}

// Compose a 3x4 o2w with the box-fit that maps the unit cube [0,1]^3 onto the
// prototype AABB [mn,mx]: world = o2w * (mn + unit * (mx - mn)). The result places
// a unit-cube prototype exactly on that AABB at the instance's world transform.
inline void BoxFitXform(const float o2w[12], const float mn[3], const float mx[3],
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

}  // namespace lusdview
