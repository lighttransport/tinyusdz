// SPDX-License-Identifier: Apache-2.0
#include "lightrt_mtlx_bridge.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "external/jsonhpp/nlohmann/json.hpp"
#include "image-loader.hh"

extern "C" {
#include "external/lightrt/mtlxrender/mtlx_doc.h"
#include "external/lightrt/mtlxrender/mtlx_eval.h"
#include "external/lightrt/mtlxrender/texture.h"
}

namespace tusdview {
namespace {

const DrawMaterialParamCPU* FindParam(const DrawMaterialCPU& mat,
                                      const char* shader,
                                      const char* name) {
  for (const DrawMaterialParamCPU& p : mat.params) {
    if (p.shader == shader && p.name == name) return &p;
  }
  return nullptr;
}

const DrawMaterialParamCPU* FindAny(const DrawMaterialCPU& mat,
                                    std::initializer_list<const char*> shaders,
                                    std::initializer_list<const char*> names) {
  for (const char* shader : shaders) {
    for (const char* name : names) {
      if (const DrawMaterialParamCPU* p = FindParam(mat, shader, name)) return p;
    }
  }
  return nullptr;
}

bool FloatParam(const DrawMaterialCPU& mat,
                std::initializer_list<const char*> shaders,
                std::initializer_list<const char*> names, float* out) {
  const DrawMaterialParamCPU* p = FindAny(mat, shaders, names);
  if (!p) return false;
  *out = p->value[0];
  return true;
}

bool Vec3Param(const DrawMaterialCPU& mat,
               std::initializer_list<const char*> shaders,
               std::initializer_list<const char*> names, float out[3]) {
  const DrawMaterialParamCPU* p = FindAny(mat, shaders, names);
  if (!p) return false;
  out[0] = p->value[0];
  out[1] = p->value[1];
  out[2] = p->value[2];
  return true;
}

bool ParamHasTexture(const DrawMaterialCPU& mat,
                     std::initializer_list<const char*> shaders,
                     std::initializer_list<const char*> names) {
  for (const char* shader : shaders) {
    for (const char* name : names) {
      const DrawMaterialParamCPU* p = FindParam(mat, shader, name);
      if (p && (p->texture >= 0 || p->renderTexture >= 0)) return true;
    }
  }
  return false;
}

bool HasTextureInput(const DrawMaterialCPU& mat) {
  for (const DrawMaterialParamCPU& p : mat.params) {
    if (p.texture >= 0 || p.renderTexture >= 0) return true;
  }
  return false;
}

bool IsNeutralNormalValue(const DrawMaterialParamCPU& p) {
  return std::fabs(p.value[0]) <= 1.0e-6f &&
         std::fabs(p.value[1]) <= 1.0e-6f &&
         std::fabs(p.value[2] - 1.0f) <= 1.0e-6f;
}

bool IsNormalParam(const DrawMaterialParamCPU& p) {
  const bool shader =
      p.shader == "OpenPBRSurface" || p.shader == "UsdPreviewSurface";
  const bool name = p.name == "normal" || p.name == "geometry_normal" ||
                    p.name == "coat_normal";
  return shader && name;
}

bool HasNormalInput(const DrawMaterialCPU& mat) {
  for (const DrawMaterialParamCPU& p : mat.params) {
    if (!IsNormalParam(p)) continue;
    if (p.texture >= 0 || p.renderTexture >= 0 || !IsNeutralNormalValue(p)) {
      return true;
    }
  }
  return false;
}

float Luminance(const float rgb[3]) {
  return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

void ClampLightRtParams(tydra::LightRtOpenPBRParams* p) {
  tinyusdz::tydra::ClampLightRtOpenPBRParams(p);
}

void Store4(const float src[4], float* dst) {
  for (int i = 0; i < 4; ++i) dst[i] = src[i];
}

void StoreUvCompact(const DrawUvXformCPU& uv, float* dst) {
  dst[0] = uv.m00;
  dst[1] = uv.m01;
  dst[2] = uv.tx;
  dst[3] = uv.m10;
  dst[4] = uv.m11;
  dst[5] = uv.ty;
}

void StoreUvVec4Rows(const DrawUvXformCPU& uv, float* dst) {
  dst[0] = uv.m00;
  dst[1] = uv.m01;
  dst[2] = uv.tx;
  dst[3] = 0.0f;
  dst[4] = uv.m10;
  dst[5] = uv.m11;
  dst[6] = uv.ty;
  dst[7] = 0.0f;
}

void BakeUsdPreviewSurface(const DrawMaterialCPU& mat, tydra::LightRtOpenPBRParams* p) {
  // Seed from the material's DIRECT fields first. The --next loader
  // (next_scene_loader.cc) sets baseColor/metallic/roughness/specularColor/ior/
  // alpha/emissive directly and does NOT populate mat.params, so the param
  // lookups below leave p at its constructor defaults (gray 0.8, etc.), and
  // BakeLightRtOpenPBR then copies those defaults back OVER the loader's values
  // -- graying out every untextured constant-color material on the next path.
  // The legacy path sets both the direct fields AND the params, so its lookups
  // override this seed with identical values (no behavior change there).
  p->baseColor[0] = mat.baseColor[0];
  p->baseColor[1] = mat.baseColor[1];
  p->baseColor[2] = mat.baseColor[2];
  p->specularColor[0] = mat.specularColor[0];
  p->specularColor[1] = mat.specularColor[1];
  p->specularColor[2] = mat.specularColor[2];
  p->metalness = mat.metallic;
  p->specularRoughness = mat.roughness;
  p->specularIor = mat.ior;
  p->opacity = mat.alpha;
  p->emissionColor[0] = mat.emissive[0];
  p->emissionColor[1] = mat.emissive[1];
  p->emissionColor[2] = mat.emissive[2];
  p->emission = (Luminance(p->emissionColor) > 0.0f) ? 1.0f : 0.0f;

  Vec3Param(mat, {"UsdPreviewSurface"}, {"diffuseColor"}, p->baseColor);
  Vec3Param(mat, {"UsdPreviewSurface"}, {"specularColor"}, p->specularColor);
  FloatParam(mat, {"UsdPreviewSurface"}, {"metallic"}, &p->metalness);
  FloatParam(mat, {"UsdPreviewSurface"}, {"roughness"}, &p->specularRoughness);
  FloatParam(mat, {"UsdPreviewSurface"}, {"ior"}, &p->specularIor);
  FloatParam(mat, {"UsdPreviewSurface"}, {"clearcoat"}, &p->coatWeight);
  FloatParam(mat, {"UsdPreviewSurface"}, {"clearcoatRoughness"}, &p->coatRoughness);
  FloatParam(mat, {"UsdPreviewSurface"}, {"opacity"}, &p->opacity);
  Vec3Param(mat, {"UsdPreviewSurface"}, {"normal"}, p->normal);
  if (Vec3Param(mat, {"UsdPreviewSurface"}, {"emissiveColor"}, p->emissionColor)) {
    p->emission = (Luminance(p->emissionColor) > 0.0f) ? 1.0f : 0.0f;
  }

  // Params retain the authored constant even when Tydra also resolved a live
  // UVTexture connection. The raster fields above have already neutralized
  // those constants; do the same for the canonical LightRT block, otherwise
  // Vulkan ray query/CUDA multiply the texel by the stale fallback (most
  // visibly metallic=0 and emissiveColor=(0,0,0)).
  if (mat.baseColorTex >= 0) {
    p->baseColor[0] = p->baseColor[1] = p->baseColor[2] = 1.0f;
  }
  if (mat.metallicTex >= 0) p->metalness = 1.0f;
  if (mat.roughnessTex >= 0) p->specularRoughness = 1.0f;
  if (mat.emissiveTex >= 0) {
    p->emissionColor[0] = p->emissionColor[1] = p->emissionColor[2] = 1.0f;
    p->emission = 1.0f;
  }
  if (mat.opacityTex >= 0) p->opacity = 1.0f;
  if (mat.specularColorTex >= 0) {
    p->specularColor[0] = p->specularColor[1] = p->specularColor[2] = 1.0f;
  }
  if (mat.coatWeightTex >= 0) p->coatWeight = 1.0f;
  if (mat.coatColorTex >= 0) {
    p->coatColor[0] = p->coatColor[1] = p->coatColor[2] = 1.0f;
  }
  if (mat.coatRoughnessTex >= 0) p->coatRoughness = 1.0f;
}

void BakeOpenPBRSurface(const DrawMaterialCPU& mat, tydra::LightRtOpenPBRParams* p) {
  FloatParam(mat, {"OpenPBRSurface"}, {"base_weight"}, &p->baseWeight);
  Vec3Param(mat, {"OpenPBRSurface"}, {"base_color"}, p->baseColor);
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"base_color"})) {
    p->baseColor[0] = 1.0f;
    p->baseColor[1] = 1.0f;
    p->baseColor[2] = 1.0f;
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"base_diffuse_roughness"},
             &p->diffuseRoughness);
  FloatParam(mat, {"OpenPBRSurface"}, {"base_roughness"},
             &p->specularRoughness);
  FloatParam(mat, {"OpenPBRSurface"}, {"base_metalness"}, &p->metalness);
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"base_metalness"})) {
    p->metalness = 1.0f;
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"specular_weight"}, &p->specularWeight);
  Vec3Param(mat, {"OpenPBRSurface"}, {"specular_color"}, p->specularColor);
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"specular_color"})) {
    p->specularColor[0] = p->specularColor[1] = p->specularColor[2] = 1.0f;
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"specular_roughness"},
             &p->specularRoughness);
  FloatParam(mat, {"OpenPBRSurface"}, {"base_roughness"},
             &p->specularRoughness);
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"base_roughness",
                                                "specular_roughness"})) {
    p->specularRoughness = 1.0f;
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"specular_ior"}, &p->specularIor);
  FloatParam(mat, {"OpenPBRSurface"}, {"specular_anisotropy"},
             &p->specularAnisotropy);
  FloatParam(mat, {"OpenPBRSurface"}, {"specular_rotation"},
             &p->specularRotation);
  FloatParam(mat, {"OpenPBRSurface"}, {"specular_roughness_anisotropy"},
             &p->specularRoughnessAnisotropy);
  FloatParam(mat, {"OpenPBRSurface"}, {"transmission_weight"},
             &p->transmission);
  Vec3Param(mat, {"OpenPBRSurface"}, {"transmission_color"},
            p->transmissionColor);
  FloatParam(mat, {"OpenPBRSurface"}, {"transmission_depth"},
             &p->transmissionDepth);
  Vec3Param(mat, {"OpenPBRSurface"}, {"transmission_scatter"},
            p->transmissionScatter);
  FloatParam(mat, {"OpenPBRSurface"}, {"transmission_scatter_anisotropy"},
             &p->transmissionScatterAnisotropy);
  FloatParam(mat, {"OpenPBRSurface"}, {"transmission_dispersion"},
             &p->transmissionDispersion);
  FloatParam(mat, {"OpenPBRSurface"}, {"transmission_dispersion_abbe_number"},
             &p->transmissionDispersionAbbeNumber);
  FloatParam(mat, {"OpenPBRSurface"}, {"transmission_dispersion_scale"},
             &p->transmissionDispersionScale);
  FloatParam(mat, {"OpenPBRSurface"}, {"subsurface_weight"}, &p->subsurface);
  Vec3Param(mat, {"OpenPBRSurface"}, {"subsurface_color"}, p->subsurfaceColor);
  float radius = p->subsurfaceRadius[0];
  if (FloatParam(mat, {"OpenPBRSurface"}, {"subsurface_radius"}, &radius)) {
    p->subsurfaceRadius[0] = radius;
    p->subsurfaceRadius[1] = radius;
    p->subsurfaceRadius[2] = radius;
  }
  float radiusScale[3]{1.0f, 1.0f, 1.0f};
  if (Vec3Param(mat, {"OpenPBRSurface"}, {"subsurface_radius_scale"},
                radiusScale)) {
    p->subsurfaceRadius[0] *= radiusScale[0];
    p->subsurfaceRadius[1] *= radiusScale[1];
    p->subsurfaceRadius[2] *= radiusScale[2];
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"subsurface_scale"}, &p->subsurfaceScale);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_weight"}, &p->coatWeight);
  Vec3Param(mat, {"OpenPBRSurface"}, {"coat_color"}, p->coatColor);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_roughness"}, &p->coatRoughness);
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"coat_weight"})) {
    p->coatWeight = 1.0f;
  }
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"coat_color"})) {
    p->coatColor[0] = p->coatColor[1] = p->coatColor[2] = 1.0f;
  }
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"coat_roughness"})) {
    p->coatRoughness = 1.0f;
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_ior"}, &p->coatIor);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_anisotropy"}, &p->coatAnisotropy);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_rotation"}, &p->coatRotation);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_affect_color"}, &p->coatAffectColor);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_affect_roughness"},
             &p->coatAffectRoughness);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_roughness_anisotropy"},
             &p->coatRoughnessAnisotropy);
  FloatParam(mat, {"OpenPBRSurface"}, {"coat_darkening"}, &p->coatDarkening);
  FloatParam(mat, {"OpenPBRSurface"}, {"sheen_weight", "fuzz_weight"},
             &p->sheenWeight);
  Vec3Param(mat, {"OpenPBRSurface"}, {"sheen_color", "fuzz_color"},
            p->sheenColor);
  FloatParam(mat, {"OpenPBRSurface"}, {"sheen_roughness", "fuzz_roughness"},
             &p->sheenRoughness);
  FloatParam(mat, {"OpenPBRSurface"}, {"thin_film_weight"}, &p->thinFilmWeight);
  if (FloatParam(mat, {"OpenPBRSurface"}, {"thin_film_thickness"},
                 &p->thinFilmThicknessNm)) {
    // OpenPBR and the LightRT evaluator both use nanometers for film
    // thickness. Keep the canonical value unchanged; multiplying here would
    // turn the documented 450 nm test film into a 450,000 nm layer.
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"thin_film_ior"}, &p->thinFilmIor);
  FloatParam(mat, {"OpenPBRSurface"}, {"emission_luminance"}, &p->emission);
  Vec3Param(mat, {"OpenPBRSurface"}, {"emission_color"}, p->emissionColor);
  if (ParamHasTexture(mat, {"OpenPBRSurface"}, {"emission_color"})) {
    p->emissionColor[0] = 1.0f;
    p->emissionColor[1] = 1.0f;
    p->emissionColor[2] = 1.0f;
  }
  FloatParam(mat, {"OpenPBRSurface"}, {"geometry_opacity", "opacity"},
             &p->opacity);
  if (ParamHasTexture(mat, {"OpenPBRSurface"},
                      {"geometry_opacity", "opacity"})) {
    p->opacity = 1.0f;
  }
  Vec3Param(mat, {"OpenPBRSurface"}, {"geometry_normal", "normal"}, p->normal);
}

void CopyV3(const v3& src, float dst[3]) {
  dst[0] = src.x;
  dst[1] = src.y;
  dst[2] = src.z;
}

void CopyLightRtEval(const OpenPBRParams& src, DrawLightRtOpenPBRCPU* dst) {
  dst->baseWeight = src.base_weight;
  CopyV3(src.base_color, dst->baseColor);
  dst->diffuseRoughness = src.diffuse_roughness;
  dst->metalness = src.metalness;
  dst->specularWeight = src.specular_weight;
  CopyV3(src.specular_color, dst->specularColor);
  dst->specularRoughness = src.specular_roughness;
  dst->specularIor = src.specular_ior;
  dst->transmission = src.transmission;
  CopyV3(src.transmission_color, dst->transmissionColor);
  dst->transmissionDepth = src.transmission_depth;
  CopyV3(src.transmission_scatter, dst->transmissionScatter);
  dst->transmissionScatterAnisotropy = src.transmission_scatter_anisotropy;
  dst->subsurface = src.subsurface;
  CopyV3(src.subsurface_color, dst->subsurfaceColor);
  CopyV3(src.subsurface_radius, dst->subsurfaceRadius);
  dst->subsurfaceScale = src.subsurface_scale;
  dst->coatWeight = src.coat_weight;
  CopyV3(src.coat_color, dst->coatColor);
  dst->coatRoughness = src.coat_roughness;
  dst->coatIor = src.coat_ior;
  dst->sheenWeight = src.sheen_weight;
  CopyV3(src.sheen_color, dst->sheenColor);
  dst->sheenRoughness = src.sheen_roughness;
  dst->thinFilmWeight = src.thin_film_weight;
  dst->thinFilmThicknessNm = src.thin_film_thickness;
  dst->thinFilmIor = src.thin_film_ior;
  dst->emission = src.emission;
  CopyV3(src.emission_color, dst->emissionColor);
  CopyV3(src.normal, dst->normal);
  dst->opacity = src.opacity;
}

std::string OpenPBREvalInputName(const std::string& name);
std::string NormalizeMtlxCategory(const std::string& category,
                                  const std::string& type);

std::set<std::string> DirectTextureInputs(const DrawMaterialCPU& mat) {
  std::set<std::string> deps;
  for (const DrawMaterialParamCPU& p : mat.params) {
    if (p.shader != "OpenPBRSurface") continue;
    if (p.texture < 0 && p.renderTexture < 0) continue;
    deps.insert(OpenPBREvalInputName(p.name));
  }
  return deps;
}

bool TextureDepsIncludeNormal(const std::set<std::string>& textureDeps) {
  return textureDeps.find("geometry_normal") != textureDeps.end() ||
         textureDeps.find("geometry_coat_normal") != textureDeps.end();
}

