// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-light-converter.cc
/// @brief Light conversion functions for RenderSceneConverter
///

#include "render-data.hh"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include "common-utils.hh"
#include "image-loader.hh"
#include "pprinter.hh"
#include "security-policy.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "usdLux.hh"
#include "value-types.hh"

#include "common-macros.inc"
#include "scene-access.hh"

namespace tinyusdz {
namespace tydra {

namespace {

// Helper to extract common light properties
template<typename LightType>
bool ExtractCommonLightProperties(
    const RenderSceneConverterEnv &env,
    const LightType &light,
    RenderLight *rlight) {

  // Extract color
  if (light.color.authored() && !light.color.is_blocked()) {
    value::color3f col;
    if (light.color.get_value().get(env.timecode, &col)) {
      rlight->color[0] = col[0];
      rlight->color[1] = col[1];
      rlight->color[2] = col[2];
    }
  }

  // Extract intensity
  if (light.intensity.authored() && !light.intensity.is_blocked()) {
    float val;
    if (light.intensity.get_value().get(env.timecode, &val)) {
      rlight->intensity = val;
    }
  }

  // Extract exposure
  if (light.exposure.authored() && !light.exposure.is_blocked()) {
    float val;
    if (light.exposure.get_value().get(env.timecode, &val)) {
      rlight->exposure = val;
    }
  }

  // Extract diffuse multiplier
  if (light.diffuse.authored() && !light.diffuse.is_blocked()) {
    float val;
    if (light.diffuse.get_value().get(env.timecode, &val)) {
      rlight->diffuse = val;
    }
  }

  // Extract specular multiplier
  if (light.specular.authored() && !light.specular.is_blocked()) {
    float val;
    if (light.specular.get_value().get(env.timecode, &val)) {
      rlight->specular = val;
    }
  }

  // Extract normalize flag
  if (light.normalize.authored() && !light.normalize.is_blocked()) {
    bool val;
    if (light.normalize.get_value().get(env.timecode, &val)) {
      rlight->normalize = val;
    }
  }

  // Extract color temperature
  if (light.enableColorTemperature.authored() && !light.enableColorTemperature.is_blocked()) {
    bool val;
    if (light.enableColorTemperature.get_value().get(env.timecode, &val)) {
      rlight->enableColorTemperature = val;
    }
  }

  if (light.colorTemperature.authored() && !light.colorTemperature.is_blocked()) {
    float val;
    if (light.colorTemperature.get_value().get(env.timecode, &val)) {
      rlight->colorTemperature = val;
    }
  }

  // Extract shadow properties
  if (light.shadowEnable.authored() && !light.shadowEnable.is_blocked()) {
    bool val;
    if (light.shadowEnable.get_value().get(env.timecode, &val)) {
      rlight->shadowEnable = val;
    }
  }

  if (light.shadowColor.authored() && !light.shadowColor.is_blocked()) {
    value::color3f col;
    if (light.shadowColor.get_value().get(env.timecode, &col)) {
      rlight->shadowColor[0] = col[0];
      rlight->shadowColor[1] = col[1];
      rlight->shadowColor[2] = col[2];
    }
  }

  if (light.shadowDistance.authored() && !light.shadowDistance.is_blocked()) {
    float val;
    if (light.shadowDistance.get_value().get(env.timecode, &val)) {
      rlight->shadowDistance = val;
    }
  }

  if (light.shadowFalloff.authored() && !light.shadowFalloff.is_blocked()) {
    float val;
    if (light.shadowFalloff.get_value().get(env.timecode, &val)) {
      rlight->shadowFalloff = val;
    }
  }

  if (light.shadowFalloffGamma.authored() && !light.shadowFalloffGamma.is_blocked()) {
    float val;
    if (light.shadowFalloffGamma.get_value().get(env.timecode, &val)) {
      rlight->shadowFalloffGamma = val;
    }
  }

  return true;
}

// Helper to extract shaping properties (for SphereLight and RectLight)
template<typename LightType>
bool ExtractShapingProperties(
    const RenderSceneConverterEnv &env,
    const LightType &light,
    RenderLight *rlight) {

  if (light.shapingFocus.authored() && !light.shapingFocus.is_blocked()) {
    float val;
    if (light.shapingFocus.get_value().get(env.timecode, &val)) {
      rlight->shapingFocus = val;
    }
  }

  if (light.shapingFocusTint.authored() && !light.shapingFocusTint.is_blocked()) {
    value::color3f col;
    if (light.shapingFocusTint.get_value().get(env.timecode, &col)) {
      rlight->shapingFocusTint[0] = col[0];
      rlight->shapingFocusTint[1] = col[1];
      rlight->shapingFocusTint[2] = col[2];
    }
  }

  if (light.shapingConeAngle.authored() && !light.shapingConeAngle.is_blocked()) {
    float val;
    if (light.shapingConeAngle.get_value().get(env.timecode, &val)) {
      rlight->shapingConeAngle = val;
    }
  }

  if (light.shapingConeSoftness.authored() && !light.shapingConeSoftness.is_blocked()) {
    float val;
    if (light.shapingConeSoftness.get_value().get(env.timecode, &val)) {
      rlight->shapingConeSoftness = val;
    }
  }

  return true;
}

}  // namespace

bool RenderSceneConverter::ConvertSphereLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const SphereLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Sphere;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract shaping properties
  if (!ExtractShapingProperties(env, light, &rlight)) {
    return false;
  }

