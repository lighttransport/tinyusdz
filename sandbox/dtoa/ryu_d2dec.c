// SPDX-License-Identifier: Apache-2.0 OR BSL-1.0
// sandbox/dtoa — Ryū double shortest-decimal accessor.
// Includes ryu's d2s.c to reach the static d2d()/d2d_small_int(); replicates
// d2s_buffered_n's decode (incl. the small-integer trailing-zero trim) but
// returns (mantissa, exponent). Compile this TU INSTEAD OF d2s.c.

#include "ryu/d2s.c"  // brings in d2d, d2d_small_int, div10, DOUBLE_* macros

#include "ryu_decimal.h"

ryu_dec64 ryu_d2dec(double f) {
  const uint64_t bits = double_to_bits(f);
  ryu_dec64 r;
  r.neg = ((bits >> (DOUBLE_MANTISSA_BITS + DOUBLE_EXPONENT_BITS)) & 1) != 0;
  const uint64_t ieeeMantissa = bits & ((1ull << DOUBLE_MANTISSA_BITS) - 1);
  const uint32_t ieeeExponent =
      (uint32_t)((bits >> DOUBLE_MANTISSA_BITS) & ((1u << DOUBLE_EXPONENT_BITS) - 1));

  floating_decimal_64 v;
  const bool isSmallInt = d2d_small_int(ieeeMantissa, ieeeExponent, &v);
  if (isSmallInt) {
    // Move trailing decimal zeros into the exponent (as d2s_buffered_n does for
    // scientific notation).
    for (;;) {
      const uint64_t q = div10(v.mantissa);
      const uint32_t rr = ((uint32_t)v.mantissa) - 10 * ((uint32_t)q);
      if (rr != 0) break;
      v.mantissa = q;
      ++v.exponent;
    }
  } else {
    v = d2d(ieeeMantissa, ieeeExponent);
  }
  r.sig = v.mantissa;
  r.exp = v.exponent;
  return r;
}
