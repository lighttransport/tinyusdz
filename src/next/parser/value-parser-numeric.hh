// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - numeric helpers shared by value-parser translation units.

#pragma once

#include "../../external/fast_float/include/fast_float/fast_float.h"

#include <cmath>
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

// pxr's usda parser COERCES float literals to integer types, truncating
// toward zero: `int a = 3.12` -> 3, `int b = 3.12e+1` -> 31, `uint u = -0.5`
// -> 0. A value out of the target range AFTER truncation is a parse error
// (`uint u = -1.5`, `int i = 1e20`), as are inf/nan. `hi_excl` is exclusive
// so the bound is an exactly-representable power of two (2^31, 2^32, ...).
inline bool CoerceFloatTokenToI64(const std::string& s, double lo,
                                  double hi_excl, int64_t* out) {
  double v = 0.0;
  if (!FastFloatParse(s.data(), s.data() + s.size(), &v)) return false;
  const double d = std::trunc(v);
  if (!(d >= lo && d < hi_excl)) return false;  // NaN/inf land here too
  *out = static_cast<int64_t>(d);
  return true;
}

inline bool CoerceFloatTokenToU64(const std::string& s, double hi_excl,
                                  uint64_t* out) {
  double v = 0.0;
  if (!FastFloatParse(s.data(), s.data() + s.size(), &v)) return false;
  const double d = std::trunc(v);
  if (!(d >= 0.0 && d < hi_excl)) return false;
  *out = static_cast<uint64_t>(d);
  return true;
}

// Strict decimal-integer token check (used to pick the exact-int fast path;
// non-matching Number tokens fall back to the float-coercion path above).
// Also rejects hex and a sign that doesn't match the type — DecimalTo* would
// silently stop at the first non-digit instead.
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

// Overflow-CHECKED variants for scalar value parsing: pxr errors on
// out-of-range integer literals; silent saturation corrupts data.
inline bool DecimalToI64Checked(const char* s, int64_t* out) {
  if (!s || !out) return false;
  bool neg = false;
  if (*s == '+' || *s == '-') {
    neg = (*s == '-');
    ++s;
  }
  const uint64_t lim = neg ? (static_cast<uint64_t>(INT64_MAX) + 1u)
                           : static_cast<uint64_t>(INT64_MAX);
  uint64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    const uint64_t d = static_cast<uint64_t>(*s - '0');
    if (v > (lim - d) / 10u) return false;  // would overflow
    v = v * 10u + d;
    ++s;
  }
  if (neg) {
    // -(2^63) can't be produced by negating an int64; build it directly.
    *out = (v == static_cast<uint64_t>(INT64_MAX) + 1u)
               ? INT64_MIN
               : -static_cast<int64_t>(v);
  } else {
    *out = static_cast<int64_t>(v);
  }
  return true;
}

inline bool DecimalToU64Checked(const char* s, uint64_t* out) {
  if (!s || !out) return false;
  if (*s == '+') ++s;
  uint64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    const uint64_t d = static_cast<uint64_t>(*s - '0');
    if (v > (UINT64_MAX - d) / 10u) return false;
    v = v * 10u + d;
    ++s;
  }
  *out = v;
  return true;
}

}  // namespace value_parser_detail
}  // namespace next
}  // namespace tinyusdz
