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

enum class CameraProjection { Perspective, Orthographic };
enum class CameraConform { Fit, Crop, Horizontal, Vertical, None };

class OrbitCamera {
 public:
  // View / projection (column-major, light3d::Mat4 layout).
  light3d::Vec3 eye() const;
  light3d::Mat4 view() const;
  light3d::Mat4 proj(bool zeroToOneDepth) const;
  float nearPlane() const;
  float farPlane() const;

  void setAspect(float aspect) { viewportAspect_ = aspect > 1e-4f ? aspect : 1.0f; }
  void setFovYDeg(float deg);
  void setProjection(CameraProjection projection) { projection_ = projection; }
  void setOrthographicHeight(float height);
  // Normalized filmback offsets: 2*apertureOffset/aperture. Zero is centered.
  void setLensShift(float horizontal, float vertical);
  void setExposure(float exposure) { exposure_ = exposure; }
  void setConform(CameraConform conform) { conform_ = conform; }
  void setAspectOverrideEnabled(bool on) { aspectOverrideEnabled_ = on; }
  void setAspectOverride(float aspect);
  void setAutoClip(bool on) { autoClip_ = on; }
  void setClipPlanes(float nearPlane, float farPlane);

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
  // Translate eye and pivot together along the current view direction. Positive
  // values move forward; used by explicit navigation keys as well as dolly.
  void moveForward(float amount);
  // Mouse wheel notches or RMB pixels. This is a true camera dolly: eye and
  // orbit pivot move together, so it never stalls at or turns around an origin.
  void dolly(float amount);
  // Scene-radius-clamped reference distance used to scale pan speed (keeps the
  // feel usable across the zoom range; see camera_nav.cc).
  float moveRefDistance() const;
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
  void setInvertYaw(bool on) { invertYaw_ = on; }
  void setInvertDolly(bool on) { invertDolly_ = on; }

  // Frame the given world-space AABB.
  void fitToScene(const float aabbMin[3], const float aabbMax[3]);

  // World-space bounds used by automatic near/far clipping. Keeping the center
  // separate from the mutable orbit pivot makes clipping stable after pan/dolly.
  void setSceneBounds(const float aabbMin[3], const float aabbMax[3]);

  // Full-scene radius used to size the near/far planes (so zooming out never
  // clips the scene/grid). Set once per loaded scene from its world AABB.
  void setSceneRadius(float r) {
    sceneRadius_ = (r > 1e-4f) ? r : 1.0f;
    haveSceneBounds_ = false;
  }

  const light3d::Vec3& target() const { return target_; }
  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }
  float distance() const { return distance_; }
  float fovYDeg() const { return fovYDeg_; }
  CameraProjection projection() const { return projection_; }
  float orthographicHeight() const { return orthographicHeight_; }
  float lensShiftX() const { return lensShiftX_; }
  float lensShiftY() const { return lensShiftY_; }
  float exposure() const { return exposure_; }
  CameraConform conform() const { return conform_; }
  float aspect() const { return aspectOverrideEnabled_ ? aspectOverride_ : viewportAspect_; }
  bool aspectOverrideEnabled() const { return aspectOverrideEnabled_; }
  float aspectOverride() const { return aspectOverride_; }
  bool autoClip() const { return autoClip_; }
  float manualNearPlane() const { return nearClip_; }
  float manualFarPlane() const { return farClip_; }
  float orbitSensitivity() const { return orbitSensitivity_; }
  float panSensitivity() const { return panSensitivity_; }
  float dollySensitivity() const { return dollySensitivity_; }
  bool invertYaw() const { return invertYaw_; }
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
  float fovYDeg_{60.0f};
  CameraProjection projection_{CameraProjection::Perspective};
  float orthographicHeight_{2.0f};
  float lensShiftX_{0.0f};
  float lensShiftY_{0.0f};
  float exposure_{0.0f};  // authored Camera exposure, in stops
  CameraConform conform_{CameraConform::Fit};
  float viewportAspect_{1.3333f};
  bool aspectOverrideEnabled_{false};
  float aspectOverride_{1.0f};
  float sceneRadius_{1.0f};  // full-scene radius; drives dynamic near/far
  light3d::Vec3 sceneCenter_{0.0f, 0.0f, 0.0f};
  bool haveSceneBounds_{false};
  bool autoClip_{true};
  float nearClip_{0.01f};
  float farClip_{10000.0f};
  float orbitSensitivity_{1.0f};
  float panSensitivity_{1.0f};
  float dollySensitivity_{1.0f};
  bool invertYaw_{true};
  bool invertDolly_{false};
};

}  // namespace tusdview
