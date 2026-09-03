// SPDX-License-Identifier: Apache-2.0
#include "camera.hh"

#include <algorithm>
#include <cmath>

namespace lusdql {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPitchLimit = 1.5533f;  // ~89 degrees

void Normalize(float v[3]) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 1e-20f) {
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
  }
}

void Cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

}  // namespace

void OrbitCamera::FrameBounds(const QlAabb& bounds, float aspect) {
  if (!bounds.valid) {
    target[0] = target[1] = target[2] = 0.0f;
    distance = 3.0f;
    near_clip = 0.01f;
    far_clip = 1000.0f;
    return;
  }

  bounds.Center(target);
  const float radius = std::max(bounds.Radius(), 1e-6f);

  // Fit the bounding sphere in the narrower of the two field-of-view angles, so
  // a wide-but-short window still frames the whole object.
  const float fov_x = 2.0f * std::atan(std::tan(fov_y * 0.5f) *
                                       std::max(aspect, 1e-3f));
  const float fov = std::min(fov_y, fov_x);
  distance = radius / std::max(std::sin(fov * 0.5f), 1e-3f);
  distance *= 1.15f;  // margin so nothing touches the frame edge

  near_clip = std::max(distance * 1e-4f, radius * 1e-4f);
  far_clip = distance + radius * 4.0f;
}

void OrbitCamera::FromDesc(const QlCameraDesc& desc, const QlAabb& bounds) {
  // Row-vector convention: translation is the last row, -Z is the view
  // direction.
  const float* m = desc.world_from_camera;
  const float eye[3] = {m[12], m[13], m[14]};
  float fwd[3] = {-m[8], -m[9], -m[10]};
  Normalize(fwd);

  fov_y = desc.fov_y_radians > 0.0f ? desc.fov_y_radians : fov_y;
  near_clip = desc.near_clip;
  far_clip = desc.far_clip;

  // Aim at the scene centre if we have one, else a point in front of the
  // camera; either way the orbit rig needs a target to spin around.
  float dist = bounds.valid ? std::max(bounds.Radius() * 2.0f, 1e-3f) : 1.0f;
  if (bounds.valid) {
    float center[3];
    bounds.Center(center);
    const float d[3] = {center[0] - eye[0], center[1] - eye[1],
                        center[2] - eye[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 1e-6f) dist = len;
    target[0] = center[0];
    target[1] = center[1];
    target[2] = center[2];
  } else {
    target[0] = eye[0] + fwd[0] * dist;
    target[1] = eye[1] + fwd[1] * dist;
    target[2] = eye[2] + fwd[2] * dist;
  }
  distance = dist;

  // Recover yaw/pitch from the eye-relative offset so subsequent orbiting
  // continues smoothly from the authored viewpoint.
  const float off[3] = {eye[0] - target[0], eye[1] - target[1],
                        eye[2] - target[2]};
  const float len = std::sqrt(off[0] * off[0] + off[1] * off[1] +
                              off[2] * off[2]);
  if (len > 1e-6f) {
    if (y_up) {
      pitch = std::asin(std::max(-1.0f, std::min(1.0f, off[1] / len)));
      yaw = std::atan2(off[0], off[2]);
    } else {
      pitch = std::asin(std::max(-1.0f, std::min(1.0f, off[2] / len)));
      yaw = std::atan2(off[0], off[1]);
    }
  }
}

void OrbitCamera::Eye(float out[3]) const {
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  if (y_up) {
    out[0] = target[0] + distance * cp * sy;
    out[1] = target[1] + distance * sp;
    out[2] = target[2] + distance * cp * cy;
  } else {
    out[0] = target[0] + distance * cp * sy;
    out[1] = target[1] + distance * cp * cy;
    out[2] = target[2] + distance * sp;
  }
}

void OrbitCamera::Basis(float right[3], float up[3], float forward[3]) const {
  float eye[3];
  Eye(eye);
  forward[0] = target[0] - eye[0];
  forward[1] = target[1] - eye[1];
  forward[2] = target[2] - eye[2];
  Normalize(forward);

  const float world_up[3] = {y_up ? 0.0f : 0.0f, y_up ? 1.0f : 0.0f,
                             y_up ? 0.0f : 1.0f};
  Cross(forward, world_up, right);
  if (right[0] * right[0] + right[1] * right[1] + right[2] * right[2] < 1e-12f) {
    // Looking straight along the up axis: pick any perpendicular.
    right[0] = 1.0f;
    right[1] = 0.0f;
    right[2] = 0.0f;
  }
  Normalize(right);
  Cross(right, forward, up);
  Normalize(up);
}

void OrbitCamera::Orbit(float d_yaw, float d_pitch) {
  yaw += d_yaw;
  while (yaw > kPi) yaw -= 2.0f * kPi;
  while (yaw < -kPi) yaw += 2.0f * kPi;
  pitch = std::max(-kPitchLimit, std::min(kPitchLimit, pitch + d_pitch));
}

void OrbitCamera::Pan(float dx_pixels, float dy_pixels, int viewport_h) {
  if (viewport_h <= 0) return;
  // Convert pixel motion into world units at the target plane, so panning
  // tracks the cursor regardless of zoom.
  const float world_per_pixel =
      2.0f * distance * std::tan(fov_y * 0.5f) / float(viewport_h);
  float right[3], up[3], fwd[3];
  Basis(right, up, fwd);
  for (int i = 0; i < 3; i++) {
    target[i] -= right[i] * dx_pixels * world_per_pixel;
    target[i] += up[i] * dy_pixels * world_per_pixel;
  }
}

void OrbitCamera::Dolly(float factor) {
  distance = std::max(1e-5f, distance * factor);
  far_clip = std::max(far_clip, distance * 2.0f);
}

void OrbitCamera::GenerateRay(int px, int py, int width, int height,
                              float jitter_x, float jitter_y, float origin[3],
                              float direction[3]) const {
  float right[3], up[3], fwd[3];
  Basis(right, up, fwd);
  Eye(origin);

  const float aspect =
      height > 0 ? float(width) / float(height) : 1.0f;
  const float tan_half = std::tan(fov_y * 0.5f);

  // NDC in [-1, 1], y up.
  const float sx = (2.0f * (float(px) + jitter_x) / float(width) - 1.0f) *
                   tan_half * aspect;
  const float sy = (1.0f - 2.0f * (float(py) + jitter_y) / float(height)) *
                   tan_half;

  for (int i = 0; i < 3; i++) {
    direction[i] = fwd[i] + right[i] * sx + up[i] * sy;
  }
  Normalize(direction);
}

void OrbitCamera::SetTarget(const float p[3]) {
  // Keep the eye where it is and re-aim: the picked point becomes the pivot
  // without the view lurching, which is what makes click-to-focus feel like
  // aiming rather than teleporting.
  float eye[3];
  Eye(eye);

  const float dx = eye[0] - p[0];
  const float dy = eye[1] - p[1];
  const float dz = eye[2] - p[2];
  const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < 1e-6f) {
    // Degenerate: the eye is sitting on the point. Keep the old distance and
    // just move the pivot, rather than producing a zero-length view vector.
    for (int i = 0; i < 3; i++) target[i] = p[i];
    return;
  }

  for (int i = 0; i < 3; i++) target[i] = p[i];
  distance = dist;

  // Re-derive the angles from the preserved eye position.
  const float up_c = y_up ? dy : dz;
  pitch = std::asin(std::max(-1.0f, std::min(1.0f, up_c / dist)));
  const float a = y_up ? dx : dx;
  const float b = y_up ? dz : dy;
  yaw = std::atan2(b, a);
}

