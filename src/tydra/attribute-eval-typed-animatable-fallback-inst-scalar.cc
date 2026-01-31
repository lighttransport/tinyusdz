// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// EvaluateTypedAnimatableAttribute (with fallback) template instantiations - Scalar types
// Split from attribute-eval-typed-animatable-fallback.cc for parallel compilation

#include "attribute-eval.hh"
#include "scene-access.hh"

#include "common-macros.inc"
#include "pprinter.hh"
#include "tiny-format.hh"
#include "value-pprint.hh"

namespace tinyusdz {
namespace tydra {

// Include template implementations
#include "attribute-eval-typed-animatable-fallback-impl.inc"

// Explicit template instantiations - Scalar types only

#define INST(__ty) \
template bool EvaluateTypedAnimatableAttribute(const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const value::TimeSampleInterpolationType tinterp);

// Basic types
INST(bool)
INST(value::AssetPath)
INST(value::token)

// Half precision types
INST(value::half)
INST(value::half2)
INST(value::half3)
INST(value::half4)

// Integer types
INST(int32_t)
INST(uint32_t)
INST(value::int2)
INST(value::int3)
INST(value::int4)
INST(value::uint2)
INST(value::uint3)
INST(value::uint4)
INST(int64_t)
INST(uint64_t)

// Float types
INST(float)
INST(value::float2)
INST(value::float3)
INST(value::float4)

// Double types
INST(double)
INST(value::double2)
INST(value::double3)
INST(value::double4)

// Quaternion types
INST(value::quath)
INST(value::quatf)
INST(value::quatd)

// Normal types
INST(value::normal3h)
INST(value::normal3f)
INST(value::normal3d)

// Vector types
INST(value::vector3h)
INST(value::vector3f)
INST(value::vector3d)

// Point types
INST(value::point3h)
INST(value::point3f)
INST(value::point3d)

// Color types
INST(value::color3f)
INST(value::color3d)
INST(value::color4h)
INST(value::color4f)
INST(value::color4d)

// Texcoord types
INST(value::texcoord2h)
INST(value::texcoord2f)
INST(value::texcoord2d)
INST(value::texcoord3h)
INST(value::texcoord3f)
INST(value::texcoord3d)

// Matrix types
INST(value::matrix2d)
INST(value::matrix3d)
INST(value::matrix4d)
INST(value::frame4d)

#undef INST

}  // namespace tydra
}  // namespace tinyusdz
