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

uint32_t MaterialXGeomPropHash(const std::string& name) {
  uint32_t h = 2166136261u;
  for (unsigned char c : name) {
    h ^= static_cast<uint32_t>(c);
    h *= 16777619u;
  }
  return h;
}
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
         s == "matrix33" || s == "matrix44" ||
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

bool ScatterModeHas(const std::string& mode, char component) {
  if (mode == "R") return component == 'R';
  if (mode == "T") return component == 'T';
  if (mode == "RT") return component == 'R' || component == 'T';
  return false;
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
                                    std::string* err,
                                    bool volume_graph = false) {
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

  if (!volume_graph) {
    ss << "  <open_pbr_surface name=\"tusdview_openpbr\" type=\"surfaceshader\">\n";
  }
  std::set<std::string> emitted;
  auto emit_graph_input = [&](const char* input,
                              const char* type, const MtlxConnection& conn) {
    ss << "    <input name=\"" << input << "\" type=\"" << type
       << "\" nodegraph=\"" << XmlEscape(conn.graph)
       << "\" output=\"" << XmlEscape(conn.output) << "\"/>\n";
  };
  if (volume_graph) {
    // The runtime representation stores density and albedo, while MaterialX
    // volume evaluation consumes absorption and scattering. Build that small
    // conversion explicitly so composed graph outputs retain their physical
    // meaning instead of being mistaken for raw extinction coefficients.
    const auto density = connections.find("volume_density");
    const auto albedo = connections.find("volume_albedo");
    if (density != connections.end() && albedo != connections.end()) {
      ss << "  <subtract name=\"tusdview_volume_one_minus\" type=\"color3\">\n"
         << "    <input name=\"in1\" type=\"color3\" value=\"1,1,1\"/>\n";
      emit_graph_input("in2", "color3", albedo->second);
      ss << "  </subtract>\n"
         << "  <multiply name=\"tusdview_volume_absorption\" type=\"color3\">\n";
      emit_graph_input("in1", "color3", density->second);
      ss << "    <input name=\"in2\" type=\"color3\" nodename=\"tusdview_volume_one_minus\"/>\n"
         << "  </multiply>\n"
         << "  <multiply name=\"tusdview_volume_scattering\" type=\"color3\">\n";
      emit_graph_input("in1", "color3", density->second);
      emit_graph_input("in2", "color3", albedo->second);
      ss << "  </multiply>\n";
    }
    ss << "  <anisotropic_vdf name=\"tusdview_vdf\" type=\"VDF\">\n";
    if (density != connections.end() && albedo != connections.end()) {
      ss << "    <input name=\"absorption\" type=\"color3\" nodename=\"tusdview_volume_absorption\"/>\n"
         << "    <input name=\"scattering\" type=\"color3\" nodename=\"tusdview_volume_scattering\"/>\n";
    } else if (density != connections.end()) {
      emit_graph_input("absorption", "color3", density->second);
    }
    ss << "  </anisotropic_vdf>\n";
    const auto emission = connections.find("volume_emission_color");
    const auto emission_scale = connections.find("volume_emission_scale");
    if (emission != connections.end()) {
      if (emission_scale != connections.end()) {
        ss << "  <multiply name=\"tusdview_volume_emission\" type=\"color3\">\n";
        emit_graph_input("in1", "color3", emission->second);
        emit_graph_input("in2", "color3", emission_scale->second);
        ss << "  </multiply>\n";
      }
      ss << "  <uniform_edf name=\"tusdview_edf\" type=\"EDF\">\n";
      if (emission_scale != connections.end()) {
        ss << "    <input name=\"color\" type=\"color3\" "
              "nodename=\"tusdview_volume_emission\"/>\n";
      } else {
        emit_graph_input("color", "color3", emission->second);
      }
      ss << "  </uniform_edf>\n";
    }
    ss << "  <volume name=\"tusdview_volume\" type=\"volumeshader\">\n"
       << "    <input name=\"vdf\" type=\"VDF\" nodename=\"tusdview_vdf\"/>\n";
    if (emission != connections.end())
      ss << "    <input name=\"edf\" type=\"EDF\" nodename=\"tusdview_edf\"/>\n";
    ss << "  </volume>\n";
  } else for (const auto& conn : connections) {
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
  if (!volume_graph) for (const DrawMaterialParamCPU& p : mat.params) {
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
  if (volume_graph) {
    ss << "  <volumematerial name=\"tusdview_material\" type=\"material\">\n";
    ss << "    <input name=\"volumeshader\" type=\"volumeshader\" "
          "nodename=\"tusdview_volume\"/>\n";
    ss << "  </volumematerial>\n";
  } else {
    ss << "  </open_pbr_surface>\n";
    ss << "  <surfacematerial name=\"tusdview_material\" type=\"material\">\n";
    ss << "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
          "nodename=\"tusdview_openpbr\"/>\n";
    ss << "  </surfacematerial>\n";
  }
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

bool EvaluateMaterialXJsonGraphForVolume(const DrawMaterialCPU& mat,
                                         DrawMaterialCPU* out,
                                         std::string* err) {
  if (!out) {
    if (err) *err = "Output pointer is null";
    return false;
  }
  DrawMaterialCPU graph = mat;
  graph.materialXNodeGraphJson = mat.volumeMaterialXNodeGraphJson;
  std::string xml;
  if (!BuildMaterialXXmlFromJsonGraph(graph, &xml, err, true)) return false;
  std::string baseDir = ".";
  const size_t slash = mat.absPath.find_last_of("/\\");
  if (slash != std::string::npos) {
    baseDir = mat.absPath.substr(0, slash);
    if (baseDir.empty()) baseDir = ".";
  }
  return EvaluateMaterialXStringToLightRtVolumeWithBaseDir(
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
        std::strcmp(c, "surface_unlit") == 0 ||
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
  ctx.V = v3_make(0.0f, 0.0f, 1.0f);
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

bool EvaluateMaterialXStringToLightRtVolume(const char* xml,
                                            const char* materialName,
                                            DrawMaterialCPU* out,
                                            std::string* err) {
  return EvaluateMaterialXStringToLightRtVolumeWithBaseDir(
      xml, materialName, nullptr, out, err);
}

bool EvaluateMaterialXStringToLightRtVolumeWithBaseDir(
    const char* xml, const char* materialName, const char* baseDir,
    DrawMaterialCPU* out, std::string* err) {
  if (err) err->clear();
  if (!xml || !xml[0] || !out) {
    if (err) *err = !out ? "Output pointer is null" :
                           "MaterialX XML string is empty";
    return false;
  }
  MtlxDoc* doc = mtlx_load_string(xml);
  if (!doc) {
    if (err) *err = "mtlx_load_string failed";
    return false;
  }
  int material = materialName && materialName[0]
      ? mtlx_find_material(doc, materialName) : (doc->nmat == 1 ? 0 : -1);
  if (material < 0 || doc->mats[material].volume_node < 0) {
    if (err) *err = "MaterialX volume material or shader not found";
    mtlx_free(doc);
    return false;
  }
  TextureCache* tex = texcache_create(baseDir);
  ShadeContext ctx{};
  ctx.doc = doc;
  ctx.tex = tex;
  ctx.uv[0] = 0.5f;
  ctx.uv[1] = 0.5f;
  ctx.P = v3_make(0.0f, 0.0f, 0.0f);
  ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
  ctx.Ng = ctx.Ns;
  ctx.V = ctx.Ns;
  std::vector<MtlxValue> memo(static_cast<size_t>(doc->nnode));
  std::vector<char> done(static_cast<size_t>(doc->nnode), 0);
  ctx.memo = memo.data();
  ctx.memo_done = done.data();
  MtlxVolumeParams volume{};
  const bool ok = mtlx_eval_volume(
      &ctx, doc->mats[material].volume_node, &volume) == 0;
  texcache_free(tex);
  mtlx_free(doc);
  if (!ok) {
    if (err) *err = "mtlx_eval_volume failed";
    return false;
  }
  const float extinction[3] = {
      volume.absorption.x + volume.scattering.x,
      volume.absorption.y + volume.scattering.y,
      volume.absorption.z + volume.scattering.z};
  out->volumeDensity = std::max(extinction[0],
      std::max(extinction[1], extinction[2]));
  const float scattering[3] = {volume.scattering.x, volume.scattering.y,
                               volume.scattering.z};
  for (int i = 0; i < 3; ++i) {
    out->volumeAlbedo[i] = extinction[i] > 1.0e-8f
        ? scattering[i] / extinction[i] : 0.0f;
  }
  out->volumeEmission[0] = volume.emission.x;
  out->volumeEmission[1] = volume.emission.y;
  out->volumeEmission[2] = volume.emission.z;
  out->volumeAnisotropy = volume.anisotropy;
  out->volumeEmissionScale = v3_maxc(volume.emission) > 0.0f ? 1.0f : 0.0f;
  out->hasVolumeOutput = true;
  return true;
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
  using ClosureLaneMap = std::map<std::string, std::string>;
  std::map<std::string, ClosureLaneMap> closureLanes;
  std::map<std::string, nlohmann::json> sourceNodes;
  for (const nlohmann::json& node : *nodesIt) {
    const std::string name = JsonString(node, "name");
    if (!name.empty()) sourceNodes[name] = node;
  }
  auto isClosureType = [](const std::string& type) {
    return type == "BSDF" || type == "EDF" || type == "VDF" ||
           type == "bsdf" || type == "edf" || type == "vdf";
  };
  auto nodeInput = [&](const nlohmann::json& node, const char* name,
                       const nlohmann::json& value) {
    return inputNamed(node, name,
                      nlohmann::json{{"name", name}, {"value", value}});
  };
  auto emitClosureLane = [&](const std::string& owner, const std::string& lane,
                             nlohmann::json input, const char* type) {
    const std::string output = owner + "__closure_" + lane;
    runtimeNodes.push_back({{"name", output}, {"category", "convert"},
                            {"type", type},
                            {"inputs", nlohmann::json::array(
                                {renamedInput(std::move(input), "in")})}});
    closureLanes[owner][lane] = output;
  };
  std::set<std::string> closureVisiting;
  std::function<const ClosureLaneMap&(const std::string&)> lowerClosure;
  lowerClosure = [&](const std::string& name) -> const ClosureLaneMap& {
    auto ready = closureLanes.find(name);
    if (ready != closureLanes.end()) return ready->second;
    ClosureLaneMap& lanes = closureLanes[name];
    const auto sourceIt = sourceNodes.find(name);
    if (sourceIt == sourceNodes.end() || !closureVisiting.insert(name).second)
      return lanes;
    const nlohmann::json& node = sourceIt->second;
    const std::string rawType = JsonString(node, "type");
    const std::string type = NormalizeMtlxType(rawType);
    const std::string cat = NormalizeMtlxCategory(JsonString(node, "category"),
                                                  JsonString(node, "type"));
    auto connectedClosure = [&](const char* inputName) -> std::string {
      const nlohmann::json input = nodeInput(node, inputName, 0.0);
      return JsonString(input, "nodename");
    };
    auto emitLeaf = [&](const char* lane, const char* inputName,
                        const nlohmann::json& fallback, const char* laneType) {
      nlohmann::json input = nodeInput(node, inputName, fallback);
      const auto valueIt = input.find("value");
      const std::string inputType = NormalizeMtlxType(JsonString(input, "type"));
      const bool vector2Scalar = std::strcmp(laneType, "float") == 0 &&
          (inputType == "vector2" ||
           (valueIt != input.end() && valueIt->is_array() &&
            valueIt->size() == 2));
      if (vector2Scalar) {
        const std::string extracted = name + "__closure_" + inputName + "_x";
        runtimeNodes.push_back({
            {"name", extracted}, {"category", "extract"}, {"type", "float"},
            {"inputs", nlohmann::json::array({
                renamedInput(input, "in"),
                nlohmann::json{{"name", "index"}, {"value", 0}}})}});
        input = nlohmann::json{{"name", inputName}, {"nodename", extracted}};
      }
      emitClosureLane(name, lane, std::move(input), laneType);
    };
    const bool closureTyped = isClosureType(type) || isClosureType(rawType) ||
        rawType.find("_bsdf") != std::string::npos ||
        rawType.find("_edf") != std::string::npos ||
        rawType.find("_vdf") != std::string::npos;
    if ((cat == "add" || cat == "mix" || cat == "multiply" || cat == "layer") &&
        closureTyped) {
      std::string aName, bName;
      if (cat == "layer") { aName = connectedClosure("base"); bName = connectedClosure("top"); }
      else if (cat == "mix") { aName = connectedClosure("bg"); bName = connectedClosure("fg"); }
      else { aName = connectedClosure("in1"); bName = connectedClosure("in2"); }
      const ClosureLaneMap& a = lowerClosure(aName);
      const ClosureLaneMap& b = lowerClosure(bName);
      std::set<std::string> keys;
      for (const auto& item : a) keys.insert(item.first);
      for (const auto& item : b) keys.insert(item.first);
      const char* scalarInput = nullptr;
      if (cat == "mix") scalarInput = "mix";
      else if (cat == "multiply")
        scalarInput = aName.empty() ? "in1" : "in2";
      const nlohmann::json factor = cat == "mix"
          ? nodeInput(node, scalarInput, 0.5)
          : nodeInput(node, scalarInput ? scalarInput : "in2", 1.0);
      auto roughnessWeightLane = [](const std::string& lane) -> const char* {
        if (lane == "base_diffuse_roughness") return "base_weight";
        if (lane == "specular_roughness") return "specular_weight";
        if (lane == "sheen_roughness") return "sheen_weight";
        return nullptr;
      };
      for (const std::string& lane : keys) {
        const auto ai = a.find(lane), bi = b.find(lane);
        const std::string output = name + "__closure_" + lane;
        const char* laneType = lane.find("color") != std::string::npos ||
                               lane.find("radius") != std::string::npos ||
                               lane.find("albedo") != std::string::npos
                                   ? "color3" : "float";
        nlohmann::json inputs = nlohmann::json::array();
        if (cat == "multiply") {
          const std::string& source = ai != a.end() ? ai->second : bi->second;
          if (roughnessWeightLane(lane)) {
            lanes[lane] = source;
            continue;
          }
          inputs.push_back({{"name", "in1"}, {"nodename", source}});
          inputs.push_back(renamedInput(factor, "in2"));
        } else if (roughnessWeightLane(lane) && ai != a.end() &&
                   bi != b.end()) {
          const char* weightLane = roughnessWeightLane(lane);
          const auto aw = a.find(weightLane), bw = b.find(weightLane);
          if (aw != a.end() && bw != b.end()) {
            const std::string weightedA = output + "__weighted_a";
            const std::string weightedB = output + "__weighted_b";
            const std::string numerator = output + "__weighted_sum";
            const std::string denominator = output + "__weight_sum";
            runtimeNodes.push_back({
                {"name", weightedA}, {"category", "multiply"},
                {"type", "float"}, {"inputs", nlohmann::json::array({
                    nlohmann::json{{"name", "in1"}, {"nodename", ai->second}},
                    nlohmann::json{{"name", "in2"}, {"nodename", aw->second}}})}});
            runtimeNodes.push_back({
                {"name", weightedB}, {"category", "multiply"},
                {"type", "float"}, {"inputs", nlohmann::json::array({
                    nlohmann::json{{"name", "in1"}, {"nodename", bi->second}},
                    nlohmann::json{{"name", "in2"}, {"nodename", bw->second}}})}});
            runtimeNodes.push_back({
                {"name", numerator}, {"category", "add"},
                {"type", "float"}, {"inputs", nlohmann::json::array({
                    nlohmann::json{{"name", "in1"}, {"nodename", weightedA}},
                    nlohmann::json{{"name", "in2"}, {"nodename", weightedB}}})}});
            runtimeNodes.push_back({
                {"name", denominator}, {"category", "add"},
                {"type", "float"}, {"inputs", nlohmann::json::array({
                    nlohmann::json{{"name", "in1"}, {"nodename", aw->second}},
                    nlohmann::json{{"name", "in2"}, {"nodename", bw->second}}})}});
            runtimeNodes.push_back({
                {"name", output}, {"category", "divide"},
                {"type", "float"}, {"inputs", nlohmann::json::array({
                    nlohmann::json{{"name", "in1"}, {"nodename", numerator}},
                    nlohmann::json{{"name", "in2"}, {"nodename", denominator}}})}});
            lanes[lane] = output;
            continue;
          }
          inputs.push_back({{"name", "in1"}, {"nodename", ai->second}});
          inputs.push_back({{"name", "in2"}, {"nodename", bi->second}});
        } else {
          inputs.push_back(ai != a.end()
              ? nlohmann::json{{"name", "in1"}, {"nodename", ai->second}}
              : nlohmann::json{{"name", "in1"}, {"value", 0.0}});
          inputs.push_back(bi != b.end()
              ? nlohmann::json{{"name", "in2"}, {"nodename", bi->second}}
              : nlohmann::json{{"name", "in2"}, {"value", 0.0}});
          if (cat == "mix") inputs.push_back(renamedInput(factor, "mix"));
        }
        runtimeNodes.push_back({{"name", output},
                                {"category", cat == "layer" ? "add" : cat},
                                {"type", laneType}, {"inputs", std::move(inputs)}});
        lanes[lane] = output;
      }
    } else if (cat == "light") {
      // A MaterialX light is a wrapper around an EDF. Preserve the closure
      // lanes and apply the light's color intensity plus EV exposure in the
      // same bounded arithmetic used by the direct EDF path.
      const ClosureLaneMap& edf = lowerClosure(connectedClosure("edf"));
      for (const auto& item : edf) lanes[item.first] = item.second;
      const std::string gain = name + "__light_gain";
      const std::string exposure = name + "__light_exposure";
      runtimeNodes.push_back({{"name", exposure}, {"category", "power"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in1"}, {"value", 2.0}},
                                  renamedInput(nodeInput(node, "exposure", 0.0), "in2")})}});
      runtimeNodes.push_back({{"name", gain}, {"category", "multiply"},
                              {"type", "color3"}, {"inputs", nlohmann::json::array({
                                  renamedInput(nodeInput(node, "intensity", nlohmann::json::array({1, 1, 1})), "in1"),
                                  nlohmann::json{{"name", "in2"}, {"nodename", exposure}}})}});
      auto multiplyLane = [&](const char* lane, const char* type,
                              const std::string& factor) {
        const auto source = lanes.find(lane);
        if (source == lanes.end()) return;
        const std::string output = name + "__light_" + lane;
        runtimeNodes.push_back({{"name", output}, {"category", "multiply"},
                                {"type", type}, {"inputs", nlohmann::json::array({
                                    nlohmann::json{{"name", "in1"}, {"nodename", source->second}},
                                    nlohmann::json{{"name", "in2"}, {"nodename", factor}}})}});
        lanes[lane] = output;
      };
      multiplyLane("emission_color", "color3", gain);
      multiplyLane("volume_emission_color", "color3", gain);
      multiplyLane("emission_luminance", "float", exposure);
      multiplyLane("volume_emission_scale", "float", exposure);
    } else if (cat == "oren_nayar_diffuse_bsdf" ||
               cat == "burley_diffuse_bsdf") {
      emitLeaf("base_weight", "weight", 1.0, "float");
      emitLeaf("base_color", "color", nlohmann::json::array({0.18,0.18,0.18}), "color3");
      emitLeaf("base_diffuse_roughness", "roughness", 0.0, "float");
    } else if (cat == "translucent_bsdf") {
      emitLeaf("transmission_weight", "weight", 1.0, "float");
      emitLeaf("transmission_color", "color", nlohmann::json::array({1,1,1}), "color3");
    } else if (cat == "dielectric_bsdf") {
      const std::string mode = JsonString(nodeInput(node, "scatter_mode", "R"), "value", "R");
      if (ScatterModeHas(mode, 'R')) {
        emitLeaf("specular_weight", "weight", 1.0, "float");
        emitLeaf("specular_color", "tint", nlohmann::json::array({1,1,1}), "color3");
      }
      if (ScatterModeHas(mode, 'T')) {
        emitLeaf("transmission_weight", "weight", 1.0, "float");
        emitLeaf("transmission_color", "tint", nlohmann::json::array({1,1,1}), "color3");
      }
      emitLeaf("specular_ior", "ior", 1.5, "float");
      emitLeaf("specular_roughness", "roughness", 0.05, "float");
      emitLeaf("thin_film_thickness", "thinfilm_thickness", 0.0, "float");
      emitLeaf("thin_film_ior", "thinfilm_ior", 1.5, "float");
    } else if (cat == "conductor_bsdf") {
      emitLeaf("base_weight", "weight", 1.0, "float");
      const nlohmann::json eta = nodeInput(
          node, "ior", nlohmann::json::array({0.183, 0.421, 1.373}));
      const nlohmann::json k = nodeInput(
          node, "extinction", nlohmann::json::array({3.424, 2.346, 1.770}));
      auto closureBinary = [&](const std::string& suffix,
                               const char* category,
                               nlohmann::json in1, nlohmann::json in2) {
        const std::string result = name + "__closure_conductor_" + suffix;
        runtimeNodes.push_back({
            {"name", result}, {"category", category}, {"type", "color3"},
            {"inputs", nlohmann::json::array({
                renamedInput(std::move(in1), "in1"),
                renamedInput(std::move(in2), "in2")})}});
        return result;
      };
      const std::string etaMinus = closureBinary(
          "eta_minus", "subtract", eta, nlohmann::json{{"value", 1.0}});
      const std::string etaPlus = closureBinary(
          "eta_plus", "add", eta, nlohmann::json{{"value", 1.0}});
      const std::string etaMinus2 = closureBinary(
          "eta_minus2", "multiply", nlohmann::json{{"nodename", etaMinus}},
          nlohmann::json{{"nodename", etaMinus}});
      const std::string etaPlus2 = closureBinary(
          "eta_plus2", "multiply", nlohmann::json{{"nodename", etaPlus}},
          nlohmann::json{{"nodename", etaPlus}});
      const std::string k2 = closureBinary(
          "k2", "multiply", k, k);
      const std::string numerator = closureBinary(
          "numerator", "add", nlohmann::json{{"nodename", etaMinus2}},
          nlohmann::json{{"nodename", k2}});
      const std::string denominator = closureBinary(
          "denominator", "add", nlohmann::json{{"nodename", etaPlus2}},
          nlohmann::json{{"nodename", k2}});
      const std::string f0 = closureBinary(
          "f0", "divide", nlohmann::json{{"nodename", numerator}},
          nlohmann::json{{"nodename", denominator}});
      emitClosureLane(name, "base_color",
                      nlohmann::json{{"nodename", f0}}, "color3");
      emitClosureLane(name, "base_metalness", nlohmann::json{{"value",1.0}}, "float");
      emitLeaf("specular_roughness", "roughness", 0.05, "float");
    } else if (cat == "generalized_schlick_bsdf") {
      const std::string mode = JsonString(
          nodeInput(node, "scatter_mode", "R"), "value", "R");
      if (ScatterModeHas(mode, 'R')) {
        emitLeaf("specular_weight", "weight", 1.0, "float");
        emitLeaf("specular_color", "color0",
                 nlohmann::json::array({1,1,1}), "color3");
      }
      if (ScatterModeHas(mode, 'T')) {
        emitLeaf("transmission_weight", "weight", 1.0, "float");
        emitClosureLane(name, "transmission_color",
                        nlohmann::json{{"value", nlohmann::json::array({1,1,1})}},
                        "color3");
      }
      emitLeaf("specular_roughness", "roughness", 0.05, "float");
    } else if (cat == "subsurface_bsdf") {
      emitLeaf("subsurface_weight", "weight", 1.0, "float");
      emitLeaf("subsurface_color", "color", nlohmann::json::array({0.18,0.18,0.18}), "color3");
      emitLeaf("subsurface_radius", "radius", nlohmann::json::array({1,1,1}), "color3");
      emitLeaf("subsurface_anisotropy", "anisotropy", 0.0, "float");
    } else if (cat == "sheen_bsdf") {
      emitLeaf("sheen_weight", "weight", 1.0, "float");
      emitLeaf("sheen_color", "color", nlohmann::json::array({1,1,1}), "color3");
      emitLeaf("sheen_roughness", "roughness", 0.3, "float");
    } else if (cat == "chiang_hair_bsdf") {
      emitLeaf("sheen_weight", "weight", 0.5, "float");
      emitLeaf("sheen_color", "tint_R", nlohmann::json::array({1,1,1}), "color3");
      emitLeaf("transmission_weight", "weight", 0.5, "float");
      emitLeaf("transmission_color", "tint_TT", nlohmann::json::array({1,1,1}), "color3");
      emitLeaf("specular_ior", "ior", 1.55, "float");
      emitLeaf("sheen_roughness", "roughness_R", 0.1, "float");
    } else if (cat == "generalized_schlick_edf") {
      const std::string baseName = connectedClosure("base");
      const ClosureLaneMap& base = lowerClosure(baseName);
      for (const auto& lane : base) {
        if (lane.first != "emission_color") lanes[lane.first] = lane.second;
      }
      const auto baseColor = base.find("emission_color");
      if (baseColor != base.end()) {
        const std::string normal = name + "__edf_normal";
        const std::string view = name + "__edf_view";
        const std::string cosine = name + "__edf_cosine";
        const std::string oneMinus = name + "__edf_one_minus";
        const std::string factor = name + "__edf_factor";
        const std::string tint = name + "__edf_tint";
        const std::string result = name + "__edf_result";
        runtimeNodes.push_back({{"name", normal}, {"category", "normal"},
                                {"type", "vector3"}, {"inputs", nlohmann::json::array()}});
        runtimeNodes.push_back({{"name", view}, {"category", "viewdirection"},
                                {"type", "vector3"}, {"inputs", nlohmann::json::array()}});
        runtimeNodes.push_back({{"name", cosine}, {"category", "dotproduct"},
                                {"type", "float"}, {"inputs", nlohmann::json::array({
                                    nlohmann::json{{"name", "in1"}, {"nodename", normal}},
                                    nlohmann::json{{"name", "in2"}, {"nodename", view}}})}});
        runtimeNodes.push_back({{"name", oneMinus}, {"category", "subtract"},
                                {"type", "float"}, {"inputs", nlohmann::json::array({
                                    nlohmann::json{{"name", "in1"}, {"value", 1.0}},
                                    nlohmann::json{{"name", "in2"}, {"nodename", cosine}}})}});
        runtimeNodes.push_back({{"name", factor}, {"category", "power"},
                                {"type", "float"}, {"inputs", nlohmann::json::array({
                                    nlohmann::json{{"name", "in1"}, {"nodename", oneMinus}},
                                    renamedInput(nodeInput(node, "exponent", 5.0), "in2")})}});
        runtimeNodes.push_back({{"name", tint}, {"category", "mix"},
                                {"type", "color3"}, {"inputs", nlohmann::json::array({
                                    renamedInput(nodeInput(node, "color0", nlohmann::json::array({1,1,1})), "in1"),
                                    renamedInput(nodeInput(node, "color90", nlohmann::json::array({1,1,1})), "in2"),
                                    nlohmann::json{{"name", "mix"}, {"nodename", factor}}})}});
        runtimeNodes.push_back({{"name", result}, {"category", "multiply"},
                                {"type", "color3"}, {"inputs", nlohmann::json::array({
                                    nlohmann::json{{"name", "in1"}, {"nodename", baseColor->second}},
                                    nlohmann::json{{"name", "in2"}, {"nodename", tint}}})}});
        emitClosureLane(name, "emission_color", nlohmann::json{{"nodename", result}}, "color3");
      }
    } else if (cat == "conical_edf") {
      // MaterialX's conical EDF uses full cone angles, converted to half-angle
      // cosine boundaries. Keep its hard-cutoff and smoothstep behavior in the
      // bounded graph instead of collapsing the closure to uniform emission.
      const std::string normal = name + "__conical_normal";
      const std::string view = name + "__conical_view";
      const std::string cosineRaw = name + "__conical_cosine_raw";
      const std::string cosine = name + "__conical_cosine";
      const std::string innerRadians = name + "__conical_inner_radians";
      const std::string outerRadians = name + "__conical_outer_radians";
      const std::string innerCosine = name + "__conical_inner_cosine";
      const std::string outerCosine = name + "__conical_outer_cosine";
      const std::string hardFalloff = name + "__conical_hard_falloff";
      const std::string smoothFalloff = name + "__conical_smooth_falloff";
      const std::string falloff = name + "__conical_falloff";
      const std::string color = name + "__conical_color";
      const std::string result = name + "__conical_result";
      const auto authoredNormal = nodeInput(node, "normal", nlohmann::json::object());
      const bool hasAuthoredNormal =
          authoredNormal.is_object() &&
          (authoredNormal.find("value") != authoredNormal.end() ||
           authoredNormal.find("nodename") != authoredNormal.end() ||
           authoredNormal.find("nodegraph") != authoredNormal.end());
      if (hasAuthoredNormal) {
        runtimeNodes.push_back({
            {"name", normal}, {"category", "convert"}, {"type", "vector3"},
            {"inputs", nlohmann::json::array({
                renamedInput(authoredNormal, "in")})}});
      } else {
        // MaterialX defaults the conical direction to the world-space normal.
        runtimeNodes.push_back({{"name", normal}, {"category", "normal"},
                                {"type", "vector3"},
                                {"inputs", nlohmann::json::array()}});
      }
      runtimeNodes.push_back({{"name", view}, {"category", "viewdirection"},
                              {"type", "vector3"},
                              {"inputs", nlohmann::json::array()}});
      runtimeNodes.push_back({{"name", cosineRaw}, {"category", "dotproduct"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in1"}, {"nodename", normal}},
                                  nlohmann::json{{"name", "in2"}, {"nodename", view}}})}});
      runtimeNodes.push_back({{"name", cosine}, {"category", "clamp"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in"}, {"nodename", cosineRaw}},
                                  nlohmann::json{{"name", "low"}, {"value", 0.0}},
                                  nlohmann::json{{"name", "high"}, {"value", 1.0}}})}});
      const nlohmann::json inner = nodeInput(node, "inner_angle", 60.0);
      const nlohmann::json outer = nodeInput(node, "outer_angle", 0.0);
      const nlohmann::json degreesToRadians = nlohmann::json{{"name", "in2"},
                                                               {"value", 0.008726646259971648}};
      runtimeNodes.push_back({{"name", innerRadians}, {"category", "multiply"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  renamedInput(inner, "in1"), degreesToRadians})}});
      runtimeNodes.push_back({{"name", outerRadians}, {"category", "multiply"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  renamedInput(outer, "in1"), degreesToRadians})}});
      runtimeNodes.push_back({{"name", innerCosine}, {"category", "cos"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in"}, {"nodename", innerRadians}}})}});
      runtimeNodes.push_back({{"name", outerCosine}, {"category", "cos"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in"}, {"nodename", outerRadians}}})}});
      runtimeNodes.push_back({{"name", hardFalloff}, {"category", "ifgreatereq"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "value1"}, {"nodename", cosine}},
                                  nlohmann::json{{"name", "value2"}, {"nodename", innerCosine}},
                                  nlohmann::json{{"name", "in1"}, {"value", 1.0}},
                                  nlohmann::json{{"name", "in2"}, {"value", 0.0}}})}});
      runtimeNodes.push_back({{"name", smoothFalloff}, {"category", "smoothstep"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in"}, {"nodename", cosine}},
                                  nlohmann::json{{"name", "low"}, {"nodename", outerCosine}},
                                  nlohmann::json{{"name", "high"}, {"nodename", innerCosine}}})}});
      runtimeNodes.push_back({{"name", falloff}, {"category", "ifgreater"},
                              {"type", "float"}, {"inputs", nlohmann::json::array({
                                  renamedInput(outer, "value1"),
                                  renamedInput(inner, "value2"),
                                  nlohmann::json{{"name", "in1"}, {"nodename", smoothFalloff}},
                                  nlohmann::json{{"name", "in2"}, {"nodename", hardFalloff}}})}});
      runtimeNodes.push_back({{"name", color}, {"category", "constant"},
                              {"type", "color3"}, {"inputs", nlohmann::json::array({
                                  renamedInput(nodeInput(node, "color", nlohmann::json::array({1, 1, 1})), "value")})}});
      runtimeNodes.push_back({{"name", result}, {"category", "multiply"},
                              {"type", "color3"}, {"inputs", nlohmann::json::array({
                                  nlohmann::json{{"name", "in1"}, {"nodename", color}},
                                  nlohmann::json{{"name", "in2"}, {"nodename", falloff}}})}});
      emitClosureLane(name, "emission_color", nlohmann::json{{"nodename", result}}, "color3");
      emitClosureLane(name, "emission_luminance", nlohmann::json{{"value", 1.0}}, "float");
      emitClosureLane(name, "volume_emission_color", nlohmann::json{{"nodename", result}}, "color3");
      emitClosureLane(name, "volume_emission_scale", nlohmann::json{{"value", 1.0}}, "float");
    } else if (cat == "uniform_edf" || cat == "measured_edf") {
      emitLeaf("emission_color", "color", nlohmann::json::array({1,1,1}), "color3");
      emitClosureLane(name, "emission_luminance", nlohmann::json{{"value",1.0}}, "float");
      emitClosureLane(name, "volume_emission_color", nodeInput(node, "color", nlohmann::json::array({1,1,1})), "color3");
      emitClosureLane(name, "volume_emission_scale", nlohmann::json{{"value",1.0}}, "float");
    } else if (cat == "absorption_vdf" || cat == "anisotropic_vdf" ||
               cat == "subsurface_vdf") {
      const nlohmann::json absorption = nodeInput(node, "absorption", nlohmann::json::array({0,0,0}));
      const nlohmann::json scattering = nodeInput(node, "scattering", nlohmann::json::array({0,0,0}));
      const std::string extinction = name + "__closure_extinction";
      runtimeNodes.push_back({{"name",extinction},{"category","add"},{"type","color3"},
                              {"inputs",nlohmann::json::array({renamedInput(absorption,"in1"),renamedInput(scattering,"in2")})}});
      const std::string density = name + "__closure_volume_density";
      runtimeNodes.push_back({{"name",density},{"category","maxcomponent"},{"type","float"},
                              {"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",extinction}}})}});
      lanes["volume_density"] = density;
      const std::string albedo = name + "__closure_volume_albedo";
      runtimeNodes.push_back({{"name",albedo},{"category","divide"},{"type","color3"},
                              {"inputs",nlohmann::json::array({renamedInput(scattering,"in1"),nlohmann::json{{"name","in2"},{"nodename",extinction}}})}});
      lanes["volume_albedo"] = albedo;
    }
    closureVisiting.erase(name);
    return lanes;
  };
  std::map<std::string, std::string> sourceOutputs;
  const auto sourceOutputsIt = ng.find("outputs");
  if (sourceOutputsIt != ng.end() && sourceOutputsIt->is_array()) {
    for (const nlohmann::json& output : *sourceOutputsIt) {
      const std::string outputName = JsonString(output, "name");
      const std::string nodeName = JsonString(output, "nodename");
      if (!outputName.empty() && !nodeName.empty())
        sourceOutputs[outputName] = nodeName;
    }
  }
  const auto sourceConnectionsIt = j.find("connections");
  if (sourceConnectionsIt != j.end() && sourceConnectionsIt->is_array()) {
    for (const nlohmann::json& connection : *sourceConnectionsIt) {
      const std::string terminalInput = JsonString(connection, "input");
      if (terminalInput != "bsdf" && terminalInput != "edf" &&
          terminalInput != "vdf") continue;
      const auto output = sourceOutputs.find(JsonString(connection, "output"));
      if (output != sourceOutputs.end()) lowerClosure(output->second);
    }
  }
  auto emitMatrixInput = [&](const nlohmann::json& node,const char* inputName,
                             const std::string& owner,int dim)->std::string {
    nlohmann::json input=inputNamed(node,inputName,nlohmann::json::object());
    const std::string source=JsonString(input,"nodename");
    if(!source.empty())return source+"__col0";
    nlohmann::json values=nlohmann::json::array();
    const auto valueIt=input.find("value");
    if(valueIt!=input.end()&&valueIt->is_array())values=*valueIt;
    for(int col=0;col<dim;col++){
      nlohmann::json column=nlohmann::json::array();
      for(int row=0;row<dim;row++){
        const size_t index=static_cast<size_t>(col*dim+row);
        column.push_back(index<values.size()&&values[index].is_number()
                             ?values[index]:nlohmann::json(col==row?1:0));
      }
      runtimeNodes.push_back({{"name",owner+"__literal_col"+std::to_string(col)},
          {"category","constant"},{"type",dim==3?"vector3":"vector4"},
          {"inputs",nlohmann::json::array({nlohmann::json{{"name","value"},{"value",column}}})}});
    }
    return owner+"__literal_col0";
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
  auto emitSwitch4 = [&](const std::string& result, const std::string& type,
                         nlohmann::json which,
                         const std::array<std::string,4>& choices) {
    std::string previous;
    for(int choice=3;choice>=0;--choice){
      const std::string lowered=choice==0?result:result+"__choice"+std::to_string(choice);
      nlohmann::json other=previous.empty()?nlohmann::json{{"name","in2"},{"value",0}}:
          nlohmann::json{{"name","in2"},{"nodename",previous}};
      runtimeNodes.push_back({{"name",lowered},{"category","ifequal"},{"type",type},
          {"inputs",nlohmann::json::array({renamedInput(which,"value1"),
          nlohmann::json{{"name","value2"},{"value",choice}},
          nlohmann::json{{"name","in1"},{"nodename",choices[choice]}},other})}});
      previous=lowered;
    }
  };
  auto emitUnifiedRange = [&](const std::string& name,const std::string& selected,
                              nlohmann::json outmin,nlohmann::json outmax,
                              nlohmann::json doclamp){
    const std::string span=name+"__span",scaled=name+"__scaled",fitted=name+"__fitted",clamped=name+"__clamped";
    runtimeNodes.push_back({{"name",span},{"category","subtract"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(outmax,"in1"),renamedInput(outmin,"in2")})}});
    runtimeNodes.push_back({{"name",scaled},{"category","multiply"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",selected}},nlohmann::json{{"name","in2"},{"nodename",span}}})}});
    runtimeNodes.push_back({{"name",fitted},{"category","add"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",scaled}},renamedInput(outmin,"in2")})}});
    runtimeNodes.push_back({{"name",clamped},{"category","clamp"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",fitted}},renamedInput(outmin,"low"),renamedInput(outmax,"high")})}});
    runtimeNodes.push_back({{"name",name},{"category","ifequal"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(doclamp,"value1"),nlohmann::json{{"name","value2"},{"value",true}},nlohmann::json{{"name","in1"},{"nodename",clamped}},nlohmann::json{{"name","in2"},{"nodename",fitted}}})}});
  };
  for (const nlohmann::json& node : *nodesIt) {
    const std::string name = JsonString(node, "name");
    const std::string type = JsonString(node, "type");
    const std::string cat = NormalizeMtlxCategory(JsonString(node, "category"), type);
    if(cat.rfind("creatematrix",0)==0&&!name.empty()){
      const int dim=type=="matrix33"?3:4;
      for(int col=0;col<dim;col++){
        const std::string inputName="in"+std::to_string(col+1);
        nlohmann::json source=inputNamed(node,inputName.c_str(),
            {{"name",inputName},{"value",nlohmann::json::array()}});
        const auto valueIt=source.find("value");
        if(valueIt!=source.end()&&valueIt->is_array()){
          nlohmann::json value=*valueIt;
          while(value.size()<static_cast<size_t>(dim))
            value.push_back(dim==4&&col==3&&value.size()==3?1:0);
          source["value"]=std::move(value);
        }
        runtimeNodes.push_back({{"name",name+"__col"+std::to_string(col)},
            {"category","convert"},{"type",dim==3?"vector3":"vector4"},
            {"inputs",nlohmann::json::array({renamedInput(source,"in")})}});
      }
      continue;
    }
    if((cat.rfind("transpose",0)==0||cat.rfind("invertmatrix",0)==0)&&!name.empty()){
      const int dim=type=="matrix33"?3:4;
      const std::string source=emitMatrixInput(node,"in",name,dim);
      for(int col=0;col<dim;col++)runtimeNodes.push_back({
          {"name",name+"__col"+std::to_string(col)},
          {"category",cat.rfind("transpose",0)==0?"matrixtransposecore":"matrixinversecore"},
          {"type",dim==3?"vector3":"vector4"},{"inputs",nlohmann::json::array()},
          {"matrix_source",source},{"matrix_dim",dim},{"matrix_column",col}});
      continue;
    }
    if(cat.rfind("determinant",0)==0&&!name.empty()){
      int dim=4;const nlohmann::json input=inputNamed(node,"in",nlohmann::json::object());
      if(JsonString(input,"type")=="matrix33")dim=3;
      const std::string source=emitMatrixInput(node,"in",name,dim);
      runtimeNodes.push_back({{"name",name},{"category","matrixdeterminantcore"},
          {"type","float"},{"inputs",nlohmann::json::array()},
          {"matrix_source",source},{"matrix_dim",dim}});continue;
    }
    if(cat.rfind("transformmatrix",0)==0&&!name.empty()){
      const nlohmann::json matrix=inputNamed(node,"mat",nlohmann::json::object());
      int dim=JsonString(matrix,"type")=="matrix33"?3:4;
      const std::string source=emitMatrixInput(node,"mat",name,dim);
      runtimeNodes.push_back({{"name",name},{"category","matrixtransformcore"},
          {"type",type},{"inputs",nlohmann::json::array({renamedInput(
              inputNamed(node,"in",{{"value",0}}),"in")})},
          {"matrix_source",source},{"matrix_dim",dim}});continue;
    }
    if (cat == "latlongimage" && !name.empty()) {
      // MaterialX stdlib NG_latlongimage.  Keep this as ordinary bounded graph
      // arithmetic so connected view directions and rotations execute on all
      // CPU/GPU interpreters without adding another packed-ABI opcode.
      const std::string x = name + "__view_x";
      const std::string y = name + "__view_y";
      const std::string z = name + "__view_z";
      const std::string angle = name + "__angle_xz";
      const std::string scaledLongitude = name + "__scaled_longitude";
      const std::string longitude = name + "__longitude";
      const std::string rotation = name + "__rotation";
      const std::string u = name + "__u";
      const std::string latitude = name + "__latitude";
      const std::string uv = name + "__uv";
      nlohmann::json view = inputNamed(node, "viewdir", nlohmann::json::object());
      if (view.empty() || (JsonString(view, "nodename").empty() &&
                           view.find("value") == view.end())) {
        const std::string viewNode = name + "__viewdirection";
        runtimeNodes.push_back({{"name", viewNode}, {"category", "viewdirection"},
            {"type", "vector3"}, {"inputs", nlohmann::json::array()}});
        view = nlohmann::json{{"nodename", viewNode}};
      }
      auto extract = [&](const std::string& dst, int lane) {
        runtimeNodes.push_back({{"name", dst}, {"category", "extract"},
            {"type", "float"}, {"inputs", nlohmann::json::array({
                renamedInput(view, "in"),
                nlohmann::json{{"name", "index"}, {"value", lane}}})}});
      };
      extract(x, 0); extract(y, 1); extract(z, 2);
      runtimeNodes.push_back({{"name", angle}, {"category", "atan2"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "iny"}, {"nodename", x}},
              nlohmann::json{{"name", "inx"}, {"nodename", z}}})}});
      runtimeNodes.push_back({{"name", scaledLongitude}, {"category", "multiply"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", angle}},
              nlohmann::json{{"name", "in2"}, {"value", -0.15915494}}})}});
      runtimeNodes.push_back({{"name", longitude}, {"category", "add"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", scaledLongitude}},
              nlohmann::json{{"name", "in2"}, {"value", 0.5}}})}});
      runtimeNodes.push_back({{"name", rotation}, {"category", "multiply"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              renamedInput(inputNamed(node, "rotation", {{"value", 0.0}}), "in1"),
              nlohmann::json{{"name", "in2"}, {"value", 0.00277778}}})}});
      runtimeNodes.push_back({{"name", u}, {"category", "add"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", longitude}},
              nlohmann::json{{"name", "in2"}, {"nodename", rotation}}})}});
      const std::string asinY = name + "__asin_y";
      runtimeNodes.push_back({{"name", asinY}, {"category", "asin"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in"}, {"nodename", y}}})}});
      runtimeNodes.push_back({{"name", latitude}, {"category", "multiply"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", asinY}},
              nlohmann::json{{"name", "in2"}, {"value", 0.31830989}}})}});
      const std::string v = name + "__v";
      runtimeNodes.push_back({{"name", v}, {"category", "add"},
          {"type", "float"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", latitude}},
              nlohmann::json{{"name", "in2"}, {"value", 0.5}}})}});
      runtimeNodes.push_back({{"name", uv}, {"category", "combine2"},
          {"type", "vector2"}, {"inputs", nlohmann::json::array({
              nlohmann::json{{"name", "in1"}, {"nodename", u}},
              nlohmann::json{{"name", "in2"}, {"nodename", v}}})}});
      runtimeNodes.push_back({{"name", name}, {"category", "image"},
          {"type", "color3"}, {"inputs", nlohmann::json::array({
              renamedInput(inputNamed(node, "file", {{"value", ""}}), "file"),
              renamedInput(inputNamed(node, "default", {{"value", nlohmann::json::array({0.0, 0.0, 0.0})}}), "default"),
              nlohmann::json{{"name", "texcoord"}, {"nodename", uv}},
              nlohmann::json{{"name", "uaddressmode"}, {"value", "periodic"}},
              nlohmann::json{{"name", "vaddressmode"}, {"value", "mirror"}}})}});
      continue;
    }
    if (cat == "triplanarprojection" && !name.empty()) {
      // Compact form of the MaterialX stdlib triplanar nodegraph.  Three image
      // nodes retain independent files while ordinary graph arithmetic builds
      // the axis projections and normalized blend weights.
      const std::string posName=name+"__position",normalName=name+"__normal";
      nlohmann::json position=inputNamed(node,"position",{{"nodename",posName}});
      nlohmann::json normal=inputNamed(node,"normal",{{"nodename",normalName}});
      if(JsonString(position,"nodename")==posName)runtimeNodes.push_back({{"name",posName},{"category","position"},{"type","vector3"},{"inputs",nlohmann::json::array()}});
      if(JsonString(normal,"nodename")==normalName)runtimeNodes.push_back({{"name",normalName},{"category","normal"},{"type","vector3"},{"inputs",nlohmann::json::array()}});
      std::string axis[3]={name+"__x",name+"__y",name+"__z"};
      for(int lane=0;lane<3;lane++)runtimeNodes.push_back({{"name",axis[lane]},{"category","extract"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(position,"in"),nlohmann::json{{"name","index"},{"value",lane}}})}});
      const std::string negY=name+"__neg_y";
      runtimeNodes.push_back({{"name",negY},{"category","multiply"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",axis[1]}},nlohmann::json{{"name","in2"},{"value",-1}}})}});
      auto combine2=[&](const std::string& dst,const std::string& a,const std::string& b){runtimeNodes.push_back({{"name",dst},{"category","combine2"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",a}},nlohmann::json{{"name","in2"},{"nodename",b}}})}});};
      const std::string yz=name+"__yz",xz=name+"__xz",xy=name+"__xy",xyX=name+"__xy_xup",xzX=name+"__xz_xup",yzY=name+"__yz_yup";
      combine2(yz,axis[1],axis[2]);combine2(xz,axis[0],axis[2]);combine2(xy,axis[0],axis[1]);
      combine2(xyX,negY,axis[0]);combine2(xzX,axis[2],axis[0]);combine2(yzY,axis[2],axis[1]);
      const nlohmann::json upaxis=inputNamed(node,"upaxis",{{"value",1}});
      auto choose=[&](const std::string& dst,int target,const std::string& yes,const std::string& no){runtimeNodes.push_back({{"name",dst},{"category","ifequal"},{"type","vector2"},{"inputs",nlohmann::json::array({renamedInput(upaxis,"value1"),nlohmann::json{{"name","value2"},{"value",target}},nlohmann::json{{"name","in1"},{"nodename",yes}},nlohmann::json{{"name","in2"},{"nodename",no}}})}});};
      const std::string uvX=name+"__uv_x",uvY=name+"__uv_y",uvZ=name+"__uv_z";
      choose(uvX,2,yz,yzY);choose(uvY,0,xzX,xz);choose(uvZ,0,xyX,xy);
      std::string imageName[3]={name+"__image_x",name+"__image_y",name+"__image_z"};
      const char* fileName[3]={"filex","filey","filez"};const std::string* uvName[3]={&uvX,&uvY,&uvZ};
      const nlohmann::json defaultValue=inputNamed(node,"default",{{"value",0.0}});
      for(int lane=0;lane<3;lane++)runtimeNodes.push_back({{"name",imageName[lane]},{"category","image"},{"type",type},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,fileName[lane],{{"value",""}}),"file"),renamedInput(defaultValue,"default"),nlohmann::json{{"name","texcoord"},{"nodename",*uvName[lane]}},nlohmann::json{{"name","uaddressmode"},{"value","periodic"}},nlohmann::json{{"name","vaddressmode"},{"value","periodic"}}})}});
      const std::string norm=name+"__normalized_normal",absn=name+"__abs_normal",sum0=name+"__weight_sum",w0=name+"__weights0",blendClamp=name+"__blend_clamp",exponent=name+"__exponent",powered=name+"__powered",sum1=name+"__powered_sum",weights=name+"__weights";
      runtimeNodes.push_back({{"name",norm},{"category","normalize"},{"type","vector3"},{"inputs",nlohmann::json::array({renamedInput(normal,"in")})}});
      runtimeNodes.push_back({{"name",absn},{"category","abs"},{"type","vector3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",norm}}})}});
      auto dotOnes=[&](const std::string& dst,const std::string& src){runtimeNodes.push_back({{"name",dst},{"category","dotproduct"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",src}},nlohmann::json{{"name","in2"},{"value",nlohmann::json::array({1,1,1})}}})}});};
      dotOnes(sum0,absn);
      runtimeNodes.push_back({{"name",w0},{"category","divide"},{"type","vector3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",absn}},nlohmann::json{{"name","in2"},{"nodename",sum0}}})}});
      runtimeNodes.push_back({{"name",blendClamp},{"category","clamp"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"blend",{{"value",0.5}}),"in"),nlohmann::json{{"name","low"},{"value",0.03}},nlohmann::json{{"name","high"},{"value",1.0e30}}})}});
      runtimeNodes.push_back({{"name",exponent},{"category","divide"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"value",1}},nlohmann::json{{"name","in2"},{"nodename",blendClamp}}})}});
      runtimeNodes.push_back({{"name",powered},{"category","power"},{"type","vector3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",w0}},nlohmann::json{{"name","in2"},{"nodename",exponent}}})}});dotOnes(sum1,powered);
      runtimeNodes.push_back({{"name",weights},{"category","divide"},{"type","vector3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",powered}},nlohmann::json{{"name","in2"},{"nodename",sum1}}})}});
      std::string weighted[3];for(int lane=0;lane<3;lane++){const std::string weight=name+"__weight_"+std::to_string(lane);weighted[lane]=name+"__weighted_"+std::to_string(lane);runtimeNodes.push_back({{"name",weight},{"category","extract"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",weights}},nlohmann::json{{"name","index"},{"value",lane}}})}});runtimeNodes.push_back({{"name",weighted[lane]},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",imageName[lane]}},nlohmann::json{{"name","in2"},{"nodename",weight}}})}});}
      const std::string first=name+"__xy_blend";runtimeNodes.push_back({{"name",first},{"category","add"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",weighted[0]}},nlohmann::json{{"name","in2"},{"nodename",weighted[1]}}})}});runtimeNodes.push_back({{"name",name},{"category","add"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",first}},nlohmann::json{{"name","in2"},{"nodename",weighted[2]}}})}});
      continue;
    }
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
    if ((cat == "cellnoise2d" || cat == "cellnoise3d" ||
         cat == "worleynoise2d" || cat == "worleynoise3d") && !name.empty()) {
      const bool is3d = cat == "cellnoise3d" || cat == "worleynoise3d";
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
      nlohmann::json inputs = nlohmann::json::array({renamedInput(source, inputName)});
      if (cat == "worleynoise2d" || cat == "worleynoise3d") {
        inputs.push_back(renamedInput(inputNamed(node,"jitter",{{"value",1}}),"jitter"));
        inputs.push_back(renamedInput(inputNamed(node,"style",{{"value",0}}),"style"));
      }
      lowered["inputs"] = std::move(inputs);
      runtimeNodes.push_back(std::move(lowered));
      continue;
    }
    if ((cat == "cloverleaf" || cat == "hexagon") && !name.empty()) {
      const std::string st=name+"__st";
      nlohmann::json tc=inputNamed(node,"texcoord",{{"name","texcoord"},{"nodename",st}});
      if(JsonString(tc,"nodename")==st)runtimeNodes.push_back({{"name",st},{"category","texcoord"},{"type","vector2"},{"inputs",nlohmann::json::array()}});
      nlohmann::json lowered=node;lowered["inputs"]=nlohmann::json::array({renamedInput(tc,"texcoord"),renamedInput(inputNamed(node,"center",{{"value",nlohmann::json::array({0.5,0.5})}}),"center"),renamedInput(inputNamed(node,"radius",{{"value",0.5}}),"radius")});runtimeNodes.push_back(std::move(lowered));continue;
    }
    if ((cat=="grid"||cat=="crosshatch"||cat=="tiledcircles"||
         cat=="tiledcloverleafs"||cat=="tiledhexagons")&&!name.empty()){
      const std::string st=name+"__st",regular=name+"__regular",staggered=name+"__staggered";
      nlohmann::json tc=inputNamed(node,"texcoord",{{"name","texcoord"},{"nodename",st}});
      if(JsonString(tc,"nodename")==st)runtimeNodes.push_back({{"name",st},{"category","texcoord"},{"type","vector2"},{"inputs",nlohmann::json::array()}});
      const char* parameter=(cat=="grid"||cat=="crosshatch")?"thickness":"size";
      const double parameterDefault=(cat=="grid"||cat=="crosshatch")?0.05:0.5;
      auto emitCore=[&](const std::string& core,bool stagger){runtimeNodes.push_back({{"name",core},{"category",cat+(stagger?"staggeredcore":"core")},{"type","color3"},{"inputs",nlohmann::json::array({renamedInput(tc,"texcoord"),renamedInput(inputNamed(node,"uvtiling",{{"value",nlohmann::json::array({1,1})}}),"uvtiling"),renamedInput(inputNamed(node,"uvoffset",{{"value",nlohmann::json::array({0,0})}}),"uvoffset"),renamedInput(inputNamed(node,parameter,{{"value",parameterDefault}}),parameter)})}});};
      emitCore(regular,false);emitCore(staggered,true);
      runtimeNodes.push_back({{"name",name},{"category","ifequal"},{"type","color3"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"staggered",{{"value",false}}),"value1"),nlohmann::json{{"name","value2"},{"value",true}},nlohmann::json{{"name","in1"},{"nodename",staggered}},nlohmann::json{{"name","in2"},{"nodename",regular}}})}});continue;
    }
    if (cat=="ramp"&&!name.empty()){
      const std::string st=name+"__st",coord=name+"__coord";
      nlohmann::json tc=inputNamed(node,"texcoord",{{"name","texcoord"},{"nodename",st}});
      if(JsonString(tc,"nodename")==st)runtimeNodes.push_back({{"name",st},{"category","texcoord"},{"type","vector2"},{"inputs",nlohmann::json::array()}});
      runtimeNodes.push_back({{"name",coord},{"category","rampcoord"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(tc,"texcoord"),renamedInput(inputNamed(node,"type",{{"value",0}}),"type")})}});
      for(int stop=1;stop<=10;stop++){
        const std::string suffix=std::to_string(stop),interval=name+"__interval"+suffix,color=name+"__color"+suffix;
        runtimeNodes.push_back({{"name",interval},{"category","convert"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,("interval"+suffix).c_str(),{{"value",stop==1?0:1}}),"in")})}});
        runtimeNodes.push_back({{"name",color},{"category","convert"},{"type","color4"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,("color"+suffix).c_str(),{{"value",stop==1?nlohmann::json::array({0,0,0,1}):nlohmann::json::array({1,1,1,1})}}),"in")})}});
      }
      runtimeNodes.push_back({{"name",name},{"category","rampcore"},{"type","color4"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","x"},{"nodename",coord}},renamedInput(inputNamed(node,"interpolation",{{"value",1}}),"interpolation"),renamedInput(inputNamed(node,"num_intervals",{{"value",2}}),"num_intervals")})}});continue;
    }
    if((cat=="rampgradient"||cat=="ramp_gradient")&&!name.empty()){
      const char* fields[6]={"interval1","interval2","color1","color2","prev_color","interval_num"};
      const char* types[6]={"float","float","color4","color4","color4","float"};
      const nlohmann::json defaults[6]={0,1,nlohmann::json::array({0,0,0,1}),nlohmann::json::array({1,1,1,1}),nlohmann::json::array({0,0,0,1}),1};
      for(int i=0;i<6;i++){const std::string data=name+"__"+fields[i];runtimeNodes.push_back({{"name",data},{"category","convert"},{"type",types[i]},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,fields[i],{{"value",defaults[i]}}),"in")})}});}
      runtimeNodes.push_back({{"name",name},{"category","rampgradientcore"},{"type","color4"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"x",{{"value",0}}),"x"),renamedInput(inputNamed(node,"interpolation",{{"value",1}}),"interpolation"),renamedInput(inputNamed(node,"num_intervals",{{"value",2}}),"num_intervals")})}});continue;
    }
    // MaterialX 1.39's stdlib implementation of blur is intentionally a
    // pass-through. Preserve its live input connection with a bounded op.
    if(cat=="blur"&&!name.empty()){
      runtimeNodes.push_back({{"name",name},{"category","convert"},{"type",type},
          {"inputs",nlohmann::json::array({renamedInput(
              inputNamed(node,"in",{{"value",0}}),"in")})}});
      continue;
    }
    if((cat=="flake2d"||cat=="flake3d")&&!name.empty()){
      const bool is3d=cat=="flake3d";
      const char* fields[7]={"size","roughness","coverage",is3d?"position":"texcoord",
                             "normal","tangent","bitangent"};
      const char* types[7]={"float","float","float",is3d?"vector3":"vector2",
                            "vector3","vector3","vector3"};
      const nlohmann::json defaults[7]={0.01,0.1,0.5,nlohmann::json(),
          nlohmann::json(),nlohmann::json(),nlohmann::json()};
      const char* geomCats[7]={nullptr,nullptr,nullptr,is3d?"position":"texcoord",
                               "normal","tangent","bitangent"};
      for(int i=0;i<7;i++){
        nlohmann::json source;
        if(geomCats[i]){
          const std::string geom=name+"__geom_"+fields[i];
          source=inputNamed(node,fields[i],{{"name",fields[i]},{"nodename",geom}});
          if(JsonString(source,"nodename")==geom)
            runtimeNodes.push_back({{"name",geom},{"category",geomCats[i]},
                {"type",types[i]},{"inputs",nlohmann::json::array()}});
        }else source=inputNamed(node,fields[i],{{"name",fields[i]},{"value",defaults[i]}});
        runtimeNodes.push_back({{"name",name+"__"+fields[i]},{"category","convert"},
            {"type",types[i]},{"inputs",nlohmann::json::array({renamedInput(source,"in")})}});
      }
      const char* outputs[4]={"id","rand","presence","flakenormal"};
      const char* outputTypes[4]={"integer","float","float","vector3"};
      for(int i=0;i<4;i++)runtimeNodes.push_back({
          {"name",name+"__"+outputs[i]},{"category","flakecore"},
          {"type",outputTypes[i]},{"inputs",nlohmann::json::array()},
          {"flake_output",i},{"flake_3d",is3d}});
      runtimeNodes.push_back({{"name",name},{"category","convert"},{"type","vector3"},
          {"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},
              {"nodename",name+"__flakenormal"}}})}});
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
    if (cat == "bump" && !name.empty()) {
      const std::string heightNormal=name+"__height_normal";
      runtimeNodes.push_back({{"name",heightNormal},{"category","heighttonormal"},
          {"type","vector3"},{"inputs",nlohmann::json::array({
              renamedInput(inputNamed(node,"height",{{"value",0.0}}),"in"),
              renamedInput(inputNamed(node,"scale",{{"value",1.0}}),"scale")})}});
      runtimeNodes.push_back({{"name",name},{"category","normalmap"},
          {"type","vector3"},{"inputs",nlohmann::json::array({
              nlohmann::json{{"name","in"},{"nodename",heightNormal}},
              renamedInput(inputNamed(node,"normal",{{"value",nlohmann::json::array({0,0,1})}}),"normal"),
              nlohmann::json{{"name","scale"},{"value",1.0}},
              renamedInput(inputNamed(node,"tangent",{{"value",nlohmann::json::array({1,0,0})}}),"tangent"),
              renamedInput(inputNamed(node,"bitangent",{{"value",nlohmann::json::array({0,1,0})}}),"bitangent")})}});
      continue;
    }
    if ((cat=="geompropvalue"||cat=="geompropvalueuniform")&&!name.empty()) {
      const std::string prop=JsonString(inputNamed(node,"geomprop",{}),"value");
      const std::string valueType = NormalizeMtlxType(JsonString(node, "type"));
      if ((valueType == "matrix33" || valueType == "matrix44") &&
          !prop.empty()) {
        const int dim = valueType == "matrix33" ? 3 : 4;
        for (int column = 0; column < dim; ++column) {
          nlohmann::json columnNode = node;
          columnNode["name"] = name + "__col" + std::to_string(column);
          columnNode["type"] = dim == 3 ? "vector3" : "vector4";
          columnNode["matrix_column"] = column;
          columnNode["matrix_components"] = dim * dim;
          runtimeNodes.push_back(std::move(columnNode));
        }
        continue;
      }
      if(prop!="st"&&prop!="uv"&&prop!="texcoord"&&
         prop!="displayColor"&&prop!="Cd"&&prop!="color"&&
         prop!="P"&&prop!="position"&&prop!="N"&&prop!="normal"&&
         prop!="tangent"&&prop!="bitangent") {
        // Keep arbitrary geompropvalue nodes live. The scene/backend supplies
        // the named stream; the graph evaluator applies the authored default
        // only when that stream is unavailable.
        runtimeNodes.push_back(node);
        continue;
      }
    }
    if(cat=="artistic_ior"&&!name.empty()){
      for(int output=0;output<2;++output)
        runtimeNodes.push_back({{"name",name+(output==0?"__ior":"__extinction")},
            {"category","artisticiorcore"},{"type","color3"},
            {"inputs",nlohmann::json::array({
                inputNamed(node,"reflectivity",{{"value",nlohmann::json::array({.944,.776,.373})}}),
                inputNamed(node,"edge_color",{{"value",nlohmann::json::array({1,1,1})}})})},
            {"artistic_output",output}});
      runtimeNodes.push_back({{"name",name},{"category","convert"},{"type","color3"},
          {"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",name+"__ior"}}})}});
      continue;
    }
    if(cat=="glossiness_anisotropy"&&!name.empty()){
      const std::string rough=name+"__roughness";
      runtimeNodes.push_back({{"name",rough},{"category","invert"},{"type","float"},
          {"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"glossiness",{{"value",1.0}}),"in")})}});
      runtimeNodes.push_back({{"name",name},{"category","roughness_anisotropy"},
          {"type","vector2"},{"inputs",nlohmann::json::array({
              nlohmann::json{{"name","roughness"},{"nodename",rough}},
              inputNamed(node,"anisotropy",{{"value",0.0}})})}});
      continue;
    }
    if(cat=="deon_hair_absorption_from_melanin"&&!name.empty()){
      const std::string euLog=name+"__eu_log",euAbs=name+"__eu_abs",
                        phLog=name+"__ph_log",phAbs=name+"__ph_abs",
                        blend=name+"__blend",result=name+"__absorption";
      runtimeNodes.push_back({{"name",euLog},{"category","ln"},{"type","color3"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"eumelanin_color",{{"value",nlohmann::json::array({.657704,.498077,.254107})}}),"in")})}});
      runtimeNodes.push_back({{"name",euAbs},{"category","multiply"},{"type","color3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",euLog}},nlohmann::json{{"name","in2"},{"value",-1.0}}})}});
      runtimeNodes.push_back({{"name",phLog},{"category","ln"},{"type","color3"},{"inputs",nlohmann::json::array({renamedInput(inputNamed(node,"pheomelanin_color",{{"value",nlohmann::json::array({.829444,.67032,.349938})}}),"in")})}});
      runtimeNodes.push_back({{"name",phAbs},{"category","multiply"},{"type","color3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",phLog}},nlohmann::json{{"name","in2"},{"value",-1.0}}})}});
      runtimeNodes.push_back({{"name",blend},{"category","mix"},{"type","color3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",euAbs}},nlohmann::json{{"name","in2"},{"nodename",phAbs}},renamedInput(inputNamed(node,"melanin_redness",{{"value",.5}}),"mix")})}});
      runtimeNodes.push_back({{"name",result},{"category","multiply"},{"type","color3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",blend}},renamedInput(inputNamed(node,"melanin_concentration",{{"value",.25}}),"in2")})}});
      runtimeNodes.push_back({{"name",name},{"category","convert"},{"type","vector3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",result}}})}});
      continue;
    }
    if(cat=="chiang_hair_roughness"&&!name.empty()){
      const char* outputs[3]={"roughness_R","roughness_TT","roughness_TRT"};
      for(const char* output:outputs)runtimeNodes.push_back({{"name",name+"__"+output},{"category","constant"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","value"},{"value",nlohmann::json::array({0,0})}}})}});
      runtimeNodes.push_back({{"name",name},{"category","convert"},{"type","vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",name+"__roughness_R"}}})}});
      continue;
    }
    if(cat=="chiang_hair_absorption_from_color"&&!name.empty()){
      const std::string result=name+"__absorption";
      runtimeNodes.push_back({{"name",result},{"category","chianghairabsorptioncore"},
          {"type","vector3"},{"inputs",nlohmann::json::array({
              inputNamed(node,"color",{{"value",nlohmann::json::array({1,1,1})}}),
              inputNamed(node,"azimuthal_roughness",{{"value",.2}})})}});
      runtimeNodes.push_back({{"name",name},{"category","convert"},{"type","vector3"},
          {"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",result}}})}});
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
    if (cat == "fractal2d" && !name.empty()) {
      const std::string st=name+"__st",core=name+"__core";
      nlohmann::json tc=inputNamed(node,"texcoord",{{"name","texcoord"},{"nodename",st}});
      if(JsonString(tc,"nodename")==st)
        runtimeNodes.push_back({{"name",st},{"category","texcoord"},{"type","vector2"},{"inputs",nlohmann::json::array()}});
      runtimeNodes.push_back({{"name",core},{"category","fractal2dcore"},{"type",type},{"inputs",nlohmann::json::array({renamedInput(tc,"texcoord"),renamedInput(inputNamed(node,"octaves",{{"value",3}}),"octaves"),renamedInput(inputNamed(node,"lacunarity",{{"value",2}}),"lacunarity"),renamedInput(inputNamed(node,"diminish",{{"value",0.5}}),"diminish")})}});
      runtimeNodes.push_back({{"name",name},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",core}},renamedInput(inputNamed(node,"amplitude",{{"value",1}}),"in2")})}});
      continue;
    }
    if ((cat == "fractal3d" || cat == "fractal") && !name.empty()) {
      const std::string pos=name+"__position",core=name+"__core";
      nlohmann::json position=inputNamed(node,"position",{{"name","position"},{"nodename",pos}});
      if(JsonString(position,"nodename")==pos)
        runtimeNodes.push_back({{"name",pos},{"category","position"},{"type","vector3"},{"inputs",nlohmann::json::array()}});
      runtimeNodes.push_back({{"name",core},{"category","fractal3dcore"},{"type",type},{"inputs",nlohmann::json::array({renamedInput(position,"position"),renamedInput(inputNamed(node,"octaves",{{"value",3}}),"octaves"),renamedInput(inputNamed(node,"lacunarity",{{"value",2}}),"lacunarity"),renamedInput(inputNamed(node,"diminish",{{"value",0.5}}),"diminish")})}});
      runtimeNodes.push_back({{"name",name},{"category","multiply"},{"type",type},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",core}},renamedInput(inputNamed(node,"amplitude",{{"value",1}}),"in2")})}});
      continue;
    }
    if ((cat == "unifiednoise2d" || cat == "unifiednoise3d") && !name.empty()) {
      const bool d3=cat=="unifiednoise3d";const char* coordInput=d3?"position":"texcoord";
      const std::string coord=name+"__coord",applyFreq=name+"__freq",applyOffset=name+"__offset";
      nlohmann::json source=inputNamed(node,coordInput,{{"name",coordInput},{"nodename",coord}});
      if(JsonString(source,"nodename")==coord)runtimeNodes.push_back({{"name",coord},{"category",d3?"position":"texcoord"},{"type",d3?"vector3":"vector2"},{"inputs",nlohmann::json::array()}});
      runtimeNodes.push_back({{"name",applyFreq},{"category","multiply"},{"type",d3?"vector3":"vector2"},{"inputs",nlohmann::json::array({renamedInput(source,"in1"),renamedInput(inputNamed(node,"freq",{{"value",d3?nlohmann::json::array({1,1,1}):nlohmann::json::array({1,1})}}),"in2")})}});
      runtimeNodes.push_back({{"name",applyOffset},{"category","add"},{"type",d3?"vector3":"vector2"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",applyFreq}},renamedInput(inputNamed(node,"offset",{{"value",d3?nlohmann::json::array({0,0,0}):nlohmann::json::array({0,0})}}),"in2")})}});
      const std::string jitterMinus=name+"__jitter_minus_one",angle=name+"__jitter_angle",jittered=name+"__jittered";
      const nlohmann::json jitter=inputNamed(node,"jitter",{{"value",1}});
      runtimeNodes.push_back({{"name",jitterMinus},{"category","subtract"},{"type","float"},{"inputs",nlohmann::json::array({renamedInput(jitter,"in1"),nlohmann::json{{"name","in2"},{"value",1}}})}});
      runtimeNodes.push_back({{"name",angle},{"category","multiply"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",jitterMinus}},nlohmann::json{{"name","in2"},{"value",90000}}})}});
      nlohmann::json rotateInputs=nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",applyOffset}},nlohmann::json{{"name","amount"},{"nodename",angle}}});
      if(d3)rotateInputs.push_back(nlohmann::json{{"name","axis"},{"value",nlohmann::json::array({0.1,1,0})}});
      runtimeNodes.push_back({{"name",jittered},{"category",d3?"rotate3d":"rotate2d"},{"type",d3?"vector3":"vector2"},{"inputs",std::move(rotateInputs)}});
      const std::string perlin=name+"__perlin",cell=name+"__cell",worley=name+"__worley",fractal=name+"__fractal";
      runtimeNodes.push_back({{"name",perlin},{"category",d3?"noise3d":"noise2d"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name",coordInput},{"nodename",jittered}},nlohmann::json{{"name","amplitude"},{"value",0.5}},nlohmann::json{{"name","pivot"},{"value",0.5}}})}});
      runtimeNodes.push_back({{"name",cell},{"category",d3?"cellnoise3d":"cellnoise2d"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name",coordInput},{"nodename",jittered}}})}});
      runtimeNodes.push_back({{"name",worley},{"category",d3?"worleynoise3d":"worleynoise2d"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name",coordInput},{"nodename",applyOffset}},renamedInput(jitter,"jitter"),renamedInput(inputNamed(node,"style",{{"value",0}}),"style")})}});
      std::string fractalPosition=jittered;
      if(!d3){const std::string x=name+"__x",y=name+"__y";fractalPosition=name+"__fractal_position";runtimeNodes.push_back({{"name",x},{"category","extract"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",applyOffset}},nlohmann::json{{"name","index"},{"value",0}}})}});runtimeNodes.push_back({{"name",y},{"category","extract"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in"},{"nodename",applyOffset}},nlohmann::json{{"name","index"},{"value",1}}})}});runtimeNodes.push_back({{"name",fractalPosition},{"category","combine3"},{"type","vector3"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","in1"},{"nodename",x}},nlohmann::json{{"name","in2"},{"nodename",y}},nlohmann::json{{"name","in3"},{"nodename",angle}}})}});}
      runtimeNodes.push_back({{"name",fractal},{"category","fractal3dcore"},{"type","float"},{"inputs",nlohmann::json::array({nlohmann::json{{"name","position"},{"nodename",fractalPosition}},renamedInput(inputNamed(node,"octaves",{{"value",3}}),"octaves"),renamedInput(inputNamed(node,"lacunarity",{{"value",2}}),"lacunarity"),renamedInput(inputNamed(node,"diminish",{{"value",0.5}}),"diminish")})}});
      const std::string selected=name+"__selected";emitSwitch4(selected,"float",inputNamed(node,"type",{{"value",0}}),{perlin,cell,worley,fractal});
      emitUnifiedRange(name,selected,inputNamed(node,"outmin",{{"value",0}}),inputNamed(node,"outmax",{{"value",1}}),inputNamed(node,"clampoutput",{{"value",true}}));
      continue;
    }
    // Closure nodes reached from terminal BSDF/EDF/VDF inputs were expanded
    // above into ordinary scalar/color utility nodes and renderer lanes.
    if (closureLanes.find(name) != closureLanes.end()) continue;
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
    else if (cat == "geompropvalue" || cat == "geompropvalueuniform") {
      const std::string prop=JsonString(inputNamed(node,"geomprop",{}),"value");
      if(prop=="st"||prop=="uv"||prop=="texcoord") out.op=MaterialXGraphOpCPU::Texcoord;
      else if(prop=="displayColor"||prop=="Cd"||prop=="color") out.op=MaterialXGraphOpCPU::GeomColor;
      else if(prop=="P"||prop=="position") out.op=MaterialXGraphOpCPU::Position;
      else if(prop=="N"||prop=="normal") out.op=MaterialXGraphOpCPU::GeometricNormal;
      else if(prop=="tangent") out.op=MaterialXGraphOpCPU::GeometricTangent;
      else if(prop=="bitangent") out.op=MaterialXGraphOpCPU::Bitangent;
      else {
        out.op = MaterialXGraphOpCPU::GeomProp;
        out.geomPropName = prop;
        const bool matrixColumn = node.contains("matrix_column");
        out.auxValue[1] = matrixColumn
                              ? node.value("matrix_components", 16.0f)
                              : 1.0f;
        out.auxValue[2] = matrixColumn
                              ? node.value("matrix_column", 0.0f)
                              : 0.0f;
        const std::string valueType = JsonString(node, "type");
        if (!matrixColumn) {
          out.auxValue[1] = valueType == "vector2" ? 2.0f :
                            valueType == "vector3" || valueType == "color3" ? 3.0f :
                            valueType == "vector4" || valueType == "color4" ? 4.0f : 1.0f;
        }
      }
    }
    else if (cat == "viewdirection" || cat == "viewdir")
      out.op = MaterialXGraphOpCPU::ViewDirection;
    else if (cat == "time") out.op = MaterialXGraphOpCPU::Time;
    else if (cat == "frame") out.op = MaterialXGraphOpCPU::Frame;
    else if (cat == "blackbody") out.op = MaterialXGraphOpCPU::Blackbody;
    else if (cat == "roughness_anisotropy")
      out.op = MaterialXGraphOpCPU::RoughnessAnisotropy;
    else if (cat == "roughness_dual")
      out.op = MaterialXGraphOpCPU::RoughnessDual;
    else if (cat == "artisticiorcore")
      out.op = MaterialXGraphOpCPU::ArtisticIor;
    else if (cat == "chianghairabsorptioncore")
      out.op = MaterialXGraphOpCPU::ChiangHairAbsorption;
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
    else if (cat == "fractal2dcore") out.op = MaterialXGraphOpCPU::Fractal2D;
    else if (cat == "worleynoise2d") out.op = MaterialXGraphOpCPU::WorleyNoise2D;
    else if (cat == "worleynoise3d") out.op = MaterialXGraphOpCPU::WorleyNoise3D;
    else if (cat == "fractal3dcore") out.op = MaterialXGraphOpCPU::Fractal3D;
    else if (cat == "cloverleaf") out.op = MaterialXGraphOpCPU::Cloverleaf;
    else if (cat == "hexagon") out.op = MaterialXGraphOpCPU::Hexagon;
    else if (cat=="gridcore"||cat=="gridstaggeredcore") out.op=MaterialXGraphOpCPU::Grid;
    else if (cat=="crosshatchcore"||cat=="crosshatchstaggeredcore") out.op=MaterialXGraphOpCPU::Crosshatch;
    else if (cat=="tiledcirclescore"||cat=="tiledcirclesstaggeredcore") out.op=MaterialXGraphOpCPU::TiledCircles;
    else if (cat=="tiledcloverleafscore"||cat=="tiledcloverleafsstaggeredcore") out.op=MaterialXGraphOpCPU::TiledCloverleafs;
    else if (cat=="tiledhexagonscore"||cat=="tiledhexagonsstaggeredcore") out.op=MaterialXGraphOpCPU::TiledHexagons;
    else if(cat=="rampcoord")out.op=MaterialXGraphOpCPU::RampCoordinate;
    else if(cat=="rampcore")out.op=MaterialXGraphOpCPU::Ramp;
    else if(cat=="rampgradientcore")out.op=MaterialXGraphOpCPU::RampGradient;
    else if(cat=="flakecore")out.op=MaterialXGraphOpCPU::Flake;
    else if(cat=="matrixtransformcore")out.op=MaterialXGraphOpCPU::MatrixTransform;
    else if(cat=="matrixtransposecore")out.op=MaterialXGraphOpCPU::MatrixTranspose;
    else if(cat=="matrixinversecore")out.op=MaterialXGraphOpCPU::MatrixInverse;
    else if(cat=="matrixdeterminantcore")out.op=MaterialXGraphOpCPU::MatrixDeterminant;
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
            (conditional && inputName == "in2") ||
            ((cat == "fractal2dcore" || cat == "fractal3dcore") &&
             inputName == "diminish") ||
            ((cat.find("core")!=std::string::npos) &&
             (inputName=="thickness"||inputName=="size"))) {
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
        else if (cat == "fractal2dcore" && inputName == "texcoord") inputSlot = 0;
        else if (cat == "fractal2dcore" && inputName == "octaves") inputSlot = 1;
        else if (cat == "fractal2dcore" && inputName == "lacunarity") inputSlot = 2;
        else if (cat == "fractal3dcore" && inputName == "position") inputSlot = 0;
        else if (cat == "fractal3dcore" && inputName == "octaves") inputSlot = 1;
        else if (cat == "fractal3dcore" && inputName == "lacunarity") inputSlot = 2;
        else if ((cat == "noise2d" || cat == "noise3d") &&
                 (inputName == "texcoord" || inputName == "position")) inputSlot = 0;
        else if ((cat == "noise2d" || cat == "noise3d") && inputName == "amplitude") inputSlot = 1;
        else if ((cat == "noise2d" || cat == "noise3d") && inputName == "pivot") inputSlot = 2;
        else if ((cat == "cloverleaf" || cat == "hexagon") && inputName == "texcoord") inputSlot = 0;
        else if ((cat == "cloverleaf" || cat == "hexagon") && inputName == "center") inputSlot = 1;
        else if ((cat == "cloverleaf" || cat == "hexagon") && inputName == "radius") inputSlot = 2;
        else if (cat.find("core")!=std::string::npos && inputName=="texcoord") inputSlot=0;
        else if (cat.find("core")!=std::string::npos && inputName=="uvtiling") inputSlot=1;
        else if (cat.find("core")!=std::string::npos && inputName=="uvoffset") inputSlot=2;
        else if(cat=="rampcoord"&&inputName=="texcoord")inputSlot=0;
        else if(cat=="rampcoord"&&inputName=="type")inputSlot=1;
        else if((cat=="rampcore"||cat=="rampgradientcore")&&inputName=="x")inputSlot=0;
        else if((cat=="rampcore"||cat=="rampgradientcore")&&inputName=="interpolation")inputSlot=1;
        else if((cat=="rampcore"||cat=="rampgradientcore")&&inputName=="num_intervals")inputSlot=2;
        else if ((cat == "worleynoise2d" || cat == "worleynoise3d") &&
                 (inputName == "texcoord" || inputName == "position")) inputSlot = 0;
        else if ((cat == "worleynoise2d" || cat == "worleynoise3d") &&
                 inputName == "jitter") inputSlot = 1;
        else if ((cat == "worleynoise2d" || cat == "worleynoise3d") &&
                 inputName == "style") inputSlot = 2;
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
        std::string source = JsonString(input, "nodename");
        const std::string sourceOutput = JsonString(input, "output");
        if(!source.empty()&&!sourceOutput.empty()&&sourceOutput!="out")
          source += "__" + sourceOutput;
        if (!source.empty()) {
          const auto found = nodeIds.find(source);
          if (found != nodeIds.end()) out.input[inputSlot] = found->second;
        }
        if (valueIt != input.end()) {
          if (valueIt->is_number() || valueIt->is_boolean()) {
            // MaterialX promotes scalar inputs lane-wise when a polymorphic
            // vector/color operation consumes them. Keeping only x made
            // colorcorrect gamma/gain/exposure affect red while green and blue
            // saw the record's unrelated zero/one defaults.
            const float scalar = valueIt->is_boolean()
                                     ? (valueIt->get<bool>() ? 1.0f : 0.0f)
                                     : valueIt->get<float>();
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
    if (cat == "fractal2dcore" || cat == "fractal3dcore") {
      out.auxValue[3] = (type.find('4') != std::string::npos) ? 4.0f :
                        (type.find('3') != std::string::npos) ? 3.0f :
                        (type.find('2') != std::string::npos) ? 2.0f : 1.0f;
    }
    if (cat == "worleynoise2d" || cat == "worleynoise3d") {
      out.value[2][1] = (type.find('3') != std::string::npos) ? 3.0f :
                        (type.find('2') != std::string::npos) ? 2.0f : 1.0f;
    }
    if (cat == "noise2d" || cat == "noise3d") {
      out.value[2][3] = (type.find('4') != std::string::npos) ? 4.0f :
                        (type.find('3') != std::string::npos) ? 3.0f :
                        (type.find('2') != std::string::npos) ? 2.0f : 1.0f;
    }
    if(cat=="gridstaggeredcore"||cat=="crosshatchstaggeredcore"||
       cat=="tiledcirclesstaggeredcore"||cat=="tiledcloverleafsstaggeredcore"||
       cat=="tiledhexagonsstaggeredcore")out.auxValue[1]=1.0f;
    if(cat=="rampcore"||cat=="rampgradientcore"){
      const auto first=nodeIds.find(name+(cat=="rampcore"?"__interval1":"__interval1"));
      if(first!=nodeIds.end())out.auxValue[0]=static_cast<float>(first->second);
    }
    if(cat=="flakecore"){
      const size_t marker=name.rfind("__");
      const std::string base=marker==std::string::npos?name:name.substr(0,marker);
      const auto first=nodeIds.find(base+"__size");
      if(first!=nodeIds.end())out.auxValue[0]=static_cast<float>(first->second);
      out.auxValue[1]=node.value("flake_output",0);
      out.auxValue[2]=node.value("flake_3d",false)?1.0f:0.0f;
    }
    if(cat=="artisticiorcore")out.auxValue[0]=node.value("artistic_output",0);
    if(cat=="matrixtransformcore"||cat=="matrixtransposecore"||
       cat=="matrixinversecore"||cat=="matrixdeterminantcore"){
      const auto source=nodeIds.find(node.value("matrix_source",std::string()));
      if(source!=nodeIds.end())out.auxValue[0]=static_cast<float>(source->second);
      out.auxValue[1]=static_cast<float>(node.value("matrix_dim",4));
      out.auxValue[2]=static_cast<float>(node.value("matrix_column",0));
      out.auxValue[3]=(type.find('4')!=std::string::npos)?4.0f:
                      (type.find('3')!=std::string::npos)?3.0f:
                      (type.find('2')!=std::string::npos)?2.0f:1.0f;
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
      std::string node = JsonString(output, "nodename");
      const std::string selectedOutput=JsonString(output,"output");
      if(!node.empty()&&!selectedOutput.empty()&&selectedOutput!="out")
        node += "__"+selectedOutput;
      if (!name.empty() && !node.empty()) outputs[name] = node;
    }
  }
  const auto connIt = j.find("connections");
  const std::map<std::string, int> closureLaneOutput = {
      {"base_color",0},{"base_metalness",1},{"specular_roughness",2},
      {"geometry_opacity",3},{"emission_color",4},{"geometry_normal",5},
      {"subsurface_weight",6},{"subsurface_color",7},{"subsurface_radius",8},
      {"specular_weight",9},{"specular_color",10},{"transmission_weight",11},
      {"transmission_color",12},{"coat_weight",13},{"coat_color",14},
      {"coat_roughness",15},{"sheen_weight",16},{"sheen_color",17},
      {"sheen_roughness",18},{"specular_ior",19},{"base_weight",20},
      {"base_diffuse_roughness",21},{"transmission_scatter",22},
      {"transmission_depth",23},{"transmission_scatter_anisotropy",24},
      {"subsurface_scale",25},{"subsurface_anisotropy",26},{"coat_ior",27},
      {"thin_film_weight",28},{"thin_film_thickness",29},{"thin_film_ior",30},
      {"specular_anisotropy",31},{"specular_rotation",32},
      {"specular_roughness_anisotropy",33},{"transmission_dispersion",34},
      {"transmission_dispersion_abbe_number",35},
      {"transmission_dispersion_scale",36},{"coat_anisotropy",37},
      {"coat_rotation",38},{"coat_roughness_anisotropy",39},
      {"volume_density",40},{"volume_albedo",41},
      {"volume_emission_color",42},{"volume_emission_scale",43},
      {"emission_luminance",44},{"coat_affect_color",45},
      {"coat_affect_roughness",46},{"coat_darkening",47}};
  if (connIt != j.end() && connIt->is_array()) {
    for (const nlohmann::json& connection : *connIt) {
      const std::string input = OpenPBREvalInputName(
          JsonString(connection, "input"));
      const auto outputIt = outputs.find(JsonString(connection, "output"));
      if (outputIt == outputs.end()) continue;
      if (input == "bsdf" || input == "edf" || input == "vdf") {
        const auto closure = closureLanes.find(outputIt->second);
        if (closure == closureLanes.end()) continue;
        for (const auto& lane : closure->second) {
          const auto route = closureLaneOutput.find(lane.first);
          const auto node = nodeIds.find(lane.second);
          if (route != closureLaneOutput.end() && node != nodeIds.end())
            graph.output[static_cast<size_t>(route->second)] = node->second;
        }
        continue;
      }
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
  if (!mat->volumeMaterialXNodeGraphJson.empty()) {
    DrawMaterialCPU volume;
    std::string volumeErr;
    if (EvaluateMaterialXJsonGraphForVolume(*mat, &volume, &volumeErr)) {
      mat->volumeDensity = volume.volumeDensity;
      std::memcpy(mat->volumeAlbedo, volume.volumeAlbedo,
                  sizeof(mat->volumeAlbedo));
      mat->volumeAnisotropy = volume.volumeAnisotropy;
      std::memcpy(mat->volumeEmission, volume.volumeEmission,
                  sizeof(mat->volumeEmission));
      mat->volumeEmissionScale = volume.volumeEmissionScale;
      mat->hasVolumeOutput = true;
    }
  }
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
  if (mat->hasVolumeOutput) {
    p.transmissionScatterAnisotropy = mat->volumeAnisotropy;
  }
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
                              node.op == MaterialXGraphOpCPU::IfEqual ||
                              node.op == MaterialXGraphOpCPU::Fractal2D ||
                              node.op == MaterialXGraphOpCPU::Fractal3D ||
                              node.op == MaterialXGraphOpCPU::Grid ||
                              node.op == MaterialXGraphOpCPU::Crosshatch ||
                              node.op == MaterialXGraphOpCPU::TiledCircles ||
                              node.op == MaterialXGraphOpCPU::TiledCloverleafs ||
                              node.op == MaterialXGraphOpCPU::TiledHexagons;
    const bool usesRampTable=node.op==MaterialXGraphOpCPU::Ramp||
                             node.op==MaterialXGraphOpCPU::RampGradient||
                             node.op==MaterialXGraphOpCPU::Flake||
                             node.op==MaterialXGraphOpCPU::MatrixTransform||
                             node.op==MaterialXGraphOpCPU::MatrixTranspose||
                             node.op==MaterialXGraphOpCPU::MatrixInverse||
                             node.op==MaterialXGraphOpCPU::MatrixDeterminant||
                             node.op==MaterialXGraphOpCPU::ArtisticIor;
    int textureId = (usesAuxInput||usesRampTable) ? node.auxInput : node.textureId;
    if (!usesAuxInput && sourceToTable && textureId >= 0 &&
        static_cast<size_t>(textureId) < sourceToTable->size()) {
      textureId = (*sourceToTable)[static_cast<size_t>(textureId)];
    }
    dst[base + 16] = static_cast<float>(textureId);
    if (node.op == MaterialXGraphOpCPU::GeomProp) {
      const uint32_t hash = MaterialXGeomPropHash(node.geomPropName);
      float encoded = 0.0f;
      std::memcpy(&encoded, &hash, sizeof(encoded));
      dst[base + 17] = encoded;
      dst[base + 18] = node.auxValue[1];
      dst[base + 19] = node.auxValue[2];
      continue;
    }
    if (node.op == MaterialXGraphOpCPU::IfGreater ||
        node.op == MaterialXGraphOpCPU::IfGreaterEqual ||
        node.op == MaterialXGraphOpCPU::IfEqual ||
        node.op == MaterialXGraphOpCPU::Fractal2D ||
        node.op == MaterialXGraphOpCPU::Fractal3D ||
        node.op == MaterialXGraphOpCPU::Grid || node.op == MaterialXGraphOpCPU::Crosshatch ||
        node.op == MaterialXGraphOpCPU::TiledCircles || node.op == MaterialXGraphOpCPU::TiledCloverleafs ||
        node.op == MaterialXGraphOpCPU::TiledHexagons || usesRampTable) {
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
        node.op == MaterialXGraphOpCPU::IfEqual ||
        node.op == MaterialXGraphOpCPU::Fractal2D ||
        node.op == MaterialXGraphOpCPU::Fractal3D ||
        node.op == MaterialXGraphOpCPU::Grid || node.op == MaterialXGraphOpCPU::Crosshatch ||
        node.op == MaterialXGraphOpCPU::TiledCircles || node.op == MaterialXGraphOpCPU::TiledCloverleafs ||
        node.op == MaterialXGraphOpCPU::TiledHexagons ||
        node.op == MaterialXGraphOpCPU::Ramp || node.op == MaterialXGraphOpCPU::RampGradient ||
        node.op == MaterialXGraphOpCPU::Flake ||
        node.op == MaterialXGraphOpCPU::MatrixTransform ||
        node.op == MaterialXGraphOpCPU::MatrixTranspose ||
        node.op == MaterialXGraphOpCPU::MatrixInverse ||
        node.op == MaterialXGraphOpCPU::MatrixDeterminant) {
      dst[base + 16] = static_cast<float>(node.auxInput);
      if (node.op == MaterialXGraphOpCPU::IfGreater ||
          node.op == MaterialXGraphOpCPU::IfGreaterEqual ||
          node.op == MaterialXGraphOpCPU::IfEqual ||
          node.op == MaterialXGraphOpCPU::Fractal2D ||
          node.op == MaterialXGraphOpCPU::Fractal3D ||
          node.op == MaterialXGraphOpCPU::Grid || node.op == MaterialXGraphOpCPU::Crosshatch ||
          node.op == MaterialXGraphOpCPU::TiledCircles || node.op == MaterialXGraphOpCPU::TiledCloverleafs ||
          node.op == MaterialXGraphOpCPU::TiledHexagons ||
          node.op == MaterialXGraphOpCPU::Ramp || node.op == MaterialXGraphOpCPU::RampGradient ||
          node.op == MaterialXGraphOpCPU::Flake ||
          node.op == MaterialXGraphOpCPU::MatrixTransform ||
          node.op == MaterialXGraphOpCPU::MatrixTranspose ||
          node.op == MaterialXGraphOpCPU::MatrixInverse ||
          node.op == MaterialXGraphOpCPU::MatrixDeterminant)
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
