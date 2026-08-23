// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace tusdview {

struct RtCameraLens {
  float focusDistance{0.0f};
  float apertureRadius{0.0f};
  bool enabled() const {
    return focusDistance > 0.0f && apertureRadius > 0.0f;
  }
};

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
