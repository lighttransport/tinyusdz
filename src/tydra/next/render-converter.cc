// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Scene Converter Implementation

#include "render-converter.hh"
#include "next/schema/color-space.hh"
#include "next/eval/value-clip.hh"
#include "next/resolver/asset-resolver.hh"
#include "materialx.hh"
#include "next/layer/asset-anchor.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/schema/usdPhysics.hh"
#include "next/schema/usd-shade.hh"
#include "next/schema/usd-skel.hh"
#include "next/types/type-info.hh"
#include "tydra/fast-mikktspace.hh"
#include "tydra/mikktspace-tangent.hh"
#include "tydra/shape-to-mesh.hh"
#include "tydra/tangent-quantize.hh"
#include "external/mapbox/earcut/earcut.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace tinyusdz {
namespace tydra {
namespace next {

using ::tinyusdz::next::Stage;
using ::tinyusdz::next::UsdPrim;
using ::tinyusdz::next::Value;

namespace {

constexpr int kMaxMtlxConstantDepth = 64;

constexpr float kAlphaEpsilon = 1.0e-6f;
// 2GB is the typical hard limit for legacy WebAssembly linear memory growth in
// non-shared-memory builds. Keep a conservative per-mesh budget for temporary
// triangulation artifacts to avoid allocator abort on pathological data.
constexpr size_t kMaxTriangulationCornerCount = 150'000'000u;
constexpr uint32_t kEarcutMaxVertices = 16384;
constexpr size_t kMaxTempAllocBytes = 256u * 1024u * 1024u;

bool WouldOverflowSizeMul(size_t a, size_t b) {
  if (a == 0 || b == 0) return false;
  return a > (std::numeric_limits<size_t>::max() / b);
}

std::string SourcePrimPathFromConnection(const std::string& connection_path) {
  size_t dot_pos = connection_path.find(".outputs:");
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.find(".inputs:");
  }
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.rfind('.');
  }
  if (dot_pos == std::string::npos) {
    return connection_path;
  }
  return connection_path.substr(0, dot_pos);
}

bool SplitConnectionPath(const std::string& connection_path,
                         std::string* prim_path,
                         std::string* prop_name) {
  if (!prim_path || !prop_name) return false;
  size_t dot_pos = connection_path.find(".outputs:");
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.find(".inputs:");
  }
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.rfind('.');
  }
  if (dot_pos == std::string::npos) return false;

  *prim_path = connection_path.substr(0, dot_pos);
  *prop_name = connection_path.substr(dot_pos + 1);
  return !prim_path->empty() && !prop_name->empty();
}

bool IsTextureEndpoint(const Stage& stage, const UsdPrim& prim,
                       double time_code) {
  if (!prim.IsValid()) return false;
  std::string id;
  GetToken(prim, "info:id", &id);
  if (id == "UsdUVTexture" || id == "HwPtexTexture" ||
      id.rfind("HwPtexTexture", 0) == 0 || id == "PxrPtexture" ||
      id == "image" || id == "tiledimage" ||
      id.rfind("ND_image_", 0) == 0 ||
      id.rfind("ND_tiledimage_", 0) == 0) {
    return true;
  }
  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);
  return eval.EvalAssetPath(prim, "inputs:file").has_value() ||
         eval.EvalString(prim, "inputs:file").has_value() ||
         eval.EvalAssetPath(prim, "inputs:filename").has_value() ||
         eval.EvalString(prim, "inputs:filename").has_value();
}

const std::vector<::tinyusdz::next::Path>* PrimaryDataInputConnection(
    const UsdPrim& prim) {
  const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
  if (!spec) return nullptr;

  static const char* kPreferredInputs[] = {
      "inputs:in", "inputs:in1", "inputs:dispScalar", "inputs:inputRGB",
      "inputs:fg", "inputs:bg"};
  for (const char* preferred : kPreferredInputs) {
    const std::vector<::tinyusdz::next::Path>* connections =
        spec->connection(preferred);
    if (connections && !connections->empty()) return connections;
  }

  auto is_factor_input = [](const std::string& name) {
    return name == "inputs:mix" || name == "inputs:amount" ||
           name == "inputs:weight" || name == "inputs:factor" ||
           name == "inputs:alpha" || name == "inputs:mask";
  };
  for (const std::string& property : prim.GetPropertyNames()) {
    if (property.rfind("inputs:", 0) != 0 || is_factor_input(property)) {
      continue;
    }
    const std::vector<::tinyusdz::next::Path>* connections =
        spec->connection(property);
    if (connections && !connections->empty()) return connections;
  }
  return nullptr;
}

bool ResolveConnectedEndpoint(const Stage& stage,
                              const std::string& connection_path,
                              double time_code,
                              std::string* endpoint_path) {
  if (!endpoint_path) return false;
  std::string current = connection_path;
  std::set<std::string> visited;
  for (int depth = 0; depth <= kMaxMtlxConstantDepth; ++depth) {
    if (!visited.insert(current).second) return false;

    std::string prim_path;
    std::string prop_name;
    if (!SplitConnectionPath(current, &prim_path, &prop_name)) return false;
    UsdPrim prim = stage.GetPrimAtPath(prim_path);
    if (!prim.IsValid()) return false;
    if (IsTextureEndpoint(stage, prim, time_code)) {
      *endpoint_path = current;
      return true;
    }

    const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
    const std::vector<::tinyusdz::next::Path>* connections =
        spec ? spec->connection(prop_name) : nullptr;
    if (!connections || connections->empty()) {
      connections = PrimaryDataInputConnection(prim);
    }
    if (!connections || connections->empty()) {
      *endpoint_path = current;
      return true;
    }
    current = (*connections)[0].str();
  }
  return false;
}

bool FindConnectedUtilityScalar(const Stage& stage, const UsdPrim& shader,
                                const std::string& shader_input,
                                const std::string& node_id_prefix,
                                const std::string& node_input,
                                double time_code, float* out) {
  if (!out || !shader.IsValid()) return false;
  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);
  const std::string property = "inputs:" + shader_input;
  if (!eval.HasConnection(shader, property)) return false;
  std::string current = eval.GetConnectionPath(shader, property);
  std::set<std::string> visited;
  for (int depth = 0; depth <= kMaxMtlxConstantDepth; ++depth) {
    if (!visited.insert(current).second) return false;
    std::string prim_path;
    std::string prop_name;
    if (!SplitConnectionPath(current, &prim_path, &prop_name)) return false;
    const UsdPrim node = stage.GetPrimAtPath(prim_path);
    if (!node.IsValid()) return false;
    std::string id;
    GetToken(node, "info:id", &id);
    if (id.rfind(node_id_prefix, 0) == 0) {
      if (std::optional<float> value =
              eval.EvalFloat(node, "inputs:" + node_input)) {
        *out = *value;
        return true;
      }
      return false;
    }
    const ::tinyusdz::next::PrimSpec* spec = node.GetPrimSpec();
    const std::vector<::tinyusdz::next::Path>* connections =
        spec ? spec->connection(prop_name) : nullptr;
    if (!connections || connections->empty()) {
      connections = PrimaryDataInputConnection(node);
    }
    if (!connections || connections->empty()) return false;
    current = (*connections)[0].str();
  }
  return false;
}

bool ResolveConnectedValue(const Stage& stage,
                           const std::string& connection_path,
                           double time_code,
                           Value* out) {
  if (!out) return false;

  std::string endpoint;
  if (!ResolveConnectedEndpoint(stage, connection_path, time_code, &endpoint)) {
    return false;
  }
  std::string prim_path;
  std::string prop_name;
  if (!SplitConnectionPath(endpoint, &prim_path, &prop_name)) return false;

  UsdPrim prim = stage.GetPrimAtPath(prim_path);
  if (!prim.IsValid()) return false;

  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);
  ::tinyusdz::next::EvalOptions opts = eval.GetOptions();
  opts.follow_connections = false;
  ::tinyusdz::next::EvalResult result = eval.EvalWith(prim, prop_name, opts);
  if (result.success) {
    *out = std::move(result.value);
    return true;
  }

  return false;
}

RenderTexture::Channel ChannelFromConnection(
    const std::string& connection_path, const UsdPrim& texture_prim) {
  size_t pos = connection_path.find(".outputs:");
  if (pos == std::string::npos) {
    return RenderTexture::Channel::RGBA;
  }

  const std::string channel = connection_path.substr(pos + 9);
  if (channel == "r" || channel == "x") return RenderTexture::Channel::R;
  if (channel == "g" || channel == "y") return RenderTexture::Channel::G;
  if (channel == "b" || channel == "z") return RenderTexture::Channel::B;
  if (channel == "a" || channel == "w") return RenderTexture::Channel::A;
  if (channel == "rgb" || channel == "xyz") return RenderTexture::Channel::RGB;

  // MaterialX image nodes conventionally expose a generic `outputs:out`.
  // Recover its scalar/vector shape from the synthesized output value/type so
  // roughness and metallic maps sample R while color/normal maps sample RGB.
  if (channel == "out" && texture_prim.IsValid()) {
    std::string type;
    if (const Value* value = texture_prim.GetPropertyValue("outputs:out")) {
      if (const std::string* token = value->as_token()) type = *token;
      else if (const std::string* str = value->as_string()) type = *str;
    }
    if (type.empty()) {
      if (const ::tinyusdz::next::PrimSpec* spec =
              texture_prim.GetPrimSpec()) {
        if (const std::string* declared =
                spec->property_type_name("outputs:out")) {
          type = *declared;
        }
      }
    }
    if (type == "float" || type == "integer" || type == "boolean") {
      return RenderTexture::Channel::R;
    }
    if (type == "color3" || type == "color3f" || type == "vector3" ||
        type == "vector3f") {
      return RenderTexture::Channel::RGB;
    }
  }
  return RenderTexture::Channel::RGBA;
}

WrapMode ParseWrapMode(const std::string& token) {
  if (token == "repeat") return WrapMode::Repeat;
  if (token == "clamp") return WrapMode::Clamp;
  if (token == "mirror") return WrapMode::Mirror;
  if (token == "black") return WrapMode::Black;
  // UsdUVTexture's wrapS/wrapT fallback is "useMetadata"; with no texture
  // metadata the effective mode is clamp-to-edge (legacy tydra behavior) —
  // NOT repeat, which visibly tiles textures authored to clamp.
  return WrapMode::Clamp;
}

ColorSpace ParseColorSpace(const std::string& token) {
  if (token == "raw") return ColorSpace::Raw;
  if (token == "linear" || token == "Linear" || token == "lin_srgb" ||
      token == "lin_rec709" || token == "scene-linear Rec.709-sRGB") {
    return ColorSpace::Linear;
  }
  if (token == "sRGB" || token == "srgb" || token == "srgb_texture") {
    return ColorSpace::sRGB;
  }
  if (token == "acescg" || token == "ACEScg") return ColorSpace::ACEScg;
  if (token == "rec709" || token == "Rec709") return ColorSpace::Rec709;
  if (token == "rec2020" || token == "Rec2020" ||
      token == "lin_rec2020") return ColorSpace::Rec2020;
  if (token == "displayP3" || token == "DisplayP3" ||
      token == "lin_displayp3" || token == "srgb_displayp3") {
    return ColorSpace::DisplayP3;
  }
  return ColorSpace::Unknown;
}

bool IsColorShaderInput(const UsdPrim& prim, const std::string& attr_name,
                        const std::string& param_name) {
  if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
    if (const std::string* type = spec->property_type_name(attr_name)) {
      if (type->rfind("color3", 0) == 0 || type->rfind("color4", 0) == 0) {
        return true;
      }
    }
  }
  static const std::set<std::string> kColorInputs = {
      "diffuseColor", "emissiveColor", "specularColor", "base_color",
      "baseColor", "specular_color", "transmission_color",
      "subsurface_color", "sheen_color", "coat_color", "emission_color"};
  return kColorInputs.count(param_name) != 0;
}

bool MaterialXConfiguredColorSpace(const UsdPrim& prim, std::string* out) {
  if (!out || !prim.IsValid()) return false;
  std::string id;
  if (!GetToken(prim, "info:id", &id) ||
      (id.rfind("ND_", 0) != 0 && id != "image" &&
       id != "tiledimage" && id != "open_pbr_surface" &&
       id != "standard_surface")) {
    return false;
  }
  for (UsdPrim current = prim; current.IsValid(); current = current.GetParent()) {
    const Value* value =
        current.GetPropertyValue("config:mtlx:colorspace");
    if (!value) continue;
    const std::string* token = value->as_token();
    if (!token) token = value->as_string();
    if (token && !token->empty()) {
      *out = ::tinyusdz::color::CanonicalizeToken(*token);
      return true;
    }
  }
  return false;
}

bool ResolveConnectedColorSource(const Stage& stage,
                                 const std::string& connection_path,
                                 double time_code, UsdPrim* source_prim,
                                 std::string* source_property) {
  if (!source_prim || !source_property) return false;
  std::string endpoint;
  if (!ResolveConnectedEndpoint(stage, connection_path, time_code, &endpoint)) {
    return false;
  }
  std::string prim_path;
  std::string property;
  if (!SplitConnectionPath(endpoint, &prim_path, &property)) return false;
  UsdPrim prim = stage.GetPrimAtPath(prim_path);
  if (!prim.IsValid()) return false;

  // Prefer metadata on the resolved output itself. MaterialX constant and
  // utility nodes usually put it on their value/data input instead, so scan
  // those inputs before falling back to the terminal shader attribute.
  if (const ::tinyusdz::next::PropMeta* meta =
          prim.GetPropertyMeta(property)) {
    if ((meta->authored & ::tinyusdz::next::PropMeta::kColorSpace) != 0u) {
      *source_prim = prim;
      *source_property = property;
      return true;
    }
  }
  std::string value_input;
  for (const std::string& candidate : prim.GetPropertyNames()) {
    if (candidate.rfind("inputs:", 0) != 0) continue;
    if (value_input.empty() || candidate == "inputs:value" ||
        candidate == "inputs:in") {
      value_input = candidate;
    }
    if (const ::tinyusdz::next::PropMeta* meta =
            prim.GetPropertyMeta(candidate)) {
      if ((meta->authored & ::tinyusdz::next::PropMeta::kColorSpace) != 0u) {
        *source_prim = prim;
        *source_property = candidate;
        return true;
      }
    }
  }
  if (!value_input.empty()) {
    *source_prim = prim;
    *source_property = value_input;
    return true;
  }
  *source_prim = prim;
  *source_property = property;
  return true;
}

void ConvertShaderColorToWorking(const UsdPrim& prim,
                                 const std::string& attr_name,
                                 const std::string& param_name,
                                 const RenderScene* scene,
                                 ShaderParam* param) {
  if (!scene || !param || param->is_texture() ||
      !IsColorShaderInput(prim, attr_name, param_name)) {
    return;
  }
  std::string source;
  bool authored = false;
  if (!::tinyusdz::next::color_management::ComputeColorSpaceName(
          prim, attr_name, &source, &authored)) {
    return;
  }
  if (!authored) {
    (void)MaterialXConfiguredColorSpace(prim, &source);
  }
  ::tinyusdz::color::ColorTransform transform;
  if (!::tinyusdz::next::color_management::BuildColorTransform(
          prim, source, scene->working_color_space, &transform)) {
    return;
  }
  float rgb[3] = {param->value.x, param->value.y, param->value.z};
  ::tinyusdz::color::TransformRGB(transform, rgb);
  param->value.x = rgb[0];
  param->value.y = rgb[1];
  param->value.z = rgb[2];
}

void SetParamFloat(ShaderParam* out, float x) {
  out->texture_id = -1;
  out->value = Float4(x, 0.0f, 0.0f, 0.0f);
}

void SetParamFloat3(ShaderParam* out, float x, float y, float z) {
  out->texture_id = -1;
  out->value = Float4(x, y, z, 1.0f);
}

void SetParamFloat4(ShaderParam* out, float x, float y, float z, float w) {
  out->texture_id = -1;
  out->value = Float4(x, y, z, w);
}

bool ValueToShaderParam(const Value& value, ShaderParam* out) {
  if (!out || value.is_empty() || value.is_array()) return false;

  if (const float* v = value.as_float()) {
    SetParamFloat(out, *v);
    return true;
  }
  if (const double* v = value.as_double()) {
    SetParamFloat(out, static_cast<float>(*v));
    return true;
  }
  if (const int32_t* v = value.as_int()) {
    SetParamFloat(out, static_cast<float>(*v));
    return true;
  }
  if (const uint32_t* v = value.as_uint()) {
    SetParamFloat(out, static_cast<float>(*v));
    return true;
  }
  if (const bool* v = value.as_bool()) {
    SetParamFloat(out, *v ? 1.0f : 0.0f);
    return true;
  }
  if (const float* v = value.as_float2()) {
    SetParamFloat4(out, v[0], v[1], 0.0f, 1.0f);
    return true;
  }
  if (const float* v = value.as_float3()) {
    SetParamFloat3(out, v[0], v[1], v[2]);
    return true;
  }
  if (const float* v = value.as_float4()) {
    SetParamFloat4(out, v[0], v[1], v[2], v[3]);
    return true;
  }
  if (const double* v = value.as_double2()) {
    SetParamFloat4(out, static_cast<float>(v[0]), static_cast<float>(v[1]),
                   0.0f, 1.0f);
    return true;
  }
  if (const double* v = value.as_double3()) {
    SetParamFloat3(out, static_cast<float>(v[0]), static_cast<float>(v[1]),
                   static_cast<float>(v[2]));
    return true;
  }
  if (const double* v = value.as_double4()) {
    SetParamFloat4(out, static_cast<float>(v[0]), static_cast<float>(v[1]),
                   static_cast<float>(v[2]), static_cast<float>(v[3]));
    return true;
  }
  // Half-precision shader inputs (half/half2/half3/half4 + role types) store
  // raw half-bit lanes; widen through the converting reads.
  {
    float h[4];
    if (value.to_float(h)) {
      SetParamFloat(out, h[0]);
      return true;
    }
    if (value.to_float2(h)) {
      SetParamFloat4(out, h[0], h[1], 0.0f, 1.0f);
      return true;
    }
    if (value.to_float3(h)) {
      SetParamFloat3(out, h[0], h[1], h[2]);
      return true;
    }
    if (value.to_float4(h)) {
      SetParamFloat4(out, h[0], h[1], h[2], h[3]);
      return true;
    }
  }

  return false;
}

struct MtlxConstantValue {
  std::array<float, 4> value{{0.0f, 0.0f, 0.0f, 0.0f}};
  int components = 0;
  bool color_managed = false;

  float component(int i) const {
    return value[static_cast<size_t>(i < components ? i : 0)];
  }
};

bool ValueToMtlxConstant(const Value& value, MtlxConstantValue* out) {
  if (!out) return false;
  ShaderParam param;
  if (!ValueToShaderParam(value, &param)) return false;
  out->value = {{param.value.x, param.value.y, param.value.z, param.value.w}};
  if (value.as_float3() || value.as_double3()) out->components = 3;
  else if (value.as_float4() || value.as_double4()) out->components = 4;
  else if (value.as_float2() || value.as_double2()) out->components = 2;
  else {
    float widened[4];
    if (value.to_float4(widened)) out->components = 4;
    else if (value.to_float3(widened)) out->components = 3;
    else if (value.to_float2(widened)) out->components = 2;
    else out->components = 1;
  }
  return true;
}

MtlxConstantValue MtlxBinary(const MtlxConstantValue& a,
                             const MtlxConstantValue& b,
                             const std::function<float(float, float)>& op) {
  MtlxConstantValue out;
  out.components = std::max(a.components, b.components);
  out.color_managed = a.color_managed || b.color_managed;
  for (int i = 0; i < out.components; ++i) {
    out.value[static_cast<size_t>(i)] = op(a.component(i), b.component(i));
  }
  return out;
}

bool EvalMtlxConstantConnection(const Stage& stage,
                                const std::string& connection,
                                double time_code,
                                const std::string& evaluation_space,
                                MtlxConstantValue* out,
                                std::set<std::string>* visiting,
                                int depth);

bool TransformMtlxColorInput(const UsdPrim& node,
                             const std::string& property,
                             const std::string& evaluation_space,
                             MtlxConstantValue* value) {
  if (!value) return false;
  if (value->components < 3 || evaluation_space.empty()) return true;
  const ::tinyusdz::next::PrimSpec* spec = node.GetPrimSpec();
  const std::string* type = spec ? spec->property_type_name(property) : nullptr;
  if (!type || (type->rfind("color3", 0) != 0 &&
                type->rfind("color4", 0) != 0)) {
    return true;
  }

  std::string source;
  bool authored = false;
  if (!::tinyusdz::next::color_management::ComputeColorSpaceName(
          node, property, &source, &authored)) {
    return false;
  }
  if (!authored) source = evaluation_space;
  ::tinyusdz::color::ColorTransform transform;
  if (!::tinyusdz::next::color_management::BuildColorTransform(
          node, source, evaluation_space, &transform)) {
    return false;
  }
  float rgb[3] = {value->value[0], value->value[1], value->value[2]};
  ::tinyusdz::color::TransformRGB(transform, rgb);
  value->value[0] = rgb[0];
  value->value[1] = rgb[1];
  value->value[2] = rgb[2];
  value->color_managed =
      transform.source.kind != ::tinyusdz::color::ColorSpaceKind::Data;
  return true;
}

bool EvalMtlxInput(const Stage& stage, const UsdPrim& node,
                   const std::string& input, double time_code,
                   const std::string& evaluation_space,
                   MtlxConstantValue* out, std::set<std::string>* visiting,
                   int depth) {
  if (!out || depth > kMaxMtlxConstantDepth) return false;
  const std::string property = "inputs:" + input;
  const ::tinyusdz::next::PrimSpec* spec = node.GetPrimSpec();
  const std::vector<::tinyusdz::next::Path>* connections =
      spec ? spec->connection(property) : nullptr;
  if (connections && !connections->empty()) {
    return EvalMtlxConstantConnection(stage, (*connections)[0].str(),
                                      time_code, evaluation_space, out,
                                      visiting, depth + 1);
  }
  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);
  ::tinyusdz::next::EvalOptions options = eval.GetOptions();
  options.follow_connections = false;
  const ::tinyusdz::next::EvalResult result =
      eval.EvalWith(node, property, options);
  return result.success && ValueToMtlxConstant(result.value, out) &&
         TransformMtlxColorInput(node, property, evaluation_space, out);
}

bool EvalMtlxConstantNode(const Stage& stage, const UsdPrim& node,
                          double time_code,
                          const std::string& evaluation_space,
                          MtlxConstantValue* out,
                          std::set<std::string>* visiting, int depth) {
  if (!out || !node.IsValid() || depth > kMaxMtlxConstantDepth || !visiting) {
    return false;
  }
  const std::string key = node.GetPath().str();
  if (!visiting->insert(key).second) return false;
  struct VisitGuard {
    std::set<std::string>* set;
    std::string key;
    ~VisitGuard() { set->erase(key); }
  } guard{visiting, key};

  std::string id;
  GetToken(node, "info:id", &id);
  auto input = [&](const char* name, MtlxConstantValue* value) {
    return EvalMtlxInput(stage, node, name, time_code, evaluation_space, value,
                         visiting, depth + 1);
  };
  auto starts = [&id](const char* prefix) { return id.rfind(prefix, 0) == 0; };

  if (id == "ND_constant_float" || id == "ND_constant_color3" ||
      id == "ND_constant_vector3" || id == "ND_constant_color4") {
    return input("value", out);
  }

  if (starts("ND_add_") || starts("ND_subtract_") ||
      starts("ND_multiply_") || starts("ND_divide_") ||
      starts("ND_min_") || starts("ND_max_") || starts("ND_power_")) {
    MtlxConstantValue a, b;
    if (!input("in1", &a) || !input("in2", &b)) return false;
    if (starts("ND_add_")) *out = MtlxBinary(a, b, [](float x, float y) { return x + y; });
    else if (starts("ND_subtract_")) *out = MtlxBinary(a, b, [](float x, float y) { return x - y; });
    else if (starts("ND_multiply_")) *out = MtlxBinary(a, b, [](float x, float y) { return x * y; });
    else if (starts("ND_divide_")) *out = MtlxBinary(a, b, [](float x, float y) { return std::abs(y) > 1.0e-8f ? x / y : 0.0f; });
    else if (starts("ND_min_")) *out = MtlxBinary(a, b, [](float x, float y) { return std::min(x, y); });
    else if (starts("ND_max_")) *out = MtlxBinary(a, b, [](float x, float y) { return std::max(x, y); });
    else *out = MtlxBinary(a, b, [](float x, float y) { return std::pow(x, y); });
    return true;
  }

  if (starts("ND_mix_")) {
    MtlxConstantValue bg, fg, amount;
    if (!input("bg", &bg) || !input("fg", &fg) ||
        !input("mix", &amount)) return false;
    const float t = amount.component(0);
    *out = MtlxBinary(bg, fg, [t](float x, float y) {
      return x * (1.0f - t) + y * t;
    });
    return true;
  }

  if (starts("ND_clamp_")) {
    MtlxConstantValue value, low, high;
    if (!input("in", &value) || !input("low", &low) ||
        !input("high", &high)) return false;
    *out = value;
    out->color_managed = value.color_managed || low.color_managed ||
                         high.color_managed;
    for (int i = 0; i < out->components; ++i) {
      out->value[static_cast<size_t>(i)] = std::min(
          std::max(value.component(i), low.component(i)), high.component(i));
    }
    return true;
  }

  if (starts("ND_remap_")) {
    MtlxConstantValue value, in_low, in_high, out_low, out_high;
    if (!input("in", &value) || !input("inlow", &in_low) ||
        !input("inhigh", &in_high) || !input("outlow", &out_low) ||
        !input("outhigh", &out_high)) return false;
    *out = value;
    out->color_managed = value.color_managed || in_low.color_managed ||
                         in_high.color_managed || out_low.color_managed ||
                         out_high.color_managed;
    for (int i = 0; i < out->components; ++i) {
      const float denom = in_high.component(i) - in_low.component(i);
      const float t = std::abs(denom) > 1.0e-8f
                          ? (value.component(i) - in_low.component(i)) / denom
                          : 0.0f;
      out->value[static_cast<size_t>(i)] =
          out_low.component(i) + t * (out_high.component(i) - out_low.component(i));
    }
    return true;
  }

  if (starts("ND_combine3_")) {
    MtlxConstantValue a, b, c;
    if (!input("in1", &a) || !input("in2", &b) || !input("in3", &c)) {
      return false;
    }
    out->components = 3;
    out->value = {{a.component(0), b.component(0), c.component(0), 0.0f}};
    out->color_managed =
        a.color_managed || b.color_managed || c.color_managed;
    return true;
  }

  if (starts("ND_extract_")) {
    MtlxConstantValue value, index;
    if (!input("in", &value) || !input("index", &index)) return false;
    int component = static_cast<int>(index.component(0));
    if (component < 0 || component >= value.components) component = 0;
    out->components = 1;
    out->value[0] = value.value[static_cast<size_t>(component)];
    out->color_managed = value.color_managed;
    return true;
  }

  if (starts("ND_normalize_")) {
    MtlxConstantValue value;
    if (!input("in", &value)) return false;
    float length_squared = 0.0f;
    for (int i = 0; i < value.components; ++i) {
      const float component = value.component(i);
      length_squared += component * component;
    }
    const float length = std::sqrt(length_squared);
    *out = value;
    if (length > 1.0e-7f) {
      for (int i = 0; i < out->components; ++i) {
        out->value[static_cast<size_t>(i)] /= length;
      }
    }
    return true;
  }

  if (starts("ND_ifgreater_") || starts("ND_ifgreatereq_") ||
      starts("ND_ifequal_")) {
    MtlxConstantValue value1, value2, when_true, when_false;
    if (!input("value1", &value1) || !input("value2", &value2) ||
        !input("in1", &when_true) || !input("in2", &when_false)) {
      return false;
    }
    const float a = value1.component(0);
    const float b = value2.component(0);
    bool condition = false;
    if (starts("ND_ifgreatereq_")) {
      condition = a >= b;
    } else if (starts("ND_ifgreater_")) {
      condition = a > b;
    } else {
      const float scale = std::max({std::fabs(a), std::fabs(b), 1.0f});
      condition = std::fabs(a - b) <=
                  std::numeric_limits<float>::epsilon() * scale;
    }
    *out = condition ? when_true : when_false;
    return true;
  }

  if (starts("ND_convert_")) return input("in", out);

  if (id == "ND_hsv_adjust_color3" || id == "ND_hsvadjust_color3") {
    MtlxConstantValue color, hue, saturation, value, factor;
    // The standard MaterialX hsvadjust amount is a direct hue offset with
    // (0, 1, 1) as its identity. Blender's separate-input variant exposes a
    // UI control where 0.5 is neutral instead.
    const float hue_neutral =
        id == "ND_hsvadjust_color3" ? 0.0f : 0.5f;
    if (!input("in", &color)) return false;
    if (id == "ND_hsvadjust_color3") {
      MtlxConstantValue amount;
      if (!input("amount", &amount) || amount.components < 3) return false;
      hue.components = saturation.components = value.components =
          factor.components = 1;
      hue.value[0] = amount.component(0);
      saturation.value[0] = amount.component(1);
      value.value[0] = amount.component(2);
      factor.value[0] = 1.0f;
    } else if (!input("hue", &hue) ||
               !input("saturation", &saturation) ||
               !input("value", &value) || !input("fac", &factor)) {
      return false;
    }
    const float r = color.component(0), g = color.component(1), b = color.component(2);
    const float maximum = std::max({r, g, b});
    const float minimum = std::min({r, g, b});
    const float delta = maximum - minimum;
    float h = 0.0f;
    if (delta > 1.0e-7f) {
      if (r >= maximum) h = (g - b) / delta;
      else if (g >= maximum) h = 2.0f + (b - r) / delta;
      else h = 4.0f + (r - g) / delta;
      h /= 6.0f;
      if (h < 0.0f) h += 1.0f;
    }
    float s = maximum > 0.0f ? delta / maximum : 0.0f;
    float v = maximum;
    h = std::fmod(h + (hue.component(0) - hue_neutral) + 1.0f, 1.0f);
    s *= saturation.component(0);
    v *= value.component(0);
    float adjusted[3] = {v, v, v};
    if (s > 0.0f) {
      const float hh = h * 6.0f;
      const int sector = static_cast<int>(std::floor(hh)) % 6;
      const float fraction = hh - std::floor(hh);
      const float p = v * (1.0f - s);
      const float q = v * (1.0f - s * fraction);
      const float t = v * (1.0f - s * (1.0f - fraction));
      const float table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t},
                                 {p, q, v}, {t, p, v}, {v, p, q}};
      for (int i = 0; i < 3; ++i) adjusted[i] = table[sector][i];
    }
    const float mix = factor.component(0);
    out->components = 3;
    out->color_managed = color.color_managed;
    for (int i = 0; i < 3; ++i) {
      out->value[static_cast<size_t>(i)] =
          color.component(i) * (1.0f - mix) + adjusted[i] * mix;
    }
    return true;
  }
  return false;
}

bool EvalMtlxConstantConnection(const Stage& stage,
                                const std::string& connection,
                                double time_code,
                                const std::string& evaluation_space,
                                MtlxConstantValue* out,
                                std::set<std::string>* visiting,
                                int depth) {
  if (!out || depth > kMaxMtlxConstantDepth) return false;
  std::string prim_path, property;
  if (!SplitConnectionPath(connection, &prim_path, &property)) return false;
  const UsdPrim prim = stage.GetPrimAtPath(prim_path);
  if (!prim.IsValid()) return false;
  const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
  const std::vector<::tinyusdz::next::Path>* forwarded =
      spec ? spec->connection(property) : nullptr;
  if (forwarded && !forwarded->empty()) {
    return EvalMtlxConstantConnection(stage, (*forwarded)[0].str(), time_code,
                                      evaluation_space, out, visiting,
                                      depth + 1);
  }
  if (::tinyusdz::next::IsShader(prim)) {
    return EvalMtlxConstantNode(stage, prim, time_code, evaluation_space, out,
                                visiting, depth + 1);
  }
  return false;
}

bool ValueToFloat4(const Value& value, Float4* out) {
  if (!out || value.is_empty() || value.is_array()) return false;

  if (const float* v = value.as_float()) {
    *out = Float4(*v, 0.0f, 0.0f, 0.0f);
    return true;
  }
  if (const double* v = value.as_double()) {
    *out = Float4(static_cast<float>(*v), 0.0f, 0.0f, 0.0f);
    return true;
  }
  if (const float* v = value.as_float3()) {
    *out = Float4(v[0], v[1], v[2], 0.0f);
    return true;
  }
  if (const double* v = value.as_double3()) {
    *out = Float4(static_cast<float>(v[0]), static_cast<float>(v[1]),
                  static_cast<float>(v[2]), 0.0f);
    return true;
  }
  if (const float* v = value.as_float4()) {
    *out = Float4(v[0], v[1], v[2], v[3]);
    return true;
  }
  if (const double* v = value.as_double4()) {
    *out = Float4(static_cast<float>(v[0]), static_cast<float>(v[1]),
                  static_cast<float>(v[2]), static_cast<float>(v[3]));
    return true;
  }
  // Authored half-precision scalars (half3 rotate/scale, quath orient, ...)
  // store raw half-bit lanes; the converting reads widen them.
  float h[4];
  if (value.to_float3(h)) {
    *out = Float4(h[0], h[1], h[2], 0.0f);
    return true;
  }
  if (value.to_float4(h)) {
    *out = Float4(h[0], h[1], h[2], h[3]);
    return true;
  }
  if (value.to_float(h)) {
    *out = Float4(h[0], 0.0f, 0.0f, 0.0f);
    return true;
  }
  return false;
}

// Closed-form Euler-degrees -> quaternion (xyzw) for all six USD rotation
// orders (rotateXYZ means apply X first: Q = Qz * Qy * Qx). Ported from the
// legacy tydra converter so Rotation channels always carry quaternions.
Float4 EulerDegreesToQuatXYZW(float xdeg, float ydeg, float zdeg,
                              const std::string& order) {
  const double kHalfDegToRad = 3.14159265358979323846 / 360.0;
  const float sx = static_cast<float>(std::sin(double(xdeg) * kHalfDegToRad));
  const float cx = static_cast<float>(std::cos(double(xdeg) * kHalfDegToRad));
  const float sy = static_cast<float>(std::sin(double(ydeg) * kHalfDegToRad));
  const float cy = static_cast<float>(std::cos(double(ydeg) * kHalfDegToRad));
  const float sz = static_cast<float>(std::sin(double(zdeg) * kHalfDegToRad));
  const float cz = static_cast<float>(std::cos(double(zdeg) * kHalfDegToRad));

  if (order == "XZY") {  // Q = Qy * Qz * Qx
    return Float4(cy*cz*sx + sy*sz*cx, cy*sz*sx + sy*cz*cx,
                  cy*sz*cx - sy*cz*sx, cy*cz*cx - sy*sz*sx);
  }
  if (order == "YXZ") {  // Q = Qz * Qx * Qy
    return Float4(cz*sx*cy - sz*cx*sy, cz*cx*sy + sz*sx*cy,
                  cz*sx*sy + sz*cx*cy, cz*cx*cy - sz*sx*sy);
  }
  if (order == "YZX") {  // Q = Qx * Qz * Qy
    return Float4(sx*cz*cy - cx*sz*sy, cx*cz*sy - sx*sz*cy,
                  cx*sz*cy + sx*cz*sy, cx*cz*cy + sx*sz*sy);
  }
  if (order == "ZXY") {  // Q = Qy * Qx * Qz
    return Float4(cy*sx*cz + sy*cx*sz, sy*cx*cz - cy*sx*sz,
                  cy*cx*sz - sy*sx*cz, cy*cx*cz + sy*sx*sz);
  }
  if (order == "ZYX") {  // Q = Qx * Qy * Qz
    return Float4(cx*sy*sz + sx*cy*cz, cx*sy*cz - sx*cy*sz,
                  cx*cy*sz + sx*sy*cz, cx*cy*cz - sx*sy*sz);
  }
  // XYZ (and fallback): Q = Qz * Qy * Qx
  return Float4(cz*cy*sx - sz*sy*cx, cz*sy*cx + sz*cy*sx,
                sz*cy*cx - cz*sy*sx, cz*cy*cx + sz*sy*sx);
}

// Extracts the axis order ("XYZ", "ZYX", ...) from an xformOp:rotate<ORDER>
// property name. Returns false for single-axis rotateX/Y/Z and non-rotate ops.
bool EulerRotationOrderFromPropName(const std::string& prop_name,
                                    std::string* out_order) {
  const size_t pos = prop_name.find("rotate");
  if (pos == std::string::npos) return false;
  const std::string tail = prop_name.substr(pos + 6, 3);
  if (tail == "XYZ" || tail == "XZY" || tail == "YXZ" || tail == "YZX" ||
      tail == "ZXY" || tail == "ZYX") {
    *out_order = tail;
    return true;
  }
  return false;
}

bool ValueToAnimationFloat4(const std::string& prop_name,
                            const Value& value,
                            Float4* out) {
  if (!out || value.is_empty() || value.is_array()) return false;

  float scalar = 0.0f;
  bool is_scalar = false;
  if (const float* v = value.as_float()) {
    scalar = *v;
    is_scalar = true;
  } else if (const double* v = value.as_double()) {
    scalar = static_cast<float>(*v);
    is_scalar = true;
  }

  if (is_scalar) {
    // Single-axis rotations become quaternions: Rotation channels are
    // consumed as xyzw quats by the render layer, never as raw degrees.
    if (prop_name.find("rotateX") != std::string::npos) {
      *out = EulerDegreesToQuatXYZW(scalar, 0.0f, 0.0f, "XYZ");
    } else if (prop_name.find("rotateY") != std::string::npos) {
      *out = EulerDegreesToQuatXYZW(0.0f, scalar, 0.0f, "XYZ");
    } else if (prop_name.find("rotateZ") != std::string::npos) {
      *out = EulerDegreesToQuatXYZW(0.0f, 0.0f, scalar, "XYZ");
    } else if (prop_name.find("scale") != std::string::npos) {
      *out = Float4(scalar, scalar, scalar, 0.0f);
    } else {
      *out = Float4(scalar, 0.0f, 0.0f, 0.0f);
    }
    return true;
  }

  if (!ValueToFloat4(value, out)) return false;

  // next-core Values keep quats real-first (w, x, y, z); render animation
  // channels use xyzw (three.js quaternion order). xformOp:orient is the
  // quat-valued xform op.
  const ::tinyusdz::next::TypeId tid = value.type_id();
  if (tid == ::tinyusdz::next::TypeId::Quatf ||
      tid == ::tinyusdz::next::TypeId::Quatd ||
      tid == ::tinyusdz::next::TypeId::Quath) {
    *out = Float4(out->y, out->z, out->w, out->x);
    return true;
  }

  // Three-axis Euler rotate ops (float3 degrees) also convert to quats.
  std::string rot_order;
  if (EulerRotationOrderFromPropName(prop_name, &rot_order)) {
    *out = EulerDegreesToQuatXYZW(out->x, out->y, out->z, rot_order);
  }
  return true;
}

void AssignNodeDataId(RenderScene* scene,
                      const std::string& prim_path,
                      int32_t data_id) {
  if (!scene) return;
  const auto node_it = scene->node_by_path.find(prim_path);
  if (node_it == scene->node_by_path.end()) return;
  const int32_t node_id = node_it->second;
  if (node_id < 0 || static_cast<size_t>(node_id) >= scene->nodes.size()) return;
  scene->nodes[static_cast<size_t>(node_id)].data_id = data_id;
}

