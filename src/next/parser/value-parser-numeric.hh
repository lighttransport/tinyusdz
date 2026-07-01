// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - numeric helpers shared by value-parser translation units.

#pragma once

#include "../../external/fast_float/include/fast_float/fast_float.h"

#include <cstdint>
#include <string>
#include <system_error>

namespace tinyusdz {
namespace next {
namespace value_parser_detail {

template <class T>
inline bool FastFloatParse(const char* b, const char* e, T* out) {
  auto r = fast_float::from_chars(b, e, *out);
  if (r.ec == std::errc{} && r.ptr == e) return true;
  if (b < e && *b == '+') {
    r = fast_float::from_chars(b + 1, e, *out);
    if (r.ec == std::errc{} && r.ptr == e) return true;
  }
  return false;
}

template <class T>
inline T FastFloatParseToken(const std::string& s) {
  T v = T(0);
  FastFloatParse(s.data(), s.data() + s.size(), &v);
  return v;
}

inline int64_t DecimalToI64(const char* s) {
  if (!s) return 0;
  bool neg = false;
  if (*s == '+' || *s == '-') {
    neg = (*s == '-');
    ++s;
  }
  const uint64_t lim =
      neg ? (static_cast<uint64_t>(INT64_MAX) + 1u)
          : static_cast<uint64_t>(INT64_MAX);
  uint64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    const uint64_t d = static_cast<uint64_t>(*s - '0');
    if (v > (lim - d) / 10u) {
      v = lim;
      break;
    }
    v = v * 10u + d;
    ++s;
  }
  return neg ? static_cast<int64_t>(0u - v) : static_cast<int64_t>(v);
}

inline uint64_t DecimalToU64(const char* s) {
  if (!s) return 0;
  if (*s == '+') ++s;
  uint64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    const uint64_t d = static_cast<uint64_t>(*s - '0');
    if (v > (UINT64_MAX - d) / 10u) {
      v = UINT64_MAX;
      break;
    }
    v = v * 10u + d;
    ++s;
  }
  return v;
}

}  // namespace value_parser_detail
}  // namespace next
}  // namespace tinyusdz
