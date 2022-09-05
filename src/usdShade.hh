// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// Material and Shader. And more, TinyUSDZ implmenents some usdImaging stuff here.
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

//
// Similar to Maya's ShadingGroup
//
struct Material {
  std::string name;

  int64_t parent_id{-1};

  PrimMeta meta;

  Relation surface; // outputs:surface.connect
  Relation volume; // outputs:volume.connect

};

// TODO
struct NodeGraph {
  std::string name;

  int64_t parent_id{-1};

  PrimMeta meta;
};

template <typename T>
struct UsdPrimvarReader {
  std::string name;

  PrimMeta meta;

  TypedAttribute<T> fallback;  // "inputs:fallback"

  TypedAttribute<value::token> varname;  // "token inputs:varname". Name of the primvar to be fetched from the geometry("primvar" namespace is omitted).


  TypedTerminalAttribute<T> result; // Terminal attr. "T outputs:result"

  // Custom properties
  std::map<std::string, Property> props;
};

using UsdPrimvarReader_float = UsdPrimvarReader<float>;
using UsdPrimvarReader_float2 = UsdPrimvarReader<value::float2>;
using UsdPrimvarReader_float3 = UsdPrimvarReader<value::float3>;
using UsdPrimvarReader_float4 = UsdPrimvarReader<value::float4>;
using UsdPrimvarReader_int = UsdPrimvarReader<int>;

using UsdPrimvarReaderType =
    tinyusdz::variant<UsdPrimvarReader_float, UsdPrimvarReader_float2,
                      UsdPrimvarReader_float3, UsdPrimvarReader_float4,
                      UsdPrimvarReader_int>;


struct UsdUVTexture {

  enum class Wrap {
    UseMetadata, // "useMetadata" (default)
    Black, // "black"
    Clamp, // "clamp"
    Repeat, // "repeat"
    Mirror, // "mirror"
  };

  enum class SourceColorSpace {
    Auto, // "auto"(default)
    Raw, // "raw"
    SRGB, // "sRGB
  };

  std::string name;

  TypedAttribute<value::AssetPath> file; // "asset inputs:file" interfaceOnly

  //TypedAttributeWithFallback<Connection<value::texcoord2f>> st{Connection<value::texcoord2f>({0.0f, 0.0f})}; // "inputs:st"
  TypedProperty<value::texcoord2f> st{{0.0f, 0.0f}}; // "inputs:st"

  nonstd::optional<Wrap> wrapS; // "inputs:wrapS" interfaceOnly
  nonstd::optional<Wrap> wrapT; // "inputs:wrapT" interfaceOnly

  TypedAttributeWithFallback<value::color4f> falllback{{0.0f, 0.0f, 0.0f, 1.0f}}; // "inputs:fallback" Fallback value when no texture is connected(TODO: Disallow Relation?(i.e, `fallback.connect = </Path/To/FallbackColor>`)

  nonstd::optional<SourceColorSpace> sourceColorSpace; // "inputs:sourceColorSpace" interfaceOnly

  TypedAttributeWithFallback<value::float4> scale{{1.0f, 1.0f, 1.0f, 1.0f}}; // "inputs:scale" interfaceOnly
  TypedAttributeWithFallback<value::float4> bias{{0.0f, 0.0f, 0.0f, 0.0f}}; // "inputs:bias" interfaceOnly

  ///
  /// Outputs
  /// 
  /// Terminal attribute. No value assign(e.g. `float outputs:r = 1.2`)
  ///
  TypedTerminalAttribute<float> outputsR; // "float outputs:r"
  TypedTerminalAttribute<float> outputsG; // "float outputs:g"
  TypedTerminalAttribute<float> outputsB; // "float outputs:b"
  TypedTerminalAttribute<float> outputsA; // "float outputs:a"
  TypedTerminalAttribute<value::float3> outputsRGB; // "float3 outputs:rgb"

  // Custom properties
  std::map<std::string, Property> props;

  PrimMeta meta;

  // TODO: orientation?
  // https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html#UsdPreviewSurfaceProposal-TextureCoordinateOrientationinUSD
};

// UsdPreviewSurface
// USD's default? PBR shader
// https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html
// $USD/pxr/usdImaging/plugin/usdShaders/shaders/shaderDefs.usda

struct UsdPreviewSurface {

  std::string name;

  TypedProperty<value::color3f> diffuseColor{{0.18f, 0.18f, 0.18f}};  // "inputs:diffuseColor"
  TypedProperty<value::color3f> emissiveColor{{0.0f, 0.0f, 0.0f}};  // "inputs:emissiveColor"

  TypedProperty<int> useSpecularWorkflow{0}; // "inputs:useSpecularWorkflow"