void SetIdentity(Matrix4* m) {
  if (!m) return;
  *m = Matrix4::Identity();
}

void CopyMatrixFromDoubles(const std::vector<double>& values,
                           size_t matrix_index,
                           Matrix4* out) {
  if (!out) return;
  SetIdentity(out);
  const size_t offset = matrix_index * 16;
  if (offset + 16 > values.size()) return;
  for (size_t i = 0; i < 16; ++i) {
    out->m[i] = static_cast<float>(values[offset + i]);
  }
}

Matrix4 MatrixFromPointInstancerTransform(
    const ::tinyusdz::next::PointInstancerTransform& src) {
  Matrix4 dst;
  for (size_t i = 0; i < 16; ++i) {
    dst.m[i] = static_cast<float>(src.matrix[i]);
  }
  return dst;
}

// General 4x4 inverse (Gauss-Jordan, double precision). Returns false for a
// singular matrix. Used to derive rest transforms from bind transforms.
bool InvertMatrix4x4D(const double m[16], double out[16]) {
  double a[4][8];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      a[r][c] = m[r * 4 + c];
      a[r][c + 4] = (r == c) ? 1.0 : 0.0;
    }
  }
  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    for (int r = col + 1; r < 4; ++r) {
      if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
    }
    if (std::fabs(a[pivot][col]) < 1e-12) return false;
    if (pivot != col) {
      for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[pivot][c]);
    }
    const double inv_p = 1.0 / a[col][col];
    for (int c = 0; c < 8; ++c) a[col][c] *= inv_p;
    for (int r = 0; r < 4; ++r) {
      if (r == col) continue;
      const double f = a[r][col];
      if (f == 0.0) continue;
      for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
    }
  }
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][c + 4];
  }
  return true;
}

Matrix4 MulMatrix4(const Matrix4& a, const Matrix4& b) {
  Matrix4 r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r.m[i * 4 + j] =
          a.m[i * 4 + 0] * b.m[0 * 4 + j] +
          a.m[i * 4 + 1] * b.m[1 * 4 + j] +
          a.m[i * 4 + 2] * b.m[2 * 4 + j] +
          a.m[i * 4 + 3] * b.m[3 * 4 + j];
    }
  }
  return r;
}

std::vector<uint8_t> BuildInstanceVisibility(
    size_t instance_count,
    const std::vector<int64_t>& ids,
    const std::vector<int64_t>& invisible_ids,
    const std::vector<int64_t>& inactive_ids) {
  std::vector<uint8_t> visible(instance_count, uint8_t{1});
  if (instance_count == 0) return visible;

  std::unordered_set<int64_t> hidden;
  hidden.reserve(invisible_ids.size() + inactive_ids.size());
  hidden.insert(invisible_ids.begin(), invisible_ids.end());
  hidden.insert(inactive_ids.begin(), inactive_ids.end());
  if (hidden.empty()) return visible;

  if (ids.size() == instance_count) {
    for (size_t i = 0; i < ids.size(); ++i) {
      if (hidden.find(ids[i]) != hidden.end()) {
        visible[i] = 0;
      }
    }
    return visible;
  }

  for (int64_t id : hidden) {
    if (id >= 0 && static_cast<size_t>(id) < instance_count) {
      visible[static_cast<size_t>(id)] = 0;
    }
  }
  return visible;
}

uint32_t PackSnorm8Quaternion(const float* q) {
  const float identity[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  if (!q) q = identity;
  uint32_t packed = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    const float component = std::max(-1.0f, std::min(1.0f, q[i]));
    const int32_t quantized = static_cast<int32_t>(std::lround(component * 127.0f));
    packed |= (static_cast<uint32_t>(static_cast<uint8_t>(quantized))
               << (i * 8));
  }
  return packed;
}

void BuildCompactInstances(
    const PointInstancerData& data, const std::vector<uint8_t>& visible,
    std::vector<RenderPointInstancer::CompactInstance>* out) {
  if (!out) return;
  out->clear();
  out->reserve(data.proto_indices.size());
  for (size_t i = 0; i < data.proto_indices.size(); ++i) {
    RenderPointInstancer::CompactInstance instance{};
    const size_t position_offset = i * 3;
    if (position_offset + 3 <= data.positions.size()) {
      for (size_t j = 0; j < 3; ++j) {
        instance.position[j] = data.positions[position_offset + j];
      }
    }
    const size_t orientation_offset = i * 4;
    const float* orientation =
        orientation_offset + 4 <= data.orientations.size()
            ? data.orientations.data() + orientation_offset
            : nullptr;
    instance.packed_orientation = PackSnorm8Quaternion(orientation);
    const size_t scale_offset = i * 3;
    for (size_t j = 0; j < 3; ++j) {
      const float scale = scale_offset + j < data.scales.size()
                              ? data.scales[scale_offset + j]
                              : 1.0f;
      instance.scale[j] = tangent_quantize::float_to_half(scale);
    }
    instance.flags =
        (i >= visible.size() || visible[i] != 0) ? uint16_t{1} : uint16_t{0};
    instance.prototype_index = data.proto_indices[i];
    instance.source_index = static_cast<uint32_t>(i);
    out->push_back(instance);
  }
}

template <typename Chunked>
void CopyChunkedArray(const Chunked& src, Chunked* dst) {
  if (!dst) return;
  dst->reserve(src.size());
  // Chunk-at-a-time, not element-at-a-time: per-element push_back costs an
  // ensure_capacity() call plus a chunk-pointer indirection for EVERY element,
  // where append() memcpys a whole 64KB chunk. This is the copy engine behind
  // every point-instance mesh clone.
  for (size_t c = 0; c < src.chunk_count(); ++c) {
    const size_t n = src.chunk_size(c);
    if (n == 0) break;
    dst->append(src.chunk_data(c), n);
  }
}

Float3 TransformPoint(const Matrix4& m, float x, float y, float z) {
  return Float3(
      x * m.m[0] + y * m.m[4] + z * m.m[8] + m.m[12],
      x * m.m[1] + y * m.m[5] + z * m.m[9] + m.m[13],
      x * m.m[2] + y * m.m[6] + z * m.m[10] + m.m[14]);
}

Float3 TransformDirection(const Matrix4& m, float x, float y, float z) {
  Float3 d(
      x * m.m[0] + y * m.m[4] + z * m.m[8],
      x * m.m[1] + y * m.m[5] + z * m.m[9],
      x * m.m[2] + y * m.m[6] + z * m.m[10]);
  const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  if (len > 1.0e-8f) {
    d.x /= len;
    d.y /= len;
    d.z /= len;
  }
  return d;
}

void CopyVertexAttribute(const VertexAttribute& src, VertexAttribute* dst) {
  if (!dst) return;
  dst->name = src.name;
  dst->format = src.format;
  dst->interpolation = src.interpolation;
  CopyChunkedArray(src.float_data, &dst->float_data);
  CopyChunkedArray(src.int_data, &dst->int_data);
  CopyChunkedArray(src.uint_data, &dst->uint_data);
  CopyChunkedArray(src.indices, &dst->indices);
}

void CopyRenderMeshCommon(const RenderMesh& src, RenderMesh* dst) {
  if (!dst) return;
  CopyChunkedArray(src.face_vertex_counts, &dst->face_vertex_counts);
  CopyChunkedArray(src.face_vertex_indices, &dst->face_vertex_indices);
  CopyChunkedArray(src.texcoords_0, &dst->texcoords_0);
  CopyChunkedArray(src.texcoords_1, &dst->texcoords_1);
  CopyChunkedArray(src.colors, &dst->colors);
  CopyChunkedArray(src.opacities, &dst->opacities);
  CopyChunkedArray(src.triangulated_indices, &dst->triangulated_indices);
  CopyChunkedArray(src.triangulated_face_vertex_indices,
                   &dst->triangulated_face_vertex_indices);
  dst->normals_interp = src.normals_interp;
  dst->tangents_interp = src.tangents_interp;
  dst->texcoords_0_interp = src.texcoords_0_interp;
  dst->texcoords_1_interp = src.texcoords_1_interp;
  dst->colors_interp = src.colors_interp;
  dst->opacities_interp = src.opacities_interp;
  dst->sanitize_dropped_faces = src.sanitize_dropped_faces;
  dst->sanitize_face_remap = src.sanitize_face_remap;
  dst->material_id = src.material_id;
  dst->material_subsets = src.material_subsets;
  dst->is_triangulated = src.is_triangulated;
  dst->hole_faces = src.hole_faces;
  dst->left_handed = src.left_handed;
  dst->bbox_min = src.bbox_min;
  dst->bbox_max = src.bbox_max;
  dst->has_bbox = src.has_bbox;

  dst->primvars.reserve(src.primvars.size());
  for (const VertexAttribute& pv : src.primvars) {
    VertexAttribute copy;
    CopyVertexAttribute(pv, &copy);
    dst->primvars.push_back(std::move(copy));
  }

  if (src.skin) {
    dst->skin = std::make_unique<RenderMesh::SkinBinding>();
    CopyChunkedArray(src.skin->joint_indices, &dst->skin->joint_indices);
    CopyChunkedArray(src.skin->joint_weights, &dst->skin->joint_weights);
    dst->skin->influences_per_vertex = src.skin->influences_per_vertex;
    dst->skin->skeleton_id = src.skin->skeleton_id;
    dst->skin->skeleton_path = src.skin->skeleton_path;
    dst->skin->geom_bind_transform = src.skin->geom_bind_transform;
  }

  dst->blend_shapes.reserve(src.blend_shapes.size());
  for (const RenderMesh::BlendShape& bs : src.blend_shapes) {
    RenderMesh::BlendShape copy;
    copy.name = bs.name;
    CopyChunkedArray(bs.point_offsets, &copy.point_offsets);
    CopyChunkedArray(bs.normal_offsets, &copy.normal_offsets);
    copy.point_indices = bs.point_indices;
    copy.weight = bs.weight;
    copy.inbetweens.reserve(bs.inbetweens.size());
    for (const RenderMesh::BlendShape::Inbetween& source : bs.inbetweens) {
      RenderMesh::BlendShape::Inbetween inbetween;
      inbetween.name = source.name;
      inbetween.weight = source.weight;
      CopyChunkedArray(source.point_offsets, &inbetween.point_offsets);
      copy.inbetweens.push_back(std::move(inbetween));
    }
    dst->blend_shapes.push_back(std::move(copy));
  }
}

bool CloneMeshForPointInstance(const RenderMesh& src,
                               const RenderPointInstanceDraw& draw,
                               RenderMesh* dst) {
  if (!dst) return false;
  dst->name = src.name + "_pointInstance_" + std::to_string(draw.instance_index);
  dst->prim_path = src.prim_path + ".pointInstance[" +
                   std::to_string(draw.instance_index) + "]";
  CopyRenderMeshCommon(src, dst);

  dst->points.reserve(src.points.size());
  for (size_t i = 0; i + 2 < src.points.size(); i += 3) {
    const Float3 p = TransformPoint(draw.transform, src.points[i],
                                    src.points[i + 1], src.points[i + 2]);
    dst->points.push_back(p.x);
    dst->points.push_back(p.y);
    dst->points.push_back(p.z);
  }

  dst->normals.reserve(src.normals.size());
  for (size_t i = 0; i + 2 < src.normals.size(); i += 3) {
    const Float3 n = TransformDirection(draw.transform, src.normals[i],
                                        src.normals[i + 1], src.normals[i + 2]);
    dst->normals.push_back(n.x);
    dst->normals.push_back(n.y);
    dst->normals.push_back(n.z);
  }

  dst->tangents.reserve(src.tangents.size());
  for (size_t i = 0; i + 3 < src.tangents.size(); i += 4) {
    const Float3 t = TransformDirection(draw.transform, src.tangents[i],
                                        src.tangents[i + 1], src.tangents[i + 2]);
    dst->tangents.push_back(t.x);
    dst->tangents.push_back(t.y);
    dst->tangents.push_back(t.z);
    dst->tangents.push_back(src.tangents[i + 3]);
  }

  if (dst->point_count() > 0) {
    dst->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    dst->bbox_max = Float3(-1e30f, -1e30f, -1e30f);
    for (size_t i = 0; i + 2 < dst->points.size(); i += 3) {
      dst->bbox_min.x = std::min(dst->bbox_min.x, dst->points[i]);
      dst->bbox_min.y = std::min(dst->bbox_min.y, dst->points[i + 1]);
      dst->bbox_min.z = std::min(dst->bbox_min.z, dst->points[i + 2]);
      dst->bbox_max.x = std::max(dst->bbox_max.x, dst->points[i]);
      dst->bbox_max.y = std::max(dst->bbox_max.y, dst->points[i + 1]);
      dst->bbox_max.z = std::max(dst->bbox_max.z, dst->points[i + 2]);
    }
    dst->has_bbox = true;
  }
  return dst->point_count() == src.point_count();
}

void CollectMeshIdsUnderNode(const RenderScene& scene,
                             int32_t node_id,
                             const Matrix4& parent_relative,
                             std::vector<int32_t>* out_ids,
                             std::vector<Matrix4>* out_transforms) {
  if (!out_ids || !out_transforms || node_id < 0 ||
      static_cast<size_t>(node_id) >= scene.nodes.size()) {
    return;
  }

  const SceneNode& node = scene.nodes[static_cast<size_t>(node_id)];
  const Matrix4 relative = MulMatrix4(node.local_transform, parent_relative);
  if (node.type == NodeType::Mesh && node.data_id >= 0) {
    out_ids->push_back(node.data_id);
    out_transforms->push_back(relative);
  }

  for (int32_t child_id : node.children) {
    CollectMeshIdsUnderNode(scene, child_id, relative, out_ids, out_transforms);
  }
}

void ResolvePointInstancerPrototypeBindings(RenderScene* scene,
                                            RenderPointInstancer* instancer) {
  if (!scene || !instancer) return;

  instancer->prototype_node_ids.clear();
  instancer->prototype_mesh_offsets.clear();
  instancer->prototype_mesh_ids.clear();
  instancer->prototype_mesh_transforms.clear();
  instancer->prototype_node_ids.reserve(instancer->prototype_paths.size());
  instancer->prototype_mesh_offsets.reserve(instancer->prototype_paths.size() + 1);
  instancer->prototype_mesh_offsets.push_back(0);

  for (const std::string& path : instancer->prototype_paths) {
    int32_t node_id = -1;
    const auto node_it = scene->node_by_path.find(path);
    if (node_it != scene->node_by_path.end()) {
      node_id = node_it->second;
    }
    instancer->prototype_node_ids.push_back(node_id);
    CollectMeshIdsUnderNode(*scene, node_id, Matrix4::Identity(),
                            &instancer->prototype_mesh_ids,
                            &instancer->prototype_mesh_transforms);
    instancer->prototype_mesh_offsets.push_back(
        static_cast<uint32_t>(instancer->prototype_mesh_ids.size()));
  }
}

void AppendPointInstanceDraws(int32_t instancer_id,
                              RenderPointInstancer* instancer,
                              RenderScene* scene) {
  if (!scene || !instancer || instancer_id < 0) return;
  instancer->draw_start = static_cast<uint32_t>(scene->point_instance_draws.size());
  instancer->draw_count = 0;
  if (!instancer->valid) return;

  const size_t instance_count = instancer->instance_count();
  for (size_t instance_index = 0; instance_index < instance_count; ++instance_index) {
    if (!instancer->instance_visible.empty() &&
        !instancer->instance_visible[instance_index]) {
      continue;
    }
    const int32_t proto_index = instancer->proto_indices[instance_index];
    if (proto_index < 0 ||
        static_cast<size_t>(proto_index + 1) >=
            instancer->prototype_mesh_offsets.size()) {
      continue;
    }

    const uint32_t begin =
        instancer->prototype_mesh_offsets[static_cast<size_t>(proto_index)];
    const uint32_t end =
        instancer->prototype_mesh_offsets[static_cast<size_t>(proto_index) + 1];
    for (uint32_t mesh_ref = begin; mesh_ref < end; ++mesh_ref) {
      if (mesh_ref >= instancer->prototype_mesh_ids.size()) continue;
      const int32_t mesh_id = instancer->prototype_mesh_ids[mesh_ref];
      if (mesh_id < 0) continue;

      RenderPointInstanceDraw draw;
      draw.point_instancer_id = instancer_id;
      draw.instance_index = static_cast<uint32_t>(instance_index);
      draw.prototype_index = static_cast<uint32_t>(proto_index);
      draw.mesh_id = mesh_id;
      if (static_cast<size_t>(mesh_id) < scene->meshes.size()) {
        draw.material_id = scene->meshes[static_cast<size_t>(mesh_id)].material_id;
      }
      Matrix4 instance_transform = Matrix4::Identity();
      if (instance_index < instancer->transforms.size()) {
        instance_transform = instancer->transforms[instance_index];
      }
      if (mesh_ref < instancer->prototype_mesh_transforms.size()) {
        draw.transform = MulMatrix4(instancer->prototype_mesh_transforms[mesh_ref],
                                    instance_transform);
      } else {
        draw.transform = instance_transform;
      }
      scene->point_instance_draws.push_back(draw);
      ++instancer->draw_count;
    }
  }
}

std::string LeafNameFromJointPath(const std::string& path) {
  size_t pos = path.rfind('/');
  if (pos == std::string::npos) return path;
  if (pos + 1 >= path.size()) return "";
  return path.substr(pos + 1);
}

bool LocalVisibility(const UsdPrim& prim) {
  const Value* value = prim.GetPropertyValue("visibility");
  if (!value) return true;
  if (const std::string* token = value->as_token()) {
    return *token != "invisible";
  }
  if (const std::string* str = value->as_string()) {
    return *str != "invisible";
  }
  return true;
}

AnimationChannel::TargetPath TargetPathForXformOp(const std::string& prop_name) {
  if (prop_name.find("translate") != std::string::npos) {
    return AnimationChannel::TargetPath::Translation;
  }
  if (prop_name.find("scale") != std::string::npos) {
    return AnimationChannel::TargetPath::Scale;
  }
  return AnimationChannel::TargetPath::Rotation;
}

bool IsXformAnimationProperty(const std::string& prop_name) {
  if (prop_name.find("xformOp:") != 0) return false;
  return prop_name.find("translate") != std::string::npos ||
         prop_name.find("scale") != std::string::npos ||
         prop_name.find("rotate") != std::string::npos ||
         prop_name.find("orient") != std::string::npos;
}

// Value-clip metadata parsing / active-clip selection / stage->clip time
// mapping now come from the CORE resolver (next/eval/value-clip.hh:
// ParseValueClipSets + ResolveValueClipFromSets) — Tydra previously owned a
// duplicate of these semantics that had drifted (no jump-discontinuity
// handling, stale out-of-range mapping, no clipSets ordering edits, no
// manifest gating, no nested-clip recursion).

// Bake-only helper: the stage times at which the bake samples a clip set
// (authored times/active knots + a uniform fill up to max_samples).
std::vector<double> ValueClipSampleTimes(
    const Stage& stage, const ::tinyusdz::next::ValueClipSet& meta,
    uint32_t max_samples) {
  std::set<double> exact;
  for (const auto& value : meta.times) exact.insert(value.first);
  for (const auto& value : meta.active) exact.insert(value.first);
  const ::tinyusdz::next::StageMeta stage_meta = stage.GetMeta();
  double start = exact.empty() ? 0.0 : *exact.begin();
  double end = exact.empty() ? start : *exact.rbegin();
  if (stage_meta.startTimeCode_set) start = stage_meta.startTimeCode;
  if (stage_meta.endTimeCode_set) end = stage_meta.endTimeCode;
  if (end < start) std::swap(start, end);
  exact.insert(start);
  exact.insert(end);

  const uint32_t limit = std::max<uint32_t>(2, max_samples);
  const double span = end - start;
  double step = 1.0;
  if (span > static_cast<double>(limit - 1)) {
    step = span / static_cast<double>(limit - 1);
  }
  for (double t = start; t <= end + step * 0.25; t += step) {
    exact.insert(std::min(t, end));
    if (exact.size() >= limit + meta.times.size() + meta.active.size()) break;
  }
  return std::vector<double>(exact.begin(), exact.end());
}

struct TextureNodeData {
  std::string file;
  // `inputs:file` customData { asset ktx2 = @...@ } — the legacy-safe compressed
  // companion hint (doc/texcomp.md). Empty when unauthored.
  std::string ktx2_hint;
  std::string wrap_s = "useMetadata";
  std::string wrap_t = "useMetadata";
  float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float bias[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  std::string source_color_space = "auto";
  // From the inputs:st chain (UsdTransform2d -> UsdPrimvarReader_float2):
  std::string uv_primvar;               // varname of the primvar reader
  float uv_translation[2] = {0.0f, 0.0f};
  float uv_rotation = 0.0f;             // degrees (UsdTransform2d convention)
  float uv_scale[2] = {1.0f, 1.0f};
};

// Trace a UsdUVTexture's inputs:st connection chain: UsdTransform2d nodes
// accumulate the UV transform (chained via their inputs:in); a
// UsdPrimvarReader_* terminates the chain and names the UV set.
static bool GetFloat2Local(const UsdPrim& prim, const std::string& name,
                           float* out2) {
  const ::tinyusdz::next::Value* v = GetAttribute(prim, name);
  if (!v) return false;
  const float* f = v->as_float2();
  if (!f) return false;
  out2[0] = f[0];
  out2[1] = f[1];
  return true;
}

void TraceTextureStChain(const Stage& stage, const UsdPrim& texture_prim,
                         TextureNodeData* out) {
  UsdPrim cur = texture_prim;
  std::string prop = "inputs:st";

  // Chained UsdTransform2d nodes COMPOSE (each node applies
  // uv' = R(rotation) * (scale * uv) + translation to its input). Accumulate
  // the affine composition walking from the texture (outermost) towards the
  // primvar reader (innermost): F_total(uv) = F_outer(F_inner(uv)).
  // A single node keeps its authored values verbatim (no decomposition).
  // Note: animated or connected translation/rotation/scale inputs are NOT
  // evaluated here (only directly-authored values are read).
  int transform_nodes = 0;
  double A[2][2] = {{1.0, 0.0}, {0.0, 1.0}};  // accumulated linear part
  double T[2] = {0.0, 0.0};                   // accumulated offset

  for (int hop = 0; hop < 4 && cur.IsValid(); ++hop) {
    const ::tinyusdz::next::PrimSpec* spec = cur.GetPrimSpec();
    const std::vector<::tinyusdz::next::Path>* conns =
        spec ? spec->connection(prop) : nullptr;
    if (!conns || conns->empty()) break;
    const std::string next_path = SourcePrimPathFromConnection((*conns)[0].str());
    UsdPrim np = stage.GetPrimAtPath(next_path);
    if (!np.IsValid()) break;
    std::string id;
    GetToken(np, "info:id", &id);
    if (id == "UsdTransform2d") {
      float tr[2] = {0.0f, 0.0f};
      GetFloat2Local(np, "inputs:translation", tr);
      float rot = 0.0f;
      GetFloat(np, "inputs:rotation", &rot);
      float sc[2] = {1.0f, 1.0f};
      GetFloat2Local(np, "inputs:scale", sc);

      if (transform_nodes == 0) {
        // First (outermost) node: keep the raw authored values so a single
        // Transform2d round-trips exactly.
        out->uv_translation[0] = tr[0];
        out->uv_translation[1] = tr[1];
        out->uv_rotation = rot;
        out->uv_scale[0] = sc[0];
        out->uv_scale[1] = sc[1];
      }

      // Local affine L(uv) = R * diag(sc) * uv + tr.
      const double rad = double(rot) * 3.14159265358979323846 / 180.0;
      const double c = std::cos(rad), s = std::sin(rad);
      const double L[2][2] = {{c * sc[0], -s * sc[1]},
                              {s * sc[0], c * sc[1]}};
      // Accumulated-so-far F is applied AFTER this (deeper) node:
      // F_new(uv) = F(L(uv)) => A_new = A*L, T_new = A*t_L + T.
      const double A00 = A[0][0] * L[0][0] + A[0][1] * L[1][0];
      const double A01 = A[0][0] * L[0][1] + A[0][1] * L[1][1];
      const double A10 = A[1][0] * L[0][0] + A[1][1] * L[1][0];
      const double A11 = A[1][0] * L[0][1] + A[1][1] * L[1][1];
      T[0] += A[0][0] * tr[0] + A[0][1] * tr[1];
      T[1] += A[1][0] * tr[0] + A[1][1] * tr[1];
      A[0][0] = A00; A[0][1] = A01; A[1][0] = A10; A[1][1] = A11;

      ++transform_nodes;
      cur = np;
      prop = "inputs:in";
      continue;
    }
    if (id.rfind("UsdPrimvarReader", 0) == 0) {
      out->uv_primvar = ::tinyusdz::next::GetPrimvarReaderVarname(stage, np);
      break;
    }
    break;
  }

  if (transform_nodes > 1) {
    // Decompose the composite affine back into the translate/rotate/scale
    // model RenderTexture carries (uv' = R * diag(scale) * uv + offset).
    // Composition of non-uniform scales and rotations can introduce shear,
    // which this model cannot represent; the QR-style decomposition below
    // drops it (best effort).
    out->uv_translation[0] = static_cast<float>(T[0]);
    out->uv_translation[1] = static_cast<float>(T[1]);
    const double sx = std::sqrt(A[0][0] * A[0][0] + A[1][0] * A[1][0]);
    if (sx > 1.0e-12) {
      const double c = A[0][0] / sx;
      const double s = A[1][0] / sx;
      const double sy = -s * A[0][1] + c * A[1][1];
      out->uv_rotation = static_cast<float>(
          std::atan2(s, c) * 180.0 / 3.14159265358979323846);
      out->uv_scale[0] = static_cast<float>(sx);
      out->uv_scale[1] = static_cast<float>(sy);
    } else {
      out->uv_rotation = 0.0f;
      out->uv_scale[0] = static_cast<float>(A[0][0]);
      out->uv_scale[1] = static_cast<float>(A[1][1]);
    }
  }
}

bool ExtractTextureNodeData(const Stage& stage,
                            const UsdPrim& texture_prim,
                            double time_code,
                            TextureNodeData* out) {
  if (!out || !texture_prim.IsValid()) return false;

  // Apply metadata from the property that actually authors the asset. Ptex
  // networks frequently forward a shader input through a material interface,
  // e.g. `inputs:file.connect = </Mat.inputs:surfaceMap>`, so that property is
  // not necessarily on the texture shader itself.
  auto apply_file_metadata = [out](const UsdPrim& owner,
                                   const std::string& property) {
    const ::tinyusdz::next::PropMeta* pm = owner.GetPropertyMeta(property);
    if (!pm) return;
    if (!pm->colorSpace.empty()) out->source_color_space = pm->colorSpace;
    if (const ::tinyusdz::next::Dict* cd = pm->customData.as_dictionary()) {
      if (const ::tinyusdz::next::Value* v = cd->find("ktx2")) {
        if (const std::string* s = v->as_asset_path()) {
          out->ktx2_hint = *s;
        } else if (const std::string* t = v->as_string()) {
          out->ktx2_hint = *t;
        }
      }
    }
  };

  ::tinyusdz::next::UVTextureData uv;
  if (::tinyusdz::next::GetUVTextureData(stage, texture_prim, &uv, time_code)) {
    out->file = uv.file;
    out->wrap_s = uv.wrap_s;
    out->wrap_t = uv.wrap_t;
    out->source_color_space = uv.source_color_space;
    std::memcpy(out->scale, uv.scale, sizeof(out->scale));
    std::memcpy(out->bias, uv.bias, sizeof(out->bias));
    apply_file_metadata(texture_prim, "inputs:file");
    TraceTextureStChain(stage, texture_prim, out);
    return !out->file.empty();
  }

  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);

  UsdPrim fileOwner;
  std::string fileAttribute;
  auto resolve_asset_input = [&](const std::string& initial,
                                 UsdPrim* resolved_owner,
                                 std::string* resolved_property)
      -> std::optional<std::string> {
    UsdPrim owner = texture_prim;
    std::string property = initial;
    std::set<std::string> visited;
    for (int depth = 0; depth <= kMaxMtlxConstantDepth && owner.IsValid();
         ++depth) {
      const std::string visit_key = owner.GetPath().str() + "." + property;
      if (!visited.insert(visit_key).second) break;

      std::optional<std::string> value = eval.EvalAssetPath(owner, property);
      if (!value) value = eval.EvalString(owner, property);
      if (value && !value->empty()) {
        *resolved_owner = owner;
        *resolved_property = property;
        return value;
      }
      if (!eval.HasConnection(owner, property)) break;

      std::string prim_path;
      std::string next_property;
      if (!SplitConnectionPath(eval.GetConnectionPath(owner, property),
                               &prim_path, &next_property)) {
        break;
      }
      owner = stage.GetPrimAtPath(prim_path);
      property = std::move(next_property);
    }
    return std::nullopt;
  };

  std::optional<std::string> file =
      resolve_asset_input("inputs:file", &fileOwner, &fileAttribute);
  if (!file) {
    file = resolve_asset_input("inputs:filename", &fileOwner, &fileAttribute);
  }
  if (!file || file->empty()) return false;
  out->file = *file;

  if (std::optional<std::string> wrap_s = eval.EvalToken(texture_prim, "inputs:wrapS")) {
    out->wrap_s = *wrap_s;
  }
  if (std::optional<std::string> wrap_t = eval.EvalToken(texture_prim, "inputs:wrapT")) {
    out->wrap_t = *wrap_t;
  }
  float scale[4];
  if (eval.EvalFloat4(texture_prim, "inputs:scale", scale)) {
    std::memcpy(out->scale, scale, sizeof(out->scale));
  }
  float bias[4];
  if (eval.EvalFloat4(texture_prim, "inputs:bias", bias)) {
    std::memcpy(out->bias, bias, sizeof(out->bias));
  }
  if (std::optional<std::string> cs = eval.EvalToken(texture_prim, "inputs:sourceColorSpace")) {
    out->source_color_space = *cs;
  }
  apply_file_metadata(fileOwner, fileAttribute);
  TraceTextureStChain(stage, texture_prim, out);

  return true;
}

// MaterialX Autodesk standard_surface (usdMtlx flatten pattern).
bool IsStandardSurfaceShaderId(const std::string& id) {
  return id == "ND_standard_surface_surfaceshader" ||
         id == "standard_surface" ||
         id == "AutodeskStandardSurface" ||
         id == "MtlxAutodeskStandardSurface";
}

bool IsOpenPBRShaderId(const std::string& id) {
  return id == "ND_open_pbr_surface_surfaceshader" ||
         id == "open_pbr_surface" ||
         id == "OpenPBRSurface";
}

bool IsPhysicsExtensionPropertyName(const std::string& name) {
  return name.rfind("mjc:", 0) == 0 ||
         name.rfind("newton:", 0) == 0 ||
         name.rfind("physx", 0) == 0 ||
         name.rfind("state:", 0) == 0;
}

void ComputePointBounds(const FloatChunked& points, Float3* bbox_min,
                        Float3* bbox_max, bool* has_bbox) {
  if (!bbox_min || !bbox_max || !has_bbox) return;
  *has_bbox = false;
  if (points.size() < 3) return;
  *bbox_min = Float3(1e30f, 1e30f, 1e30f);
  *bbox_max = Float3(-1e30f, -1e30f, -1e30f);
  const size_t point_count = points.size() / 3;
  for (size_t i = 0; i < point_count; ++i) {
    const float x = points[i * 3 + 0];
    const float y = points[i * 3 + 1];
    const float z = points[i * 3 + 2];
    bbox_min->x = std::min(bbox_min->x, x);
    bbox_min->y = std::min(bbox_min->y, y);
    bbox_min->z = std::min(bbox_min->z, z);
    bbox_max->x = std::max(bbox_max->x, x);
    bbox_max->y = std::max(bbox_max->y, y);
    bbox_max->z = std::max(bbox_max->z, z);
  }
  *has_bbox = true;
}

std::string ValueSummary(const Value& value) {
  if (const bool* b = value.as_bool()) return *b ? "true" : "false";
  if (const int32_t* i = value.as_int()) return std::to_string(*i);
  if (const int64_t* i = value.as_int64()) return std::to_string(*i);
  if (const float* f = value.as_float()) return std::to_string(*f);
  if (const double* d = value.as_double()) return std::to_string(*d);
  if (const std::string* s = value.as_string()) return *s;
  if (const std::string* s = value.as_token()) return *s;
  if (const std::string* s = value.as_asset_path()) return *s;
  if (const float* v = value.as_float3()) {
    return std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
           std::to_string(v[2]);
  }
  if (const float* v = value.as_float4()) {
    return std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
           std::to_string(v[2]) + "," + std::to_string(v[3]);
  }
  if (const std::vector<float>* arr = value.as_float_array()) {
    return "float[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<double>* arr = value.as_double_array()) {
    return "double[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<int32_t>* arr = value.as_int_array()) {
    return "int[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<int64_t>* arr = value.as_int64_array()) {
    return "int64[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<std::string>* arr = value.as_token_array()) {
    return "token[" + std::to_string(arr->size()) + "]";
  }
  const char* type_name = ::tinyusdz::next::GetTypeName(value.type_id());
  return type_name ? type_name : "value";
}

bool ReadStringLikeProperty(const UsdPrim& prim, const std::string& name,
                            std::string* out) {
  if (!out) return false;
  if (GetString(prim, name, out) || GetToken(prim, name, out)) return true;
  const Value* v = GetAttribute(prim, name);
  if (!v) return false;
  if (const std::string* ap = v->as_asset_path()) {
    *out = *ap;
    return true;
  }
  return false;
}

std::vector<std::string> ReadTokenArrayProperty(const UsdPrim& prim,
                                                const std::string& name) {
  std::vector<std::string> out;
  const Value* value = prim.GetPropertyValue(name);
  if (!value) return out;
  if (const std::vector<std::string>* arr = value->as_token_array()) {
    return *arr;
  }
  if (const std::string* tok = value->as_token()) {
    out.push_back(*tok);
  } else if (const std::string* str = value->as_string()) {
    out.push_back(*str);
  }
  return out;
}

bool FirstArrayElementToFloat4(const std::vector<float>& values,
                               uint32_t stride,
                               Float4* out) {
  if (!out || values.empty() || stride == 0) return false;
  const float x = values.size() > 0 ? values[0] : 0.0f;
  const float y = values.size() > 1 ? values[1] : 0.0f;
  const float z = values.size() > 2 ? values[2] : 0.0f;
  const float w = values.size() > 3 ? values[3] : 0.0f;
  if (stride == 1) {
    *out = Float4(x, 0.0f, 0.0f, 0.0f);
  } else if (stride == 3) {
    *out = Float4(x, y, z, 0.0f);
  } else {
    *out = Float4(x, y, z, w);
  }
  return true;
}

bool JointTokenMatches(const SkeletonJoint& joint, const std::string& token) {
  if (token.empty()) return false;
  if (joint.path == token || joint.name == token) return true;
  if (LeafNameFromJointPath(joint.path) == token) return true;
  if (joint.path.size() > token.size() &&
      joint.path.compare(joint.path.size() - token.size(), token.size(),
                         token) == 0) {
    const size_t sep = joint.path.size() - token.size();
    return sep == 0 || joint.path[sep - 1] == '/';
  }
  return false;
}

using JointIndexLookup = std::unordered_map<std::string, int32_t>;

JointIndexLookup BuildJointIndexLookup(const Skeleton& skeleton) {
  JointIndexLookup lookup;
  lookup.reserve(skeleton.joints.size() * 3);
  for (size_t i = 0; i < skeleton.joints.size(); ++i) {
    const SkeletonJoint& joint = skeleton.joints[i];
    const int32_t index = static_cast<int32_t>(i);
    lookup.emplace(joint.path, index);
    lookup.emplace(joint.name, index);
    lookup.emplace(LeafNameFromJointPath(joint.path), index);
  }
  return lookup;
}

