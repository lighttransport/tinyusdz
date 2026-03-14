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

#include "ascii-parser.hh"
#include "str-util.hh"
#include "tiny-format.hh"
//
#include "io-util.hh"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "timesamples.hh"  // For unified binary storage
//
#include "common-macros.inc"

namespace tinyusdz {

namespace ascii {

// Templated function to parse typed TimeSamples for binary-serializable types.
template<typename T>
bool AsciiParser::ParseTypedTimeSamples(value::TimeSamples *ts_out) {
  if (!value::IsBinarySerializableType(value::TypeTraits<T>::type_id())) {
    return false;
  }
  if (!ts_out) {
    return false;
  }

  // Try to initialize with unified binary storage.
  if (!ts_out->init(value::TypeTraits<T>::type_id())) {
    // Already initialized with different type
    return false;
  }

  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  // Phase 3: Use TimeSamples methods directly
  // Note: Scalar dedup not yet implemented in TimeSamples - only arrays support dedup

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

    // Check for None/ValueBlock
    if (MaybeNone()) {
      std::string err;
      if (!ts_out->add_blocked_sample<T>(timeVal, &err)) {
        if (!err.empty()) {
          PushError(err);
        }
        return false;
      }
    } else {
      // Parse typed value directly
      T typed_val;
      if (!ReadBasicType(&typed_val)) {
        PushError("Failed to parse value of type " + std::string(value::TypeTraits<T>::type_name()));
        return false;
      }

      std::string err;
      if (!ts_out->add_sample(timeVal, typed_val, &err)) {
        if (!err.empty()) {
          PushError(err);
        }
        return false;
      }
    }

    // Handle separator
    {
      if (!SkipWhitespace()) {
        return false;
      }

      char sep{};
      if (!Char1(&sep)) {
        return false;
      }

      if (sep == '}') {
        break;
      } else if (sep == ',') {
        // ok
      } else {
        Rewind(1);

        auto loc = CurrLoc();
        if (SkipWhitespaceAndNewline()) {
          char nc;
          if (!Char1(&nc)) {
            return false;
          }

          if (nc == '}') {
            break;
          }
        }

        SeekTo(loc);
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }
  }

  return true;
}

bool AsciiParser::ParseTimeSampleValue(const uint32_t type_id, value::Value *result) {

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
    __type typed_val; \
    if (!ReadBasicType(&typed_val)) {                             \
      PUSH_ERROR_AND_RETURN("Failed to parse value with requested type `" + value::GetTypeName(__tyid) + "`"); \
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

bool AsciiParser::ParseTimeSampleValue(const std::string &type_name, value::Value *result) {

  nonstd::optional<uint32_t> type_id = value::TryGetTypeId(type_name);

  if (!type_id) {
    PUSH_ERROR_AND_RETURN("Unsupported/invalid timeSamples type " + type_name);
  }

  return ParseTimeSampleValue(type_id.value(), result);
}


bool AsciiParser::ParseTimeSamples(const std::string &type_name,
                                   value::TimeSamples *ts_out) {

  // Get type_id to check whether unified binary storage can be used.
  nonstd::optional<uint32_t> type_id = value::TryGetTypeId(type_name);
  if (!type_id) {
    PUSH_ERROR_AND_RETURN("Unknown type for timeSamples: " + type_name);
  }

  // Clear ts_out to ensure clean state before parsing
  // This prevents issues where init() fails if ts_out was partially initialized
  if (ts_out) {
    ts_out->clear();
  }

  // Try optimized binary-serializable parsing first.
  // IMPORTANT: Save cursor position BEFORE attempting binary-storage path
  // The typed path will consume the '{' if it tries to parse,
  // but we need to restore position for the generic fallback path
  uint64_t saved_cursor = CurrLoc();
#define TRY_BINARY_TYPE(__type)                                     \
  if (type_id.value() == value::TypeTraits<__type>::type_id()) {   \
    if (ParseTypedTimeSamples<__type>(ts_out)) {                    \
      return true;                                                  \
    }                                                                \
    /* typed path failed - restore cursor to original position */    \
    /* so the generic fallback can parse from the beginning */ \
    SeekTo(saved_cursor);                                           \
  }

  // Try binary-serializable numeric, role, quaternion, and matrix types.
  TRY_BINARY_TYPE(int32_t)
  TRY_BINARY_TYPE(uint32_t)
  TRY_BINARY_TYPE(int64_t)
  TRY_BINARY_TYPE(uint64_t)
  TRY_BINARY_TYPE(value::half)
  TRY_BINARY_TYPE(value::half2)
  TRY_BINARY_TYPE(value::half3)
  TRY_BINARY_TYPE(value::half4)
  TRY_BINARY_TYPE(float)
  TRY_BINARY_TYPE(value::float2)
  TRY_BINARY_TYPE(value::float3)
  TRY_BINARY_TYPE(value::float4)
  TRY_BINARY_TYPE(double)
  TRY_BINARY_TYPE(value::double2)
  TRY_BINARY_TYPE(value::double3)
  TRY_BINARY_TYPE(value::double4)
  TRY_BINARY_TYPE(value::int2)
  TRY_BINARY_TYPE(value::int3)
  TRY_BINARY_TYPE(value::int4)
  TRY_BINARY_TYPE(value::quath)
  TRY_BINARY_TYPE(value::quatf)
  TRY_BINARY_TYPE(value::quatd)
  TRY_BINARY_TYPE(value::color3f)
  TRY_BINARY_TYPE(value::color4f)
  TRY_BINARY_TYPE(value::color3d)
  TRY_BINARY_TYPE(value::color4d)
  TRY_BINARY_TYPE(value::vector3f)
  TRY_BINARY_TYPE(value::normal3f)
  TRY_BINARY_TYPE(value::point3f)
  TRY_BINARY_TYPE(value::texcoord2f)
  TRY_BINARY_TYPE(value::texcoord3f)
  TRY_BINARY_TYPE(value::matrix2f)
  TRY_BINARY_TYPE(value::matrix3f)
  TRY_BINARY_TYPE(value::matrix4f)
  TRY_BINARY_TYPE(value::matrix2d)
  TRY_BINARY_TYPE(value::matrix3d)
  TRY_BINARY_TYPE(value::matrix4d)

#undef TRY_BINARY_TYPE

  // Fall back to generic value::Value-based parsing for other types.
  // (strings, tokens, paths, arrays, etc.)
  value::TimeSamples ts;

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
    if (!ParseTimeSampleValue(type_name, &value)) { // could be None(ValueBlock)
      return false;
    }

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
        ts.add_sample(timeVal, value);
        break;
      } else if (sep == ',') {
        // ok
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
            ts.add_sample(timeVal, value);
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

    ts.add_sample(timeVal, value);
  }

  DCOUT("Parse TimeSamples success. # of items = " << ts.size());

  if (ts_out) {
    (*ts_out) = std::move(ts);
  }

  return true;
}

// Explicit template instantiations for binary-serializable types
template bool AsciiParser::ParseTypedTimeSamples<bool>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<int32_t>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<uint32_t>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<int64_t>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<uint64_t>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::half>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::half2>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::half3>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::half4>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<float>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::float2>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::float3>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::float4>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<double>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::double2>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::double3>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::double4>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::int2>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::int3>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::int4>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::quath>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::quatf>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::quatd>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::color3f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::color4f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::color3d>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::color4d>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::vector3f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::normal3f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::point3f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::texcoord2f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::texcoord3f>(value::TimeSamples*);
// Matrix types - now trivial with default constructors
template bool AsciiParser::ParseTypedTimeSamples<value::matrix2f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::matrix3f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::matrix4f>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::matrix2d>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::matrix3d>(value::TimeSamples*);
template bool AsciiParser::ParseTypedTimeSamples<value::matrix4d>(value::TimeSamples*);

}  // namespace ascii
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDA_READER

#endif  // TINYUSDZ_DISABLE_MODULE_USDA_READER