  // specular workflow
  TypedProperty<value::color3f> specularColor{{0.0f, 0.0f, 0.0f}};

  // metalness workflow
  //TypedAttributeWithFallback<float> metallic{0.0f};  // "inputs:metallic"
  TypedProperty<float> metallic{0.0f};  // "inputs:metallic"

  //
  TypedProperty<float> clearcoat{0.0f};  // "inputs:clearcoat"
  TypedProperty<float> clearcoatRoughness{0.01f};  // "inputs:clearcoatRouighness"
  TypedProperty<float> roughness{0.5f};  // "inputs:roughness"
  TypedProperty<float> opacity{1.0f};  // "inputs:opacity"
  TypedProperty<float> opacityThreshold{0.0f};  // "inputs:opacityThreshold"
  TypedProperty<float> ior{1.5f};  // "inputs:ior"

  TypedProperty<value::normal3f> normal{{0.0f, 0.0f, 1.0f}}; // "inputs:normal"
  TypedProperty<float> displacement{0.0f}; // "inputs:displacement"
  TypedProperty<float> occlusion{0.0f}; // "inputs:occlusion"

  ///
  /// Outputs
  ///
  /// Terminal attribute(No value assigned or `.connect` only)
  ///
  nonstd::optional<Relation> outputsSurface; // "token outputs:surface", "outputs:surface.connect"
  nonstd::optional<Relation> outputsDisplacement; // "token outputs:displacement", "outputs:displacement.connect"

  // Custom properties
  std::map<std::string, Property> props;

  PrimMeta meta;
};

#if 0 // TODO: Move to Tydra
struct PreviewSurface {
  std::string doc;

  //
  // Infos
  //
  std::string info_id;  // `uniform token`

  //
  // Inputs
  //
  // Currently we don't support nested shader description.
  //
  Color3OrTexture diffuseColor{0.18f, 0.18f, 0.18f};
  Color3OrTexture emissiveColor{0.0f, 0.0f, 0.0f};
  int usdSpecularWorkflow{0};  // 0 = metalness workflow, 1 = specular workflow

  // specular workflow
  Color3OrTexture specularColor{0.0f, 0.0f, 0.0f};

  // metalness workflow
  FloatOrTexture metallic{0.0f};

  FloatOrTexture roughness{0.5f};
  FloatOrTexture clearcoat{0.0f};
  FloatOrTexture clearcoatRoughness{0.01f};
  FloatOrTexture opacity{1.0f};
  FloatOrTexture opacityThreshold{0.0f};
  FloatOrTexture ior{1.5f};
  Color3OrTexture normal{0.0f, 0.0f, 1.0f};
  FloatOrTexture displacement{0.0f};
  FloatOrTexture occlusion{1.0f};

  // Blender Specific?
  FloatOrTexture specular{0.5f};

  //
  // Outputs
  //
  int64_t surface_id{-1};       // index to `Scene::shaders`
  int64_t displacement_id{-1};  // index to `Scene::shaders`
};
#endif

struct Shader {
  std::string name;

  std::string info_id;  // Shader type.

  // UsdPreviewSurface, UsdUVTexture, UsdPrimvarReader_float2, ...
  value::Value value;
#if 0
  // Currently we only support PreviewSurface, UVTexture and
  // PrimvarReader_float2
  tinyusdz::variant<tinyusdz::monostate, PreviewSurface, UVTexture,
                    PrimvarReader_float2>
      value;
#endif

  PrimMeta meta;
};


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// Mateiral
DEFINE_TYPE_TRAIT(Material, "Material",
                  TYPE_ID_MATERIAL, 1);

// Shader
DEFINE_TYPE_TRAIT(Shader, "Shader",
                  TYPE_ID_SHADER, 1);

DEFINE_TYPE_TRAIT(UsdPreviewSurface, "UsdPreviewSurface",
                  TYPE_ID_IMAGING_PREVIEWSURFACE, 1);
DEFINE_TYPE_TRAIT(UsdUVTexture, "UsdUVTexture", TYPE_ID_IMAGING_UVTEXTURE, 1);

DEFINE_TYPE_TRAIT(UsdPrimvarReader_float, "UsdPrimvarReader_float",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT, 1);
DEFINE_TYPE_TRAIT(UsdPrimvarReader_float2, "UsdPrimvarReader_float2",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT2, 1);
DEFINE_TYPE_TRAIT(UsdPrimvarReader_float3, "UsdPrimvarReader_float3",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT3, 1);
DEFINE_TYPE_TRAIT(UsdPrimvarReader_float4, "UsdPrimvarReader_float4",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT4, 1);
DEFINE_TYPE_TRAIT(UsdPrimvarReader_int, "UsdPrimvarReader_int",
                  TYPE_ID_IMAGING_PRIMVAR_READER_INT, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
