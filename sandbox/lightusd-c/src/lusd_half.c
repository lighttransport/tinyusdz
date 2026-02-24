/*
 * lusd_half.c - Half-float (uint16_t) <-> float conversion
 *
 * IEEE 754 half-precision: 1 sign, 5 exponent, 10 mantissa bits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_platform.h"
#include <string.h>

/* Half to float conversion */
static float half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t f;

    if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            f = sign;
        } else {
            /* Denormalized */
            exp = 1;
            while (!(mant & 0x400)) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x3FF;
            f = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        /* Inf or NaN */
        f = sign | 0x7F800000U | (mant << 13);
    } else {
        /* Normalized */
        f = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }

    float result;
    memcpy(&result, &f, sizeof(float));
    return result;
}

/* Float to half conversion */
static uint16_t float_to_half(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(uint32_t));

    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = ((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = bits & 0x7FFFFF;

    if (exp <= 0) {
        if (exp < -10) {
            /* Too small, flush to zero */
            return (uint16_t)sign;
        }
        /* Denormalized */
        mant = (mant | 0x800000) >> (1 - exp);
        return (uint16_t)(sign | (mant >> 13));
    } else if (exp == 0xFF - 127 + 15) {
        if (mant) {
            /* NaN */
            return (uint16_t)(sign | 0x7C00 | (mant >> 13));
        }
        /* Inf */
        return (uint16_t)(sign | 0x7C00);
    } else if (exp > 30) {
        /* Overflow -> Inf */
        return (uint16_t)(sign | 0x7C00);
    }

    return (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
}

/* Public functions (not in public API yet, used internally) */
float lusd_half_to_float(uint16_t h) {
    return half_to_float(h);
}

uint16_t lusd_float_to_half(float f) {
    return float_to_half(f);
}