int32_t FindJointIndex(const Skeleton& skeleton,
                       const JointIndexLookup& lookup,
                       const std::string& token) {
  const auto exact = lookup.find(token);
  if (exact != lookup.end()) return exact->second;
  // Preserve support for relative multi-segment tokens. This uncommon path
  // keeps the original matching semantics without penalizing exact paths and
  // leaf names used by ordinary UsdSkel exports.
  for (size_t i = 0; i < skeleton.joints.size(); ++i) {
    if (JointTokenMatches(skeleton.joints[i], token)) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

// Material-driven UV primvar promotion: a UsdPrimvarReader varname that the
// mesh's own UV-set selection (MeshConfig::uv_primvar_names) did not pick only
// survives as a generic mesh.primvars entry, which no texture consumer samples.
// After materials are bound, promote the primvar each bound material's textures
// actually reference into texcoords_0/1 (legacy selects UV sets from the shader
// network the same way).
void PromoteMaterialUVPrimvars(RenderScene* scene,
                               std::vector<std::string>* warnings) {
  if (!scene) return;

  auto texture_uv_names = [scene](const RenderMaterial& mat,
                                  std::vector<std::string>* names) {
    auto add = [scene, names](const ShaderParam& p) {
      if (p.texture_id < 0 ||
          static_cast<size_t>(p.texture_id) >= scene->textures.size()) {
        return;
      }
      const std::string& uv =
          scene->textures[static_cast<size_t>(p.texture_id)].uv_primvar;
      if (uv.empty()) return;
      if (std::find(names->begin(), names->end(), uv) == names->end()) {
        names->push_back(uv);
      }
    };
    if (mat.preview_surface) {
      const PreviewSurfaceShader& ps = *mat.preview_surface;
      for (const ShaderParam* p :
           {&ps.diffuse_color, &ps.emissive_color, &ps.specular_color,
            &ps.metallic, &ps.roughness, &ps.clearcoat,
            &ps.clearcoat_roughness, &ps.opacity, &ps.opacity_threshold,
            &ps.ior, &ps.normal, &ps.displacement, &ps.occlusion}) {
        add(*p);
      }
    }
    if (mat.openpbr) {
      const OpenPBRSurfaceShader& o = *mat.openpbr;
      for (const ShaderParam* p :
           {&o.base_weight, &o.base_color, &o.base_roughness,
            &o.base_metalness, &o.specular_weight, &o.specular_color,
            &o.specular_roughness, &o.specular_ior, &o.specular_anisotropy,
            &o.specular_roughness_anisotropy, &o.transmission_weight,
            &o.transmission_dispersion, &o.transmission_dispersion_scale,
            &o.coat_weight, &o.coat_color, &o.coat_roughness,
            &o.coat_anisotropy, &o.coat_roughness_anisotropy,
            &o.coat_normal,
            &o.thin_film_weight, &o.thin_film_thickness, &o.thin_film_ior,
            &o.emission_luminance, &o.emission_color, &o.normal,
            &o.opacity, &o.displacement}) {
        add(*p);
      }
    }
  };

  for (RenderMesh& mesh : scene->meshes) {
    // Gather UV names referenced by every material bound to this mesh
    // (direct binding + subsets).
    std::vector<int32_t> material_ids;
    if (mesh.material_id >= 0) material_ids.push_back(mesh.material_id);
    for (const RenderMesh::MaterialSubset& subset : mesh.material_subsets) {
      if (subset.material_id >= 0) material_ids.push_back(subset.material_id);
    }
    std::vector<std::string> wanted;
    for (int32_t mid : material_ids) {
      if (static_cast<size_t>(mid) >= scene->materials.size()) continue;
      texture_uv_names(scene->materials[static_cast<size_t>(mid)], &wanted);
    }
    if (wanted.empty()) continue;

    auto take_primvar = [&mesh, warnings](const std::string& name,
                                         FloatChunked* value,
                                         Interpolation* interp) -> bool {
      for (size_t ai = 0; ai < mesh.primvars.size(); ++ai) {
        VertexAttribute& attr = mesh.primvars[ai];
        if (attr.name != name || attr.format != VertexFormat::Vec2) continue;
        FloatChunked promoted;
        if (attr.has_indices()) {
          const size_t elems = attr.float_data.size() / 2;
          for (size_t k = 0; k < attr.indices.size(); ++k) {
            const uint32_t idx = attr.indices[k];
            if (idx >= elems) {
              warnings->push_back("Mesh '" + mesh.prim_path + "': UV primvar '" +
                                  name + "' has out-of-range indices; not promoted");
              return false;
            }
            promoted.push_back(attr.float_data[idx * 2 + 0]);
            promoted.push_back(attr.float_data[idx * 2 + 1]);
          }
        } else {
          for (size_t k = 0; k < attr.float_data.size(); ++k) {
            promoted.push_back(attr.float_data[k]);
          }
        }
        if (promoted.alloc_failed()) {
          warnings->push_back("Mesh '" + mesh.prim_path + "': UV primvar '" +
                              name + "' allocation failed; not promoted");
          return false;
        }
        *value = std::move(promoted);
        *interp = attr.interpolation;
        mesh.primvars.erase(mesh.primvars.begin() +
                            static_cast<std::ptrdiff_t>(ai));
        return true;
      }
      return false;
    };

    auto swap_uv_slots = [&mesh]() {
      std::swap(mesh.texcoords_0, mesh.texcoords_1);
      std::swap(mesh.texcoords_0_interp, mesh.texcoords_1_interp);
      std::swap(mesh.texcoords_0_name, mesh.texcoords_1_name);
    };

    // The first material-referenced UV set must occupy the primary slot
    // (matching legacy's shader-network-driven selection). Mesh extraction may
    // already have selected it as texcoords_1; normalize that case instead of
    // treating either occupied slot as equivalent. If a second referenced set
    // currently occupies slot 0, preserve it by swapping before slot 0 is
    // overwritten by a promoted generic primvar.
    bool primary_ready = false;
    if (mesh.texcoords_0_name == wanted[0]) {
      primary_ready = true;
    } else if (mesh.texcoords_1_name == wanted[0]) {
      swap_uv_slots();
      primary_ready = true;
    } else {
      FloatChunked primary;
      Interpolation primary_interp = Interpolation::Vertex;
      if (take_primvar(wanted[0], &primary, &primary_interp)) {
        if (wanted.size() > 1 && mesh.texcoords_0_name == wanted[1]) {
          swap_uv_slots();
        }
        mesh.texcoords_0 = std::move(primary);
        mesh.texcoords_0_interp = primary_interp;
        mesh.texcoords_0_name = wanted[0];
        primary_ready = true;
      }
    }

    // Keep the second distinct material-referenced UV set in slot 1. It may
    // already be there after the normalization above or may still be a generic
    // primvar that needs promotion.
    if (primary_ready && wanted.size() > 1 &&
        mesh.texcoords_1_name != wanted[1]) {
      FloatChunked secondary;
      Interpolation secondary_interp = Interpolation::Vertex;
      if (take_primvar(wanted[1], &secondary, &secondary_interp)) {
        mesh.texcoords_1 = std::move(secondary);
        mesh.texcoords_1_interp = secondary_interp;
        mesh.texcoords_1_name = wanted[1];
      }
    }
  }
}

void ResolveSkeletalAnimationTargets(RenderScene* scene) {
  if (!scene) return;
  std::vector<JointIndexLookup> joint_lookups;
  joint_lookups.reserve(scene->skeletons.size());
  for (const Skeleton& skeleton : scene->skeletons) {
    joint_lookups.push_back(BuildJointIndexLookup(skeleton));
  }
  for (size_t ai = 0; ai < scene->animations.size(); ++ai) {
    AnimationClip& clip = scene->animations[ai];
    for (AnimationChannel& channel : clip.channels) {
      if (!channel.is_skeletal) continue;

      int32_t skeleton_id = -1;
      for (size_t si = 0; si < scene->skeletons.size(); ++si) {
        const Skeleton& skel = scene->skeletons[si];
        if (!skel.animation_source_path.empty() &&
            skel.animation_source_path == clip.prim_path) {
          skeleton_id = static_cast<int32_t>(si);
          break;
        }
      }
      if (skeleton_id < 0 && !channel.joint_order.empty()) {
        size_t best_matches = 0;
        for (size_t si = 0; si < scene->skeletons.size(); ++si) {
          const Skeleton& skel = scene->skeletons[si];
          size_t matches = 0;
          for (const std::string& token : channel.joint_order) {
            if (FindJointIndex(skel, joint_lookups[si], token) >= 0) ++matches;
          }
          if (matches > best_matches) {
            best_matches = matches;
            skeleton_id = static_cast<int32_t>(si);
          }
        }
      }

      channel.target_skeleton = skeleton_id;
      channel.joint_remap.clear();
      if (skeleton_id < 0 ||
          static_cast<size_t>(skeleton_id) >= scene->skeletons.size()) {
        continue;
      }
      const Skeleton& skel = scene->skeletons[static_cast<size_t>(skeleton_id)];
      const JointIndexLookup& lookup =
          joint_lookups[static_cast<size_t>(skeleton_id)];
      channel.target_skeleton_path = skel.prim_path;
      channel.joint_remap.reserve(channel.joint_order.size());
      for (const std::string& token : channel.joint_order) {
        channel.joint_remap.push_back(FindJointIndex(skel, lookup, token));
      }
      scene->skeletons[static_cast<size_t>(skeleton_id)].animation_id =
          static_cast<int32_t>(ai);
    }
  }
}

bool PathIsAtOrUnder(const std::string& path, const std::string& root) {
  if (root.empty() || path.empty()) return false;
  if (path == root) return true;
  return path.size() > root.size() &&
         path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}

// Resolve one CollectionAPI instance (collection:<name>:*) on a light prim
// to RenderScene mesh indices, mirroring legacy ResolveLightLinking:
// excludes take hierarchical precedence, includeRoot adds the light prim's
// subtree, explicitOnly matches exact paths, expandPrims (default) and
// expandPrimsAndProperties match descendants. Unauthored collections keep
// *links_all = true (light affects everything); membershipExpression
// collections are not evaluated (no path-expression parser in next) and
// also keep the links-all default.
void ResolveLightLinkInstance(const UsdPrim& prim, const RenderScene& scene,
                              const std::string& instance_name,
                              bool* links_all,
                              std::vector<int32_t>* mesh_indices) {
  const std::string base = "collection:" + instance_name + ":";
  if (prim.HasProperty(base + "membershipExpression")) return;

  const std::vector<::tinyusdz::next::Path>* includes =
      prim.GetRelationship(base + "includes");
  const std::vector<::tinyusdz::next::Path>* excludes =
      prim.GetRelationship(base + "excludes");
  if (!includes && !excludes) return;  // unauthored -> links all

  bool include_root = false;
  GetBool(prim, base + "includeRoot", &include_root);
  std::string rule = "expandPrims";
  GetToken(prim, base + "expansionRule", &rule);
  const bool explicit_only = (rule == "explicitOnly");
  const std::string& owner_path = prim.GetPath().str();

  *links_all = false;
  mesh_indices->clear();
  for (size_t mi = 0; mi < scene.meshes.size(); ++mi) {
    const std::string& mesh_path = scene.meshes[mi].prim_path;
    bool excluded = false;
    if (excludes) {
      for (const ::tinyusdz::next::Path& p : *excludes) {
        if (PathIsAtOrUnder(mesh_path, p.str())) { excluded = true; break; }
      }
    }
    if (excluded) continue;

    bool included = include_root && PathIsAtOrUnder(mesh_path, owner_path);
    if (!included && includes) {
      for (const ::tinyusdz::next::Path& p : *includes) {
        if (explicit_only ? (mesh_path == p.str())
                          : PathIsAtOrUnder(mesh_path, p.str())) {
          included = true;
          break;
        }
      }
    }
    if (included) mesh_indices->push_back(static_cast<int32_t>(mi));
  }
}

void ResolveLightLinking(const Stage& stage, RenderScene* scene) {
  if (!scene) return;
  for (RenderLight& light : scene->lights) {
    UsdPrim prim = stage.GetPrimAtPath(light.prim_path);
    if (!prim.IsValid()) continue;
    ResolveLightLinkInstance(prim, *scene, "lightLink",
                             &light.light_links_all,
                             &light.light_link_mesh_indices);
    ResolveLightLinkInstance(prim, *scene, "shadowLink",
                             &light.shadow_links_all,
                             &light.shadow_link_mesh_indices);
  }
}

std::vector<std::string> ReadRelationshipTargets(const UsdPrim& prim,
                                                 const std::string& name) {
  std::vector<std::string> out;
  const std::vector<::tinyusdz::next::Path>* targets =
      prim.GetRelationship(name);
  if (!targets) return out;
  out.reserve(targets->size());
  for (const ::tinyusdz::next::Path& target : *targets) {
    out.push_back(target.str());
  }
  return out;
}

double ReadDoubleProperty(const UsdPrim& prim, const std::string& name,
                          double fallback) {
  double d = fallback;
  if (GetDouble(prim, name, &d)) return d;
  return fallback;
}

void ApplyAxis(std::vector<value::float3>* points,
               std::vector<value::float3>* normals,
               const std::string& axis) {
  if (axis == "Y" || axis.empty()) return;
  // Proper ROTATIONS mapping the generator's +Y symmetry axis onto the
  // authored axis. The previous axis swap was a mirror (determinant -1),
  // which flipped the winding and turned the analytic meshes inside out.
  auto map_point = [&](value::float3& v) {
    const float x = v[0], y = v[1], z = v[2];
    if (axis == "Z") {
      // R_x(+90 deg): +Y -> +Z
      v[0] = x; v[1] = -z; v[2] = y;
    } else if (axis == "X") {
      // R_z(-90 deg): +Y -> +X
      v[0] = y; v[1] = -x; v[2] = z;
    }
  };
  for (value::float3& p : *points) map_point(p);
  if (normals) {
    for (value::float3& n : *normals) map_point(n);
  }
}

void FillGeneratedMesh(const UsdPrim& prim,
                       const std::vector<value::float3>& points,
                       const std::vector<int>& face_counts,
                       const std::vector<int>& face_indices,
                       const std::vector<value::float3>& normals,
                       const std::vector<value::float2>& uvs,
                       RenderMesh* out) {
  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->face_vertex_counts.reserve(face_counts.size());
  for (int c : face_counts) {
    out->face_vertex_counts.push_back(c < 0 ? uint32_t{0}
                                            : static_cast<uint32_t>(c));
  }
  out->face_vertex_indices.reserve(face_indices.size());
  for (int idx : face_indices) {
    out->face_vertex_indices.push_back(idx < 0 ? uint32_t{0}
                                               : static_cast<uint32_t>(idx));
  }
  for (const value::float3& p : points) {
    out->points.push_back(p[0]);
    out->points.push_back(p[1]);
    out->points.push_back(p[2]);
  }
  if (!normals.empty()) {
    out->normals_interp = Interpolation::FaceVarying;
    for (const value::float3& n : normals) {
      out->normals.push_back(n[0]);
      out->normals.push_back(n[1]);
      out->normals.push_back(n[2]);
    }
  }
  if (!uvs.empty()) {
    out->texcoords_0_interp = Interpolation::FaceVarying;
    for (const value::float2& uv : uvs) {
      out->texcoords_0.push_back(uv[0]);
      out->texcoords_0.push_back(uv[1]);
    }
  }
  std::string orientation;
  if (GetToken(prim, "orientation", &orientation)) {
    out->left_handed = (orientation == "leftHanded");
  }
  GetBool(prim, "doubleSided", &out->double_sided);
  if (!points.empty()) {
    out->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    out->bbox_max = Float3(-1e30f, -1e30f, -1e30f);
    for (const value::float3& p : points) {
      out->bbox_min.x = std::min(out->bbox_min.x, p[0]);
      out->bbox_min.y = std::min(out->bbox_min.y, p[1]);
      out->bbox_min.z = std::min(out->bbox_min.z, p[2]);
      out->bbox_max.x = std::max(out->bbox_max.x, p[0]);
      out->bbox_max.y = std::max(out->bbox_max.y, p[1]);
      out->bbox_max.z = std::max(out->bbox_max.z, p[2]);
    }
    out->has_bbox = true;
  }
}

size_t SaturatingAdd(size_t a, size_t b) {
  if (b > std::numeric_limits<size_t>::max() - a) {
    return std::numeric_limits<size_t>::max();
  }
  return a + b;
}

size_t SaturatingMul(size_t a, size_t b) {
  if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
    return std::numeric_limits<size_t>::max();
  }
  return a * b;
}

size_t AuthoredArraySize(const UsdPrim& prim, const char* name) {
  const Value* value = GetAttribute(prim, name);
  return value && value->is_array() ? value->array_size() : 0;
}

bool HasAuthoredExtent(const UsdPrim& prim) {
  return AuthoredArraySize(prim, "extent") >= 2;
}

GeometryInfo BuildGeometryInfo(const UsdPrim& prim, GeometryKind kind,
                               int32_t id) {
  GeometryInfo info;
  info.kind = kind;
  info.id = id;
  info.prim_path = prim.GetPath().str();
  info.type_name = prim.GetTypeName();
  info.point_count = AuthoredArraySize(prim, "points");
  info.has_authored_extent = HasAuthoredExtent(prim);

  size_t estimate = 0;
  if (kind == GeometryKind::Mesh) {
    info.index_count = AuthoredArraySize(prim, "faceVertexIndices");
    // Account for decoded source arrays, render arrays, triangulation, and
    // face-varying remaps. This intentionally errs high for residency policy.
    estimate = SaturatingMul(info.point_count, 24);
    estimate = SaturatingAdd(estimate,
                             SaturatingMul(info.index_count, 16));
    estimate = SaturatingAdd(
        estimate,
        SaturatingMul(AuthoredArraySize(prim, "faceVertexCounts"), 8));
    estimate = SaturatingAdd(
        estimate, SaturatingMul(AuthoredArraySize(prim, "normals"), 24));
    estimate = SaturatingAdd(
        estimate,
        SaturatingMul(AuthoredArraySize(prim, "primvars:st"), 16));
    estimate = SaturatingAdd(
        estimate,
        SaturatingMul(AuthoredArraySize(prim, "primvars:displayColor"), 24));
  } else if (kind == GeometryKind::Points) {
    estimate = SaturatingMul(info.point_count, 48);
  } else {
    estimate = SaturatingMul(info.point_count, 40);
    estimate = SaturatingAdd(
        estimate,
        SaturatingMul(AuthoredArraySize(prim, "curveVertexCounts"), 8));
  }
  info.estimated_resident_bytes = estimate;
  return info;
}

bool ReadExtent(const UsdPrim& prim, double time, value::float3* minimum,
                value::float3* maximum) {
  if (!minimum || !maximum) return false;
  ValueArrayRead<float> extent;
  if (!ReadFloatArray(prim, "extent", time, &extent) || extent.size() < 6) {
    return false;
  }
  *minimum = value::float3{extent[0], extent[1], extent[2]};
  *maximum = value::float3{extent[3], extent[4], extent[5]};
  return true;
}

bool FillBoundsProxyMesh(const UsdPrim& prim, const value::float3& mn,
                         const value::float3& mx, RenderMesh* out) {
  if (!out) return false;
  const std::vector<value::float3> points = {
      {mn[0], mn[1], mn[2]}, {mx[0], mn[1], mn[2]},
      {mx[0], mx[1], mn[2]}, {mn[0], mx[1], mn[2]},
      {mn[0], mn[1], mx[2]}, {mx[0], mn[1], mx[2]},
      {mx[0], mx[1], mx[2]}, {mn[0], mx[1], mx[2]}};
  const std::vector<int> counts(6, 4);
  const std::vector<int> indices = {
      0, 3, 2, 1, 4, 5, 6, 7, 0, 1, 5, 4,
      3, 7, 6, 2, 0, 4, 7, 3, 1, 2, 6, 5};
  FillGeneratedMesh(prim, points, counts, indices, {}, {}, out);
  out->is_proxy = true;
  return true;
}

bool FillExtentProxyMesh(const UsdPrim& prim, double time, RenderMesh* out) {
  value::float3 mn;
  value::float3 mx;
  if (!ReadExtent(prim, time, &mn, &mx)) return false;
  return FillBoundsProxyMesh(prim, mn, mx, out);
}

void ExtractMaterialXConfig(const UsdPrim& prim,
                            RenderMaterial::MaterialXConfig* out) {
  if (!out || !prim.IsValid()) return;
  std::string v;
  if (ReadStringLikeProperty(prim, "config:mtlx:version", &v)) {
    out->version = v;
    out->authored = true;
  }
  if (ReadStringLikeProperty(prim, "config:mtlx:namespace", &v)) {
    out->name_space = v;
    out->authored = true;
  }
  if (ReadStringLikeProperty(prim, "config:mtlx:colorspace", &v)) {
    out->colorspace = v;
    out->authored = true;
  }
  if (ReadStringLikeProperty(prim, "config:mtlx:sourceUri", &v) ||
      ReadStringLikeProperty(prim, "config:mtlx:sourceAsset", &v) ||
      ReadStringLikeProperty(prim, "config:mtlx:file", &v)) {
    out->source_uri = v;
    out->authored = true;
  }
}

std::vector<PhysicsProperty> CollectPhysicsExtensionProperties(
    const UsdPrim& prim) {
  std::vector<PhysicsProperty> props;
  for (const std::string& name : prim.GetPropertyNames()) {
    if (!IsPhysicsExtensionPropertyName(name)) continue;
    if (const Value* value = prim.GetPropertyValue(name)) {
      PhysicsProperty prop;
      prop.name = name;
      prop.value = ValueSummary(*value);
      props.push_back(std::move(prop));
    }
  }
  for (const std::string& name : prim.GetRelationshipNames()) {
    if (!IsPhysicsExtensionPropertyName(name)) continue;
    PhysicsProperty prop;
    prop.name = name;
    const std::vector<::tinyusdz::next::Path>* targets =
        prim.GetRelationship(name);
    if (targets) {
      prop.value = "rel[" + std::to_string(targets->size()) + "]";
    }
    props.push_back(std::move(prop));
  }
  return props;
}

Float3 Float3FromArray(const float v[3]) {
  return Float3(v[0], v[1], v[2]);
}

Float4 Float4FromArray(const float v[4]) {
  return Float4(v[0], v[1], v[2], v[3]);
}

// First bound material path whose target actually resolves to a Material,
// walking the purpose order (preview, all-purpose, full). A dangling
// purpose-specific rel must fall through to the weaker-purpose rel on the
// SAME prim, not reject the prim (GetBoundMaterialPath returns only the
// first authored rel).
std::string FirstValidBoundMaterialPath(const Stage& stage,
                                        const ::tinyusdz::next::UsdPrim& prim) {
  static const char* kBindingOrder[] = {"material:binding:preview",
                                        "material:binding",
                                        "material:binding:full"};
  for (const char* rel : kBindingOrder) {
    const std::vector<::tinyusdz::next::Path>* targets =
        prim.GetRelationship(rel);
    if (!targets || targets->empty()) continue;
    const std::string p = (*targets)[0].str();
    if (!p.empty() && ::tinyusdz::tydra::next::IsMaterial(
                          stage.GetPrimAtPath(p))) {
      return p;
    }
  }
  return "";
}

std::string FindInheritedMaterialBinding(const Stage& stage,
                                         const std::string& prim_path) {
  // Core UsdShade resolution now validates targets at every purpose/ancestor
  // step and associates bindMaterialAs with the relationship that actually
  // won. Keep one implementation shared by the converter and applications.
  return ::tinyusdz::next::GetInheritedBoundMaterialPath(stage, prim_path);
}

}  // namespace

//
// Constructor / Destructor
//

RenderSceneConverter::RenderSceneConverter(const ConverterConfig& config)
    : config_(config) {}

RenderSceneConverter::~RenderSceneConverter() = default;

GeometryInfo RenderSceneConverter::GetGeometryInfo(const UsdPrim& prim,
                                                    GeometryKind kind,
                                                    int32_t id) const {
  return BuildGeometryInfo(prim, kind, id);
}

bool RenderSceneConverter::ConvertExtentProxy(const UsdPrim& prim,
                                              RenderMesh* out) {
  if (!FillExtentProxyMesh(prim, config_.time_code, out)) return false;
  if (config_.mesh.triangulate && !TriangulateMesh(out)) return false;
  if (config_.mesh.compute_normals && !out->has_normals() &&
      !ComputeVertexNormals(out)) {
    return false;
  }
  out->compact();
  return !out->has_alloc_failure();
}

bool RenderSceneConverter::ConvertBoundsProxy(const UsdPrim& prim,
                                              const Float3& minimum,
                                              const Float3& maximum,
                                              RenderMesh* out) {
  const value::float3 mn = {minimum.x, minimum.y, minimum.z};
  const value::float3 mx = {maximum.x, maximum.y, maximum.z};
  if (!FillBoundsProxyMesh(prim, mn, mx, out)) return false;
  if (config_.mesh.triangulate && !TriangulateMesh(out)) return false;
  if (config_.mesh.compute_normals && !out->has_normals() &&
      !ComputeVertexNormals(out)) {
    return false;
  }
  out->compact();
  return !out->has_alloc_failure();
}

namespace {

// Release arrays that are not needed by the metadata-only RenderScene path.
// Material binding still needs polygon counts/triangle offsets and material UV
// promotion still needs texcoords/primvars, so conversion uses this in two
// phases. Assignment from an empty ChunkedArray releases chunks immediately;
// ChunkedArray::clear() intentionally retains them for reuse.
void ReleaseMeshGeometry(RenderMesh* mesh, bool keep_binding_inputs,
                         bool keep_triangulation = false) {
  if (!mesh) return;

  mesh->face_vertex_indices = UInt32Chunked();
  mesh->points = FloatChunked();
  mesh->normals = FloatChunked();
  mesh->tangents = FloatChunked();
  mesh->colors = FloatChunked();
  mesh->opacities = FloatChunked();
  if (!keep_triangulation) {
    mesh->triangulated_indices = UInt32Chunked();
    mesh->triangulated_face_vertex_indices = UInt32Chunked();
  }

  if (keep_binding_inputs) return;

  mesh->face_vertex_counts = UInt32Chunked();
  mesh->texcoords_0 = FloatChunked();
  mesh->texcoords_1 = FloatChunked();
  std::vector<VertexAttribute>().swap(mesh->primvars);
  std::vector<uint32_t>().swap(mesh->face_triangle_offsets);
  std::vector<int32_t>().swap(mesh->sanitize_face_remap);
  std::vector<uint32_t>().swap(mesh->hole_faces);
}

}  // namespace

//
// Main conversion
//

ConvertResult RenderSceneConverter::Convert(const Stage& stage) {
  ConvertResult result;
  warnings_.clear();

  // Built with -fno-exceptions: the conversion helpers report failures via
  // return codes / the warnings_ list rather than throwing, so no try/catch.
  {
    // Report progress
    if (config_.progress_callback) {
      config_.progress_callback(0.0f, "Starting conversion...");
    }

    // Set scene metadata
    auto meta = stage.GetMeta();
    result.scene.name = meta.defaultPrim;
    result.scene.default_prim = meta.defaultPrim;
    result.scene.meters_per_unit = static_cast<float>(meta.metersPerUnit);
    result.scene.up_axis = (meta.upAxis == "Z") ?
                           RenderScene::UpAxis::Z : RenderScene::UpAxis::Y;
    result.scene.start_time = meta.startTimeCode;
    result.scene.end_time = meta.endTimeCode;
    result.scene.frames_per_second = meta.timeCodesPerSecond;
    ::tinyusdz::next::color_management::RenderingColorConfig color_config;
    std::string color_warning;
    (void)::tinyusdz::next::color_management::ResolveRenderingColorConfig(
        stage, config_.material.render_settings_path, &color_config,
        &color_warning);
    result.scene.render_settings_path = color_config.render_settings_path;
    result.scene.working_color_space = color_config.working_space;
    ::tinyusdz::color::ColorSpaceDesc display_linear;
    ::tinyusdz::color::ColorTransform working_to_display;
    if (::tinyusdz::color::GetBuiltinColorSpace("lin_rec709_scene",
                                                &display_linear) &&
        ::tinyusdz::color::BuildColorTransform(
            color_config.working_definition, display_linear,
            &working_to_display)) {
      for (size_t i = 0; i < 9; ++i) {
        result.scene.working_to_display_linear[i] =
            working_to_display.matrix[i];
      }
    }
    if (!color_warning.empty()) warnings_.push_back(color_warning);

    RenderExtractOptions xopts;
    xopts.time_code = config_.time_code;
    xopts.collect_other = true;
    RenderExtractResult extracted;
    CollectRenderPrims(stage, xopts, &extracted);

    // Build node hierarchy first
    if (config_.progress_callback) {
      config_.progress_callback(0.1f, "Building node hierarchy...");
    }
    BuildNodeHierarchy(extracted, &result.scene);
    ExtractPhysicsAnnotations(stage, &result.scene);
    for (const RenderPrimRecord& rec : extracted.records) {
      if (rec.type_name == "Points") continue;
      if (!IsUnsupportedRenderableTypeName(rec.type_name)) continue;
      UnsupportedRenderable unsupported;
      unsupported.prim_path = rec.path;
      unsupported.type_name = rec.type_name;
      unsupported.reason =
          "recognized by extraction but not converted to render geometry";
      result.scene.unsupported_renderables.push_back(unsupported);
      warnings_.push_back("Unsupported renderable prim '" + rec.path +
                          "' of type '" + rec.type_name + "'");
    }

    for (const auto& rec : extracted.records) {
      AnimationClip clip;
      if (ConvertAnimation(stage, rec.prim, &clip)) {
        const auto node_it = result.scene.node_by_path.find(rec.path);
        if (node_it != result.scene.node_by_path.end()) {
          for (AnimationChannel& channel : clip.channels) {
            channel.target_node = node_it->second;
          }
        }
        result.scene.animations.push_back(std::move(clip));
      }
    }

    // Convert meshes
    float mesh_progress_start = 0.2f;
    float mesh_progress_end = 0.5f;

    for (size_t i = 0; i < extracted.meshes.size(); ++i) {
      const UsdPrim& mesh_prim = extracted.meshes[i].prim;
      if (config_.progress_callback) {
        float p = mesh_progress_start +
                  (mesh_progress_end - mesh_progress_start) * i /
                      std::max<size_t>(extracted.meshes.size(), 1);
        config_.progress_callback(p, "Converting mesh: " + mesh_prim.GetName());
      }

      RenderMesh mesh;
      const bool converted = ConvertRenderableMesh(stage, mesh_prim, &mesh);
      if (converted && mesh.has_alloc_failure()) {
        // ConvertGeomPrimitive does not run ConvertMesh's alloc check.
        warnings_.push_back("Out of memory converting prim '" +
                            mesh_prim.GetPath().str() +
                            "'; the prim was skipped");
        continue;
      }
      if (converted) {
        const bool analytic = mesh_prim.GetTypeName() != "Mesh";
        if (!config_.mesh.retain_geometry &&
            !(analytic && config_.mesh.retain_analytic_geometry)) {
          // Retain only the small inputs still required by the later material
          // binding and UV-selection passes. This bounds conversion memory by
          // one source mesh instead of accumulating the whole render scene.
          ReleaseMeshGeometry(&mesh, true,
                              config_.mesh.retain_triangulation);
        }
        // Release chunk-allocation slack before retaining: thousands of small
        // meshes each holding 64KB-minimum chunks otherwise OOM wasm32.
        mesh.compact();
        int32_t mesh_id = static_cast<int32_t>(result.scene.meshes.size());
        result.scene.mesh_by_path[mesh.prim_path] = mesh_id;
        result.scene.meshes.push_back(std::move(mesh));
        AssignNodeDataId(&result.scene, mesh_prim.GetPath().str(), mesh_id);
      } else {
        warnings_.push_back("Failed to convert renderable mesh prim: " +
                            mesh_prim.GetPath().str());
      }
    }

    for (const auto& rec : extracted.records) {
      if (rec.type_name != "Points") continue;
      RenderPoints points;
      if (ConvertPoints(rec.prim, &points)) {
        if (points.points.alloc_failed() || points.widths.alloc_failed() ||
            points.colors.alloc_failed()) {
          warnings_.push_back("Out of memory converting Points '" + rec.path +
                              "'; the prim was skipped");
          continue;
        }
        int32_t points_id = static_cast<int32_t>(result.scene.points.size());
        result.scene.points_by_path[points.prim_path] = points_id;
        result.scene.points.push_back(std::move(points));
        AssignNodeDataId(&result.scene, rec.path, points_id);
      } else {
        warnings_.push_back("Failed to convert Points: " + rec.path);
      }
    }

    for (const auto& rec : extracted.curves) {
      RenderCurves curves;
      if (ConvertCurves(rec.prim, &curves)) {
        if (curves.points.alloc_failed() || curves.widths.alloc_failed() ||
            curves.colors.alloc_failed() ||
            curves.tessellated_points.alloc_failed() ||
            curves.tessellated_widths.alloc_failed() ||
            curves.tessellated_colors.alloc_failed()) {
          warnings_.push_back("Out of memory converting curves '" + rec.path +
                              "'; the prim was skipped");
          continue;
        }
        int32_t curves_id = static_cast<int32_t>(result.scene.curves.size());
        result.scene.curves_by_path[curves.prim_path] = curves_id;
        result.scene.curves.push_back(std::move(curves));
        AssignNodeDataId(&result.scene, rec.path, curves_id);
      } else {
        warnings_.push_back("Failed to convert curves prim: " + rec.path);
      }
    }

    for (const auto& rec : extracted.point_instancers) {
      RenderPointInstancer instancer;
      if (ConvertPointInstancer(rec.prim, &instancer)) {
        int32_t instancer_id =
            static_cast<int32_t>(result.scene.point_instancers.size());
        result.scene.point_instancer_by_path[instancer.prim_path] = instancer_id;
        if (!instancer.valid) {
          warnings_.push_back("Invalid PointInstancer data at " +
                              instancer.prim_path + ": " +
                              instancer.validation_error);
        }
        ResolvePointInstancerPrototypeBindings(&result.scene, &instancer);
        for (size_t proto_i = 0; proto_i < instancer.prototype_paths.size();
             ++proto_i) {
          if (proto_i >= instancer.prototype_node_ids.size() ||
              instancer.prototype_node_ids[proto_i] < 0) {
            warnings_.push_back("Unresolved PointInstancer prototype at " +
                                instancer.prim_path + ": " +
                                instancer.prototype_paths[proto_i]);
          } else if (instancer.prototype_mesh_count(proto_i) == 0) {
            warnings_.push_back("PointInstancer prototype has no meshes at " +
                                instancer.prim_path + ": " +
                                instancer.prototype_paths[proto_i]);
          }
        }
        if (config_.point_instancer.build_instance_draws ||
            config_.point_instancer.duplicate_meshes) {
          AppendPointInstanceDraws(instancer_id, &instancer, &result.scene);
        }
        result.scene.point_instancers.push_back(std::move(instancer));
        AssignNodeDataId(&result.scene, rec.path, instancer_id);
      } else {
        warnings_.push_back("Failed to convert PointInstancer: " + rec.path);
      }
    }

    // Convert materials
    float mat_progress_start = 0.5f;
    float mat_progress_end = 0.7f;

    for (size_t i = 0; i < extracted.materials.size(); ++i) {
      const UsdPrim& mat_prim = extracted.materials[i].prim;
      if (config_.progress_callback) {
        float p = mat_progress_start +
                  (mat_progress_end - mat_progress_start) * i /
                      std::max<size_t>(extracted.materials.size(), 1);
        config_.progress_callback(p, "Converting material: " + mat_prim.GetName());
      }

      RenderMaterial material;
      if (ConvertMaterial(stage, mat_prim, &material, &result.scene)) {
        int32_t mat_id = static_cast<int32_t>(result.scene.materials.size());
        result.scene.material_by_path[material.prim_path] = mat_id;
        result.scene.materials.push_back(std::move(material));
      } else {
        warnings_.push_back("Failed to convert material: " + mat_prim.GetPath().str());
      }
    }

    AssignMaterialBindings(stage, &result.scene);
    // 2-arg on purpose: promotion compares against the mesh's ACTUAL
    // texcoords_0/1 names now (53415635e), which supersedes the old
    // `default_uv` / `default_uv + "1"` heuristic and its config field.
    PromoteMaterialUVPrimvars(&result.scene, &warnings_);
    if (!config_.mesh.retain_geometry) {
      for (RenderMesh& mesh : result.scene.meshes) {
        const UsdPrim source = stage.GetPrimAtPath(mesh.prim_path);
        const bool analytic = source.IsValid() && source.GetTypeName() != "Mesh";
        if (analytic && config_.mesh.retain_analytic_geometry) continue;
        ReleaseMeshGeometry(&mesh, false,
                            config_.mesh.retain_triangulation);
      }
    }
    AssignPointInstanceDrawMaterials(&result.scene);
    if (config_.point_instancer.duplicate_meshes) {
      DuplicatePointInstanceMeshes(&result.scene);
    }

    // Convert lights
    for (const auto& rec : extracted.lights) {
      RenderLight light;
      if (ConvertLight(rec.prim, &light)) {
        for (int i = 0; i < 16; ++i) {
          light.transform.m[i] = static_cast<float>(rec.world[i]);
        }
        // DomeLight environment texture -> image, id stored in params.dome.
        if (light.type == LightType::Dome) {
          light.params.dome.texture_id = -1;
          std::string tex;
          const Value* fv = GetAttribute(rec.prim, "inputs:texture:file");
          if (fv) {
            if (const std::string* ap = fv->as_asset_path()) tex = *ap;
            else if (const std::string* s = fv->as_string()) tex = *s;
            else if (const std::string* t = fv->as_token()) tex = *t;
          }
          if (!tex.empty()) {
            light.params.dome.texture_id = ResolveImageId(
                &result.scene, tex, ColorSpace::Linear, AssetAnchorOf(rec.prim));
          }
        }
        int32_t light_id = static_cast<int32_t>(result.scene.lights.size());
        result.scene.lights.push_back(std::move(light));
        AssignNodeDataId(&result.scene, rec.path, light_id);
      }
    }

    // Convert cameras
    for (const auto& rec : extracted.cameras) {
      RenderCamera camera;
      if (ConvertCamera(rec.prim, &camera)) {
        for (int i = 0; i < 16; ++i) {
          camera.transform.m[i] = static_cast<float>(rec.world[i]);
        }
        int32_t camera_id = static_cast<int32_t>(result.scene.cameras.size());
        result.scene.cameras.push_back(std::move(camera));
        AssignNodeDataId(&result.scene, rec.path, camera_id);
      }
    }

    // Convert skeletons
    for (const auto& rec : extracted.skeletons) {
      Skeleton skeleton;
      if (ConvertSkeleton(rec.prim, &skeleton)) {
        // skel:animationSource may be authored on the SkelRoot (or another
        // ancestor) instead of the Skeleton itself; every descendant
        // Skeleton inherits it (UsdSkel binding inheritance).
        if (skeleton.animation_source_path.empty()) {
          UsdPrim anc = GetParent(stage, rec.prim);
          while (anc.IsValid()) {
            const std::vector<std::string> sources =
                ReadRelationshipTargets(anc, "skel:animationSource");
            if (!sources.empty()) {
              skeleton.animation_source_path = sources[0];
              break;
            }
            if (::tinyusdz::tydra::next::IsSkelRoot(anc)) break;
            anc = GetParent(stage, anc);
          }
        }
        int32_t skeleton_id = static_cast<int32_t>(result.scene.skeletons.size());
        result.scene.skeletons.push_back(std::move(skeleton));
        AssignNodeDataId(&result.scene, rec.path, skeleton_id);
      }
    }

    // Resolve mesh skin bindings to skeleton ids (skeletons converted above).
    for (RenderMesh& mesh : result.scene.meshes) {
      if (!mesh.skin || mesh.skin->skeleton_path.empty()) continue;
      for (size_t si = 0; si < result.scene.skeletons.size(); ++si) {
        if (result.scene.skeletons[si].prim_path == mesh.skin->skeleton_path) {
          mesh.skin->skeleton_id = static_cast<int32_t>(si);
          break;
        }
      }

      // Mesh-local `skel:joints`: jointIndices index into the mesh's own
      // (subset/permuted) joint list — remap them onto the skeleton's joint
      // order. Unmatched tokens zero the influence weight rather than
      // silently deforming by joint 0.
      if (!mesh.skin->mesh_joint_order.empty() &&
          mesh.skin->skeleton_id >= 0) {
        const Skeleton& skel =
            result.scene.skeletons[static_cast<size_t>(mesh.skin->skeleton_id)];
        const JointIndexLookup joint_lookup = BuildJointIndexLookup(skel);
        std::vector<int32_t> remap(mesh.skin->mesh_joint_order.size(), -1);
        bool identity = true;
        for (size_t k = 0; k < mesh.skin->mesh_joint_order.size(); ++k) {
          const std::string& token = mesh.skin->mesh_joint_order[k];
          remap[k] = FindJointIndex(skel, joint_lookup, token);
          if (remap[k] != static_cast<int32_t>(k)) identity = false;
          if (remap[k] < 0) {
            warnings_.push_back("Mesh " + mesh.prim_path +
                                " skel:joints token '" + token +
                                "' not found in skeleton " + skel.prim_path);
          }
        }
        if (!identity) {
          const size_t n = mesh.skin->joint_indices.size();
          for (size_t k = 0; k < n; ++k) {
            const uint16_t local = mesh.skin->joint_indices[k];
            const int32_t target =
                local < remap.size() ? remap[local] : -1;
            if (target >= 0 && target <= 65535) {
              mesh.skin->joint_indices[k] = static_cast<uint16_t>(target);
            } else {
              mesh.skin->joint_indices[k] = 0;
              if (k < mesh.skin->joint_weights.size()) {
                mesh.skin->joint_weights[k] = 0.0f;
              }
            }
          }
        }
      }
    }

    ResolveSkeletalAnimationTargets(&result.scene);

    ResolveLightLinking(stage, &result.scene);

    if (config_.progress_callback) {
      config_.progress_callback(1.0f, "Conversion complete");
    }

    result.success = true;
    result.warnings = std::move(warnings_);
  }

  return result;
}

