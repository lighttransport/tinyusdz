// SPDX-License-Identifier: Apache 2.0
//
// Built-in MaterialX XML import plugIn.
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

#pragma once

#include <string>
#include <vector>
#include <map>

namespace tinyusdz {
namespace usdMtlx {

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

bool ReadMaterialXFromString(const std::string &str, UsdMtlx *mtlx, std::string *err = nullptr);
bool ReadMaterialXFromFile(const std::string &filepath, UsdMtlx *mtlx, std::string *err = nullptr);


} // namespace usdMtlx
} // namespace tinyusdz
