// SPDX-License-Identifier: Apache-2.0
// Backend-neutral bounded direct-light records for real-time raster shading.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "gpu_scene.hh"

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
};

inline bool IsRasterDirectLight(const DrawLightCPU& light) {
  return light.type != DrawLightCPU::Type::Dome &&
         light.type != DrawLightCPU::Type::Geometry &&
         light.type != DrawLightCPU::Type::Portal;
}

inline RasterLightSet PackRasterLights(const std::vector<DrawLightCPU>& src,
                                       size_t meshCount) {
  RasterLightSet out;
  out.meshMasks.assign(meshCount, 0u);
  for (const DrawLightCPU& light : src) {
    if (!IsRasterDirectLight(light)) continue;
    if (out.count >= kMaxRasterLights) {
      ++out.truncated;
      continue;
    }
    const int slot = out.count++;
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
  }
  return out;
}

inline uint32_t RasterLightMaskForMesh(const RasterLightSet& lights,
                                       int meshIndex) {
  if (meshIndex >= 0 && static_cast<size_t>(meshIndex) < lights.meshMasks.size())
    return lights.meshMasks[static_cast<size_t>(meshIndex)];
  return lights.count >= 32 ? ~uint32_t{0}
                            : ((uint32_t{1} << lights.count) - uint32_t{1});
}

}  // namespace tusdview
