// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TypedTimeSamples explicit template instantiations - Role/matrix/special array types
// Split from timesamples.cc for parallel compilation
// Basic array types are in timesamples-inst-array-basic.cc

#include "value-types.hh"
#include "timesamples.hh"
#include "value-eval-util.hh"  // For lerp functions

namespace tinyusdz {

// Include get() implementations (must be in tinyusdz namespace)
#include "timesamples-get-impl.inc"

// Role types vectors - point3
template struct TypedTimeSamples<std::vector<value::point3h>>;
template struct TypedTimeSamples<std::vector<value::point3f>>;
template struct TypedTimeSamples<std::vector<value::point3d>>;

// Role types vectors - normal3
template struct TypedTimeSamples<std::vector<value::normal3h>>;
template struct TypedTimeSamples<std::vector<value::normal3f>>;
template struct TypedTimeSamples<std::vector<value::normal3d>>;

// Role types vectors - vector3
template struct TypedTimeSamples<std::vector<value::vector3h>>;
template struct TypedTimeSamples<std::vector<value::vector3f>>;
template struct TypedTimeSamples<std::vector<value::vector3d>>;

// Role types vectors - color3
template struct TypedTimeSamples<std::vector<value::color3h>>;
template struct TypedTimeSamples<std::vector<value::color3f>>;
template struct TypedTimeSamples<std::vector<value::color3d>>;

// Role types vectors - color4
template struct TypedTimeSamples<std::vector<value::color4h>>;
template struct TypedTimeSamples<std::vector<value::color4f>>;
template struct TypedTimeSamples<std::vector<value::color4d>>;

// Role types vectors - texcoord2
template struct TypedTimeSamples<std::vector<value::texcoord2h>>;
template struct TypedTimeSamples<std::vector<value::texcoord2f>>;
template struct TypedTimeSamples<std::vector<value::texcoord2d>>;

// Role types vectors - texcoord3
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

// String/token/path arrays
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

// ============================================================================
// Explicit template instantiations for TypedTimeSamples::get() - Role/matrix arrays
// ============================================================================

// Role types vectors (needed by usdGeom.cc and usdSkel.cc)
template bool TypedTimeSamples<std::vector<value::point3h>>::get<std::vector<value::point3h>>(std::vector<value::point3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::point3f>>::get<std::vector<value::point3f>>(std::vector<value::point3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::point3d>>::get<std::vector<value::point3d>>(std::vector<value::point3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::normal3h>>::get<std::vector<value::normal3h>>(std::vector<value::normal3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::normal3f>>::get<std::vector<value::normal3f>>(std::vector<value::normal3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::normal3d>>::get<std::vector<value::normal3d>>(std::vector<value::normal3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::vector3h>>::get<std::vector<value::vector3h>>(std::vector<value::vector3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::vector3f>>::get<std::vector<value::vector3f>>(std::vector<value::vector3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::vector3d>>::get<std::vector<value::vector3d>>(std::vector<value::vector3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color3h>>::get<std::vector<value::color3h>>(std::vector<value::color3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color3f>>::get<std::vector<value::color3f>>(std::vector<value::color3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color3d>>::get<std::vector<value::color3d>>(std::vector<value::color3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color4h>>::get<std::vector<value::color4h>>(std::vector<value::color4h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color4f>>::get<std::vector<value::color4f>>(std::vector<value::color4f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color4d>>::get<std::vector<value::color4d>>(std::vector<value::color4d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord2h>>::get<std::vector<value::texcoord2h>>(std::vector<value::texcoord2h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord2f>>::get<std::vector<value::texcoord2f>>(std::vector<value::texcoord2f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord2d>>::get<std::vector<value::texcoord2d>>(std::vector<value::texcoord2d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord3h>>::get<std::vector<value::texcoord3h>>(std::vector<value::texcoord3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord3f>>::get<std::vector<value::texcoord3f>>(std::vector<value::texcoord3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord3d>>::get<std::vector<value::texcoord3d>>(std::vector<value::texcoord3d>*, double, value::TimeSampleInterpolationType) const;

// Matrix types vectors
template bool TypedTimeSamples<std::vector<value::matrix2f>>::get<std::vector<value::matrix2f>>(std::vector<value::matrix2f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix3f>>::get<std::vector<value::matrix3f>>(std::vector<value::matrix3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix4f>>::get<std::vector<value::matrix4f>>(std::vector<value::matrix4f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix2d>>::get<std::vector<value::matrix2d>>(std::vector<value::matrix2d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix3d>>::get<std::vector<value::matrix3d>>(std::vector<value::matrix3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix4d>>::get<std::vector<value::matrix4d>>(std::vector<value::matrix4d>*, double, value::TimeSampleInterpolationType) const;

// String/token/path arrays
template bool TypedTimeSamples<std::vector<std::string>>::get<std::vector<std::string>>(std::vector<std::string>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::token>>::get<std::vector<value::token>>(std::vector<value::token>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::AssetPath>>::get<std::vector<value::AssetPath>>(std::vector<value::AssetPath>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::frame4d>>::get<std::vector<value::frame4d>>(std::vector<value::frame4d>*, double, value::TimeSampleInterpolationType) const;

// Special types used by tydra
template bool TypedTimeSamples<std::vector<value::StringData>>::get<std::vector<value::StringData>>(std::vector<value::StringData>*, double, value::TimeSampleInterpolationType) const;

// Additional vector array types
template bool TypedTimeSamples<std::vector<std::array<unsigned int, 2>>>::get<std::vector<std::array<unsigned int, 2>>>(std::vector<std::array<unsigned int, 2>>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<std::array<unsigned int, 3>>>::get<std::vector<std::array<unsigned int, 3>>>(std::vector<std::array<unsigned int, 3>>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<std::array<unsigned int, 4>>>::get<std::vector<std::array<unsigned int, 4>>>(std::vector<std::array<unsigned int, 4>>*, double, value::TimeSampleInterpolationType) const;

}  // namespace tinyusdz