  // Extract radius
  if (light.radius.authored() && !light.radius.is_blocked()) {
    float val;
    if (light.radius.get_value().get(env.timecode, &val)) {
      rlight.radius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertDistantLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const DistantLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Distant;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract angle (angular diameter in degrees)
  if (light.angle.authored() && !light.angle.is_blocked()) {
    float val;
    if (light.angle.get_value().get(env.timecode, &val)) {
      rlight.angle = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertDomeLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const DomeLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Dome;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract texture file and load envmap image
  if (light.file.authored() && !light.file.is_blocked()) {
    value::AssetPath assetPath;
    if (light.file.get_value()->get(env.timecode, &assetPath)) {
      rlight.textureFile = assetPath.GetAssetPath();

      // Load the envmap texture if scene config allows
      if (env.scene_config.load_texture_assets && !assetPath.GetAssetPath().empty()) {
        TextureImage texImage;
        BufferData imageBuffer;
        imageBuffer.componentType = ComponentType::UInt8;

        std::string warn, err;

        TextureImageLoaderFunction tex_loader_fun =
            env.material_config.texture_image_loader_function;
        if (!tex_loader_fun) {
          tex_loader_fun = DefaultTextureImageLoaderFunction;
        }

        AssetInfo assetInfo;  // Empty asset info for now
        bool tex_loaded = tex_loader_fun(
            assetPath, assetInfo, env.asset_resolver, &texImage,
            &imageBuffer.data,
            env.material_config.texture_image_loader_function_userdata,
            &warn, &err);

        if (warn.size()) {
          PushWarn(warn);
        }

        if (tex_loaded) {
          texImage.asset_identifier = assetPath.GetAssetPath();
          texImage.decoded = true;

          // HDR images (like EXR) should be treated as linear/Raw colorspace
          // Most envmaps are HDR and should not have sRGB gamma
          texImage.usdColorSpace = ColorSpace::Raw;
          texImage.colorSpace = ColorSpace::Lin_sRGB;

          // Add buffer
          texImage.buffer_id = int64_t(buffers.size());
          buffers.emplace_back(imageBuffer);

          // Add image and set envmap_texture_id
          rlight.envmap_texture_id = int32_t(images.size());
          images.emplace_back(texImage);

          DCOUT("Loaded envmap texture: " << assetPath.GetAssetPath()
                << " width=" << texImage.width
                << " height=" << texImage.height
                << " channels=" << texImage.channels);
        } else {
          // Fallback: store raw asset when decoding fails (e.g., EXR/HDR not supported)
          if (err.size()) {
            PushWarn(fmt::format("Failed to decode envmap texture: `{}`. reason = {}. Falling back to raw asset storage.",
                                 assetPath.GetAssetPath(), err));
          }

          // Try to store the raw asset for later decoding (e.g., in JS layer)
          Asset asset;
          std::string sanitized_path =
              utils::SanitizeAssetPath(assetPath.GetAssetPath());
          if (sanitized_path.empty()) {
            PushWarn(fmt::format("Unsafe envmap asset path: `{}`",
                                 assetPath.GetAssetPath()));
            continue;
          }
          std::string resolvedPath = env.asset_resolver.resolve(sanitized_path);
          std::string readWarn, readErr;

          bool asset_opened = env.asset_resolver.open_asset(
              resolvedPath, sanitized_path, &asset, &readWarn, &readErr);

          if (asset_opened) {
            if (asset.size() > security_policy::kResolverMaxAssetReadBytes) {
              PushWarn(fmt::format(
                  "Envmap asset exceeds max bytes ({} > {}).",
                  asset.size(), security_policy::kResolverMaxAssetReadBytes));
              continue;
            }
            TextureImage fallbackTexImage;
            BufferData fallbackImageBuffer;
            fallbackImageBuffer.componentType = ComponentType::UInt8;

            fallbackTexImage.asset_identifier = resolvedPath;

            fallbackImageBuffer.data.resize(asset.size());
            memcpy(fallbackImageBuffer.data.data(), asset.data(), asset.size());

            fallbackTexImage.buffer_id = int64_t(buffers.size());
            buffers.emplace_back(fallbackImageBuffer);

            fallbackTexImage.decoded = false;
            fallbackTexImage.usdColorSpace = ColorSpace::Raw;

            rlight.envmap_texture_id = int32_t(images.size());
            images.emplace_back(fallbackTexImage);

            DCOUT("Stored envmap asset (fallback): " << resolvedPath);
          } else {
            PushWarn(fmt::format("Failed to read envmap asset: `{}`. reason = {}",
                                 assetPath.GetAssetPath(), readErr));
          }
        }
      } else if (!env.scene_config.load_texture_assets) {
        // Store asset path only without decoding
        Asset asset;
        std::string sanitized_path =
            utils::SanitizeAssetPath(assetPath.GetAssetPath());
        if (sanitized_path.empty()) {
          PushWarn(fmt::format("Unsafe envmap asset path: `{}`",
                               assetPath.GetAssetPath()));
          continue;
        }
        std::string resolvedPath = env.asset_resolver.resolve(sanitized_path);
        std::string readWarn, readErr;

        bool asset_opened = env.asset_resolver.open_asset(
            resolvedPath, sanitized_path, &asset, &readWarn, &readErr);

        if (asset_opened) {
          if (asset.size() > security_policy::kResolverMaxAssetReadBytes) {
            PushWarn(fmt::format(
                "Envmap asset exceeds max bytes ({} > {}).",
                asset.size(), security_policy::kResolverMaxAssetReadBytes));
            continue;
          }
          TextureImage texImage;
          BufferData imageBuffer;
          imageBuffer.componentType = ComponentType::UInt8;

          texImage.asset_identifier = resolvedPath;

          imageBuffer.data.resize(asset.size());
          memcpy(imageBuffer.data.data(), asset.data(), asset.size());

          texImage.buffer_id = int64_t(buffers.size());
          buffers.emplace_back(imageBuffer);

          texImage.decoded = false;
          texImage.usdColorSpace = ColorSpace::Raw;

          rlight.envmap_texture_id = int32_t(images.size());
          images.emplace_back(texImage);

          DCOUT("Stored envmap asset: " << resolvedPath);
        } else {
          PushWarn(fmt::format("Failed to read envmap asset (load_texture_assets=false): `{}`. reason = {}",
                               assetPath.GetAssetPath(), readErr));
        }
      }
    }
  }

  // Extract texture format
  if (light.textureFormat.authored() && !light.textureFormat.is_blocked()) {
    const auto& fmt_animatable = light.textureFormat.get_value();
    if (fmt_animatable.is_scalar()) {
      DomeLight::TextureFormat fmt;
      if (fmt_animatable.get_scalar(&fmt)) {
        switch (fmt) {
          case DomeLight::TextureFormat::Automatic:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::Automatic;
            break;
          case DomeLight::TextureFormat::Latlong:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::Latlong;
            break;
          case DomeLight::TextureFormat::MirroredBall:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::MirroredBall;
            break;
          case DomeLight::TextureFormat::Angular:
            rlight.domeTextureFormat = RenderLight::DomeTextureFormat::Angular;
            break;
        }
      }
    }
  }

  // Extract guide radius
  if (light.guideRadius.authored() && !light.guideRadius.is_blocked()) {
    float val;
    if (light.guideRadius.get_value().get(env.timecode, &val)) {
      rlight.guideRadius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertRectLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const RectLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Rect;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract shaping properties
  if (!ExtractShapingProperties(env, light, &rlight)) {
    return false;
  }

  // Extract width
  if (light.width.authored() && !light.width.is_blocked()) {
    float val;
    if (light.width.get_value().get(env.timecode, &val)) {
      rlight.width = val;
    }
  }

  // Extract height
  if (light.height.authored() && !light.height.is_blocked()) {
    float val;
    if (light.height.get_value().get(env.timecode, &val)) {
      rlight.height = val;
    }
  }

  // Extract texture file (optional)
  if (light.file.authored() && !light.file.is_blocked()) {
    value::AssetPath asset;
    if (light.file.get_value()->get(env.timecode, &asset)) {
      rlight.textureFile = asset.GetAssetPath();
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertDiskLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const DiskLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Disk;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract radius
  if (light.radius.authored() && !light.radius.is_blocked()) {
    float val;
    if (light.radius.get_value().get(env.timecode, &val)) {
      rlight.radius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertCylinderLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const CylinderLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Cylinder;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract length
  if (light.length.authored() && !light.length.is_blocked()) {
    float val;
    if (light.length.get_value().get(env.timecode, &val)) {
      rlight.length = val;
    }
  }

  // Extract radius
  if (light.radius.authored() && !light.radius.is_blocked()) {
    float val;
    if (light.radius.get_value().get(env.timecode, &val)) {
      rlight.radius = val;
    }
  }

  (*rlight_out) = std::move(rlight);
  return true;
}

bool RenderSceneConverter::ConvertGeometryLight(
    const RenderSceneConverterEnv &env,
    const Path &light_abs_path,
    const GeometryLight &light,
    RenderLight *rlight_out) {

  if (!rlight_out) {
    PUSH_ERROR_AND_RETURN("rlight_out arg is nullptr.");
  }

  RenderLight rlight;
  rlight.name = light.name;
  rlight.abs_path = light_abs_path.full_path_name();
  rlight.type = RenderLight::Type::Geometry;

  // Extract common properties
  if (!ExtractCommonLightProperties(env, light, &rlight)) {
    return false;
  }

  // Extract geometry relationship to find the target mesh
  if (light.geometry.authored() && !light.geometry.is_blocked()) {
    const std::vector<Path> targets = light.geometry.get_targetPaths();
    if (!targets.empty()) {
      // Use the first target path
      const Path &target_path = targets[0];
      std::string geometry_path = target_path.full_path_name();

      // The actual mesh_id will be resolved during scene building
      rlight.geometry_mesh_id = -1;

      DCOUT("GeometryLight " << rlight.abs_path << " references geometry: " << geometry_path);
    } else {
      PUSH_WARN("GeometryLight " + rlight.abs_path + " has no geometry targets");
    }
  } else {
    PUSH_WARN("GeometryLight " + rlight.abs_path + " missing geometry relationship");
  }

  // Default material sync mode for GeometryLight
  rlight.material_sync_mode = "materialGlowTintsLight";

  (*rlight_out) = std::move(rlight);
  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
