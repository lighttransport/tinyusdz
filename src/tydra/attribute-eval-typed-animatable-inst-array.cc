// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// EvaluateTypedAnimatableAttribute template instantiations - Array types
// Split from attribute-eval-typed-animatable.cc for parallel compilation

#include "attribute-eval.hh"
#include "scene-access.hh"

#include "common-macros.inc"
#include "pprinter.hh"
#include "tiny-format.hh"
#include "value-pprint.hh"

namespace tinyusdz {
namespace tydra {

// Include template implementations
#include "attribute-eval-typed-animatable-impl.inc"

// Explicit template instantiations - Array types only

#define INST(__ty) \
template bool EvaluateTypedAnimatableAttribute(const tinyusdz::Stage &stage, const TypedAttribute<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const value::TimeSampleInterpolationType tinterp);

// Basic array types
INST(std::vector<bool>)
INST(std::vector<value::AssetPath>)
INST(std::vector<value::token>)
INST(std::vector<std::string>)
INST(std::vector<value::StringData>)

// Half precision array types
INST(std::vector<value::half>)
INST(std::vector<value::half2>)
INST(std::vector<value::half3>)
INST(std::vector<value::half4>)

// Integer array types
INST(std::vector<int32_t>)
INST(std::vector<uint32_t>)
INST(std::vector<value::int2>)
INST(std::vector<value::int3>)
INST(std::vector<value::int4>)
INST(std::vector<value::uint2>)
INST(std::vector<value::uint3>)
INST(std::vector<value::uint4>)
INST(std::vector<int64_t>)
INST(std::vector<uint64_t>)

// Float array types
INST(std::vector<float>)
INST(std::vector<value::float2>)
INST(std::vector<value::float3>)
INST(std::vector<value::float4>)

// Double array types
INST(std::vector<double>)
INST(std::vector<value::double2>)
INST(std::vector<value::double3>)
INST(std::vector<value::double4>)

// Quaternion array types
INST(std::vector<value::quath>)
INST(std::vector<value::quatf>)
INST(std::vector<value::quatd>)

// Normal array types
INST(std::vector<value::normal3h>)
INST(std::vector<value::normal3f>)
INST(std::vector<value::normal3d>)

// Vector array types
INST(std::vector<value::vector3h>)
INST(std::vector<value::vector3f>)
INST(std::vector<value::vector3d>)

// Point array types
INST(std::vector<value::point3h>)
INST(std::vector<value::point3f>)
INST(std::vector<value::point3d>)

// Color array types
INST(std::vector<value::color3f>)
INST(std::vector<value::color3d>)
INST(std::vector<value::color4h>)
INST(std::vector<value::color4f>)
INST(std::vector<value::color4d>)

// Texcoord array types
INST(std::vector<value::texcoord2h>)
INST(std::vector<value::texcoord2f>)
INST(std::vector<value::texcoord2d>)
INST(std::vector<value::texcoord3h>)
INST(std::vector<value::texcoord3f>)
INST(std::vector<value::texcoord3d>)

// Matrix array types
INST(std::vector<value::matrix2d>)
INST(std::vector<value::matrix3d>)
INST(std::vector<value::matrix4d>)
INST(std::vector<value::frame4d>)

#undef INST

}  // namespace tydra
}  // namespace tinyusdz
