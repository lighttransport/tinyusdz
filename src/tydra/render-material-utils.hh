// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Material and texture processing utilities for render scene conversion
// Extracted from render-data.cc

#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "../value-types.hh"
#include "../path.hh"
#include "render-data.hh"

namespace tinyusdz {

// Forward declarations
class Material;
class Shader;
class UsdPreviewSurface;
class UsdUVTexture;

namespace tydra {

// Forward declarations
class RenderMaterial;
class UVTexture;
struct TextureImage;
struct RenderSceneConverterEnv;

namespace material_utils {

// Texture transform utilities
struct TexTransform2d {
  std::array<float, 2> translation = {0.0f, 0.0f};
  float rotation = 0.0f;  // in degrees
  std::array<float, 2> scale = {1.0f, 1.0f};
  
  // Convert to 3x3 matrix
  std::array<float, 9> ToMatrix3x3() const;
};

TexTransform2d ConvertTexTransform2d(const value::texcoord2f &xform);

// Texture connection and loading
struct TextureConnection {
  Path texture_path;
  std::string uv_name = "st";
  TexTransform2d transform;
  bool found = false;
};

TextureConnection GetConnectedUVTexture(
    const Shader &shader,
    const std::string &input_name,
    const RenderSceneConverterEnv &env);

// Color space utilities
enum class ColorSpace {
  Unknown,
  Linear,
  sRGB,
  ACEScg,
  Rec709,
  Raw
};

ColorSpace InferColorSpace(const std::string &filename);
ColorSpace ParseColorSpace(const std::string &colorSpace);
std::string ToString(ColorSpace cs);

// Texture image loading
using TextureImageLoaderFunction = std::function<bool(
    const std::string &filename,
    const std::string &base_dir,
    TextureImage *image,
    std::string *warn,
    std::string *err)>;

bool DefaultTextureImageLoaderFunction(
    const std::string &filename,
    const std::string &base_dir,
    TextureImage *image,
    std::string *warn,
    std::string *err);

// Asset reading utilities
struct AssetInfo {
  std::vector<uint8_t> data;
  std::string resolved_path;
  std::string content_type;
  bool success = false;
  std::string error;
};

AssetInfo RawAssetRead(
    const std::string &asset_path,
    const std::string &base_dir,
    const RenderSceneConverterEnv &env);

// Material conversion utilities
struct MaterialConversionOptions {
  bool convert_preview_surface = true;
  bool convert_mdl_materials = false;
  bool use_display_colors = false;
  bool flatten_texture_transforms = false;
};

bool ConvertMaterial(
    const Material &usd_material,
    RenderMaterial &render_material,
    const MaterialConversionOptions &options,
    const RenderSceneConverterEnv &env);

// Preview surface conversion
bool ConvertPreviewSurface(
    const UsdPreviewSurface &surface,
    RenderMaterial &material,
    const RenderSceneConverterEnv &env);

// UV texture conversion
bool ConvertUVTexture(
    const UsdUVTexture &uv_texture,
    UVTexture &texture,
    const RenderSceneConverterEnv &env);

// Material property extraction
struct MaterialProperties {
  value::float3 diffuseColor = {0.18f, 0.18f, 0.18f};
  value::float3 emissiveColor = {0.0f, 0.0f, 0.0f};
  value::float3 specularColor = {0.0f, 0.0f, 0.0f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float clearcoat = 0.0f;
  float clearcoatRoughness = 0.01f;
  float ior = 1.5f;
  float opacity = 1.0f;
  float displacement = 0.0f;
  float occlusion = 1.0f;
  
  // Texture connections
  TextureConnection diffuseColorTexture;
  TextureConnection emissiveColorTexture;
  TextureConnection metallicTexture;
  TextureConnection roughnessTexture;
  TextureConnection normalTexture;
  TextureConnection occlusionTexture;
  TextureConnection opacityTexture;
};

MaterialProperties ExtractMaterialProperties(
    const Shader &shader,
    const RenderSceneConverterEnv &env);

// Material assignment utilities
struct MaterialBinding {
  Path material_path;
  Path collection_path;
  std::string purpose = "allPurpose";
  uint32_t strength = 0;  // Higher is stronger
};

MaterialBinding GetMaterialBinding(
    const Prim &prim,
    const RenderSceneConverterEnv &env);

std::vector<MaterialBinding> GetMaterialBindings(
    const Prim &prim,
    const RenderSceneConverterEnv &env);

// Shader node graph utilities
struct ShaderNode {
  std::string type;
  std::string name;
  std::map<std::string, value::Value> inputs;
  std::map<std::string, value::Value> outputs;
  std::map<std::string, Path> connections;
};

struct ShaderGraph {
  std::vector<ShaderNode> nodes;
  std::map<std::string, size_t> node_map;
  Path output_terminal;
};

ShaderGraph BuildShaderGraph(
    const Material &material,
    const RenderSceneConverterEnv &env);

} // namespace material_utils

} // namespace tydra
} // namespace tinyusdz