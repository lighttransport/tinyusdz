// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdShade Schema APIs
// Convenience accessors for USD materials and shaders

#pragma once

#include "../stage/stage.hh"
#include "../eval/attribute-eval.hh"
#include <map>

namespace tinyusdz {
namespace next {

/// Shader input/output info
struct ShaderPort {
  std::string name;
  TypeId type_id = TypeId::Invalid;
  Value value;
  std::string connection_path;  // If connected, path to source
  bool is_connected = false;
};

/// Material surface output binding
struct MaterialBinding {
  std::string surface_shader_path;
  std::string displacement_shader_path;
  std::string volume_shader_path;
};

/// Check if prim is a Material
bool IsMaterial(const UsdPrim& prim);

/// Check if prim is a Shader
bool IsShader(const UsdPrim& prim);

/// Check if prim is a NodeGraph
bool IsNodeGraph(const UsdPrim& prim);

// ============================================================
// Material API
// ============================================================

/// Get material surface shader path
std::string GetSurfaceShader(const Stage& stage, const UsdPrim& material);

/// Get material displacement shader path
std::string GetDisplacementShader(const Stage& stage, const UsdPrim& material);

/// Get material volume shader path
std::string GetVolumeShader(const Stage& stage, const UsdPrim& material);

/// Get all material bindings
bool GetMaterialBinding(const Stage& stage, const UsdPrim& material,
                        MaterialBinding* out);

/// Find material bound to a prim (via material:binding relationship)
UsdPrim GetBoundMaterial(const Stage& stage, const UsdPrim& prim);

/// Get bound material path
std::string GetBoundMaterialPath(const UsdPrim& prim);

// ============================================================
// Shader API
// ============================================================

/// Get shader ID (info:id attribute)
std::string GetShaderId(const UsdPrim& shader);

/// Get shader implementation source (info:implementationSource)
std::string GetShaderImplementationSource(const UsdPrim& shader);

/// Get all shader inputs
std::vector<ShaderPort> GetShaderInputs(const Stage& stage, const UsdPrim& shader,
                                         double time = 0.0);

/// Get all shader outputs
std::vector<ShaderPort> GetShaderOutputs(const UsdPrim& shader);

/// Get a specific shader input by name
bool GetShaderInput(const Stage& stage, const UsdPrim& shader,
                    const std::string& input_name, ShaderPort* out,
                    double time = 0.0);

/// Get shader input value with connection following
EvalResult EvalShaderInput(const Stage& stage, const UsdPrim& shader,
                           const std::string& input_name, double time = 0.0);

// ============================================================
// UsdPreviewSurface helpers
// ============================================================

/// UsdPreviewSurface material data
struct PreviewSurfaceData {
  float diffuse_color[3] = {0.18f, 0.18f, 0.18f};
  float emissive_color[3] = {0.0f, 0.0f, 0.0f};
  float specular_color[3] = {1.0f, 1.0f, 1.0f};

  float metallic = 0.0f;
  float roughness = 0.5f;
  float clearcoat = 0.0f;
  float clearcoat_roughness = 0.01f;
  float opacity = 1.0f;
  float opacity_threshold = 0.0f;
  float ior = 1.5f;

  float normal[3] = {0.0f, 0.0f, 1.0f};
  float displacement = 0.0f;
  float occlusion = 1.0f;

  bool use_specular_workflow = false;

  // Texture connections (empty if not connected)
  std::string diffuse_texture;
  std::string normal_texture;
  std::string metallic_texture;
  std::string roughness_texture;
  std::string emissive_texture;
  std::string occlusion_texture;
};

/// Check if shader is UsdPreviewSurface
bool IsPreviewSurface(const UsdPrim& shader);

/// Get UsdPreviewSurface data
bool GetPreviewSurfaceData(const Stage& stage, const UsdPrim& shader,
                           PreviewSurfaceData* out, double time = 0.0);

// ============================================================
// UsdUVTexture helpers
// ============================================================

/// UsdUVTexture data
struct UVTextureData {
  std::string file;
  float st[2] = {0.0f, 0.0f};
  std::string wrap_s = "useMetadata";
  std::string wrap_t = "useMetadata";
  float fallback[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float bias[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  std::string source_color_space = "auto";
};

/// Check if shader is UsdUVTexture
bool IsUVTexture(const UsdPrim& shader);

/// Get UsdUVTexture data
bool GetUVTextureData(const Stage& stage, const UsdPrim& shader,
                      UVTextureData* out, double time = 0.0);

// ============================================================
// UsdPrimvarReader helpers
// ============================================================

/// Check if shader is a UsdPrimvarReader
bool IsPrimvarReader(const UsdPrim& shader);

/// Get primvar name being read
std::string GetPrimvarReaderVarname(const UsdPrim& shader);

}  // namespace next
}  // namespace tinyusdz
