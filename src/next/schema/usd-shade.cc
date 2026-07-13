// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdShade Schema Implementation

#include "usd-shade.hh"

namespace tinyusdz {
namespace next {

bool IsMaterial(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Material";
}

bool IsShader(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Shader";
}

bool IsNodeGraph(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "NodeGraph";
}

// ============================================================
// Material API Implementation
// ============================================================

// A material terminal ("outputs:surface" etc.) is an ATTRIBUTE CONNECTION in
// USD (`token outputs:surface.connect = </Mat/Shader.outputs:surface>`), not a
// relationship. Accept both (some hand-authored files use a rel). The returned
// path is the connected PRIM (property suffix stripped).
static std::string GetTerminalShaderPath(const UsdPrim& material,
                                         const char* output_name) {
  auto strip_prop = [](const std::string& p) {
    size_t dot = p.rfind('.');
    // Property separator only when the dot comes after the last '/'.
    size_t slash = p.rfind('/');
    if (dot != std::string::npos &&
        (slash == std::string::npos || dot > slash)) {
      return p.substr(0, dot);
    }
    return p;
  };
  if (const PrimSpec* spec = material.GetPrimSpec()) {
    if (const std::vector<Path>* conns = spec->connection(output_name)) {
      if (!conns->empty()) return strip_prop((*conns)[0].str());
    }
  }
  const std::vector<Path>* targets = material.GetRelationship(output_name);
  if (targets && !targets->empty()) {
    return strip_prop((*targets)[0].str());
  }
  return "";
}

std::string GetSurfaceShader(const Stage& /* stage */, const UsdPrim& material) {
  if (!IsMaterial(material)) return "";
  return GetTerminalShaderPath(material, "outputs:surface");
}

std::string GetDisplacementShader(const Stage& /* stage */, const UsdPrim& material) {
  if (!IsMaterial(material)) return "";
  return GetTerminalShaderPath(material, "outputs:displacement");
}

std::string GetVolumeShader(const Stage& /* stage */, const UsdPrim& material) {
  if (!IsMaterial(material)) return "";
  return GetTerminalShaderPath(material, "outputs:volume");
}

bool GetMaterialBinding(const Stage& stage, const UsdPrim& material,
                        MaterialBinding* out) {
  if (!IsMaterial(material) || !out) return false;

  out->surface_shader_path = GetSurfaceShader(stage, material);
  out->displacement_shader_path = GetDisplacementShader(stage, material);
  out->volume_shader_path = GetVolumeShader(stage, material);

  return !out->surface_shader_path.empty() ||
         !out->displacement_shader_path.empty() ||
         !out->volume_shader_path.empty();
}

UsdPrim GetBoundMaterial(const Stage& stage, const UsdPrim& prim) {
  std::string path = GetBoundMaterialPath(prim);
  if (path.empty()) return UsdPrim();

  return stage.GetPrimAtPath(path);
}

std::string GetBoundMaterialPath(const UsdPrim& prim) {
  if (!prim.IsValid()) return "";

  // Purpose-specific bindings take precedence for a preview extractor:
  // material:binding:preview, then the all-purpose material:binding, then
  // material:binding:full (UsdShade ComputeBoundMaterial semantics for the
  // "preview" purpose).
  static const char* kBindingOrder[] = {"material:binding:preview",
                                        "material:binding",
                                        "material:binding:full"};
  for (const char* rel : kBindingOrder) {
    const std::vector<Path>* targets = prim.GetRelationship(rel);
    if (targets && !targets->empty()) {
      return (*targets)[0].str();
    }
  }
  return "";
}

// ============================================================
// Shader API Implementation
// ============================================================

std::string GetShaderId(const UsdPrim& shader) {
  if (!IsShader(shader)) return "";

  const Value* result = shader.GetPropertyValue("info:id");
  if (result && result->type_id() == TypeId::Token) {
    if (const std::string* str = result->as_token()) {
      return *str;
    }
  }
  return "";
}

std::string GetShaderImplementationSource(const UsdPrim& shader) {
  if (!IsShader(shader)) return "";

  const Value* result = shader.GetPropertyValue("info:implementationSource");
  if (result && result->type_id() == TypeId::Token) {
    if (const std::string* str = result->as_token()) {
      return *str;
    }
  }
  return "id";  // Default
}

std::vector<ShaderPort> GetShaderInputs(const Stage& stage, const UsdPrim& shader,
                                         double time) {
  std::vector<ShaderPort> inputs;
  if (!IsShader(shader)) return inputs;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  // Get all property names starting with "inputs:"
  std::vector<std::string> props = shader.GetPropertyNames();
  for (const std::string& prop_name : props) {
    if (prop_name.size() <= 7 || prop_name.substr(0, 7) != "inputs:") continue;

    ShaderPort port;
    port.name = prop_name.substr(7);  // Remove "inputs:" prefix

    EvalResult result = eval.Eval(shader, prop_name);
    if (result.success) {
      port.value = result.value;
      port.type_id = result.value.type_id();
    }

    // Check for connection
    if (eval.HasConnection(shader, prop_name)) {
      port.connection_path = eval.GetConnectionPath(shader, prop_name);
      port.is_connected = true;
    }

    inputs.push_back(std::move(port));
  }

  return inputs;
}

std::vector<ShaderPort> GetShaderOutputs(const UsdPrim& shader) {
  std::vector<ShaderPort> outputs;
  if (!IsShader(shader)) return outputs;

  // Get all properties starting with "outputs:"
  std::vector<std::string> props = shader.GetPropertyNames();
  for (const std::string& prop_name : props) {
    if (prop_name.size() <= 8 || prop_name.substr(0, 8) != "outputs:") continue;

    ShaderPort port;
    port.name = prop_name.substr(8);  // Remove "outputs:" prefix

    const Value* result = shader.GetPropertyValue(prop_name);
    if (result) {
      port.value = *result;
      port.type_id = result->type_id();
    }

    outputs.push_back(std::move(port));
  }

  return outputs;
}

bool GetShaderInput(const Stage& stage, const UsdPrim& shader,
                    const std::string& input_name, ShaderPort* out,
                    double time) {
  if (!IsShader(shader) || !out) return false;

  std::string attr_name = "inputs:" + input_name;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  EvalResult result = eval.Eval(shader, attr_name);
  if (!result.success) return false;

  out->name = input_name;
  out->value = result.value;
  out->type_id = result.value.type_id();

  // Check for connection
  if (eval.HasConnection(shader, attr_name)) {
    out->connection_path = eval.GetConnectionPath(shader, attr_name);
    out->is_connected = true;
  }

  return true;
}

EvalResult EvalShaderInput(const Stage& stage, const UsdPrim& shader,
                           const std::string& input_name, double time) {
  if (!IsShader(shader)) {
    EvalResult err;
    err.success = false;
    return err;
  }

  std::string attr_name = "inputs:" + input_name;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  // This will follow connections automatically
  return eval.Eval(shader, attr_name);
}

// ============================================================
// UsdPreviewSurface Implementation
// ============================================================

bool IsPreviewSurface(const UsdPrim& shader) {
  if (!IsShader(shader)) return false;
  std::string id = GetShaderId(shader);
  return id == "UsdPreviewSurface";
}

bool GetPreviewSurfaceData(const Stage& stage, const UsdPrim& shader,
                           PreviewSurfaceData* out, double time) {
  if (!IsPreviewSurface(shader) || !out) return false;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  // Diffuse color
  float diffuse[3];
  if (eval.EvalFloat3(shader, "inputs:diffuseColor", diffuse)) {
    out->diffuse_color[0] = diffuse[0];
    out->diffuse_color[1] = diffuse[1];
    out->diffuse_color[2] = diffuse[2];
  }

  // Emissive color
  float emissive[3];
  if (eval.EvalFloat3(shader, "inputs:emissiveColor", emissive)) {
    out->emissive_color[0] = emissive[0];
    out->emissive_color[1] = emissive[1];
    out->emissive_color[2] = emissive[2];
  }

  // Specular color
  float specular[3];
  if (eval.EvalFloat3(shader, "inputs:specularColor", specular)) {
    out->specular_color[0] = specular[0];
    out->specular_color[1] = specular[1];
    out->specular_color[2] = specular[2];
  }

  // Scalar values
  out->metallic = eval.EvalOr(shader, "inputs:metallic", 0.0f);
  out->roughness = eval.EvalOr(shader, "inputs:roughness", 0.5f);
  out->clearcoat = eval.EvalOr(shader, "inputs:clearcoat", 0.0f);
  out->clearcoat_roughness = eval.EvalOr(shader, "inputs:clearcoatRoughness", 0.01f);
  out->opacity = eval.EvalOr(shader, "inputs:opacity", 1.0f);
  out->opacity_threshold = eval.EvalOr(shader, "inputs:opacityThreshold", 0.0f);
  out->ior = eval.EvalOr(shader, "inputs:ior", 1.5f);
  out->displacement = eval.EvalOr(shader, "inputs:displacement", 0.0f);
  out->occlusion = eval.EvalOr(shader, "inputs:occlusion", 1.0f);

  // Normal
  float normal[3];
  if (eval.EvalFloat3(shader, "inputs:normal", normal)) {
    out->normal[0] = normal[0];
    out->normal[1] = normal[1];
    out->normal[2] = normal[2];
  }

  // Specular workflow
  int use_specular = eval.EvalOr(shader, "inputs:useSpecularWorkflow", 0);
  out->use_specular_workflow = (use_specular != 0);

  // Check for texture connections
  auto check_texture = [&eval, &shader](const std::string& input) -> std::string {
    std::string attr_name = "inputs:" + input;
    if (eval.HasConnection(shader, attr_name)) {
      return eval.GetConnectionPath(shader, attr_name);
    }
    return "";
  };

  out->diffuse_texture = check_texture("diffuseColor");
  out->normal_texture = check_texture("normal");
  out->metallic_texture = check_texture("metallic");
  out->roughness_texture = check_texture("roughness");
  out->emissive_texture = check_texture("emissiveColor");
  out->occlusion_texture = check_texture("occlusion");
  out->opacity_texture = check_texture("opacity");

  return true;
}

// ============================================================
// UsdUVTexture Implementation
// ============================================================

bool IsUVTexture(const UsdPrim& shader) {
  if (!IsShader(shader)) return false;
  std::string id = GetShaderId(shader);
  return id == "UsdUVTexture";
}

bool GetUVTextureData(const Stage& stage, const UsdPrim& shader,
                      UVTextureData* out, double time) {
  if (!IsUVTexture(shader) || !out) return false;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  // File path
  std::optional<std::string> file = eval.EvalAssetPath(shader, "inputs:file");
  if (file) out->file = *file;

  // ST coordinates
  float st[2];
  if (eval.EvalFloat2(shader, "inputs:st", st)) {
    out->st[0] = st[0];
    out->st[1] = st[1];
  }

  // Wrap modes
  std::optional<std::string> wrap_s = eval.EvalToken(shader, "inputs:wrapS");
  if (wrap_s) out->wrap_s = *wrap_s;

  std::optional<std::string> wrap_t = eval.EvalToken(shader, "inputs:wrapT");
  if (wrap_t) out->wrap_t = *wrap_t;

  // Fallback value
  float fallback[4];
  if (eval.EvalFloat4(shader, "inputs:fallback", fallback)) {
    out->fallback[0] = fallback[0];
    out->fallback[1] = fallback[1];
    out->fallback[2] = fallback[2];
    out->fallback[3] = fallback[3];
  }

  // Scale
  float scale[4];
  if (eval.EvalFloat4(shader, "inputs:scale", scale)) {
    out->scale[0] = scale[0];
    out->scale[1] = scale[1];
    out->scale[2] = scale[2];
    out->scale[3] = scale[3];
  }

  // Bias
  float bias[4];
  if (eval.EvalFloat4(shader, "inputs:bias", bias)) {
    out->bias[0] = bias[0];
    out->bias[1] = bias[1];
    out->bias[2] = bias[2];
    out->bias[3] = bias[3];
  }

  // Source color space
  std::optional<std::string> color_space = eval.EvalToken(shader, "inputs:sourceColorSpace");
  if (color_space) out->source_color_space = *color_space;

  return true;
}

// ============================================================
// UsdPrimvarReader Implementation
// ============================================================

bool IsPrimvarReader(const UsdPrim& shader) {
  if (!IsShader(shader)) return false;
  std::string id = GetShaderId(shader);
  return id.size() >= 16 && id.substr(0, 16) == "UsdPrimvarReader";
}

namespace {

std::string TokenishValue(const Value* v) {
  if (!v) return "";
  if (const std::string* tok = v->as_token()) return *tok;
  if (const std::string* str = v->as_string()) return *str;
  return "";
}

}  // namespace

std::string GetPrimvarReaderVarname(const UsdPrim& shader) {
  if (!IsPrimvarReader(shader)) return "";
  return TokenishValue(shader.GetPropertyValue("inputs:varname"));
}

std::string GetPrimvarReaderVarname(const Stage& stage,
                                    const UsdPrim& shader) {
  if (!IsPrimvarReader(shader)) return "";

  // Authored value wins; otherwise follow inputs:varname connections
  // (usdMtlx/Apple flattens author e.g.
  // `token inputs:varname.connect = </Mat.inputs:frame:stPrimvarName>`).
  std::string value = TokenishValue(shader.GetPropertyValue("inputs:varname"));
  if (!value.empty()) return value;

  const ::tinyusdz::next::PrimSpec* spec = shader.GetPrimSpec();
  const std::vector<Path>* conns =
      spec ? spec->connection("inputs:varname") : nullptr;
  for (int hop = 0; conns && !conns->empty() && hop < 4; ++hop) {
    const std::string target = (*conns)[0].str();
    const size_t dot = target.rfind('.');
    if (dot == std::string::npos) break;
    const std::string prim_path = target.substr(0, dot);
    const std::string prop_name = target.substr(dot + 1);
    UsdPrim src = stage.GetPrimAtPath(prim_path);
    if (!src.IsValid()) break;
    value = TokenishValue(src.GetPropertyValue(prop_name));
    if (!value.empty()) return value;
    const ::tinyusdz::next::PrimSpec* src_spec = src.GetPrimSpec();
    conns = src_spec ? src_spec->connection(prop_name) : nullptr;
  }
  return "";
}

}  // namespace next
}  // namespace tinyusdz
