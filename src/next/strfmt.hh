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

#include <cstdint>
#include <string>

namespace tinyusdz {
namespace next {

// Append the decimal form of `v` to `o` (no heap churn for the common short case;
// a uint64_t needs at most 20 digits).
inline void AppendUInt(std::string& o, uint64_t v) {
  char buf[20];
  char* p = buf + sizeof(buf);
  do {
    *--p = static_cast<char>('0' + (v % 10));
    v /= 10;
  } while (v);
  o.append(p, static_cast<size_t>(buf + sizeof(buf) - p));
}

inline void AppendInt(std::string& o, int64_t v) {
  if (v < 0) {
    o += '-';
    // Negate in unsigned space so INT64_MIN is handled without UB.
    AppendUInt(o, ~static_cast<uint64_t>(v) + 1u);
  } else {
    AppendUInt(o, static_cast<uint64_t>(v));
  }
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
