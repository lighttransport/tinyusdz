// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TypedTimeSamples explicit template instantiations - Array types
// Split from timesamples.cc for parallel compilation

#include "value-types.hh"
#include "timesamples.hh"
#include "value-eval-util.hh"  // For lerp functions

namespace tinyusdz {

// Include get() implementations (must be in tinyusdz namespace)
#include "timesamples-get-impl.inc"

// Explicit template struct instantiations for array types
// TypedTimeSamples is in tinyusdz namespace

// Common array types
template struct TypedTimeSamples<std::vector<bool>>;
template struct TypedTimeSamples<std::vector<int32_t>>;
template struct TypedTimeSamples<std::vector<uint32_t>>;
template struct TypedTimeSamples<std::vector<int64_t>>;
template struct TypedTimeSamples<std::vector<uint64_t>>;
template struct TypedTimeSamples<std::vector<value::half>>;
template struct TypedTimeSamples<std::vector<float>>;
template struct TypedTimeSamples<std::vector<double>>;
template struct TypedTimeSamples<std::vector<value::half2>>;
template struct TypedTimeSamples<std::vector<value::half3>>;
template struct TypedTimeSamples<std::vector<value::half4>>;
template struct TypedTimeSamples<std::vector<value::float2>>;
template struct TypedTimeSamples<std::vector<value::float3>>;
template struct TypedTimeSamples<std::vector<value::float4>>;
template struct TypedTimeSamples<std::vector<value::double2>>;
template struct TypedTimeSamples<std::vector<value::double3>>;
template struct TypedTimeSamples<std::vector<value::double4>>;
template struct TypedTimeSamples<std::vector<value::int2>>;
template struct TypedTimeSamples<std::vector<value::int3>>;
template struct TypedTimeSamples<std::vector<value::int4>>;
template struct TypedTimeSamples<std::vector<value::quath>>;
template struct TypedTimeSamples<std::vector<value::quatf>>;
template struct TypedTimeSamples<std::vector<value::quatd>>;

// Role types vectors (needed by usdGeom.cc and usdSkel.cc)
template struct TypedTimeSamples<std::vector<value::point3h>>;
template struct TypedTimeSamples<std::vector<value::point3f>>;
template struct TypedTimeSamples<std::vector<value::point3d>>;
template struct TypedTimeSamples<std::vector<value::normal3h>>;
template struct TypedTimeSamples<std::vector<value::normal3f>>;
template struct TypedTimeSamples<std::vector<value::normal3d>>;
template struct TypedTimeSamples<std::vector<value::vector3h>>;
template struct TypedTimeSamples<std::vector<value::vector3f>>;
template struct TypedTimeSamples<std::vector<value::vector3d>>;
template struct TypedTimeSamples<std::vector<value::color3h>>;
template struct TypedTimeSamples<std::vector<value::color3f>>;
template struct TypedTimeSamples<std::vector<value::color3d>>;
template struct TypedTimeSamples<std::vector<value::color4h>>;
template struct TypedTimeSamples<std::vector<value::color4f>>;
template struct TypedTimeSamples<std::vector<value::color4d>>;
template struct TypedTimeSamples<std::vector<value::texcoord2h>>;
template struct TypedTimeSamples<std::vector<value::texcoord2f>>;
template struct TypedTimeSamples<std::vector<value::texcoord2d>>;
template struct TypedTimeSamples<std::vector<value::texcoord3h>>;
template struct TypedTimeSamples<std::vector<value::texcoord3f>>;
template struct TypedTimeSamples<std::vector<value::texcoord3d>>;

// Matrix types vectors
template struct TypedTimeSamples<std::vector<value::matrix2f>>;
template struct TypedTimeSamples<std::vector<value::matrix3f>>;
template struct TypedTimeSamples<std::vector<value::matrix4f>>;
template struct TypedTimeSamples<std::vector<value::matrix2d>>;
template struct TypedTimeSamples<std::vector<value::matrix3d>>;
template struct TypedTimeSamples<std::vector<value::matrix4d>>;
template struct TypedTimeSamples<std::vector<std::string>>;
template struct TypedTimeSamples<std::vector<value::token>>;
template struct TypedTimeSamples<std::vector<value::AssetPath>>;
template struct TypedTimeSamples<std::vector<value::frame4d>>;

// Special types used by tydra
template struct TypedTimeSamples<std::vector<value::StringData>>;

// Additional vector array types
template struct TypedTimeSamples<std::vector<std::array<unsigned int, 2>>>;
template struct TypedTimeSamples<std::vector<std::array<unsigned int, 3>>>;
template struct TypedTimeSamples<std::vector<std::array<unsigned int, 4>>>;

}  // namespace tinyusdz
