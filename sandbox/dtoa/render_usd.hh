// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// sandbox/dtoa — shared OpenUSD-notation renderer.
//
// The "USD-optimized" formatting layer, factored out of
// src/next/writer/dtoa.cc so that EVERY candidate dtoa core (dragonbox / ryu /
// zmij) can feed the SAME renderer its shortest decimal (significand, exponent)
// and produce byte-identical `usdcat` notation. This isolates the comparison to
// the digit-generation algorithm.
//
// Notation (mirrors pxr_double_conversion ToShortest/ToShortestSingle, matching
// `usdcat` byte-for-byte): shortest round-trip decimal, fixed for output decimal
// exponent in [-6, 15) and scientific outside, NO `+` on the exponent, NO
// zero-padding, integer-valued floats without a trailing `.0` (1.0 -> "1"),
// `-0` preserved, `nan` / `inf` / `-inf`.
//
// Every helper here is copied verbatim from dtoa.cc's private digit layout so
// the output is identical; the one addition is CANONICALIZE (trailing-zero trim)
// at the top of render_usd_finite, needed because some cores (zmij, and ryu's
// small-int path) hand back a significand WITH trailing zeros while dragonbox
// hands back the trimmed shortest. Trimming makes all cores canonical, which is
// required for byte-identity in scientific notation (e.g. 1.200e3 vs 1.2e3).

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace usddtoa {
namespace detail {

template <typename T>
inline int count_digits(T n) {
  int count = 1;
  for (;;) {
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}

inline void write2digits(char* out, std::size_t value) {
  *out++ = static_cast<char>('0' + value / 10);
  *out = static_cast<char>('0' + value % 10);
}

// OpenUSD exponent: leading '-' only (no '+'), minimal digits, no zero padding.
inline char* write_exponent(int exp, char* out) {
  if (exp < 0) {
    *out++ = '-';
    exp = -exp;
  }
  auto uexp = static_cast<uint32_t>(exp);
  char tmp[8];
  int n = 0;
  if (uexp == 0) tmp[n++] = '0';
  while (uexp) {
    tmp[n++] = static_cast<char>('0' + (uexp % 10u));
    uexp /= 10u;
  }
  for (int i = n - 1; i >= 0; --i) *out++ = tmp[i];
  return out;
}

inline char* fill_n(char* p, int n, char c) {
  for (int i = 0; i < n; i++, p++) *p = c;
  return p;
}

inline void format_decimal_impl(char* out, uint64_t value, uint32_t size) {
  unsigned n = size;
  while (value >= 100) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value % 100));
    value /= 100;
  }
  if (value >= 10) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value));
  } else {
    out[--n] = static_cast<char>('0' + value);
  }
}

inline char* format_decimal(char* out, uint64_t value, uint32_t num_digits) {
  format_decimal_impl(out, value, num_digits);
  return out + num_digits;
}

inline char* write_significand_e(char* out, uint64_t significand,
                                 int significand_size, int exponent) {
  out = format_decimal(out, significand, static_cast<uint32_t>(significand_size));
  return fill_n(out, exponent, '0');
}

inline char* write_significand(char* out, uint64_t significand,
                               int significand_size, int integral_size,
                               char decimal_point) {
  if (!decimal_point)
    return format_decimal(out, significand,
                          static_cast<uint32_t>(significand_size));
  out += significand_size + 1;
  char* end = out;
  int floating_size = significand_size - integral_size;
  for (int i = floating_size / 2; i > 0; --i) {
    out -= 2;
    write2digits(out, static_cast<std::size_t>(significand % 100));
    significand /= 100;
  }
  if (floating_size % 2 != 0) {
    *--out = static_cast<char>('0' + significand % 10);
    significand /= 10;
  }
  *--out = decimal_point;
  format_decimal(out - integral_size, significand,
                 static_cast<uint32_t>(integral_size));
  return end;
}

}  // namespace detail

// Special-value + unit fast paths, identical to dtoa_impl / dtoa_impl_t in
// src/next/writer/dtoa.cc. Returns true and fills [dst, *n) when handled
// (nan / ±inf / ±0 / ±1); returns false for a finite non-unit non-zero value
// that the core must convert.
template <typename Float>
inline bool try_special(char* dst, Float v, std::size_t* n) {
  if (std::isnan(v)) {
    std::memcpy(dst, "nan", 3);
    *n = 3;
    return true;
  }
  if (std::isinf(v)) {
    if (std::signbit(v)) {
      std::memcpy(dst, "-inf", 4);
      *n = 4;
    } else {
      std::memcpy(dst, "inf", 3);
      *n = 3;
    }
    return true;
  }
  if (std::fpclassify(v) == FP_ZERO) {  // OpenUSD preserves `-0`.
    char* p = dst;
    if (std::signbit(v)) *p++ = '-';
    *p++ = '0';
    *n = static_cast<std::size_t>(p - dst);
    return true;
  }
  // ±1 exact-bit fast path (matches dtoa_impl).
  if (v == static_cast<Float>(1)) {
    dst[0] = '1';
    *n = 1;
    return true;
  }
  if (v == static_cast<Float>(-1)) {
    dst[0] = '-';
    dst[1] = '1';
    *n = 2;
    return true;
  }
  return false;
}

