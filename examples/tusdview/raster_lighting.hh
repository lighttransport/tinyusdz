// SPDX-License-Identifier: Apache-2.0
// Backend-neutral bounded direct-light records for real-time raster shading.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>

#include "gpu_scene.hh"
#include "light3d/math.h"

namespace tusdview {

constexpr int kMaxRasterLights = 16;

// Four vec4s per light. This layout is deliberately friendly to both GLSL
// uniform arrays (OpenGL) and std140 Frame UBO arrays (Vulkan).
struct alignas(16) RasterLightGPU {
  float positionType[4]{};   // xyz position, w DrawLightCPU::Type
  float directionAngle[4]{}; // xyz direction, w shaping cone angle in degrees
  float colorDiffuse[4]{};   // rgb normalized radiance, w diffuse multiplier
  float specularShape[4]{};  // x specular, y cone softness, z focus, w hasShaping
};
static_assert(sizeof(RasterLightGPU) == 16 * sizeof(float),
              "RasterLightGPU must be four tightly packed vec4s");

struct RasterLightSet {
  std::array<RasterLightGPU, kMaxRasterLights> lights{};
  int count{0};
  int truncated{0};
  // One 16-bit stage-order direct-light mask per mesh. A set bit means that
  // light's collection includes the mesh. Empty when meshCount == 0.
  std::vector<uint32_t> meshMasks;
  // Equivalent collection mask for shadow visibility. This is intentionally
  // separate: USD permits a mesh to receive a light while being excluded from
  // that light's shadow collection (and vice versa).
  std::vector<uint32_t> shadowMeshMasks;
  // Direct-light slot selected for the single deterministic raster shadow map.
  // Prefer the first enabled distant light; otherwise use the first enabled,
  // shaped sphere light. -1 means no supported shadow caster.
  int shadowLightSlot{-1};
};

inline bool IsRasterDirectLight(const DrawLightCPU& light) {
  return light.type != DrawLightCPU::Type::Dome &&
         light.type != DrawLightCPU::Type::Geometry &&
         light.type != DrawLightCPU::Type::Portal;
}

inline RasterLightSet PackRasterLights(const std::vector<DrawLightCPU>& src,
                                       size_t meshCount) {
  RasterLightSet out;
  int shapedSphereFallback = -1;
  out.meshMasks.assign(meshCount, 0u);
  out.shadowMeshMasks.assign(meshCount, 0u);
  for (const DrawLightCPU& light : src) {
    if (!IsRasterDirectLight(light)) continue;
    if (out.count >= kMaxRasterLights) {
      ++out.truncated;
      continue;
    }
    const int slot = out.count++;
    if (light.shadowEnable) {
      if (light.type == DrawLightCPU::Type::Distant &&
          out.shadowLightSlot < 0) {
        out.shadowLightSlot = slot;
      } else if (light.type == DrawLightCPU::Type::Sphere && light.hasShaping &&
                 shapedSphereFallback < 0) {
        shapedSphereFallback = slot;
      }
    }
    RasterLightGPU& dst = out.lights[static_cast<size_t>(slot)];
    for (int i = 0; i < 3; ++i) {
      dst.positionType[i] = light.position[i];
      // DrawLightCPU stores the emission direction. Distant-light shading needs
      // the opposite vector (surface -> light); finite-light shaping needs the
      // authored emission direction as-is.
      dst.directionAngle[i] =
          light.type == DrawLightCPU::Type::Distant ? -light.direction[i]
                                                     : light.direction[i];
      dst.colorDiffuse[i] = light.normalizedColor[i];
    }
    dst.positionType[3] = static_cast<float>(light.type);
    dst.directionAngle[3] = light.shapingConeAngle;
    dst.colorDiffuse[3] = light.diffuse;
    dst.specularShape[0] = light.specular;
    dst.specularShape[1] = light.shapingConeSoftness;
    dst.specularShape[2] = light.shapingFocus;
    dst.specularShape[3] = light.hasShaping ? 1.0f : 0.0f;

    const uint32_t bit = uint32_t{1} << static_cast<uint32_t>(slot);
    if (light.lightLinksAll) {
      for (uint32_t& mask : out.meshMasks) mask |= bit;
    } else {
      for (int meshIndex : light.lightLinkMeshIndices) {
        if (meshIndex >= 0 && static_cast<size_t>(meshIndex) < meshCount)
          out.meshMasks[static_cast<size_t>(meshIndex)] |= bit;
      }
    }
    if (light.shadowLinksAll) {
      for (uint32_t& mask : out.shadowMeshMasks) mask |= bit;
    } else {
      for (int meshIndex : light.shadowLinkMeshIndices) {
        if (meshIndex >= 0 && static_cast<size_t>(meshIndex) < meshCount)
          out.shadowMeshMasks[static_cast<size_t>(meshIndex)] |= bit;
      }
    }
  }
  if (out.shadowLightSlot < 0) out.shadowLightSlot = shapedSphereFallback;
  return out;
}

inline bool RasterShadowIncludesMesh(const RasterLightSet& lights,
                                     int meshIndex) {
  if (lights.shadowLightSlot < 0) return false;
  if (meshIndex < 0 ||
      static_cast<size_t>(meshIndex) >= lights.shadowMeshMasks.size())
    return true;
  const uint32_t bit = uint32_t{1} <<
      static_cast<uint32_t>(lights.shadowLightSlot);
  return (lights.shadowMeshMasks[static_cast<size_t>(meshIndex)] & bit) != 0u;
}

inline uint32_t RasterLightMaskForMesh(const RasterLightSet& lights,
                                       int meshIndex) {
  if (meshIndex >= 0 && static_cast<size_t>(meshIndex) < lights.meshMasks.size())
    return lights.meshMasks[static_cast<size_t>(meshIndex)];
  return lights.count >= 32 ? ~uint32_t{0}
                            : ((uint32_t{1} << lights.count) - uint32_t{1});
}

struct RasterShadowCamera {
  light3d::Mat4 viewProj;
  int lightSlot{-1};
  float nearPlane{0.01f};
  float farPlane{1.0f};
  bool perspective{false};
};

inline light3d::Mat4 ShadowOrtho(float left, float right, float bottom,
                                 float top, float nearPlane, float farPlane,
                                 bool zeroToOne) {
  light3d::Mat4 p{};
  p.m[0] = 2.0f / (right - left);
  p.m[5] = 2.0f / (top - bottom);
  p.m[10] = (zeroToOne ? -1.0f : -2.0f) / (farPlane - nearPlane);
  p.m[12] = -(right + left) / (right - left);
  p.m[13] = -(top + bottom) / (top - bottom);
  p.m[14] = zeroToOne ? -nearPlane / (farPlane - nearPlane)
                      : -(farPlane + nearPlane) / (farPlane - nearPlane);
  p.m[15] = 1.0f;
  return p;
}

// Fit the one supported shadow camera to the scene bounds. `zeroToOne` selects
// Vulkan depth convention; GL uses the default -1..1 convention.
inline bool BuildRasterShadowCamera(const RasterLightSet& lights,
                                    const float sceneMin[3],
                                    const float sceneExtent[3], bool zeroToOne,
                                    RasterShadowCamera* out) {
  if (!out || lights.shadowLightSlot < 0 ||
      lights.shadowLightSlot >= lights.count) return false;
  const RasterLightGPU& light =
      lights.lights[static_cast<size_t>(lights.shadowLightSlot)];
  const int type = static_cast<int>(light.positionType[3] + 0.5f);
  const light3d::Vec3 center{
      sceneMin[0] + sceneExtent[0] * 0.5f,
      sceneMin[1] + sceneExtent[1] * 0.5f,
      sceneMin[2] + sceneExtent[2] * 0.5f};
  const float radius = std::max(
      0.01f, 0.5f * std::sqrt(sceneExtent[0] * sceneExtent[0] +
                              sceneExtent[1] * sceneExtent[1] +
                              sceneExtent[2] * sceneExtent[2]));
  light3d::Vec3 direction{light.directionAngle[0], light.directionAngle[1],
                         light.directionAngle[2]};
  if (light3d::length(direction) < 1.0e-6f) direction = {0.0f, -1.0f, 0.0f};
  direction = light3d::normalize(direction);
  const light3d::Vec3 up = std::fabs(direction.y) > 0.99f
                               ? light3d::Vec3{1.0f, 0.0f, 0.0f}
                               : light3d::Vec3{0.0f, 1.0f, 0.0f};
  light3d::Mat4 view;
  light3d::Mat4 proj;
  float nearPlane = 0.01f;
  float farPlane = radius * 4.0f;
  bool perspective = false;
  if (type == static_cast<int>(DrawLightCPU::Type::Distant)) {
    view = light3d::lookAt(center + direction * (radius * 2.0f), center, up);
    // A square scene-enclosing volume is stable under light rotation and leaves
    // a small guard band for PCF taps at the fitted edge.
    const float span = radius * 1.05f;
    nearPlane = std::max(0.01f, radius * 0.95f);
    farPlane = radius * 3.05f;
    proj = ShadowOrtho(-span, span, -span, span, nearPlane, farPlane,
                       zeroToOne);
  } else if (type == static_cast<int>(DrawLightCPU::Type::Sphere) &&
             light.specularShape[3] > 0.5f) {
    perspective = true;
    const light3d::Vec3 eye{light.positionType[0], light.positionType[1],
                            light.positionType[2]};
    view = light3d::lookAt(eye, eye + direction, up);
    farPlane = std::max(radius * 4.0f, light3d::length(center - eye) + radius);
    nearPlane = std::max(0.01f, farPlane * 1.0e-4f);
    const float halfAngle = std::max(0.5f, std::min(light.directionAngle[3], 89.0f));
    constexpr float kDegToRad = 0.01745329251994329577f;
    proj = zeroToOne
               ? light3d::perspectiveZeroOne(2.0f * halfAngle * kDegToRad,
                                             1.0f, nearPlane, farPlane)
               : light3d::perspective(2.0f * halfAngle * kDegToRad, 1.0f,
                                      nearPlane, farPlane);
  } else {
    return false;
  }
  out->viewProj = proj * view;
  out->lightSlot = lights.shadowLightSlot;
  out->nearPlane = nearPlane;
  out->farPlane = farPlane;
  out->perspective = perspective;
  return true;
}

}  // namespace tusdview