bool OrbitCamera::ApproachToward(const OrbitCamera& goal, float dt,
                                 float scene_radius) {
  // Exponential smoothing, frame-rate independent: the fraction of the
  // remaining distance covered depends on elapsed time, not on how often this
  // happens to be called.
  const float rate = 18.0f;
  float t = 1.0f - std::exp(-rate * std::max(0.0f, dt));
  if (t > 1.0f) t = 1.0f;

  // Angles are interpolated on the shorter arc so a wrap past +-pi does not
  // send the camera the long way round.
  auto lerp_angle = [&](float from, float to) {
    float d = to - from;
    while (d > 3.14159265358979f) d -= 6.28318530717959f;
    while (d < -3.14159265358979f) d += 6.28318530717959f;
    return from + d * t;
  };

  const float radius = std::max(scene_radius, 1e-6f);
  const float pos_eps = radius * 1e-4f;
  const float ang_eps = 1e-4f;

  float d_target = 0.0f;
  for (int i = 0; i < 3; i++) {
    d_target = std::max(d_target, std::fabs(goal.target[i] - target[i]));
  }
  float d_yaw = goal.yaw - yaw;
  while (d_yaw > 3.14159265358979f) d_yaw -= 6.28318530717959f;
  while (d_yaw < -3.14159265358979f) d_yaw += 6.28318530717959f;

  const bool settled =
      d_target <= pos_eps && std::fabs(d_yaw) <= ang_eps &&
      std::fabs(goal.pitch - pitch) <= ang_eps &&
      std::fabs(goal.distance - distance) <= radius * 1e-4f &&
      std::fabs(goal.fov_y - fov_y) <= ang_eps;

  if (settled) {
    // Snap, so Differs() reports equal and the renderer stops resetting.
    *this = goal;
    return false;
  }

  for (int i = 0; i < 3; i++) {
    target[i] += (goal.target[i] - target[i]) * t;
  }
  yaw = lerp_angle(yaw, goal.yaw);
  pitch += (goal.pitch - pitch) * t;
  distance += (goal.distance - distance) * t;
  fov_y += (goal.fov_y - fov_y) * t;
  near_clip = goal.near_clip;
  far_clip = goal.far_clip;
  y_up = goal.y_up;
  return true;
}

bool OrbitCamera::Differs(const OrbitCamera& o) const {
  auto ne = [](float a, float b) { return std::fabs(a - b) > 1e-7f; };
  return ne(target[0], o.target[0]) || ne(target[1], o.target[1]) ||
         ne(target[2], o.target[2]) || ne(distance, o.distance) ||
         ne(yaw, o.yaw) || ne(pitch, o.pitch) || ne(fov_y, o.fov_y) ||
         y_up != o.y_up;
}

}  // namespace lusdql