void MergeGraphParamsPreservingTextureDeps(
    const DrawLightRtOpenPBRCPU& graph,
    const std::set<std::string>& textureDeps,
    DrawLightRtOpenPBRCPU* dst) {
  if (!dst) return;
  auto safe = [&](const char* name) {
    return textureDeps.find(name) == textureDeps.end();
  };
  auto copy3 = [](const float src[3], float dst3[3]) {
    dst3[0] = src[0];
    dst3[1] = src[1];
    dst3[2] = src[2];
  };

  if (safe("base_weight")) dst->baseWeight = graph.baseWeight;
  if (safe("base_color")) copy3(graph.baseColor, dst->baseColor);
  if (safe("base_metalness")) dst->metalness = graph.metalness;
  if (safe("base_diffuse_roughness")) {
    dst->diffuseRoughness = graph.diffuseRoughness;
  }
  if (safe("specular_weight")) dst->specularWeight = graph.specularWeight;
  if (safe("specular_color")) copy3(graph.specularColor, dst->specularColor);
  if (safe("specular_roughness")) {
    dst->specularRoughness = graph.specularRoughness;
  }
  if (safe("specular_ior")) dst->specularIor = graph.specularIor;
  if (safe("transmission_weight")) dst->transmission = graph.transmission;
  if (safe("transmission_color")) {
    copy3(graph.transmissionColor, dst->transmissionColor);
  }
  if (safe("transmission_depth")) dst->transmissionDepth = graph.transmissionDepth;
  if (safe("transmission_scatter")) {
    copy3(graph.transmissionScatter, dst->transmissionScatter);
  }
  if (safe("transmission_scatter_anisotropy")) {
    dst->transmissionScatterAnisotropy =
        graph.transmissionScatterAnisotropy;
  }
  if (safe("subsurface_weight")) dst->subsurface = graph.subsurface;
  if (safe("subsurface_color")) {
    copy3(graph.subsurfaceColor, dst->subsurfaceColor);
  }
  if (safe("subsurface_radius")) {
    copy3(graph.subsurfaceRadius, dst->subsurfaceRadius);
  }
  if (safe("subsurface_radius_scale")) {
    dst->subsurfaceScale = graph.subsurfaceScale;
  }
  if (safe("coat_weight")) dst->coatWeight = graph.coatWeight;
  if (safe("coat_color")) copy3(graph.coatColor, dst->coatColor);
  if (safe("coat_roughness")) dst->coatRoughness = graph.coatRoughness;
  if (safe("coat_ior")) dst->coatIor = graph.coatIor;
  if (safe("fuzz_weight")) dst->sheenWeight = graph.sheenWeight;
  if (safe("fuzz_color")) copy3(graph.sheenColor, dst->sheenColor);
  if (safe("fuzz_roughness")) dst->sheenRoughness = graph.sheenRoughness;
  if (safe("thin_film_weight")) dst->thinFilmWeight = graph.thinFilmWeight;
  if (safe("thin_film_thickness")) {
    dst->thinFilmThicknessNm = graph.thinFilmThicknessNm;
  }
  if (safe("thin_film_ior")) dst->thinFilmIor = graph.thinFilmIor;
  if (safe("emission_luminance")) dst->emission = graph.emission;
  if (safe("emission_color")) copy3(graph.emissionColor, dst->emissionColor);
  if (safe("geometry_opacity")) dst->opacity = graph.opacity;
  if (safe("geometry_normal")) copy3(graph.normal, dst->normal);
}

std::string XmlEscape(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  for (char c : src) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default: out += c; break;
    }
  }
  return out;
}

bool IsMtlxTypeName(const std::string& s) {
  return s == "float" || s == "color3" || s == "color4" ||
         s == "vector2" || s == "vector3" || s == "vector4" ||
         s == "integer" || s == "boolean" || s == "string" ||
         s == "filename";
}

std::string NormalizeMtlxType(const std::string& type) {
  if (IsMtlxTypeName(type)) return type;
  if (type == "float2") return "vector2";
  if (type == "float3") return "vector3";
  if (type == "float4") return "vector4";
  if (type == "color3f") return "color3";
  if (type == "color4f") return "color4";
  if (type == "asset") return "filename";
  if (type.rfind("ND_", 0) == 0) {
    const size_t last = type.rfind('_');
    if (last != std::string::npos && last + 1 < type.size()) {
      const std::string suffix = type.substr(last + 1);
      if (IsMtlxTypeName(suffix)) return suffix;
    }
  }
  return "float";
}

std::string TypeForParam(DrawMaterialParamType type) {
  switch (type) {
    case DrawMaterialParamType::Vec2: return "vector2";
    case DrawMaterialParamType::Vec3: return "color3";
    case DrawMaterialParamType::Vec4: return "color4";
    case DrawMaterialParamType::Float:
    default: return "float";
  }
}

int ComponentCount(DrawMaterialParamType type) {
  switch (type) {
    case DrawMaterialParamType::Vec2: return 2;
    case DrawMaterialParamType::Vec3: return 3;
    case DrawMaterialParamType::Vec4: return 4;
    case DrawMaterialParamType::Float:
    default: return 1;
  }
}

std::string ValueForParam(const DrawMaterialParamCPU& p) {
  std::stringstream ss;
  const int n = ComponentCount(p.type);
  for (int i = 0; i < n; ++i) {
    if (i) ss << ", ";
    ss << p.value[i];
  }
  return ss.str();
}

std::string JsonString(const nlohmann::json& obj, const char* key,
                       const std::string& fallback = std::string()) {
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) return fallback;
  return it->get<std::string>();
}

bool IsTextureNodeCategory(const std::string& category) {
  return category == "image" || category == "tiledimage" ||
         category == "hextiledimage" || category == "gltf_image" ||
         category == "gltf_colorimage";
}

bool JsonNodeHasFileInput(const nlohmann::json& node) {
  const auto inputsIt = node.find("inputs");
  if (inputsIt == node.end() || !inputsIt->is_array()) return false;
  for (const nlohmann::json& input : *inputsIt) {
    const std::string name = JsonString(input, "name");
    const std::string type = NormalizeMtlxType(JsonString(input, "type"));
    if (name == "file" || type == "filename") return true;
  }
  return false;
}

struct GraphTextureDeps {
  bool parsed{false};
  bool hasTextureNodes{false};
  std::set<std::string> textureInputs;
};

GraphTextureDeps AnalyzeMaterialXJsonTextureDeps(const std::string& jsonText) {
  GraphTextureDeps result;
  if (jsonText.empty()) return result;
  nlohmann::json j = nlohmann::json::parse(
      jsonText.begin(), jsonText.end(), nullptr, false);
  if (j.is_discarded() || !j.is_object()) return result;
  const auto ngIt = j.find("nodegraph");
  if (ngIt == j.end() || !ngIt->is_object()) return result;
  result.parsed = true;
  const nlohmann::json& ng = *ngIt;

  std::map<std::string, nlohmann::json> nodes;
  const auto nodesIt = ng.find("nodes");
  if (nodesIt != ng.end() && nodesIt->is_array()) {
    for (const nlohmann::json& node : *nodesIt) {
      const std::string name = JsonString(node, "name");
      if (!name.empty()) nodes[name] = node;
      const std::string category = NormalizeMtlxCategory(
          JsonString(node, "category"), JsonString(node, "type"));
      if (IsTextureNodeCategory(category) ||
          (category == "normalmap" && JsonNodeHasFileInput(node))) {
        result.hasTextureNodes = true;
      }
    }
  }

  std::map<std::string, std::string> outputs;
  const auto outputsIt = ng.find("outputs");
  if (outputsIt != ng.end() && outputsIt->is_array()) {
    for (const nlohmann::json& output : *outputsIt) {
      const std::string name = JsonString(output, "name");
      const std::string nodeName = JsonString(output, "nodename");
      if (!name.empty() && !nodeName.empty()) outputs[name] = nodeName;
    }
  }

  std::map<std::string, bool> memo;
  std::set<std::string> visiting;
  std::function<bool(const std::string&)> nodeDependsOnTexture =
      [&](const std::string& nodeName) -> bool {
        const auto memoIt = memo.find(nodeName);
        if (memoIt != memo.end()) return memoIt->second;
        if (visiting.count(nodeName)) return false;
        visiting.insert(nodeName);

        bool dep = false;
        const auto nodeIt = nodes.find(nodeName);
        if (nodeIt != nodes.end()) {
          const nlohmann::json& node = nodeIt->second;
          const std::string category = NormalizeMtlxCategory(
              JsonString(node, "category"), JsonString(node, "type"));
          dep = IsTextureNodeCategory(category) ||
                (category == "normalmap" && JsonNodeHasFileInput(node));
          const auto inputsIt = node.find("inputs");
          if (!dep && inputsIt != node.end() && inputsIt->is_array()) {
            for (const nlohmann::json& input : *inputsIt) {
              const std::string srcNode = JsonString(input, "nodename");
              if (!srcNode.empty() && nodeDependsOnTexture(srcNode)) {
                dep = true;
                break;
              }
            }
          }
        }

        visiting.erase(nodeName);
        memo[nodeName] = dep;
        return dep;
      };

  const auto connIt = j.find("connections");
  if (connIt != j.end() && connIt->is_array()) {
    for (const nlohmann::json& c : *connIt) {
      const std::string input = OpenPBREvalInputName(JsonString(c, "input"));
      const std::string output = JsonString(c, "output");
      if (input.empty() || output.empty()) continue;
      const auto outIt = outputs.find(output);
      if (outIt != outputs.end() && nodeDependsOnTexture(outIt->second)) {
        result.textureInputs.insert(input);
      }
    }
  }
  return result;
}

std::string JsonValueToMtlxString(const nlohmann::json& v) {
  std::stringstream ss;
  if (v.is_number_float() || v.is_number_integer() || v.is_number_unsigned()) {
    ss << v.get<double>();
  } else if (v.is_boolean()) {
    ss << (v.get<bool>() ? "true" : "false");
  } else if (v.is_string()) {
    ss << v.get<std::string>();
  } else if (v.is_array()) {
    for (size_t i = 0; i < v.size(); ++i) {
      if (i) ss << ", ";
      ss << JsonValueToMtlxString(v[i]);
    }
  }
  return ss.str();
}

std::string InferTypeFromJsonValue(const nlohmann::json& v) {
  if (v.is_boolean()) return "boolean";
  if (v.is_string()) return "string";
  if (v.is_array()) {
    if (v.size() == 2) return "vector2";
    if (v.size() == 3) return "color3";
    if (v.size() == 4) return "color4";
  }
  return "float";
}

std::string OpenPBREvalInputName(const std::string& name) {
  // Standard Surface and common interchange graphs use shorter parameter
  // names. The runtime bake target is OpenPBR, so normalize these aliases at
  // the graph boundary instead of silently dropping connected inputs.
  if (name == "roughness") return "specular_roughness";
  if (name == "metalness") return "base_metalness";
  if (name == "specular") return "specular_weight";
  if (name == "transmission") return "transmission_weight";
  if (name == "subsurface") return "subsurface_weight";
  if (name == "coat") return "coat_weight";
  if (name == "emission") return "emission_luminance";
  if (name == "base_roughness") return "specular_roughness";
  if (name == "opacity") return "geometry_opacity";
  if (name == "normal") return "geometry_normal";
  if (name == "tangent") return "geometry_tangent";
  if (name == "coat_normal") return "geometry_coat_normal";
  if (name == "coat_tangent") return "geometry_coat_tangent";
  return name;
}

std::string NormalizeMtlxCategory(const std::string& category,
                                  const std::string& type) {
  if (category == "MaterialXMultiply") return "multiply";
  if (category == "MaterialXMix") return "mix";
  if (category == "MaterialXNoise") return "noise3d";
  if (category == "MaterialXConstant") return "constant";

  if (category.rfind("ND_", 0) == 0) {
    std::string stem = category.substr(3);
    const std::string normalized_type = NormalizeMtlxType(type);
    if (!normalized_type.empty()) {
      const std::string suffix = "_" + normalized_type;
      if (stem.size() > suffix.size() &&
          stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
        stem.resize(stem.size() - suffix.size());
      }
    }
    if (stem == "gltf_colorimage") return "gltf_colorimage";
    if (stem == "gltf_image") return "gltf_image";
    if (stem == "gltf_normalmap") return "gltf_normalmap";
    // Swizzle nodedef names carry both input and output types (for example
    // ND_swizzle_color4_color3). Removing only the output suffix leaves the
    // input type in the stem; the runtime operation itself is type-agnostic.
    if (stem.rfind("swizzle_", 0) == 0) return "swizzle";
    if (stem == "open_pbr_surface_surfaceshader") return "open_pbr_surface";
    if (stem == "standard_surface_surfaceshader") return "standard_surface";
    if (stem == "UsdPreviewSurface_surfaceshader") return "UsdPreviewSurface";
    return stem;
  }

  return category;
}

void EmitXmlInput(std::stringstream& ss, const nlohmann::json& input) {
  const std::string name = JsonString(input, "name");
  if (name.empty()) return;
  std::string type = NormalizeMtlxType(JsonString(input, "type"));
  const auto valueIt = input.find("value");
  if (type == "float" && valueIt != input.end()) {
    type = NormalizeMtlxType(InferTypeFromJsonValue(*valueIt));
  }

  ss << "      <input name=\"" << XmlEscape(name)
     << "\" type=\"" << XmlEscape(type) << "\"";
  if (valueIt != input.end()) {
    ss << " value=\"" << XmlEscape(JsonValueToMtlxString(*valueIt)) << "\"";
  }
  const std::string nodeName = JsonString(input, "nodename");
  const std::string nodeGraph = JsonString(input, "nodegraph");
  const std::string interfaceName = JsonString(input, "interfacename");
  const std::string output = JsonString(input, "output");
  const std::string colorspace = JsonString(input, "colorspace");
  const std::string channels = JsonString(input, "channels");
  if (!nodeName.empty()) ss << " nodename=\"" << XmlEscape(nodeName) << "\"";
  if (!nodeGraph.empty()) ss << " nodegraph=\"" << XmlEscape(nodeGraph) << "\"";
  if (!interfaceName.empty()) {
    ss << " interfacename=\"" << XmlEscape(interfaceName) << "\"";
  }
  if (!output.empty()) ss << " output=\"" << XmlEscape(output) << "\"";
  if (!colorspace.empty()) ss << " colorspace=\"" << XmlEscape(colorspace) << "\"";
  if (!channels.empty()) ss << " channels=\"" << XmlEscape(channels) << "\"";
  ss << "/>\n";
}

struct MtlxConnection {
  std::string graph;
  std::string output;
  std::string channels;
};

bool BuildMaterialXXmlFromJsonGraph(const DrawMaterialCPU& mat,
                                    std::string* xml,
                                    std::string* err) {
  if (!xml) {
    if (err) *err = "Output XML pointer is null";
    return false;
  }
  if (mat.materialXNodeGraphJson.empty()) {
    if (err) *err = "MaterialX node graph JSON is empty";
    return false;
  }

  nlohmann::json j = nlohmann::json::parse(
      mat.materialXNodeGraphJson.begin(), mat.materialXNodeGraphJson.end(),
      nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    if (err) *err = "Failed to parse MaterialX node graph JSON";
    return false;
  }
  const auto ngIt = j.find("nodegraph");
  if (ngIt == j.end() || !ngIt->is_object()) {
    if (err) *err = "MaterialX JSON has no nodegraph object";
    return false;
  }
  const nlohmann::json& ng = *ngIt;
  const std::string graphName = JsonString(ng, "name", "NG_tusdview");

  std::map<std::string, std::string> outputTypes;
  const auto outputsIt = ng.find("outputs");
  if (outputsIt != ng.end() && outputsIt->is_array()) {
    for (const nlohmann::json& output : *outputsIt) {
      const std::string name = JsonString(output, "name");
      if (name.empty()) continue;
      outputTypes[name] = NormalizeMtlxType(JsonString(output, "type"));
    }
  }

  std::map<std::string, MtlxConnection> connections;
  const auto connIt = j.find("connections");
  if (connIt != j.end() && connIt->is_array()) {
    for (const nlohmann::json& c : *connIt) {
      const std::string input = OpenPBREvalInputName(JsonString(c, "input"));
      const std::string graph = JsonString(c, "nodegraph", graphName);
      const std::string output = JsonString(c, "output");
      if (!input.empty() && !graph.empty() && !output.empty()) {
        connections[input] = {graph, output, JsonString(c, "channels")};
      }
    }
  }
  if (connections.empty()) {
    if (err) *err = "MaterialX JSON has no shader input connections";
    return false;
  }

  std::stringstream ss;
  ss << "<materialx version=\"1.39\">\n";
  ss << "  <nodegraph name=\"" << XmlEscape(graphName) << "\">\n";
  const auto graphInputsIt = ng.find("inputs");
  if (graphInputsIt != ng.end() && graphInputsIt->is_array()) {
    for (const nlohmann::json& input : *graphInputsIt) {
      EmitXmlInput(ss, input);
    }
  }
  const auto nodesIt = ng.find("nodes");
  if (nodesIt != ng.end() && nodesIt->is_array()) {
    for (const nlohmann::json& node : *nodesIt) {
      const std::string type = NormalizeMtlxType(JsonString(node, "type"));
      const std::string category =
          NormalizeMtlxCategory(JsonString(node, "category"), type);
      const std::string name = JsonString(node, "name");
      if (category.empty() || name.empty()) continue;
      ss << "    <" << XmlEscape(category)
         << " name=\"" << XmlEscape(name)
         << "\" type=\"" << XmlEscape(type) << "\">\n";
      const auto inputsIt = node.find("inputs");
      if (inputsIt != node.end() && inputsIt->is_array()) {
        for (const nlohmann::json& input : *inputsIt) {
          EmitXmlInput(ss, input);
        }
      }
      ss << "    </" << XmlEscape(category) << ">\n";
    }
  }
  if (outputsIt != ng.end() && outputsIt->is_array()) {
    for (const nlohmann::json& output : *outputsIt) {
      const std::string name = JsonString(output, "name");
      if (name.empty()) continue;
      const std::string type = NormalizeMtlxType(JsonString(output, "type"));
      ss << "    <output name=\"" << XmlEscape(name)
         << "\" type=\"" << XmlEscape(type) << "\"";
      const std::string nodeName = JsonString(output, "nodename");
      const std::string outName = JsonString(output, "output");
      if (!nodeName.empty()) ss << " nodename=\"" << XmlEscape(nodeName) << "\"";
      if (!outName.empty()) ss << " output=\"" << XmlEscape(outName) << "\"";
      ss << "/>\n";
    }
  }
  ss << "  </nodegraph>\n";

  ss << "  <open_pbr_surface name=\"tusdview_openpbr\" type=\"surfaceshader\">\n";
  std::set<std::string> emitted;
  for (const auto& conn : connections) {
    const std::string type = outputTypes.count(conn.second.output)
                                 ? outputTypes[conn.second.output]
                                 : "float";
    ss << "    <input name=\"" << XmlEscape(conn.first)
       << "\" type=\"" << XmlEscape(type)
       << "\" nodegraph=\"" << XmlEscape(conn.second.graph)
       << "\" output=\"" << XmlEscape(conn.second.output) << "\"";
    if (!conn.second.channels.empty()) {
      ss << " channels=\"" << XmlEscape(conn.second.channels) << "\"";
    }
    ss << "/>\n";
    emitted.insert(conn.first);
  }
  for (const DrawMaterialParamCPU& p : mat.params) {
    if (p.shader != "OpenPBRSurface") continue;
    const std::string name = OpenPBREvalInputName(p.name);
    if (emitted.count(name)) continue;
    // Runtime texture inputs are sampled by the preview shaders; this constant
    // graph bake is only a fallback for texture-free values.
    if (p.texture >= 0 || p.renderTexture >= 0) continue;
    ss << "    <input name=\"" << XmlEscape(name)
       << "\" type=\"" << XmlEscape(TypeForParam(p.type))
       << "\" value=\"" << XmlEscape(ValueForParam(p)) << "\"/>\n";
    emitted.insert(name);
  }
  ss << "  </open_pbr_surface>\n";
  ss << "  <surfacematerial name=\"tusdview_material\" type=\"material\">\n";
  ss << "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
        "nodename=\"tusdview_openpbr\"/>\n";
  ss << "  </surfacematerial>\n";
  ss << "</materialx>\n";

  *xml = ss.str();
  return true;
}

