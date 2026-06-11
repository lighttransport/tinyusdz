// SPDX-License-Identifier: Apache-2.0
#include "camera_nav.hh"

#include <algorithm>
#include <cmath>

namespace tusdview {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kPitchLimit = 89.0f * kDeg2Rad;

light3d::Vec3 DirFromAngles(float yaw, float pitch, int upAxis) {
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float sy = std::sin(yaw);
  const float cy = std::cos(yaw);
  // Azimuth rotates in the ground plane; elevation lifts toward the up axis.
  if (upAxis == 2) {  // +Z up: ground = XY plane
    return light3d::Vec3{sy * cp, cy * cp, sp};
  }
  return light3d::Vec3{sy * cp, sp, cy * cp};  // +Y up: ground = XZ plane
}
}  // namespace

light3d::Vec3 OrbitCamera::eye() const {
  return target_ + DirFromAngles(yaw_, pitch_, upAxis_) * distance_;
}

light3d::Mat4 OrbitCamera::view() const {
  return light3d::lookAt(eye(), target_, worldUp());
}

light3d::Mat4 OrbitCamera::proj(bool zeroToOneDepth) const {
  const float fovY = fovYDeg_ * kDeg2Rad;
  // Near/far adapt to the live orbit distance and the scene radius: far grows
  // with distance (+ scene margin) so zooming out never clips the scene/grid,
  // and near scales with distance so close geometry stays visible while keeping
  // a usable depth ratio.
  const float r = sceneRadius_ > 1e-4f ? sceneRadius_ : 1.0f;
  const float znear = std::max(distance_ * 0.01f, 1e-4f);
  const float zfar = std::max(distance_ + r * 3.0f, distance_ * 2.0f);
  if (zeroToOneDepth) {
    return light3d::perspectiveZeroOne(fovY, aspect_, znear, zfar);
  }
  return light3d::perspective(fovY, aspect_, znear, zfar);
}

void OrbitCamera::orbit(float dxPix, float dyPix) {
  const float k = 0.008f * orbitSensitivity_;
  yaw_ -= dxPix * k;
  pitch_ += dyPix * k;
  pitch_ = std::max(-kPitchLimit, std::min(kPitchLimit, pitch_));
}

void OrbitCamera::pan(float dxPix, float dyPix) {
  // Camera basis (relative to the world up axis).
  light3d::Vec3 fwd = light3d::normalize(target_ - eye());
  light3d::Vec3 right = light3d::normalize(light3d::cross(fwd, worldUp()));
  light3d::Vec3 up = light3d::cross(right, fwd);
  const float scale = distance_ * 0.0015f * panSensitivity_;
  target_ = target_ - right * (dxPix * scale) + up * (dyPix * scale);
}

void OrbitCamera::dolly(float amount) {
  // Exponential zoom keeps the feel consistent at any distance.
  const float signedAmount = invertDolly_ ? -amount : amount;
  distance_ *= std::exp(-signedAmount * 0.12f * dollySensitivity_);
  distance_ = std::max(distance_, 1e-3f);
}

void OrbitCamera::setPreset(CameraViewPreset preset) {
  switch (preset) {
    case CameraViewPreset::Isometric:
      yaw_ = 0.6f;
      pitch_ = 0.35f;
      break;
    case CameraViewPreset::Front:
      yaw_ = 0.0f;
      pitch_ = 0.0f;
      break;
    case CameraViewPreset::Back:
      yaw_ = kPi;
      pitch_ = 0.0f;
      break;
    case CameraViewPreset::Right:
      yaw_ = 0.5f * kPi;
      pitch_ = 0.0f;
      break;
    case CameraViewPreset::Left:
      yaw_ = -0.5f * kPi;
      pitch_ = 0.0f;
      break;
    case CameraViewPreset::Top:
      yaw_ = 0.0f;
      pitch_ = kPitchLimit;
      break;
    case CameraViewPreset::Bottom:
      yaw_ = 0.0f;
      pitch_ = -kPitchLimit;
      break;
  }
}

void OrbitCamera::fitToScene(const float aabbMin[3], const float aabbMax[3]) {
  light3d::Vec3 mn{aabbMin[0], aabbMin[1], aabbMin[2]};
  light3d::Vec3 mx{aabbMax[0], aabbMax[1], aabbMax[2]};
  light3d::Vec3 center = (mn + mx) * 0.5f;
  float radius = 0.5f * light3d::length(mx - mn);
  if (!(radius > 1e-4f)) radius = 1.0f;  // also catches NaN

  target_ = center;
  const float halfV = 0.5f * fovYDeg_ * kDeg2Rad;
  const float halfH = std::atan(std::tan(halfV) * aspect_);
  const float halfMin = std::max(1e-3f, std::min(halfV, halfH));
  distance_ = (radius / std::sin(halfMin)) * 1.1f;
  // Near/far are derived dynamically in proj() from distance_ + sceneRadius_.
}

void OrbitCamera::setOrbit(const light3d::Vec3& target, float yawRad, float pitchRad,
                           float dist) {
  target_ = target;
  yaw_ = yawRad;
  pitch_ = std::max(-kPitchLimit, std::min(kPitchLimit, pitchRad));
  distance_ = std::max(dist, 1e-3f);
}

}  // namespace tusdview
