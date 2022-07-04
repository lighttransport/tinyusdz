#pragma once

#include "primvar.hh"

namespace std {

// primvar type

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::int2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::int3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::int4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::uint2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::uint3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::uint4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::half2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::half3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::half4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::float2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::float3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::float4 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::double2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::double3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::double4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::point3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::point3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::point3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::normal3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::normal3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::normal3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3d &v);


std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color3d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color4f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color4d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord2h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord2f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord2d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::quath &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::quatf &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::quatd &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::token &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::dict &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::TimeSample &ts);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::matrix2d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::matrix3d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::matrix4d &v);


} // namespace std

namespace tinyusdz {
namespace primvar {

//std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::Value &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::any_value &v);

} // namespace primvar
} // namespace tinyusdz