bool EvaluateMaterialXJsonGraphForMaterial(const DrawMaterialCPU& mat,
                                           DrawLightRtOpenPBRCPU* out,
                                           std::string* err) {
  std::string xml;
  if (!BuildMaterialXXmlFromJsonGraph(mat, &xml, err)) return false;
  // MaterialX image filenames are relative to the owning asset. Keep the
  // evaluator's cache rooted at that asset instead of silently evaluating
  // every image through its default input.
  std::string baseDir = ".";
  const size_t slash = mat.absPath.find_last_of("/\\");
  if (slash != std::string::npos) {
    baseDir = mat.absPath.substr(0, slash);
    if (baseDir.empty()) baseDir = ".";
  }

  return EvaluateMaterialXStringToLightRtOpenPBRWithBaseDir(
      xml.c_str(), "tusdview_material", baseDir.c_str(), out, err);
}

int FindSurfaceNode(const MtlxDoc* doc, const char* materialName,
                    std::string* err) {
  if (!doc) return -1;
  if (materialName && materialName[0]) {
    const int matId = mtlx_find_material(doc, materialName);
    if (matId < 0) {
      if (err) *err = std::string("MaterialX material not found: ") + materialName;
      return -1;
    }
    if (doc->mats[matId].surface_node < 0) {
      if (err) *err = std::string("MaterialX material has no surface shader: ") +
                      materialName;
      return -1;
    }
    return doc->mats[matId].surface_node;
  }
  if (doc->nmat > 0 && doc->mats[0].surface_node >= 0) {
    return doc->mats[0].surface_node;
  }
  for (int i = 0; i < doc->nnode; ++i) {
    const char* c = doc->nodes[i].category;
    if (!c) continue;
    if (std::strcmp(c, "open_pbr_surface") == 0 ||
        std::strcmp(c, "standard_surface") == 0 ||
        std::strcmp(c, "UsdPreviewSurface") == 0 ||
        std::strcmp(c, "gltf_pbr") == 0 ||
        std::strcmp(c, "disney_principled") == 0) {
      return i;
    }
  }
  if (err) *err = "MaterialX document has no supported surface shader";
  return -1;
}

bool EvaluateMtlxDocAtUv(const MtlxDoc* doc, int surfaceNode,
                         TextureCache* tex, float u, float v,
                         std::vector<MtlxValue>* memo,
                         std::vector<char>* memoDone,
                         DrawLightRtOpenPBRCPU* out) {
  if (!doc || surfaceNode < 0 || !tex || !memo || !memoDone || !out ||
      memo->size() < static_cast<size_t>(doc->nnode) ||
      memoDone->size() < static_cast<size_t>(doc->nnode)) {
    return false;
  }
  ShadeContext ctx{};
  ctx.doc = doc;
  ctx.tex = tex;
  ctx.uv[0] = u;
  ctx.uv[1] = v;
  ctx.P = v3_make(0.0f, 0.0f, 0.0f);
  ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
  ctx.Ng = v3_make(0.0f, 0.0f, 1.0f);
  ctx.dpdu = v3_make(1.0f, 0.0f, 0.0f);
  ctx.dpdv = v3_make(0.0f, 1.0f, 0.0f);
  ctx.memo = memo->data();
  ctx.memo_done = memoDone->data();
  OpenPBRParams params;
  if (mtlx_eval_surface(&ctx, surfaceNode, &params) != 0) return false;
  CopyLightRtEval(params, out);
  out->hasTextureInputs = false;
  out->hasNormalInput = (std::fabs(out->normal[0]) > 0.0f ||
                         std::fabs(out->normal[1]) > 0.0f ||
                         std::fabs(out->normal[2] - 1.0f) > 1.0e-6f);
  ClampLightRtParams(out);
  return true;
}

}  // namespace

bool EvaluateMaterialXStringToLightRtOpenPBRAtUv(
    const char* xml, const char* materialName, const char* baseDir, float u,
    float v, DrawLightRtOpenPBRCPU* out, std::string* err) {
  if (err) err->clear();
  if (!xml || !xml[0]) {
    if (err) *err = "MaterialX XML string is empty";
    return false;
  }
  if (!out) {
    if (err) *err = "Output pointer is null";
    return false;
  }

  MtlxDoc* doc = mtlx_load_string(xml);
  if (!doc) {
    if (err) *err = "mtlx_load_string failed";
    return false;
  }

  const int surfaceNode = FindSurfaceNode(doc, materialName, err);
  if (surfaceNode < 0) {
    mtlx_free(doc);
    return false;
  }

  TextureCache* tex = texcache_create(baseDir);
  std::vector<MtlxValue> memo(static_cast<size_t>(doc->nnode));
  std::vector<char> memoDone(static_cast<size_t>(doc->nnode), 0);
  DrawLightRtOpenPBRCPU baked;
  const bool ok = EvaluateMtlxDocAtUv(doc, surfaceNode, tex, u, v, &memo,
                                      &memoDone, &baked);
  texcache_free(tex);
  mtlx_free(doc);
  if (!ok) {
    if (err) *err = "mtlx_eval_surface failed";
    return false;
  }
  *out = baked;
  return true;
}

bool EvaluateMaterialXStringToLightRtOpenPBRWithBaseDir(
    const char* xml, const char* materialName, const char* baseDir,
    DrawLightRtOpenPBRCPU* out, std::string* err) {
  return EvaluateMaterialXStringToLightRtOpenPBRAtUv(
      xml, materialName, baseDir, 0.5f, 0.5f, out, err);
}

bool EvaluateMaterialXStringToLightRtOpenPBR(const char* xml,
                                             const char* materialName,
                                             DrawLightRtOpenPBRCPU* out,
                                             std::string* err) {
  return EvaluateMaterialXStringToLightRtOpenPBRWithBaseDir(
      xml, materialName, nullptr, out, err);
}

