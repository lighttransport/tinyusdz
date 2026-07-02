// SPDX-License-Identifier: Apache-2.0 OR BSL-1.0
// sandbox/dtoa — Ryū float shortest-decimal accessor.
// Includes ryu's f2s.c to reach the static f2d(); replicates f2s_buffered_n's
// bit decode but returns the (mantissa, exponent) instead of a string. Compile
// this TU INSTEAD OF f2s.c (it pulls f2s.c in via #include).

#include "ryu/f2s.c"  // brings in f2d, floating_decimal_32, FLOAT_* macros

#include "ryu_decimal.h"

ryu_dec32 ryu_f2dec(float f) {
  const uint32_t bits = float_to_bits(f);
  ryu_dec32 r;
  r.neg = ((bits >> (FLOAT_MANTISSA_BITS + FLOAT_EXPONENT_BITS)) & 1) != 0;
  const uint32_t ieeeMantissa = bits & ((1u << FLOAT_MANTISSA_BITS) - 1);
  const uint32_t ieeeExponent =
      (bits >> FLOAT_MANTISSA_BITS) & ((1u << FLOAT_EXPONENT_BITS) - 1);
  const floating_decimal_32 v = f2d(ieeeMantissa, ieeeExponent);
  r.sig = v.mantissa;
  r.exp = v.exponent;
  return r;
}
