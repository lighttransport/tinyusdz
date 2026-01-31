// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TypedTimeSamples explicit template instantiations - Scalar types
// Split from timesamples.cc for parallel compilation

#include "value-types.hh"
#include "timesamples.hh"
#include "value-eval-util.hh"  // For lerp functions

namespace tinyusdz {

// Include get() implementations (must be in tinyusdz namespace)
#include "timesamples-get-impl.inc"

// Explicit template struct instantiations
// TypedTimeSamples is in tinyusdz namespace, types like half/float2 are in value namespace

// Integer types (POD, non-lerp'able)
template struct TypedTimeSamples<bool>;
template struct TypedTimeSamples<int32_t>;
template struct TypedTimeSamples<uint32_t>;
template struct TypedTimeSamples<int64_t>;
template struct TypedTimeSamples<uint64_t>;

// Floating point scalar types (POD, lerp'able)
template struct TypedTimeSamples<value::half>;
template struct TypedTimeSamples<float>;
template struct TypedTimeSamples<double>;

// Vector types (POD, lerp'able)
template struct TypedTimeSamples<value::half2>;
template struct TypedTimeSamples<value::half3>;
template struct TypedTimeSamples<value::half4>;
template struct TypedTimeSamples<value::float2>;
template struct TypedTimeSamples<value::float3>;
template struct TypedTimeSamples<value::float4>;
template struct TypedTimeSamples<value::double2>;
template struct TypedTimeSamples<value::double3>;
template struct TypedTimeSamples<value::double4>;

// Integer vector types (POD, non-lerp'able)
template struct TypedTimeSamples<value::int2>;
template struct TypedTimeSamples<value::int3>;
template struct TypedTimeSamples<value::int4>;

// Quaternion types (POD, lerp'able)
template struct TypedTimeSamples<value::quath>;
template struct TypedTimeSamples<value::quatf>;
template struct TypedTimeSamples<value::quatd>;

// Matrix types (lerp'able)
template struct TypedTimeSamples<value::matrix2f>;
template struct TypedTimeSamples<value::matrix3f>;
template struct TypedTimeSamples<value::matrix4f>;
template struct TypedTimeSamples<value::matrix2d>;
template struct TypedTimeSamples<value::matrix3d>;
template struct TypedTimeSamples<value::matrix4d>;

// Role types (POD, lerp'able)
template struct TypedTimeSamples<value::normal3h>;
template struct TypedTimeSamples<value::normal3f>;
template struct TypedTimeSamples<value::normal3d>;
template struct TypedTimeSamples<value::vector3h>;
template struct TypedTimeSamples<value::vector3f>;
template struct TypedTimeSamples<value::vector3d>;
template struct TypedTimeSamples<value::point3h>;
template struct TypedTimeSamples<value::point3f>;
template struct TypedTimeSamples<value::point3d>;
template struct TypedTimeSamples<value::color3h>;
template struct TypedTimeSamples<value::color3f>;
template struct TypedTimeSamples<value::color3d>;
template struct TypedTimeSamples<value::color4h>;
template struct TypedTimeSamples<value::color4f>;
template struct TypedTimeSamples<value::color4d>;
template struct TypedTimeSamples<value::texcoord2h>;
template struct TypedTimeSamples<value::texcoord2f>;
template struct TypedTimeSamples<value::texcoord2d>;
template struct TypedTimeSamples<value::texcoord3h>;
template struct TypedTimeSamples<value::texcoord3f>;
template struct TypedTimeSamples<value::texcoord3d>;

// Other types
template struct TypedTimeSamples<value::timecode>;
template struct TypedTimeSamples<value::frame4d>;
template struct TypedTimeSamples<std::string>;
template struct TypedTimeSamples<value::token>;
template struct TypedTimeSamples<value::dict>;
template struct TypedTimeSamples<value::AssetPath>;

// ============================================================================
// Explicit template instantiations for TypedTimeSamples::get() - Scalar types
// ============================================================================

// For non-interpolatable integer types
template bool TypedTimeSamples<bool>::get<bool>(bool*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<int32_t>::get<int32_t>(int32_t*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<uint32_t>::get<uint32_t>(uint32_t*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<int64_t>::get<int64_t>(int64_t*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<uint64_t>::get<uint64_t>(uint64_t*, double, value::TimeSampleInterpolationType) const;

// For interpolatable floating-point types
template bool TypedTimeSamples<value::half>::get<value::half>(value::half*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<float>::get<float>(float*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<double>::get<double>(double*, double, value::TimeSampleInterpolationType) const;

// For interpolatable vector types - using std::array forms
template bool TypedTimeSamples<std::array<value::half, 2>>::get<std::array<value::half, 2>>(std::array<value::half, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<value::half, 3>>::get<std::array<value::half, 3>>(std::array<value::half, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<value::half, 4>>::get<std::array<value::half, 4>>(std::array<value::half, 4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<float, 2>>::get<std::array<float, 2>>(std::array<float, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<float, 3>>::get<std::array<float, 3>>(std::array<float, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<float, 4>>::get<std::array<float, 4>>(std::array<float, 4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<double, 2>>::get<std::array<double, 2>>(std::array<double, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<double, 3>>::get<std::array<double, 3>>(std::array<double, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<double, 4>>::get<std::array<double, 4>>(std::array<double, 4>*, double, value::TimeSampleInterpolationType) const;

// For non-interpolatable integer vector types
template bool TypedTimeSamples<std::array<int, 2>>::get<std::array<int, 2>>(std::array<int, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<int, 3>>::get<std::array<int, 3>>(std::array<int, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<int, 4>>::get<std::array<int, 4>>(std::array<int, 4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<uint32_t, 2>>::get<std::array<uint32_t, 2>>(std::array<uint32_t, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<uint32_t, 3>>::get<std::array<uint32_t, 3>>(std::array<uint32_t, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<uint32_t, 4>>::get<std::array<uint32_t, 4>>(std::array<uint32_t, 4>*, double, value::TimeSampleInterpolationType) const;

// For interpolatable quaternion types
template bool TypedTimeSamples<value::quath>::get<value::quath>(value::quath*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::quatf>::get<value::quatf>(value::quatf*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::quatd>::get<value::quatd>(value::quatd*, double, value::TimeSampleInterpolationType) const;

// For interpolatable matrix types
template bool TypedTimeSamples<value::matrix2f>::get<value::matrix2f>(value::matrix2f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix3f>::get<value::matrix3f>(value::matrix3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix4f>::get<value::matrix4f>(value::matrix4f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix2d>::get<value::matrix2d>(value::matrix2d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix3d>::get<value::matrix3d>(value::matrix3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix4d>::get<value::matrix4d>(value::matrix4d*, double, value::TimeSampleInterpolationType) const;

// For interpolatable role types
template bool TypedTimeSamples<value::normal3h>::get<value::normal3h>(value::normal3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::normal3f>::get<value::normal3f>(value::normal3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::normal3d>::get<value::normal3d>(value::normal3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::vector3h>::get<value::vector3h>(value::vector3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::vector3f>::get<value::vector3f>(value::vector3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::vector3d>::get<value::vector3d>(value::vector3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::point3h>::get<value::point3h>(value::point3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::point3f>::get<value::point3f>(value::point3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::point3d>::get<value::point3d>(value::point3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color3h>::get<value::color3h>(value::color3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color3f>::get<value::color3f>(value::color3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color3d>::get<value::color3d>(value::color3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color4h>::get<value::color4h>(value::color4h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color4f>::get<value::color4f>(value::color4f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color4d>::get<value::color4d>(value::color4d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord2h>::get<value::texcoord2h>(value::texcoord2h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord2f>::get<value::texcoord2f>(value::texcoord2f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord2d>::get<value::texcoord2d>(value::texcoord2d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord3h>::get<value::texcoord3h>(value::texcoord3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord3f>::get<value::texcoord3f>(value::texcoord3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord3d>::get<value::texcoord3d>(value::texcoord3d*, double, value::TimeSampleInterpolationType) const;

// For non-interpolatable other types
template bool TypedTimeSamples<value::timecode>::get<value::timecode>(value::timecode*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::frame4d>::get<value::frame4d>(value::frame4d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::string>::get<std::string>(std::string*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::token>::get<value::token>(value::token*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::dict>::get<value::dict>(value::dict*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::AssetPath>::get<value::AssetPath>(value::AssetPath*, double, value::TimeSampleInterpolationType) const;

}  // namespace tinyusdz