// Render a FINITE, NON-ZERO value from its shortest decimal (significand,
// exponent) + sign into `dst`, returning the byte count. `max_digits` = 9
// (float) / 17 (double). Byte-identical to dtoa_impl_t's post-`to_decimal` path.
inline std::size_t render_usd_finite(char* dst, uint64_t significand,
                                     int exponent, bool is_negative,
                                     int max_digits) {
  using namespace detail;
  char* buf = dst;

  // CANONICALIZE: trim trailing zeros so every core feeds the same shortest
  // (significand, exponent) dragonbox already emits (no-op for dragonbox).
  while (significand % 10u == 0u) {
    significand /= 10u;
    ++exponent;
  }

  const int exp_lower = -6;
  const int exp_upper = 15;

  int significand_size = count_digits(significand);

  // Defensive round-to-max_digits (a no-op for a trailing-zero-free shortest,
  // which never exceeds max_digits). Kept for exact parity with dtoa.cc.
  if (significand_size > max_digits) {
    int digits_to_remove = significand_size - max_digits;
    uint64_t divisor = 1;
    for (int i = 0; i < digits_to_remove; i++) divisor *= 10;
    uint64_t remainder = significand % divisor;
    significand /= divisor;
    exponent += digits_to_remove;
    uint64_t half = divisor / 2;
    if (remainder > half || (remainder == half && (significand & 1))) {
      significand++;
      if (count_digits(significand) > max_digits) {
        significand /= 10;
        exponent++;
      }
    }
    significand_size = count_digits(significand);
  }

  int output_exp = exponent + significand_size - 1;
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper);

  char decimal_point = '.';
  if (use_exp_format) {
    if (significand_size == 1) decimal_point = '\0';
    if (is_negative) *buf++ = '-';
    buf = write_significand(buf, significand, significand_size, 1, decimal_point);
    *buf++ = 'e';
    buf = write_exponent(output_exp, buf);
    return static_cast<std::size_t>(buf - dst);
  }

  int exp = exponent + significand_size;
  if (exponent >= 0) {
    if (is_negative) *buf++ = '-';
    buf = write_significand_e(buf, significand, significand_size, exponent);
    return static_cast<std::size_t>(buf - dst);
  } else if (exp > 0) {
    if (is_negative) *buf++ = '-';
    buf = write_significand(buf, significand, significand_size, exp,
                            decimal_point);
    return static_cast<std::size_t>(buf - dst);
  }

  int num_zeros = -exp;
  bool pointy = num_zeros != 0 || significand_size != 0;
  if (is_negative) *buf++ = '-';
  *buf++ = '0';
  if (!pointy) return static_cast<std::size_t>(buf - dst);
  *buf++ = decimal_point;
  buf = fill_n(buf, num_zeros, '0');
  buf = format_decimal(buf, significand, static_cast<uint32_t>(significand_size));
  return static_cast<std::size_t>(buf - dst);
}

// FUSED renderer — same usdcat output as render_usd_finite, but tailored to
// consume a core's shortest (significand, exponent) with less work:
//   * NO trailing-zero trim loop (up to 16 divisions on a padded significand
//     like zmij's) — the digits are materialized ONCE and trailing zeros are
//     found by a byte scan;
//   * NO integer re-decomposition in the layout (write_significand's div-by-100
//     dance) — the point/exponent are placed by memcpy of the ready ASCII digits.
// This is the "fuse the digit core with usdcat rendering" path: the digit-gen
// (dragonbox/zmij to_decimal) stays, the RENDER is replaced with a lean layout.
// Byte-identical to render_usd_finite (verified by the exhaustive gate).
inline std::size_t render_usd_fused(char* dst, uint64_t significand,
                                    int exponent, bool is_negative,
                                    int /*max_digits*/) {
  using namespace detail;
  // Materialize every digit of the significand once (most-significant first).
  char digs[24];
  int total = count_digits(significand);
  format_decimal(digs, significand, static_cast<uint32_t>(total));
  // Significant length = total minus trailing zeros (byte scan, no division).
  int nd = total;
  while (nd > 1 && digs[nd - 1] == '0') --nd;
  // Base-10 exponent of the leading digit (magnitude counts trailing zeros).
  const int output_exp = total - 1 + exponent;

  char* p = dst;
  if (is_negative) *p++ = '-';

  if (output_exp < -6 || output_exp >= 15) {  // scientific
    *p++ = digs[0];
    if (nd > 1) {
      *p++ = '.';
      std::memcpy(p, digs + 1, static_cast<std::size_t>(nd - 1));
      p += nd - 1;
    }
    *p++ = 'e';
    p = write_exponent(output_exp, p);
    return static_cast<std::size_t>(p - dst);
  }

  const int point_after = output_exp + 1;  // digits before the decimal point
  if (point_after >= nd) {                  // integer, maybe trailing zeros
    std::memcpy(p, digs, static_cast<std::size_t>(nd));
    p += nd;
    p = fill_n(p, point_after - nd, '0');
  } else if (point_after <= 0) {            // 0.00..digits
    *p++ = '0';
    *p++ = '.';
    p = fill_n(p, -point_after, '0');
    std::memcpy(p, digs, static_cast<std::size_t>(nd));
    p += nd;
  } else {                                  // ddd.ddd
    std::memcpy(p, digs, static_cast<std::size_t>(point_after));
    p += point_after;
    *p++ = '.';
    std::memcpy(p, digs + point_after, static_cast<std::size_t>(nd - point_after));
    p += nd - point_after;
  }
  return static_cast<std::size_t>(p - dst);
}

}  // namespace usddtoa
