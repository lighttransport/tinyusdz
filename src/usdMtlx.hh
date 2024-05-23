// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.
//
// Predefined MaterialX shadingmodel & Built-in MaterialX XML import plugIn.
// Import only. Export is not supported(yet).
//
//
// example usage
//
// def Shader "mtlx_shader" (
// )
// {
//    uniform token info:id = "..."
//    asset inputs:file = @input.mtlx@
//    ...
// }
//
//
// Corresponding USD Shader C++ class is defined in usdShade.hh.
//
// This module implements .mtlx load and access to its content.
//
// Based on MaterialX spec v1.38

#pragma once

#include <string>
#include <vector>
#include <map>

#include "asset-resolution.hh"
#include "usdShade.hh"

namespace tinyusdz {

constexpr auto kMtlxUsdPreviewSurface = "MtlxUsdPreviewSurface";
constexpr auto kMtlxAutodeskStandardSurface = "MtlxAutodeskStandaradSurface";

namespace mtlx {

// TODO: use tinyusdz::value?
enum class MtlxType {
  Invalid, // invalid
  Filename,
  Boolean,
  String,
  Integer,
  Float,
  Float2,
  Float3,
  Float4,
  Color3,
  Color4,
  Vector2,
  Vector3,
  Vector4,
  Matrix22,
  Matrix33,
  Matrix44,
  BSDF,
  EDF
};

// For NodeDef
struct InputNode
{
  std::string name;
  std::string implname;
  MtlxType type{MtlxType::Invalid};
  bool uniform{false};

  std::vector<std::string> enums;

  std::vector<std::string> svalues; // for string or filename type
  std::vector<double> dvalues; // for non-string types

  std::string uiname;
  std::string uifolder;
  std::vector<double> uimax{1.0};
  std::vector<double> uisoftmax{1.0};
  std::vector<double> uimin{0.0};
  bool uiadvanced{false};

  // Unknown or user-defined parameters
  std::map<std::string, std::string> user_params;
};

struct OutputNode
{
  std::string name;
  std::string type;
};

struct NodeDef
{
  std::string name;
  std::string node;
  std::string nodegroup;
  std::string doc;

  std::vector<InputNode> inputs;
  std::vector<OutputNode> outputs;
};

struct Convert
{
  // in
};

struct AddNode
{
  // in1
  // in2
};

struct SubtractNode
{
  // in1
  // in2
};

struct MultiplyNode
{
  // in1
  // in2
};

struct MixNode
{
  // fg
  // bg
  // mix
};

struct ClampNode
{
  // in
  // low
  // high
};

struct IfGreaterEq
{
  // value1
  // value2
  // in1
  // in2
};


struct OrenNayarDiffuseBsdf
{
  // weight
  // color
  // roughness
  // normal
};

struct DielectricBsdf
{
  // weight
  // tint
  // ior
  // roughness
  // normal
  // scatter_mode
};

struct RoughnessAnisotropy
{
  // roughness
  // anisotropy
};

struct GeneratedSchlickBsdf
{
  // weight
  // color0
  // color90
  // roughness
  // normal

};

struct UniformEdf
{

};

struct Layer
{
  // top
  // base
};

// User defined Node
struct CustomNode
{

};

struct NodeGraph
{

};

class UsdMtlx
{
  std::string filepath; // file path of .mtlx. empty when .mtlx is read from string.
  std::string version{"1.38"};
};


enum class ColorSpace {
  Lin_rec709, // lin_rec709
  Unknown
};

} // namespace mtlx

// <surfacematerial>
struct MtlxMaterial {
  std::string name;
  std::string typeName;
  std::string nodename;
};

struct MtlxModel {
  std::string asset_name;

  std::string version;
  std::string cms;
  std::string cmsconfig; // filename
  std::string color_space; // colorspace
  std::string name_space; // namespace

  //mtlx::ColorSpace colorspace{Lin_rec709};
  // TODO

  std::string shader_name;

  // Content of shader.
  // MtlxUsdPreviewSurface or MtlxAutodeskStandaradSurface
  value::Value shader;

  std::map<std::string, MtlxMaterial> surface_materials;
  std::map<std::string, value::Value> shaders; // MtlxUsdPreviewSurface or MtlxAutodeskStandaradSurface
};

struct MtlxUsdPreviewSurface : UsdPreviewSurface {
  //  TODO: add mtlx specific attribute.
};

// https://github.com/Autodesk/standard-surface/blob/master/reference/standard_surface.mtlx
// We only support v1.0.1
struct MtlxAutodeskStandardSurface : ShaderNode {
  TypedAttributeWithFallback<Animatable<float>> base{1.0f};
  TypedAttributeWithFallback<Animatable<value::color3f>> baseColor{
      value::color3f{0.8f, 0.8f, 0.8f}};  // color3

  // TODO
  // ...

  // (coat_affect_roughness * coat) * coat_roughness
  TypedAttribute<Animatable<float>> coat_affect_roughness;
  TypedAttribute<Animatable<float>> coat;
  TypedAttribute<Animatable<float>> coat_roughness;

  // (specular_roughness + transmission_extra_roughness)
  TypedAttribute<Animatable<float>> specular_roughness;
  TypedAttribute<Animatable<float>> transmission_extra_roughness;
  TypedAttribute<Animatable<float>> transmission_roughness_add;

  // tangent_rotate_normalize
  // normalize(rotate3d(/* in */tangent, /*amount*/(specular_rotation * 360), /*
  // axis */normal))
  TypedAttribute<Animatable<float>> specular_rotation;

  // Output
  TypedTerminalAttribute<value::token> out;  // 'out'
};

//
// IO
//

///
/// Load MaterialX XML from a string.
///
/// @param[in] str String representation of XML data.
/// @param[in] asset_name Corresponding asset name. Can be empty.
/// @param[out] mtlx Output
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
bool ReadMaterialXFromString(const std::string &str, const std::string &asset_name, MtlxModel *mtlx,
                             std::string *warn, std::string *err);

///
/// Load MaterialX XML from a file.
///
/// @param[in] str String representation of XML data.
/// @param[in] asset_name Corresponding asset name. Can be empty.
/// @param[out] mtlx Output
/// @param[out] err Error message
///
/// @return true upon success.
///
/// TODO: Use FileSystem handler

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver,
                            const std::string &asset_path, MtlxModel *mtlx,
                            std::string *warn, std::string *err);

bool WriteMaterialXToString(const MtlxModel &mtlx, std::string &xml_str,
                             std::string *warn, std::string *err);

bool ToPrimSpec(const MtlxModel &model, PrimSpec &ps, std::string *err);

///
/// Load MaterialX from Asset and construct USD PrimSpec
///
bool LoadMaterialXFromAsset(const Asset &asset,
                            const std::string &asset_path, PrimSpec &ps /* inout */,
                            std::string *warn, std::string *err);

// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// ShaderNodes
DEFINE_TYPE_TRAIT(MtlxUsdPreviewSurface, kMtlxUsdPreviewSurface,
                  TYPE_ID_IMAGING_MTLX_PREVIEWSURFACE, 1);
DEFINE_TYPE_TRAIT(MtlxAutodeskStandardSurface, kMtlxAutodeskStandardSurface,
                  TYPE_ID_IMAGING_MTLX_STANDARDSURFACE, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
