// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - freestanding integer<->string formatting
//
// Locale-independent, libc-free decimal integer formatting (no std::to_string,
// no snprintf). Output is byte-identical to `ss << v` under the classic locale:
// plain decimal, leading '-' for negatives, no '+'/padding/grouping. Kept
// dependency-light (<string>/<cstdint> only) so every "next" subsystem shares one
// copy instead of re-deriving these helpers (they were duplicated as
// AppendI64/AppendU64 in the value printer and IntToStr/UIntToStr in the writer).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyusdz {
namespace next {

// Format the decimal form of `v` into a caller buffer, returning the byte count.
// `buf` capacity must be >= 20 (a uint64_t needs at most 20 digits). No heap.
inline size_t UIntTo(char* buf, uint64_t v) {
  char tmp[20];
  char* p = tmp + sizeof(tmp);
  do {
    *--p = static_cast<char>('0' + (v % 10));
    v /= 10;
  } while (v);
  const size_t n = static_cast<size_t>(tmp + sizeof(tmp) - p);
  for (size_t i = 0; i < n; ++i) buf[i] = p[i];
  return n;
}

// Format the decimal form of `v` into a caller buffer, returning the byte count.
// `buf` capacity must be >= 21 (leading '-' + up to 20 digits). No heap.
inline size_t IntTo(char* buf, int64_t v) {
  if (v < 0) {
    buf[0] = '-';
    // Negate in unsigned space so INT64_MIN is handled without UB.
    return 1 + UIntTo(buf + 1, ~static_cast<uint64_t>(v) + 1u);
  }
  return UIntTo(buf, static_cast<uint64_t>(v));
}

// Append the decimal form of `v` to `o` (no heap churn for the common short case;
// a uint64_t needs at most 20 digits).
inline void AppendUInt(std::string& o, uint64_t v) {
  char buf[20];
  o.append(buf, UIntTo(buf, v));
}

inline void AppendInt(std::string& o, int64_t v) {
  char buf[21];
  o.append(buf, IntTo(buf, v));
}

inline std::string UIntToStr(uint64_t v) {
  std::string s;
  AppendUInt(s, v);
  return s;
}

inline std::string IntToStr(int64_t v) {
  std::string s;
  AppendInt(s, v);
  return s;
}

}  // namespace next
}  // namespace tinyusdz
