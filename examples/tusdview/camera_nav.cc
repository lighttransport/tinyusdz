// SPDX-License-Identifier: Apache-2.0
#include "camera_nav.hh"

#include <algorithm>
#include <cmath>

namespace tusdview {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kPitchLimit = 89.0f * kDeg2Rad;

light3d::Vec3 DirFromAngles(float yaw, float pitch) {
  const float cp = std::cos(pitch);
  return light3d::Vec3{cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
}
}  // namespace

light3d::Vec3 OrbitCamera::eye() const {
  return target_ + DirFromAngles(yaw_, pitch_) * distance_;
}

light3d::Mat4 OrbitCamera::view() const {
  return light3d::lookAt(eye(), target_, light3d::Vec3{0.0f, 1.0f, 0.0f});
}

light3d::Mat4 OrbitCamera::proj(bool zeroToOneDepth) const {
  const float fovY = fovYDeg_ * kDeg2Rad;
  if (zeroToOneDepth) {
    return light3d::perspectiveZeroOne(fovY, aspect_, znear_, zfar_);
  }
  return light3d::perspective(fovY, aspect_, znear_, zfar_);
}

void OrbitCamera::orbit(float dxPix, float dyPix) {
  const float k = 0.008f;
  yaw_ -= dxPix * k;
  pitch_ += dyPix * k;
  pitch_ = std::max(-kPitchLimit, std::min(kPitchLimit, pitch_));
}

void OrbitCamera::pan(float dxPix, float dyPix) {
  // Camera basis.
  light3d::Vec3 fwd = light3d::normalize(target_ - eye());
  light3d::Vec3 right = light3d::normalize(light3d::cross(fwd, light3d::Vec3{0, 1, 0}));
  light3d::Vec3 up = light3d::cross(right, fwd);
  const float scale = distance_ * 0.0015f;
  target_ = target_ - right * (dxPix * scale) + up * (dyPix * scale);
}

void OrbitCamera::dolly(float amount) {
  // Exponential zoom keeps the feel consistent at any distance.
  distance_ *= std::exp(-amount * 0.12f);
  distance_ = std::max(distance_, 1e-3f);
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
  znear_ = std::max(distance_ * 1e-3f, distance_ - radius * 1.5f);
  zfar_ = distance_ + radius * 4.0f;
}

}  // namespace tusdview
