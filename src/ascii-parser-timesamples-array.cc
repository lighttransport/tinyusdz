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

#define PARSE_TYPE(__tyid, __type)                       \
  if (__tyid == value::TypeTraits<__type>::type_id()) {             \
    std::vector<__type> typed_val; \
    if (!ParseBasicTypeArray(&typed_val)) {                             \
      PUSH_ERROR_AND_RETURN("Failed to parse value with requested type `" + value::GetTypeName(__tyid) + "[]`"); \
    }                                                                  \
    val = value::Value(typed_val); \
  } else

  // NOTE: `string` does not support multi-line string.
  PARSE_TYPE(type_id, value::AssetPath)
  PARSE_TYPE(type_id, value::token)
  PARSE_TYPE(type_id, std::string)
  // Boolean
  PARSE_TYPE(type_id, bool)
  // Int types
  PARSE_TYPE(type_id, int32_t)
  PARSE_TYPE(type_id, value::int2)
  PARSE_TYPE(type_id, value::int3)
  PARSE_TYPE(type_id, value::int4)
  // Unsigned int types
  PARSE_TYPE(type_id, uint32_t)
  PARSE_TYPE(type_id, value::uint2)
  PARSE_TYPE(type_id, value::uint3)
  PARSE_TYPE(type_id, value::uint4)
  // Char types (int8_t)
  PARSE_TYPE(type_id, char)
  PARSE_TYPE(type_id, value::char2)
  PARSE_TYPE(type_id, value::char3)
  PARSE_TYPE(type_id, value::char4)
  // Uchar types (uint8_t)
  PARSE_TYPE(type_id, uint8_t)
  PARSE_TYPE(type_id, value::uchar2)
  PARSE_TYPE(type_id, value::uchar3)
  PARSE_TYPE(type_id, value::uchar4)
  // Short types (int16_t)
  PARSE_TYPE(type_id, int16_t)
  PARSE_TYPE(type_id, value::short2)
  PARSE_TYPE(type_id, value::short3)
  PARSE_TYPE(type_id, value::short4)
  // Ushort types (uint16_t)
  PARSE_TYPE(type_id, uint16_t)
  PARSE_TYPE(type_id, value::ushort2)
  PARSE_TYPE(type_id, value::ushort3)
  PARSE_TYPE(type_id, value::ushort4)
  // 64-bit integer types
  PARSE_TYPE(type_id, int64_t)
  PARSE_TYPE(type_id, uint64_t)
  // Half precision types
  PARSE_TYPE(type_id, value::half)
  PARSE_TYPE(type_id, value::half2)
  PARSE_TYPE(type_id, value::half3)
  PARSE_TYPE(type_id, value::half4)
  // Float types
  PARSE_TYPE(type_id, float)
  PARSE_TYPE(type_id, value::float2)
  PARSE_TYPE(type_id, value::float3)
  PARSE_TYPE(type_id, value::float4)
  // Double types
  PARSE_TYPE(type_id, double)
  PARSE_TYPE(type_id, value::double2)
  PARSE_TYPE(type_id, value::double3)
  PARSE_TYPE(type_id, value::double4)
  // Quaternion types
  PARSE_TYPE(type_id, value::quath)
  PARSE_TYPE(type_id, value::quatf)
  PARSE_TYPE(type_id, value::quatd)
  // Color types (half precision)
  PARSE_TYPE(type_id, value::color3h)
  PARSE_TYPE(type_id, value::color4h)
  // Color types (float precision)
  PARSE_TYPE(type_id, value::color3f)
  PARSE_TYPE(type_id, value::color4f)
  // Color types (double precision)
  PARSE_TYPE(type_id, value::color3d)
  PARSE_TYPE(type_id, value::color4d)
  // Vector types
  PARSE_TYPE(type_id, value::vector3h)
  PARSE_TYPE(type_id, value::vector3f)
  PARSE_TYPE(type_id, value::vector3d)
  // Normal types
  PARSE_TYPE(type_id, value::normal3h)
  PARSE_TYPE(type_id, value::normal3f)
  PARSE_TYPE(type_id, value::normal3d)
  // Point types
  PARSE_TYPE(type_id, value::point3h)
  PARSE_TYPE(type_id, value::point3f)
  PARSE_TYPE(type_id, value::point3d)
  // Texcoord types
  PARSE_TYPE(type_id, value::texcoord2h)
  PARSE_TYPE(type_id, value::texcoord2f)
  PARSE_TYPE(type_id, value::texcoord2d)
  PARSE_TYPE(type_id, value::texcoord3h)
  PARSE_TYPE(type_id, value::texcoord3f)
  PARSE_TYPE(type_id, value::texcoord3d)
  // Matrix types (float)
  PARSE_TYPE(type_id, value::matrix2f)
  PARSE_TYPE(type_id, value::matrix3f)
  PARSE_TYPE(type_id, value::matrix4f)
  // Matrix types (double)
  PARSE_TYPE(type_id, value::matrix2d)
  PARSE_TYPE(type_id, value::matrix3d)
  PARSE_TYPE(type_id, value::matrix4d)
  // Frame type (same as matrix4d)
  PARSE_TYPE(type_id, value::frame4d) {
    PUSH_ERROR_AND_RETURN(" : TODO: timeSamples type " + value::GetTypeName(type_id));
  }

