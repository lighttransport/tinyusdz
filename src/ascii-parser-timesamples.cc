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
#include "timesamples.hh"  // For PODTimeSamples
//
#include "common-macros.inc"

namespace tinyusdz {

namespace ascii {

namespace {

// ============================================================================
// TimeSample Value Parser Registry
// Replaces PARSE_TYPE macro if-else chain with O(1) lookup
// ============================================================================

// Function pointer type for timesample value parsers
using TimeSampleValueParserFn = bool (*)(AsciiParser*, value::Value*);

// Registry for ParseTimeSampleValue dispatch
struct TimeSampleValueParserRegistry {
  std::map<uint32_t, TimeSampleValueParserFn> parsers;

  static const TimeSampleValueParserRegistry& Instance() {
    // Use pointer to avoid exit-time destructor (intentional leak for static registry)
    static TimeSampleValueParserRegistry* instance = new TimeSampleValueParserRegistry();
    return *instance;
  }

  TimeSampleValueParserRegistry();

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

// Template helper for generating parser functions
template<typename T>
static bool ParseTimeSampleValue_impl(AsciiParser* parser, value::Value* result) {
  T typed_val;
  if (!parser->ReadBasicType(&typed_val)) {
    return false;
  }
  *result = value::Value(typed_val);
  return true;
}

// Initialize all parsers
TimeSampleValueParserRegistry::TimeSampleValueParserRegistry() {
  // Special types
  parsers[value::TypeTraits<value::AssetPath>::type_id()] = &ParseTimeSampleValue_impl<value::AssetPath>;
  parsers[value::TypeTraits<value::token>::type_id()] = &ParseTimeSampleValue_impl<value::token>;
  parsers[value::TypeTraits<std::string>::type_id()] = &ParseTimeSampleValue_impl<std::string>;
  // Boolean
  parsers[value::TypeTraits<bool>::type_id()] = &ParseTimeSampleValue_impl<bool>;
  // Int types
  parsers[value::TypeTraits<int32_t>::type_id()] = &ParseTimeSampleValue_impl<int32_t>;
  parsers[value::TypeTraits<value::int2>::type_id()] = &ParseTimeSampleValue_impl<value::int2>;
  parsers[value::TypeTraits<value::int3>::type_id()] = &ParseTimeSampleValue_impl<value::int3>;
  parsers[value::TypeTraits<value::int4>::type_id()] = &ParseTimeSampleValue_impl<value::int4>;
  // Unsigned int types
  parsers[value::TypeTraits<uint32_t>::type_id()] = &ParseTimeSampleValue_impl<uint32_t>;
  parsers[value::TypeTraits<value::uint2>::type_id()] = &ParseTimeSampleValue_impl<value::uint2>;
  parsers[value::TypeTraits<value::uint3>::type_id()] = &ParseTimeSampleValue_impl<value::uint3>;
  parsers[value::TypeTraits<value::uint4>::type_id()] = &ParseTimeSampleValue_impl<value::uint4>;
  // Char types
  parsers[value::TypeTraits<char>::type_id()] = &ParseTimeSampleValue_impl<char>;
  parsers[value::TypeTraits<value::char2>::type_id()] = &ParseTimeSampleValue_impl<value::char2>;
  parsers[value::TypeTraits<value::char3>::type_id()] = &ParseTimeSampleValue_impl<value::char3>;
  parsers[value::TypeTraits<value::char4>::type_id()] = &ParseTimeSampleValue_impl<value::char4>;
  // Uchar types
  parsers[value::TypeTraits<uint8_t>::type_id()] = &ParseTimeSampleValue_impl<uint8_t>;
  parsers[value::TypeTraits<value::uchar2>::type_id()] = &ParseTimeSampleValue_impl<value::uchar2>;
  parsers[value::TypeTraits<value::uchar3>::type_id()] = &ParseTimeSampleValue_impl<value::uchar3>;
  parsers[value::TypeTraits<value::uchar4>::type_id()] = &ParseTimeSampleValue_impl<value::uchar4>;
  // Short types
  parsers[value::TypeTraits<int16_t>::type_id()] = &ParseTimeSampleValue_impl<int16_t>;
  parsers[value::TypeTraits<value::short2>::type_id()] = &ParseTimeSampleValue_impl<value::short2>;
  parsers[value::TypeTraits<value::short3>::type_id()] = &ParseTimeSampleValue_impl<value::short3>;
  parsers[value::TypeTraits<value::short4>::type_id()] = &ParseTimeSampleValue_impl<value::short4>;
  // Ushort types
  parsers[value::TypeTraits<uint16_t>::type_id()] = &ParseTimeSampleValue_impl<uint16_t>;
  parsers[value::TypeTraits<value::ushort2>::type_id()] = &ParseTimeSampleValue_impl<value::ushort2>;
  parsers[value::TypeTraits<value::ushort3>::type_id()] = &ParseTimeSampleValue_impl<value::ushort3>;
  parsers[value::TypeTraits<value::ushort4>::type_id()] = &ParseTimeSampleValue_impl<value::ushort4>;
  // 64-bit integer types
  parsers[value::TypeTraits<int64_t>::type_id()] = &ParseTimeSampleValue_impl<int64_t>;
  parsers[value::TypeTraits<uint64_t>::type_id()] = &ParseTimeSampleValue_impl<uint64_t>;
  // Half precision types
  parsers[value::TypeTraits<value::half>::type_id()] = &ParseTimeSampleValue_impl<value::half>;
  parsers[value::TypeTraits<value::half2>::type_id()] = &ParseTimeSampleValue_impl<value::half2>;
  parsers[value::TypeTraits<value::half3>::type_id()] = &ParseTimeSampleValue_impl<value::half3>;
  parsers[value::TypeTraits<value::half4>::type_id()] = &ParseTimeSampleValue_impl<value::half4>;
  // Float types
  parsers[value::TypeTraits<float>::type_id()] = &ParseTimeSampleValue_impl<float>;
  parsers[value::TypeTraits<value::float2>::type_id()] = &ParseTimeSampleValue_impl<value::float2>;
  parsers[value::TypeTraits<value::float3>::type_id()] = &ParseTimeSampleValue_impl<value::float3>;
  parsers[value::TypeTraits<value::float4>::type_id()] = &ParseTimeSampleValue_impl<value::float4>;
  // Double types
  parsers[value::TypeTraits<double>::type_id()] = &ParseTimeSampleValue_impl<double>;
  parsers[value::TypeTraits<value::double2>::type_id()] = &ParseTimeSampleValue_impl<value::double2>;
  parsers[value::TypeTraits<value::double3>::type_id()] = &ParseTimeSampleValue_impl<value::double3>;
  parsers[value::TypeTraits<value::double4>::type_id()] = &ParseTimeSampleValue_impl<value::double4>;
  // Quaternion types
  parsers[value::TypeTraits<value::quath>::type_id()] = &ParseTimeSampleValue_impl<value::quath>;
  parsers[value::TypeTraits<value::quatf>::type_id()] = &ParseTimeSampleValue_impl<value::quatf>;
  parsers[value::TypeTraits<value::quatd>::type_id()] = &ParseTimeSampleValue_impl<value::quatd>;
  // Color types (half)
  parsers[value::TypeTraits<value::color3h>::type_id()] = &ParseTimeSampleValue_impl<value::color3h>;
  parsers[value::TypeTraits<value::color4h>::type_id()] = &ParseTimeSampleValue_impl<value::color4h>;
  // Color types (float)
  parsers[value::TypeTraits<value::color3f>::type_id()] = &ParseTimeSampleValue_impl<value::color3f>;
  parsers[value::TypeTraits<value::color4f>::type_id()] = &ParseTimeSampleValue_impl<value::color4f>;
  // Color types (double)
  parsers[value::TypeTraits<value::color3d>::type_id()] = &ParseTimeSampleValue_impl<value::color3d>;
  parsers[value::TypeTraits<value::color4d>::type_id()] = &ParseTimeSampleValue_impl<value::color4d>;
  // Vector types
  parsers[value::TypeTraits<value::vector3h>::type_id()] = &ParseTimeSampleValue_impl<value::vector3h>;
  parsers[value::TypeTraits<value::vector3f>::type_id()] = &ParseTimeSampleValue_impl<value::vector3f>;
  parsers[value::TypeTraits<value::vector3d>::type_id()] = &ParseTimeSampleValue_impl<value::vector3d>;
  // Normal types
  parsers[value::TypeTraits<value::normal3h>::type_id()] = &ParseTimeSampleValue_impl<value::normal3h>;
  parsers[value::TypeTraits<value::normal3f>::type_id()] = &ParseTimeSampleValue_impl<value::normal3f>;
  parsers[value::TypeTraits<value::normal3d>::type_id()] = &ParseTimeSampleValue_impl<value::normal3d>;
  // Point types
  parsers[value::TypeTraits<value::point3h>::type_id()] = &ParseTimeSampleValue_impl<value::point3h>;
  parsers[value::TypeTraits<value::point3f>::type_id()] = &ParseTimeSampleValue_impl<value::point3f>;
  parsers[value::TypeTraits<value::point3d>::type_id()] = &ParseTimeSampleValue_impl<value::point3d>;
  // Texcoord types
  parsers[value::TypeTraits<value::texcoord2h>::type_id()] = &ParseTimeSampleValue_impl<value::texcoord2h>;
  parsers[value::TypeTraits<value::texcoord2f>::type_id()] = &ParseTimeSampleValue_impl<value::texcoord2f>;
  parsers[value::TypeTraits<value::texcoord2d>::type_id()] = &ParseTimeSampleValue_impl<value::texcoord2d>;
  parsers[value::TypeTraits<value::texcoord3h>::type_id()] = &ParseTimeSampleValue_impl<value::texcoord3h>;
  parsers[value::TypeTraits<value::texcoord3f>::type_id()] = &ParseTimeSampleValue_impl<value::texcoord3f>;
  parsers[value::TypeTraits<value::texcoord3d>::type_id()] = &ParseTimeSampleValue_impl<value::texcoord3d>;
  // Matrix types (float)
  parsers[value::TypeTraits<value::matrix2f>::type_id()] = &ParseTimeSampleValue_impl<value::matrix2f>;
  parsers[value::TypeTraits<value::matrix3f>::type_id()] = &ParseTimeSampleValue_impl<value::matrix3f>;
  parsers[value::TypeTraits<value::matrix4f>::type_id()] = &ParseTimeSampleValue_impl<value::matrix4f>;
  // Matrix types (double)
  parsers[value::TypeTraits<value::matrix2d>::type_id()] = &ParseTimeSampleValue_impl<value::matrix2d>;
  parsers[value::TypeTraits<value::matrix3d>::type_id()] = &ParseTimeSampleValue_impl<value::matrix3d>;
  parsers[value::TypeTraits<value::matrix4d>::type_id()] = &ParseTimeSampleValue_impl<value::matrix4d>;
  // Frame type
  parsers[value::TypeTraits<value::frame4d>::type_id()] = &ParseTimeSampleValue_impl<value::frame4d>;
}

// ============================================================================
// POD TimeSamples Parser Registry
// Replaces TRY_POD_TYPE macro if-else chain with O(1) lookup
// ============================================================================

// Function pointer type for POD timesample parsers
using PODTimeSamplesParserFn = bool (*)(AsciiParser*, value::TimeSamples*);

// Registry for ParseTimeSamples POD type dispatch
struct PODTimeSamplesParserRegistry {
  std::map<uint32_t, PODTimeSamplesParserFn> parsers;

