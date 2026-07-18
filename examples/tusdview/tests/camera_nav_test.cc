// SPDX-License-Identifier: Apache-2.0
#include "camera_nav.hh"

#include <cmath>
#include <cstdio>

namespace {

bool Near(float a, float b, float eps = 1.0e-5f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
  tusdview::OrbitCamera camera;
  const float boundsMin[3] = {-1.0f, -1.0f, -1.0f};
  const float boundsMax[3] = {1.0f, 1.0f, 1.0f};
  camera.setSceneBounds(boundsMin, boundsMax);

  // Outside a compact scene, auto-clip should spend depth precision on the
  // occupied range instead of retaining an inspection-scale near plane.
  camera.setOrbit(light3d::Vec3{0.0f, 0.0f, 0.0f}, 0.6f, 0.35f, 100.0f);
  if (!(camera.nearPlane() > 1.0f) || !(camera.farPlane() < 110.0f) ||
      !(camera.farPlane() > camera.nearPlane())) {
    std::fprintf(stderr, "outside-scene auto clip is not bounds-aware\n");
    return 1;
  }

  // Inside architecture, retain a close inspection plane even if the orbit
  // pivot is not on the nearest surface.
  camera.setOrbit(light3d::Vec3{0.0f, 0.0f, 0.0f}, 0.6f, 0.35f, 0.001f);
  if (!(camera.nearPlane() <= 0.001f)) {
    std::fprintf(stderr, "inside-scene near plane clips close geometry\n");
    return 1;
  }

  const float yaw = camera.yaw();
  const float pitch = camera.pitch();
  const float distance = camera.distance();
  const light3d::Vec3 oldEye = camera.eye();
  const light3d::Vec3 oldTarget = camera.target();
  const light3d::Vec3 oldBack = light3d::normalize(oldEye - camera.target());
  camera.dolly(11.0f);
  const light3d::Vec3 newEye = camera.eye();
  const light3d::Vec3 eyeDelta = newEye - oldEye;
  const light3d::Vec3 targetDelta = camera.target() - oldTarget;
  if (!(light3d::dot(newEye, oldBack) < 0.0f) ||
      !(light3d::dot(camera.target(), oldBack) <
        light3d::dot(newEye, oldBack)) ||
      light3d::length(eyeDelta - targetDelta) > 1e-5f ||
      !Near(camera.distance(), distance) || !Near(camera.yaw(), yaw) ||
      !Near(camera.pitch(), pitch)) {
    std::fprintf(stderr, "dolly did not cross the pivot continuously\n");
    return 1;
  }

  return 0;
}
