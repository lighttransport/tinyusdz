// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cmath>

#include "light3d/math.h"

namespace tusdview {

struct RtCameraLens {
  float focusDistance{0.0f};
  float apertureRadius{0.0f};
  bool enabled() const {
    return focusDistance > 0.0f && apertureRadius > 0.0f;
  }
};

// Thin-lens renderers use a focus plane perpendicular to the camera forward
// vector.  Projecting the picked world-space point onto that vector keeps an
// off-axis click in focus too (using its radial distance would put the plane
// slightly behind the clicked surface).
inline float RtFocusDistanceToPoint(const light3d::Vec3& eye,
                                    const light3d::Vec3& forward,
                                    const light3d::Vec3& point) {
  const float forwardLength2 = light3d::dot(forward, forward);
  if (!std::isfinite(forwardLength2) || forwardLength2 <= 1.0e-20f) {
    return 0.0f;
  }
  const float distance =
      light3d::dot(point - eye, forward) / std::sqrt(forwardLength2);
  return std::isfinite(distance) ? distance : 0.0f;
}

inline RtCameraLens MakeRtCameraLens(float focalLength,
                                     float focusDistance, float fStop,
                                     bool perspective) {
  RtCameraLens lens;
  if (perspective && focalLength > 0.0f && focusDistance > 0.0f &&
      fStop > 0.0f) {
    lens.focusDistance = focusDistance;
    lens.apertureRadius = focalLength / (20.0f * fStop);
  }
  return lens;
}

// Auto-fit cameras have no authored USD focal length. Use a conventional
// 50 mm equivalent and convert its physical aperture into stage units.
inline RtCameraLens MakeAutoRtCameraLens(float focusDistance, float fStop,
                                         double metersPerUnit) {
  RtCameraLens lens;
  if (focusDistance > 0.0f && fStop > 0.0f && metersPerUnit > 0.0) {
    lens.focusDistance = focusDistance;
    lens.apertureRadius =
        static_cast<float>(0.025 / (metersPerUnit * double(fStop)));
  }
  return lens;
}

}  // namespace tusdview
