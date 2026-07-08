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

// Strict decimal-integer token check: pxr rejects `int x = 1e3`, hex, and a
// sign that doesn't match the type — DecimalTo* would silently stop at the
// first non-digit instead.
inline bool IsDecimalIntToken(const std::string& s, bool allow_neg) {
  size_t i = 0;
  if (i < s.size() && (s[i] == '+' || (allow_neg && s[i] == '-'))) ++i;
  if (i >= s.size()) return false;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
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

// 32-bit forms SATURATE at the type range (matching the 64-bit helpers'
// saturation) instead of silently truncating bits: `int i = 1e23` must not
// come back as -1.
inline int32_t DecimalToI32(const char* s) {
  const int64_t v = DecimalToI64(s);
  if (v > INT32_MAX) return INT32_MAX;
  if (v < INT32_MIN) return INT32_MIN;
  return static_cast<int32_t>(v);
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

inline uint32_t DecimalToU32(const char* s) {
  const uint64_t v = DecimalToU64(s);
  return v > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(v);
}

}  // namespace value_parser_detail
}  // namespace next
}  // namespace tinyusdz
