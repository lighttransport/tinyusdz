// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//

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

#include "external/fast_float/include/fast_float/fast_float.h"
#include "external/jsteemann/atoi.h"
#include "external/simple_match/include/simple_match/simple_match.hpp"
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

#include "io-util.hh"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"

#if 0
// s = std::string
#define PUSH_ERROR_AND_RETURN(s)                                     \
  do {                                                               \
    std::ostringstream ss_e;                                         \
    ss_e << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss_e << s;                                                       \
    PushError(ss_e.str());                                           \
    return false;                                                    \
  } while (0)

#define PUSH_WARN(s)                                                 \
  do {                                                               \
    std::ostringstream ss_w;                                         \
    ss_w << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss_w << s;                                                       \
    PushWarn(ss_w.str());                                            \
  } while (0)
#endif

#include "common-macros.inc"

namespace tinyusdz {

namespace ascii {

//constexpr auto kAscii = "[ASCII]";


//
// -- impl ParseTimeSampleData
//

// TODO: Share code with array version as much as possible.
template <typename T>
nonstd::optional<AsciiParser::TimeSampleData<T>>
AsciiParser::TryParseTimeSamples() {
  // timeSamples = '{' ((int : T) sep)+ '}'
  // sep = ','(may ok to omit for the last element.

  TimeSampleData<T> data;

  if (!Expect('{')) {
    return nonstd::nullopt;
  }

  if (!SkipWhitespaceAndNewline()) {
    return nonstd::nullopt;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return nonstd::nullopt;
    }

    if (c == '}') {
      break;
    }

    Rewind(1);

    double timeVal;
    // -inf, inf and nan are handled.
    if (!ReadBasicType(&timeVal)) {
      PushError("Parse time value failed.");
      return nonstd::nullopt;
    }

    if (!SkipWhitespace()) {
      return nonstd::nullopt;
    }

    if (!Expect(':')) {
      return nonstd::nullopt;
    }

    if (!SkipWhitespace()) {
      return nonstd::nullopt;
    }

    nonstd::optional<T> value;
    if (!ReadBasicType(&value)) {
      return nonstd::nullopt;
    }

    // The last element may have separator ','
    {
      // Semicolon ';' is not allowed as a separator for timeSamples array
      // values.
      if (!SkipWhitespace()) {
        return nonstd::nullopt;
      }

      char sep;
      if (!Char1(&sep)) {
        return nonstd::nullopt;
      }

      DCOUT("sep = " << sep);
      if (sep == '}') {
        // End of item
        data.push_back({timeVal, value});
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
            return nonstd::nullopt;
          }

          if (nc == '}') {
            // End of item
            data.push_back({timeVal, value});
            break;
          }
        }

        // Rewind and continue parsing.
        SeekTo(loc);
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return nonstd::nullopt;
    }

    data.push_back({timeVal, value});
  }

  DCOUT("Parse TimeSamples success. # of items = " << data.size());

  return std::move(data);
}

// TODO: Share code with array version as much as possible.
template <typename T>
nonstd::optional<AsciiParser::TimeSampleData<std::vector<T>>>
AsciiParser::TryParseTimeSamplesOfArray() {
  // timeSamples = '{' ((int : [T]) sep)+ '}'
  // sep = ','(may ok to omit for the last element.

  TimeSampleData<std::vector<T>> data;

  if (!Expect('{')) {
    return nonstd::nullopt;
  }

  if (!SkipWhitespaceAndNewline()) {
    return nonstd::nullopt;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return nonstd::nullopt;
    }

    if (c == '}') {
      break;
    }

    Rewind(1);

    double timeVal;
    // -inf, inf and nan are handled.
    if (!ReadBasicType(&timeVal)) {
      PushError("Parse time value failed.");
      return nonstd::nullopt;
    }

    if (!SkipWhitespace()) {
      return nonstd::nullopt;
    }

    if (!Expect(':')) {
      return nonstd::nullopt;
    }

    if (!SkipWhitespace()) {
      return nonstd::nullopt;
    }

    // None(nullopt) or T[]
    nonstd::optional<std::vector<T>> tsValue;

    if (MaybeNone()) {
      tsValue = nonstd::nullopt;
    } else {
      std::vector<T> value;
      if (!ParseBasicTypeArray(&value)) {
        PushError("Failed to parse array value.");
        return nonstd::nullopt;
      }
      tsValue = value;
    }

    // The last element may have separator ','
    {
      // Semicolon ';' is not allowed as a separator for timeSamples array
      // values.
      if (!SkipWhitespace()) {
        return nonstd::nullopt;
      }

      char sep;
      if (!Char1(&sep)) {
        return nonstd::nullopt;
      }

      DCOUT("sep = " << sep);
      if (sep == '}') {
        // End of item
        data.push_back({timeVal, tsValue});
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
            return nonstd::nullopt;
          }

          if (nc == '}') {
            // End of item
            data.push_back({timeVal, tsValue});
            break;
          }
        }

        // Rewind and continue parsing.
        SeekTo(loc);
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return nonstd::nullopt;
    }

    data.push_back({timeVal, tsValue});
  }

  DCOUT(
      "Parse TimeSamples of array type success. # of items = " << data.size());

  return std::move(data);
}

template <typename T>
value::TimeSamples AsciiParser::ConvertToTimeSamples(
    const TimeSampleData<T> &ts) {
  value::TimeSamples dst;

  for (const auto &item : ts) {
    dst.times.push_back(std::get<0>(item));

    if (item.second) {
      dst.values.push_back(item.second.value());
    } else {
      // Blocked.
      dst.values.push_back(value::ValueBlock());
    }
  }

  return dst;
}

template <typename T>
value::TimeSamples AsciiParser::ConvertToTimeSamples(
    const TimeSampleData<std::vector<T>> &ts) {
  value::TimeSamples dst;

  for (const auto &item : ts) {
    dst.times.push_back(std::get<0>(item));

    if (item.second) {
      dst.values.push_back(item.second.value());
    } else {
      // Blocked.
      dst.values.push_back(value::ValueBlock());
    }
  }

  return dst;
}

}  // namespace ascii
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDA_READER

#endif  // TINYUSDZ_DISABLE_MODULE_USDA_READER