StreamConvertResult RenderSceneConverter::ConvertToSink(const Stage& stage,
                                                        SceneSink* sink) {
  StreamConvertResult result;
  warnings_.clear();
  if (!sink) {
    result.error = "ConvertToSink: null scene sink";
    return result;
  }
  const auto cancellation_requested = [&]() {
    return config_.cancel_callback && config_.cancel_callback();
  };
  if (cancellation_requested()) {
    result.cancelled = true;
    result.error = "conversion cancelled";
    return result;
  }

  RenderScene catalog;
  const auto meta = stage.GetMeta();
  catalog.name = meta.defaultPrim;
  catalog.default_prim = meta.defaultPrim;
  catalog.meters_per_unit = static_cast<float>(meta.metersPerUnit);
  catalog.up_axis = meta.upAxis == "Z" ? RenderScene::UpAxis::Z
                                        : RenderScene::UpAxis::Y;
  catalog.start_time = meta.startTimeCode;
  catalog.end_time = meta.endTimeCode;
  catalog.frames_per_second = meta.timeCodesPerSecond;
  ::tinyusdz::next::color_management::RenderingColorConfig color_config;
  std::string color_warning;
  (void)::tinyusdz::next::color_management::ResolveRenderingColorConfig(
      stage, config_.material.render_settings_path, &color_config,
      &color_warning);
  catalog.render_settings_path = color_config.render_settings_path;
  catalog.working_color_space = color_config.working_space;
  ::tinyusdz::color::ColorSpaceDesc display_linear;
  ::tinyusdz::color::ColorTransform working_to_display;
  if (::tinyusdz::color::GetBuiltinColorSpace("lin_rec709_scene",
                                              &display_linear) &&
      ::tinyusdz::color::BuildColorTransform(
          color_config.working_definition, display_linear,
          &working_to_display)) {
    for (size_t i = 0; i < 9; ++i) {
      catalog.working_to_display_linear[i] = working_to_display.matrix[i];
    }
  }
  if (!color_warning.empty()) warnings_.push_back(color_warning);

  RenderExtractOptions xopts;
  xopts.time_code = config_.time_code;
  xopts.collect_other = true;
  RenderExtractResult extracted;
  CollectRenderPrims(stage, xopts, &extracted);
  if (cancellation_requested()) {
    result.cancelled = true;
    result.error = "conversion cancelled";
    return result;
  }
  BuildNodeHierarchy(extracted, &catalog);
  ExtractPhysicsAnnotations(stage, &catalog);

  for (const RenderPrimRecord& rec : extracted.records) {
    if (rec.type_name != "Points" &&
        IsUnsupportedRenderableTypeName(rec.type_name)) {
      UnsupportedRenderable unsupported;
      unsupported.prim_path = rec.path;
      unsupported.type_name = rec.type_name;
      unsupported.reason = "recognized but not converted to render geometry";
      catalog.unsupported_renderables.push_back(std::move(unsupported));
      warnings_.push_back("Unsupported renderable prim '" + rec.path +
                          "' of type '" + rec.type_name + "'");
    }

    AnimationClip clip;
    if (ConvertAnimation(stage, rec.prim, &clip)) {
      const auto node = catalog.node_by_path.find(rec.path);
      if (node != catalog.node_by_path.end()) {
        for (AnimationChannel& channel : clip.channels) {
          channel.target_node = node->second;
        }
      }
      catalog.animations.push_back(std::move(clip));
    }
  }

  // Materials and their image/texture descriptors are a small catalog and
  // must be assigned before geometry is emitted.
  for (const RenderPrimRecord& rec : extracted.materials) {
    RenderMaterial material;
    if (ConvertMaterial(stage, rec.prim, &material, &catalog)) {
      const int32_t id = static_cast<int32_t>(catalog.materials.size());
      catalog.material_by_path[material.prim_path] = id;
      catalog.materials.push_back(std::move(material));
    } else {
      warnings_.push_back("Failed to convert material: " + rec.path);
    }
  }

  for (const RenderPrimRecord& rec : extracted.skeletons) {
    Skeleton skeleton;
    if (ConvertSkeleton(rec.prim, &skeleton)) {
      const int32_t id = static_cast<int32_t>(catalog.skeletons.size());
      catalog.skeletons.push_back(std::move(skeleton));
      AssignNodeDataId(&catalog, rec.path, id);
    }
  }
  ResolveSkeletalAnimationTargets(&catalog);

  for (const RenderPrimRecord& rec : extracted.lights) {
    RenderLight light;
    if (!ConvertLight(rec.prim, &light)) continue;
    for (int i = 0; i < 16; ++i) light.transform.m[i] = float(rec.world[i]);
    if (light.type == LightType::Dome) {
      std::string texture;
      if (const Value* value = GetAttribute(rec.prim, "inputs:texture:file")) {
        if (const std::string* p = value->as_asset_path()) texture = *p;
        else if (const std::string* p = value->as_string()) texture = *p;
        else if (const std::string* p = value->as_token()) texture = *p;
      }
      light.params.dome.texture_id =
          texture.empty() ? -1
                          : ResolveImageId(&catalog, texture, ColorSpace::Linear,
                                           AssetAnchorOf(rec.prim));
    }
    const int32_t id = static_cast<int32_t>(catalog.lights.size());
    catalog.lights.push_back(std::move(light));
    AssignNodeDataId(&catalog, rec.path, id);
  }
  for (const RenderPrimRecord& rec : extracted.cameras) {
    RenderCamera camera;
    if (!ConvertCamera(rec.prim, &camera)) continue;
    for (int i = 0; i < 16; ++i) camera.transform.m[i] = float(rec.world[i]);
    const int32_t id = static_cast<int32_t>(catalog.cameras.size());
    catalog.cameras.push_back(std::move(camera));
    AssignNodeDataId(&catalog, rec.path, id);
  }

  // Reserve stable geometry IDs with lightweight placeholders. This is enough
  // for native/PointInstancer prototype binding without retaining geometry.
  catalog.meshes.resize(extracted.meshes.size());
  for (size_t i = 0; i < extracted.meshes.size(); ++i) {
    RenderMesh& placeholder = catalog.meshes[i];
    placeholder.name = extracted.meshes[i].prim.GetName();
    placeholder.prim_path = extracted.meshes[i].path;
    catalog.mesh_by_path[placeholder.prim_path] = static_cast<int32_t>(i);
    AssignMeshMaterialBinding(stage, catalog, &placeholder);
    AssignNodeDataId(&catalog, placeholder.prim_path, static_cast<int32_t>(i));
  }
  catalog.points.reserve(extracted.records.size());
  for (const RenderPrimRecord& rec : extracted.records) {
    if (rec.type_name != "Points") continue;
    const int32_t id = static_cast<int32_t>(catalog.points.size());
    RenderPoints placeholder;
    placeholder.name = rec.prim.GetName();
    placeholder.prim_path = rec.path;
    catalog.points_by_path[rec.path] = id;
    catalog.points.push_back(std::move(placeholder));
    AssignNodeDataId(&catalog, rec.path, id);
  }
  catalog.curves.resize(extracted.curves.size());
  for (size_t i = 0; i < extracted.curves.size(); ++i) {
    RenderCurves& placeholder = catalog.curves[i];
    placeholder.name = extracted.curves[i].prim.GetName();
    placeholder.prim_path = extracted.curves[i].path;
    const std::string material =
        FindInheritedMaterialBinding(stage, placeholder.prim_path);
    const auto found = catalog.material_by_path.find(material);
    if (found != catalog.material_by_path.end()) {
      placeholder.material_id = found->second;
    }
    catalog.curves_by_path[placeholder.prim_path] = static_cast<int32_t>(i);
    AssignNodeDataId(&catalog, placeholder.prim_path, static_cast<int32_t>(i));
  }

  for (const RenderPrimRecord& rec : extracted.point_instancers) {
    RenderPointInstancer instancer;
    if (!ConvertPointInstancer(rec.prim, &instancer)) {
      warnings_.push_back("Failed to convert PointInstancer: " + rec.path);
      continue;
    }
    const int32_t id = static_cast<int32_t>(catalog.point_instancers.size());
    catalog.point_instancer_by_path[instancer.prim_path] = id;
    ResolvePointInstancerPrototypeBindings(&catalog, &instancer);
    if (config_.point_instancer.build_instance_draws ||
        config_.point_instancer.duplicate_meshes) {
      AppendPointInstanceDraws(id, &instancer, &catalog);
    }
    catalog.point_instancers.push_back(std::move(instancer));
    AssignNodeDataId(&catalog, rec.path, id);
  }
  AssignPointInstanceDrawMaterials(&catalog);

  RenderScene binding_catalog;
  binding_catalog.material_by_path = catalog.material_by_path;
  std::unordered_map<std::string, int32_t> skeleton_by_path;
  for (size_t i = 0; i < catalog.skeletons.size(); ++i) {
    skeleton_by_path[catalog.skeletons[i].prim_path] = static_cast<int32_t>(i);
  }

  if (!sink->BeginScene(std::move(catalog))) {
    result.error = "scene sink rejected catalog";
    return result;
  }

  const auto abort = [&](const std::string& error, bool cancelled) {
    sink->AbortScene();
    result.cancelled = cancelled;
    result.error = error;
    result.warnings = std::move(warnings_);
  };

  for (size_t i = 0; i < extracted.meshes.size(); ++i) {
    const UsdPrim& prim = extracted.meshes[i].prim;
    if (cancellation_requested()) {
      abort("conversion cancelled", true);
      return result;
    }
    const GeometryInfo info =
        GetGeometryInfo(prim, GeometryKind::Mesh, static_cast<int32_t>(i));
    const GeometryDisposition disposition = sink->SelectGeometry(info);
    if (disposition == GeometryDisposition::Cancel) {
      abort("conversion cancelled by scene sink", true);
      return result;
    }
    if (disposition == GeometryDisposition::Skip) continue;

    RenderMesh mesh;
    bool converted = false;
    if (disposition == GeometryDisposition::Proxy) {
      converted = ConvertExtentProxy(prim, &mesh);
      if (!converted) {
        warnings_.push_back("Skipping proxy without a valid extent: " +
                            prim.GetPath().str());
        continue;
      }
    } else {
      converted = ConvertRenderableMesh(stage, prim, &mesh);
    }
    if (!converted || mesh.has_alloc_failure()) {
      warnings_.push_back("Failed to convert renderable mesh prim: " +
                          prim.GetPath().str());
      continue;
    }
    AssignMeshMaterialBinding(stage, binding_catalog, &mesh);
    if (mesh.skin) {
      const auto skeleton = skeleton_by_path.find(mesh.skin->skeleton_path);
      if (skeleton != skeleton_by_path.end()) {
        mesh.skin->skeleton_id = skeleton->second;
      }
    }
    mesh.compact();
    if (!sink->AddMesh(static_cast<int32_t>(i), std::move(mesh))) {
      abort("scene sink rejected mesh", false);
      return result;
    }
    ++result.mesh_count;
  }

  int32_t points_id = 0;
  for (const RenderPrimRecord& rec : extracted.records) {
    if (rec.type_name != "Points") continue;
    if (cancellation_requested()) {
      abort("conversion cancelled", true);
      return result;
    }
    const GeometryInfo info =
        GetGeometryInfo(rec.prim, GeometryKind::Points, points_id);
    const GeometryDisposition disposition = sink->SelectGeometry(info);
    if (disposition == GeometryDisposition::Cancel) {
      abort("conversion cancelled by scene sink", true);
      return result;
    }
    if (disposition != GeometryDisposition::Full) {
      if (disposition == GeometryDisposition::Proxy) {
        warnings_.push_back("Skipping Points proxy without mesh expansion: " +
                            rec.path);
      }
      ++points_id;
      continue;
    }
    RenderPoints points;
    if (ConvertPoints(rec.prim, &points)) {
      if (!sink->AddPoints(points_id, std::move(points))) {
        abort("scene sink rejected points", false);
        return result;
      }
      ++result.point_count;
    }
    ++points_id;
  }
  for (size_t i = 0; i < extracted.curves.size(); ++i) {
    if (cancellation_requested()) {
      abort("conversion cancelled", true);
      return result;
    }
    const GeometryInfo info = GetGeometryInfo(
        extracted.curves[i].prim, GeometryKind::Curves,
        static_cast<int32_t>(i));
    const GeometryDisposition disposition = sink->SelectGeometry(info);
    if (disposition == GeometryDisposition::Cancel) {
      abort("conversion cancelled by scene sink", true);
      return result;
    }
    if (disposition != GeometryDisposition::Full) {
      if (disposition == GeometryDisposition::Proxy) {
        warnings_.push_back("Skipping Curves proxy without mesh expansion: " +
                            extracted.curves[i].path);
      }
      continue;
    }
    RenderCurves curves;
    if (!ConvertCurves(extracted.curves[i].prim, &curves)) continue;
    const std::string material =
        FindInheritedMaterialBinding(stage, curves.prim_path);
    const auto found = binding_catalog.material_by_path.find(material);
    if (found != binding_catalog.material_by_path.end()) {
      curves.material_id = found->second;
    }
    if (!sink->AddCurves(static_cast<int32_t>(i), std::move(curves))) {
      abort("scene sink rejected curves", false);
      return result;
    }
    ++result.curve_count;
  }

  if (!sink->EndScene()) {
    abort("scene sink failed to finalize", false);
    return result;
  }
  result.success = true;
  result.warnings = std::move(warnings_);
  return result;
}

void RenderSceneConverter::ExtractPhysicsAnnotations(const Stage& stage,
                                                     RenderScene* scene) {
  if (!scene) return;

  stage.Traverse([&](const UsdPrim& prim) {
    const std::string path = prim.GetPath().str();

    if (::tinyusdz::next::IsPhysicsScene(prim)) {
      ::tinyusdz::next::PhysicsSceneData data;
      if (::tinyusdz::next::GetPhysicsSceneData(stage, prim, &data,
                                                config_.time_code)) {
        PhysicsSceneAnnotation out;
        out.prim_path = path;
        out.gravity_direction = Float3FromArray(data.gravityDirection);
        out.gravity_magnitude = data.gravityMagnitude;
        out.extension_properties = CollectPhysicsExtensionProperties(prim);
        scene->physics.scenes.push_back(std::move(out));
      }
    }

    if (::tinyusdz::next::HasPhysicsRigidBodyAPI(prim) ||
        ::tinyusdz::next::HasPhysicsMassAPI(prim)) {
      PhysicsRigidBodyAnnotation out;
      out.prim_path = path;
      if (::tinyusdz::next::HasPhysicsRigidBodyAPI(prim)) {
        ::tinyusdz::next::PhysicsRigidBodyData data;
        if (::tinyusdz::next::GetPhysicsRigidBodyData(stage, prim, &data,
                                                      config_.time_code)) {
          out.rigid_body_enabled = data.rigidBodyEnabled;
          out.kinematic_enabled = data.kinematicEnabled;
          out.simulation_owner = data.simulationOwner;
          out.velocity = Float3FromArray(data.velocity);
          out.angular_velocity = Float3FromArray(data.angularVelocity);
          out.starts_asleep = data.startsAsleep;
        }
      }
      if (::tinyusdz::next::HasPhysicsMassAPI(prim)) {
        ::tinyusdz::next::PhysicsMassData data;
        if (::tinyusdz::next::GetPhysicsMassData(stage, prim, &data)) {
          out.has_mass = true;
          out.mass = data.mass;
          out.density = data.density;
          out.center_of_mass = Float3FromArray(data.centerOfMass);
          out.diagonal_inertia = Float3FromArray(data.diagonalInertia);
          out.principal_axes = Float4FromArray(data.principalAxes);
        }
      }
      out.extension_properties = CollectPhysicsExtensionProperties(prim);
      scene->physics.rigid_bodies.push_back(std::move(out));
    }

    if (::tinyusdz::next::HasPhysicsCollisionAPI(prim) ||
        ::tinyusdz::next::HasPhysicsMeshCollisionAPI(prim)) {
      PhysicsColliderAnnotation out;
      out.prim_path = path;
      if (::tinyusdz::next::HasPhysicsCollisionAPI(prim)) {
        ::tinyusdz::next::PhysicsCollisionData data;
        if (::tinyusdz::next::GetPhysicsCollisionData(stage, prim, &data)) {
          out.collision_enabled = data.collisionEnabled;
          out.simulation_owner = data.simulationOwner;
        }
      }
      if (::tinyusdz::next::HasPhysicsMeshCollisionAPI(prim)) {
        ::tinyusdz::next::PhysicsMeshCollisionData data;
        if (::tinyusdz::next::GetPhysicsMeshCollisionData(prim, &data)) {
          out.has_mesh_collision = true;
          out.approximation = data.approximation;
        }
      }
      out.extension_properties = CollectPhysicsExtensionProperties(prim);
      scene->physics.colliders.push_back(std::move(out));
    }

    if (::tinyusdz::next::IsPhysicsJoint(prim)) {
      PhysicsJointAnnotation out;
      out.prim_path = path;
      out.type_name = prim.GetTypeName();

      ::tinyusdz::next::PhysicsJointData base;
      if (::tinyusdz::next::GetPhysicsJointData(stage, prim, &base,
                                                config_.time_code)) {
        out.body0 = base.body0;
        out.body1 = base.body1;
        out.has_body0 = base.hasBody0;
        out.has_body1 = base.hasBody1;
        out.local_pos0 = Float3FromArray(base.localPos0);
        out.local_pos1 = Float3FromArray(base.localPos1);
        out.local_rot0 = Float4FromArray(base.localQuat0);
        out.local_rot1 = Float4FromArray(base.localQuat1);
        out.collision_enabled = base.collisionEnabled;
      }

      if (::tinyusdz::next::IsPhysicsRevoluteJoint(prim)) {
        ::tinyusdz::next::PhysicsRevoluteJointData data;
        if (::tinyusdz::next::GetPhysicsRevoluteJointData(
                stage, prim, &data, config_.time_code)) {
          out.axis = Float3FromArray(data.axis);
          out.lower_limit = data.lowerLimit;
          out.upper_limit = data.upperLimit;
        }
      } else if (::tinyusdz::next::IsPhysicsPrismaticJoint(prim)) {
        ::tinyusdz::next::PhysicsPrismaticJointData data;
        if (::tinyusdz::next::GetPhysicsPrismaticJointData(
                stage, prim, &data, config_.time_code)) {
          out.axis = Float3FromArray(data.axis);
          out.lower_limit = data.lowerLimit;
          out.upper_limit = data.upperLimit;
        }
      } else if (::tinyusdz::next::IsPhysicsSliderJoint(prim)) {
        ::tinyusdz::next::PhysicsSliderJointData data;
        if (::tinyusdz::next::GetPhysicsSliderJointData(
                stage, prim, &data, config_.time_code)) {
          out.axis = Float3FromArray(data.axis);
          out.lower_limit = data.lowerLimit;
          out.upper_limit = data.upperLimit;
        }
      } else if (::tinyusdz::next::IsPhysicsSphericalJoint(prim)) {
        ::tinyusdz::next::PhysicsSphericalJointData data;
        if (::tinyusdz::next::GetPhysicsSphericalJointData(
                stage, prim, &data, config_.time_code)) {
          out.cone_angle0_limit = data.coneAngle0Limit;
          out.cone_angle1_limit = data.coneAngle1Limit;
        }
      } else if (::tinyusdz::next::IsPhysicsBallJoint(prim)) {
        ::tinyusdz::next::PhysicsBallJointData data;
        if (::tinyusdz::next::GetPhysicsBallJointData(
                stage, prim, &data, config_.time_code)) {
          out.cone_angle0_limit = data.coneAngle0Limit;
          out.cone_angle1_limit = data.coneAngle1Limit;
        }
      } else if (::tinyusdz::next::IsPhysicsDistanceJoint(prim)) {
        ::tinyusdz::next::PhysicsDistanceJointData data;
        if (::tinyusdz::next::GetPhysicsDistanceJointData(
                stage, prim, &data, config_.time_code)) {
          out.min_distance = data.minDistance;
          out.max_distance = data.maxDistance;
        }
      }

      out.extension_properties = CollectPhysicsExtensionProperties(prim);
      scene->physics.joints.push_back(std::move(out));
    }

    if (::tinyusdz::next::HasPhysicsMaterialAPI(prim)) {
      ::tinyusdz::next::PhysicsMaterialData data;
      if (::tinyusdz::next::GetPhysicsMaterialData(stage, prim, &data)) {
        PhysicsMaterialAnnotation out;
        out.prim_path = path;
        out.static_friction = data.staticFriction;
        out.dynamic_friction = data.dynamicFriction;
        out.restitution = data.restitution;
        out.density = data.density;
        out.extension_properties = CollectPhysicsExtensionProperties(prim);
        scene->physics.materials.push_back(std::move(out));
      }
    }

    if (::tinyusdz::next::HasPhysicsFilteredPairsAPI(prim)) {
      ::tinyusdz::next::PhysicsFilteredPairsData data;
      if (::tinyusdz::next::GetPhysicsFilteredPairsData(prim, &data)) {
        PhysicsFilteredPairsAnnotation out;
        out.prim_path = path;
        out.filtered_pair_paths = std::move(data.filteredPairPaths);
        scene->physics.filtered_pairs.push_back(std::move(out));
      }
    }

    if (::tinyusdz::next::HasPhysicsArticulationRootAPI(prim)) {
      scene->physics.articulation_roots.push_back(path);
    }

    return true;
  });
}

//
// Node hierarchy
//

void RenderSceneConverter::BuildNodeHierarchy(const RenderExtractResult& extracted,
                                              RenderScene* scene) {
  std::unordered_map<std::string, int32_t> path_to_node;

  for (const RenderPrimRecord& rec : extracted.records) {
    const UsdPrim& prim = rec.prim;
    SceneNode node;
    node.name = prim.GetName();
    node.prim_path = rec.path;

    // Determine node type
    const std::string& type = rec.type_name;
    if (IsMeshRenderableTypeName(type)) node.type = NodeType::Mesh;
    else if (type == "Points") node.type = NodeType::Points;
    else if (type == "BasisCurves" || type == "NurbsCurves" ||
             type == "HermiteCurves") node.type = NodeType::Curves;
    else if (type == "PointInstancer") node.type = NodeType::PointInstancer;
    else if (type == "Xform") node.type = NodeType::Xform;
    else if (type == "Camera") node.type = NodeType::Camera;
    else if (type == "Skeleton") node.type = NodeType::Skeleton;
    else if (IsLight(prim)) {
      LightKind kind = GetLightKind(prim);
      switch (kind) {
        case LightKind::DistantLight: node.type = NodeType::DirectionalLight; break;
        case LightKind::DomeLight: node.type = NodeType::DomeLight; break;
        case LightKind::RectLight: node.type = NodeType::RectLight; break;
        case LightKind::DiskLight: node.type = NodeType::DiskLight; break;
        case LightKind::SphereLight: node.type = NodeType::SphereLight; break;
        case LightKind::PointLight: node.type = NodeType::PointLight; break;
        case LightKind::GeometryLight: node.type = NodeType::PointLight; break;
        case LightKind::PortalLight: node.type = NodeType::RectLight; break;
        case LightKind::PluginLight: node.type = NodeType::PointLight; break;
        case LightKind::LightFilter: node.type = NodeType::PointLight; break;
        case LightKind::PluginLightFilter: node.type = NodeType::PointLight; break;
        case LightKind::Unknown: node.type = NodeType::PointLight; break;
        default: node.type = NodeType::PointLight; break;
      }
    }

    // Compute transforms
    for (int i = 0; i < 16; ++i) {
      node.local_transform.m[i] = static_cast<float>(rec.local[i]);
      node.world_transform.m[i] = static_cast<float>(rec.world[i]);
    }

    int32_t node_id = static_cast<int32_t>(scene->nodes.size());
    path_to_node[node.prim_path] = node_id;
    scene->node_by_path[node.prim_path] = node_id;

    // Set parent
    std::string parent_path = GetParentPath(node.prim_path);
    bool parent_visible = true;
    if (!parent_path.empty() && parent_path != "/") {
      auto it = path_to_node.find(parent_path);
      if (it != path_to_node.end()) {
        node.parent_id = it->second;
        scene->nodes[it->second].children.push_back(node_id);
        parent_visible = scene->nodes[it->second].visible;
      }
    } else {
      scene->root_nodes.push_back(node_id);
    }

    node.visible = parent_visible && LocalVisibility(prim);

    scene->nodes.push_back(std::move(node));
  }
}


void RenderSceneConverter::AssignMaterialBindings(const Stage& stage,
                                                  RenderScene* scene) {
  if (!scene) return;
  for (RenderCurves& curves : scene->curves) {
    const std::string material_path =
        FindInheritedMaterialBinding(stage, curves.prim_path);
    if (!material_path.empty()) {
      const auto it = scene->material_by_path.find(material_path);
      if (it != scene->material_by_path.end()) curves.material_id = it->second;
    }
  }
  for (RenderMesh& mesh : scene->meshes) {
    AssignMeshMaterialBinding(stage, *scene, &mesh);
  }

  // Optional default material for unbound geometry (legacy
  // assign_default_material parity).
  if (config_.material.assign_default_material) {
    for (RenderMesh& mesh : scene->meshes) {
      if (mesh.material_id < 0) {
        mesh.material_id = GetOrCreateDefaultMaterial(scene);
      }
    }
    for (RenderCurves& curves : scene->curves) {
      if (curves.material_id < 0) {
        curves.material_id = GetOrCreateDefaultMaterial(scene);
      }
    }
  }
}

void RenderSceneConverter::AssignMeshMaterialBinding(const Stage& stage,
                                                     const RenderScene& scene,
                                                     RenderMesh* mesh) {
  if (!mesh) return;
  const std::string material_path =
      FindInheritedMaterialBinding(stage, mesh->prim_path);
  if (!material_path.empty()) {
    const auto it = scene.material_by_path.find(material_path);
    if (it != scene.material_by_path.end()) mesh->material_id = it->second;
  }

  // GeomSubset material bindings (familyName == materialBind): USD subsets
  // are arbitrary face-index sets; the range-based MaterialSubset model
  // stores one entry per CONSECUTIVE run of face indices.
  UsdPrim mesh_prim = stage.GetPrimAtPath(mesh->prim_path);
  if (!mesh_prim.IsValid()) return;
  for (const GeomSubset& sub : GetGeomSubsets(mesh_prim)) {
    if (!sub.family_name.empty() && sub.family_name != "materialBind") continue;
    std::string sub_mat = sub.material_path;
    if (sub_mat.empty()) {
      UsdPrim sub_prim = stage.GetPrimAtPath(sub.path);
      if (sub_prim.IsValid()) {
        sub_mat = FirstValidBoundMaterialPath(stage, sub_prim);
      }
    }
    if (sub_mat.empty()) continue;
    const auto mit = scene.material_by_path.find(sub_mat);
    if (mit == scene.material_by_path.end()) continue;
    const uint32_t nfaces = static_cast<uint32_t>(mesh->face_count());
    // Sort + split into consecutive runs, dropping out-of-range faces.
    // Authored subset indices use the ORIGINAL face numbering; when
    // sanitization dropped faces, route them through the old->new remap
    // first so the surviving faces keep their bindings.
    std::vector<uint32_t> faces;
    faces.reserve(sub.indices.size());
    for (int32_t fi : sub.indices) {
      if (fi < 0) continue;
      uint32_t face = static_cast<uint32_t>(fi);
      if (mesh->sanitize_dropped_faces > 0) {
        if (face >= mesh->sanitize_face_remap.size()) continue;
        const int32_t remapped = mesh->sanitize_face_remap[face];
        if (remapped < 0) continue;  // face was dropped by sanitize
        face = static_cast<uint32_t>(remapped);
      }
      if (face < nfaces) {
        faces.push_back(face);
      }
    }
    std::sort(faces.begin(), faces.end());
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
    size_t run_start = 0;
    for (size_t i = 1; i <= faces.size(); ++i) {
      if (i == faces.size() || faces[i] != faces[i - 1] + 1) {
        RenderMesh::MaterialSubset ms;
        ms.face_start = faces[run_start];
        ms.face_count = static_cast<uint32_t>(i - run_start);
        ms.material_id = mit->second;
        mesh->material_subsets.push_back(ms);
        run_start = i;
      }
    }
  }

  // Remap subset runs from polygon-face space into TRIANGLE space using
  // the triangulation prefix sums (an N-gon becomes N-2 triangles;
  // holes/degenerate faces contribute 0). Subset indices were already
  // remapped into the post-sanitize face numbering above, so this holds
  // even when sanitization dropped faces.
  if (!mesh->material_subsets.empty() &&
      !mesh->face_triangle_offsets.empty()) {
    const std::vector<uint32_t>& offs = mesh->face_triangle_offsets;
    const uint32_t nfaces_tri =
        static_cast<uint32_t>(offs.size() > 0 ? offs.size() - 1 : 0);
    std::vector<RenderMesh::MaterialSubset> remapped;
    remapped.reserve(mesh->material_subsets.size());
    for (const RenderMesh::MaterialSubset& ms : mesh->material_subsets) {
      if (ms.face_start >= nfaces_tri) continue;
      const uint32_t face_end =
          std::min(ms.face_start + ms.face_count, nfaces_tri);
      const uint32_t tri_start = offs[ms.face_start];
      const uint32_t tri_count = offs[face_end] - tri_start;
      if (tri_count == 0) continue;
      remapped.push_back(
          RenderMesh::MaterialSubset{tri_start, tri_count, ms.material_id});
    }
    mesh->material_subsets = std::move(remapped);
  }
}

int32_t RenderSceneConverter::GetOrCreateDefaultMaterial(RenderScene* scene) {
  // Same sentinel path as the legacy converter.
  constexpr const char* kDefaultMaterialPath = "/__tinyusdz_default_material__";
  const auto it = scene->material_by_path.find(kDefaultMaterialPath);
  if (it != scene->material_by_path.end()) return it->second;
  RenderMaterial material;
  material.name = config_.material.default_material_name.empty()
                      ? "defaultMaterial"
                      : config_.material.default_material_name;
  material.prim_path = kDefaultMaterialPath;
  material.shader_type = RenderMaterial::ShaderType::PreviewSurface;
  // PreviewSurfaceShader defaults (0.18 diffuse / 0.5 roughness / opaque)
  // match the legacy default material parameters.
  material.preview_surface = std::make_unique<PreviewSurfaceShader>();
  const int32_t id = static_cast<int32_t>(scene->materials.size());
  scene->materials.push_back(std::move(material));
  scene->material_by_path[kDefaultMaterialPath] = id;
  return id;
}

void RenderSceneConverter::AssignPointInstanceDrawMaterials(RenderScene* scene) {
  if (!scene) return;
  for (RenderPointInstanceDraw& draw : scene->point_instance_draws) {
    if (draw.mesh_id < 0 ||
        static_cast<size_t>(draw.mesh_id) >= scene->meshes.size()) {
      draw.material_id = -1;
      continue;
    }
    draw.material_id = scene->meshes[static_cast<size_t>(draw.mesh_id)].material_id;
  }
}

void RenderSceneConverter::DuplicatePointInstanceMeshes(RenderScene* scene) {
  if (!scene) return;
  const size_t draw_count = scene->point_instance_draws.size();
  for (size_t draw_id = 0; draw_id < draw_count; ++draw_id) {
    RenderPointInstanceDraw& draw = scene->point_instance_draws[draw_id];
    if (draw.expanded_mesh_id >= 0) continue;
    const RenderMesh* src = scene->get_mesh(draw.mesh_id);
    if (!src) continue;

    RenderMesh expanded;
    if (!CloneMeshForPointInstance(*src, draw, &expanded)) {
      continue;
    }
    const int32_t mesh_id = static_cast<int32_t>(scene->meshes.size());
    scene->mesh_by_path[expanded.prim_path] = mesh_id;
    scene->meshes.push_back(std::move(expanded));
    draw.expanded_mesh_id = mesh_id;
  }
}

//
// Mesh conversion
//

bool RenderSceneConverter::ConvertRenderableMesh(const Stage& stage,
                                                 const UsdPrim& prim,
                                                 RenderMesh* out) {
  if (!prim.IsValid() || !IsMeshRenderableTypeName(prim.GetTypeName())) {
    last_error_ = "Invalid renderable mesh prim";
    return false;
  }
  return prim.GetTypeName() == "Mesh" ? ConvertMesh(stage, prim, out)
                                      : ConvertGeomPrimitive(prim, out);
}

bool RenderSceneConverter::ConvertGeomPrimitive(const UsdPrim& prim,
                                                RenderMesh* out) {
  if (!out || !prim.IsValid() ||
      (!IsAnalyticGeomTypeName(prim.GetTypeName()) &&
       prim.GetTypeName() != "TetMesh")) {
    last_error_ = "Invalid generated geom prim";
    return false;
  }

  std::vector<value::float3> points;
  std::vector<int> face_counts;
  std::vector<int> face_indices;
  std::vector<value::float3> normals;
  std::vector<value::float2> uvs;

  const std::string type = prim.GetTypeName();
  if (type == "TetMesh") {
    ValueArrayRead<float> authored_points;
    ValueArrayRead<int32_t> authored_tets;
    if (!ReadFloatArray(prim, "points", config_.time_code, &authored_points) ||
        authored_points.empty() || (authored_points.view.size % 3) != 0 ||
        !ReadIntArray(prim, "tetVertexIndices", config_.time_code,
                      &authored_tets) ||
        authored_tets.empty() || (authored_tets.view.size % 4) != 0) {
      last_error_ = "Invalid TetMesh points or tetVertexIndices";
      return false;
    }

    const size_t point_count = authored_points.view.size / 3;
    points.reserve(point_count);
    for (size_t i = 0; i < point_count; ++i) {
      points.push_back(value::float3{authored_points.view.data[i * 3 + 0],
                                     authored_points.view.data[i * 3 + 1],
                                     authored_points.view.data[i * 3 + 2]});
    }

    using FaceKey = std::array<int32_t, 3>;
    struct BoundaryFace {
      FaceKey key{};
      FaceKey oriented{};
    };
    const size_t tet_count = authored_tets.view.size / 4;
    if (tet_count > (kMaxTempAllocBytes / (4 * sizeof(BoundaryFace)))) {
      last_error_ = "TetMesh boundary extraction exceeds temporary-memory cap";
      return false;
    }
    std::vector<BoundaryFace> faces;
    faces.reserve(tet_count * 4);
    for (size_t tet = 0; tet < tet_count; ++tet) {
      const int32_t* v = authored_tets.view.data + tet * 4;
      bool valid = true;
      for (size_t corner = 0; corner < 4; ++corner) {
        if (v[corner] < 0 || static_cast<size_t>(v[corner]) >= point_count) {
          valid = false;
        }
      }
      if (!valid || v[0] == v[1] || v[0] == v[2] || v[0] == v[3] ||
          v[1] == v[2] || v[1] == v[3] || v[2] == v[3]) {
        warnings_.push_back("TetMesh '" + prim.GetPath().str() +
                            "': skipped malformed tetrahedron " +
                            std::to_string(tet));
        continue;
      }
      const FaceKey oriented[4] = {{v[0], v[2], v[1]}, {v[0], v[1], v[3]},
                                   {v[0], v[3], v[2]}, {v[1], v[2], v[3]}};
      for (const FaceKey& face : oriented) {
        FaceKey key = face;
        std::sort(key.begin(), key.end());
        faces.push_back(BoundaryFace{key, face});
      }
    }
    std::sort(faces.begin(), faces.end(),
              [](const BoundaryFace& a, const BoundaryFace& b) {
                return a.key < b.key;
              });
    for (size_t begin = 0; begin < faces.size();) {
      size_t end = begin + 1;
      while (end < faces.size() && faces[end].key == faces[begin].key) ++end;
      if (end == begin + 1) {
        face_counts.push_back(3);
        face_indices.insert(face_indices.end(),
                            faces[begin].oriented.begin(),
                            faces[begin].oriented.end());
      }
      begin = end;
    }
    if (face_counts.empty()) {
      last_error_ = "TetMesh has no valid boundary faces";
      return false;
    }
  } else if (type == "Cube") {
    ::tinyusdz::tydra::GenerateCubeMesh(
        ReadDoubleProperty(prim, "size", 2.0), points, face_counts,
        face_indices, normals, uvs);
  } else if (type == "Sphere") {
    ::tinyusdz::tydra::GenerateIcosphereMesh(
        // USD's Sphere.radius default is 1 (Cube.size is 2). Normally the
        // schema registry supplies it and this fallback never fires.
        ReadDoubleProperty(prim, "radius", 1.0),
        std::max(0, std::min(config_.mesh.sphere_subdivisions, 6)), points,
        face_counts,
        face_indices, normals, uvs);
  } else if (type == "Cylinder" || type == "Cylinder_1") {
    double radius = ReadDoubleProperty(prim, "radius", 1.0);
    if (type == "Cylinder_1") {
      const double rt = ReadDoubleProperty(prim, "radiusTop", 1.0);
      const double rb = ReadDoubleProperty(prim, "radiusBottom", 1.0);
      radius = std::max(rt, rb);
      if (std::fabs(rt - rb) > 1.0e-9) {
        warnings_.push_back("Cylinder_1 '" + prim.GetPath().str() +
                            "': tapered radii are approximated with max radius");
      }
    }
    ::tinyusdz::tydra::GenerateCylinderMesh(
        radius, ReadDoubleProperty(prim, "height", 2.0), 24, 1, points,
        face_counts, face_indices, normals, uvs);
  } else if (type == "Cone") {
    ::tinyusdz::tydra::GenerateConeMesh(
        ReadDoubleProperty(prim, "radius", 1.0),
        ReadDoubleProperty(prim, "height", 2.0), 24, points, face_counts,
        face_indices, normals, uvs);
  } else if (type == "Capsule" || type == "Capsule_1") {
    double radius = ReadDoubleProperty(prim, "radius", 0.5);
    double height = ReadDoubleProperty(prim, "height", type == "Capsule_1" ? 1.0 : 2.0);
    if (type == "Capsule_1") {
      const double rt = ReadDoubleProperty(prim, "radiusTop", 0.5);
      const double rb = ReadDoubleProperty(prim, "radiusBottom", 0.5);
      radius = std::max(rt, rb);
      if (std::fabs(rt - rb) > 1.0e-9) {
        warnings_.push_back("Capsule_1 '" + prim.GetPath().str() +
                            "': asymmetric radii are approximated with max radius");
      }
    }
    ::tinyusdz::tydra::GenerateCapsuleMesh(radius, height, 24, 1, points,
                                           face_counts, face_indices, normals,
                                           uvs);
  } else if (type == "Plane") {
    ::tinyusdz::tydra::GeneratePlaneMesh(
        ReadDoubleProperty(prim, "width", 2.0),
        ReadDoubleProperty(prim, "length", 2.0), 1, 1, points, face_counts,
        face_indices, normals, uvs);
  } else {
    last_error_ = "Unsupported analytic geom prim";
    return false;
  }

  // The shape generators are inconsistent about winding: capsule/cone emit
  // INWARD (negative signed volume) faces while cube/sphere/cylinder are
  // outward. The old axis MIRROR happened to flip capsule/cone right side
  // out at the default Z axis (while turning the cylinder inside out); with
  // the axis applied as a proper rotation below, normalize the winding here
  // so every closed solid is outward. Authored normals already point
  // outward, so only the corner order flips (normals/uvs are per-corner and
  // reverse with it to stay parallel).
  {
    double volume = 0.0;
    size_t off = 0;
    for (int c : face_counts) {
      if (c < 3 || off + size_t(c) > face_indices.size()) break;
      const value::float3& a = points[size_t(face_indices[off])];
      for (int k = 1; k + 1 < c; ++k) {
        const value::float3& b = points[size_t(face_indices[off + size_t(k)])];
        const value::float3& d =
            points[size_t(face_indices[off + size_t(k) + 1])];
        volume += (double(a[0]) * (double(b[1]) * d[2] - double(b[2]) * d[1]) +
                   double(a[1]) * (double(b[2]) * d[0] - double(b[0]) * d[2]) +
                   double(a[2]) * (double(b[0]) * d[1] - double(b[1]) * d[0])) /
                  6.0;
      }
      off += size_t(c);
    }
    if (volume < -1.0e-9) {
      size_t start = 0;
      for (int c : face_counts) {
        if (c <= 0 || start + size_t(c) > face_indices.size()) break;
        std::reverse(face_indices.begin() + long(start),
                     face_indices.begin() + long(start + size_t(c)));
        if (normals.size() >= start + size_t(c)) {
          std::reverse(normals.begin() + long(start),
                       normals.begin() + long(start + size_t(c)));
        }
        if (uvs.size() >= start + size_t(c)) {
          std::reverse(uvs.begin() + long(start),
                       uvs.begin() + long(start + size_t(c)));
        }
        start += size_t(c);
      }
    }
  }

  if (type != "TetMesh") {
    std::string axis = "Z";
    GetToken(prim, "axis", &axis);
    if (type == "Cube" || type == "Sphere") axis = "Y";
    ApplyAxis(&points, &normals, axis);
  }
  FillGeneratedMesh(prim, points, face_counts, face_indices, normals, uvs, out);
  SanitizeMeshTopology(out);
  if (config_.mesh.triangulate && !out->is_triangulated) {
    TriangulateMesh(out);
  }
  if (config_.mesh.compute_normals && out->normals.empty()) {
    ComputeVertexNormals(out);
  }
  if (config_.mesh.compute_tangents && out->tangents.empty()) {
    ComputeVertexTangents(out);
  }
  return true;
}

