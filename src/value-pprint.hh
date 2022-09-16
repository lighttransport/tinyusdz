// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#pragma once

#include "value-types.hh"

// forward decl
namespace tinyusdz {

// in prim-types.hh
class Path;
struct StringData; 

} // namespace tinyusdz

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float4 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double2 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double3 &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double4 &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3d &v);


std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::texcoord2h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::texcoord2f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::texcoord2d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::texcoord3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::texcoord3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::texcoord3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quath &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatf &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatd &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::token &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::dict &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::TimeSamples &ts);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::matrix2d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::matrix3d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::matrix4d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::AssetPath &v);

// NOTE: Implemented in pprinter.cc
std::ostream &operator<<(std::ostream &os, const tinyusdz::Path &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::StringData &v);

// 1D array
template <typename T>
inline std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
  os << "[";
  for (size_t i = 0; i < v.size(); i++) {
    os << v[i];
    if (i != (v.size() - 1)) {
      os << ", ";
    }
  }
  os << "]";
  return os;
}

// Provide specialized version for int and float array.
template <>
std::ostream &operator<<(std::ostream &os, const std::vector<double> &v);

template <>
std::ostream &operator<<(std::ostream &os, const std::vector<float> &v);

template <>
std::ostream &operator<<(std::ostream &os, const std::vector<int32_t> &v);

template <>
std::ostream &operator<<(std::ostream &os, const std::vector<uint32_t> &v);

template <>
std::ostream &operator<<(std::ostream &os, const std::vector<int64_t> &v);

template <>
std::ostream &operator<<(std::ostream &os, const std::vector<uint64_t> &v);


} // namespace std

namespace tinyusdz {
namespace value {

std::string pprint_value(const tinyusdz::value::Value &v, const uint32_t indent = 0, bool closing_brace = true);

// TODO: Remove
//std::string pprint_any(const linb::any &v, const uint32_t indent = 0, bool closing_brace = true);

} // namespace primvar
} // namespace tinyusdz
