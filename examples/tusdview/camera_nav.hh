// SPDX-License-Identifier: Apache-2.0
// tusdview - orbit camera with Maya-style navigation and fit-to-scene.
#pragma once

#include "light3d/math.h"

namespace tusdview {

class OrbitCamera {
 public:
  // View / projection (column-major, light3d::Mat4 layout).
  light3d::Vec3 eye() const;
  light3d::Mat4 view() const;
  light3d::Mat4 proj(bool zeroToOneDepth) const;

  void setAspect(float aspect) { aspect_ = aspect > 1e-4f ? aspect : 1.0f; }

  // World up axis: 1 = +Y (default), 2 = +Z. Used so Z-up USD scenes display
  // upright and navigation/grid orient correctly.
  void setUpAxis(int axis) { upAxis_ = (axis == 2) ? 2 : 1; }
  int upAxis() const { return upAxis_; }
  light3d::Vec3 worldUp() const {
    return upAxis_ == 2 ? light3d::Vec3{0, 0, 1} : light3d::Vec3{0, 1, 0};
  }

  // Pixel-delta driven navigation (call only when the viewport is interactive).
  void orbit(float dxPix, float dyPix);
  void pan(float dxPix, float dyPix);
  void dolly(float amount);  // mouse wheel notches or RMB horizontal pixels

  // Frame the given world-space AABB.
  void fitToScene(const float aabbMin[3], const float aabbMax[3]);

  const light3d::Vec3& target() const { return target_; }

 private:
  light3d::Vec3 target_{0.0f, 0.0f, 0.0f};
  float yaw_{0.6f};       // radians, azimuth around the up axis
  float pitch_{0.35f};    // radians, elevation, clamped to (-89deg, 89deg)
  float distance_{5.0f};
  int upAxis_{1};         // 1 = +Y, 2 = +Z
  float fovYDeg_{45.0f};
  float aspect_{1.3333f};
  float znear_{0.05f};
  float zfar_{1000.0f};
};

}  // namespace tusdview