bool RenderSceneConverter::ConvertMesh(const Stage& stage, const UsdPrim& prim, RenderMesh* out) {
  if (!out || !IsMesh(prim)) {
    last_error_ = "Invalid mesh prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Extract topology
  if (!ExtractMeshTopology(prim, out)) {
    return false;
  }

  // Extract geometry
  if (!ExtractMeshGeometry(prim, out)) {
    return false;
  }

  // Sanitize topology BEFORE any consumer walks it: negative / out-of-range
  // faceVertexIndices previously flowed into normal generation and the output
  // buffers (a negative index casts to ~4 billion -> segfault; an OOB index
  // hands the renderer an out-of-bounds read). Drop offending faces with a
  // warning; also truncate a counts list that overruns the index buffer.
  SanitizeMeshTopology(out);

  // Extract render vertex attributes only when the caller retains geometry.
  // Metadata-only consumers source these arrays elsewhere; decoding/copying
  // large face-varying normals and UVs here would be pure transient overhead.
  if (config_.mesh.retain_geometry) {
    ExtractMeshPrimvars(prim, out);
  }

  // Skinning binding (skel:skeleton + skel:jointIndices/Weights primvars).
  {
    SkinBindingInfo sb;
    const bool has_skin_binding = GetSkinBinding(prim, &sb);
    if (has_skin_binding && !sb.joint_indices.empty() &&
        sb.joint_indices.size() != sb.joint_weights.size()) {
      // e.g. one indexed skin primvar expanded while its pair stayed
      // authored (malformed :indices). Skipping silently leaves the mesh in
      // bind pose with no hint why.
      warnings_.push_back("Mismatched skin jointIndices/jointWeights sizes on " +
                          prim.GetPath().str() + "; mesh renders unskinned");
    }
    if (has_skin_binding && !sb.joint_indices.empty() &&
        sb.joint_indices.size() == sb.joint_weights.size()) {
      // UsdSkel binding inheritance: `skel:skeleton` may be authored on an
      // ancestor (typically the enclosing SkelRoot) rather than on the mesh
      // itself, in which case every descendant skinnable prim inherits it.
      // GetSkinBinding only reads the mesh prim, so walk up to the nearest
      // ancestor binding when the mesh doesn't author one directly. Without
      // this, meshes that bind their skeleton at the SkelRoot (e.g. the
      // MetaHuman standalone face/body exports) resolve no skeleton_id and
      // render unskinned in bind pose.
      if (sb.skeleton_path.empty()) {
        UsdPrim anc = GetParent(stage, prim);
        UsdPrim skel_root;
        while (anc.IsValid()) {
          std::string inherited = GetBoundSkeleton(anc);
          if (!inherited.empty()) {
            sb.skeleton_path = inherited;
            break;
          }
          // Binding inheritance is scoped to the SkelRoot subtree.
          if (::tinyusdz::tydra::next::IsSkelRoot(anc)) {
            skel_root = anc;
            break;
          }
          anc = GetParent(stage, anc);
        }
        // No authored binding anywhere: fall back to a Skeleton contained
        // in the enclosing SkelRoot (common Blender / older-exporter shape;
        // legacy tydra binds this way too). First Skeleton in the subtree
        // wins, matching legacy.
        if (sb.skeleton_path.empty() && skel_root.IsValid()) {
          for (const UsdPrim& desc : GetDescendants(skel_root)) {
            if (::tinyusdz::tydra::next::IsSkeleton(desc)) {
              sb.skeleton_path = desc.GetPath().str();
              break;
            }
          }
        }
      }
      const size_t point_count = out->point_count();
      size_t influences = sb.influences_per_vertex > 0
                              ? static_cast<size_t>(sb.influences_per_vertex)
                              : 0;
      if (influences == 0 && point_count > 0 &&
          (sb.joint_indices.size() % point_count) == 0) {
        influences = sb.joint_indices.size() / point_count;
      }
      if (influences > 0 && point_count > 1 &&
          sb.joint_indices.size() == influences) {
        const std::vector<int32_t> indices = sb.joint_indices;
        const std::vector<float> weights = sb.joint_weights;
        sb.joint_indices.clear();
        sb.joint_weights.clear();
        sb.joint_indices.reserve(point_count * influences);
        sb.joint_weights.reserve(point_count * influences);
        for (size_t point = 0; point < point_count; ++point) {
          sb.joint_indices.insert(sb.joint_indices.end(), indices.begin(),
                                  indices.end());
          sb.joint_weights.insert(sb.joint_weights.end(), weights.begin(),
                                  weights.end());
        }
      }
      if (influences == 0 || point_count == 0 ||
          sb.joint_indices.size() != point_count * influences) {
        warnings_.push_back("Ignoring malformed skin influences on " +
                            prim.GetPath().str());
      } else {
        size_t output_influences = influences;
        std::vector<int32_t> reduced_indices;
        std::vector<float> reduced_weights;
        if (config_.mesh.enable_bone_reduction &&
            config_.mesh.target_bone_count > 0 &&
            config_.mesh.target_bone_count < influences &&
            // ~8B per point-influence pair of temporaries; on a nearly-full
            // heap keep the authored influences instead of abort()ing.
            !WouldOverflowSizeMul(point_count, output_influences * 8) &&
            ProbeAlloc(point_count * config_.mesh.target_bone_count * 8)) {
          output_influences = config_.mesh.target_bone_count;
          reduced_indices.resize(point_count * output_influences, 0);
          reduced_weights.resize(point_count * output_influences, 0.0f);
          std::vector<std::pair<float, int32_t>> ranked(influences);
          for (size_t point = 0; point < point_count; ++point) {
            const size_t source = point * influences;
            for (size_t i = 0; i < influences; ++i) {
              ranked[i] = {sb.joint_weights[source + i],
                           sb.joint_indices[source + i]};
            }
            std::stable_sort(
                ranked.begin(), ranked.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
            float sum = 0.0f;
            for (size_t i = 0; i < output_influences; ++i) {
              sum += std::max(0.0f, ranked[i].first);
            }
            for (size_t i = 0; i < output_influences; ++i) {
              const size_t destination = point * output_influences + i;
              reduced_indices[destination] = ranked[i].second;
              reduced_weights[destination] =
                  sum > 0.0f ? std::max(0.0f, ranked[i].first) / sum
                             : (i == 0 ? 1.0f : 0.0f);
            }
          }
          sb.joint_indices = std::move(reduced_indices);
          sb.joint_weights = std::move(reduced_weights);
        }

      out->skin = std::make_unique<RenderMesh::SkinBinding>();
      out->skin->joint_indices.reserve(sb.joint_indices.size());
      for (int32_t ji : sb.joint_indices) {
        out->skin->joint_indices.push_back(
            ji < 0 ? uint16_t(0)
                   : static_cast<uint16_t>(std::min<int32_t>(ji, 65535)));
      }
      out->skin->joint_weights.append(sb.joint_weights.data(),
                                      sb.joint_weights.size());
      out->skin->influences_per_vertex =
          static_cast<uint32_t>(output_influences);
      out->skin->mesh_joint_order = std::move(sb.joint_order);
      std::memcpy(out->skin->geom_bind_transform.m, sb.geom_bind_transform,
                  sizeof(sb.geom_bind_transform));
      // skeleton_id is resolved by the caller once skeletons are converted
      // (stored in skin->skeleton_id via the path recorded here).
      out->skin->skeleton_path = sb.skeleton_path;
      }
    }
  }

  // Blend shapes (skel:blendShapes names + skel:blendShapeTargets prims).
  for (const BlendShapeInfo& bs : GetBlendShapes(prim)) {
    UsdPrim bs_prim = stage.GetPrimAtPath(bs.path);
    if (!bs_prim.IsValid()) continue;
    ::tinyusdz::next::BlendShapeData bd;
    if (!::tinyusdz::next::GetBlendShapeData(stage, bs_prim, &bd)) continue;
    if (bd.offsets.empty()) continue;
    RenderMesh::BlendShape shape;
    shape.name = bs.name.empty() ? bs_prim.GetName() : bs.name;

    // Sparse targets: offsets[k] (and normalOffsets[k] / in-between
    // offsets[k]) apply to point pointIndices[k]. An out-of-range index must
    // drop the WHOLE parallel entry — dropping it from point_indices alone
    // (the previous behavior) misaligned every remaining offset.
    std::vector<size_t> kept;  // kept entry positions (sparse targets only)
    const size_t authored_entries = bd.offsets.size() / 3;
    if (bd.hasPointIndices) {
      const size_t npts = out->point_count();
      const size_t nentries =
          std::min(bd.pointIndices.size(), authored_entries);
      kept.reserve(nentries);
      for (size_t k = 0; k < nentries; ++k) {
        const int32_t pi = bd.pointIndices[k];
        if (pi >= 0 && static_cast<size_t>(pi) < npts) {
          kept.push_back(k);
        }
      }
      if (kept.size() != bd.pointIndices.size()) {
        warnings_.push_back("BlendShape '" + bs_prim.GetPath().str() +
                            "': dropped out-of-range pointIndices entries "
                            "(with their parallel offsets)");
      }
    }

    // Copy 3-float entries at the kept positions of a parallel array.
    auto append_kept = [&kept](const std::vector<float>& src,
                               FloatChunked* dst) {
      for (size_t k : kept) {
        dst->push_back(src[k * 3 + 0]);
        dst->push_back(src[k * 3 + 1]);
        dst->push_back(src[k * 3 + 2]);
      }
    };

    if (bd.hasPointIndices) {
      append_kept(bd.offsets, &shape.point_offsets);
      if (bd.hasNormalOffsets &&
          bd.normalOffsets.size() == bd.offsets.size()) {
        append_kept(bd.normalOffsets, &shape.normal_offsets);
      }
      shape.point_indices.reserve(kept.size());
      for (size_t k : kept) {
        shape.point_indices.push_back(
            static_cast<uint32_t>(bd.pointIndices[k]));
      }
    } else {
      shape.point_offsets.append(bd.offsets.data(), bd.offsets.size());
      if (bd.hasNormalOffsets && !bd.normalOffsets.empty()) {
        shape.normal_offsets.append(bd.normalOffsets.data(),
                                    bd.normalOffsets.size());
      }
    }
    for (const ::tinyusdz::next::BlendShapeData::Inbetween& source :
         bd.inbetweens) {
      if (source.offsets.size() != bd.offsets.size()) {
        warnings_.push_back("Ignoring malformed in-between '" + source.name +
                            "' on " + bs_prim.GetPath().str());
        continue;
      }
      if (!source.has_weight) {
        // A weightless in-between would sit at 0.0 and collide with the base
        // shape; legacy tydra skips these too.
        warnings_.push_back("In-between '" + source.name + "' on " +
                            bs_prim.GetPath().str() +
                            " has no authored weight; skipped");
        continue;
      }
      RenderMesh::BlendShape::Inbetween inbetween;
      inbetween.name = source.name;
      inbetween.weight = source.weight;
      if (bd.hasPointIndices) {
        append_kept(source.offsets, &inbetween.point_offsets);
      } else {
        inbetween.point_offsets.append(source.offsets.data(),
                                       source.offsets.size());
      }
      shape.inbetweens.push_back(std::move(inbetween));
    }
    out->blend_shapes.push_back(std::move(shape));
  }

  // Triangulate if requested. A mesh whose faces were all sanitized away is
  // still a valid (empty) render mesh; only meshes with real topology that
  // cannot be triangulated (e.g. over the temp-allocation budget) are dropped.
  if ((config_.mesh.retain_geometry || config_.mesh.retain_triangulation) &&
      config_.mesh.triangulate &&
      !out->is_triangulated) {
    if (!TriangulateMesh(out) && !out->face_vertex_counts.empty()) {
      warnings_.push_back("Failed to triangulate mesh '" + out->prim_path +
                          "'; skipping it to avoid conversion abort");
      return false;
    }
  }

  // Compute normals if needed
  if (config_.mesh.retain_geometry && config_.mesh.compute_normals &&
      out->normals.empty()) {
    ComputeVertexNormals(out);
  }

  // Compute tangents if requested (needs triangles, per-vertex normals and
  // per-vertex UVs).
  if (config_.mesh.retain_geometry && config_.mesh.compute_tangents &&
      out->tangents.empty()) {
    ComputeVertexTangents(out);
  }

  // A chunk allocation may have failed anywhere above (nothrow growth): the
  // mesh data is truncated, so report and drop the prim instead of rendering
  // partial geometry (or aborting the module, as a throwing new would under
  // -fno-exceptions).
  if (out->has_alloc_failure()) {
    warnings_.push_back("Out of memory converting mesh '" + out->prim_path +
                        "'; the prim was skipped");
    return false;
  }

  return true;
}

// Tangent frame from triangulated topology. Lengyel keeps the compact
// per-vertex path. MikkTSpace-style methods expand to face corners first so
// UV seams and mirrored islands are not averaged through shared point indices.
bool RenderSceneConverter::ComputeVertexTangents(RenderMesh* mesh) {
  if (!mesh->is_triangulated) {
    if (!TriangulateMesh(mesh)) return false;
  }
  const size_t np = mesh->point_count();
  if (np == 0) return false;

  const bool vertex_normals =
      mesh->normals_interp == Interpolation::Vertex &&
      mesh->normals.size() == np * 3;
  const bool vertex_uvs =
      mesh->texcoords_0_interp == Interpolation::Vertex &&
      mesh->texcoords_0.size() == np * 2;

  const size_t authored_corner_count = mesh->face_vertex_indices.size();
  const size_t tri_corner_count = mesh->triangulated_indices.size();
  const bool facevarying_normals =
      mesh->normals_interp == Interpolation::FaceVarying &&
      (mesh->normals.size() == authored_corner_count * 3 ||
       mesh->normals.size() == tri_corner_count * 3);
  const bool facevarying_uvs =
      mesh->texcoords_0_interp == Interpolation::FaceVarying &&
      (mesh->texcoords_0.size() == authored_corner_count * 2 ||
       mesh->texcoords_0.size() == tri_corner_count * 2);

  if ((!vertex_normals && !facevarying_normals) ||
      (!vertex_uvs && !facevarying_uvs)) {
    return false;
  }

  // Pre-flight the temporary buffers. The corner-expanded MikkTSpace path holds
  // positions(12) + normals(12) + uvs(8) + tri_counts(~1.3) + tangents(12) +
  // binormals(12) simultaneously across the tangent call = ~58 B per
  // triangulated corner; kMikkBytesPerCorner adds margin for the fv_out(16)
  // phase, which overlaps only partially because the dead inputs are released
  // first (see below). The old estimate of 64 both understated the peak and
  // probed a single block rather than the sum. The Lengyel path is ~40B per
  // point. A failed probe skips tangents for this mesh (they are optional)
  // instead of abort()ing the module under -fno-exceptions.
  constexpr size_t kMikkBytesPerCorner = 80;
  const size_t probe_bytes =
      (config_.mesh.tangent_method ==
           MeshConfig::TangentComputationMethod::Lengyel &&
       vertex_normals && vertex_uvs)
          ? np * (3 + 3 + 4) * sizeof(float)
          : tri_corner_count * kMikkBytesPerCorner;
  if (WouldOverflowSizeMul(tri_corner_count, kMikkBytesPerCorner) ||
      !ProbeAlloc(probe_bytes)) {
    warnings_.push_back("Out of memory computing tangents for mesh '" +
                        mesh->prim_path + "'; tangents skipped");
    return false;
  }

  std::vector<float> tan(np * 3, 0.0f);
  std::vector<float> bit(np * 3, 0.0f);
  const size_t ntris = mesh->triangulated_indices.size() / 3;
  if (config_.mesh.tangent_method == MeshConfig::TangentComputationMethod::Lengyel &&
      vertex_normals && vertex_uvs) {
  for (size_t t = 0; t < ntris; ++t) {
    const uint32_t i0 = mesh->triangulated_indices[t * 3 + 0];
    const uint32_t i1 = mesh->triangulated_indices[t * 3 + 1];
    const uint32_t i2 = mesh->triangulated_indices[t * 3 + 2];
    if (i0 >= np || i1 >= np || i2 >= np) continue;
    // Copy out, never `&chunked[i]` + offset: an xyz/uv run straddles the
    // chunk boundary (see ChunkedArray::read_n).
    float p0[3], p1[3], p2[3], u0[2], u1[2], u2[2];
    mesh->points.read3(i0 * 3, p0);
    mesh->points.read3(i1 * 3, p1);
    mesh->points.read3(i2 * 3, p2);
    mesh->texcoords_0.read2(i0 * 2, u0);
    mesh->texcoords_0.read2(i1 * 2, u1);
    mesh->texcoords_0.read2(i2 * 2, u2);
    const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    const float du1 = u1[0] - u0[0], dv1 = u1[1] - u0[1];
    const float du2 = u2[0] - u0[0], dv2 = u2[1] - u0[1];
    const float det = du1 * dv2 - du2 * dv1;
    const float r = (std::fabs(det) > 1e-12f) ? 1.0f / det : 0.0f;
    const float sdir[3] = {(dv2 * e1[0] - dv1 * e2[0]) * r,
                           (dv2 * e1[1] - dv1 * e2[1]) * r,
                           (dv2 * e1[2] - dv1 * e2[2]) * r};
    const float tdir[3] = {(du1 * e2[0] - du2 * e1[0]) * r,
                           (du1 * e2[1] - du2 * e1[1]) * r,
                           (du1 * e2[2] - du2 * e1[2]) * r};
    for (uint32_t vi : {i0, i1, i2}) {
      tan[vi * 3 + 0] += sdir[0];
      tan[vi * 3 + 1] += sdir[1];
      tan[vi * 3 + 2] += sdir[2];
      bit[vi * 3 + 0] += tdir[0];
      bit[vi * 3 + 1] += tdir[1];
      bit[vi * 3 + 2] += tdir[2];
    }
  }

  std::vector<float> out_tan(np * 4, 0.0f);
  for (size_t v = 0; v < np; ++v) {
    float n[3];
    mesh->normals.read3(v * 3, n);
    const float* tv = &tan[v * 3];
    // Gram-Schmidt orthogonalize t against n.
    const float ndt = n[0] * tv[0] + n[1] * tv[1] + n[2] * tv[2];
    float tx = tv[0] - n[0] * ndt;
    float ty = tv[1] - n[1] * ndt;
    float tz = tv[2] - n[2] * ndt;
    const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 1e-12f) {
      tx /= len; ty /= len; tz /= len;
    } else {
      tx = 1.0f; ty = 0.0f; tz = 0.0f;
    }
    // Handedness: sign of dot(cross(n, t), bitangent).
    const float* bv = &bit[v * 3];
    const float cx = n[1] * tz - n[2] * ty;
    const float cy = n[2] * tx - n[0] * tz;
    const float cz = n[0] * ty - n[1] * tx;
    const float w = (cx * bv[0] + cy * bv[1] + cz * bv[2]) < 0.0f ? -1.0f : 1.0f;
    out_tan[v * 4 + 0] = tx;
    out_tan[v * 4 + 1] = ty;
    out_tan[v * 4 + 2] = tz;
    out_tan[v * 4 + 3] = w;
  }
  mesh->tangents.clear();
  mesh->tangents.append(out_tan.data(), out_tan.size());
  mesh->tangents_interp = Interpolation::Vertex;
  return true;
  }

  std::vector<value::float3> fv_positions(tri_corner_count);
  std::vector<value::float3> fv_normals(tri_corner_count);
  std::vector<value::float2> fv_uvs(tri_corner_count);
  std::vector<uint32_t> tri_counts(ntris, 3);

  const bool tri_corner_remap =
      mesh->triangulated_face_vertex_indices.size() == tri_corner_count;

  for (size_t c = 0; c < tri_corner_count; ++c) {
    const uint32_t point_id = mesh->triangulated_indices[c];
    if (point_id >= np) return false;

    const size_t authored_corner =
        tri_corner_remap ? mesh->triangulated_face_vertex_indices[c] : c;

    const size_t p3 = size_t(point_id) * 3;
    fv_positions[c] = {mesh->points[p3 + 0], mesh->points[p3 + 1],
                       mesh->points[p3 + 2]};

    size_t nidx = 0;
    if (vertex_normals) {
      nidx = size_t(point_id);
    } else if (mesh->normals.size() == tri_corner_count * 3) {
      nidx = c;
    } else {
      if (authored_corner >= authored_corner_count) return false;
      nidx = authored_corner;
    }
    const size_t n3 = nidx * 3;
    if (n3 + 2 >= mesh->normals.size()) return false;
    fv_normals[c] = {mesh->normals[n3 + 0], mesh->normals[n3 + 1],
                     mesh->normals[n3 + 2]};

    size_t uvidx = 0;
    if (vertex_uvs) {
      uvidx = size_t(point_id);
    } else if (mesh->texcoords_0.size() == tri_corner_count * 2) {
      uvidx = c;
    } else {
      if (authored_corner >= authored_corner_count) return false;
      uvidx = authored_corner;
    }
    const size_t uv2 = uvidx * 2;
    if (uv2 + 1 >= mesh->texcoords_0.size()) return false;
    fv_uvs[c] = {mesh->texcoords_0[uv2 + 0], mesh->texcoords_0[uv2 + 1]};
  }

  std::vector<value::float3> fv_tangents;
  std::vector<value::float3> fv_binormals;
  std::string tangent_error;
  bool tangent_ok = false;

  switch (config_.mesh.tangent_method) {
    case MeshConfig::TangentComputationMethod::MikkTSpace:
      tangent_ok = ::tinyusdz::tydra::ComputeTangentsMikkTSpace(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, &tangent_error);
      break;
    case MeshConfig::TangentComputationMethod::FastMikkTSpace:
      tangent_ok = ::tinyusdz::tydra::fast_mikkt::ComputeTangentsFastMikkTSpace(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, &tangent_error);
      break;
    case MeshConfig::TangentComputationMethod::Hybrid: {
      ::tinyusdz::tydra::fast_mikkt::HybridStats stats = {};
      tangent_ok = ::tinyusdz::tydra::fast_mikkt::ComputeTangentsHybrid(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, &stats, &tangent_error);
      break;
    }
    case MeshConfig::TangentComputationMethod::Lengyel:
      // Face-varying Lengyel is intentionally not duplicated here; Hybrid is
      // the O(n) seam-aware fallback for non-vertex data.
      tangent_ok = ::tinyusdz::tydra::fast_mikkt::ComputeTangentsHybrid(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, nullptr, &tangent_error);
      break;
  }

  if (!tangent_ok || fv_tangents.size() != tri_corner_count ||
      fv_binormals.size() != tri_corner_count) {
    if (!tangent_error.empty()) {
      warnings_.push_back("Tangent computation failed for mesh '" +
                          mesh->prim_path + "': " + tangent_error);
    }
    return false;
  }

  // Positions, UVs and the face-size list are dead once the tangent call
  // returns; release them BEFORE allocating fv_out so the two do not stack
  // (24 B/corner, i.e. ~720 MB on a 30M-corner mesh).
  { std::vector<value::float3>().swap(fv_positions); }
  { std::vector<value::float2>().swap(fv_uvs); }
  { std::vector<uint32_t>().swap(tri_counts); }

  std::vector<float> fv_out(tri_corner_count * 4, 0.0f);
  for (size_t i = 0; i < tri_corner_count; ++i) {
    const value::float3& n = fv_normals[i];
    const value::float3& t = fv_tangents[i];
    const value::float3& b = fv_binormals[i];
    const float cx = n[1] * t[2] - n[2] * t[1];
    const float cy = n[2] * t[0] - n[0] * t[2];
    const float cz = n[0] * t[1] - n[1] * t[0];
    const float sign =
        (cx * b[0] + cy * b[1] + cz * b[2]) < 0.0f ? -1.0f : 1.0f;
    fv_out[i * 4 + 0] = t[0];
    fv_out[i * 4 + 1] = t[1];
    fv_out[i * 4 + 2] = t[2];
    fv_out[i * 4 + 3] = sign;
  }

  // Same again: normals/tangents/binormals are consumed by the loop above and
  // must not stay resident alongside vertex_out below (36 B/corner).
  { std::vector<value::float3>().swap(fv_normals); }
  { std::vector<value::float3>().swap(fv_tangents); }
  { std::vector<value::float3>().swap(fv_binormals); }

  if (vertex_normals && vertex_uvs) {
    std::vector<float> vertex_out(np * 4, 0.0f);
    std::vector<uint8_t> seen(np, 0);
    for (size_t c = 0; c < tri_corner_count; ++c) {
      const uint32_t point_id = mesh->triangulated_indices[c];
      if (point_id >= np || seen[point_id]) continue;
      seen[point_id] = 1;
      vertex_out[size_t(point_id) * 4 + 0] = fv_out[c * 4 + 0];
      vertex_out[size_t(point_id) * 4 + 1] = fv_out[c * 4 + 1];
      vertex_out[size_t(point_id) * 4 + 2] = fv_out[c * 4 + 2];
      vertex_out[size_t(point_id) * 4 + 3] = fv_out[c * 4 + 3];
    }
    mesh->tangents.clear();
    mesh->tangents.append(vertex_out.data(), vertex_out.size());
    mesh->tangents_interp = Interpolation::Vertex;
  } else {
    mesh->tangents.clear();
    mesh->tangents.append(fv_out.data(), fv_out.size());
    mesh->tangents_interp = Interpolation::FaceVarying;
  }
  return true;
}

void RenderSceneConverter::SanitizeMeshTopology(RenderMesh* mesh) {
  const uint32_t point_count = static_cast<uint32_t>(mesh->point_count());
  const size_t index_count = mesh->face_vertex_indices.size();

  // Fast path: everything consistent.
  bool ok = true;
  size_t need = 0;
  for (uint32_t c : mesh->face_vertex_counts) {
    need += c;
    if (need > index_count) { ok = false; break; }
  }
  if (ok && need <= index_count) {
    for (uint32_t idx : mesh->face_vertex_indices) {
      if (idx >= point_count) { ok = false; break; }
    }
    if (ok && need == index_count) return;
  }

  std::vector<uint32_t> counts;
  std::vector<uint32_t> indices;
  // Authored face index -> post-sanitize face index (-1 = dropped), so
  // consumers of authored face numbering (holeIndices, GeomSubset indices)
  // can be remapped instead of discarded.
  std::vector<int32_t> face_remap(mesh->face_vertex_counts.size(), -1);
  counts.reserve(mesh->face_vertex_counts.size());
  indices.reserve(index_count);
  size_t offset = 0;
  size_t dropped = 0;
  size_t authored_face = 0;
  for (uint32_t c : mesh->face_vertex_counts) {
    if (offset + c > index_count) {
      // counts overrun the index buffer: drop this and all later faces.
      dropped += 1;
      break;
    }
    bool face_ok = true;
    for (uint32_t i = 0; i < c; ++i) {
      // face_vertex_indices is uint32; a negative authored index arrived as a
      // huge value and fails this check too.
      if (mesh->face_vertex_indices[offset + i] >= point_count) {
        face_ok = false;
        break;
      }
    }
    if (face_ok) {
      face_remap[authored_face] = static_cast<int32_t>(counts.size());
      counts.push_back(c);
      for (uint32_t i = 0; i < c; ++i) {
        indices.push_back(mesh->face_vertex_indices[offset + i]);
      }
    } else {
      ++dropped;
    }
    offset += c;
    ++authored_face;
  }
  if (dropped > 0 || indices.size() != index_count ||
      counts.size() != mesh->face_vertex_counts.size()) {
    mesh->sanitize_dropped_faces = static_cast<uint32_t>(
        mesh->face_vertex_counts.size() - counts.size());
    if (mesh->sanitize_dropped_faces > 0) {
      mesh->sanitize_face_remap = std::move(face_remap);
    }
    warnings_.push_back("Mesh '" + mesh->prim_path +
                        "': dropped invalid faces (out-of-range or negative "
                        "faceVertexIndices, or counts overrunning the index "
                        "buffer)");
    mesh->face_vertex_counts.clear();
    mesh->face_vertex_counts.append(counts.data(), counts.size());
    mesh->face_vertex_indices.clear();
    mesh->face_vertex_indices.append(indices.data(), indices.size());

    // holeIndices were read in authored face numbering; remap them so hole
    // faces keep pointing at the same topological faces after the drop.
    if (mesh->sanitize_dropped_faces > 0 && !mesh->hole_faces.empty()) {
      std::vector<uint32_t> remapped_holes;
      remapped_holes.reserve(mesh->hole_faces.size());
      for (uint32_t h : mesh->hole_faces) {
        if (h < mesh->sanitize_face_remap.size() &&
            mesh->sanitize_face_remap[h] >= 0) {
          remapped_holes.push_back(
              static_cast<uint32_t>(mesh->sanitize_face_remap[h]));
        }
      }
      std::sort(remapped_holes.begin(), remapped_holes.end());
      mesh->hole_faces = std::move(remapped_holes);
    }
  }
}

bool RenderSceneConverter::ExtractMeshTopology(const UsdPrim& prim, RenderMesh* mesh) {
  // Get face vertex counts
  ValueArrayRead<int32_t> face_counts;
  ReadIntArray(prim, "faceVertexCounts", config_.time_code, &face_counts);
  if (face_counts.empty()) {
    last_error_ = "Mesh has no faceVertexCounts";
    return false;
  }

  mesh->face_vertex_counts.reserve(face_counts.size());
  for (int32_t c : face_counts) {
    mesh->face_vertex_counts.push_back(static_cast<uint32_t>(c));
  }

  // Get face vertex indices
  ValueArrayRead<int32_t> indices;
  ReadIntArray(prim, "faceVertexIndices", config_.time_code, &indices);
  if (indices.empty()) {
    last_error_ = "Mesh has no faceVertexIndices";
    return false;
  }

  mesh->face_vertex_indices.reserve(indices.size());
  for (int32_t i : indices) {
    mesh->face_vertex_indices.push_back(static_cast<uint32_t>(i));
  }

  std::string orientation;
  if (GetToken(prim, "orientation", &orientation)) {
    mesh->left_handed = (orientation == "leftHanded");
  }

  GetBool(prim, "doubleSided", &mesh->double_sided);

  // holeIndices: face indices excluded from rendering.
  {
    ValueArrayRead<int32_t> holes;
    if (ReadIntArray(prim, "holeIndices", config_.time_code, &holes)) {
      for (int32_t h : holes) {
        if (h >= 0) mesh->hole_faces.push_back(static_cast<uint32_t>(h));
      }
      std::sort(mesh->hole_faces.begin(), mesh->hole_faces.end());
    }
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshGeometry(const UsdPrim& prim, RenderMesh* mesh) {
  ValueArrayRead<float> points;
  ReadFloatArray(prim, "points", config_.time_code, &points);
  if (points.empty()) {
    last_error_ = "Invalid points data";
    return false;
  }

  // Copy directly to chunked array
  mesh->points.append(points.view.data, points.view.size);

  // Compute bounding box
  size_t num_points = mesh->point_count();
  if (num_points > 0) {
    mesh->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    mesh->bbox_max = Float3(-1e30f, -1e30f, -1e30f);

    for (size_t i = 0; i < num_points; ++i) {
      float x = mesh->points[i * 3 + 0];
      float y = mesh->points[i * 3 + 1];
      float z = mesh->points[i * 3 + 2];

      mesh->bbox_min.x = std::min(mesh->bbox_min.x, x);
      mesh->bbox_min.y = std::min(mesh->bbox_min.y, y);
      mesh->bbox_min.z = std::min(mesh->bbox_min.z, z);
      mesh->bbox_max.x = std::max(mesh->bbox_max.x, x);
      mesh->bbox_max.y = std::max(mesh->bbox_max.y, y);
      mesh->bbox_max.z = std::max(mesh->bbox_max.z, z);
    }
    mesh->has_bbox = true;
  }

  // Authored normals are handled in ExtractMeshPrimvars (after topology
  // sanitization, where interpolation metadata and element-count validation
  // live).

  return true;
}

namespace {

Interpolation ParsePrimvarInterp(const std::string& s) {
  if (s == "constant") return Interpolation::Constant;
  if (s == "uniform") return Interpolation::Uniform;
  if (s == "faceVarying") return Interpolation::FaceVarying;
  if (s == "varying") return Interpolation::Varying;
  return Interpolation::Vertex;
}

// Flatten a primvar Value into floats (float/half/double backed, any comps).
// Returns comps per element (0 = unsupported/absent).
// `*view` is set to the float array to read from: the SOURCE array itself when
// the primvar is already float-backed (no copy), otherwise `scratch` holding
// the converted values. Copying unconditionally cost a full extra transient of
// every UV/color/normal primvar (40 MB for a 5M-vertex `st` set) on top of the
// copies the caller already makes.
uint32_t PrimvarToFloats(const Value& v, std::vector<float>* scratch,
                         const std::vector<float>** view) {
  if (!v.is_array()) return 0;
  const uint32_t comps =
      static_cast<uint32_t>(GetComponentCount(v.type_id()));
  if (comps == 0) return 0;
  if (const std::vector<float>* fa = v.as_float_array()) {
    *view = fa;
    return comps;
  }
  if (const std::vector<double>* da = v.as_double_array()) {
    scratch->reserve(da->size());
    for (double d : *da) scratch->push_back(static_cast<float>(d));
    *view = scratch;
    return comps;
  }
  return 0;
}

}  // namespace

bool RenderSceneConverter::ExtractMeshPrimvars(const UsdPrim& prim, RenderMesh* mesh) {
  std::vector<Primvar> primvars = GetPrimvars(prim);

  // The primary UV set is the first configured name this mesh actually authors.
  // Without the fallback, a Blender-exported "UVMap" mesh reads as having no UVs
  // at all.
  std::string uv_base;
  for (const std::string& candidate : config_.mesh.uv_primvar_names) {
    for (const Primvar& pv : primvars) {
      if (pv.name == candidate) {
        uv_base = candidate;
        break;
      }
    }
    if (!uv_base.empty()) break;
  }
  if (uv_base.empty()) {
    uv_base = config_.mesh.uv_primvar_names.empty()
                  ? std::string("st")
                  : config_.mesh.uv_primvar_names.front();
  }

  // The SECONDARY UV set. This used to be hard-coded to `uv_base + "1"`, so only
  // st1 / UVMap1 / uv1 were ever extracted -- a texture whose UsdPrimvarReader
  // names `uvSet1`, `map2` or `UVMap.001` (all common) referenced a set the
  // converter never built, and RenderTexture::uv_primvar pointed at nothing.
  //
  // Take any second 2-component primvar instead, preferring the conventional
  // names so existing assets keep their slot assignment: uv_base + "1" first,
  // then the other configured UV names, then any remaining float2 primvar (in
  // name order, so the choice is deterministic rather than dependent on authoring
  // order). Skinning primvars are consumed by the skin binding, not here.
  auto two_component = [&](const std::string& name) -> bool {
    for (const Primvar& pv : primvars) {
      if (pv.name != name || !pv.value || !pv.value->is_array()) continue;
      return GetComponentCount(pv.value->type_id()) == 2;
    }
    return false;
  };
  std::string uv_second;
  if (two_component(uv_base + "1")) {
    uv_second = uv_base + "1";
  }
  if (uv_second.empty()) {
    for (const std::string& candidate : config_.mesh.uv_primvar_names) {
      if (candidate != uv_base && two_component(candidate)) {
        uv_second = candidate;
        break;
      }
    }
  }
  if (uv_second.empty()) {
    std::vector<std::string> others;
    for (const Primvar& pv : primvars) {
      if (pv.name == uv_base || !pv.value || !pv.value->is_array()) continue;
      if (GetComponentCount(pv.value->type_id()) != 2) continue;
      if (pv.name.rfind("skel:", 0) == 0) continue;
      others.push_back(pv.name);
    }
    if (!others.empty()) {
      std::sort(others.begin(), others.end());
      uv_second = others.front();
    }
  }

  const size_t npoints = mesh->point_count();
  const size_t nfaces = mesh->face_count();
  const size_t ncorners = mesh->face_vertex_indices.size();

  auto expected_elems = [&](Interpolation it) -> size_t {
    switch (it) {
      case Interpolation::Constant: return 1;
      case Interpolation::Uniform: return nfaces;
      case Interpolation::FaceVarying: return ncorners;
      case Interpolation::Vertex:
      case Interpolation::Varying:
      default: return npoints;
    }
  };

  // Expand an indexed primvar to direct form; false on any out-of-range index.
  auto expand_indexed = [](const std::vector<float>& data, uint32_t comps,
                           const std::vector<int32_t>& idxs,
                           std::vector<float>* out) -> bool {
    const size_t elems = comps ? data.size() / comps : 0;
    out->clear();
    out->reserve(idxs.size() * comps);
    for (int32_t raw : idxs) {
      if (raw < 0 || static_cast<size_t>(raw) >= elems) return false;
      const float* src = data.data() + static_cast<size_t>(raw) * comps;
      out->insert(out->end(), src, src + comps);
    }
    return true;
  };

  // Authored `normals` attribute (primvars:normals, handled in the loop
  // below, takes precedence per USD).
  {
    ValueArrayRead<float> normals;
    if (ReadFloatArray(prim, "normals", config_.time_code, &normals) &&
        !normals.empty()) {
      std::string interp_tok = "vertex";
      if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
        if (const ::tinyusdz::next::PropMeta* pm =
                spec->property_meta("normals")) {
          if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
            interp_tok = pm->interpolation;
          }
        }
      }
      const Interpolation ni = ParsePrimvarInterp(interp_tok);
      const size_t elems = normals.view.size / 3;
      if (elems == expected_elems(ni)) {
        mesh->normals.append(normals.view.data, normals.view.size);
        mesh->normals_interp = ni;
      } else {
        warnings_.push_back("Mesh '" + mesh->prim_path +
                            "': authored normals element count does not match "
                            "their interpolation; ignoring (normals will be "
                            "computed)");
      }
    }
  }

  for (Primvar& pv : primvars) {
    if (!pv.value) continue;
    // Skinning primvars are consumed by the skin binding (GetSkinBinding), not
    // by the generic vertex-attribute channel. Skip BEFORE flattening: the
    // check used to sit below, after a full copy of the array had been made.
    if (pv.name.rfind("skel:", 0) == 0) continue;

    std::vector<float> data;
    const std::vector<float>* fdata = nullptr;
    const uint32_t comps = PrimvarToFloats(*pv.value, &data, &fdata);
    const bool is_uv0 = (pv.name == uv_base);
    const bool is_uv1 = (!uv_second.empty() && pv.name == uv_second);
    const bool is_color = (pv.name == "displayColor");
    const bool is_opacity = (pv.name == "displayOpacity");
    const bool is_normals = (pv.name == "normals");
    const bool builtin =
        is_uv0 || is_uv1 || is_color || is_opacity || is_normals;

    // Unauthored interpolation defaults to `constant` per the USD spec
    // (pxr UsdGeomPrimvar / legacy GeomPrimvar parity). Unauthored arrays
    // sized per-point/per-corner/per-face are common in the wild though, so
    // infer the mode from the LOGICAL element count for those.
    auto resolve_interp = [&](size_t elems) -> Interpolation {
      if (!pv.interpolation_authored && elems > 1) {
        if (elems == npoints) return Interpolation::Vertex;
        if (elems == ncorners) return Interpolation::FaceVarying;
        if (elems == nfaces) return Interpolation::Uniform;
      }
      return ParsePrimvarInterp(pv.interpolation);
    };

    if (comps == 0) {
      // Non-float primvar: only representable as a generic int attribute.
      if (builtin) continue;
      const std::vector<int32_t>* ia = pv.value->as_int_array();
      if (!ia || ia->empty()) continue;
      VertexAttribute attr;
      attr.name = pv.name;
      attr.format = VertexFormat::Int;
      attr.interpolation = resolve_interp(
          pv.indices.empty() ? ia->size() : pv.indices.size());
      attr.int_data.append(ia->data(), ia->size());
      bool idx_ok = true;
      for (int32_t raw : pv.indices) {
        if (raw < 0 || static_cast<size_t>(raw) >= ia->size()) {
          idx_ok = false;
          break;
        }
        attr.indices.push_back(static_cast<uint32_t>(raw));
      }
      if (!idx_ok) {
        warnings_.push_back("Mesh '" + mesh->prim_path + "': primvar '" +
                            pv.name + "' has out-of-range indices; dropped");
        continue;
      }
      mesh->primvars.push_back(std::move(attr));
      continue;
    }

    // Indexed builtin primvars are expanded to direct form (the builtin
    // buffers carry no index channel).
    if (!pv.indices.empty() && builtin) {
      std::vector<float> expanded;
      if (!expand_indexed(*fdata, comps, pv.indices, &expanded)) {
        warnings_.push_back("Mesh '" + mesh->prim_path + "': primvar '" +
                            pv.name + "' has out-of-range indices; dropped");
        continue;
      }
      data = std::move(expanded);
      fdata = &data;
    }

    if (builtin) {
      // Size must match the declared interpolation or a consumer indexes OOB.
      const size_t elems = fdata->size() / comps;
      const Interpolation interp = resolve_interp(elems);
      if (elems != expected_elems(interp)) {
        warnings_.push_back(
            "Mesh '" + mesh->prim_path + "': primvar '" + pv.name +
            "' element count does not match its interpolation; dropped");
        continue;
      }
      if (is_uv0 && comps == 2) {
        mesh->texcoords_0.append(fdata->data(), fdata->size());
        mesh->texcoords_0_interp = interp;
        mesh->texcoords_0_name = pv.name;
      } else if (is_uv1 && comps == 2) {
        mesh->texcoords_1.append(fdata->data(), fdata->size());
        mesh->texcoords_1_interp = interp;
        mesh->texcoords_1_name = pv.name;
      } else if (is_color && (comps == 3 || comps == 4)) {
        mesh->colors.append(fdata->data(), fdata->size());
        mesh->colors_interp = interp;
      } else if (is_opacity && comps == 1) {
        // displayOpacity as a render channel (legacy exposes it alongside
        // displayColor; consumers combine it as the vertex-color alpha).
        mesh->opacities.append(fdata->data(), fdata->size());
        mesh->opacities_interp = interp;
      } else if (is_normals && comps == 3) {
        // primvars:normals takes precedence over the raw `normals` attribute.
        mesh->normals.clear();
        mesh->normals.append(fdata->data(), fdata->size());
        mesh->normals_interp = interp;
      }
      continue;
    }

    // Generic primvar: keep indices as an index channel (validated).
    VertexAttribute attr;
    attr.name = pv.name;
    attr.format = comps == 1   ? VertexFormat::Float
                  : comps == 2 ? VertexFormat::Vec2
                  : comps == 3 ? VertexFormat::Vec3
                               : VertexFormat::Vec4;
    if (comps > 4) continue;  // matrices etc.: not a vertex attribute
    attr.interpolation = resolve_interp(
        pv.indices.empty() ? (fdata->size() / comps) : pv.indices.size());
    attr.float_data.append(fdata->data(), fdata->size());
    bool idx_ok = true;
    const size_t elems = fdata->size() / comps;
    for (int32_t raw : pv.indices) {
      if (raw < 0 || static_cast<size_t>(raw) >= elems) {
        idx_ok = false;
        break;
      }
      attr.indices.push_back(static_cast<uint32_t>(raw));
    }
    if (!idx_ok) {
      warnings_.push_back("Mesh '" + mesh->prim_path + "': primvar '" +
                          pv.name + "' has out-of-range indices; dropped");
      continue;
    }
    mesh->primvars.push_back(std::move(attr));
  }

  return true;
}

bool RenderSceneConverter::ConvertPoints(const UsdPrim& prim,
                                         RenderPoints* out) {
  if (!out || !prim.IsValid() || prim.GetTypeName() != "Points") {
    last_error_ = "Invalid Points prim";
    return false;
  }

  ValueArrayRead<float> points;
  if (!ReadFloatArray(prim, "points", config_.time_code, &points) ||
      points.empty() || (points.view.size % 3) != 0) {
    last_error_ = "Invalid Points.points data";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->points.append(points.view.data, points.view.size);

  ValueArrayRead<float> widths;
  if (ReadFloatArray(prim, "widths", config_.time_code, &widths) &&
      !widths.empty()) {
    const size_t n = out->point_count();
    if (widths.view.size == 1 || widths.view.size == n) {
      out->widths.append(widths.view.data, widths.view.size);
    } else {
      warnings_.push_back("Points '" + out->prim_path +
                          "': ignoring widths with mismatched element count");
    }
  }

  ValueArrayRead<float> colors;
  if (ReadFloatArray(prim, "primvars:displayColor", config_.time_code,
                     &colors) &&
      !colors.empty()) {
    std::string interp_tok;
    if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
      if (const ::tinyusdz::next::PropMeta* pm =
              spec->property_meta("primvars:displayColor")) {
        if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
          interp_tok = pm->interpolation;
        }
      }
    }
    const size_t elems = colors.view.size / 3;
    Interpolation interp;
    if (interp_tok.empty()) {
      // Unauthored: spec default is constant; per-point arrays are common in
      // the wild, so classify by element count.
      interp = (elems == out->point_count() && elems != 1)
                   ? Interpolation::Vertex
                   : Interpolation::Constant;
    } else {
      interp = ParsePrimvarInterp(interp_tok);
    }
    const size_t expected = (interp == Interpolation::Constant)
                                ? 1
                                : out->point_count();
    if ((colors.view.size % 3) == 0 && elems == expected) {
      out->colors.append(colors.view.data, colors.view.size);
      out->colors_interp = interp;
    } else {
      warnings_.push_back("Points '" + out->prim_path +
                          "': ignoring displayColor with mismatched element count");
    }
  }

  ValueArrayRead<float> opacities;
  if (ReadFloatArray(prim, "primvars:displayOpacity", config_.time_code,
                     &opacities) &&
      !opacities.empty()) {
    std::string interp_tok;
    if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
      if (const ::tinyusdz::next::PropMeta* pm =
              spec->property_meta("primvars:displayOpacity")) {
        if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
          interp_tok = pm->interpolation;
        }
      }
    }
    const size_t elems = opacities.view.size;
    Interpolation interp;
    if (interp_tok.empty()) {
      interp = (elems == out->point_count() && elems != 1)
                   ? Interpolation::Vertex
                   : Interpolation::Constant;
    } else {
      interp = ParsePrimvarInterp(interp_tok);
    }
    const size_t expected = (interp == Interpolation::Constant)
                                ? 1
                                : out->point_count();
    if (elems == expected) {
      out->opacities.append(opacities.view.data, elems);
      out->opacities_interp = interp;
    } else {
      warnings_.push_back(
          "Points '" + out->prim_path +
          "': ignoring displayOpacity with mismatched element count");
    }
  }

  ComputePointBounds(out->points, &out->bbox_min, &out->bbox_max,
                     &out->has_bbox);
  return true;
}