bool CompileMaterialXGraphRuntime(DrawMaterialCPU* mat, std::string* err) {
  if (err) err->clear();
  if (!mat || mat->materialXNodeGraphJson.empty()) {
    if (err) *err = "MaterialX graph JSON is empty";
    return false;
  }
  MaterialXGraphRuntimeCPU graph;
  nlohmann::json j = nlohmann::json::parse(
      mat->materialXNodeGraphJson.begin(), mat->materialXNodeGraphJson.end(),
      nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    if (err) *err = "MaterialX graph JSON is invalid";
    return false;
  }
  const auto ngIt = j.find("nodegraph");
  if (ngIt == j.end() || !ngIt->is_object()) {
    if (err) *err = "MaterialX graph has no nodegraph";
    return false;
  }
  const nlohmann::json& ng = *ngIt;
  const auto nodesIt = ng.find("nodes");
  if (nodesIt == ng.end() || !nodesIt->is_array()) {
    if (err) *err = "MaterialX graph has no nodes";
    return false;
  }
  // Lower high-arity standard nodes into the bounded primitive runtime ABI.
  // The original node name is retained by the final primitive, so graph
  // outputs and downstream connections require no rewriting.
  nlohmann::json runtimeNodes = nlohmann::json::array();
  auto inputNamed = [](const nlohmann::json& node, const char* name,
                       const nlohmann::json& fallback) {
    const auto it = node.find("inputs");
    if (it != node.end() && it->is_array())
      for (const auto& input : *it)
        if (JsonString(input, "name") == name) return input;
    return fallback;
  };
  auto renamedInput = [](nlohmann::json input, const char* name) {
    input["name"] = name;
    return input;
  };
  auto emitRandomFloat = [&](const std::string& base, nlohmann::json input,
                             nlohmann::json seed, nlohmann::json minimum,
                             nlohmann::json maximum, bool scaleInput) {
    const std::string scaled=base+"__scaled_input",pair=base+"__pair";
    const std::string cell=base+"__cell",span=base+"__span";
    const std::string ranged=base+"__ranged";
    nlohmann::json randomInput = input;
    if (scaleInput) {
      runtimeNodes.push_back({{"name",scaled},{"category","multiply"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(input,"in1"),nlohmann::json{{"name","in2"},{"value",4096}}})}});
      randomInput={{"nodename",scaled}};
    }
    runtimeNodes.push_back({{"name",pair},{"category","combine2"},{"type","vector2"},{"inputs",nlohmann::json::array({renamedInput(randomInput,"in1"),renamedInput(seed,"in2")})}});
    runtimeNodes.push_back({{"name",cell},{"category","cellnoise2d"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","texcoord"},{"nodename",pair}}})}});
    runtimeNodes.push_back({{"name",span},{"category","subtract"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(maximum,"in1"),renamedInput(minimum,"in2")})}});
    runtimeNodes.push_back({{"name",ranged},{"category","multiply"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",cell}},nlohmann::json{{"name","in2"},{"nodename",span}}})}});
    runtimeNodes.push_back({{"name",base},{"category","add"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",ranged}},renamedInput(minimum,"in2")})}});
  };
  for (const nlohmann::json& node : *nodesIt) {
    const std::string name = JsonString(node, "name");
    const std::string type = JsonString(node, "type");
    const std::string cat = NormalizeMtlxCategory(JsonString(node, "category"), type);
    if (cat == "ramp4" && !name.empty()) {
      const std::string st = name + "__st";
      const std::string u = name + "__u";
      const std::string v = name + "__v";
      const std::string top = name + "__top";
      const std::string bottom = name + "__bottom";
      nlohmann::json tc = inputNamed(node, "texcoord",
          {{"name", "texcoord"}, {"nodename", st}});
      if (JsonString(tc, "nodename").empty() && tc.find("value") == tc.end())
        tc["nodename"] = st;
      if (JsonString(tc, "nodename") == st)
        runtimeNodes.push_back({{"name", st}, {"category", "texcoord"},
                                {"type", "vector2"}, {"inputs", nlohmann::json::array()}});
      runtimeNodes.push_back({{"name", u}, {"category", "extract"}, {"type", "float"},
          {"inputs", nlohmann::json::array({renamedInput(tc, "in"),
              nlohmann::json{{"name", "index"}, {"value", 0}}})}});
      runtimeNodes.push_back({{"name", v}, {"category", "extract"}, {"type", "float"},
          {"inputs", nlohmann::json::array({renamedInput(tc, "in"),
              nlohmann::json{{"name", "index"}, {"value", 1}}})}});
      runtimeNodes.push_back({{"name", top}, {"category", "mix"}, {"type", type},
          {"inputs", nlohmann::json::array({
              renamedInput(inputNamed(node, "valuetl", {{"value", 0.0}}), "bg"),
              renamedInput(inputNamed(node, "valuetr", {{"value", 0.0}}), "fg"),
              nlohmann::json{{"name", "mix"}, {"nodename", u}}})}});
      runtimeNodes.push_back({{"name", bottom}, {"category", "mix"}, {"type", type},
          {"inputs", nlohmann::json::array({
              renamedInput(inputNamed(node, "valuebl", {{"value", 0.0}}), "bg"),
              renamedInput(inputNamed(node, "valuebr", {{"value", 0.0}}), "fg"),
              nlohmann::json{{"name", "mix"}, {"nodename", u}}})}});
      runtimeNodes.push_back({{"name", name}, {"category", "mix"}, {"type", type},
          {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "bg"}, {"nodename", top}},
              nlohmann::json{{"name", "fg"}, {"nodename", bottom}},
              nlohmann::json{{"name", "mix"}, {"nodename", v}}})}});
      continue;
    }
    if (cat == "switch" && !name.empty()) {
      const nlohmann::json which = inputNamed(node, "which", {{"value", 0.0}});
      nlohmann::json fallback = inputNamed(node, "in10", {{"value", 0.0}});
      for (int choice = 8; choice >= 0; --choice) {
        const std::string lowered = choice == 0 ? name :
            name + "__choice" + std::to_string(choice);
        const std::string previous = choice == 8 ? std::string() :
            name + "__choice" + std::to_string(choice + 1);
        nlohmann::json other = choice == 8 ? renamedInput(fallback, "in2") :
            nlohmann::json{{"name", "in2"}, {"nodename", previous}};
        runtimeNodes.push_back({{"name", lowered}, {"category", "ifequal"},
            {"type", type}, {"inputs", nlohmann::json::array({
                renamedInput(which, "value1"),
                nlohmann::json{{"name", "value2"}, {"value", choice}},
                renamedInput(inputNamed(node, ("in" + std::to_string(choice + 1)).c_str(),
                                        {{"value", 0.0}}), "in1"), other})}});
      }
      continue;
    }
    if (cat == "trianglewave" && !name.empty()) {
      const nlohmann::json input = inputNamed(node, "in", {{"value", 0.0}});
      const std::string abs1 = name + "__abs1";
      const std::string mod = name + "__mod";
      const std::string sub = name + "__sub";
      const std::string abs2 = name + "__abs2";
      runtimeNodes.push_back({{"name", abs1}, {"category", "absval"}, {"type", "float"},
          {"inputs", nlohmann::json::array({renamedInput(input, "in")})}});
      runtimeNodes.push_back({{"name", mod}, {"category", "modulo"}, {"type", "float"},
          {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", abs1}},
              nlohmann::json{{"name", "in2"}, {"value", 1.0}}})}});
      runtimeNodes.push_back({{"name", sub}, {"category", "subtract"}, {"type", "float"},
          {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", mod}},
              nlohmann::json{{"name", "in2"}, {"value", 0.5}}})}});
      runtimeNodes.push_back({{"name", abs2}, {"category", "absval"}, {"type", "float"},
          {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in"}, {"nodename", sub}}})}});
      runtimeNodes.push_back({{"name", name}, {"category", "subtract"}, {"type", "float"},
          {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"value", 0.5}},
              nlohmann::json{{"name", "in2"}, {"nodename", abs2}}})}});
      continue;
    }
    if (cat == "checkerboard" && !name.empty()) {
      const std::string st=name+"__st",mul=name+"__mul",add=name+"__add";
      const std::string flr=name+"__floor",ux=name+"__x",vy=name+"__y";
      const std::string sum=name+"__sum",parity=name+"__parity";
      nlohmann::json tc=inputNamed(node,"texcoord",{{"name","texcoord"},{"nodename",st}});
      if (JsonString(tc,"nodename")==st)
        runtimeNodes.push_back({{"name",st},{"category","texcoord"},{"type","vector2"},{"inputs",nlohmann::json::array()}});
      runtimeNodes.push_back({{"name",mul},{"category","multiply"},{"type","vector2"},{"inputs",nlohmann::json::array({renamedInput(tc,"in1"),renamedInput(inputNamed(node,"uvtiling",{{"value",nlohmann::json::array({8,8})}}),"in2")})}});
      runtimeNodes.push_back({{"name",add},{"category","add"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",mul}},renamedInput(inputNamed(node,"uvoffset",{{"value",nlohmann::json::array({0,0})}}),"in2")})}});
      runtimeNodes.push_back({{"name",flr},{"category","floor"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",add}}})}});
      for (const auto& lane : {std::pair<std::string,int>{ux,0},{vy,1}})
        runtimeNodes.push_back({{"name",lane.first},{"category","extract"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",flr}},nlohmann::json{{"name","index"},{"value",lane.second}}})}});
      runtimeNodes.push_back({{"name",sum},{"category","add"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",ux}},nlohmann::json{{"name","in2"},{"nodename",vy}}})}});
      runtimeNodes.push_back({{"name",parity},{"category","modulo"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",sum}},nlohmann::json{{"name","in2"},{"value",2}}})}});
      runtimeNodes.push_back({{"name",name},{"category","select"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",parity}},renamedInput(inputNamed(node,"color2",{{"value",nlohmann::json::array({0,0,0})}}),"in1"),renamedInput(inputNamed(node,"color1",{{"value",nlohmann::json::array({1,1,1})}}),"in2")})}});
      continue;
    }
    if (cat == "circle" && !name.empty()) {
      const std::string st = name + "__st";
      const std::string delta = name + "__delta";
      const std::string distance2 = name + "__distance2";
      const std::string radius2 = name + "__radius2";
      nlohmann::json tc = inputNamed(
          node, "texcoord", {{"name", "texcoord"}, {"nodename", st}});
      if (JsonString(tc, "nodename") == st)
        runtimeNodes.push_back({{"name", st}, {"category", "texcoord"},
            {"type", "vector2"}, {"inputs", nlohmann::json::array()}});
      runtimeNodes.push_back({{"name", delta}, {"category", "subtract"},
          {"type", "vector2"}, {"inputs", nlohmann::json::array({
              renamedInput(tc, "in1"),
              renamedInput(inputNamed(node, "center",
                  {{"value", nlohmann::json::array({0.0, 0.0})}}), "in2")})}});
      runtimeNodes.push_back({{"name", distance2}, {"category", "dotproduct"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", delta}},
              nlohmann::json{{"name", "in2"}, {"nodename", delta}}})}});
      const nlohmann::json radius = inputNamed(node, "radius", {{"value", 0.5}});
      runtimeNodes.push_back({{"name", radius2}, {"category", "multiply"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              renamedInput(radius, "in1"), renamedInput(radius, "in2")})}});
      runtimeNodes.push_back({{"name", name}, {"category", "ifgreater"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "value1"}, {"nodename", distance2}},
              nlohmann::json{{"name", "value2"}, {"nodename", radius2}},
              nlohmann::json{{"name", "in1"}, {"value", 0.0}},
              nlohmann::json{{"name", "in2"}, {"value", 1.0}}})}});
      continue;
    }
    if (cat == "line" && !name.empty()) {
      const std::string st=name+"__st",delta=name+"__delta",pa=name+"__pa";
      const std::string ba=name+"__ba",dotpa=name+"__dotpa",dotba=name+"__dotba";
      const std::string ratio=name+"__ratio",bounded=name+"__bounded";
      const std::string nearest=name+"__nearest",distance=name+"__distance";
      nlohmann::json tc=inputNamed(node,"texcoord",{{"name","texcoord"},{"nodename",st}});
      if(JsonString(tc,"nodename")==st)
        runtimeNodes.push_back({{"name",st},{"category","texcoord"},{"type","vector2"},{"inputs",nlohmann::json::array()}});
      const nlohmann::json center=inputNamed(node,"center",{{"value",nlohmann::json::array({0,0})}});
      const nlohmann::json p1=inputNamed(node,"point1",{{"value",nlohmann::json::array({0.25,0.25})}});
      const nlohmann::json p2=inputNamed(node,"point2",{{"value",nlohmann::json::array({0.75,0.75})}});
      runtimeNodes.push_back({{"name",delta},{"category","subtract"},{"type","vector2"},{"inputs",nlohmann::json::array({renamedInput(tc,"in1"),renamedInput(center,"in2")})}});
      runtimeNodes.push_back({{"name",pa},{"category","subtract"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",delta}},renamedInput(p1,"in2")})}});
      runtimeNodes.push_back({{"name",ba},{"category","subtract"},{"type","vector2"},{"inputs",nlohmann::json::array({renamedInput(p2,"in1"),renamedInput(p1,"in2")})}});
      runtimeNodes.push_back({{"name",dotpa},{"category","dotproduct"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",pa}},nlohmann::json{{"name","in2"},{"nodename",ba}}})}});
      runtimeNodes.push_back({{"name",dotba},{"category","dotproduct"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",ba}},nlohmann::json{{"name","in2"},{"nodename",ba}}})}});
      runtimeNodes.push_back({{"name",ratio},{"category","divide"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",dotpa}},nlohmann::json{{"name","in2"},{"nodename",dotba}}})}});
      runtimeNodes.push_back({{"name",bounded},{"category","clamp"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",ratio}}})}});
      runtimeNodes.push_back({{"name",nearest},{"category","multiply"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",ba}},nlohmann::json{{"name","in2"},{"nodename",bounded}}})}});
      runtimeNodes.push_back({{"name",distance},{"category","distance"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",pa}},nlohmann::json{{"name","in2"},{"nodename",nearest}}})}});
      runtimeNodes.push_back({{"name",name},{"category","ifgreater"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","value1"},{"nodename",distance}},renamedInput(inputNamed(node,"radius",{{"value",0.1}}),"value2"),nlohmann::json{{"name","in1"},{"value",0}},nlohmann::json{{"name","in2"},{"value",1}}})}});
      continue;
    }
    if (cat == "colorcorrect" && !name.empty()) {
      const std::string amount=name+"__hsv_amount",hsv=name+"__hsv";
      const std::string saturation=name+"__saturation",gamma=name+"__gamma";
      const std::string gammaReciprocal=name+"__gamma_reciprocal";
      const std::string gammaAbsolute=name+"__gamma_absolute";
      const std::string gammaPower=name+"__gamma_power";
      const std::string gammaSign=name+"__gamma_sign";
      const std::string liftSubtract=name+"__lift_subtract";
      const std::string liftMultiply=name+"__lift_multiply";
      const std::string liftAdd=name+"__lift_add",gain=name+"__gain";
      const std::string contrast=name+"__contrast",exposurePower=name+"__exposure_power";
      const bool preserveAlpha = type == "color4" || type == "vector4";
      const std::string corrected = preserveAlpha ? name+"__corrected" : name;
      const nlohmann::json source=inputNamed(node,"in",{{"value",nlohmann::json::array({1,1,1,0})}});
      runtimeNodes.push_back({{"name",amount},{"category","combine3"},{"type","vector3"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"hue",{{"value",0}}),"in1"),nlohmann::json{{"name","in2"},{"value",1}},nlohmann::json{{"name","in3"},{"value",1}}})}});
      runtimeNodes.push_back({{"name",hsv},{"category","hsvadjust"},{"type",type},{"inputs",nlohmann::json::array({renamedInput(source,"in"),nlohmann::json{{"name","amount"},{"nodename",amount}}})}});
      runtimeNodes.push_back({{"name",saturation},{"category","saturate"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",hsv}},renamedInput(inputNamed(node,"saturation",{{"value",1}}),"amount")})}});
      runtimeNodes.push_back({{"name",gammaReciprocal},{"category","divide"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"value",1}},renamedInput(inputNamed(node,"gamma",{{"value",1}}),"in2")})}});
      runtimeNodes.push_back({{"name",gammaAbsolute},{"category","absval"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",saturation}}})}});
      runtimeNodes.push_back({{"name",gammaPower},{"category","power"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",gammaAbsolute}},nlohmann::json{{"name","in2"},{"nodename",gammaReciprocal}}})}});
      runtimeNodes.push_back({{"name",gammaSign},{"category","sign"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",saturation}}})}});
      runtimeNodes.push_back({{"name",gamma},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",gammaPower}},nlohmann::json{{"name","in2"},{"nodename",gammaSign}}})}});
      runtimeNodes.push_back({{"name",liftSubtract},{"category","subtract"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"value",1}},renamedInput(inputNamed(node,"lift",{{"value",0}}),"in2")})}});
      runtimeNodes.push_back({{"name",liftMultiply},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",gamma}},nlohmann::json{{"name","in2"},{"nodename",liftSubtract}}})}});
      runtimeNodes.push_back({{"name",liftAdd},{"category","add"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",liftMultiply}},renamedInput(inputNamed(node,"lift",{{"value",0}}),"in2")})}});
      runtimeNodes.push_back({{"name",gain},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",liftAdd}},renamedInput(inputNamed(node,"gain",{{"value",1}}),"in2")})}});
      runtimeNodes.push_back({{"name",contrast},{"category","contrast"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",gain}},renamedInput(inputNamed(node,"contrast",{{"value",1}}),"amount"),renamedInput(inputNamed(node,"contrastpivot",{{"value",0.5}}),"pivot")})}});
      runtimeNodes.push_back({{"name",exposurePower},{"category","power"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"value",2}},renamedInput(inputNamed(node,"exposure",{{"value",0}}),"in2")})}});
      runtimeNodes.push_back({{"name",corrected},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",contrast}},nlohmann::json{{"name","in2"},{"nodename",exposurePower}}})}});
      if(preserveAlpha){const std::string alpha=name+"__alpha";runtimeNodes.push_back({{"name",alpha},{"category","extract"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(source,"in"),nlohmann::json{{"name","index"},{"value",3}}})}});runtimeNodes.push_back({{"name",name},{"category","setalpha"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",corrected}},nlohmann::json{{"name","alpha"},{"nodename",alpha}}})}});}
      continue;
    }
    if ((cat == "cellnoise2d" || cat == "cellnoise3d") && !name.empty()) {
      const bool is3d = cat == "cellnoise3d";
      const char* inputName = is3d ? "position" : "texcoord";
      const std::string sourceName = name + (is3d ? "__position" : "__st");
      nlohmann::json source = inputNamed(
          node, inputName, {{"name", inputName}, {"nodename", sourceName}});
      if (JsonString(source, "nodename") == sourceName) {
        runtimeNodes.push_back({{"name", sourceName},
            {"category", is3d ? "position" : "texcoord"},
            {"type", is3d ? "vector3" : "vector2"},
            {"inputs", nlohmann::json::array()}});
      }
      nlohmann::json lowered = node;
      lowered["inputs"] = nlohmann::json::array({renamedInput(source, inputName)});
      runtimeNodes.push_back(std::move(lowered));
      continue;
    }
    if (cat == "randomfloat" && !name.empty()) {
      const nlohmann::json input=inputNamed(node,"in",{{"value",0}});
      emitRandomFloat(name, input, inputNamed(node,"seed",{{"value",0}}),
                      inputNamed(node,"min",{{"value",0}}),
                      inputNamed(node,"max",{{"value",1}}),
                      JsonString(input,"type") != "integer");
      continue;
    }
    if (cat == "randomcolor" && !name.empty()) {
      const nlohmann::json input=inputNamed(node,"in",{{"value",0}});
      const nlohmann::json seed=inputNamed(node,"seed",{{"value",0}});
      const char* labels[3]={"hue","saturation","brightness"};
      const double offsets[3]={413.3,1522.4,1813.8};
      const char* lows[3]={"huelow","saturationlow","brightnesslow"};
      const char* highs[3]={"huehigh","saturationhigh","brightnesshigh"};
      const double lowDefaults[3]={0.0,0.825,1.0};
      const double highDefaults[3]={1.0,1.0,1.0};
      std::string randomNames[3];
      for(int channel=0;channel<3;++channel){const std::string offset=name+"__seed_"+labels[channel]+"_offset";const std::string rounded=name+"__seed_"+labels[channel];randomNames[channel]=name+"__random_"+labels[channel];runtimeNodes.push_back({{"name",offset},{"category","add"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(seed,"in1"),nlohmann::json{{"name","in2"},{"value",offsets[channel]}}})}});runtimeNodes.push_back({{"name",rounded},{"category","ceil"},{"type","integer"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",offset}}})}});emitRandomFloat(randomNames[channel],input,nlohmann::json{{"nodename",rounded}},inputNamed(node,lows[channel],{{"value",lowDefaults[channel]}}),inputNamed(node,highs[channel],{{"value",highDefaults[channel]}}),true);}
      const std::string hsv=name+"__hsv";
      runtimeNodes.push_back({{"name",hsv},{"category","combine3"},{"type","color3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",randomNames[0]}},nlohmann::json{{"name","in2"},{"nodename",randomNames[1]}},nlohmann::json{{"name","in3"},{"nodename",randomNames[2]}}})}});
      runtimeNodes.push_back({{"name",name},{"category","hsvtorgb"},{"type","color3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",hsv}}})}});
      continue;
    }
    runtimeNodes.push_back(node);
  }
  std::map<std::string, int> nodeIds;
  for (const nlohmann::json& node : runtimeNodes) {
    const std::string name = JsonString(node, "name");
    if (!name.empty() && !nodeIds.count(name))
      nodeIds[name] = static_cast<int>(nodeIds.size());
  }
  std::set<std::string> emittedNodes;
  for (const nlohmann::json& node : runtimeNodes) {
    const std::string name = JsonString(node, "name");
    if (name.empty() || !emittedNodes.insert(name).second) continue;
    MaterialXGraphNodeCPU out;
    out.name = name;
    const std::string type = JsonString(node, "type");
    const std::string cat = NormalizeMtlxCategory(JsonString(node, "category"), type);
    if (cat == "constant") out.op = MaterialXGraphOpCPU::Constant;
    else if (cat == "image" || cat == "gltf_image" || cat == "gltf_colorimage") {
      out.op = MaterialXGraphOpCPU::Image;
      graph.hasImages = true;
    } else if (cat == "tiledimage" || cat == "hextiledimage") {
      out.op = MaterialXGraphOpCPU::TiledImage;
      graph.hasImages = true;
    } else if (cat == "normalmap" || cat == "gltf_normalmap" ||
               cat == "hextilednormalmap") {
      out.op = MaterialXGraphOpCPU::NormalMap;
      graph.hasImages = true;
    } else if (cat == "add" || cat == "plus") out.op = MaterialXGraphOpCPU::Add;
    else if (cat == "subtract" || cat == "minus") out.op = MaterialXGraphOpCPU::Subtract;
    else if (cat == "multiply") out.op = MaterialXGraphOpCPU::Multiply;
    else if (cat == "divide") out.op = MaterialXGraphOpCPU::Divide;
    else if (cat == "mix") out.op = MaterialXGraphOpCPU::Mix;
    else if (cat == "clamp") out.op = MaterialXGraphOpCPU::Clamp;
    else if (cat == "saturate") {
      out.op = MaterialXGraphOpCPU::Saturate;
      out.value[1][0] = out.value[1][1] =
          out.value[1][2] = out.value[1][3] = 1.0f;
    }
    else if (cat == "dot" || cat == "dotproduct") out.op = MaterialXGraphOpCPU::Dot;
    else if (cat == "normalize") out.op = MaterialXGraphOpCPU::Normalized;
    else if (cat == "power" || cat == "pow" || cat == "safepower")
      out.op = MaterialXGraphOpCPU::Power;
    else if (cat == "min" || cat == "minimum")
      out.op = MaterialXGraphOpCPU::Minimum;
    else if (cat == "max" || cat == "maximum")
      out.op = MaterialXGraphOpCPU::Maximum;
    else if (cat == "abs" || cat == "absval") out.op = MaterialXGraphOpCPU::Absolute;
    else if (cat == "sqrt") out.op = MaterialXGraphOpCPU::SquareRoot;
    else if (cat == "sin") out.op = MaterialXGraphOpCPU::Sine;
    else if (cat == "cos") out.op = MaterialXGraphOpCPU::Cosine;
    else if (cat == "luminance") out.op = MaterialXGraphOpCPU::Luminance;
    else if (cat == "select") out.op = MaterialXGraphOpCPU::Select;
    else if (cat == "ifgreater") out.op = MaterialXGraphOpCPU::IfGreater;
    else if (cat == "ifgreatereq" || cat == "ifgreaterequal")
      out.op = MaterialXGraphOpCPU::IfGreaterEqual;
    else if (cat == "ifequal") out.op = MaterialXGraphOpCPU::IfEqual;
    else if (cat == "texcoord" || cat == "texcoord0" || cat == "texcoord1") {
      out.op = MaterialXGraphOpCPU::Texcoord;
      // Preserve the explicit second-set form in the graph IR.  The z lane
      // of the third fallback value is otherwise unused by texcoord nodes;
      // the w lane remains reserved for image UV-input routing.
      out.value[2][2] = (cat == "texcoord1") ? 1.0f : 0.0f;
    }
    else if (cat == "floor") out.op = MaterialXGraphOpCPU::Floor;
    else if (cat == "ceil" || cat == "ceiling") out.op = MaterialXGraphOpCPU::Ceil;
    else if (cat == "fract" || cat == "fraction") out.op = MaterialXGraphOpCPU::Fract;
    else if (cat == "step") out.op = MaterialXGraphOpCPU::Step;
    else if (cat == "smoothstep") out.op = MaterialXGraphOpCPU::Smoothstep;
    else if (cat == "cross" || cat == "crossproduct") out.op = MaterialXGraphOpCPU::Cross;
    else if (cat == "length" || cat == "magnitude") out.op = MaterialXGraphOpCPU::Length;
    else if (cat == "noise3d") out.op = MaterialXGraphOpCPU::Noise3D;
    else if (cat == "noise2d" || cat == "noise")
      out.op = MaterialXGraphOpCPU::Noise2D;
    else if (cat == "tan") out.op = MaterialXGraphOpCPU::Tangent;
    else if (cat == "tangent") out.op = MaterialXGraphOpCPU::GeometricTangent;
    else if (cat == "normal") out.op = MaterialXGraphOpCPU::GeometricNormal;
    else if (cat == "rotate3d" || cat == "rotate")
      out.op = MaterialXGraphOpCPU::Rotate3D;
    else if (cat == "transform2d" || cat == "place2d" ||
             cat == "place2dtransform")
      out.op = MaterialXGraphOpCPU::Transform2D;
    else if (cat == "exp" || cat == "exponential")
      out.op = MaterialXGraphOpCPU::Exponential;
    else if (cat == "log" || cat == "ln" || cat == "logarithm")
      out.op = MaterialXGraphOpCPU::Logarithm;
    else if (cat == "modulo" || cat == "mod") out.op = MaterialXGraphOpCPU::Modulo;
    else if (cat == "invert") out.op = MaterialXGraphOpCPU::Invert;
    else if (cat == "oneminus") out.op = MaterialXGraphOpCPU::Invert;
    else if (cat == "remap" || cat == "range") out.op = MaterialXGraphOpCPU::Remap;
    else if (cat == "atan2" || cat == "arctan2") out.op = MaterialXGraphOpCPU::Atan2;
    else if (cat == "sign" || cat == "signum") out.op = MaterialXGraphOpCPU::Sign;
    else if (cat == "round") out.op = MaterialXGraphOpCPU::Round;
    else if (cat == "combine2" || cat == "combine3" || cat == "combine4")
      out.op = MaterialXGraphOpCPU::Combine;
    else if (cat == "extract" || cat == "separate" || cat == "separate2" ||
             cat == "separate3" || cat == "separate4")
      out.op = MaterialXGraphOpCPU::Extract;
    else if (cat.rfind("convert", 0) == 0)
      out.op = MaterialXGraphOpCPU::Convert;
    else if (cat == "position") out.op = MaterialXGraphOpCPU::Position;
    else if (cat == "hsvadjust") {
      out.op = MaterialXGraphOpCPU::HsvAdjust;
      out.value[1][1] = out.value[1][2] = 1.0f;
    }
    else if (cat == "rgbtohsv") out.op = MaterialXGraphOpCPU::RgbToHsv;
    else if (cat == "hsvtorgb") out.op = MaterialXGraphOpCPU::HsvToRgb;
    else if (cat == "rotate2d") out.op = MaterialXGraphOpCPU::Rotate2D;
    else if (cat == "distance") out.op = MaterialXGraphOpCPU::Distance;
    else if (cat == "reflect") out.op = MaterialXGraphOpCPU::Reflect;
    else if (cat == "refract") out.op = MaterialXGraphOpCPU::Refract;
    else if (cat == "premult") out.op = MaterialXGraphOpCPU::Premult;
    else if (cat == "unpremult") out.op = MaterialXGraphOpCPU::Unpremult;
    else if (cat == "mincomponent") out.op = MaterialXGraphOpCPU::MinComponent;
    else if (cat == "maxcomponent") out.op = MaterialXGraphOpCPU::MaxComponent;
    else if (cat == "and") out.op = MaterialXGraphOpCPU::LogicalAnd;
    else if (cat == "or") out.op = MaterialXGraphOpCPU::LogicalOr;
    else if (cat == "xor") out.op = MaterialXGraphOpCPU::LogicalXor;
    else if (cat == "not") out.op = MaterialXGraphOpCPU::LogicalNot;
    else if (cat == "inside") out.op = MaterialXGraphOpCPU::Inside;
    else if (cat == "outside") out.op = MaterialXGraphOpCPU::Outside;
    else if (cat == "geomcolor") {
      out.op = MaterialXGraphOpCPU::GeomColor;
    }
    else if (cat == "bitangent") out.op = MaterialXGraphOpCPU::Bitangent;
    else if (cat == "difference" || cat == "in" || cat == "mask" ||
             cat == "matte" || cat == "out" || cat == "over" ||
             cat == "disjointover") {
      out.op = cat == "difference" ? MaterialXGraphOpCPU::Difference :
               cat == "in" ? MaterialXGraphOpCPU::In :
               cat == "mask" ? MaterialXGraphOpCPU::Mask :
               cat == "matte" ? MaterialXGraphOpCPU::Matte :
               cat == "out" ? MaterialXGraphOpCPU::Out :
               cat == "over" ? MaterialXGraphOpCPU::Over :
               MaterialXGraphOpCPU::DisjointOver;
      out.value[2][0] = out.value[2][1] =
          out.value[2][2] = out.value[2][3] = 1.0f;
    }
    else if (cat == "setalpha") out.op = MaterialXGraphOpCPU::SetAlpha;
    else if (cat == "cellnoise2d") out.op = MaterialXGraphOpCPU::CellNoise2D;
    else if (cat == "cellnoise3d") out.op = MaterialXGraphOpCPU::CellNoise3D;
    else if (cat == "heighttonormal")
      out.op = MaterialXGraphOpCPU::HeightToNormal;
    else if (cat == "asin" || cat == "arcsin")
      out.op = MaterialXGraphOpCPU::Arcsine;
    else if (cat == "acos" || cat == "arccos")
      out.op = MaterialXGraphOpCPU::Arccosine;
    else if (cat == "atan" || cat == "arctan")
      out.op = MaterialXGraphOpCPU::Arctangent;
    else if (cat == "contrast")
      out.op = MaterialXGraphOpCPU::Contrast;
    else if (cat == "screen") {
      out.op = MaterialXGraphOpCPU::Screen;
      out.value[2][0] = out.value[2][1] =
          out.value[2][2] = out.value[2][3] = 1.0f;
    }
    else if (cat == "overlay") {
      out.op = MaterialXGraphOpCPU::Overlay;
      out.value[2][0] = out.value[2][1] =
          out.value[2][2] = out.value[2][3] = 1.0f;
    }
    else if (cat == "burn") {
      out.op = MaterialXGraphOpCPU::Burn;
      out.value[2][0] = out.value[2][1] =
          out.value[2][2] = out.value[2][3] = 1.0f;
    }
    else if (cat == "dodge") {
      out.op = MaterialXGraphOpCPU::Dodge;
      out.value[2][0] = out.value[2][1] =
          out.value[2][2] = out.value[2][3] = 1.0f;
    }
    else if (cat == "ramplr")
      out.op = MaterialXGraphOpCPU::RampLR;
    else if (cat == "ramptb")
      out.op = MaterialXGraphOpCPU::RampTB;
    else if (cat == "splitlr")
      out.op = MaterialXGraphOpCPU::SplitLR;
    else if (cat == "splittb")
      out.op = MaterialXGraphOpCPU::SplitTB;
    else if (cat == "swizzle" || cat.rfind("swizzle_", 0) == 0) {
      out.op = MaterialXGraphOpCPU::Swizzle;
      out.value[1][0] = 0.0f;
      out.value[1][1] = 1.0f;
      out.value[1][2] = 2.0f;
      out.value[1][3] = 3.0f;
    }
    else if (cat == "fractal3d" || cat == "fractal")
      out.op = MaterialXGraphOpCPU::Noise3D;
    if (out.op == MaterialXGraphOpCPU::Unknown) {
      if (err) *err = "Unsupported MaterialX graph node category: " + cat;
      return false;
    }
    if (out.op == MaterialXGraphOpCPU::Image ||
        out.op == MaterialXGraphOpCPU::TiledImage)
      out.value[2][3] = -1.0f;
    const auto inputsIt = node.find("inputs");
    int nextInput = 0;
    int uvInput = -1;
    bool usedInput[3]{false, false, false};
    if (inputsIt != node.end() && inputsIt->is_array()) {
      for (const nlohmann::json& input : *inputsIt) {
        const std::string inputName = JsonString(input, "name");
        const auto valueIt = input.find("value");
        const std::string inputType = NormalizeMtlxType(JsonString(input, "type"));
        const bool conditional = cat == "ifgreater" || cat == "ifgreatereq" ||
                                 cat == "ifgreaterequal" || cat == "ifequal";
        if (((cat == "splitlr" || cat == "splittb") &&
             inputName == "texcoord") ||
            (conditional && inputName == "in2")) {
          const std::string source = JsonString(input, "nodename");
          const auto found = nodeIds.find(source);
          if (found != nodeIds.end()) out.auxInput = found->second;
          if (valueIt != input.end()) {
            if (valueIt->is_number()) {
              const float v = valueIt->get<float>();
              for (float& lane : out.auxValue) lane = v;
            } else if (valueIt->is_array()) {
              for (size_t c = 0; c < valueIt->size() && c < 4; ++c)
                if ((*valueIt)[c].is_number())
                  out.auxValue[c] = (*valueIt)[c].get<float>();
            }
          }
          continue;
        }
        if ((cat == "swizzle" || cat.rfind("swizzle_", 0) == 0) &&
            inputName == "channels" &&
            valueIt != input.end() && valueIt->is_string()) {
          const std::string channels = valueIt->get<std::string>();
          auto selector = [](char ch) -> float {
            switch (ch) {
              case 'r': case 'x': return 0.0f;
              case 'g': case 'y': return 1.0f;
              case 'b': case 'z': return 2.0f;
              case 'a': case 'w': return 3.0f;
              case '0': return 4.0f;
              case '1': return 5.0f;
              default: return 0.0f;
            }
          };
          for (size_t lane = 0; lane < channels.size() && lane < 4; ++lane)
            out.value[1][lane] = selector(channels[lane]);
          continue;
        }
        if (inputName == "file" || inputType == "filename") {
          out.imagePath = JsonString(input, "value");
          if (!out.imagePath.empty()) graph.hasImages = true;
          continue;
        }
        // Tiled-image graphs commonly author a local UV scale/offset on the
        // image node. Keep these controls out of the value-input arity so the
        // graph's arithmetic inputs retain their canonical indices.
        const bool uvControlNode = cat == "image" || cat == "tiledimage" ||
            cat == "hextiledimage" || cat == "transform2d" ||
            cat == "place2d" || cat == "place2dtransform";
        if (uvControlNode && valueIt != input.end() && valueIt->is_array() &&
            (inputName == "scale" || inputName == "uv_scale" ||
             inputName == "offset" || inputName == "uv_offset")) {
          float* dst = (inputName == "offset" || inputName == "uv_offset")
                           ? out.uvOffset : out.uvScale;
          for (size_t c = 0; c < valueIt->size() && c < 2; ++c)
            if ((*valueIt)[c].is_number()) dst[c] = (*valueIt)[c].get<float>();
          continue;
        }
        if (valueIt != input.end() && valueIt->is_number() &&
            (inputName == "rotation" || inputName == "rotate" ||
             inputName == "angle") &&
            (cat == "transform2d" || cat == "place2d" ||
             cat == "place2dtransform")) {
          // The packed node has no spare scalar lane. Transform2D's value[2].w
          // is reserved for this authored rotation (degrees); other nodes keep
          // their normal fallback value untouched.
          out.value[2][3] = valueIt->get<float>();
          continue;
        }
        int inputSlot = -1;
        if ((cat == "rotate3d" || cat == "rotate") && inputName == "amount")
          inputSlot = 0;
        else if ((cat == "rotate3d" || cat == "rotate") && inputName == "axis")
          inputSlot = 1;
        else if ((cat == "rotate3d" || cat == "rotate") && inputName == "in")
          inputSlot = 2;
        else if (cat == "rotate2d" && inputName == "in") inputSlot = 0;
        else if (cat == "rotate2d" && inputName == "amount") inputSlot = 1;
        else if (cat == "clamp" && inputName == "low") inputSlot = 1;
        else if (cat == "clamp" && inputName == "high") inputSlot = 2;
        else if (cat == "mix" && (inputName == "bg" || inputName == "in1"))
          inputSlot = 0;
        else if (cat == "mix" && (inputName == "fg" || inputName == "in2"))
          inputSlot = 1;
        else if (cat == "mix" && (inputName == "mix" || inputName == "amount"))
          inputSlot = 2;
        else if (cat == "saturate" && inputName == "in")
          inputSlot = 0;
        else if (cat == "saturate" && inputName == "amount")
          inputSlot = 1;
        else if (cat == "hsvadjust" && inputName == "in") inputSlot = 0;
        else if (cat == "hsvadjust" && inputName == "amount") inputSlot = 1;
        else if (cat == "contrast" && inputName == "in") inputSlot = 0;
        else if (cat == "contrast" && inputName == "amount") inputSlot = 1;
        else if (cat == "contrast" && inputName == "pivot") inputSlot = 2;
        else if (cat == "setalpha" && inputName == "in") inputSlot = 0;
        else if (cat == "setalpha" && inputName == "alpha") inputSlot = 1;
        else if (conditional && inputName == "value1") inputSlot = 0;
        else if (conditional && inputName == "value2") inputSlot = 1;
        else if (conditional && inputName == "in1") inputSlot = 2;
        else if (cat == "select" && (inputName == "in" || inputName == "which"))
          inputSlot = 0;
        else if (cat == "select" && inputName == "in1") inputSlot = 1;
        else if (cat == "select" && inputName == "in2") inputSlot = 2;
        else if ((cat == "screen" || cat == "overlay" || cat == "burn" ||
                  cat == "dodge" || cat == "difference" || cat == "in" ||
                  cat == "mask" || cat == "matte" || cat == "out" ||
                  cat == "over" || cat == "disjointover") && inputName == "fg")
          inputSlot = 0;
        else if ((cat == "screen" || cat == "overlay" || cat == "burn" ||
                  cat == "dodge" || cat == "difference" || cat == "in" ||
                  cat == "mask" || cat == "matte" || cat == "out" ||
                  cat == "over" || cat == "disjointover") && inputName == "bg")
          inputSlot = 1;
        else if ((cat == "screen" || cat == "overlay" || cat == "burn" ||
                  cat == "dodge" || cat == "difference" || cat == "in" ||
                  cat == "mask" || cat == "matte" || cat == "out" ||
                  cat == "over" || cat == "disjointover") && inputName == "mix")
          inputSlot = 2;
        else if (cat == "ramplr" && inputName == "valuel")
          inputSlot = 0;
        else if (cat == "ramplr" && inputName == "valuer")
          inputSlot = 1;
        else if (cat == "ramptb" && inputName == "valuet")
          inputSlot = 0;
        else if (cat == "ramptb" && inputName == "valueb")
          inputSlot = 1;
        else if ((cat == "ramplr" || cat == "ramptb") &&
                 inputName == "texcoord")
          inputSlot = 2;
        else if (cat == "splitlr" && inputName == "valuel")
          inputSlot = 0;
        else if (cat == "splitlr" && inputName == "valuer")
          inputSlot = 1;
        else if (cat == "splittb" && inputName == "valuet")
          inputSlot = 0;
        else if (cat == "splittb" && inputName == "valueb")
          inputSlot = 1;
        else if ((cat == "splitlr" || cat == "splittb") &&
                 inputName == "center")
          inputSlot = 2;
        else if (inputName == "in" || inputName == "in1" ||
            inputName == "value" || inputName == "color" ||
            inputName == "position" || inputName == "texcoord" ||
            inputName == "uv" || inputName == "st" || inputName == "coord")
          inputSlot = 0;
        else if (inputName == "in2" || inputName == "amount" ||
                 inputName == "index" || inputName == "lacunarity" ||
                 inputName == "scale")
          inputSlot = 1;
        else if (inputName == "in3" || inputName == "octaves")
          inputSlot = 2;
        if (inputSlot < 0) {
          while (nextInput < 3 && usedInput[nextInput]) ++nextInput;
          inputSlot = nextInput;
        }
        if (inputSlot < 0 || inputSlot >= 3) continue;
        usedInput[inputSlot] = true;
        nextInput = std::max(nextInput, inputSlot + 1);
        // Preserve connected graph coordinates for image nodes. The runtime
        // interpreters use this metadata instead of silently sampling the hit
        // UV whenever an image has a texcoord/place2d input.
        if ((inputName == "texcoord" || inputName == "uv" ||
             inputName == "st" || inputName == "coord") &&
            (cat == "image" || cat == "tiledimage" ||
             cat == "hextiledimage")) {
          uvInput = inputSlot;
        }
        const std::string source = JsonString(input, "nodename");
        if (!source.empty()) {
          const auto found = nodeIds.find(source);
          if (found != nodeIds.end()) out.input[inputSlot] = found->second;
        }
        if (valueIt != input.end()) {
          if (valueIt->is_number()) {
            // MaterialX promotes scalar inputs lane-wise when a polymorphic
            // vector/color operation consumes them. Keeping only x made
            // colorcorrect gamma/gain/exposure affect red while green and blue
            // saw the record's unrelated zero/one defaults.
            const float scalar = valueIt->get<float>();
            for (float& lane : out.value[inputSlot]) lane = scalar;
          }
          else if (valueIt->is_array()) {
            for (size_t c = 0; c < valueIt->size() && c < 4; ++c)
              if ((*valueIt)[c].is_number())
                out.value[inputSlot][c] = (*valueIt)[c].get<float>();
          }
        }
      }
    }
    if (uvInput >= 0) out.value[2][3] = static_cast<float>(uvInput);
    graph.nodes.push_back(std::move(out));
  }
  if (graph.nodes.empty()) {
    if (err) *err = "MaterialX graph node list is empty";
    return false;
  }
  std::map<std::string, std::string> outputs;
  const auto outputsIt = ng.find("outputs");
  if (outputsIt != ng.end() && outputsIt->is_array()) {
    for (const nlohmann::json& output : *outputsIt) {
      const std::string name = JsonString(output, "name");
      const std::string node = JsonString(output, "nodename");
      if (!name.empty() && !node.empty()) outputs[name] = node;
    }
  }
  const auto connIt = j.find("connections");
  if (connIt != j.end() && connIt->is_array()) {
    for (const nlohmann::json& connection : *connIt) {
      const std::string input = OpenPBREvalInputName(
          JsonString(connection, "input"));
      const auto outputIt = outputs.find(JsonString(connection, "output"));
      if (outputIt == outputs.end()) continue;
      const auto nodeIt = nodeIds.find(outputIt->second);
      if (nodeIt == nodeIds.end()) continue;
      int* destination = nullptr;
      if (input == "base_color") destination = &graph.output[0];
      else if (input == "base_metalness") destination = &graph.output[1];
      else if (input == "specular_roughness") destination = &graph.output[2];
      else if (input == "geometry_opacity") destination = &graph.output[3];
      else if (input == "emission_color") destination = &graph.output[4];
      else if (input == "geometry_normal") destination = &graph.output[5];
      else if (input == "subsurface_weight") destination = &graph.output[6];
      else if (input == "subsurface_color") destination = &graph.output[7];
      else if (input == "subsurface_radius") destination = &graph.output[8];
      else if (input == "specular_weight") destination = &graph.output[9];
      else if (input == "specular_color") destination = &graph.output[10];
      else if (input == "transmission_weight") destination = &graph.output[11];
      else if (input == "transmission_color") destination = &graph.output[12];
      else if (input == "coat_weight") destination = &graph.output[13];
      else if (input == "coat_color") destination = &graph.output[14];
      else if (input == "coat_roughness") destination = &graph.output[15];
      else if (input == "fuzz_weight" || input == "sheen_weight")
        destination = &graph.output[16];
      else if (input == "fuzz_color" || input == "sheen_color")
        destination = &graph.output[17];
      else if (input == "fuzz_roughness" || input == "sheen_roughness")
        destination = &graph.output[18];
      else if (input == "specular_ior") destination = &graph.output[19];
      else if (input == "base_weight") destination = &graph.output[20];
      else if (input == "base_diffuse_roughness" ||
               input == "diffuse_roughness") destination = &graph.output[21];
      else if (input == "transmission_scatter") destination = &graph.output[22];
      else if (input == "transmission_depth") destination = &graph.output[23];
      else if (input == "transmission_scatter_anisotropy")
        destination = &graph.output[24];
      else if (input == "subsurface_scale" ||
               input == "subsurface_radius_scale") destination = &graph.output[25];
      else if (input == "subsurface_anisotropy" ||
               input == "subsurface_scatter_anisotropy")
        destination = &graph.output[26];
      else if (input == "coat_ior") destination = &graph.output[27];
      else if (input == "thin_film_weight") destination = &graph.output[28];
      else if (input == "thin_film_thickness") destination = &graph.output[29];
      else if (input == "thin_film_ior") destination = &graph.output[30];
      else if (input == "specular_anisotropy") destination = &graph.output[31];
      else if (input == "specular_rotation") destination = &graph.output[32];
      else if (input == "specular_roughness_anisotropy")
        destination = &graph.output[33];
      else if (input == "transmission_dispersion") destination = &graph.output[34];
      else if (input == "transmission_dispersion_abbe_number")
        destination = &graph.output[35];
      else if (input == "transmission_dispersion_scale")
        destination = &graph.output[36];
      else if (input == "coat_anisotropy") destination = &graph.output[37];
      else if (input == "coat_rotation") destination = &graph.output[38];
      else if (input == "coat_roughness_anisotropy")
        destination = &graph.output[39];
      else if (input == "volume_density") destination = &graph.output[40];
      else if (input == "volume_albedo") destination = &graph.output[41];
      else if (input == "volume_emission_color") destination = &graph.output[42];
      else if (input == "volume_emission_scale") destination = &graph.output[43];
      else if (input == "emission_luminance") destination = &graph.output[44];
      else if (input == "coat_affect_color") destination = &graph.output[45];
      else if (input == "coat_affect_roughness") destination = &graph.output[46];
      else if (input == "coat_darkening") destination = &graph.output[47];
      if (destination) *destination = nodeIt->second;
    }
  }
  // GPU interpreters execute a single bounded pass. Canonicalize the retained
  // graph into dependency-first order here so runtime evaluation never needs
  // the old 64x64 fixed-point fallback (a severe NVRTC/Vulkan driver-JIT cost).
  // Cyclic MaterialX graphs are malformed and keep the caller's bake fallback.
  if (graph.nodes.size() > kRtMaterialGraphMaxNodes) {
    if (err) *err = "MaterialX graph exceeds the 64-node runtime limit";
    return false;
  }
  std::vector<unsigned char> visit(graph.nodes.size(), 0);
  std::vector<int> order;
  order.reserve(graph.nodes.size());
  std::function<bool(int)> emitDependencyFirst = [&](int index) {
    if (index < 0 || static_cast<size_t>(index) >= graph.nodes.size()) return true;
    unsigned char& state = visit[static_cast<size_t>(index)];
    if (state == 2) return true;
    if (state == 1) return false;
    state = 1;
    for (int input : graph.nodes[static_cast<size_t>(index)].input)
      if (!emitDependencyFirst(input)) return false;
    if (!emitDependencyFirst(graph.nodes[static_cast<size_t>(index)].auxInput))
      return false;
    state = 2;
    order.push_back(index);
    return true;
  };
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    if (!emitDependencyFirst(static_cast<int>(i))) {
      if (err) *err = "MaterialX graph contains a dependency cycle";
      return false;
    }
  }
  std::vector<int> oldToNew(graph.nodes.size(), -1);
  std::vector<MaterialXGraphNodeCPU> sorted;
  sorted.reserve(graph.nodes.size());
  for (int oldIndex : order) {
    oldToNew[static_cast<size_t>(oldIndex)] = static_cast<int>(sorted.size());
    sorted.push_back(std::move(graph.nodes[static_cast<size_t>(oldIndex)]));
  }
  for (MaterialXGraphNodeCPU& node : sorted) {
    for (int& input : node.input)
      if (input >= 0) input = oldToNew[static_cast<size_t>(input)];
    if (node.auxInput >= 0)
      node.auxInput = oldToNew[static_cast<size_t>(node.auxInput)];
  }
  for (int& output : graph.output)
    if (output >= 0) output = oldToNew[static_cast<size_t>(output)];
  graph.nodes = std::move(sorted);
  graph.valid = true;
  mat->materialXGraph = std::move(graph);
  return true;
}

void BakeMaterialXGraphTextures(DrawMaterialCPU* mat, DrawScene* scene) {
  if (!mat || !scene || mat->materialXNodeGraphJson.empty()) return;
  if (!mat->materialXGraph.valid) {
    std::string compileError;
    CompileMaterialXGraphRuntime(mat, &compileError);
  }
  // Bind graph image nodes to the already-resolved DrawScene texture slots.
  // MaterialX JSON stores the authored filename while the scene loader may
  // store an absolute or normalized asset identifier, so accept exact,
  // normalized, and basename matches. This keeps the runtime IR independent
  // of the loader that populated the texture array.
  auto normalizePath = [](std::string value) {
    for (char& c : value) if (c == '\\') c = '/';
    while (value.size() > 1 && value.back() == '/') value.pop_back();
    return value;
  };
  std::string baseDir = ".";
  const size_t materialSlash = mat->absPath.find_last_of("/\\");
  if (materialSlash != std::string::npos) {
    baseDir = mat->absPath.substr(0, materialSlash);
    if (baseDir.empty()) baseDir = ".";
  }
  auto isAbsolutePath = [](const std::string& value) {
    return (!value.empty() && (value[0] == '/' || value[0] == '\\')) ||
           (value.size() > 1 && value[1] == ':');
  };
  // Infer the color-data subgraph for images that must be loaded here.  The
  // normal scene texture table already carries authored sRGB metadata, but a
  // graph image that was not discovered by the loader needs the same choice:
  // base/emission/subsurface-color are color data; roughness, normal, and
  // scalar masks are linear.  Shared nodes conservatively use sRGB whenever
  // they feed any color output.
  std::set<int> srgbNodes;
  std::function<void(int)> markSrgb = [&](int index) {
    if (index < 0 || static_cast<size_t>(index) >= mat->materialXGraph.nodes.size() ||
        !srgbNodes.insert(index).second) return;
    const MaterialXGraphNodeCPU& source = mat->materialXGraph.nodes[static_cast<size_t>(index)];
    for (int input : source.input) markSrgb(input);
  };
  for (int route : {0, 4, 7, 10, 12, 14, 17, 22, 41, 42})
    markSrgb(mat->materialXGraph.output[route]);

  for (size_t nodeIndex = 0; nodeIndex < mat->materialXGraph.nodes.size();
       ++nodeIndex) {
    MaterialXGraphNodeCPU& node = mat->materialXGraph.nodes[nodeIndex];
    if (node.textureId >= 0 || node.imagePath.empty()) continue;
    const std::string wanted = normalizePath(node.imagePath);
    const size_t slash = wanted.find_last_of('/');
    const std::string basename = slash == std::string::npos
                                     ? wanted
                                     : wanted.substr(slash + 1);
    for (size_t textureId = 0; textureId < scene->textures.size(); ++textureId) {
      const std::string asset = normalizePath(scene->textures[textureId].assetIdentifier);
      const size_t assetSlash = asset.find_last_of('/');
      const std::string assetBase = assetSlash == std::string::npos
                                        ? asset
                                        : asset.substr(assetSlash + 1);
      if (asset == wanted || assetBase == basename ||
          (!mat->absPath.empty() &&
           normalizePath(mat->absPath.substr(0, mat->absPath.find_last_of("/\\")) +
                         "/" + wanted) == asset)) {
        node.textureId = static_cast<int>(textureId);
        break;
      }
    }
    if (node.textureId < 0) {
      const std::string assetPath = normalizePath(
          isAbsolutePath(wanted) ? wanted : baseDir + "/" + wanted);
      auto loaded = tinyusdz::image::LoadImageFromFile(assetPath);
      if (loaded) {
        const tinyusdz::Image& image = loaded.value().image;
        if (image.width > 0 && image.height > 0 && image.bpp == 8 &&
            (image.channels == 1 || image.channels == 2 ||
             image.channels == 3 || image.channels == 4) &&
            image.data.size() >= static_cast<size_t>(image.width) *
                                      static_cast<size_t>(image.height) *
                                      static_cast<size_t>(image.channels)) {
          DrawTextureCPU texture;
          texture.assetIdentifier = assetPath;
          texture.srgb = srgbNodes.count(static_cast<int>(nodeIndex)) != 0;
          texture.image.width = image.width;
          texture.image.height = image.height;
          texture.image.channels = 4;
          texture.image.data.resize(static_cast<size_t>(image.width) *
                                    static_cast<size_t>(image.height) * 4u);
          for (size_t p = 0, n = static_cast<size_t>(image.width) *
                                       static_cast<size_t>(image.height);
               p < n; ++p) {
            const uint8_t* src = image.data.data() + p * image.channels;
            uint8_t* dst = texture.image.data.data() + p * 4u;
            dst[0] = src[0];
            dst[1] = image.channels > 1 ? src[1] : src[0];
            dst[2] = image.channels > 2 ? src[2] : src[0];
            dst[3] = image.channels > 3 ? src[3] : 255u;
          }
          node.textureId = static_cast<int>(scene->textures.size());
          scene->textures.push_back(std::move(texture));
        }
      }
    }
    if (node.textureId >= 0 &&
        static_cast<size_t>(node.textureId) < scene->textures.size()) {
      node.isUdim = scene->textures[static_cast<size_t>(node.textureId)].isUdim;
    }
  }
  const GraphTextureDeps deps =
      AnalyzeMaterialXJsonTextureDeps(mat->materialXNodeGraphJson);
  if (!deps.parsed || !deps.hasTextureNodes) return;

  const auto& outputs = mat->materialXGraph.output;
  std::string xml;
  std::string error;
  if (!BuildMaterialXXmlFromJsonGraph(*mat, &xml, &error)) return;
  MtlxDoc* graphDoc = mtlx_load_string(xml.c_str());
  if (!graphDoc) return;
  std::string surfaceError;
  const int surfaceNode = FindSurfaceNode(
      graphDoc, "tusdview_material", &surfaceError);
  if (surfaceNode < 0) {
    mtlx_free(graphDoc);
    return;
  }
  TextureCache* graphTex = texcache_create(baseDir.c_str());
  if (!graphTex) {
    mtlx_free(graphDoc);
    return;
  }
  texcache_preload(graphTex, graphDoc);
  std::vector<MtlxValue> graphMemo(static_cast<size_t>(graphDoc->nnode));
  std::vector<char> graphMemoDone(static_cast<size_t>(graphDoc->nnode), 0);

  // Bounded compatibility bake: arbitrary image/procedural networks become
  // ordinary semantic maps until descriptor-indexed graph evaluation lands.
  constexpr int kSize = 16;
  struct Lane { const char* name; int* slot; };
  Lane lanes[] = {{"base_color", &mat->baseColorTex},
                  {"base_metalness", &mat->metallicTex},
                  {"specular_roughness", &mat->roughnessTex},
                  {"geometry_opacity", &mat->opacityTex},
                  {"emission_color", &mat->emissiveTex},
                  {"geometry_normal", &mat->normalTex}};
  for (int laneIndex = 0; laneIndex < 6; ++laneIndex) {
    const Lane& lane = lanes[laneIndex];
    if (outputs[laneIndex] < 0 || *lane.slot >= 0) continue;
    DrawTextureCPU tex;
    tex.image.width = kSize;
    tex.image.height = kSize;
    tex.image.channels = 4;
    tex.image.data.resize(static_cast<size_t>(kSize * kSize * 4));
    tex.assetIdentifier = "mtlx-bake:" + mat->name + ":" + lane.name;
    bool ok = true;
    for (int y = 0; y < kSize && ok; ++y) {
      for (int x = 0; x < kSize; ++x) {
        DrawLightRtOpenPBRCPU p;
        if (!EvaluateMtlxDocAtUv(
                graphDoc, surfaceNode, graphTex,
                (static_cast<float>(x) + 0.5f) / kSize,
                (static_cast<float>(y) + 0.5f) / kSize, &graphMemo,
                &graphMemoDone, &p)) {
          ok = false;
          break;
        }
        float value[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        if (std::strcmp(lane.name, "base_color") == 0) {
          value[0] = p.baseColor[0]; value[1] = p.baseColor[1]; value[2] = p.baseColor[2];
        } else if (std::strcmp(lane.name, "base_metalness") == 0) {
          value[0] = value[1] = value[2] = p.metalness;
        } else if (std::strcmp(lane.name, "specular_roughness") == 0) {
          value[0] = value[1] = value[2] = p.specularRoughness;
        } else if (std::strcmp(lane.name, "geometry_opacity") == 0) {
          value[0] = value[1] = value[2] = p.opacity;
        } else if (std::strcmp(lane.name, "emission_color") == 0) {
          value[0] = p.emissionColor[0] * p.emission;
          value[1] = p.emissionColor[1] * p.emission;
          value[2] = p.emissionColor[2] * p.emission;
        } else {
          value[0] = p.normal[0] * 0.5f + 0.5f;
          value[1] = p.normal[1] * 0.5f + 0.5f;
          value[2] = p.normal[2] * 0.5f + 0.5f;
        }
        const size_t base = static_cast<size_t>((y * kSize + x) * 4);
        for (int c = 0; c < 4; ++c) {
          const float v = std::max(0.0f, std::min(1.0f, value[c]));
          tex.image.data[base + static_cast<size_t>(c)] =
              static_cast<uint8_t>(v * 255.0f + 0.5f);
        }
      }
    }
    if (!ok) continue;
    *lane.slot = static_cast<int>(scene->textures.size());
    scene->textures.push_back(std::move(tex));
    for (size_t nodeIndex = 0; nodeIndex < mat->materialXGraph.nodes.size();
         ++nodeIndex) {
      if (outputs[laneIndex] == static_cast<int>(nodeIndex))
        mat->materialXGraph.nodes[nodeIndex].textureId = *lane.slot;
    }
    if (std::strcmp(lane.name, "base_color") == 0) {
      mat->baseColor[0] = mat->baseColor[1] = mat->baseColor[2] = 1.0f;
    } else if (std::strcmp(lane.name, "base_metalness") == 0) {
      mat->metallic = 1.0f;
    } else if (std::strcmp(lane.name, "specular_roughness") == 0) {
      mat->roughness = 1.0f;
    } else if (std::strcmp(lane.name, "geometry_opacity") == 0) {
      mat->alpha = 1.0f;
    } else if (std::strcmp(lane.name, "emission_color") == 0) {
      mat->emissive[0] = mat->emissive[1] = mat->emissive[2] = 1.0f;
    } else {
      for (int c = 0; c < 3; ++c) {
        mat->normalSample.scale[c] = 2.0f;
        mat->normalSample.bias[c] = -1.0f;
      }
    }
  }
  texcache_free(graphTex);
  mtlx_free(graphDoc);
}

static void StoreEditableParam(DrawMaterialCPU* mat,
                               std::initializer_list<const char*> names,
                               const float* value, int components) {
  if (!mat || !value) return;
  for (DrawMaterialParamCPU& param : mat->params) {
    if (param.shader != "OpenPBRSurface" || param.texture >= 0 ||
        param.renderTexture >= 0) {
      continue;
    }
    bool match = false;
    for (const char* name : names) {
      if (param.name == name) {
        match = true;
        break;
      }
    }
    if (!match) continue;
    for (int c = 0; c < components; ++c) param.value[c] = value[c];
  }
}

void ApplyOpenPBRMaterialConstants(
    DrawMaterialCPU* mat, const DrawLightRtOpenPBRCPU& constants) {
  if (!mat || !mat->hasOpenPBRSurface) return;

  mat->lightRtOpenPBR = constants;
  tinyusdz::tydra::ClampRealtimePbrMaterial(&mat->lightRtOpenPBR);
  mat->hasLightRtOpenPBR = true;
  mat->openPbrSpecularModel = true;
  const DrawLightRtOpenPBRCPU& p = mat->lightRtOpenPBR;

  auto copy3 = [](const float src[3], float dst[3]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
  };
  copy3(p.baseColor, mat->baseColor);
  mat->metallic = p.metalness;
  mat->roughness = p.specularRoughness;
  copy3(p.specularColor, mat->specularColor);
  mat->ior = p.specularIor;
  mat->coatWeight = p.coatWeight;
  copy3(p.coatColor, mat->coatColor);
  mat->coatRoughness = p.coatRoughness;
  mat->coatIor = p.coatIor;
  mat->emissive[0] = p.emissionColor[0] * p.emission;
  mat->emissive[1] = p.emissionColor[1] * p.emission;
  mat->emissive[2] = p.emissionColor[2] * p.emission;
  mat->alpha = p.opacity;
  if (mat->opacityTex < 0) {
    mat->alphaMode = p.opacity < 0.999f
                         ? static_cast<int>(AlphaMode::Blend)
                         : static_cast<int>(AlphaMode::Opaque);
  }
  mat->volumeDensity = p.volumeDensity;
  copy3(p.volumeAlbedo, mat->volumeAlbedo);
  copy3(p.volumeEmission, mat->volumeEmission);
  mat->volumeEmissionScale = p.volumeEmissionScale;

  auto scalar = [&](std::initializer_list<const char*> names, float value) {
    StoreEditableParam(mat, names, &value, 1);
  };
  auto color = [&](std::initializer_list<const char*> names,
                   const float value[3]) {
    StoreEditableParam(mat, names, value, 3);
  };
  scalar({"base_weight"}, p.baseWeight);
  color({"base_color"}, p.baseColor);
  scalar({"base_diffuse_roughness"}, p.diffuseRoughness);
  scalar({"base_metalness"}, p.metalness);
  scalar({"specular_weight"}, p.specularWeight);
  color({"specular_color"}, p.specularColor);
  scalar({"specular_roughness", "base_roughness"}, p.specularRoughness);
  scalar({"specular_ior"}, p.specularIor);
  scalar({"specular_anisotropy"}, p.specularAnisotropy);
  scalar({"specular_rotation"}, p.specularRotation);
  scalar({"specular_roughness_anisotropy"},
         p.specularRoughnessAnisotropy);
  scalar({"transmission_weight"}, p.transmission);
  color({"transmission_color"}, p.transmissionColor);
  scalar({"transmission_depth"}, p.transmissionDepth);
  color({"transmission_scatter"}, p.transmissionScatter);
  scalar({"transmission_scatter_anisotropy"},
         p.transmissionScatterAnisotropy);
  scalar({"transmission_dispersion"}, p.transmissionDispersion);
  scalar({"transmission_dispersion_abbe_number"},
         p.transmissionDispersionAbbeNumber);
  scalar({"transmission_dispersion_scale"}, p.transmissionDispersionScale);
  scalar({"subsurface_weight"}, p.subsurface);
  color({"subsurface_color"}, p.subsurfaceColor);
  bool hasRadiusScaleParam = false;
  for (const DrawMaterialParamCPU& param : mat->params) {
    if (param.shader == "OpenPBRSurface" &&
        param.name == "subsurface_radius_scale" && param.texture < 0 &&
        param.renderTexture < 0) {
      hasRadiusScaleParam = true;
      break;
    }
  }
  const float radius = (p.subsurfaceRadius[0] + p.subsurfaceRadius[1] +
                        p.subsurfaceRadius[2]) /
                       3.0f;
  scalar({"subsurface_radius"}, hasRadiusScaleParam ? 1.0f : radius);
  if (hasRadiusScaleParam)
    color({"subsurface_radius_scale"}, p.subsurfaceRadius);
  scalar({"subsurface_scale"}, p.subsurfaceScale);
  scalar({"subsurface_anisotropy"}, p.subsurfaceAnisotropy);
  scalar({"subsurface_scatter_anisotropy"},
         p.subsurfaceScatterAnisotropy);
  scalar({"coat_weight"}, p.coatWeight);
  color({"coat_color"}, p.coatColor);
  scalar({"coat_roughness"}, p.coatRoughness);
  scalar({"coat_ior"}, p.coatIor);
  scalar({"coat_anisotropy"}, p.coatAnisotropy);
  scalar({"coat_rotation"}, p.coatRotation);
  scalar({"coat_affect_color"}, p.coatAffectColor);
  scalar({"coat_affect_roughness"}, p.coatAffectRoughness);
  scalar({"coat_roughness_anisotropy"}, p.coatRoughnessAnisotropy);
  scalar({"coat_darkening"}, p.coatDarkening);
  scalar({"sheen_weight", "fuzz_weight"}, p.sheenWeight);
  color({"sheen_color", "fuzz_color"}, p.sheenColor);
  scalar({"sheen_roughness", "fuzz_roughness"}, p.sheenRoughness);
  scalar({"thin_film_weight"}, p.thinFilmWeight);
  scalar({"thin_film_thickness"}, p.thinFilmThicknessNm);
  scalar({"thin_film_ior"}, p.thinFilmIor);
  scalar({"emission_luminance"}, p.emission);
  color({"emission_color"}, p.emissionColor);
  scalar({"geometry_opacity", "opacity"}, p.opacity);
}

void MakeConstantOpenPBRMaterial(DrawMaterialCPU* mat) {
  if (!mat || !mat->hasOpenPBRSurface) return;

  int* textureSlots[] = {
      &mat->baseColorTex,   &mat->metallicTex,      &mat->roughnessTex,
      &mat->normalTex,      &mat->coatNormalTex,    &mat->emissiveTex,
      &mat->opacityTex,     &mat->occlusionTex,     &mat->specularColorTex,
      &mat->coatWeightTex,  &mat->coatColorTex,     &mat->coatRoughnessTex};
  for (int* slot : textureSlots) *slot = -1;

  DrawTexSampleCPU* samples[] = {
      &mat->baseColorSample,  &mat->metallicSample,
      &mat->roughnessSample,  &mat->normalSample,
      &mat->coatNormalSample, &mat->emissiveSample,
      &mat->opacitySample,    &mat->occlusionSample,
      &mat->specularColorSample, &mat->coatWeightSample,
      &mat->coatColorSample,  &mat->coatRoughnessSample};
  for (DrawTexSampleCPU* sample : samples) sample->tex = -1;

  for (DrawMaterialParamCPU& param : mat->params) {
    if (param.shader == "OpenPBRSurface") {
      param.texture = -1;
      param.renderTexture = -1;
      param.sample.tex = -1;
    }
  }
  mat->materialXNodeGraphJson.clear();
  mat->materialXGraph = MaterialXGraphRuntimeCPU{};
  mat->lightRtOpenPBR.hasTextureInputs = false;
  mat->lightRtOpenPBR.hasNormalInput = false;
  mat->hasUsdPreviewSurface = false;
  mat->openPbrSpecularModel = true;
}

void BakeRealtimePbrMaterial(DrawMaterialCPU* mat) {
  if (!mat) return;
  DrawLightRtOpenPBRCPU p;
  if (mat->hasOpenPBRSurface) {
    BakeOpenPBRSurface(*mat, &p);
  } else if (mat->hasUsdPreviewSurface) {
    BakeUsdPreviewSurface(*mat, &p);
  } else {
    std::memcpy(p.baseColor, mat->baseColor, sizeof(p.baseColor));
    p.metalness = mat->metallic;
    p.specularRoughness = mat->roughness;
    std::memcpy(p.emissionColor, mat->emissive, sizeof(p.emissionColor));
    p.emission = (Luminance(p.emissionColor) > 0.0f) ? 1.0f : 0.0f;
    p.opacity = mat->alpha;
  }
  p.hasTextureInputs = HasTextureInput(*mat);
  p.hasNormalInput = HasNormalInput(*mat);
  p.volumeDensity = mat->volumeDensity;
  std::memcpy(p.volumeAlbedo, mat->volumeAlbedo, sizeof(p.volumeAlbedo));
  std::memcpy(p.volumeEmission, mat->volumeEmission, sizeof(p.volumeEmission));
  p.volumeEmissionScale = mat->volumeEmissionScale;
  ClampLightRtParams(&p);

  std::set<std::string> textureDeps = DirectTextureInputs(*mat);
  const GraphTextureDeps graphDeps =
      AnalyzeMaterialXJsonTextureDeps(mat->materialXNodeGraphJson);
  textureDeps.insert(graphDeps.textureInputs.begin(), graphDeps.textureInputs.end());
  if (!textureDeps.empty()) {
    p.hasTextureInputs = true;
  }
  if (TextureDepsIncludeNormal(textureDeps)) {
    p.hasNormalInput = true;
  }

  // If Tydra extracted a MaterialX node graph, use LightRT's evaluator for the
  // graph constants. For mixed graphs, preserve lanes driven by image/normalmap
  // nodes or direct runtime textures; LightRT's no-image texture stub would
  // otherwise bake MaterialX `default` values over live texture inputs.
  if (mat->hasOpenPBRSurface && !mat->materialXNodeGraphJson.empty()) {
    const float directThinFilmWeight = p.thinFilmWeight;
    const float directThinFilmThickness = p.thinFilmThicknessNm;
    DrawLightRtOpenPBRCPU graphParams;
    std::string graphErr;
    if (EvaluateMaterialXJsonGraphForMaterial(*mat, &graphParams, &graphErr)) {
      graphParams.hasTextureInputs = p.hasTextureInputs;
      graphParams.hasNormalInput = p.hasNormalInput ||
          TextureDepsIncludeNormal(textureDeps) ||
          (std::fabs(graphParams.normal[0]) > 0.0f ||
           std::fabs(graphParams.normal[1]) > 0.0f ||
           std::fabs(graphParams.normal[2] - 1.0f) > 1.0e-6f);
      MergeGraphParamsPreservingTextureDeps(graphParams, textureDeps, &p);
      // A graph evaluator starts from OpenPBR defaults (500 nm film), while a
      // converted Standard Surface with no authored film is explicitly 0 nm.
      // Do not let evaluation of an unrelated graph-connected lane (such as a
      // specular-color image) manufacture an iridescent film lobe.
      if (directThinFilmWeight <= 0.0f) {
        p.thinFilmWeight = 0.0f;
        p.thinFilmThicknessNm = directThinFilmThickness;
      }
      p.hasTextureInputs = graphParams.hasTextureInputs;
      p.hasNormalInput = graphParams.hasNormalInput;
      ClampLightRtParams(&p);
    }
  }

  mat->lightRtOpenPBR = p;
  mat->hasLightRtOpenPBR = true;

  // Keep legacy material slots aligned with the canonical OpenPBR block;
  // texture-backed lanes remain neutralized and are sampled at hit time.
  if (mat->baseColorTex < 0) {
    std::memcpy(mat->baseColor, p.baseColor, sizeof(mat->baseColor));
  }
  if (mat->metallicTex < 0) {
    mat->metallic = p.metalness;
  }
  if (mat->roughnessTex < 0) {
    mat->roughness = p.specularRoughness;
  }
  mat->ior = p.specularIor;
  if (mat->specularColorTex < 0) {
    mat->specularColor[0] = p.specularColor[0];
    mat->specularColor[1] = p.specularColor[1];
    mat->specularColor[2] = p.specularColor[2];
  }
  if (mat->emissiveTex < 0) {
    mat->emissive[0] = p.emissionColor[0] * p.emission;
    mat->emissive[1] = p.emissionColor[1] * p.emission;
    mat->emissive[2] = p.emissionColor[2] * p.emission;
  }
  mat->alpha = p.opacity;
  mat->coatWeight = p.coatWeight;
  mat->coatColor[0] = p.coatColor[0];
  mat->coatColor[1] = p.coatColor[1];
  mat->coatColor[2] = p.coatColor[2];
  mat->coatRoughness = p.coatRoughness;
  mat->coatIor = p.coatIor;
  FloatParam(*mat, {"UsdPreviewSurface"}, {"occlusion"}, &mat->occlusion);
}

void BakeLightRtOpenPBR(DrawMaterialCPU* mat) {
  BakeRealtimePbrMaterial(mat);
}

void PackLightRtOpenPBR(const DrawMaterialCPU& mat, float* dst) {
  if (!dst) return;
  DrawLightRtOpenPBRCPU fallback;
  const DrawLightRtOpenPBRCPU& m =
      mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR : fallback;
  tinyusdz::tydra::PackLightRtOpenPBRParams(
      m, mat.hasLightRtOpenPBR, static_cast<float>(mat.alphaMode),
      mat.alphaCutoff, dst);
}

void PackRtMaterialTextureParams(const DrawMaterialCPU& mat, float* dst) {
  if (!dst) return;
  std::fill(dst, dst + kRtMaterialTextureParamFloats, 0.0f);
  StoreUvCompact(mat.baseColorSample.uv, dst + 0);
  StoreUvCompact(mat.metallicSample.uv, dst + 6);
  StoreUvCompact(mat.roughnessSample.uv, dst + 12);
  StoreUvCompact(mat.normalSample.uv, dst + 18);
  StoreUvCompact(mat.emissiveSample.uv, dst + 24);
  StoreUvCompact(mat.opacitySample.uv, dst + 30);
  Store4(mat.baseColorSample.scale, dst + 36);
  Store4(mat.baseColorSample.bias, dst + 40);
  Store4(mat.normalSample.scale, dst + 44);
  Store4(mat.normalSample.bias, dst + 48);
  Store4(mat.emissiveSample.scale, dst + 52);
  Store4(mat.emissiveSample.bias, dst + 56);
  dst[60] = static_cast<float>(mat.metallicChannel);
  dst[61] = mat.metallicTexScale;
  dst[62] = mat.metallicTexBias;
  dst[63] = static_cast<float>(mat.roughnessChannel);
  dst[64] = mat.roughnessTexScale;
  dst[65] = mat.roughnessTexBias;
  dst[66] = static_cast<float>(mat.opacityChannel);
  dst[67] = mat.opacityTexScale;
  dst[68] = mat.opacityTexBias;
  // Per-slot UV set, bit-packed: base, metallic, roughness, normal, emissive,
  // opacity. The float stores a small exact integer.
  int uvSetBits = 0;
  if (mat.baseColorSample.uvSet == 1) uvSetBits |= 1;
  if (mat.metallicSample.uvSet == 1) uvSetBits |= 2;
  if (mat.roughnessSample.uvSet == 1) uvSetBits |= 4;
  if (mat.normalSample.uvSet == 1) uvSetBits |= 8;
  if (mat.emissiveSample.uvSet == 1) uvSetBits |= 16;
  if (mat.opacitySample.uvSet == 1) uvSetBits |= 32;
  dst[69] = static_cast<float>(uvSetBits);
  dst[70] = mat.occlusionTexScale;
  dst[71] = mat.occlusionTexBias;
  // Extra slots keep the same slot*6 UV-transform convention: 12 = occlusion,
  // 13 = coat weight, 14 = coat color, 15 = coat roughness.
  StoreUvCompact(mat.occlusionSample.uv, dst + 72);
  StoreUvCompact(mat.coatWeightSample.uv, dst + 78);
  StoreUvCompact(mat.coatColorSample.uv, dst + 84);
  StoreUvCompact(mat.coatRoughnessSample.uv, dst + 90);
  // Scalar slots default to channel 0 (R) when nothing was authored.
  dst[96] = static_cast<float>(mat.occlusionChannel < 0 ? 0
                                                        : mat.occlusionChannel);
  dst[97] = static_cast<float>(
      mat.coatWeightSample.channel < 0 ? 0 : mat.coatWeightSample.channel);
  dst[98] = static_cast<float>(mat.coatRoughnessSample.channel < 0
                                   ? 0
                                   : mat.coatRoughnessSample.channel);
  int uvSetBits2 = 0;
  if (mat.occlusionSample.uvSet == 1) uvSetBits2 |= 1;
  if (mat.coatWeightSample.uvSet == 1) uvSetBits2 |= 2;
  if (mat.coatColorSample.uvSet == 1) uvSetBits2 |= 4;
  if (mat.coatRoughnessSample.uvSet == 1) uvSetBits2 |= 8;
  dst[99] = static_cast<float>(uvSetBits2);
  Store4(mat.coatWeightSample.scale, dst + 100);
  Store4(mat.coatWeightSample.bias, dst + 104);
  Store4(mat.coatColorSample.scale, dst + 108);
  Store4(mat.coatColorSample.bias, dst + 112);
  Store4(mat.coatRoughnessSample.scale, dst + 116);
  Store4(mat.coatRoughnessSample.bias, dst + 120);
  StoreUvCompact(mat.specularColorSample.uv, dst + 124);
  dst[130] = static_cast<float>(mat.specularColorSample.uvSet);
  dst[131] = mat.useSpecularWorkflow ? 1.0f : 0.0f;
  Store4(mat.specularColorSample.scale, dst + 132);
  Store4(mat.specularColorSample.bias, dst + 136);
  dst[139] = mat.openPbrSpecularModel ? 1.0f : 0.0f;
  StoreUvCompact(mat.coatNormalSample.uv, dst + 140);
  dst[146] = static_cast<float>(mat.coatNormalSample.uvSet);
  Store4(mat.coatNormalSample.scale, dst + 147);
  Store4(mat.coatNormalSample.bias, dst + 151);
}

void PackMaterialXGraphRuntime(const DrawMaterialCPU& mat, float* dst,
                               const std::vector<int>* sourceToTable) {
  if (!dst) return;
  std::fill(dst, dst + kRtMaterialGraphFloats, 0.0f);
  for (int i = 0; i < kRtMaterialGraphOutputCount; ++i) dst[1 + i] = -1.0f;
  const MaterialXGraphRuntimeCPU& graph = mat.materialXGraph;
  if (!graph.valid) return;
  const size_t count = std::min<size_t>(graph.nodes.size(),
                                        kRtMaterialGraphMaxNodes);
  dst[0] = static_cast<float>(count);
  for (int i = 0; i < kRtMaterialGraphOutputCount; ++i)
    dst[1 + i] = static_cast<float>(graph.output[i]);
  for (size_t i = 0; i < count; ++i) {
    const MaterialXGraphNodeCPU& node = graph.nodes[i];
    const size_t base = kRtMaterialGraphHeaderFloats +
                        i * kRtMaterialGraphNodeFloats;
    dst[base + 0] = static_cast<float>(node.op);
    dst[base + 1] = static_cast<float>(node.input[0]);
    dst[base + 2] = static_cast<float>(node.input[1]);
    dst[base + 3] = static_cast<float>(node.input[2]);
    for (int input = 0; input < 3; ++input)
      for (int lane = 0; lane < 4; ++lane)
        dst[base + 4 + input * 4 + lane] = node.value[input][lane];
    const bool usesAuxInput = node.op == MaterialXGraphOpCPU::SplitLR ||
                              node.op == MaterialXGraphOpCPU::SplitTB ||
                              node.op == MaterialXGraphOpCPU::IfGreater ||
                              node.op == MaterialXGraphOpCPU::IfGreaterEqual ||
                              node.op == MaterialXGraphOpCPU::IfEqual;
    int textureId = usesAuxInput ? node.auxInput : node.textureId;
    if (!usesAuxInput && sourceToTable && textureId >= 0 &&
        static_cast<size_t>(textureId) < sourceToTable->size()) {
      textureId = (*sourceToTable)[static_cast<size_t>(textureId)];
    }
    dst[base + 16] = static_cast<float>(textureId);
    if (node.op == MaterialXGraphOpCPU::IfGreater ||
        node.op == MaterialXGraphOpCPU::IfGreaterEqual ||
        node.op == MaterialXGraphOpCPU::IfEqual) {
      for (int lane = 0; lane < 4; ++lane)
        dst[base + 17 + lane] = node.auxValue[lane];
      continue;
    }
    dst[base + 17] = node.uvScale[0];
    dst[base + 18] = node.uvScale[1];
    dst[base + 19] = node.uvOffset[0];
    dst[base + 20] = node.uvOffset[1];
  }
}

void PackRasterMaterialXGraphRuntime(const DrawMaterialCPU& mat, float* dst) {
  if (!dst) return;
  std::fill(dst, dst + kRtMaterialGraphFloats, 0.0f);
  for (int i = 0; i < kRtMaterialGraphOutputCount; ++i) dst[1 + i] = -1.0f;
  const MaterialXGraphRuntimeCPU& graph = mat.materialXGraph;
  if (!graph.valid) return;
  const size_t count = std::min<size_t>(graph.nodes.size(),
                                        kRtMaterialGraphMaxNodes);
  dst[0] = static_cast<float>(count);
  for (int i = 0; i < kRtMaterialGraphOutputCount; ++i)
    dst[1 + i] = static_cast<float>(graph.output[i]);
  std::vector<int> textureIds;
  textureIds.reserve(kRasterMaterialGraphImageCount);
  auto isUdim = [&](const MaterialXGraphNodeCPU& node) {
    if (node.isUdim) return true;
    const int id = node.textureId;
    return (id >= 0 && id == mat.baseColorTex && mat.baseColorSample.isUdim) ||
           (id >= 0 && id == mat.metallicTex && mat.metallicSample.isUdim) ||
           (id >= 0 && id == mat.roughnessTex && mat.roughnessSample.isUdim) ||
           (id >= 0 && id == mat.normalTex && mat.normalSample.isUdim) ||
           (id >= 0 && id == mat.emissiveTex && mat.emissiveSample.isUdim) ||
           (id >= 0 && id == mat.opacityTex && mat.opacitySample.isUdim) ||
           (id >= 0 && id == mat.occlusionTex && mat.occlusionSample.isUdim) ||
           (id >= 0 && id == mat.specularColorTex && mat.specularColorSample.isUdim) ||
           (id >= 0 && id == mat.coatWeightTex && mat.coatWeightSample.isUdim) ||
           (id >= 0 && id == mat.coatColorTex && mat.coatColorSample.isUdim) ||
           (id >= 0 && id == mat.coatRoughnessTex && mat.coatRoughnessSample.isUdim) ||
           (id >= 0 && id == mat.coatNormalTex && mat.coatNormalSample.isUdim);
  };
  for (size_t i = 0; i < count; ++i) {
    const MaterialXGraphNodeCPU& node = graph.nodes[i];
    const size_t base = kRtMaterialGraphHeaderFloats +
                        i * kRtMaterialGraphNodeFloats;
    dst[base + 0] = static_cast<float>(node.op);
    dst[base + 1] = static_cast<float>(node.input[0]);
    dst[base + 2] = static_cast<float>(node.input[1]);
    dst[base + 3] = static_cast<float>(node.input[2]);
    for (int input = 0; input < 3; ++input)
      for (int lane = 0; lane < 4; ++lane)
        dst[base + 4 + input * 4 + lane] = node.value[input][lane];
    if (node.op == MaterialXGraphOpCPU::SplitLR ||
        node.op == MaterialXGraphOpCPU::SplitTB ||
        node.op == MaterialXGraphOpCPU::IfGreater ||
        node.op == MaterialXGraphOpCPU::IfGreaterEqual ||
        node.op == MaterialXGraphOpCPU::IfEqual) {
      dst[base + 16] = static_cast<float>(node.auxInput);
      if (node.op == MaterialXGraphOpCPU::IfGreater ||
          node.op == MaterialXGraphOpCPU::IfGreaterEqual ||
          node.op == MaterialXGraphOpCPU::IfEqual)
        for (int lane = 0; lane < 4; ++lane)
          dst[base + 17 + lane] = node.auxValue[lane];
      continue;
    }
    if (node.textureId < 0) {
      dst[base + 16] = -1.0f;
      continue;
    }
    auto found = std::find(textureIds.begin(), textureIds.end(), node.textureId);
    if (found == textureIds.end()) {
      if (textureIds.size() >= kRasterMaterialGraphImageCount) {
        dst[base + 16] = -1.0f;
        continue;
      }
      textureIds.push_back(node.textureId);
      found = textureIds.end() - 1;
    }
    const int local = static_cast<int>(found - textureIds.begin());
    dst[base + 16] = isUdim(node) ? -static_cast<float>(local + 1)
                                  : static_cast<float>(local);
    // The existing fixed record has no spare lane. For UDIM image nodes the
    // fallback alpha is not observable, so value.w carries the source texture
    // row used by the shared raster UDIM LUT.
    if (isUdim(node)) dst[base + 7] = static_cast<float>(node.textureId);
    dst[base + 17] = node.uvScale[0];
    dst[base + 18] = node.uvScale[1];
    dst[base + 19] = node.uvOffset[0];
    dst[base + 20] = node.uvOffset[1];
  }
}

void PackRasterMaterialTextureParams(const DrawMaterialCPU& mat, float* dst) {
  if (!dst) return;
  std::fill(dst, dst + kRasterMaterialTextureParamFloats, 0.0f);
  dst[67 * 4 + 0] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.transmission : 0.0f;
  dst[67 * 4 + 1] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.transmissionDepth : 0.0f;
  dst[67 * 4 + 2] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.transmissionDispersion : 0.0f;
  dst[68 * 4 + 0] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.transmissionColor[0] : 1.0f;
  dst[68 * 4 + 1] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.transmissionColor[1] : 1.0f;
  dst[68 * 4 + 2] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.transmissionColor[2] : 1.0f;
  dst[69 * 4 + 0] = mat.volumeDensity;
  dst[69 * 4 + 1] = mat.volumeEmissionScale;
  dst[70 * 4 + 0] = mat.volumeAlbedo[0];
  dst[70 * 4 + 1] = mat.volumeAlbedo[1];
  dst[70 * 4 + 2] = mat.volumeAlbedo[2];
  dst[71 * 4 + 0] = mat.volumeEmission[0];
  dst[71 * 4 + 1] = mat.volumeEmission[1];
  dst[71 * 4 + 2] = mat.volumeEmission[2];
  dst[72 * 4 + 0] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.subsurface : 0.0f;
  dst[72 * 4 + 1] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.subsurfaceScale : 1.0f;
  // Raster keeps a scalar radius for its bounded diffusion approximation;
  // reduce the authored OpenPBR radius color consistently with the other
  // scalar color reductions instead of silently discarding G/B channels.
  dst[72 * 4 + 2] = mat.hasLightRtOpenPBR
                        ? (0.2126f * mat.lightRtOpenPBR.subsurfaceRadius[0] +
                           0.7152f * mat.lightRtOpenPBR.subsurfaceRadius[1] +
                           0.0722f * mat.lightRtOpenPBR.subsurfaceRadius[2])
                        : 1.0f;
  dst[73 * 4 + 0] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.subsurfaceColor[0] : 1.0f;
  dst[73 * 4 + 1] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.subsurfaceColor[1] : 1.0f;
  dst[73 * 4 + 2] = mat.hasLightRtOpenPBR ? mat.lightRtOpenPBR.subsurfaceColor[2] : 1.0f;
  StoreUvVec4Rows(mat.baseColorSample.uv, dst + 0 * 4);
  StoreUvVec4Rows(mat.metallicSample.uv, dst + 2 * 4);
  StoreUvVec4Rows(mat.normalSample.uv, dst + 4 * 4);
  StoreUvVec4Rows(mat.emissiveSample.uv, dst + 6 * 4);
  StoreUvVec4Rows(mat.displacementUv, dst + 8 * 4);
  Store4(mat.baseColorSample.scale, dst + 10 * 4);
  Store4(mat.baseColorSample.bias, dst + 11 * 4);
  Store4(mat.normalSample.scale, dst + 12 * 4);
  Store4(mat.normalSample.bias, dst + 13 * 4);
  Store4(mat.emissiveSample.scale, dst + 14 * 4);
  Store4(mat.emissiveSample.bias, dst + 15 * 4);
  dst[16 * 4 + 0] = static_cast<float>(mat.metallicChannel);
  dst[16 * 4 + 1] = static_cast<float>(mat.roughnessChannel);
  dst[16 * 4 + 2] = mat.metallicTexScale;
  dst[16 * 4 + 3] = mat.metallicTexBias;
  dst[17 * 4 + 0] = mat.roughnessTexScale;
  dst[17 * 4 + 1] = mat.roughnessTexBias;
  // Ptex and UDIM displacement are baked before raster upload. Disable the
  // vertex-stage sample so the baked surface is not moved a second time (the
  // vertex stage cannot select a Ptex face and the CPU bake handles both paths).
  dst[17 * 4 + 2] = (mat.displacementSample.isPtex ||
                     mat.displacementSample.isUdim)
                         ? 0.0f
                         : mat.displacementTexScale;
  dst[17 * 4 + 3] = (mat.displacementSample.isPtex ||
                     mat.displacementSample.isUdim)
                         ? 0.0f
                         : mat.displacementTexBias;
  // Per-slot UV set. Displacement stays on uv0: it is sampled in the vertex /
  // tessellation stages, which do not carry the second set.
  dst[18 * 4 + 0] = static_cast<float>(mat.baseColorSample.uvSet);
  dst[18 * 4 + 1] = static_cast<float>(mat.metallicSample.uvSet);
  dst[18 * 4 + 2] = static_cast<float>(mat.normalSample.uvSet);
  dst[18 * 4 + 3] = static_cast<float>(mat.emissiveSample.uvSet);
  // Specular F0 (T12): rgb = inputs:specularColor, w = ior with the specular-
  // workflow flag folded into its SIGN (w < 0 => use specularColor directly as
  // F0; w >= 0 => dielectric F0 from |ior|, lerped to base by metalness). ior is
  // always positive, so the sign is a free flag and no push-constant lane is
  // needed.
  dst[19 * 4 + 0] = mat.specularColor[0];
  dst[19 * 4 + 1] = mat.specularColor[1];
  dst[19 * 4 + 2] = mat.specularColor[2];
  dst[19 * 4 + 3] = mat.useSpecularWorkflow
                         ? -mat.ior
                         : (mat.openPbrSpecularModel ? mat.ior + 100.0f
                                                     : mat.ior);
  StoreUvVec4Rows(mat.opacitySample.uv, dst + 20 * 4);
  dst[22 * 4 + 0] = static_cast<float>(mat.opacityChannel);
  dst[22 * 4 + 1] = mat.opacityTexScale;
  dst[22 * 4 + 2] = mat.opacityTexBias;
  dst[22 * 4 + 3] = static_cast<float>(mat.opacitySample.uvSet);
  // Keep atlas coordinates valid even for an unbound slot. Feature flags gate
  // all actual samples, but some software Vulkan compilers speculate both sides
  // of the texture branch and otherwise form a negative image coordinate.
  dst[23 * 4 + 0] = static_cast<float>(std::max(mat.baseColorTex, 0));
  dst[23 * 4 + 1] = static_cast<float>(std::max(mat.metallicTex, 0));
  dst[23 * 4 + 2] = static_cast<float>(std::max(mat.normalTex, 0));
  dst[23 * 4 + 3] = static_cast<float>(std::max(mat.emissiveTex, 0));
  dst[24 * 4 + 0] = static_cast<float>(std::max(mat.opacityTex, 0));
  dst[24 * 4 + 1] = static_cast<float>(std::max(mat.roughnessTex, 0));
  StoreUvVec4Rows(mat.roughnessSample.uv, dst + 25 * 4);
  // roughUv0.w is otherwise padding and carries its UV-set selector.
  dst[25 * 4 + 3] = static_cast<float>(mat.roughnessSample.uvSet);
  dst[27 * 4 + 0] = mat.coatWeight;
  dst[27 * 4 + 1] = mat.coatRoughness;
  dst[27 * 4 + 2] = mat.coatIor;
  dst[27 * 4 + 3] = mat.occlusion;
  dst[28 * 4 + 0] = mat.coatColor[0];
  dst[28 * 4 + 1] = mat.coatColor[1];
  dst[28 * 4 + 2] = mat.coatColor[2];
  StoreUvVec4Rows(mat.occlusionSample.uv, dst + 29 * 4);
  dst[31 * 4 + 0] = static_cast<float>(mat.occlusionChannel);
  dst[31 * 4 + 1] = mat.occlusionTexScale;
  dst[31 * 4 + 2] = mat.occlusionTexBias;
  dst[31 * 4 + 3] = static_cast<float>(mat.occlusionSample.uvSet);
  dst[24 * 4 + 2] = static_cast<float>(std::max(mat.occlusionTex, 0));
  // The ordinary-binding push flag distinguishes a 2D specular map from a
  // UDIM map; retain -1 here only when the semantic slot is genuinely absent.
  dst[24 * 4 + 3] = static_cast<float>(mat.specularColorTex);
  // Extra semantic slots. The loaders neutralize the matching constant to 1.0
  // when a texture is bound, so the shader always multiplies constant * texel.
  StoreUvVec4Rows(mat.specularColorSample.uv, dst + 32 * 4);
  StoreUvVec4Rows(mat.coatWeightSample.uv, dst + 34 * 4);
  StoreUvVec4Rows(mat.coatColorSample.uv, dst + 36 * 4);
  StoreUvVec4Rows(mat.coatRoughnessSample.uv, dst + 38 * 4);
  // A negative channel selector means "whole value"; the scalar coat slots
  // default that to channel 0 (R).
  dst[40 * 4 + 0] = static_cast<float>(
      mat.coatWeightSample.channel < 0 ? 0 : mat.coatWeightSample.channel);
  dst[40 * 4 + 1] = static_cast<float>(
      mat.coatRoughnessSample.channel < 0 ? 0
                                          : mat.coatRoughnessSample.channel);
  dst[40 * 4 + 2] = static_cast<float>(mat.coatWeightSample.uvSet);
  dst[40 * 4 + 3] = static_cast<float>(mat.coatRoughnessSample.uvSet);
  dst[41 * 4 + 0] = static_cast<float>(mat.specularColorSample.uvSet);
  dst[41 * 4 + 1] = static_cast<float>(mat.coatColorSample.uvSet);
  Store4(mat.specularColorSample.scale, dst + 42 * 4);
  Store4(mat.specularColorSample.bias, dst + 43 * 4);
  Store4(mat.coatWeightSample.scale, dst + 44 * 4);
  Store4(mat.coatWeightSample.bias, dst + 45 * 4);
  Store4(mat.coatColorSample.scale, dst + 46 * 4);
  Store4(mat.coatColorSample.bias, dst + 47 * 4);
  Store4(mat.coatRoughnessSample.scale, dst + 48 * 4);
  Store4(mat.coatRoughnessSample.bias, dst + 49 * 4);
  StoreUvVec4Rows(mat.coatNormalSample.uv, dst + 50 * 4);
  Store4(mat.coatNormalSample.scale, dst + 52 * 4);
  Store4(mat.coatNormalSample.bias, dst + 53 * 4);
  dst[53 * 4 + 3] = static_cast<float>(mat.coatNormalSample.uvSet);
  dst[52 * 4 + 3] = mat.coatNormalTex >= 0 ? 1.0f : 0.0f;
  dst[54 * 4 + 0] = static_cast<float>(mat.specularColorTex);
  dst[54 * 4 + 1] = static_cast<float>(mat.coatWeightTex);
  dst[54 * 4 + 2] = static_cast<float>(mat.coatColorTex);
  dst[54 * 4 + 3] = static_cast<float>(mat.coatRoughnessTex);
  dst[55 * 4 + 0] = mat.coatNormalSample.isUdim
                         ? static_cast<float>(mat.coatNormalTex)
                         : -1.0f;
  dst[55 * 4 + 1] = mat.displacementSample.isUdim
                         ? static_cast<float>(mat.displacementTex)
                         : -1.0f;
  // Ptex base-color atlas: (rect texel offset, face count, enabled, reserved).
  // The face id itself is fetched from the per-triangle source-face SSBO.
  dst[56 * 4 + 0] = mat.baseColorSample.isPtex
                         ? static_cast<float>(
                               mat.baseColorSample.ptexRectTexelOffset)
                         : 0.0f;
  dst[56 * 4 + 1] = mat.baseColorSample.isPtex
                         ? static_cast<float>(mat.baseColorSample.ptexFaceCount)
                         : 0.0f;
  dst[56 * 4 + 2] = mat.baseColorSample.isPtex
                         ? 1.0f
                         : 0.0f;
  dst[56 * 4 + 3] = 0.0f;
  auto packPtexInfo = [dst](int slot, const DrawTexSampleCPU& sample) {
    dst[slot * 4 + 0] = sample.isPtex
                             ? static_cast<float>(sample.ptexRectTexelOffset)
                             : 0.0f;
    dst[slot * 4 + 1] = sample.isPtex
                             ? static_cast<float>(sample.ptexFaceCount)
                             : 0.0f;
    dst[slot * 4 + 2] = sample.isPtex ? 1.0f : 0.0f;
    dst[slot * 4 + 3] = 0.0f;
  };
  packPtexInfo(57, mat.metallicSample);
  packPtexInfo(58, mat.roughnessSample);
  packPtexInfo(59, mat.normalSample);
  packPtexInfo(60, mat.emissiveSample);
  packPtexInfo(61, mat.opacitySample);
  packPtexInfo(62, mat.occlusionSample);
  packPtexInfo(63, mat.specularColorSample);
  packPtexInfo(64, mat.coatWeightSample);
  packPtexInfo(65, mat.coatColorSample);
  packPtexInfo(66, mat.coatRoughnessSample);
}

}  // namespace tusdview