  static const PODTimeSamplesParserRegistry& Instance() {
    // Use pointer to avoid exit-time destructor (intentional leak for static registry)
    static PODTimeSamplesParserRegistry* instance = new PODTimeSamplesParserRegistry();
    return *instance;
  }

  PODTimeSamplesParserRegistry();

  bool TryParse(AsciiParser* parser, uint32_t type_id, value::TimeSamples* ts_out, uint64_t saved_cursor) const {
    auto it = parsers.find(type_id);
    if (it == parsers.end()) {
      return false;  // Type not registered, use fallback
    }
    if (it->second(parser, ts_out)) {
      return true;  // POD path succeeded
    }
    // POD path failed - restore cursor
    parser->SeekTo(saved_cursor);
    return false;
  }

  bool HasParser(uint32_t type_id) const {
    return parsers.find(type_id) != parsers.end();
  }
};

// Template helper for generating POD parser functions
template<typename T>
static bool ParseTypedTimeSamples_wrapper(AsciiParser* parser, value::TimeSamples* ts_out) {
  return parser->ParseTypedTimeSamples<T>(ts_out);
}

// Initialize all POD parsers
PODTimeSamplesParserRegistry::PODTimeSamplesParserRegistry() {
  parsers[value::TypeTraits<bool>::type_id()] = &ParseTypedTimeSamples_wrapper<bool>;
  parsers[value::TypeTraits<int32_t>::type_id()] = &ParseTypedTimeSamples_wrapper<int32_t>;
  parsers[value::TypeTraits<uint32_t>::type_id()] = &ParseTypedTimeSamples_wrapper<uint32_t>;
  parsers[value::TypeTraits<int64_t>::type_id()] = &ParseTypedTimeSamples_wrapper<int64_t>;
  parsers[value::TypeTraits<uint64_t>::type_id()] = &ParseTypedTimeSamples_wrapper<uint64_t>;
  parsers[value::TypeTraits<value::half>::type_id()] = &ParseTypedTimeSamples_wrapper<value::half>;
  parsers[value::TypeTraits<value::half2>::type_id()] = &ParseTypedTimeSamples_wrapper<value::half2>;
  parsers[value::TypeTraits<value::half3>::type_id()] = &ParseTypedTimeSamples_wrapper<value::half3>;
  parsers[value::TypeTraits<value::half4>::type_id()] = &ParseTypedTimeSamples_wrapper<value::half4>;
  parsers[value::TypeTraits<float>::type_id()] = &ParseTypedTimeSamples_wrapper<float>;
  parsers[value::TypeTraits<value::float2>::type_id()] = &ParseTypedTimeSamples_wrapper<value::float2>;
  parsers[value::TypeTraits<value::float3>::type_id()] = &ParseTypedTimeSamples_wrapper<value::float3>;
  parsers[value::TypeTraits<value::float4>::type_id()] = &ParseTypedTimeSamples_wrapper<value::float4>;
  parsers[value::TypeTraits<double>::type_id()] = &ParseTypedTimeSamples_wrapper<double>;
  parsers[value::TypeTraits<value::double2>::type_id()] = &ParseTypedTimeSamples_wrapper<value::double2>;
  parsers[value::TypeTraits<value::double3>::type_id()] = &ParseTypedTimeSamples_wrapper<value::double3>;
  parsers[value::TypeTraits<value::double4>::type_id()] = &ParseTypedTimeSamples_wrapper<value::double4>;
  parsers[value::TypeTraits<value::int2>::type_id()] = &ParseTypedTimeSamples_wrapper<value::int2>;
  parsers[value::TypeTraits<value::int3>::type_id()] = &ParseTypedTimeSamples_wrapper<value::int3>;
  parsers[value::TypeTraits<value::int4>::type_id()] = &ParseTypedTimeSamples_wrapper<value::int4>;
  parsers[value::TypeTraits<value::quath>::type_id()] = &ParseTypedTimeSamples_wrapper<value::quath>;
  parsers[value::TypeTraits<value::quatf>::type_id()] = &ParseTypedTimeSamples_wrapper<value::quatf>;
  parsers[value::TypeTraits<value::quatd>::type_id()] = &ParseTypedTimeSamples_wrapper<value::quatd>;
  parsers[value::TypeTraits<value::color3f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::color3f>;
  parsers[value::TypeTraits<value::color4f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::color4f>;
  parsers[value::TypeTraits<value::color3d>::type_id()] = &ParseTypedTimeSamples_wrapper<value::color3d>;
  parsers[value::TypeTraits<value::color4d>::type_id()] = &ParseTypedTimeSamples_wrapper<value::color4d>;
  parsers[value::TypeTraits<value::vector3f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::vector3f>;
  parsers[value::TypeTraits<value::normal3f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::normal3f>;
  parsers[value::TypeTraits<value::point3f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::point3f>;
  parsers[value::TypeTraits<value::texcoord2f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::texcoord2f>;
  parsers[value::TypeTraits<value::texcoord3f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::texcoord3f>;
  // Matrix types
  parsers[value::TypeTraits<value::matrix2f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::matrix2f>;
  parsers[value::TypeTraits<value::matrix3f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::matrix3f>;
  parsers[value::TypeTraits<value::matrix4f>::type_id()] = &ParseTypedTimeSamples_wrapper<value::matrix4f>;
  parsers[value::TypeTraits<value::matrix2d>::type_id()] = &ParseTypedTimeSamples_wrapper<value::matrix2d>;
  parsers[value::TypeTraits<value::matrix3d>::type_id()] = &ParseTypedTimeSamples_wrapper<value::matrix3d>;
  parsers[value::TypeTraits<value::matrix4d>::type_id()] = &ParseTypedTimeSamples_wrapper<value::matrix4d>;
}

}  // anonymous namespace

// Templated function to parse typed TimeSamples for POD types
template<typename T>
bool AsciiParser::ParseTypedTimeSamples(value::TimeSamples *ts_out) {
  // Check if T is POD
  if (!(std::is_trivial<T>::value && std::is_standard_layout<T>::value)) {
    // Non-POD type - return false to use fallback
    return false;
  }
  if (!ts_out) {
    return false;
  }

  // Try to initialize with POD storage
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
      if (!ts_out->add_pod_blocked_sample<T>(timeVal, &err)) {
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
      if (!ts_out->add_pod_sample(timeVal, typed_val, &err)) {
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

  // Use registry-based lookup instead of macro if-else chain
  const auto& registry = TimeSampleValueParserRegistry::Instance();
  if (!registry.Parse(this, type_id, &val)) {
    PUSH_ERROR_AND_RETURN("Failed to parse value with requested type `" + value::GetTypeName(type_id) + "`");
  }

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

  // Get type_id to check if it's a POD type
  nonstd::optional<uint32_t> type_id = value::TryGetTypeId(type_name);
  if (!type_id) {
    PUSH_ERROR_AND_RETURN("Unknown type for timeSamples: " + type_name);
  }

  // Clear ts_out to ensure clean state before parsing
  // This prevents issues where init() fails if ts_out was partially initialized
  if (ts_out) {
    ts_out->clear();
  }

  // Try optimized path for POD types first using registry-based lookup
  // IMPORTANT: Save cursor position BEFORE attempting POD path
  // The POD path will consume the '{' if it tries to parse,
  // but we need to restore position for the generic fallback path
  uint64_t saved_cursor = CurrLoc();

  const auto& pod_registry = PODTimeSamplesParserRegistry::Instance();
  if (pod_registry.TryParse(this, type_id.value(), ts_out, saved_cursor)) {
    return true;
  }

  // Fall back to generic value::Value-based parsing for non-POD types
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

// Explicit template instantiations for POD types
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