//
// Curves conversion (BasisCurves / NurbsCurves / HermiteCurves)
//

namespace {

// Cubic blending weights for control points [P0,P1,P2,P3] at span-local
// parameter t in [0,1]. Standard uniform basis matrices.
void EvalCubicBasisWeights(CurveBasis basis, float t, float w[4]) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  switch (basis) {
    case CurveBasis::BSpline:
      w[0] = (1.0f - 3.0f * t + 3.0f * t2 - t3) / 6.0f;
      w[1] = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
      w[2] = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
      w[3] = t3 / 6.0f;
      break;
    case CurveBasis::CatmullRom:
      w[0] = 0.5f * (-t3 + 2.0f * t2 - t);
      w[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
      w[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
      w[3] = 0.5f * (t3 - t2);
      break;
    case CurveBasis::Bezier:
    default: {
      const float s = 1.0f - t;
      w[0] = s * s * s;
      w[1] = 3.0f * t * s * s;
      w[2] = 3.0f * t2 * s;
      w[3] = t3;
      break;
    }
  }
}

// Linear sample of a per-curve scalar channel (e.g. widths) at normalized
// curve parameter u01 in [0,1]. Periodic channels wrap so the closing point
// maps back to element 0.
float SampleChannelLinear(const float* vals, size_t count, float u01,
                          bool periodic, size_t stride = 1,
                          size_t component = 0) {
  if (!vals || count == 0) return 0.0f;
  if (count == 1) return vals[component];
  u01 = std::min(std::max(u01, 0.0f), 1.0f);
  if (periodic) {
    const float f = u01 * static_cast<float>(count);
    const size_t i = static_cast<size_t>(f) % count;
    const size_t j = (i + 1) % count;
    const float frac = f - std::floor(f);
    return vals[i * stride + component] * (1.0f - frac) +
           vals[j * stride + component] * frac;
  }
  const float f = u01 * static_cast<float>(count - 1);
  const size_t i = static_cast<size_t>(f);
  if (i >= count - 1) return vals[(count - 1) * stride + component];
  const float frac = f - static_cast<float>(i);
  return vals[i * stride + component] * (1.0f - frac) +
         vals[(i + 1) * stride + component] * frac;
}

constexpr int kMaxNurbsDegree = 9;

// NURBS curve point at parameter u via de Boor's algorithm.
// `knots` must have ncv + degree + 1 non-decreasing entries; u should lie in
// [knots[degree], knots[ncv]].
bool DeBoorEval(const float* cvs, size_t ncv, const float* knots, int degree,
                float u, float out[3]) {
  if (degree < 1 || degree > kMaxNurbsDegree ||
      ncv < static_cast<size_t>(degree) + 1) {
    return false;
  }
  const int n = static_cast<int>(ncv) - 1;
  int k = degree;
  if (u >= knots[n + 1]) {
    k = n;
  } else if (u > knots[degree]) {
    while (k < n && !(u >= knots[k] && u < knots[k + 1])) ++k;
  }
  float d[kMaxNurbsDegree + 1][3];
  for (int j = 0; j <= degree; ++j) {
    const size_t idx = static_cast<size_t>(j + k - degree);
    d[j][0] = cvs[idx * 3 + 0];
    d[j][1] = cvs[idx * 3 + 1];
    d[j][2] = cvs[idx * 3 + 2];
  }
  for (int r = 1; r <= degree; ++r) {
    for (int j = degree; j >= r; --j) {
      const float tj = knots[j + k - degree];
      const float denom = knots[j + 1 + k - r] - tj;
      float alpha = 0.0f;
      if (denom > 0.0f) {
        alpha = (u - tj) / denom;
        alpha = std::min(std::max(alpha, 0.0f), 1.0f);
      }
      d[j][0] = (1.0f - alpha) * d[j - 1][0] + alpha * d[j][0];
      d[j][1] = (1.0f - alpha) * d[j - 1][1] + alpha * d[j][1];
      d[j][2] = (1.0f - alpha) * d[j - 1][2] + alpha * d[j][2];
    }
  }
  out[0] = d[degree][0];
  out[1] = d[degree][1];
  out[2] = d[degree][2];
  return true;
}

// Read a float-ish array attribute, converting double-backed data (e.g.
// NurbsCurves knots/ranges which are double[]/double2[]).
bool ReadFloatsFlexible(const UsdPrim& prim, const char* name, double time,
                        std::vector<float>* out) {
  ValueArrayRead<float> f;
  if (ReadFloatArray(prim, name, time, &f) && !f.empty()) {
    out->assign(f.begin(), f.end());
    return true;
  }
  const Value* v = GetAttribute(prim, name);
  if (!v) return false;
  ::tinyusdz::next::ArrayScratch<double> scratch;
  ::tinyusdz::next::ArrayView<double> view;
  if (!::tinyusdz::next::GetDoubleArrayView(*v, &scratch, &view) ||
      view.empty()) {
    return false;
  }
  out->clear();
  out->reserve(view.size);
  for (size_t i = 0; i < view.size; ++i) {
    out->push_back(static_cast<float>(view[i]));
  }
  return true;
}

// Per-curve tessellation plan.
struct CurveTessPlan {
  uint32_t n = 0;            // authored control point count
  uint32_t nsegs = 0;        // cubic/NURBS spans (unused for linear)
  bool linear = false;       // passthrough as polyline (also fallback mode)
  bool periodic = false;
  bool pinned = false;       // duplicate end CVs (bspline x2 / catmullRom x1)
  int degree = 0;            // NURBS only
  size_t knot_offset = 0;    // NURBS only, into the flattened knots array
  float u0 = 0.0f;           // NURBS eval domain
  float u1 = 0.0f;
  uint32_t varying_count = 0;  // varying-interp elements owned by this curve
};

}  // namespace

bool RenderSceneConverter::ConvertCurves(const UsdPrim& prim,
                                         RenderCurves* out) {
  const std::string type_name =
      prim.IsValid() ? prim.GetTypeName() : std::string();
  if (!out || (type_name != "BasisCurves" && type_name != "NurbsCurves" &&
               type_name != "HermiteCurves")) {
    last_error_ = "Invalid curves prim";
    return false;
  }
  out->is_nurbs = (type_name == "NurbsCurves");
  out->is_hermite = (type_name == "HermiteCurves");
  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  ValueArrayRead<int32_t> counts;
  if (!ReadIntArray(prim, "curveVertexCounts", config_.time_code, &counts) ||
      counts.empty()) {
    last_error_ = "Invalid curves.curveVertexCounts data";
    return false;
  }
  ValueArrayRead<float> points;
  if (!ReadFloatArray(prim, "points", config_.time_code, &points) ||
      points.empty() || (points.view.size % 3) != 0) {
    last_error_ = "Invalid curves.points data";
    return false;
  }

  size_t total_cp = 0;
  for (int32_t c : counts) {
    if (c <= 0) {
      last_error_ = "Non-positive curveVertexCounts entry";
      return false;
    }
    total_cp += static_cast<size_t>(c);
  }
  if (total_cp != points.view.size / 3) {
    last_error_ = "curveVertexCounts sum does not match points size";
    return false;
  }

  out->curve_vertex_counts.reserve(counts.size());
  for (int32_t c : counts) {
    out->curve_vertex_counts.push_back(static_cast<uint32_t>(c));
  }
  out->points.append(points.view.data, points.view.size);

  // type / basis / wrap tokens (BasisCurves; NurbsCurves have order/knots).
  if (out->is_nurbs) {
    out->type = CurveType::Cubic;
  } else {
    std::string tok;
    if (GetToken(prim, "type", &tok) && tok == "linear") {
      out->type = CurveType::Linear;
    }
    tok.clear();
    if (GetToken(prim, "basis", &tok) && !tok.empty() && tok != "bezier") {
      if (tok == "bspline") {
        out->basis = CurveBasis::BSpline;
      } else if (tok == "catmullRom") {
        out->basis = CurveBasis::CatmullRom;
      } else {
        warnings_.push_back("BasisCurves '" + out->prim_path +
                            "': unsupported basis '" + tok +
                            "', treating as bezier");
      }
    }
    tok.clear();
    if (GetToken(prim, "wrap", &tok)) {
      if (tok == "periodic") out->wrap = CurveWrap::Periodic;
      else if (tok == "pinned") out->wrap = CurveWrap::Pinned;
    }
  }

  // NURBS attributes.
  std::vector<int32_t> nurbs_order;
  std::vector<float> nurbs_knots;
  std::vector<float> nurbs_ranges;  // 2 floats per curve, optional
  std::vector<float> hermite_tangents;
  bool nurbs_data_ok = true;
  if (out->is_nurbs) {
    nurbs_order = ReadIntArrayCopy(prim, "order", config_.time_code);
    if (!ReadFloatsFlexible(prim, "knots", config_.time_code, &nurbs_knots)) {
      warnings_.push_back("NurbsCurves '" + out->prim_path +
                          "': missing/unreadable knots; using control-polygon "
                          "passthrough");
      nurbs_data_ok = false;
    }
    ReadFloatsFlexible(prim, "ranges", config_.time_code, &nurbs_ranges);
  } else if (out->is_hermite) {
    if (!ReadFloatsFlexible(prim, "tangents", config_.time_code,
                            &hermite_tangents) ||
        hermite_tangents.size() != points.view.size) {
      warnings_.push_back("HermiteCurves '" + out->prim_path +
                          "': tangents must match points; using control-polygon "
                          "passthrough");
      hermite_tangents.clear();
    }
  }

  const uint32_t segs = std::max(1u, config_.curves.tessellation_segments);
  const size_t ncurves = out->curve_vertex_counts.size();

  //
  // Build per-curve tessellation plans (validation + varying counts).
  //
  std::vector<CurveTessPlan> plans(ncurves);
  size_t knot_cursor = 0;
  for (size_t ci = 0; ci < ncurves; ++ci) {
    CurveTessPlan& plan = plans[ci];
    const uint32_t n = out->curve_vertex_counts[ci];
    plan.n = n;

    auto fall_back_linear = [&](const std::string& why) {
      plan.linear = true;
      plan.periodic = (!out->is_nurbs && out->wrap == CurveWrap::Periodic);
      plan.varying_count = n;
      warnings_.push_back("Curves '" + out->prim_path + "' curve " +
                          std::to_string(ci) + ": " + why +
                          "; using control-polygon passthrough");
    };

    if (out->is_nurbs) {
      int order = 4;
      if (nurbs_order.size() == ncurves) order = nurbs_order[ci];
      else if (nurbs_order.size() == 1) order = nurbs_order[0];
      const size_t knot_count = static_cast<size_t>(n) + static_cast<size_t>(
          order > 0 ? order : 0);
      const size_t knot_offset = knot_cursor;
      if (order >= 2 && order <= kMaxNurbsDegree + 1) {
        knot_cursor += knot_count;  // advance even if this curve falls back
      }
      if (!nurbs_data_ok) {
        plan.linear = true;
        plan.varying_count = n;
        continue;
      }
      if (order < 2 || order > kMaxNurbsDegree + 1) {
        fall_back_linear("unsupported NURBS order " + std::to_string(order));
        continue;
      }
      if (n < static_cast<uint32_t>(order)) {
        fall_back_linear("fewer control points than NURBS order");
        continue;
      }
      if (knot_offset + knot_count > nurbs_knots.size()) {
        fall_back_linear("knot vector too short");
        continue;
      }
      const float* kn = nurbs_knots.data() + knot_offset;
      bool monotonic = true;
      for (size_t i = 1; i < knot_count; ++i) {
        if (kn[i] < kn[i - 1]) {
          monotonic = false;
          break;
        }
      }
      if (!monotonic) {
        fall_back_linear("decreasing knot vector");
        continue;
      }
      const int degree = order - 1;
      float u0 = kn[degree];
      float u1 = kn[n];
      if (nurbs_ranges.size() >= (ci + 1) * 2) {
        const float r0 = nurbs_ranges[ci * 2 + 0];
        const float r1 = nurbs_ranges[ci * 2 + 1];
        if (r0 < r1) {
          u0 = std::max(u0, r0);
          u1 = std::min(u1, r1);
        }
      }
      if (!(u1 > u0)) {
        fall_back_linear("degenerate NURBS parameter range");
        continue;
      }
      plan.degree = degree;
      plan.knot_offset = knot_offset;
      plan.u0 = u0;
      plan.u1 = u1;
      plan.nsegs = n - static_cast<uint32_t>(order) + 1;
      plan.varying_count = plan.nsegs + 1;
      continue;
    }

    if (out->is_hermite) {
      if (n < 2 || hermite_tangents.empty()) {
        fall_back_linear(n < 2 ? "too few Hermite control points"
                               : "missing or mismatched Hermite tangents");
        continue;
      }
      plan.nsegs = n - 1;
      plan.varying_count = n;
      continue;
    }

    // BasisCurves.
    if (out->type == CurveType::Linear) {
      plan.linear = true;
      plan.periodic = (out->wrap == CurveWrap::Periodic);
      if (plan.periodic && n < 3) plan.periodic = false;
      plan.varying_count = n;
      continue;
    }

    const bool bezier = (out->basis == CurveBasis::Bezier);
    // "pinned" only applies to cubic bspline/catmullRom.
    const bool pinned = (out->wrap == CurveWrap::Pinned) && !bezier;
    const bool periodic = (out->wrap == CurveWrap::Periodic);
    if (periodic) {
      if (n < 3 || (bezier && (n % 3) != 0)) {
        fall_back_linear("invalid periodic cubic control point count");
        continue;
      }
      plan.periodic = true;
      plan.nsegs = bezier ? (n / 3) : n;
      plan.varying_count = plan.nsegs;
      continue;
    }
    if (pinned) {
      if (n < 2) {
        fall_back_linear("too few control points for pinned cubic curve");
        continue;
      }
      plan.pinned = true;
      // bspline: endpoints tripled (dup x2); catmullRom: doubled (dup x1).
      const uint32_t dup = (out->basis == CurveBasis::BSpline) ? 2u : 1u;
      plan.nsegs = (n + 2 * dup) - 3;
      plan.varying_count = plan.nsegs + 1;
      continue;
    }
    // nonperiodic
    if (n < 4 || (bezier && ((n - 4) % 3) != 0)) {
      fall_back_linear("invalid cubic control point count");
      continue;
    }
    plan.nsegs = bezier ? ((n - 4) / 3 + 1) : (n - 3);
    plan.varying_count = plan.nsegs + 1;
  }

  size_t varying_total = 0;
  for (const CurveTessPlan& plan : plans) varying_total += plan.varying_count;

  //
  // widths (classified by element count; default schema interp is vertex).
  //
  ValueArrayRead<float> widths;
  if (ReadFloatArray(prim, "widths", config_.time_code, &widths) &&
      !widths.empty()) {
    const size_t m = widths.view.size;
    if (m == 1) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Constant;
    } else if (m == ncurves) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Uniform;
    } else if (m == total_cp) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Vertex;
    } else if (m == varying_total) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Varying;
    } else {
      warnings_.push_back("Curves '" + out->prim_path +
                          "': ignoring widths with mismatched element count");
    }
  }

  //
  // displayColor (control data only; rgb).
  //
  ValueArrayRead<float> colors;
  if (ReadFloatArray(prim, "primvars:displayColor", config_.time_code,
                     &colors) &&
      !colors.empty() && (colors.view.size % 3) == 0) {
    std::string interp_tok = "constant";
    if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
      if (const ::tinyusdz::next::PropMeta* pm =
              spec->property_meta("primvars:displayColor")) {
        if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
          interp_tok = pm->interpolation;
        }
      }
    }
    Interpolation interp = ParsePrimvarInterp(interp_tok);
    const size_t elems = colors.view.size / 3;
    auto expected = [&](Interpolation it) -> size_t {
      switch (it) {
        case Interpolation::Constant: return 1;
        case Interpolation::Uniform: return ncurves;
        case Interpolation::Varying: return varying_total;
        case Interpolation::Vertex:
        default: return total_cp;
      }
    };
    if (elems != expected(interp)) {
      // Authored interp does not match; classify by size instead.
      if (elems == 1) interp = Interpolation::Constant;
      else if (elems == total_cp) interp = Interpolation::Vertex;
      else if (elems == ncurves) interp = Interpolation::Uniform;
      else if (elems == varying_total) interp = Interpolation::Varying;
      else {
        warnings_.push_back(
            "Curves '" + out->prim_path +
            "': ignoring displayColor with mismatched element count");
        interp = Interpolation::Constant;  // expected(Constant)==1 != elems
      }
    }
    if (elems == expected(interp)) {
      out->colors.append(colors.view.data, colors.view.size);
      out->colors_interp = interp;
    }
  }

  // displayOpacity follows the same constant/uniform/vertex/varying rules as
  // displayColor and is resampled onto the tessellated centerline below.
  ValueArrayRead<float> opacities;
  if (ReadFloatArray(prim, "primvars:displayOpacity", config_.time_code,
                     &opacities) && !opacities.empty()) {
    std::string interp_tok = "constant";
    if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
      if (const ::tinyusdz::next::PropMeta* pm =
              spec->property_meta("primvars:displayOpacity")) {
        if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
          interp_tok = pm->interpolation;
        }
      }
    }
    Interpolation interp = ParsePrimvarInterp(interp_tok);
    const size_t elems = opacities.view.size;
    auto expected = [&](Interpolation it) -> size_t {
      switch (it) {
        case Interpolation::Constant: return 1;
        case Interpolation::Uniform: return ncurves;
        case Interpolation::Varying: return varying_total;
        case Interpolation::Vertex:
        default: return total_cp;
      }
    };
    if (elems != expected(interp)) {
      if (elems == 1) interp = Interpolation::Constant;
      else if (elems == total_cp) interp = Interpolation::Vertex;
      else if (elems == ncurves) interp = Interpolation::Uniform;
      else if (elems == varying_total) interp = Interpolation::Varying;
    }
    if (elems == expected(interp)) {
      out->opacities.append(opacities.view.data, elems);
      out->opacities_interp = interp;
    } else {
      warnings_.push_back(
          "Curves '" + out->prim_path +
          "': ignoring displayOpacity with mismatched element count");
    }
  }

  //
  // Tessellate.
  //
  const bool emit_widths = out->has_widths() &&
                           out->widths_interp != Interpolation::Constant;
  const bool emit_colors = out->has_colors();
  const bool emit_opacities = !out->opacities.empty() &&
      out->opacities_interp != Interpolation::Constant;
  size_t cp_offset = 0;
  size_t var_offset = 0;
  std::vector<float> emitted;
  std::vector<float> pinned_cvs;
  for (size_t ci = 0; ci < ncurves; ++ci) {
    const CurveTessPlan& plan = plans[ci];
    const uint32_t n = plan.n;
    const float* cv = points.view.data + cp_offset * 3;
    emitted.clear();

    if (plan.linear) {
      emitted.assign(cv, cv + static_cast<size_t>(n) * 3);
      if (plan.periodic) {
        emitted.push_back(cv[0]);
        emitted.push_back(cv[1]);
        emitted.push_back(cv[2]);
      }
    } else if (out->is_hermite) {
      const float* tangent = hermite_tangents.data() + cp_offset * 3;
      emitted.reserve((static_cast<size_t>(plan.nsegs) * segs + 1) * 3);
      auto eval_hermite = [&](uint32_t span, float t, float p[3]) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;
        for (size_t component = 0; component < 3; ++component) {
          p[component] = h00 * cv[span * 3 + component] +
                         h10 * tangent[span * 3 + component] +
                         h01 * cv[(span + 1) * 3 + component] +
                         h11 * tangent[(span + 1) * 3 + component];
        }
      };
      for (uint32_t span = 0; span < plan.nsegs; ++span) {
        for (uint32_t sample = 0; sample < segs; ++sample) {
          float p[3];
          eval_hermite(span,
                       static_cast<float>(sample) / static_cast<float>(segs),
                       p);
          emitted.insert(emitted.end(), p, p + 3);
        }
      }
      float p[3];
      eval_hermite(plan.nsegs - 1, 1.0f, p);
      emitted.insert(emitted.end(), p, p + 3);
    } else if (out->is_nurbs) {
      const float* kn = nurbs_knots.data() + plan.knot_offset;
      const uint32_t nsamples = plan.nsegs * segs + 1;
      emitted.reserve(static_cast<size_t>(nsamples) * 3);
      for (uint32_t k = 0; k < nsamples; ++k) {
        const float u =
            plan.u0 + (plan.u1 - plan.u0) *
                          (static_cast<float>(k) /
                           static_cast<float>(nsamples - 1));
        float p[3] = {0.0f, 0.0f, 0.0f};
        DeBoorEval(cv, n, kn, plan.degree, u, p);
        emitted.push_back(p[0]);
        emitted.push_back(p[1]);
        emitted.push_back(p[2]);
      }
    } else {
      // Cubic BasisCurves.
      const float* ecv = cv;
      uint32_t en = n;
      if (plan.pinned) {
        const uint32_t dup = (out->basis == CurveBasis::BSpline) ? 2u : 1u;
        pinned_cvs.clear();
        pinned_cvs.reserve((static_cast<size_t>(n) + 2 * dup) * 3);
        for (uint32_t d = 0; d < dup; ++d) {
          pinned_cvs.insert(pinned_cvs.end(), cv, cv + 3);
        }
        pinned_cvs.insert(pinned_cvs.end(), cv, cv + static_cast<size_t>(n) * 3);
        const float* last = cv + (static_cast<size_t>(n) - 1) * 3;
        for (uint32_t d = 0; d < dup; ++d) {
          pinned_cvs.insert(pinned_cvs.end(), last, last + 3);
        }
        ecv = pinned_cvs.data();
        en = n + 2 * dup;
      }
      const uint32_t vstep = (out->basis == CurveBasis::Bezier) ? 3u : 1u;
      emitted.reserve((static_cast<size_t>(plan.nsegs) * segs + 1) * 3);
      float w[4];
      auto eval_span = [&](uint32_t span, float t, float p[3]) {
        EvalCubicBasisWeights(out->basis, t, w);
        p[0] = p[1] = p[2] = 0.0f;
        const uint32_t base = span * vstep;
        for (uint32_t k = 0; k < 4; ++k) {
          const uint32_t idx = plan.periodic ? ((base + k) % en) : (base + k);
          p[0] += w[k] * ecv[idx * 3 + 0];
          p[1] += w[k] * ecv[idx * 3 + 1];
          p[2] += w[k] * ecv[idx * 3 + 2];
        }
      };
      for (uint32_t s = 0; s < plan.nsegs; ++s) {
        for (uint32_t j = 0; j < segs; ++j) {
          float p[3];
          eval_span(s, static_cast<float>(j) / static_cast<float>(segs), p);
          emitted.push_back(p[0]);
          emitted.push_back(p[1]);
          emitted.push_back(p[2]);
        }
      }
      if (plan.periodic) {
        // Close the loop with a copy of the first tessellated point.
        emitted.push_back(emitted[0]);
        emitted.push_back(emitted[1]);
        emitted.push_back(emitted[2]);
      } else {
        float p[3];
        eval_span(plan.nsegs - 1, 1.0f, p);
        emitted.push_back(p[0]);
        emitted.push_back(p[1]);
        emitted.push_back(p[2]);
      }
    }

    const size_t emit_count = emitted.size() / 3;
    out->tessellated_vertex_counts.push_back(
        static_cast<uint32_t>(emit_count));
    out->tessellated_points.append(emitted.data(), emitted.size());

    if (emit_widths) {
      const float* wvals = nullptr;
      size_t wcount = 0;
      switch (out->widths_interp) {
        case Interpolation::Uniform:
          wvals = widths.view.data + ci;
          wcount = 1;
          break;
        case Interpolation::Vertex:
          wvals = widths.view.data + cp_offset;
          wcount = n;
          break;
        case Interpolation::Varying:
          wvals = widths.view.data + var_offset;
          wcount = plan.varying_count;
          break;
        default: break;
      }
      for (size_t k = 0; k < emit_count; ++k) {
        const float u01 =
            emit_count > 1
                ? static_cast<float>(k) / static_cast<float>(emit_count - 1)
                : 0.0f;
        out->tessellated_widths.push_back(
            SampleChannelLinear(wvals, wcount, u01, plan.periodic));
      }
    }

    if (emit_colors) {
      const float* cvals = nullptr;
      size_t ccount = 0;
      switch (out->colors_interp) {
        case Interpolation::Constant:
          cvals = colors.view.data;
          ccount = 1;
          break;
        case Interpolation::Uniform:
          cvals = colors.view.data + ci * 3;
          ccount = 1;
          break;
        case Interpolation::Vertex:
          cvals = colors.view.data + cp_offset * 3;
          ccount = n;
          break;
        case Interpolation::Varying:
          cvals = colors.view.data + var_offset * 3;
          ccount = plan.varying_count;
          break;
        case Interpolation::FaceVarying:
          break;
      }
      if (cvals && ccount > 0) {
        for (size_t k = 0; k < emit_count; ++k) {
          const float u01 =
              emit_count > 1
                  ? static_cast<float>(k) / static_cast<float>(emit_count - 1)
                  : 0.0f;
          for (size_t component = 0; component < 3; ++component) {
            out->tessellated_colors.push_back(SampleChannelLinear(
                cvals, ccount, u01, plan.periodic, 3, component));
          }
        }
      }
    }

    if (emit_opacities) {
      const float* ovals = nullptr;
      size_t ocount = 0;
      switch (out->opacities_interp) {
        case Interpolation::Uniform:
          ovals = opacities.view.data + ci;
          ocount = 1;
          break;
        case Interpolation::Vertex:
          ovals = opacities.view.data + cp_offset;
          ocount = n;
          break;
        case Interpolation::Varying:
          ovals = opacities.view.data + var_offset;
          ocount = plan.varying_count;
          break;
        default: break;
      }
      if (ovals && ocount > 0) {
        for (size_t k = 0; k < emit_count; ++k) {
          const float u01 = emit_count > 1
                                ? static_cast<float>(k) /
                                      static_cast<float>(emit_count - 1)
                                : 0.0f;
          out->tessellated_opacities.push_back(
              SampleChannelLinear(ovals, ocount, u01, plan.periodic));
        }
      }
    }

    cp_offset += n;
    var_offset += plan.varying_count;
  }

  ComputePointBounds(out->tessellated_points, &out->bbox_min, &out->bbox_max,
                     &out->has_bbox);
  return true;
}

