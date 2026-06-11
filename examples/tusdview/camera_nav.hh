// SPDX-License-Identifier: Apache-2.0
// tusdview - orbit camera with Maya-style navigation and fit-to-scene.
#pragma once

#include "light3d/math.h"

namespace tusdview {

enum class CameraViewPreset {
  Isometric,
  Front,
  Back,
  Right,
  Left,
  Top,
  Bottom,
};

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
  void setPreset(CameraViewPreset preset);
  void setOrbitSensitivity(float s) {
    if (s > 0.0f) orbitSensitivity_ = s;
  }
  void setPanSensitivity(float s) {
    if (s > 0.0f) panSensitivity_ = s;
  }
  void setDollySensitivity(float s) {
    if (s > 0.0f) dollySensitivity_ = s;
  }
  void setInvertDolly(bool on) { invertDolly_ = on; }

  // Frame the given world-space AABB.
  void fitToScene(const float aabbMin[3], const float aabbMax[3]);

  // Full-scene radius used to size the near/far planes (so zooming out never
  // clips the scene/grid). Set once per loaded scene from its world AABB.
  void setSceneRadius(float r) { sceneRadius_ = (r > 1e-4f) ? r : 1.0f; }

  const light3d::Vec3& target() const { return target_; }
  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }
  float distance() const { return distance_; }
  float orbitSensitivity() const { return orbitSensitivity_; }
  float panSensitivity() const { return panSensitivity_; }
  float dollySensitivity() const { return dollySensitivity_; }
  bool invertDolly() const { return invertDolly_; }

  // Absolute camera placement (e.g. driven by the MCP viewport tool). Pitch is
  // clamped like orbit(); distance kept positive.
  void setOrbit(const light3d::Vec3& target, float yawRad, float pitchRad,
                float dist);

 private:
  light3d::Vec3 target_{0.0f, 0.0f, 0.0f};
  float yaw_{0.6f};       // radians, azimuth around the up axis
  float pitch_{0.35f};    // radians, elevation, clamped to (-89deg, 89deg)
  float distance_{5.0f};
  int upAxis_{1};         // 1 = +Y, 2 = +Z
  float fovYDeg_{45.0f};
  float aspect_{1.3333f};
  float sceneRadius_{1.0f};  // full-scene radius; drives dynamic near/far
  float orbitSensitivity_{1.0f};
  float panSensitivity_{1.0f};
  float dollySensitivity_{1.0f};
  bool invertDolly_{false};
};

}  // namespace tusdview
