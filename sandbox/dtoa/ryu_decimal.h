// SPDX-License-Identifier: Apache-2.0 OR BSL-1.0
// sandbox/dtoa — expose Ryū's internal shortest-decimal (digits + exponent).
//
// Upstream ryu only exposes the string API (f2s/d2s, scientific notation). Its
// per-value decimal (floating_decimal_32/64 via f2d/d2d) is file-static. The
// two wrapper TUs ryu_f2dec.c / ryu_d2dec.c each `#include` one ryu source to
// reach those statics and expose the raw (significand, exponent) so it can feed
// the shared render_usd() layer. Caller handles nan/inf/zero/±1 first (via
// usddtoa::try_special), so these are only invoked for finite non-zero values.

#ifndef RYU_DECIMAL_H
#define RYU_DECIMAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t sig;
  int32_t exp;
  int neg;
} ryu_dec32;

typedef struct {
  uint64_t sig;
  int32_t exp;
  int neg;
} ryu_dec64;

ryu_dec32 ryu_f2dec(float f);
ryu_dec64 ryu_d2dec(double f);

#ifdef __cplusplus
}
#endif

#endif  // RYU_DECIMAL_H
