// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
//
// Shader network evaluation

#include <unordered_map>

#include "nonstd/expected.hpp"
#include "value-types.hh"

namespace tinyusdz {

// forward decl of usdShade
class Stage;
class Prim;
struct Material;
struct Shader;

template<typename T>
struct UsdPrimvarReader;

using UsdPrimvarReader_float = UsdPrimvarReader<float>;
using UsdPrimvarReader_float2 = UsdPrimvarReader<value::float2>;
using UsdPrimvarReader_float3 = UsdPrimvarReader<value::float3>;
using UsdPrimvarReader_float4 = UsdPrimvarReader<value::float4>;

namespace tydra {

// GLSL like data types
using vec2 = value::float2;
using vec3 = value::float3;
using vec4 = value::float4;
using mat2 = value::matrix2f;  // float precision


///
/// Evaluate and return terminal value of the shader connection.
/// It follows the value producing Attribute and copy its value.
/// Since the type of shader connection is known in advance, we return the value by template type T, not by `value::Value`
/// NOTE: The returned value is COPIED. This should be OK for Shader network since usually it does not hold large data...
///
/// @param[in] stage Stage
/// @param[in] shader Shader node in `stage`.
/// @param[in] prop_name Property name
/// @param[out] out_val Output evaluated value.
/// @param[out] err Error message
/// @param[in] timeCode (Optional) Evaluate the value at specified time(for timeSampled value)
///
/// @return true when specified `prop_name` exists in the given `shader` and can resolve the connection and retrieve "terminal" value.
///
template<typename T>
bool EvaluateShaderConnection(
  const Stage &stage,
  const Shader &shader, const std::string &prop_name,
  T * out_val,
  std::string *err,
  const value::TimeCode timeCode = value::TimeCode::Default());
 
// Currently float2 only
//std::vector<UsdPrimvarReader_float2> ExtractPrimvarReadersFromMaterialNode(const Prim &node);

}  // namespace tydra
}  // namespace tinyusdz
