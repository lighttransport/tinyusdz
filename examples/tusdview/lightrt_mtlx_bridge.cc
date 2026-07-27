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
  FloatParam(mat, {"OpenPBRSurface"}, {"sheen_weight", "fuzz_weight"},
             &p->sheenWeight);
  Vec3Param(mat, {"OpenPBRSurface"}, {"sheen_color", "fuzz_color"},
            p->sheenColor);
  FloatParam(mat, {"OpenPBRSurface"}, {"sheen_roughness", "fuzz_roughness"},
             &p->sheenRoughness);
  FloatParam(mat, {"OpenPBRSurface"}, {"thin_film_weight"}, &p->thinFilmWeight);
  if (FloatParam(mat, {"OpenPBRSurface"}, {"thin_film_thickness"},
                 &p->thinFilmThicknessNm)) {
    p->thinFilmThicknessNm *= 1000.0f;  // OpenPBR micrometers -> LightRT nm.
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
  return EvaluateMaterialXStringToLightRtOpenPBR(
      xml.c_str(), "tusdview_material", out, err);
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

}  // namespace

bool EvaluateMaterialXStringToLightRtOpenPBR(const char* xml,
                                             const char* materialName,
                                             DrawLightRtOpenPBRCPU* out,
                                             std::string* err) {
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

  std::vector<MtlxValue> memo(static_cast<size_t>(doc->nnode));
  std::vector<char> memoDone(static_cast<size_t>(doc->nnode), 0);
  TextureCache* tex = texcache_create(nullptr);
  ShadeContext ctx{};
  ctx.doc = doc;
  ctx.tex = tex;
  ctx.uv[0] = 0.5f;
  ctx.uv[1] = 0.5f;
  ctx.P = v3_make(0.0f, 0.0f, 0.0f);
  ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
  ctx.Ng = v3_make(0.0f, 0.0f, 1.0f);
  ctx.dpdu = v3_make(1.0f, 0.0f, 0.0f);
  ctx.dpdv = v3_make(0.0f, 1.0f, 0.0f);
  ctx.memo = memo.data();
  ctx.memo_done = memoDone.data();

  OpenPBRParams params;
  const int rc = mtlx_eval_surface(&ctx, surfaceNode, &params);
  texcache_free(tex);
  mtlx_free(doc);
  if (rc != 0) {
    if (err) *err = "mtlx_eval_surface failed";
    return false;
  }

  DrawLightRtOpenPBRCPU baked;
  CopyLightRtEval(params, &baked);
  baked.hasTextureInputs = false;
  baked.hasNormalInput = (std::fabs(baked.normal[0]) > 0.0f ||
                          std::fabs(baked.normal[1]) > 0.0f ||
                          std::fabs(baked.normal[2] - 1.0f) > 1.0e-6f);
  ClampLightRtParams(&baked);
  *out = baked;
  return true;
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
      p.hasTextureInputs = graphParams.hasTextureInputs;
      p.hasNormalInput = graphParams.hasNormalInput;
      ClampLightRtParams(&p);
    }
  }

  mat->lightRtOpenPBR = p;
  mat->hasLightRtOpenPBR = true;

  // Keep the existing limited GL/VK/CUDA material buffers aligned with the
  // LightRT/OpenPBR constant fallback until those backends consume the full block.
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

void PackRasterMaterialTextureParams(const DrawMaterialCPU& mat, float* dst) {
  if (!dst) return;
  std::fill(dst, dst + kRasterMaterialTextureParamFloats, 0.0f);
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
  // Ptex displacement is baked with a texture-local face id before raster
  // upload. Disable the vertex-stage sample so the baked surface is not moved a
  // second time (the vertex stage has no primitive id with which to select a
  // Ptex face).
  dst[17 * 4 + 2] = mat.displacementSample.isPtex
                         ? 0.0f
                         : mat.displacementTexScale;
  dst[17 * 4 + 3] = mat.displacementSample.isPtex
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
}

}  // namespace tusdview
