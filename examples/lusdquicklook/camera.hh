// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — orbit camera and ray generation.
//
// Deliberately an orbit rig rather than a free-fly camera: a Quick Look user
// wants to spin an object and look at it, and an orbit camera cannot get lost.
#pragma once

#include "ql_scene.hh"

namespace lusdql {

struct OrbitCamera {
  float target[3] = {0.0f, 0.0f, 0.0f};
  float distance = 3.0f;
  float yaw = 0.7f;    // radians, around the up axis
  float pitch = 0.35f; // radians, clamped away from the poles
  float fov_y = 0.7f;  // radians
  float near_clip = 0.01f;
  float far_clip = 1000.0f;
  bool y_up = true;

  // Frame `bounds` so the whole object fits with a small margin.
  void FrameBounds(const QlAabb& bounds, float aspect);

  // Adopt an authored USD camera.
  void FromDesc(const QlCameraDesc& desc, const QlAabb& bounds);

  void Eye(float out[3]) const;
  // Orthonormal camera basis: right, up, and forward (eye -> target).
  void Basis(float right[3], float up[3], float forward[3]) const;

  void Orbit(float d_yaw, float d_pitch);
  void Pan(float dx_pixels, float dy_pixels, int viewport_h);
  void Dolly(float factor);  // >1 moves away

  // Primary ray for pixel (px, py) with the given sub-pixel jitter in [0,1).
  void GenerateRay(int px, int py, int width, int height, float jitter_x,
                   float jitter_y, float origin[3], float direction[3]) const;

  // Re-centre the orbit on `p` without moving the eye: the view does not jump,
  // but everything afterwards orbits around what was picked.
  void SetTarget(const float p[3]);

  // Move toward `goal` with an exponential damper. Returns false once the
  // residual is below `scene_radius`-relative epsilon, at which point this
  // camera is snapped exactly to the goal.
  //
  // Settling exactly is a hard requirement, not a nicety: the app treats "still
  // animating" as being busy, so a damper that only ever approaches its goal
  // would keep the event loop spinning forever and never let the process go
  // idle.
  bool ApproachToward(const OrbitCamera& goal, float dt, float scene_radius);

  // True when the two cameras would produce a different image.
  bool Differs(const OrbitCamera& other) const;
};

}  // namespace lusdql
