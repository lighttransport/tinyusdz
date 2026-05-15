// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-texture-converter.cc
/// @brief Texture-related conversion functions for RenderSceneConverter
///

#include "render-data.hh"

#include <cstring>
#include <functional>

#include "common-utils.hh"
#include "image-loader.hh"
#include "security-policy.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "value-types.hh"

#include "common-macros.inc"

namespace tinyusdz {
namespace tydra {

bool DefaultTextureImageLoaderFunction(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, TextureImage *texImageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  if (!texImageOut) {
    if (err) {
      (*err) = "`imageOut` argument is nullptr\n";
    }
    return false;
  }

  if (!imageData) {
    if (err) {
      (*err) = "`imageData` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string sanitized_path =
      utils::SanitizeAssetPath(assetPath.GetAssetPath());
  if (sanitized_path.empty()) {
    if (err) {
      (*err) += fmt::format("Unsafe asset path: {}\n", assetPath.GetAssetPath());
    }
    return false;
  }

  std::string resolvedPath = assetResolver.resolve(sanitized_path);

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  Asset asset;
  bool ret = assetResolver.open_asset(resolvedPath, sanitized_path,
                                      &asset, warn, err);
  if (!ret) {
    if (err) {
      (*err) += fmt::format("Failed to open asset: {}", resolvedPath);
    }
    return false;
  }

  if (asset.size() > security_policy::kResolverMaxAssetReadBytes) {
    if (err) {
      (*err) += fmt::format("Resolved asset exceeds max bytes ({} > {}).",
                            asset.size(), security_policy::kResolverMaxAssetReadBytes);
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);

  // TODO: user-defined image loader handler.
  auto result = tinyusdz::image::LoadImageFromMemory(asset.data(), asset.size(),
                                                     resolvedPath);
  if (!result) {
    if (err) {
      (*err) += "Failed to load image file: " + result.error() + "\n";
    }
    return false;
  }

  TextureImage texImage;

  texImage.asset_identifier = resolvedPath;
  texImage.channels = result.value().image.channels;

  const auto &imgret = result.value();

  if (imgret.image.bpp == 8) {
    // assume uint8
    texImage.assetTexelComponentType = ComponentType::UInt8;
  } else if (imgret.image.bpp == 16) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt16;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int16;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Half;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }

  } else if (imgret.image.bpp == 32) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt32;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int32;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Float;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }
  } else {
    DCOUT("TODO: bpp = " << result.value().image.bpp);
    if (err) {
      (*err) += "TODO or unsupported bpp: " +
               std::to_string(result.value().image.bpp) + "\n";
    }
    return false;
  }

  texImage.channels = result.value().image.channels;
  texImage.width = result.value().image.width;
  texImage.height = result.value().image.height;

  (*texImageOut) = texImage;

  // raw image data
  (*imageData) = result.value().image.data;

  return true;
}

bool InferColorSpace(const value::token &tok, ColorSpace *cty) {
  if (!cty) {
    return false;
  }

  if (tok.str() == "raw") {
    (*cty) = ColorSpace::Raw;
  } else if (tok.str() == "Raw") {
    (*cty) = ColorSpace::Raw;
  } else if (tok.str() == "srgb") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "sRGB") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "srgb_texture") {  // MaterialX texture colorspace
    (*cty) = ColorSpace::sRGB_Texture;
  } else if (tok.str() == "linear") { // guess linear_srgb
    (*cty) = ColorSpace::Lin_sRGB;
  } else if (tok.str() == "lin_srgb") {
    (*cty) = ColorSpace::Lin_sRGB;
  } else if (tok.str() == "rec709") {
    (*cty) = ColorSpace::Rec709;
  } else if (tok.str() == "lin_rec709") {  // MaterialX linear Rec.709
    (*cty) = ColorSpace::Lin_Rec709;
  } else if (tok.str() == "g22_rec709") {  // MaterialX gamma 2.2 Rec.709
    (*cty) = ColorSpace::g22_Rec709;
  } else if (tok.str() == "g18_rec709") {  // MaterialX gamma 1.8 Rec.709
    (*cty) = ColorSpace::g18_Rec709;
  } else if (tok.str() == "lin_rec2020") {  // Linear Rec.2020
    (*cty) = ColorSpace::Lin_Rec2020;
  } else if (tok.str() == "acescg") {  // Alternative ACES CG naming
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "lin_ap1") {  // Linear AP1 (same as ACEScg)
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "aces2065-1") {  // ACES 2065-1
    (*cty) = ColorSpace::ACES2065_1;
  } else if (tok.str() == "ocio") {
    (*cty) = ColorSpace::OCIO;
  } else if (tok.str() == "lin_displayp3") {
    (*cty) = ColorSpace::Lin_DisplayP3;
  } else if (tok.str() == "srgb_displayp3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;

    //
    // seen in Apple's USDZ model(or OCIO?)
    //

  } else if (tok.str() == "ACES - ACEScg") {
    (*cty) = ColorSpace::Lin_ACEScg;
  } else if (tok.str() == "Input - Texture - sRGB - Display P3") {
    (*cty) = ColorSpace::sRGB_DisplayP3;
  } else if (tok.str() == "Input - Texture - sRGB - sRGB") {
    (*cty) = ColorSpace::sRGB;
  } else if (tok.str() == "custom") {
    (*cty) = ColorSpace::Custom;
  } else {
    return false;
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
