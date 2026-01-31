// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// To deal with too many sections in generated .obj error(happens in MinGW and MSVC)
// Split ParseTimeSamples to two .cc files.
//
// TODO
// - [x] Rewrite code with less C++ template code.

#include <cstdio>
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <atomic>
//#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#if defined(__wasi__)
#else
#include <mutex>
#include <thread>
#endif
#include <vector>

#include "ascii-parser.hh"
#include "str-util.hh"
#include "tiny-format.hh"

//
#if !defined(TINYUSDZ_DISABLE_MODULE_USDA_READER)

//

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external

//#include "external/fast_float/include/fast_float/fast_float.h"
//#include "external/jsteemann/atoi.h"
//#include "external/simple_match/include/simple_match/simple_match.hpp"
#include "nonstd/expected.hpp"

//

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

// Tentative
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

#include "common-macros.inc"
#include "io-util.hh"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"

// Extern template declarations for ParseBasicTypeArray
// These templates are explicitly instantiated in ascii-parser-basetype.cc
namespace tinyusdz {
namespace ascii {

// Int tuple types
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::int2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::int3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::int4> *result);
// Uint tuple types
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::uint2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::uint3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::uint4> *result);
// Char types
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<char> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::char2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::char3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::char4> *result);
// Uchar types
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<uint8_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::uchar2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::uchar3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::uchar4> *result);
// Short types
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<int16_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::short2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::short3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::short4> *result);
// Ushort types
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<uint16_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::ushort2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::ushort3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::ushort4> *result);
// Frame type
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<value::frame4d> *result);

}  // namespace ascii
}  // namespace tinyusdz

namespace tinyusdz {

namespace ascii {

namespace {

// ============================================================================
// TimeSample Array Value Parser Registry
// Replaces PARSE_TYPE macro if-else chain with O(1) lookup
// ============================================================================

// Function pointer type for array timesample value parsers
using TimeSampleArrayValueParserFn = bool (*)(AsciiParser*, value::Value*);

// Registry for ParseTimeSampleValueOfArrayType dispatch
struct TimeSampleArrayValueParserRegistry {
  std::map<uint32_t, TimeSampleArrayValueParserFn> parsers;

  static const TimeSampleArrayValueParserRegistry& Instance() {
    // Use pointer to avoid exit-time destructor (intentional leak for static registry)
    static TimeSampleArrayValueParserRegistry* instance = new TimeSampleArrayValueParserRegistry();
    return *instance;
  }

  TimeSampleArrayValueParserRegistry();

  bool Parse(AsciiParser* parser, uint32_t type_id, value::Value* result) const {
    auto it = parsers.find(type_id);
    if (it == parsers.end()) {
      return false;
    }
    return it->second(parser, result);
  }