#undef PARSE_TYPE

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

            // Call the appropriate typed dedup method based on element type
            switch (elem_tid) {
              case value::TYPE_ID_INT32:
                dedup_added = ts.add_dedup_array_sample_pod<int32_t>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_UINT32:
                dedup_added = ts.add_dedup_array_sample_pod<uint32_t>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_INT64:
                dedup_added = ts.add_dedup_array_sample_pod<int64_t>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_UINT64:
                dedup_added = ts.add_dedup_array_sample_pod<uint64_t>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_FLOAT:
                dedup_added = ts.add_dedup_array_sample_pod<float>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_DOUBLE:
                dedup_added = ts.add_dedup_array_sample_pod<double>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_FLOAT2:
                dedup_added = ts.add_dedup_array_sample_pod<value::float2>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_FLOAT3:
                dedup_added = ts.add_dedup_array_sample_pod<value::float3>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_FLOAT4:
                dedup_added = ts.add_dedup_array_sample_pod<value::float4>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_DOUBLE2:
                dedup_added = ts.add_dedup_array_sample_pod<value::double2>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_DOUBLE3:
                dedup_added = ts.add_dedup_array_sample_pod<value::double3>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_DOUBLE4:
                dedup_added = ts.add_dedup_array_sample_pod<value::double4>(timeVal, ref_index, &err);
                break;
              // Matrix types - now trivial with default constructors and have operator==
              case value::TYPE_ID_MATRIX2F:
                dedup_added = ts.add_dedup_array_sample_pod<value::matrix2f>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_MATRIX3F:
                dedup_added = ts.add_dedup_array_sample_pod<value::matrix3f>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_MATRIX4F:
                dedup_added = ts.add_dedup_array_sample_pod<value::matrix4f>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_MATRIX2D:
                dedup_added = ts.add_dedup_array_sample_pod<value::matrix2d>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_MATRIX3D:
                dedup_added = ts.add_dedup_array_sample_pod<value::matrix3d>(timeVal, ref_index, &err);
                break;
              case value::TYPE_ID_MATRIX4D:
                dedup_added = ts.add_dedup_array_sample_pod<value::matrix4d>(timeVal, ref_index, &err);
                break;
              // Note: Other types like half, quaternions, colors etc. would need operator==
              // to be properly supported in arrays_equal comparison first
              default:
                DCOUT("Array dedup (ASCII): unsupported type for POD dedup optimization, falling back to regular sample");
                ts.add_sample(timeVal, value, &err);
                break;
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