bool RenderSceneConverter::ConvertPointInstancer(const UsdPrim& prim,
                                                 RenderPointInstancer* out) {
  if (!out || !::tinyusdz::next::IsPointInstancer(prim)) {
    last_error_ = "Invalid PointInstancer prim";
    return false;
  }

  PointInstancerData data;
  const bool build_draws = config_.point_instancer.build_instance_draws ||
                           config_.point_instancer.duplicate_meshes;
  const bool build_transforms =
      config_.point_instancer.build_instance_transforms || build_draws;
  if (!ReadPointInstancerData(prim, config_.time_code, &data,
                             build_transforms)) {
    last_error_ = data.validation_error.empty()
                      ? "Failed to read PointInstancer data"
                      : data.validation_error;
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->prototype_paths.reserve(data.prototypes.size());
  for (const ::tinyusdz::next::Path& path : data.prototypes) {
    out->prototype_paths.push_back(path.str());
  }
  std::vector<uint8_t> visibility = BuildInstanceVisibility(
      data.proto_indices.size(), data.ids, data.invisible_ids,
      data.inactive_ids);
  if (config_.point_instancer.compact_instances) {
    BuildCompactInstances(data, visibility, &out->compact_instances);
  }
  const bool retain_source_arrays =
      config_.point_instancer.retain_source_arrays || build_draws ||
      !config_.point_instancer.compact_instances;
  if (retain_source_arrays) {
    out->proto_indices = std::move(data.proto_indices);
    out->positions = std::move(data.positions);
    out->orientations = std::move(data.orientations);
    out->scales = std::move(data.scales);
    out->velocities = std::move(data.velocities);
    out->angular_velocities = std::move(data.angular_velocities);
    out->ids = std::move(data.ids);
    out->invisible_ids = std::move(data.invisible_ids);
    out->inactive_ids = std::move(data.inactive_ids);
    out->instance_visible = std::move(visibility);
  }
  out->transforms.reserve(data.transforms.size());
  for (const ::tinyusdz::next::PointInstancerTransform& transform :
       data.transforms) {
    out->transforms.push_back(MatrixFromPointInstancerTransform(transform));
  }
  out->valid = data.valid;
  out->validation_error = std::move(data.validation_error);
  return true;
}

//
// Triangulation
//

bool RenderSceneConverter::TriangulateMesh(RenderMesh* mesh) {
  if (mesh->face_vertex_counts.empty()) return false;
  mesh->triangulated_indices.clear();  // re-entry / failure-path hardening
  mesh->triangulated_face_vertex_indices.clear();

  // Check if already triangulated
  bool all_triangles = true;
  for (size_t i = 0; i < mesh->face_vertex_counts.size(); ++i) {
    if (mesh->face_vertex_counts[i] != 3) {
      all_triangles = false;
      break;
    }
  }

  if (all_triangles && !mesh->left_handed && mesh->hole_faces.empty()) {
    // Just copy indices; corner remap is identity, one triangle per face.
    mesh->face_triangle_offsets.resize(mesh->face_vertex_counts.size() + 1);
    for (size_t f = 0; f <= mesh->face_vertex_counts.size(); ++f) {
      mesh->face_triangle_offsets[f] = static_cast<uint32_t>(f);
    }
    const size_t n = mesh->face_vertex_indices.size();
    if (WouldOverflowSizeMul(n, sizeof(uint32_t)) ||
        (n * sizeof(uint32_t)) > kMaxTempAllocBytes * 4u) {
      warnings_.push_back("Mesh '" + mesh->prim_path +
                          "' triangulated index allocation too large; skipping");
      return false;
    }
    if (!mesh->triangulated_indices.resize(n) ||
        !mesh->triangulated_face_vertex_indices.resize(n)) {
      warnings_.push_back("Out of memory triangulating mesh '" +
                          mesh->prim_path + "'");
      return false;
    }
    for (size_t i = 0; i < n; ++i) {
      mesh->triangulated_indices[i] = mesh->face_vertex_indices[i];
      mesh->triangulated_face_vertex_indices[i] = static_cast<uint32_t>(i);
    }
    mesh->is_triangulated = true;
    return true;
  }

  size_t tri_count = 0;
  for (size_t i = 0; i < mesh->face_vertex_counts.size(); ++i) {
    uint32_t nverts = mesh->face_vertex_counts[i];
    if (nverts >= 3) tri_count += nverts - 2;
  }
  const size_t tri_corner_count = tri_count * 3;
  if (tri_count >= kMaxTriangulationCornerCount) {
    warnings_.push_back("Mesh '" + mesh->prim_path +
                        "' has too many triangulated corners (" +
                        std::to_string(tri_corner_count) +
                        "); skipping");
    return false;
  }
  if (WouldOverflowSizeMul(tri_corner_count, sizeof(uint32_t)) ||
      (tri_corner_count * sizeof(uint32_t)) > kMaxTempAllocBytes * 4u) {
    warnings_.push_back("Mesh '" + mesh->prim_path +
                        "' triangulated index allocation too large; skipping");
    return false;
  }

  if (!mesh->triangulated_indices.reserve(tri_count * 3) ||
      !mesh->triangulated_face_vertex_indices.reserve(tri_count * 3)) {
    warnings_.push_back("Out of memory triangulating mesh '" +
                        mesh->prim_path + "'");
    return false;
  }
  mesh->face_triangle_offsets.assign(mesh->face_vertex_counts.size() + 1, 0);
  size_t idx_offset = 0;
  // Hoisted out of the per-face loop: constructing the nested vector inside it
  // cost two heap allocations per n-gon (1M allocations for a 500k-n-gon mesh).
  // clear() keeps the ring's capacity across faces.
  using EarcutPoint2 = std::array<double, 2>;
  std::vector<std::vector<EarcutPoint2>> earcut_polygon(1);

  for (size_t f = 0; f < mesh->face_vertex_counts.size(); ++f) {
    mesh->face_triangle_offsets[f] =
        static_cast<uint32_t>(mesh->triangulated_indices.size() / 3);
    const uint32_t nverts = mesh->face_vertex_counts[f];
    if (idx_offset + nverts > mesh->face_vertex_indices.size()) return false;
    const bool is_hole = std::binary_search(mesh->hole_faces.begin(),
                                            mesh->hole_faces.end(),
                                            static_cast<uint32_t>(f));
    if (nverts >= 3 && !is_hole) {
      auto emit_triangle = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (mesh->left_handed) std::swap(b, c);
        const uint32_t corners[3] = {a, b, c};
        for (uint32_t corner : corners) {
          mesh->triangulated_indices.push_back(
              mesh->face_vertex_indices[idx_offset + corner]);
          mesh->triangulated_face_vertex_indices.push_back(
              static_cast<uint32_t>(idx_offset + corner));
        }
      };

      if (nverts == 4) {
        // Split the quad along the SHORTER diagonal (legacy parity): better
        // triangle quality, and non-planar / concave-ish quads render
        // correctly. A tie (e.g. a planar rectangle) keeps the classic
        // 0-2 fan split.
        const uint32_t i0 = mesh->face_vertex_indices[idx_offset + 0];
        const uint32_t i1 = mesh->face_vertex_indices[idx_offset + 1];
        const uint32_t i2 = mesh->face_vertex_indices[idx_offset + 2];
        const uint32_t i3 = mesh->face_vertex_indices[idx_offset + 3];
        auto dist_sq = [&](uint32_t a, uint32_t b) -> float {
          const float dx = mesh->points[size_t(a) * 3 + 0] -
                           mesh->points[size_t(b) * 3 + 0];
          const float dy = mesh->points[size_t(a) * 3 + 1] -
                           mesh->points[size_t(b) * 3 + 1];
          const float dz = mesh->points[size_t(a) * 3 + 2] -
                           mesh->points[size_t(b) * 3 + 2];
          return dx * dx + dy * dy + dz * dz;
        };
        if (dist_sq(i1, i3) < dist_sq(i0, i2)) {
          // Diagonal 1-3: triangles (0,1,3) and (1,2,3).
          emit_triangle(0, 1, 3);
          emit_triangle(1, 2, 3);
        } else {
          // Diagonal 0-2: triangles (0,1,2) and (0,2,3).
          emit_triangle(0, 1, 2);
          emit_triangle(0, 2, 3);
        }
        idx_offset += nverts;
        continue;
      }

      bool used_earcut = false;
      if (config_.mesh.triangulation_method ==
              MeshConfig::TriangulationMethod::Earcut &&
          nverts > 4) {
        if (nverts > kEarcutMaxVertices) {
          // Extremely large polygons are safer with fan triangulation in this
          // converter to avoid temporary O(nverts) geometry explosions in
          // earcut allocation paths.
          used_earcut = false;
        } else {
        using Point2 = EarcutPoint2;
        std::vector<std::vector<Point2>>& polygon = earcut_polygon;
        polygon[0].clear();
        polygon[0].reserve(nverts);

        // Newell normal chooses the projection plane with the largest area,
        // keeping concave and non-axis-aligned polygons stable.
        double normal[3] = {0.0, 0.0, 0.0};
        for (uint32_t i = 0; i < nverts; ++i) {
          const uint32_t ia = mesh->face_vertex_indices[idx_offset + i];
          const uint32_t ib =
              mesh->face_vertex_indices[idx_offset + ((i + 1) % nverts)];
          const size_t a = static_cast<size_t>(ia) * 3;
          const size_t b = static_cast<size_t>(ib) * 3;
          normal[0] += (mesh->points[a + 1] - mesh->points[b + 1]) *
                       (mesh->points[a + 2] + mesh->points[b + 2]);
          normal[1] += (mesh->points[a + 2] - mesh->points[b + 2]) *
                       (mesh->points[a] + mesh->points[b]);
          normal[2] += (mesh->points[a] - mesh->points[b]) *
                       (mesh->points[a + 1] + mesh->points[b + 1]);
        }
        int drop_axis = 0;
        if (std::fabs(normal[1]) > std::fabs(normal[drop_axis])) drop_axis = 1;
        if (std::fabs(normal[2]) > std::fabs(normal[drop_axis])) drop_axis = 2;
        for (uint32_t i = 0; i < nverts; ++i) {
          const uint32_t vertex = mesh->face_vertex_indices[idx_offset + i];
          const size_t p = static_cast<size_t>(vertex) * 3;
          if (drop_axis == 0) {
            polygon[0].push_back({mesh->points[p + 1], mesh->points[p + 2]});
          } else if (drop_axis == 1) {
            polygon[0].push_back({mesh->points[p], mesh->points[p + 2]});
          } else {
            polygon[0].push_back({mesh->points[p], mesh->points[p + 1]});
          }
        }
        const std::vector<uint32_t> local =
            mapbox::earcut<uint32_t>(polygon);
        if (!local.empty() && (local.size() % 3) == 0) {
          used_earcut = true;
          // earcut emits every triangle in ONE fixed 2D orientation
          // regardless of the input ring winding. The triangles must follow
          // the AUTHORED ring winding (so their 3D orientation matches the
          // face); compare the projected ring's signed area against the
          // orientation of the first non-degenerate output triangle and flip
          // when they disagree. (The previous unconditional reverse flipped
          // faces whose projection preserved orientation — e.g. a
          // +dominant-axis polygon projected without an axis-order swap.)
          double ring_area2 = 0.0;  // 2x signed area of the projected ring
          const std::vector<Point2>& ring = polygon[0];
          for (size_t i = 0; i < ring.size(); ++i) {
            const Point2& a = ring[i];
            const Point2& b = ring[(i + 1) % ring.size()];
            ring_area2 += a[0] * b[1] - b[0] * a[1];
          }
          double tri_cross = 0.0;
          for (size_t i = 0; i < local.size() && tri_cross == 0.0; i += 3) {
            const Point2& a = ring[local[i]];
            const Point2& b = ring[local[i + 1]];
            const Point2& c = ring[local[i + 2]];
            tri_cross = (b[0] - a[0]) * (c[1] - a[1]) -
                        (b[1] - a[1]) * (c[0] - a[0]);
          }
          const bool flip = (ring_area2 != 0.0) && (tri_cross != 0.0) &&
                            ((ring_area2 > 0.0) != (tri_cross > 0.0));
          for (size_t i = 0; i < local.size(); i += 3) {
            if (flip) {
              emit_triangle(local[i], local[i + 2], local[i + 1]);
            } else {
              emit_triangle(local[i], local[i + 1], local[i + 2]);
            }
          }
        } else {
          warnings_.push_back("Earcut failed for face " + std::to_string(f) +
                              " of " + mesh->prim_path +
                              "; using triangle fan fallback");
        }
        }
      }

      if (!used_earcut) {
        for (uint32_t i = 1; i < nverts - 1; ++i) {
          emit_triangle(0, i, i + 1);
        }
      }
    }
    idx_offset += nverts;
  }
  mesh->face_triangle_offsets[mesh->face_vertex_counts.size()] =
      static_cast<uint32_t>(mesh->triangulated_indices.size() / 3);

  mesh->is_triangulated = true;
  return true;
}

bool RenderSceneConverter::TriangulateFan(
    const uint32_t* face_vertex_counts, size_t face_count,
    const uint32_t* indices, size_t index_count,
    UInt32Chunked* out_indices) {

  // Count triangles
  size_t tri_count = 0;
  size_t required_index_count = 0;
  for (size_t i = 0; i < face_count; ++i) {
    uint32_t nverts = face_vertex_counts[i];
    required_index_count += nverts;
    if (nverts >= 3) {
      tri_count += nverts - 2;
    }
  }
  if (required_index_count > index_count) {
    return false;
  }

  out_indices->reserve(tri_count * 3);

  size_t idx_offset = 0;
  for (size_t f = 0; f < face_count; ++f) {
    uint32_t nverts = face_vertex_counts[f];
    if (nverts < 3) {
      idx_offset += nverts;
      continue;
    }

    // Triangle fan: v0, v1, v2; v0, v2, v3; v0, v3, v4; ...
    uint32_t v0 = indices[idx_offset];
    for (uint32_t i = 1; i < nverts - 1; ++i) {
      out_indices->push_back(v0);
      out_indices->push_back(indices[idx_offset + i]);
      out_indices->push_back(indices[idx_offset + i + 1]);
    }

    idx_offset += nverts;
  }

  return true;
}

//
// Normal computation
//

bool RenderSceneConverter::ComputeVertexNormals(RenderMesh* mesh) {
  if (mesh->points.empty() || !mesh->is_triangulated) {
    // Need triangulated mesh for normal computation
    if (!mesh->is_triangulated) {
      TriangulateMesh(mesh);
    }
    if (!mesh->is_triangulated) return false;
  }

  size_t num_points = mesh->point_count();
  size_t num_tris = mesh->triangulated_indices.size() / 3;

  // Initialize normals to zero
  if (!mesh->normals.resize(num_points * 3, 0.0f)) {
    warnings_.push_back("Out of memory computing normals for mesh '" +
                        mesh->prim_path + "'");
    return false;
  }

  // Accumulate face normals at each vertex
  for (size_t t = 0; t < num_tris; ++t) {
    uint32_t i0 = mesh->triangulated_indices[t * 3 + 0];
    uint32_t i1 = mesh->triangulated_indices[t * 3 + 1];
    uint32_t i2 = mesh->triangulated_indices[t * 3 + 2];

    float p0[3] = {mesh->points[i0*3], mesh->points[i0*3+1], mesh->points[i0*3+2]};
    float p1[3] = {mesh->points[i1*3], mesh->points[i1*3+1], mesh->points[i1*3+2]};
    float p2[3] = {mesh->points[i2*3], mesh->points[i2*3+1], mesh->points[i2*3+2]};

    // Edge vectors
    float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
    float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

    // Cross product
    float n[3] = {
      e1[1]*e2[2] - e1[2]*e2[1],
      e1[2]*e2[0] - e1[0]*e2[2],
      e1[0]*e2[1] - e1[1]*e2[0]
    };

    // Add to each vertex
    mesh->normals[i0*3+0] += n[0]; mesh->normals[i0*3+1] += n[1]; mesh->normals[i0*3+2] += n[2];
    mesh->normals[i1*3+0] += n[0]; mesh->normals[i1*3+1] += n[1]; mesh->normals[i1*3+2] += n[2];
    mesh->normals[i2*3+0] += n[0]; mesh->normals[i2*3+1] += n[1]; mesh->normals[i2*3+2] += n[2];
  }

  // Normalize
  for (size_t v = 0; v < num_points; ++v) {
    float nx = mesh->normals[v*3+0];
    float ny = mesh->normals[v*3+1];
    float nz = mesh->normals[v*3+2];
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-8f) {
      mesh->normals[v*3+0] = nx / len;
      mesh->normals[v*3+1] = ny / len;
      mesh->normals[v*3+2] = nz / len;
    } else {
      mesh->normals[v*3+0] = 0.0f;
      mesh->normals[v*3+1] = 1.0f;
      mesh->normals[v*3+2] = 0.0f;
    }
  }

  mesh->normals_interp = Interpolation::Vertex;
  return true;
}

//
// Material conversion
//

std::string RenderSceneConverter::ResolveAssetPath(const std::string& file,
                                                   uint32_t asset_anchor_id) const {
  if (file.empty() || file[0] == '/' ||
      file.find("://") != std::string::npos) {
    return file;
  }

  // A relative asset path anchors to the LAYER THAT AUTHORED IT, not to the
  // stage's root layer. `asset_anchor_id` carries that layer's directory through
  // composition (see asset-anchor.hh); production look layers sit several dirs
  // below the root and reach their textures with `../..`, so anchoring at the
  // root yields a nonexistent path. Fall back to the stage base dir for prims
  // with no anchor (the root layer itself, and USDZ package entries).
  const std::string& anchor =
      ::tinyusdz::next::AssetAnchorPath(asset_anchor_id);
  const std::string& base = anchor.empty() ? config_.asset_base_dir : anchor;

  // A configured AssetResolver owns resolution (anchor/search paths + suffix
  // fallback). Anchor it at the AUTHORING layer's directory, same as above.
  if (config_.asset_resolver) {
    ::tinyusdz::next::ResolvedAsset resolved = config_.asset_resolver->Resolve(
        file, base.empty() ? std::string() : base + "/");
    if (resolved.exists && !resolved.resolved_path.empty()) {
      return resolved.resolved_path;
    }
    return file;
  }

  if (base.empty()) return file;

  // Normalize: the join commonly yields `look/binding/../../texture/x.png`, and
  // an un-collapsed path would also defeat the `resolved_path` image dedup below.
  return ::tinyusdz::next::AssetResolver::NormalizePath(
      ::tinyusdz::next::AssetResolver::JoinPath(base, file));
}

uint32_t RenderSceneConverter::AssetAnchorOf(const UsdPrim& prim) {
  const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
  return spec ? spec->asset_anchor_id() : 0u;
}

// Dedup key for scene->images. Both image-dedup sites used a linear scan with
// full string compares over every image already in the scene -- O(n^2) with a
// long-path comparison per step (a 5000-image scene cost ~12.5M string
// compares). image_id_by_key_ makes it O(1); FindImageId/RememberImageId are
// the single place that maintains it.
std::string RenderSceneConverter::ImageKey(const std::string& resolved_path,
                                           ColorSpace cs) {
  // '\x1f' = unit separator, never valid in a path.
  return resolved_path + '\x1f' +
         std::to_string(static_cast<int>(cs));
}

int32_t RenderSceneConverter::FindImageId(const RenderScene* scene,
                                          const std::string& resolved_path,
                                          ColorSpace cs) {
  if (!scene) return -1;
  // Rebuild lazily if the caller mutated scene->images behind our back (or
  // this is a fresh scene), so the map can never report a stale index.
  if (image_id_by_key_.size() != scene->images.size()) {
    image_id_by_key_.clear();
    image_id_by_key_.reserve(scene->images.size());
    for (size_t i = 0; i < scene->images.size(); ++i) {
      image_id_by_key_.emplace(
          ImageKey(scene->images[i].resolved_path, scene->images[i].color_space),
          static_cast<int32_t>(i));
    }
  }
  auto it = image_id_by_key_.find(ImageKey(resolved_path, cs));
  return it == image_id_by_key_.end() ? -1 : it->second;
}

void RenderSceneConverter::RememberImageId(const RenderScene* scene,
                                           const std::string& resolved_path,
                                           ColorSpace cs, int32_t id) {
  (void)scene;
  image_id_by_key_.emplace(ImageKey(resolved_path, cs), id);
}

int32_t RenderSceneConverter::ResolveImageId(RenderScene* scene,
                                             const std::string& file,
                                             ColorSpace color_space,
                                             uint32_t asset_anchor_id) {
  if (!scene || file.empty()) return -1;
  const std::string resolved = ResolveAssetPath(file, asset_anchor_id);
  const ColorSpace csp =
      color_space == ColorSpace::Unknown ? ColorSpace::sRGB : color_space;
  const int32_t hit = FindImageId(scene, resolved, csp);
  if (hit >= 0) return hit;
  TextureImage image;
  image.name = file;
  image.resolved_path = resolved;
  image.color_space = csp;
  const int32_t id = static_cast<int32_t>(scene->images.size());
  scene->images.push_back(std::move(image));
  RememberImageId(scene, resolved, csp, id);
  return id;
}

bool RenderSceneConverter::ConvertMaterial(const Stage& stage,
                                           const UsdPrim& prim,
                                           RenderMaterial* out) {
  return ConvertMaterial(stage, prim, out, nullptr);
}

bool RenderSceneConverter::ConvertMaterial(const Stage& stage,
                                           const UsdPrim& prim,
                                           RenderMaterial* out,
                                           RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsMaterial(prim)) {
    last_error_ = "Invalid material prim";
    return false;
  }

  // The rendering color config depends only on `stage` and
  // config_.material.render_settings_path -- both invariant across a
  // conversion -- yet this did a stage path lookup, token canonicalization,
  // color-space resolve and a 3x3 transform build once PER MATERIAL, writing
  // the identical result to `scene` every time.
  if (scene && scene != color_config_scene_) {
    color_config_scene_ = scene;
    ::tinyusdz::next::color_management::RenderingColorConfig color_config;
    std::string color_warning;
    if (::tinyusdz::next::color_management::ResolveRenderingColorConfig(
            stage, config_.material.render_settings_path, &color_config,
            &color_warning)) {
      scene->render_settings_path = color_config.render_settings_path;
      scene->working_color_space = color_config.working_space;
      ::tinyusdz::color::ColorSpaceDesc display_linear;
      ::tinyusdz::color::ColorTransform display_transform;
      if (::tinyusdz::color::GetBuiltinColorSpace("lin_rec709_scene",
                                                   &display_linear) &&
          ::tinyusdz::color::BuildColorTransform(
              color_config.working_definition, display_linear,
              &display_transform)) {
        std::copy(display_transform.matrix, display_transform.matrix + 9,
                  scene->working_to_display_linear);
      }
      (void)color_warning;
    }
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  ExtractMaterialXConfig(prim, &out->mtlx_config);

  // Displacement/volume terminal metadata (legacy parity: recorded, not
  // converted into shader networks).
  {
    const std::string disp =
        ::tinyusdz::next::GetDisplacementShader(stage, prim);
    if (!disp.empty()) {
      out->has_displacement = true;
      out->displacement_shader_path = disp;
    }
    const std::string vol = ::tinyusdz::next::GetVolumeShader(stage, prim);
    if (!vol.empty()) {
      out->has_volume = true;
      out->volume_shader_path = vol;
    }
  }

  // Find shader(s) in material. The material's `outputs:surface` connection
  // names the authoritative surface shader (child iteration order previously
  // decided ties, and shaders living OUTSIDE the material prim never resolved).
  bool found_shader = false;
  std::vector<UsdPrim> degraded_candidates;

  std::vector<UsdPrim> candidates;
  // Materials exported by Blender commonly author both a PreviewSurface
  // fallback on outputs:surface and the authoritative MaterialX graph on
  // outputs:mtlx:surface. Prefer the explicit MaterialX render context; using
  // the fallback made every graph-driven parameter look unauthored.
  if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
    const std::vector<::tinyusdz::next::Path>* mtlx_surface =
        spec->connection("outputs:mtlx:surface");
    if (mtlx_surface && !mtlx_surface->empty()) {
      UsdPrim sp = stage.GetPrimAtPath(
          SourcePrimPathFromConnection((*mtlx_surface)[0].str()));
      if (sp.IsValid()) candidates.push_back(sp);
    }
  }
  {
    // When both MaterialX and universal PreviewSurface terminals are authored,
    // prefer the MaterialX terminal. Blender commonly emits a textured OpenPBR
    // graph plus an untextured PreviewSurface fallback; selecting the fallback
    // here silently turns the material white.
    ::tinyusdz::next::AttributeEval eval(&stage);
    if (eval.HasConnection(prim, "outputs:mtlx:surface")) {
      const std::string mtlx_surface =
          eval.GetConnectionPath(prim, "outputs:mtlx:surface");
      UsdPrim sp = stage.GetPrimAtPath(
          SourcePrimPathFromConnection(mtlx_surface));
      if (sp.IsValid()) candidates.push_back(sp);
    } else if (const std::vector<::tinyusdz::next::Path>* targets =
                   prim.GetRelationship("outputs:mtlx:surface")) {
      if (!targets->empty()) {
        UsdPrim sp = stage.GetPrimAtPath(
            SourcePrimPathFromConnection((*targets)[0].str()));
        if (sp.IsValid()) candidates.push_back(sp);
      }
    }
    // External MaterialX references are synthesized by the next layer
    // registry with this relationship instead of outputs:mtlx:surface.
    if (const std::vector<::tinyusdz::next::Path>* sources =
            prim.GetRelationship("mtlx:surface:source")) {
      if (!sources->empty()) {
        UsdPrim sp = stage.GetPrimAtPath(sources->front().str());
        if (sp.IsValid() &&
            std::none_of(candidates.begin(), candidates.end(),
                         [&sp](const UsdPrim& p) {
                           return p.GetPath().str() == sp.GetPath().str();
                         })) {
          candidates.push_back(sp);
        }
      }
    }
    const std::string surf =
        ::tinyusdz::next::GetSurfaceShader(stage, prim);
    if (!surf.empty()) {
      UsdPrim sp = stage.GetPrimAtPath(surf);
      if (sp.IsValid() &&
          std::none_of(candidates.begin(), candidates.end(),
                       [&sp](const UsdPrim& p) {
                         return p.GetPath().str() == sp.GetPath().str();
                       })) {
        candidates.push_back(sp);
      }
    }
  }
  for (const auto& child : prim.GetChildren()) {
    candidates.push_back(child);
  }

  // A common UsdShade authoring pattern puts the terminal shader behind one
  // or more NodeGraph output pass-throughs.  The graph is the material's
  // surface target, but the shader is the object that carries the usable
  // `info:id` and inputs.  Resolve these hops before classifying the material
  // as degraded.  Keep this local to material discovery so parameter
  // extraction still follows the original shader connections.
  {
    std::set<std::string> visited;
    std::function<void(const UsdPrim&, int)> append_graph_shader =
        [&](const UsdPrim& source, int depth) {
          if (!source.IsValid() || depth > 16 ||
              !visited.insert(source.GetPath().str()).second) {
            return;
          }
          if (!::tinyusdz::next::IsNodeGraph(source)) return;

          const ::tinyusdz::next::PrimSpec* spec = source.GetPrimSpec();
          if (!spec) return;
          static const char* kOutputs[] = {"outputs:surface", "outputs:out"};
          for (const char* output : kOutputs) {
            const std::vector<::tinyusdz::next::Path>* connections =
                spec->connection(output);
            if (!connections || connections->empty()) continue;
            UsdPrim target = stage.GetPrimAtPath(
                SourcePrimPathFromConnection((*connections)[0].str()));
            if (!target.IsValid()) continue;
            candidates.push_back(target);
            if (::tinyusdz::next::IsNodeGraph(target)) {
              append_graph_shader(target, depth + 1);
            }
            return;
          }
        };
    const size_t initial_count = candidates.size();
    for (size_t i = 0; i < initial_count; ++i) {
      append_graph_shader(candidates[i], 0);
    }
  }

  for (const auto& child : candidates) {
    if (found_shader) break;
    if (::tinyusdz::next::IsShader(child)) {
      std::string shader_id;
      GetToken(child, "info:id", &shader_id);

      if (shader_id == "UsdPreviewSurface" ||
          shader_id == "ND_UsdPreviewSurface_surfaceshader") {
        // MaterialX's UsdPreviewSurface node (`ND_UsdPreviewSurface_surfaceshader`)
        // has the same inputs as UsdPreviewSurface — treat it as one (matches the
        // legacy tydra path, e.g. usd-wg MaterialXTest/basic_flatten).
        out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
        out->preview_surface = std::make_unique<PreviewSurfaceShader>();
        ExtractPreviewSurface(stage, child, out->preview_surface.get(), scene);
        if (out->preview_surface->opacity.is_texture() ||
            out->preview_surface->opacity.value.x < 1.0f - kAlphaEpsilon) {
          out->alpha_mode = RenderMaterial::AlphaMode::Blend;
        }
        if (out->preview_surface->opacity_threshold.value.x > kAlphaEpsilon) {
          out->alpha_mode = RenderMaterial::AlphaMode::Mask;
          out->alpha_cutoff = out->preview_surface->opacity_threshold.value.x;
        }
        found_shader = true;
      } else if (IsOpenPBRShaderId(shader_id)) {
        out->shader_type = RenderMaterial::ShaderType::OpenPBR;
        out->openpbr = std::make_unique<OpenPBRSurfaceShader>();
        ExtractOpenPBRSurface(stage, child, out->openpbr.get(), scene);
        if (out->openpbr->opacity.is_texture() ||
            out->openpbr->opacity.value.x < 1.0f - kAlphaEpsilon ||
            out->openpbr->transmission_weight.value.x > kAlphaEpsilon) {
          // Transmissive OpenPBR (glass) needs the blend path even at
          // opacity 1 (legacy marks these Translucent).
          out->alpha_mode = RenderMaterial::AlphaMode::Blend;
        }
        found_shader = true;
      } else if (IsStandardSurfaceShaderId(shader_id)) {
        // MaterialX standard_surface maps onto OpenPBR (legacy tydra's
        // ConvertMtlxStandardSurfaceToOpenPBRSurface table).
        out->shader_type = RenderMaterial::ShaderType::OpenPBR;
        out->openpbr = std::make_unique<OpenPBRSurfaceShader>();
        ExtractStandardSurfaceAsOpenPBR(stage, child, out->openpbr.get(), scene);
        if (out->openpbr->opacity.is_texture() ||
            out->openpbr->opacity.value.x < 1.0f - kAlphaEpsilon) {
          out->alpha_mode = RenderMaterial::AlphaMode::Blend;
        }
        found_shader = true;
      } else {
        // Keep the authoritative unsupported shader (and then any unsupported
        // child shaders) as degraded-material sources. Engine shaders and
        // newer MaterialX nodes often retain familiar PBR input names even
        // when their full implementation cannot be evaluated.
        degraded_candidates.push_back(child);
        MaterialDiagnostic diagnostic;
        diagnostic.kind = shader_id.rfind("ND_", 0) == 0
                              ? MaterialDiagnosticKind::UnsupportedMaterialXNode
                              : MaterialDiagnosticKind::UnsupportedShader;
        diagnostic.material_path = out->prim_path;
        diagnostic.node_path = child.GetPath().str();
        diagnostic.shader_id = shader_id;
        diagnostic.message = "unsupported surface shader";
        out->diagnostics.push_back(std::move(diagnostic));
      }
    }
  }

  if (!found_shader) {
    // MaterialX surface shaders (e.g. ND_standard_surface_surfaceshader):
    // convert through the MaterialX -> PreviewSurface mapping.
    MtlxConverter mtlx;
    RenderMaterial mtlx_out;
    // We are the fallback path: we already failed to find a shader we know, so
    // the converter must not hand this same material back to us.
    if (mtlx.ConvertUsdMtlxMaterial(stage, prim, &mtlx_out,
                                    /*allow_converter_delegation=*/false)) {
      if (mtlx_out.preview_surface && !mtlx_out.default_fallback) {
        out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
        out->preview_surface = std::move(mtlx_out.preview_surface);
        out->alpha_mode = mtlx_out.alpha_mode;
        out->alpha_cutoff = mtlx_out.alpha_cutoff;
        if (!out->mtlx_config.authored) {
          out->mtlx_config = std::move(mtlx_out.mtlx_config);
        }
        found_shader = true;
      } else if (!out->mtlx_config.authored && mtlx_out.mtlx_config.authored) {
        // Retain the MaterialX document metadata even when its surface node is
        // unsupported and the PBR inputs below have to be salvaged by name.
        out->mtlx_config = std::move(mtlx_out.mtlx_config);
      }
    }
  }

  if (!found_shader) {
    // No convertible surface shader. This happens for materials that only
    // reference an engine source asset and author no UsdPreviewSurface — e.g.
    // Unreal Engine USD exports whose surface is `outputs:unreal:surface` ->
    // an `info:implementationSource = "sourceAsset"` shader
    // (`info:unreal:sourceAsset = @...uasset@`), as in MetaHuman face/body
    // materials — or whose surface connection doesn't resolve after
    // composition. Emit a per-material degraded PreviewSurface rather than
    // dropping it. Recover conventional PBR input aliases from the unsupported
    // surface shader first, then from Material interface inputs. This preserves
    // the useful constants around an unsupported node instead of replacing the
    // entire material with shared gray.
    out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
    out->preview_surface = std::make_unique<PreviewSurfaceShader>();
    out->default_fallback = true;

    PreviewSurfaceShader* degraded = out->preview_surface.get();
    // White is the neutral multiplier for a mesh-authored displayColor. If no
    // recognizable base color can be recovered, this lets displayColor remain
    // visible instead of being darkened by the PreviewSurface schema fallback.
    SetParamFloat3(&degraded->diffuse_color, 1.0f, 1.0f, 1.0f);

    size_t recovered_count = 0;
    auto recover = [&](ShaderParam* dst,
                       std::initializer_list<const char*> aliases) {
      // An unsupported terminal appears first in candidates; deduplicate child
      // traversal so texture/image nodes cannot accidentally override it.
      std::set<std::string> visited;
      auto recover_from = [&](const UsdPrim& source) {
        if (!source.IsValid() ||
            !visited.insert(source.GetPath().str()).second) {
          return false;
        }
        for (const char* alias : aliases) {
          ShaderParam value = *dst;
          if (ExtractShaderParam(stage, source, alias, &value, scene)) {
            *dst = value;
            ++recovered_count;
            return true;
          }
        }
        return false;
      };
      for (const UsdPrim& source : degraded_candidates) {
        if (recover_from(source)) return;
      }
      // Material interface inputs are common in MaterialX exports and remain
      // meaningful even when the connected surface implementation is unknown.
      recover_from(prim);
    };

    // Preserve every evaluatable authored input from the unsupported terminal
    // and the material interface, not only the conventional aliases above.
    // This keeps advanced lobes and future shader inputs available without
    // making the degraded PreviewSurface pretend to evaluate them.
    std::set<std::string> retained_names;
    auto retain_inputs = [&](const UsdPrim& source, const std::string& shader) {
      if (!source.IsValid()) return;
      for (const std::string& property : source.GetPropertyNames()) {
        constexpr const char* kPrefix = "inputs:";
        if (property.rfind(kPrefix, 0) != 0) continue;
        const std::string name = property.substr(7);
        if (name.empty() || !retained_names.insert(name).second) continue;
        ShaderParam value;
        if (!ExtractShaderParam(stage, source, name, &value, scene)) continue;
        RetainedMaterialParam param;
        param.shader = shader;
        param.name = name;
        param.value = value;
        out->retained_params.push_back(std::move(param));
      }
    };
    for (const UsdPrim& source : degraded_candidates) {
      std::string shader;
      GetToken(source, "info:id", &shader);
      retain_inputs(source, shader);
    }
    retain_inputs(prim, "MaterialInterface");

    recover(&degraded->diffuse_color,
            {"diffuseColor", "baseColor", "base_color", "color"});
    recover(&degraded->emissive_color,
            {"emissiveColor", "emissionColor", "emission_color"});
    recover(&degraded->specular_color,
            {"specularColor", "specular_color"});
    recover(&degraded->metallic,
            {"metallic", "metalness", "base_metalness"});
    recover(&degraded->roughness,
            {"roughness", "base_roughness", "specular_roughness"});
    recover(&degraded->clearcoat,
            {"clearcoat", "coat", "coat_weight"});
    recover(&degraded->clearcoat_roughness,
            {"clearcoatRoughness", "coat_roughness"});
    recover(&degraded->opacity,
            {"opacity", "geometry_opacity", "alpha"});
    recover(&degraded->opacity_threshold,
            {"opacityThreshold", "alphaCutoff", "alpha_cutoff"});
    recover(&degraded->ior, {"ior", "specular_ior", "specular_IOR"});
    recover(&degraded->normal, {"normal", "geometry_normal"});
    recover(&degraded->displacement, {"displacement"});
    recover(&degraded->occlusion, {"occlusion"});

    ShaderParam use_spec;
    recover(&use_spec, {"useSpecularWorkflow", "use_specular_workflow"});
    degraded->use_specular_workflow = use_spec.value.x >= 0.5f;

    if (degraded->opacity.is_texture() ||
        degraded->opacity.value.x < 1.0f - kAlphaEpsilon) {
      out->alpha_mode = RenderMaterial::AlphaMode::Blend;
    }
    if (degraded->opacity_threshold.value.x > kAlphaEpsilon) {
      out->alpha_mode = RenderMaterial::AlphaMode::Mask;
      out->alpha_cutoff = degraded->opacity_threshold.value.x;
    }
    warnings_.push_back(
        "Material '" + out->prim_path +
        "' has no fully convertible surface shader; using a degraded material "
        "with " + std::to_string(recovered_count) +
        " recovered input(s).");
    MaterialDiagnostic diagnostic;
    diagnostic.kind = MaterialDiagnosticKind::DegradedMaterial;
    diagnostic.material_path = out->prim_path;
    diagnostic.node_path = degraded_candidates.empty()
                               ? std::string()
                               : degraded_candidates.front().GetPath().str();
    if (!degraded_candidates.empty()) {
      GetToken(degraded_candidates.front(), "info:id", &diagnostic.shader_id);
    }
    diagnostic.message = "using degraded material with " +
                         std::to_string(recovered_count) +
                         " recovered input(s)";
    out->diagnostics.push_back(std::move(diagnostic));
    found_shader = true;
  }

  // Translate the production RenderMan Ptex displacement pattern used by the
  // Island asset into the renderer-neutral PreviewSurface displacement lane:
  //
  // PxrDisplace.dispScalar <- PxrDispTransform.dispScalar <- PxrPtexture
  // result = (texture - dispCenter) * dispAmount  (dispRemapMode == 2)
  //
  // Keep this compatibility adapter narrow and diagnosed. Unknown Pxr graphs
  // remain visible as unsupported displacement instead of being guessed.
  if (found_shader && out->preview_surface && out->has_displacement && scene) {
    const UsdPrim displacement =
        stage.GetPrimAtPath(out->displacement_shader_path);
    std::string displacementId;
    GetToken(displacement, "info:id", &displacementId);
    if (displacement.IsValid() && displacementId == "PxrDisplace") {
      ::tinyusdz::next::AttributeEval eval(&stage);
      eval.SetTime(config_.time_code);
      const std::string scalarConnection =
          eval.GetConnectionPath(displacement, "inputs:dispScalar");
      const UsdPrim transform = stage.GetPrimAtPath(
          SourcePrimPathFromConnection(scalarConnection));
      std::string transformId;
      GetToken(transform, "info:id", &transformId);
      const std::optional<int32_t> remap =
          transform.IsValid()
              ? eval.EvalInt(transform, "inputs:dispRemapMode")
              : std::optional<int32_t>();
      if (transform.IsValid() && transformId == "PxrDispTransform" &&
          (!remap || *remap == 2)) {
        ShaderParam scalar;
        ShaderParam amountParam;
        ShaderParam centerParam;
        const bool haveScalar = ExtractShaderParam(
            stage, displacement, "dispScalar", &scalar, scene);
        const bool haveAmount = ExtractShaderParam(
            stage, displacement, "dispAmount", &amountParam, scene);
        const bool haveCenter = ExtractShaderParam(
            stage, transform, "dispCenter", &centerParam, scene);
        const float amount = haveAmount ? amountParam.value.x : 1.0f;
        const float center = haveCenter ? centerParam.value.x : 0.0f;
        if (haveScalar) {
          if (scalar.texture_id >= 0 &&
              static_cast<size_t>(scalar.texture_id) < scene->textures.size()) {
            RenderTexture& texture =
                scene->textures[static_cast<size_t>(scalar.texture_id)];
            texture.scale_value.x *= amount;
            texture.bias.x = texture.bias.x * amount - center * amount;
            texture.source_color_space = "raw";
            if (texture.image_id >= 0 &&
                static_cast<size_t>(texture.image_id) < scene->images.size()) {
              scene->images[static_cast<size_t>(texture.image_id)].color_space =
                  ColorSpace::Raw;
            }
          } else {
            scalar.value.x = (scalar.value.x - center) * amount;
          }
          out->preview_surface->displacement = scalar;
        } else {
          warnings_.push_back("Material '" + out->prim_path +
                              "' has an unreadable Pxr Ptex displacement graph");
        }
      } else {
        MaterialDiagnostic diagnostic;
        diagnostic.kind = MaterialDiagnosticKind::UnsupportedShader;
        diagnostic.material_path = out->prim_path;
        diagnostic.node_path = transform.IsValid()
                                   ? transform.GetPath().str()
                                   : out->displacement_shader_path;
        diagnostic.shader_id = transformId;
        diagnostic.message = "unsupported Pxr displacement transform/remap mode";
        out->diagnostics.push_back(std::move(diagnostic));
      }
    }
  }

  return found_shader;
}

bool RenderSceneConverter::ExtractPreviewSurface(const Stage& stage,
                                                 const UsdPrim& shader_prim,
                                                 PreviewSurfaceShader* out,
                                                 RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  ExtractShaderParam(stage, shader_prim, "diffuseColor", &out->diffuse_color, scene);
  ExtractShaderParam(stage, shader_prim, "emissiveColor", &out->emissive_color, scene);
  ExtractShaderParam(stage, shader_prim, "specularColor", &out->specular_color, scene);
  ExtractShaderParam(stage, shader_prim, "metallic", &out->metallic, scene);
  ExtractShaderParam(stage, shader_prim, "roughness", &out->roughness, scene);
  ExtractShaderParam(stage, shader_prim, "clearcoat", &out->clearcoat, scene);
  ExtractShaderParam(stage, shader_prim, "clearcoatRoughness",
                     &out->clearcoat_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "opacity", &out->opacity, scene);
  ExtractShaderParam(stage, shader_prim, "opacityThreshold",
                     &out->opacity_threshold, scene);
  ExtractShaderParam(stage, shader_prim, "ior", &out->ior, scene);
  ExtractShaderParam(stage, shader_prim, "normal", &out->normal, scene);
  ExtractShaderParam(stage, shader_prim, "displacement", &out->displacement, scene);
  ExtractShaderParam(stage, shader_prim, "occlusion", &out->occlusion, scene);

  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(config_.time_code);
  if (std::optional<int32_t> use_spec =
          eval.EvalInt(shader_prim, "inputs:useSpecularWorkflow")) {
    out->use_specular_workflow = (*use_spec != 0);
  }

  return true;
}

