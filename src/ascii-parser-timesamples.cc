// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
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
//
#include "common-macros.inc"

namespace tinyusdz {

namespace ascii {

bool AsciiParser::ParseTimeSampleValue(const std::string &type_name, value::Value *result) {

  if (!result) {
    return false;
  }

  if (MaybeNone()) {
    (*result) = value::ValueBlock();
    return true;     
  }

  value::Value val;

#define PARSE_TYPE(__tyname, __type)                       \
  if (__tyname == value::TypeTraits<__type>::type_name()) {             \
    __type typed_val; \
    if (!ReadBasicType(&typed_val)) {                             \
      PUSH_ERROR_AND_RETURN("Failed to parse value with requested type `" + __tyname + "`"); \
    }                                                                  \
    val = value::Value(typed_val); \
  } else

  // NOTE: `string` does not support multi-line string.
  PARSE_TYPE(type_name, value::AssetPath)
  PARSE_TYPE(type_name, value::token)
  PARSE_TYPE(type_name, std::string)
  PARSE_TYPE(type_name, float)
  PARSE_TYPE(type_name, int32_t)
  PARSE_TYPE(type_name, uint32_t)
  PARSE_TYPE(type_name, int64_t)
  PARSE_TYPE(type_name, uint64_t)
  PARSE_TYPE(type_name, value::half)
  PARSE_TYPE(type_name, value::half2)
  PARSE_TYPE(type_name, value::half3)
  PARSE_TYPE(type_name, value::half4)
  PARSE_TYPE(type_name, float)
  PARSE_TYPE(type_name, value::float2)
  PARSE_TYPE(type_name, value::float3)
  PARSE_TYPE(type_name, value::float4)
  PARSE_TYPE(type_name, double)
  PARSE_TYPE(type_name, value::double2)
  PARSE_TYPE(type_name, value::double3)
  PARSE_TYPE(type_name, value::double4)
  PARSE_TYPE(type_name, value::quath)
  PARSE_TYPE(type_name, value::quatf)
  PARSE_TYPE(type_name, value::quatd)
  PARSE_TYPE(type_name, value::color3f)
  PARSE_TYPE(type_name, value::color4f)
  PARSE_TYPE(type_name, value::color3d)
  PARSE_TYPE(type_name, value::color4d)
  PARSE_TYPE(type_name, value::vector3f)
  PARSE_TYPE(type_name, value::normal3f)
  PARSE_TYPE(type_name, value::point3f)
  PARSE_TYPE(type_name, value::texcoord2f)
  PARSE_TYPE(type_name, value::texcoord3f)
  PARSE_TYPE(type_name, value::matrix4d) {
    PUSH_ERROR_AND_RETURN(" : TODO: timeSamples type " + type_name);
  }

#undef PARSE_TYPE

  (*result) = val;

  return true;
}


bool AsciiParser::ParseTimeSamples(const std::string &type_name,
                                   value::TimeSamples *ts_out) {

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
        ts.times.push_back(timeVal);
        ts.values.push_back(value);
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
            ts.times.push_back(timeVal);
            ts.values.push_back(value);
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

    ts.times.push_back(timeVal);
    ts.values.push_back(value);
  }

  DCOUT("Parse TimeSamples success. # of items = " << ts.times.size());

  if (ts_out) {
    (*ts_out) = std::move(ts);
  }

  return true;
}

}  // namespace ascii
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDA_READER

#endif  // TINYUSDZ_DISABLE_MODULE_USDA_READER