  bool HasParser(uint32_t type_id) const {
    return parsers.find(type_id) != parsers.end();
  }
};

// Template helper for generating array parser functions
template<typename T>
static bool ParseTimeSampleArrayValue_impl(AsciiParser* parser, value::Value* result) {
  std::vector<T> typed_val;
  if (!parser->ParseBasicTypeArray(&typed_val)) {
    return false;
  }
  *result = value::Value(typed_val);
  return true;
}

// Initialize all parsers
TimeSampleArrayValueParserRegistry::TimeSampleArrayValueParserRegistry() {
  // Special types
  parsers[value::TypeTraits<value::AssetPath>::type_id()] = &ParseTimeSampleArrayValue_impl<value::AssetPath>;
  parsers[value::TypeTraits<value::token>::type_id()] = &ParseTimeSampleArrayValue_impl<value::token>;
  parsers[value::TypeTraits<std::string>::type_id()] = &ParseTimeSampleArrayValue_impl<std::string>;
  // Boolean
  parsers[value::TypeTraits<bool>::type_id()] = &ParseTimeSampleArrayValue_impl<bool>;
  // Int types
  parsers[value::TypeTraits<int32_t>::type_id()] = &ParseTimeSampleArrayValue_impl<int32_t>;
  parsers[value::TypeTraits<value::int2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::int2>;
  parsers[value::TypeTraits<value::int3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::int3>;
  parsers[value::TypeTraits<value::int4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::int4>;
  // Unsigned int types
  parsers[value::TypeTraits<uint32_t>::type_id()] = &ParseTimeSampleArrayValue_impl<uint32_t>;
  parsers[value::TypeTraits<value::uint2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::uint2>;
  parsers[value::TypeTraits<value::uint3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::uint3>;
  parsers[value::TypeTraits<value::uint4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::uint4>;
  // Char types
  parsers[value::TypeTraits<char>::type_id()] = &ParseTimeSampleArrayValue_impl<char>;
  parsers[value::TypeTraits<value::char2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::char2>;
  parsers[value::TypeTraits<value::char3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::char3>;
  parsers[value::TypeTraits<value::char4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::char4>;
  // Uchar types
  parsers[value::TypeTraits<uint8_t>::type_id()] = &ParseTimeSampleArrayValue_impl<uint8_t>;
  parsers[value::TypeTraits<value::uchar2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::uchar2>;
  parsers[value::TypeTraits<value::uchar3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::uchar3>;
  parsers[value::TypeTraits<value::uchar4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::uchar4>;
  // Short types
  parsers[value::TypeTraits<int16_t>::type_id()] = &ParseTimeSampleArrayValue_impl<int16_t>;
  parsers[value::TypeTraits<value::short2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::short2>;
  parsers[value::TypeTraits<value::short3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::short3>;
  parsers[value::TypeTraits<value::short4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::short4>;
  // Ushort types
  parsers[value::TypeTraits<uint16_t>::type_id()] = &ParseTimeSampleArrayValue_impl<uint16_t>;
  parsers[value::TypeTraits<value::ushort2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::ushort2>;
  parsers[value::TypeTraits<value::ushort3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::ushort3>;
  parsers[value::TypeTraits<value::ushort4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::ushort4>;
  // 64-bit integer types
  parsers[value::TypeTraits<int64_t>::type_id()] = &ParseTimeSampleArrayValue_impl<int64_t>;
  parsers[value::TypeTraits<uint64_t>::type_id()] = &ParseTimeSampleArrayValue_impl<uint64_t>;
  // Half precision types
  parsers[value::TypeTraits<value::half>::type_id()] = &ParseTimeSampleArrayValue_impl<value::half>;
  parsers[value::TypeTraits<value::half2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::half2>;
  parsers[value::TypeTraits<value::half3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::half3>;
  parsers[value::TypeTraits<value::half4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::half4>;
  // Float types
  parsers[value::TypeTraits<float>::type_id()] = &ParseTimeSampleArrayValue_impl<float>;
  parsers[value::TypeTraits<value::float2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::float2>;
  parsers[value::TypeTraits<value::float3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::float3>;
  parsers[value::TypeTraits<value::float4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::float4>;
  // Double types
  parsers[value::TypeTraits<double>::type_id()] = &ParseTimeSampleArrayValue_impl<double>;
  parsers[value::TypeTraits<value::double2>::type_id()] = &ParseTimeSampleArrayValue_impl<value::double2>;
  parsers[value::TypeTraits<value::double3>::type_id()] = &ParseTimeSampleArrayValue_impl<value::double3>;
  parsers[value::TypeTraits<value::double4>::type_id()] = &ParseTimeSampleArrayValue_impl<value::double4>;
  // Quaternion types
  parsers[value::TypeTraits<value::quath>::type_id()] = &ParseTimeSampleArrayValue_impl<value::quath>;
  parsers[value::TypeTraits<value::quatf>::type_id()] = &ParseTimeSampleArrayValue_impl<value::quatf>;
  parsers[value::TypeTraits<value::quatd>::type_id()] = &ParseTimeSampleArrayValue_impl<value::quatd>;
  // Color types (half)
  parsers[value::TypeTraits<value::color3h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::color3h>;
  parsers[value::TypeTraits<value::color4h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::color4h>;
  // Color types (float)
  parsers[value::TypeTraits<value::color3f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::color3f>;
  parsers[value::TypeTraits<value::color4f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::color4f>;
  // Color types (double)
  parsers[value::TypeTraits<value::color3d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::color3d>;
  parsers[value::TypeTraits<value::color4d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::color4d>;
  // Vector types
  parsers[value::TypeTraits<value::vector3h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::vector3h>;
  parsers[value::TypeTraits<value::vector3f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::vector3f>;
  parsers[value::TypeTraits<value::vector3d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::vector3d>;
  // Normal types
  parsers[value::TypeTraits<value::normal3h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::normal3h>;
  parsers[value::TypeTraits<value::normal3f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::normal3f>;
  parsers[value::TypeTraits<value::normal3d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::normal3d>;
  // Point types
  parsers[value::TypeTraits<value::point3h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::point3h>;
  parsers[value::TypeTraits<value::point3f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::point3f>;
  parsers[value::TypeTraits<value::point3d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::point3d>;
  // Texcoord types
  parsers[value::TypeTraits<value::texcoord2h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::texcoord2h>;
  parsers[value::TypeTraits<value::texcoord2f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::texcoord2f>;
  parsers[value::TypeTraits<value::texcoord2d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::texcoord2d>;
  parsers[value::TypeTraits<value::texcoord3h>::type_id()] = &ParseTimeSampleArrayValue_impl<value::texcoord3h>;
  parsers[value::TypeTraits<value::texcoord3f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::texcoord3f>;
  parsers[value::TypeTraits<value::texcoord3d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::texcoord3d>;
  // Matrix types (float)
  parsers[value::TypeTraits<value::matrix2f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::matrix2f>;
  parsers[value::TypeTraits<value::matrix3f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::matrix3f>;
  parsers[value::TypeTraits<value::matrix4f>::type_id()] = &ParseTimeSampleArrayValue_impl<value::matrix4f>;
  // Matrix types (double)
  parsers[value::TypeTraits<value::matrix2d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::matrix2d>;
  parsers[value::TypeTraits<value::matrix3d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::matrix3d>;
  parsers[value::TypeTraits<value::matrix4d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::matrix4d>;
  // Frame type
  parsers[value::TypeTraits<value::frame4d>::type_id()] = &ParseTimeSampleArrayValue_impl<value::frame4d>;
}

// ============================================================================
// Dedup Sample Adder Registry
// Replaces switch statement in add_array_sample_with_dedup with O(1) lookup
// ============================================================================

// Function pointer type for adding dedup array samples
using DedupArraySampleAdderFn = bool (*)(value::TimeSamples*, double, size_t, std::string*);

// Registry for dedup sample addition
struct DedupArraySampleAdderRegistry {
  std::map<uint32_t, DedupArraySampleAdderFn> adders;

  static const DedupArraySampleAdderRegistry& Instance() {
    // Use pointer to avoid exit-time destructor (intentional leak for static registry)
    static DedupArraySampleAdderRegistry* instance = new DedupArraySampleAdderRegistry();
    return *instance;
  }

  DedupArraySampleAdderRegistry();

  bool AddDedupSample(value::TimeSamples* ts, uint32_t elem_type_id, double time, size_t ref_index, std::string* err) const {
    auto it = adders.find(elem_type_id);
    if (it == adders.end()) {
      return false;  // Type not registered
    }
    return it->second(ts, time, ref_index, err);
  }

  bool HasAdder(uint32_t elem_type_id) const {
    return adders.find(elem_type_id) != adders.end();
  }
};

// Template helper for generating dedup adder functions
template<typename T>
static bool AddDedupArraySample_impl(value::TimeSamples* ts, double time, size_t ref_index, std::string* err) {
  return ts->add_dedup_array_sample_pod<T>(time, ref_index, err);
}

// Initialize all dedup adders
DedupArraySampleAdderRegistry::DedupArraySampleAdderRegistry() {
  adders[value::TYPE_ID_INT32] = &AddDedupArraySample_impl<int32_t>;
  adders[value::TYPE_ID_UINT32] = &AddDedupArraySample_impl<uint32_t>;
  adders[value::TYPE_ID_INT64] = &AddDedupArraySample_impl<int64_t>;
  adders[value::TYPE_ID_UINT64] = &AddDedupArraySample_impl<uint64_t>;
  adders[value::TYPE_ID_FLOAT] = &AddDedupArraySample_impl<float>;
  adders[value::TYPE_ID_DOUBLE] = &AddDedupArraySample_impl<double>;
  adders[value::TYPE_ID_FLOAT2] = &AddDedupArraySample_impl<value::float2>;
  adders[value::TYPE_ID_FLOAT3] = &AddDedupArraySample_impl<value::float3>;
  adders[value::TYPE_ID_FLOAT4] = &AddDedupArraySample_impl<value::float4>;
  adders[value::TYPE_ID_DOUBLE2] = &AddDedupArraySample_impl<value::double2>;
  adders[value::TYPE_ID_DOUBLE3] = &AddDedupArraySample_impl<value::double3>;
  adders[value::TYPE_ID_DOUBLE4] = &AddDedupArraySample_impl<value::double4>;
  // Matrix types
  adders[value::TYPE_ID_MATRIX2F] = &AddDedupArraySample_impl<value::matrix2f>;
  adders[value::TYPE_ID_MATRIX3F] = &AddDedupArraySample_impl<value::matrix3f>;
  adders[value::TYPE_ID_MATRIX4F] = &AddDedupArraySample_impl<value::matrix4f>;
  adders[value::TYPE_ID_MATRIX2D] = &AddDedupArraySample_impl<value::matrix2d>;
  adders[value::TYPE_ID_MATRIX3D] = &AddDedupArraySample_impl<value::matrix3d>;
  adders[value::TYPE_ID_MATRIX4D] = &AddDedupArraySample_impl<value::matrix4d>;
}

}  // anonymous namespace

//
// -- Deduplication support for array timesamples
//

// Compare two array values for exact equality
// Returns true if both values represent the same array content
// Helper function to check if a type is POD
// This is a local copy of the function from timesamples.cc
// TODO: Use this when we can properly dispatch to typed dedup methods
#ifdef __GNUC__
__attribute__((unused))
#endif
static bool IsPODType(uint32_t type_id) {
  // Extract the base type by masking off the array bit
  uint32_t base_type_id = type_id & (~value::TYPE_ID_1D_ARRAY_BIT);

  switch (base_type_id) {
    case value::TYPE_ID_BOOL:
    case value::TYPE_ID_INT32:
    case value::TYPE_ID_UINT32:
    case value::TYPE_ID_INT64:
    case value::TYPE_ID_UINT64:
    case value::TYPE_ID_HALF:
    case value::TYPE_ID_FLOAT:
    case value::TYPE_ID_DOUBLE:
    case value::TYPE_ID_INT2:
    case value::TYPE_ID_UINT2:
    case value::TYPE_ID_HALF2:
    case value::TYPE_ID_FLOAT2:
    case value::TYPE_ID_DOUBLE2:
    case value::TYPE_ID_INT3:
    case value::TYPE_ID_UINT3:
    case value::TYPE_ID_HALF3:
    case value::TYPE_ID_FLOAT3:
    case value::TYPE_ID_DOUBLE3:
    case value::TYPE_ID_INT4:
    case value::TYPE_ID_UINT4:
    case value::TYPE_ID_HALF4:
    case value::TYPE_ID_FLOAT4:
    case value::TYPE_ID_DOUBLE4:
    case value::TYPE_ID_QUATH:
    case value::TYPE_ID_QUATF:
    case value::TYPE_ID_QUATD:
    case value::TYPE_ID_COLOR3F:
    case value::TYPE_ID_COLOR3D:
    case value::TYPE_ID_COLOR4F:
    case value::TYPE_ID_COLOR4D:
    case value::TYPE_ID_POINT3H:
    case value::TYPE_ID_POINT3F:
    case value::TYPE_ID_POINT3D:
    case value::TYPE_ID_NORMAL3H:
    case value::TYPE_ID_NORMAL3F:
    case value::TYPE_ID_NORMAL3D:
    case value::TYPE_ID_VECTOR3H:
    case value::TYPE_ID_VECTOR3F:
    case value::TYPE_ID_VECTOR3D:
    case value::TYPE_ID_TEXCOORD2H:
    case value::TYPE_ID_TEXCOORD2F:
    case value::TYPE_ID_TEXCOORD2D:
    case value::TYPE_ID_TEXCOORD3H:
    case value::TYPE_ID_TEXCOORD3F:
    case value::TYPE_ID_TEXCOORD3D:
    case value::TYPE_ID_MATRIX2F:
    case value::TYPE_ID_MATRIX2D:
    case value::TYPE_ID_MATRIX3F:
    case value::TYPE_ID_MATRIX3D:
    case value::TYPE_ID_MATRIX4F:
    case value::TYPE_ID_MATRIX4D:
      return true;
    default:
      return false;
  }
}

// Compare two value::Value objects for array equality
// Used for deduplication detection in timesample arrays
static bool arrays_equal(const value::Value &a, const value::Value &b) {
  if (a.type_id() != b.type_id()) {
    return false;
  }

  if (!a.is_array() || !b.is_array()) {
    return false;
  }

  // Type-specific comparisons
#define COMPARE_ARRAY_TYPE(__type)                              \
  {                                                             \
    auto *vec_a = a.as<std::vector<__type>>();               \
    auto *vec_b = b.as<std::vector<__type>>();               \
    if (vec_a && vec_b) return *vec_a == *vec_b;              \
  }

  // Basic POD types with operator==
  COMPARE_ARRAY_TYPE(bool)
  // int8_t type is not directly supported in Value type system
  // COMPARE_ARRAY_TYPE(int8_t)
  COMPARE_ARRAY_TYPE(uint8_t)
  COMPARE_ARRAY_TYPE(int16_t)
  COMPARE_ARRAY_TYPE(uint16_t)
  COMPARE_ARRAY_TYPE(int32_t)
  COMPARE_ARRAY_TYPE(uint32_t)
  COMPARE_ARRAY_TYPE(int64_t)
  COMPARE_ARRAY_TYPE(uint64_t)
  COMPARE_ARRAY_TYPE(float)
  COMPARE_ARRAY_TYPE(double)

  // Vector types - these have operator== defined in value-types.hh
  COMPARE_ARRAY_TYPE(value::int2)
  COMPARE_ARRAY_TYPE(value::int3)
  COMPARE_ARRAY_TYPE(value::int4)
  COMPARE_ARRAY_TYPE(value::float2)
  COMPARE_ARRAY_TYPE(value::float3)
  COMPARE_ARRAY_TYPE(value::float4)
  COMPARE_ARRAY_TYPE(value::double2)
  COMPARE_ARRAY_TYPE(value::double3)
  COMPARE_ARRAY_TYPE(value::double4)

  // Half precision types - TODO: Add once operator== is defined
  // COMPARE_ARRAY_TYPE(value::half)
  // COMPARE_ARRAY_TYPE(value::half2)
  // COMPARE_ARRAY_TYPE(value::half3)
  // COMPARE_ARRAY_TYPE(value::half4)

  // Quaternion types - TODO: Add once operator== is defined
  // COMPARE_ARRAY_TYPE(value::quath)
  // COMPARE_ARRAY_TYPE(value::quatf)
  // COMPARE_ARRAY_TYPE(value::quatd)

  // Color types - TODO: Add once operator== is defined
  // COMPARE_ARRAY_TYPE(value::color3h)
  // COMPARE_ARRAY_TYPE(value::color3f)
  // COMPARE_ARRAY_TYPE(value::color3d)
  // COMPARE_ARRAY_TYPE(value::color4h)
  // COMPARE_ARRAY_TYPE(value::color4f)
  // COMPARE_ARRAY_TYPE(value::color4d)

  // Point/normal/vector types - TODO: Add once operator== is defined
  // COMPARE_ARRAY_TYPE(value::point3h)
  // COMPARE_ARRAY_TYPE(value::point3f)
  // COMPARE_ARRAY_TYPE(value::point3d)
  // COMPARE_ARRAY_TYPE(value::normal3h)
  // COMPARE_ARRAY_TYPE(value::normal3f)
  // COMPARE_ARRAY_TYPE(value::normal3d)
  // COMPARE_ARRAY_TYPE(value::vector3h)
  // COMPARE_ARRAY_TYPE(value::vector3f)
  // COMPARE_ARRAY_TYPE(value::vector3d)

  // Texcoord types - TODO: Add once operator== is defined
  // COMPARE_ARRAY_TYPE(value::texcoord2h)
  // COMPARE_ARRAY_TYPE(value::texcoord2f)
  // COMPARE_ARRAY_TYPE(value::texcoord2d)
  // COMPARE_ARRAY_TYPE(value::texcoord3h)
  // COMPARE_ARRAY_TYPE(value::texcoord3f)
  // COMPARE_ARRAY_TYPE(value::texcoord3d)

  // Matrix types - now trivial and support POD path with dedup
  COMPARE_ARRAY_TYPE(value::matrix2f)
  COMPARE_ARRAY_TYPE(value::matrix2d)
  COMPARE_ARRAY_TYPE(value::matrix3f)
  COMPARE_ARRAY_TYPE(value::matrix3d)
  COMPARE_ARRAY_TYPE(value::matrix4f)
  COMPARE_ARRAY_TYPE(value::matrix4d)

#undef COMPARE_ARRAY_TYPE

  return false;
}

extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<bool> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<int32_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<uint32_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<int64_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<uint64_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half2> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half3> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half4> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<float> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::float2> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::float3> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::float4> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<double> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::double2> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::double3> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::double4> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord2h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord2f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord2d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::point3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::point3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::point3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::normal3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::normal3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::normal3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::vector3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::vector3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::vector3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color4h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color4f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color4d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix2f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix4f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix2d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix4d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::quath> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::quatf> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::quatd> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::token> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::StringData> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<std::string> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<Reference> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<Path> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::AssetPath> *result);

//
// -- impl ParseTimeSampleData
//

bool AsciiParser::ParseTimeSampleValueOfArrayType(const uint32_t type_id, value::Value *result) {

  if (!result) {
    return false;
  }

  if (MaybeNone()) {
    (*result) = value::ValueBlock();
    return true;
  }

  value::Value val;

  // Use registry-based lookup instead of macro if-else chain
  const auto& registry = TimeSampleArrayValueParserRegistry::Instance();
  if (!registry.Parse(this, type_id, &val)) {
    PUSH_ERROR_AND_RETURN("Failed to parse value with requested type `" + value::GetTypeName(type_id) + "[]`");
  }

  (*result) = val;

  return true;

}

// `type_name` does not contain "[]"
bool AsciiParser::ParseTimeSampleValueOfArrayType(const std::string &type_name, value::Value *result) {
  nonstd::optional<uint32_t> type_id = value::TryGetTypeId(type_name);
  if (!type_id) {
    PUSH_ERROR_AND_RETURN("Unsupported/invalid type name: " + type_name);
  }

  return ParseTimeSampleValueOfArrayType(type_id.value(), result);
}

bool AsciiParser::ParseTimeSamplesOfArray(const std::string &type_name,
                                   value::TimeSamples *ts_out) {

  value::TimeSamples ts;

  // Initialize TimeSamples with the array type_id early to enable POD storage
  nonstd::optional<uint32_t> array_type_id = value::TryGetTypeId(type_name);
  if (array_type_id) {
    // Add the array bit to the type_id
    uint32_t full_type_id = array_type_id.value() | value::TYPE_ID_1D_ARRAY_BIT;
    ts.init(full_type_id);
    DCOUT("Initialized TimeSamples with array type_id: " << full_type_id << " for type: " << type_name << "[]");
  }

  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c == '}') {
      break;
    }

    Rewind(1);

    double timeVal;
    // -inf, inf and nan are handled.
    if (!ReadBasicType(&timeVal)) {
      PushError("Parse time value failed.");
      return false;
    }

    if (!SkipWhitespace()) {
      return false;
    }

    if (!Expect(':')) {
      return false;
    }

    if (!SkipWhitespace()) {
      return false;
    }

    value::Value value;
    if (!ParseTimeSampleValueOfArrayType(type_name, &value)) { // could be None(ValueBlock)
      return false;
    }

    // Helper lambda to check and add array sample with dedup
    auto add_array_sample_with_dedup = [&]() -> bool {
      if (value.is_array()) {
        // Check if this array value has been seen before in existing samples
        // We need to compare with all previous samples to find duplicates
        size_t ref_index = std::numeric_limits<size_t>::max();

        // Iterate through all samples added so far to find a match
        for (size_t i = 0; i < ts.size(); ++i) {
          // Get the value at sample index i
          auto existing_value = ts.get_value(i);
          if (existing_value && existing_value->is_array()) {
            // Compare the arrays using our arrays_equal function
            if (arrays_equal(value, *existing_value)) {
              ref_index = i;
              DCOUT("Array dedup (ASCII): found duplicate at index " << i << " for time " << timeVal);
              break;
            }
          }
        }

        // If we found a duplicate, use the dedup methods
        if (ref_index != std::numeric_limits<size_t>::max()) {
          DCOUT("Array dedup (ASCII): detected duplicate at index " << ref_index << " for time " << timeVal);

          // Use dedup storage optimization if TimeSamples is using POD storage
          if (ts.is_using_pod()) {
            // Get the array type ID to determine which typed dedup method to call
            uint32_t array_tid = value.type_id();
            uint32_t elem_tid = array_tid & (~value::TYPE_ID_1D_ARRAY_BIT);

            std::string err;
            bool dedup_added = false;

            // Use registry-based lookup instead of switch statement
            const auto& dedup_registry = DedupArraySampleAdderRegistry::Instance();
            if (dedup_registry.HasAdder(elem_tid)) {
              dedup_added = dedup_registry.AddDedupSample(&ts, elem_tid, timeVal, ref_index, &err);
            } else {
              // Type not supported for dedup optimization
              DCOUT("Array dedup (ASCII): unsupported type for POD dedup optimization, falling back to regular sample");
              ts.add_sample(timeVal, value, &err);
            }

            if (dedup_added) {
              DCOUT("Array dedup (ASCII): successfully added dedup reference for time " << timeVal);
            } else if (!err.empty()) {
              DCOUT("Array dedup (ASCII): failed to add dedup sample: " << err);
              // Fall back to regular sample on error
              ts.add_sample(timeVal, value, &err);
            }
          } else {
            // Non-POD storage or POD storage gets disabled by add_sample
            DCOUT("Array dedup (ASCII): falling back to regular sample (POD storage limitation)");
            std::string err;
            ts.add_sample(timeVal, value, &err);
          }
        } else {
          // No duplicate found, add as a regular sample
          DCOUT("Array dedup (ASCII): no duplicate found, adding new sample at index " << ts.size());
          std::string err;
          // Note: This will disable POD storage if it was enabled
          // TODO: Extract typed array data and use add_array_sample_pod directly
          ts.add_sample(timeVal, value, &err);
        }
      } else {
        // Not an array, just add normally
        ts.add_sample(timeVal, value);
      }
      return true;
    };

    // The last element may have separator ','
    {
      // Semicolon ';' is not allowed as a separator for timeSamples array
      // values.
      if (!SkipWhitespace()) {
        return false;
      }

      char sep{};
      if (!Char1(&sep)) {
        return false;
      }

      DCOUT("sep = " << sep);
      if (sep == '}') {
        // End of item
        if (!add_array_sample_with_dedup()) {
          return false;
        }
        break;
      } else if (sep == ',') {
        // ok - continue to next iteration
      } else {
        Rewind(1);

        // Look ahead Newline + '}'
        auto loc = CurrLoc();

        if (SkipWhitespaceAndNewline()) {
          char nc;
          if (!Char1(&nc)) {
            return false;
          }

          if (nc == '}') {
            // End of item
            if (!add_array_sample_with_dedup()) {
              return false;
            }
            break;
          }
        }

        // Rewind and continue parsing.
        SeekTo(loc);
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    // Add the sample with dedup check
    if (!add_array_sample_with_dedup()) {
      return false;
    }
  }

  DCOUT("Parse TimeSamples success. # of items = " << ts.size());

  if (ts_out) {
    (*ts_out) = std::move(ts);
  }

  return true;
}

}  // namespace ascii
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDA_READER

#endif  // TINYUSDZ_DISABLE_MODULE_USDA_READER
