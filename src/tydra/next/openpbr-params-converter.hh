// SPDX-License-Identifier: Apache-2.0
// Convert tydra-next render materials into the shared LightRT/OpenPBR block.
#pragma once

#include <initializer_list>

#include "render-data.hh"
#include "tydra/openpbr-params.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

inline void CopyShaderParam3ToOpenPBR(const ShaderParam &param, float dst[3]) {
  dst[0] = param.value.x;
  dst[1] = param.value.y;
  dst[2] = param.value.z;
}

inline bool ShaderParamHasTexture(const ShaderParam &param) {
  return param.texture_id >= 0;
}

inline bool AnyShaderParamHasTexture(
    std::initializer_list<const ShaderParam *> params) {
  for (const ShaderParam *p : params) {
    if (p && ShaderParamHasTexture(*p)) return true;
  }
  return false;
}

inline bool BuildLightRtOpenPBRParams(
    const RenderMaterial &rm, tinyusdz::tydra::LightRtOpenPBRParams *out) {
  if (!out) return false;
  tinyusdz::tydra::LightRtOpenPBRParams p;
  if (rm.shader_type == RenderMaterial::ShaderType::PreviewSurface &&
      rm.preview_surface) {
    const PreviewSurfaceShader &s = *rm.preview_surface;
    CopyShaderParam3ToOpenPBR(s.diffuse_color, p.baseColor);
    CopyShaderParam3ToOpenPBR(s.specular_color, p.specularColor);
    p.metalness = s.metallic.value.x;
    p.specularRoughness = s.roughness.value.x;
    p.specularIor = s.ior.value.x > 0.0f ? s.ior.value.x : 1.5f;
    p.coatWeight = s.clearcoat.value.x;
    p.coatRoughness = s.clearcoat_roughness.value.x;
    p.opacity = s.opacity.value.x;
    CopyShaderParam3ToOpenPBR(s.normal, p.normal);
    CopyShaderParam3ToOpenPBR(s.emissive_color, p.emissionColor);
    p.emission = (p.emissionColor[0] > 0.0f || p.emissionColor[1] > 0.0f ||
                  p.emissionColor[2] > 0.0f)
                     ? 1.0f
                     : 0.0f;
    p.hasTextureInputs = AnyShaderParamHasTexture({
        &s.diffuse_color, &s.emissive_color, &s.specular_color, &s.metallic,
        &s.roughness, &s.clearcoat, &s.clearcoat_roughness, &s.opacity,
        &s.normal, &s.displacement, &s.occlusion});
    p.hasNormalInput = ShaderParamHasTexture(s.normal);
  } else if (rm.shader_type == RenderMaterial::ShaderType::OpenPBR &&
             rm.openpbr) {
    const OpenPBRSurfaceShader &s = *rm.openpbr;
    p.baseWeight = s.base_weight.value.x;
    CopyShaderParam3ToOpenPBR(s.base_color, p.baseColor);
    p.diffuseRoughness = s.base_roughness.value.x;
    p.metalness = s.base_metalness.value.x;
    p.specularWeight = s.specular_weight.value.x;
    CopyShaderParam3ToOpenPBR(s.specular_color, p.specularColor);
    p.specularRoughness = s.specular_roughness.value.x;
    p.specularIor = s.specular_ior.value.x > 0.0f ? s.specular_ior.value.x : 1.5f;
    p.transmission = s.transmission_weight.value.x;
    CopyShaderParam3ToOpenPBR(s.transmission_color, p.transmissionColor);
    p.transmissionDepth = s.transmission_depth.value.x;
    p.subsurface = s.subsurface_weight.value.x;
    CopyShaderParam3ToOpenPBR(s.subsurface_color, p.subsurfaceColor);
    CopyShaderParam3ToOpenPBR(s.subsurface_radius, p.subsurfaceRadius);
    p.coatWeight = s.coat_weight.value.x;
    CopyShaderParam3ToOpenPBR(s.coat_color, p.coatColor);
    p.coatRoughness = s.coat_roughness.value.x;
    p.coatIor = s.coat_ior.value.x > 0.0f ? s.coat_ior.value.x : 1.5f;
    p.sheenWeight = s.sheen_weight.value.x;
    CopyShaderParam3ToOpenPBR(s.sheen_color, p.sheenColor);
    p.sheenRoughness = s.sheen_roughness.value.x;
    p.emission = s.emission_luminance.value.x;
    CopyShaderParam3ToOpenPBR(s.emission_color, p.emissionColor);
    p.opacity = s.opacity.value.x;
    CopyShaderParam3ToOpenPBR(s.normal, p.normal);
    p.hasTextureInputs = AnyShaderParamHasTexture({
        &s.base_weight, &s.base_color, &s.base_roughness, &s.base_metalness,
        &s.specular_weight, &s.specular_color, &s.specular_roughness,
        &s.specular_ior, &s.transmission_weight, &s.transmission_color,
        &s.transmission_depth, &s.subsurface_weight, &s.subsurface_color,
        &s.subsurface_radius, &s.coat_weight, &s.coat_color,
        &s.coat_roughness, &s.coat_ior, &s.sheen_weight, &s.sheen_color,
        &s.sheen_roughness, &s.emission_luminance, &s.emission_color,
        &s.opacity, &s.normal, &s.displacement});
    p.hasNormalInput = ShaderParamHasTexture(s.normal);
  } else {
    return false;
  }
  tinyusdz::tydra::ClampLightRtOpenPBRParams(&p);
  *out = p;
  return true;
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