// MaterialX standard_surface -> OpenPBR field mapping (mirrors legacy
// ConvertMtlxStandardSurfaceToOpenPBRSurface). ExtractShaderParam follows
// connections, so textured inputs (ND_image chains that resolve to a file)
// come through as textures.
bool RenderSceneConverter::ExtractStandardSurfaceAsOpenPBR(
    const Stage& stage, const UsdPrim& shader_prim, OpenPBRSurfaceShader* out,
    RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  // Base layer
  ExtractShaderParam(stage, shader_prim, "base", &out->base_weight, scene);
  ExtractShaderParam(stage, shader_prim, "base_color", &out->base_color, scene);
  ExtractShaderParam(stage, shader_prim, "diffuse_roughness",
                     &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "metalness", &out->base_metalness,
                     scene);

  // Specular layer
  ExtractShaderParam(stage, shader_prim, "specular", &out->specular_weight,
                     scene);
  ExtractShaderParam(stage, shader_prim, "specular_color",
                     &out->specular_color, scene);
  ExtractShaderParam(stage, shader_prim, "specular_roughness",
                     &out->specular_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "specular_IOR", &out->specular_ior,
                     scene);
  ExtractShaderParam(stage, shader_prim, "specular_anisotropy",
                     &out->specular_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "specular_roughness_anisotropy",
                     &out->specular_roughness_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "specular_rotation",
                     &out->specular_rotation, scene);

  // Transmission
  ExtractShaderParam(stage, shader_prim, "transmission",
                     &out->transmission_weight, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_color",
                     &out->transmission_color, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_depth",
                     &out->transmission_depth, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_dispersion",
                     &out->transmission_dispersion, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_dispersion_scale",
                     &out->transmission_dispersion_scale, scene);

  // Subsurface
  ExtractShaderParam(stage, shader_prim, "subsurface",
                     &out->subsurface_weight, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_color",
                     &out->subsurface_color, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_radius",
                     &out->subsurface_radius, scene);

  // Sheen
  ExtractShaderParam(stage, shader_prim, "sheen", &out->sheen_weight, scene);
  ExtractShaderParam(stage, shader_prim, "sheen_color", &out->sheen_color,
                     scene);
  ExtractShaderParam(stage, shader_prim, "sheen_roughness",
                     &out->sheen_roughness, scene);

  // Coat
  ExtractShaderParam(stage, shader_prim, "coat", &out->coat_weight, scene);
  ExtractShaderParam(stage, shader_prim, "coat_color", &out->coat_color,
                     scene);
  ExtractShaderParam(stage, shader_prim, "coat_roughness",
                     &out->coat_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "coat_IOR", &out->coat_ior, scene);
  ExtractShaderParam(stage, shader_prim, "coat_normal", &out->coat_normal,
                     scene);

  // Emission
  ExtractShaderParam(stage, shader_prim, "emission", &out->emission_luminance,
                     scene);
  ExtractShaderParam(stage, shader_prim, "emission_color",
                     &out->emission_color, scene);

  // Geometry
  ExtractShaderParam(stage, shader_prim, "normal", &out->normal, scene);
  ExtractShaderParam(stage, shader_prim, "opacity", &out->opacity, scene);
  ExtractShaderParam(stage, shader_prim, "displacement", &out->displacement,
                     scene);

  return true;
}

bool RenderSceneConverter::ExtractOpenPBRSurface(const Stage& stage,
                                                 const UsdPrim& shader_prim,
                                                 OpenPBRSurfaceShader* out,
                                                 RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  ExtractShaderParam(stage, shader_prim, "base_weight", &out->base_weight, scene);
  ExtractShaderParam(stage, shader_prim, "base_color", &out->base_color, scene);
  ExtractShaderParam(stage, shader_prim, "baseColor", &out->base_color, scene);
  ExtractShaderParam(stage, shader_prim, "base_roughness", &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "base_diffuse_roughness",
                     &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "roughness", &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "base_metalness", &out->base_metalness, scene);
  ExtractShaderParam(stage, shader_prim, "metalness", &out->base_metalness, scene);

  ExtractShaderParam(stage, shader_prim, "specular_weight", &out->specular_weight, scene);
  ExtractShaderParam(stage, shader_prim, "specular_color", &out->specular_color, scene);
  ExtractShaderParam(stage, shader_prim, "specular_roughness",
                     &out->specular_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "specular_ior", &out->specular_ior, scene);
  ExtractShaderParam(stage, shader_prim, "specular_anisotropy",
                     &out->specular_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "specular_roughness_anisotropy",
                     &out->specular_roughness_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "specular_rotation",
                     &out->specular_rotation, scene);

  ExtractShaderParam(stage, shader_prim, "transmission_weight",
                     &out->transmission_weight, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_color",
                     &out->transmission_color, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_depth",
                     &out->transmission_depth, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_dispersion",
                     &out->transmission_dispersion, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_dispersion_scale",
                     &out->transmission_dispersion_scale, scene);

  ExtractShaderParam(stage, shader_prim, "subsurface_weight",
                     &out->subsurface_weight, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_color",
                     &out->subsurface_color, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_radius",
                     &out->subsurface_radius, scene);

  ExtractShaderParam(stage, shader_prim, "coat_weight", &out->coat_weight, scene);
  ExtractShaderParam(stage, shader_prim, "coat_color", &out->coat_color, scene);
  ExtractShaderParam(stage, shader_prim, "coat_roughness", &out->coat_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "coat_ior", &out->coat_ior, scene);
  ExtractShaderParam(stage, shader_prim, "coat_anisotropy",
                     &out->coat_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "coat_roughness_anisotropy",
                     &out->coat_roughness_anisotropy, scene);
  if (!ExtractShaderParam(stage, shader_prim, "geometry_coat_normal",
                          &out->coat_normal, scene)) {
    ExtractShaderParam(stage, shader_prim, "coat_normal", &out->coat_normal,
                       scene);
  }

  ExtractShaderParam(stage, shader_prim, "sheen_weight", &out->sheen_weight, scene);
  ExtractShaderParam(stage, shader_prim, "sheen_color", &out->sheen_color, scene);
  ExtractShaderParam(stage, shader_prim, "sheen_roughness", &out->sheen_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "thin_film_weight",
                     &out->thin_film_weight, scene);
  ExtractShaderParam(stage, shader_prim, "thin_film_thickness",
                     &out->thin_film_thickness, scene);
  ExtractShaderParam(stage, shader_prim, "thin_film_ior",
                     &out->thin_film_ior, scene);

  ExtractShaderParam(stage, shader_prim, "emission_luminance",
                     &out->emission_luminance, scene);
  ExtractShaderParam(stage, shader_prim, "emission_color", &out->emission_color, scene);

  // OpenPBR prefixes geometry inputs. Accept the older short aliases as a
  // fallback for exporters that authored pre-1.39 spellings.
  if (!ExtractShaderParam(stage, shader_prim, "geometry_opacity",
                          &out->opacity, scene)) {
    ExtractShaderParam(stage, shader_prim, "opacity", &out->opacity, scene);
  }
  if (!ExtractShaderParam(stage, shader_prim, "geometry_normal",
                          &out->normal, scene)) {
    ExtractShaderParam(stage, shader_prim, "normal", &out->normal, scene);
  }
  if (!ExtractShaderParam(stage, shader_prim, "geometry_tangent",
                          &out->tangent, scene)) {
    ExtractShaderParam(stage, shader_prim, "tangent", &out->tangent, scene);
  }
  (void)FindConnectedUtilityScalar(
      stage, shader_prim, "geometry_normal", "ND_normalmap_", "scale",
      config_.time_code, &out->normal_map_scale);
  (void)FindConnectedUtilityScalar(
      stage, shader_prim, "geometry_tangent", "ND_rotate3d_", "amount",
      config_.time_code, &out->tangent_rotation);

  return true;
}

bool RenderSceneConverter::ExtractShaderParam(const Stage& stage,
                                              const UsdPrim& shader_prim,
                                              const std::string& param_name,
                                              ShaderParam* out,
                                              RenderScene* scene) {
  // Material interface inputs use the same `inputs:*` namespace and value /
  // connection semantics as Shader inputs. Accept either here so degraded
  // material recovery can preserve constants authored on the Material prim.
  if (!out || !shader_prim.IsValid() ||
      (!::tinyusdz::next::IsShader(shader_prim) &&
       !::tinyusdz::next::IsMaterial(shader_prim))) {
    return false;
  }

  const std::string attr_name = "inputs:" + param_name;
  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(config_.time_code);

  if (eval.HasConnection(shader_prim, attr_name)) {
    std::string connection_path = eval.GetConnectionPath(shader_prim, attr_name);
    std::string evaluation_space = "lin_rec709_scene";
    UsdPrim evaluation_context = shader_prim;
    std::string evaluation_endpoint;
    if (ResolveConnectedEndpoint(stage, connection_path, config_.time_code,
                                 &evaluation_endpoint)) {
      std::string evaluation_prim_path;
      std::string evaluation_property;
      if (SplitConnectionPath(evaluation_endpoint, &evaluation_prim_path,
                              &evaluation_property)) {
        UsdPrim candidate = stage.GetPrimAtPath(evaluation_prim_path);
        if (candidate.IsValid()) evaluation_context = candidate;
      }
    }
    (void)MaterialXConfiguredColorSpace(evaluation_context,
                                        &evaluation_space);
    MtlxConstantValue evaluated;
    std::set<std::string> visiting;
    if (EvalMtlxConstantConnection(stage, connection_path, config_.time_code,
                                   evaluation_space, &evaluated, &visiting,
                                   0)) {
      if (evaluated.components >= 3) {
        SetParamFloat3(out, evaluated.value[0], evaluated.value[1],
                       evaluated.value[2]);
      } else if (evaluated.components == 2) {
        SetParamFloat4(out, evaluated.value[0], evaluated.value[1], 0.0f,
                       1.0f);
      } else {
        SetParamFloat(out, evaluated.value[0]);
      }
      if (scene && evaluated.components >= 3 && evaluated.color_managed &&
          IsColorShaderInput(shader_prim, attr_name, param_name)) {
        ::tinyusdz::color::ColorTransform transform;
        if (::tinyusdz::next::color_management::BuildColorTransform(
                evaluation_context, evaluation_space,
                scene->working_color_space, &transform)) {
          float rgb[3] = {out->value.x, out->value.y, out->value.z};
          ::tinyusdz::color::TransformRGB(transform, rgb);
          out->value.x = rgb[0];
          out->value.y = rgb[1];
          out->value.z = rgb[2];
        }
      }
      return true;
    }
    // Follow localized NodeGraph outputs and primary utility-node inputs to
    // the terminal image/value. The shared walker is cycle-safe and is also
    // used by ResolveConnectedValue below.
    std::string endpoint;
    if (ResolveConnectedEndpoint(stage, connection_path, config_.time_code,
                                 &endpoint)) {
      connection_path = std::move(endpoint);
    }
    const std::string texture_prim_path = SourcePrimPathFromConnection(connection_path);
    UsdPrim texture_prim = stage.GetPrimAtPath(texture_prim_path);

    TextureNodeData tex_data;
    if (scene && ExtractTextureNodeData(stage, texture_prim, config_.time_code, &tex_data)) {
      if (tex_data.source_color_space.empty() ||
          tex_data.source_color_space == "auto") {
        bool authored = false;
        std::string inherited;
        (void)::tinyusdz::next::color_management::ComputeColorSpaceName(
            texture_prim, "inputs:file", &inherited, &authored);
        if (authored) {
          tex_data.source_color_space = inherited;
        } else {
          (void)MaterialXConfiguredColorSpace(
              texture_prim, &tex_data.source_color_space);
        }
      }
      const ColorSpace cs = ParseColorSpace(tex_data.source_color_space);
      const ColorSpace image_color_space =
          cs == ColorSpace::Unknown ? ColorSpace::sRGB : cs;
      const std::string resolved =
          ResolveAssetPath(tex_data.file, AssetAnchorOf(texture_prim));
      int32_t image_id = FindImageId(scene, resolved, image_color_space);
      if (image_id < 0) {
        TextureImage image;
        image.name = texture_prim.IsValid() ? texture_prim.GetName() : tex_data.file;
        image.resolved_path = resolved;
        image.color_space = image_color_space;
        if (config_.material.load_textures) {
          TextureImage loaded;
          if (LoadTexture(resolved, &loaded)) {
            if (loaded.name.empty()) loaded.name = image.name;
            if (loaded.resolved_path.empty()) loaded.resolved_path = resolved;
            if (!config_.material.custom_texture_loader ||
                loaded.color_space == ColorSpace::Unknown) {
              loaded.color_space = image.color_space;
            }
            image = std::move(loaded);
          } else if (!config_.material.allow_missing_textures) {
            warnings_.push_back("Failed to load texture: " + tex_data.file);
            return false;
          }
        }
        image_id = static_cast<int32_t>(scene->images.size());
        // Key on the RESOLVED path/colorspace we looked up with -- a loaded
        // image may carry different values, and the next lookup uses these.
        scene->images.push_back(std::move(image));
        RememberImageId(scene, resolved, image_color_space, image_id);
      }

      RenderTexture texture;
      texture.name = texture_prim.IsValid() ? texture_prim.GetName() : param_name;
      texture.prim_path = texture_prim_path;
      texture.asset_path = tex_data.file;
      texture.ktx2_hint = tex_data.ktx2_hint;
      texture.wrap_s = ParseWrapMode(tex_data.wrap_s);
      texture.wrap_t = ParseWrapMode(tex_data.wrap_t);
      texture.scale_value = Float4(tex_data.scale[0], tex_data.scale[1],
                                   tex_data.scale[2], tex_data.scale[3]);
      texture.bias = Float4(tex_data.bias[0], tex_data.bias[1],
                            tex_data.bias[2], tex_data.bias[3]);
      texture.image_id = image_id;
      texture.source_color_space = tex_data.source_color_space;
      texture.target_color_space = scene->working_color_space;
      {
        std::string source_space = tex_data.source_color_space;
        if (source_space.empty() || source_space == "auto") {
          source_space = "srgb_rec709_scene";
        }
        ::tinyusdz::color::ColorTransform source_to_display;
        if (::tinyusdz::next::color_management::BuildColorTransform(
                texture_prim, source_space, "lin_rec709_scene",
                &source_to_display)) {
          texture.color_transform_valid = true;
          texture.color_transform_bypass = source_to_display.bypass;
          texture.source_color_is_data =
              source_to_display.source.kind ==
              ::tinyusdz::color::ColorSpaceKind::Data;
          texture.source_gamma = source_to_display.source.gamma;
          texture.source_linear_bias = source_to_display.source.linear_bias;
          std::copy(source_to_display.matrix, source_to_display.matrix + 9,
                    texture.source_to_display_linear);
        }
      }
      texture.output_channel =
          ChannelFromConnection(connection_path, texture_prim);
      // UsdTransform2d on the st chain (rotation is authored in degrees;
      // RenderTexture stores radians).
      texture.offset = Float2(tex_data.uv_translation[0],
                              tex_data.uv_translation[1]);
      texture.scale = Float2(tex_data.uv_scale[0], tex_data.uv_scale[1]);
      texture.rotation = tex_data.uv_rotation * 3.14159265358979323846f / 180.0f;
      texture.uv_primvar = tex_data.uv_primvar;

      out->texture_id = static_cast<int32_t>(scene->textures.size());
      scene->textures.push_back(std::move(texture));
      return true;
    }

    Value connected_value;
    if (ResolveConnectedValue(stage, connection_path, config_.time_code,
                              &connected_value) &&
        ValueToShaderParam(connected_value, out)) {
      UsdPrim color_prim = shader_prim;
      std::string color_property = attr_name;
      (void)ResolveConnectedColorSource(stage, connection_path,
                                        config_.time_code, &color_prim,
                                        &color_property);
      ConvertShaderColorToWorking(color_prim, color_property, param_name,
                                  scene, out);
      return true;
    }
  }

  ::tinyusdz::next::EvalOptions direct_opts = eval.GetOptions();
  direct_opts.follow_connections = false;
  ::tinyusdz::next::EvalResult direct =
      eval.EvalWith(shader_prim, attr_name, direct_opts);
  if (direct.success && ValueToShaderParam(direct.value, out)) {
    ConvertShaderColorToWorking(shader_prim, attr_name, param_name, scene,
                                out);
    return true;
  }

  ::tinyusdz::next::EvalResult followed = eval.Eval(shader_prim, attr_name);
  if (followed.success && ValueToShaderParam(followed.value, out)) {
    ConvertShaderColorToWorking(shader_prim, attr_name, param_name, scene,
                                out);
    return true;
  }

  return false;
}

//
// Light conversion
//

bool RenderSceneConverter::ConvertLight(const UsdPrim& prim, RenderLight* out) {
  if (!out || !IsLight(prim)) {
    last_error_ = "Invalid light prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Determine light type
  LightKind kind = GetLightKind(prim);
  switch (kind) {
    case LightKind::DistantLight: out->type = LightType::Directional; break;
    case LightKind::DomeLight: out->type = LightType::Dome; break;
    case LightKind::RectLight: out->type = LightType::Rect; break;
    case LightKind::DiskLight: out->type = LightType::Disk; break;
    case LightKind::SphereLight: out->type = LightType::Sphere; break;
    case LightKind::CylinderLight: out->type = LightType::Cylinder; break;
    case LightKind::GeometryLight: out->type = LightType::Geometry; break;
    case LightKind::PortalLight: out->type = LightType::Rect; break;
    case LightKind::PluginLight:
      out->type = LightType::Point;
      warnings_.push_back("PluginLight '" + prim.GetPath().str() +
                          "': shader registry evaluation is unsupported; "
                          "using point light fallback");
      break;
    case LightKind::LightFilter:
    case LightKind::PluginLightFilter:
      out->type = LightType::Point;
      warnings_.push_back("Light filter '" + prim.GetPath().str() +
                          "': filter evaluation is unsupported; "
                          "using inert point light fallback");
      out->intensity = 0.0f;
      break;
    default: out->type = LightType::Point; break;
  }

  // Common properties
  GetFloat3(prim, "inputs:color", &out->color.x, &out->color.y, &out->color.z);
  GetFloat(prim, "inputs:intensity", &out->intensity);
  GetFloat(prim, "inputs:exposure", &out->exposure);
  GetBool(prim, "inputs:normalize", &out->normalize);
  GetBool(prim, "inputs:enableColorTemperature",
          &out->enable_color_temperature);
  GetFloat(prim, "inputs:colorTemperature", &out->color_temperature);
  GetFloat(prim, "inputs:diffuse", &out->diffuse);
  GetFloat(prim, "inputs:specular", &out->specular);
  GetFloat(prim, "inputs:shaping:cone:angle", &out->shaping_cone_angle);
  GetFloat(prim, "inputs:shaping:focus", &out->shaping_focus);
  GetFloat3(prim, "inputs:shaping:focusTint", &out->shaping_focus_tint.x,
            &out->shaping_focus_tint.y, &out->shaping_focus_tint.z);
  GetFloat(prim, "inputs:shaping:cone:softness",
           &out->shaping_cone_softness);
  ReadStringLikeProperty(prim, "inputs:shaping:ies:file",
                         &out->shaping_ies_file);
  GetFloat(prim, "inputs:shaping:ies:angleScale",
           &out->shaping_ies_angle_scale);
  GetBool(prim, "inputs:shaping:ies:normalize", &out->shaping_ies_normalize);
  out->light_link_targets = ReadRelationshipTargets(prim, "light:link");
  if (out->light_link_targets.empty()) {
    out->light_link_targets = ReadRelationshipTargets(prim, "collection:lightLink:includes");
  }
  out->shadow_link_targets = ReadRelationshipTargets(prim, "shadow:link");
  if (out->shadow_link_targets.empty()) {
    out->shadow_link_targets =
        ReadRelationshipTargets(prim, "collection:shadowLink:includes");
  }
  out->filter_targets = ReadRelationshipTargets(prim, "filters");
  if (out->filter_targets.empty()) {
    out->filter_targets = ReadRelationshipTargets(prim, "light:filters");
  }

  // Type-specific properties
  switch (out->type) {
    case LightType::Sphere: {
      out->params.sphere.radius = 0.5f;
      GetFloat(prim, "inputs:radius", &out->params.sphere.radius);
      // Cone shaping on a sphere light makes it a spot light.
      float cone_angle = 0.0f;
      if (GetFloat(prim, "inputs:shaping:cone:angle", &cone_angle)) {
        out->type = LightType::Spot;
        out->params.spot.angle = cone_angle * 3.14159265358979323846f / 180.0f;
      }
      break;
    }
    case LightType::Rect:
      out->params.rect.width = 1.0f;
      out->params.rect.height = 1.0f;
      GetFloat(prim, "inputs:width", &out->params.rect.width);
      GetFloat(prim, "inputs:height", &out->params.rect.height);
      break;
    case LightType::Disk:
      out->params.disk.radius = 0.5f;
      GetFloat(prim, "inputs:radius", &out->params.disk.radius);
      break;
    case LightType::Cylinder:
      out->params.cylinder.radius = 0.5f;
      out->params.cylinder.length = 1.0f;
      GetFloat(prim, "inputs:radius", &out->params.cylinder.radius);
      GetFloat(prim, "inputs:length", &out->params.cylinder.length);
      break;
    case LightType::Directional:
      out->params.distant.angle = 0.53f;
      GetFloat(prim, "inputs:angle", &out->params.distant.angle);
      break;
    case LightType::Dome: {
      std::string format;
      if (GetToken(prim, "inputs:texture:format", &format)) {
        if (format == "latlong") {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::Latlong;
        } else if (format == "mirroredBall") {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::MirroredBall;
        } else if (format == "angular") {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::Angular;
        } else {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::Automatic;
        }
      }
      break;
    }
    default:
      break;
  }

  // Shadow settings (UsdLux authors `inputs:shadow:enable`; accept the
  // legacy `inputs:enableShadows` spelling too).
  if (!GetBool(prim, "inputs:shadow:enable", &out->enable_shadow)) {
    GetBool(prim, "inputs:enableShadows", &out->enable_shadow);
  }
  GetFloat3(prim, "inputs:shadow:color", &out->shadow_color.x,
            &out->shadow_color.y, &out->shadow_color.z);
  GetFloat(prim, "inputs:shadow:distance", &out->shadow_distance);
  GetFloat(prim, "inputs:shadow:falloff", &out->shadow_falloff);
  GetFloat(prim, "inputs:shadow:falloffGamma", &out->shadow_falloff_gamma);

  return true;
}

//
// Camera conversion
//

bool RenderSceneConverter::ConvertCamera(const UsdPrim& prim, RenderCamera* out) {
  if (!out || !IsCamera(prim)) {
    last_error_ = "Invalid camera prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Projection type
  std::string projection;
  GetToken(prim, "projection", &projection);
  out->type = (projection == "orthographic") ?
              CameraType::Orthographic : CameraType::Perspective;

  // Lens parameters
  GetFloat(prim, "focalLength", &out->focal_length);
  GetFloat(prim, "horizontalAperture", &out->horizontal_aperture);
  GetFloat(prim, "verticalAperture", &out->vertical_aperture);
  GetFloat(prim, "horizontalApertureOffset",
           &out->horizontal_aperture_offset);
  GetFloat(prim, "verticalApertureOffset", &out->vertical_aperture_offset);

  // Clipping
  float clip_range[2] = {0.1f, 10000.0f};
  const Value* clip_val = GetAttribute(prim, "clippingRange");
  if (clip_val) {
    const float* cr = clip_val->as_float2();
    if (cr) {
      clip_range[0] = cr[0];
      clip_range[1] = cr[1];
    }
  }
  out->near_clip = clip_range[0];
  out->far_clip = clip_range[1];

  // Depth of field / exposure
  GetFloat(prim, "focusDistance", &out->focus_distance);
  GetFloat(prim, "fStop", &out->fstop);
  GetFloat(prim, "exposure", &out->exposure);

  std::string stereo_role;
  if (GetToken(prim, "stereoRole", &stereo_role)) {
    if (stereo_role == "left") {
      out->stereo_role = RenderCamera::StereoRole::Left;
    } else if (stereo_role == "right") {
      out->stereo_role = RenderCamera::StereoRole::Right;
    }
  }
  if (const Value* planes = GetAttribute(prim, "clippingPlanes")) {
    if (const std::vector<float>* values = planes->as_float_array()) {
      out->clipping_planes.reserve(values->size() / 4);
      for (size_t i = 0; i + 3 < values->size(); i += 4) {
        out->clipping_planes.emplace_back(
            (*values)[i], (*values)[i + 1], (*values)[i + 2],
            (*values)[i + 3]);
      }
    }
  }

  // Motion-blur shutter interval
  GetDouble(prim, "shutter:open", &out->shutter_open);
  GetDouble(prim, "shutter:close", &out->shutter_close);

  return true;
}

//
// Skeleton conversion
//

bool RenderSceneConverter::ConvertSkeleton(const UsdPrim& prim, Skeleton* out) {
  if (!out || !::tinyusdz::next::IsSkeleton(prim)) {
    last_error_ = "Invalid skeleton prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->root_joint = -1;

  Stage stage;
  (void)stage;

  const Stage* stage_ptr = nullptr;
  // GetSkeletonData currently only needs the stage for API symmetry. Keep a
  // local empty Stage out of the hot path and read directly from the prim.
  (void)stage_ptr;

  ::tinyusdz::next::SkeletonData skel;
  // The schema accessor does not dereference Stage for Skeleton fields.
  if (!::tinyusdz::next::GetSkeletonData(stage, prim, &skel) ||
      skel.joints.empty()) {
    return true;
  }
  out->animation_source_path = skel.animationSource;

  // Authored-count validation: a short bindTransforms/restTransforms array
  // silently identity-fills the tail joints (visually collapsed limbs with
  // no hint why). Unauthored (empty) is fine — rest derives from bind below.
  if (!skel.bindTransforms.empty() &&
      skel.bindTransforms.size() != skel.joints.size() * 16) {
    warnings_.push_back(
        "Skeleton " + prim.GetPath().str() + " authors " +
        std::to_string(skel.bindTransforms.size() / 16) +
        " bindTransforms for " + std::to_string(skel.joints.size()) +
        " joints; missing entries use identity");
  }
  if (!skel.restTransforms.empty() &&
      skel.restTransforms.size() != skel.joints.size() * 16) {
    warnings_.push_back(
        "Skeleton " + prim.GetPath().str() + " authors " +
        std::to_string(skel.restTransforms.size() / 16) +
        " restTransforms for " + std::to_string(skel.joints.size()) +
        " joints; missing entries derive from bindTransforms");
  }

  std::vector<int> topology;
  std::string err;
  if (!::tinyusdz::next::BuildSkelTopology(skel.joints, topology, &err)) {
    warnings_.push_back("Invalid skeleton topology for " + prim.GetPath().str() +
                        ": " + err);
    topology.assign(skel.joints.size(), -1);
  }

  out->joints.resize(skel.joints.size());
  for (size_t i = 0; i < skel.joints.size(); ++i) {
    SkeletonJoint& joint = out->joints[i];
    joint.path = skel.joints[i];
    if (i < skel.jointNames.size() && !skel.jointNames[i].empty()) {
      joint.name = skel.jointNames[i];
    } else {
      joint.name = LeafNameFromJointPath(skel.joints[i]);
    }
    joint.parent_id = (i < topology.size()) ? topology[i] : -1;
    CopyMatrixFromDoubles(skel.bindTransforms, i, &joint.bind_transform);
    CopyMatrixFromDoubles(skel.restTransforms, i, &joint.rest_transform);

    if (joint.parent_id < 0 && out->root_joint < 0) {
      out->root_joint = static_cast<int32_t>(i);
    }
  }

  // restTransforms are optional in UsdSkel: when unauthored (or too short),
  // derive the parent-local rest pose from the world-space bindTransforms —
  // rest[i] = bind[i] * inverse(bind[parent]) (row-vector convention).
  // Leaving identity here collapses every joint onto its parent.
  if (skel.restTransforms.size() < skel.joints.size() * 16 &&
      skel.bindTransforms.size() >= skel.joints.size() * 16) {
    const size_t authored_rest = skel.restTransforms.size() / 16;
    for (size_t i = authored_rest; i < out->joints.size(); ++i) {
      SkeletonJoint& joint = out->joints[i];
      const int32_t parent = joint.parent_id;
      if (parent < 0) {
        joint.rest_transform = joint.bind_transform;
        continue;
      }
      double parent_bind[16];
      double parent_inv[16];
      for (int e = 0; e < 16; ++e) {
        parent_bind[e] =
            double(out->joints[static_cast<size_t>(parent)].bind_transform.m[e]);
      }
      if (!InvertMatrix4x4D(parent_bind, parent_inv)) {
        joint.rest_transform = joint.bind_transform;
        continue;
      }
      // rest = bind * parent_inv (row-vector: local * parent = world)
      double bind[16];
      for (int e = 0; e < 16; ++e) bind[e] = double(joint.bind_transform.m[e]);
      double rest[16];
      for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
          double sum = 0.0;
          for (int k = 0; k < 4; ++k) {
            sum += bind[r * 4 + k] * parent_inv[k * 4 + c];
          }
          rest[r * 4 + c] = sum;
        }
      }
      for (int e = 0; e < 16; ++e) {
        joint.rest_transform.m[e] = static_cast<float>(rest[e]);
      }
    }
  }

  for (size_t i = 0; i < out->joints.size(); ++i) {
    const int32_t parent = out->joints[i].parent_id;
    if (parent >= 0 && static_cast<size_t>(parent) < out->joints.size()) {
      out->joints[parent].children.push_back(static_cast<int32_t>(i));
    }
  }

  if (out->root_joint < 0 && !out->joints.empty()) {
    out->root_joint = 0;
  }

  return true;
}

//
// Animation conversion
//

bool RenderSceneConverter::ConvertAnimation(const Stage& stage,
                                            const UsdPrim& prim,
                                            AnimationClip* out) {
  if (!out || !prim.IsValid()) return false;

  out->name = prim.GetName() + "_Anim";
  out->prim_path = prim.GetPath().str();
  out->start_time = std::numeric_limits<double>::max();
  out->end_time = -std::numeric_limits<double>::max();

  if (config_.animation.bake_value_clips) {
    // Clip METADATA semantics come from the core resolver: parse the sets
    // once, then resolve every (property, time) sample through
    // ResolveValueClipFromSets (set-name strength order, times jump
    // discontinuities, out-of-range mapping, manifest gating, nested clips).
    std::vector<::tinyusdz::next::ValueClipSet> clip_sets;
    std::string clip_error;
    if (::tinyusdz::next::ParseValueClipSets(prim, &clip_sets, &clip_error)) {
      // Pre-load every clip asset once: records clip_asset_paths for
      // diagnostics, drives property ENUMERATION (the bake needs the property
      // names, which the resolver does not report), and seeds the shared
      // stage cache the core resolver consumes (no double loading).
      ::tinyusdz::next::ValueClipStageCache clip_cache;
      for (const ::tinyusdz::next::ValueClipSet& clip_set : clip_sets) {
        for (const std::string& asset_path : clip_set.asset_paths) {
          if (std::find(out->clip_asset_paths.begin(),
                        out->clip_asset_paths.end(), asset_path) ==
              out->clip_asset_paths.end()) {
            out->clip_asset_paths.push_back(asset_path);
          }
          if (clip_cache.entries.count(asset_path)) continue;
          if (!config_.animation.clip_stage_loader) continue;
          auto clip_stage = std::make_shared<Stage>();
          std::string warn;
          std::string err;
          ::tinyusdz::next::ValueClipStageCache::Entry entry;
          if (config_.animation.clip_stage_loader(asset_path,
                                                  clip_stage.get(), &warn,
                                                  &err)) {
            entry.stage = std::move(clip_stage);
          } else {
            entry.error = "Unable to load value clip '" + asset_path +
                          "' for " + prim.GetPath().str() +
                          (err.empty() ? std::string() : ": " + err);
            warnings_.push_back(entry.error);
          }
          clip_cache.entries.emplace(asset_path, std::move(entry));
          if (!warn.empty()) warnings_.push_back(std::move(warn));
        }
      }

      // A property provided by more than one set bakes ONCE: the core
      // resolver already applies the set strength order per query.
      std::set<std::string> baked_properties;
      for (const ::tinyusdz::next::ValueClipSet& clip_set : clip_sets) {
        std::set<std::string> properties;
        const std::string clip_prim_path =
            clip_set.prim_path.empty() ? prim.GetPath().str()
                                       : clip_set.prim_path;
        for (const std::string& asset_path : clip_set.asset_paths) {
          const auto stage_it = clip_cache.entries.find(asset_path);
          if (stage_it == clip_cache.entries.end() ||
              !stage_it->second.stage) {
            continue;
          }
          const UsdPrim clip_prim =
              stage_it->second.stage->GetPrimAtPath(clip_prim_path);
          if (!clip_prim.IsValid()) continue;
          for (const std::string& property : clip_prim.GetPropertyNames()) {
            properties.insert(property);
          }
        }

        const std::vector<double> sample_times = ValueClipSampleTimes(
            stage, clip_set, config_.animation.max_value_clip_samples);
        for (const std::string& property : properties) {
          if (!baked_properties.insert(property).second) continue;
          AnimationChannel channel;
          channel.target_path =
              IsXformAnimationProperty(property)
                  ? TargetPathForXformOp(property)
                  : AnimationChannel::TargetPath::CustomProperty;
          channel.target_prim_path = prim.GetPath().str();
          channel.property_name = property;
          channel.keyframes.reserve(sample_times.size());

          for (double stage_time : sample_times) {
            Value value;
            bool have_value = ::tinyusdz::next::ResolveValueClipFromSets(
                clip_sets, prim, property, stage_time,
                config_.animation.clip_stage_loader, &value, nullptr, nullptr,
                nullptr, &clip_cache);
            if (!have_value && prim.HasProperty(property)) {
              value = prim.GetInterpolatedValue(property, stage_time);
              have_value = !value.is_empty();
            }
            if (!have_value) continue;
            Float4 converted;
            if (!ValueToAnimationFloat4(property, value, &converted)) {
              continue;
            }
            channel.keyframes.push_back(Keyframe{stage_time, converted});
            out->start_time = std::min(out->start_time, stage_time);
            out->end_time = std::max(out->end_time, stage_time);
          }

          if (!channel.keyframes.empty()) {
            out->channels.push_back(std::move(channel));
            out->value_clip_baked = true;
          }
        }
      }
    } else if (!clip_error.empty()) {
      warnings_.push_back("Invalid value clips on " + prim.GetPath().str() +
                          ": " + clip_error);
    }
  }

  if (::tinyusdz::next::IsSkelAnimation(prim)) {
    const std::vector<std::string> joint_order =
        ReadTokenArrayProperty(prim, "joints");
    const std::vector<std::string> blend_shape_order =
        ReadTokenArrayProperty(prim, "blendShapes");

    auto append_skel_channel = [&](const char* prop_name,
                                   AnimationChannel::TargetPath target_path,
                                   uint32_t stride) {
      std::vector<double> times = prim.GetTimeSampleTimes(prop_name);
      if (times.empty()) return;
      std::sort(times.begin(), times.end());
      times.erase(std::unique(times.begin(), times.end()), times.end());

      AnimationChannel channel;
      channel.target_path = target_path;
      channel.target_prim_path = prim.GetPath().str();
      channel.property_name = prop_name;
      channel.joint_order = joint_order;
      channel.blend_shape_order = blend_shape_order;
      channel.value_stride = stride;
      channel.is_skeletal = true;

      uint32_t expected_elements = 0;
      for (double t : times) {
        // Read only this channel. GetSkelAnimationDataAtTime evaluates and
        // copies translations, rotations, scales and blend-shape weights on
        // every call; invoking it once per property made long clips perform
        // the complete animation decode up to four times.
        // `times` contains exact authored sample times, so borrow the stored
        // Value directly. AttributeEval/GetInterpolatedValue returns a Value
        // by copy; for a 3000-joint rig that copied tens of thousands of
        // floats once per frame and then copied them again into the channel.
        const Value* sampled = prim.GetValueAtTime(prop_name, t);
        const std::vector<float>* values =
            sampled ? sampled->as_float_array() : nullptr;
        if (!values || values->empty() || ((*values).size() % stride) != 0) {
          continue;
        }

        const uint32_t element_count =
            static_cast<uint32_t>((*values).size() / stride);
        if (expected_elements == 0) {
          expected_elements = element_count;
          channel.element_count = element_count;
          channel.array_values.reserve(times.size() * values->size());
          // Width validation: blendShapeWeights samples must be as wide as
          // the declared blendShapes list, or weights drive the wrong shapes.
          if (target_path == AnimationChannel::TargetPath::Weights &&
              !blend_shape_order.empty() &&
              element_count != blend_shape_order.size()) {
            warnings_.push_back(
                "SkelAnimation " + prim.GetPath().str() + " has " +
                std::to_string(element_count) +
                " blendShapeWeights per sample for " +
                std::to_string(blend_shape_order.size()) +
                " declared blendShapes");
          }
        } else if (element_count != expected_elements) {
          warnings_.push_back("Skipping inconsistent SkelAnimation sample for " +
                              prim.GetPath().str() + "." + prop_name);
          continue;
        }

        // next-core keeps quats REAL-FIRST (w, x, y, z); an AnimationChannel is
        // the GPU-facing form and its rotation values are xyzw (what a
        // three.js QuaternionKeyframeTrack expects). Swizzle here, at the
        // boundary -- SkelAnimationData itself stays real-first.
        std::vector<float> swizzled;
        if (target_path == AnimationChannel::TargetPath::Rotation) {
          swizzled.resize(values->size());
          for (size_t i = 0; i + 3 < values->size(); i += 4) {
            swizzled[i + 0] = (*values)[i + 1];  // x
            swizzled[i + 1] = (*values)[i + 2];  // y
            swizzled[i + 2] = (*values)[i + 3];  // z
            swizzled[i + 3] = (*values)[i + 0];  // w (real)
          }
          values = &swizzled;
        }

        Float4 preview;
        if (!FirstArrayElementToFloat4(*values, stride, &preview)) continue;
        channel.keyframes.push_back(Keyframe{t, preview});
        channel.array_values.insert(channel.array_values.end(),
                                    values->begin(), values->end());
        out->start_time = std::min(out->start_time, t);
        out->end_time = std::max(out->end_time, t);
      }

      if (!channel.keyframes.empty()) {
        out->channels.push_back(std::move(channel));
      }
    };

    append_skel_channel("translations",
                        AnimationChannel::TargetPath::Translation, 3);
    append_skel_channel("rotations",
                        AnimationChannel::TargetPath::Rotation, 4);
    append_skel_channel("scales",
                        AnimationChannel::TargetPath::Scale, 3);
    append_skel_channel("blendShapeWeights",
                        AnimationChannel::TargetPath::Weights, 1);

    if (!out->channels.empty()) {
      return true;
    }
  }

  for (const std::string& prop_name : prim.GetPropertyNames()) {
    const std::vector<double> times = prim.GetTimeSampleTimes(prop_name);
    if (times.empty()) continue;

    const bool is_xform = IsXformAnimationProperty(prop_name);
    AnimationChannel channel;
    channel.target_path = is_xform ? TargetPathForXformOp(prop_name)
                                   : AnimationChannel::TargetPath::CustomProperty;
    channel.target_prim_path = prim.GetPath().str();
    channel.property_name = prop_name;
    channel.keyframes.reserve(times.size());

    for (double t : times) {
      Value value = prim.GetInterpolatedValue(prop_name, t);
      Float4 v;
      if (!ValueToAnimationFloat4(prop_name, value, &v)) continue;
      channel.keyframes.push_back(Keyframe{t, v});
      out->start_time = std::min(out->start_time, t);
      out->end_time = std::max(out->end_time, t);
    }

    if (!channel.keyframes.empty()) {
      out->channels.push_back(std::move(channel));
    }
  }

  if (out->channels.empty()) {
    out->start_time = 0.0;
    out->end_time = 0.0;
    return false;
  }

  return true;
}

//
// Texture loading
//

bool RenderSceneConverter::LoadTexture(const std::string& asset_path, TextureImage* out) {
  if (!out) return false;

  // Use custom loader if provided
  if (config_.material.custom_texture_loader) {
    return config_.material.custom_texture_loader(asset_path, out);
  }

  // Built-in loader is metadata-only by design. Applications that need decoded
  // pixels should provide `MaterialConfig::custom_texture_loader`.
  out->resolved_path = asset_path;

  return true;
}

//
// Utility functions
//

void ComputeTriangleNormal(const float* p0, const float* p1, const float* p2, float* normal) {
  float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
  float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

  normal[0] = e1[1]*e2[2] - e1[2]*e2[1];
  normal[1] = e1[2]*e2[0] - e1[0]*e2[2];
  normal[2] = e1[0]*e2[1] - e1[1]*e2[0];

  float len = std::sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
  if (len > 1e-8f) {
    normal[0] /= len;
    normal[1] /= len;
    normal[2] /= len;
  }
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
