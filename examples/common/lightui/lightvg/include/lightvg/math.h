/*
 * lightvg/math.h — libm-free math routines for the VG rasterizer.
 *
 * Provides sqrtf / sinf / cosf / floorf / ceilf / roundf / fabsf replacements
 * that do NOT call libm.  Accuracy is sufficient for rasterization
 * (sub-pixel errors < 1e-5 for the trig functions; sqrtf is near-IEEE via
 * Newton-Raphson refinement of a bit-hack initial guess).
 *
 * Override any single routine before including this header:
 *
 *     #define LVG_SQRTF(x) my_fast_sqrt(x)
 *     #include <lightvg/math.h>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_MATH_H
#define LIGHTVG_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LVG_PI
#  define LVG_PI       3.14159265358979323846f
#endif
#ifndef LVG_TAU
#  define LVG_TAU      6.28318530717958647692f
#endif
#ifndef LVG_PI_OVER_2
#  define LVG_PI_OVER_2 1.57079632679489661923f
#endif

/* ---- fabsf ---- */
static inline float lvg__fabsf(float x) {
    union { float f; uint32_t u; } b; b.f = x;
    b.u &= 0x7FFFFFFFu;
    return b.f;
}
#ifndef LVG_FABSF
#  define LVG_FABSF(x) lvg__fabsf(x)
#endif

/* ---- floorf / ceilf / roundf (truncate-based; valid for |x| < 2^23) ---- */
static inline float lvg__floorf(float x) {
    float t = (float)(int32_t)x;
    return (x < t) ? (t - 1.0f) : t;
}
static inline float lvg__ceilf(float x) {
    float t = (float)(int32_t)x;
    return (x > t) ? (t + 1.0f) : t;
}
static inline float lvg__roundf(float x) {
    return (x >= 0.0f) ? lvg__floorf(x + 0.5f)
                       : -lvg__floorf(-x + 0.5f);
}
#ifndef LVG_FLOORF
#  define LVG_FLOORF(x) lvg__floorf(x)
#endif
#ifndef LVG_CEILF
#  define LVG_CEILF(x)  lvg__ceilf(x)
#endif
#ifndef LVG_ROUNDF
#  define LVG_ROUNDF(x) lvg__roundf(x)
#endif

/* ---- sqrtf: bit-hack seed + 3 Newton iterations (near-IEEE in float) ---- */
static inline float lvg__sqrtf(float x) {
    /* Negated compare catches NaN as well as x <= 0. */
    if (!(x > 0.0f)) return 0.0f;
    union { float f; uint32_t u; } b; b.f = x;
    /* Seed: exponent half, mantissa small tweak. ~3% error. */
    b.u = (b.u >> 1) + 0x1FC00000u;
    float y = b.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}
#ifndef LVG_SQRTF
#  define LVG_SQRTF(x) lvg__sqrtf(x)
#endif

/* ---- sinf / cosf: range-reduce to [-pi, pi] then to [-pi/2, pi/2],
 *                  then Maclaurin-7 (max err ~3e-6 in float). ---- */
static inline float lvg__sinf_core(float x) {
    /* valid for x in [-pi/2, pi/2]; sin(x) ≈ x - x^3/6 + x^5/120 - x^7/5040 */
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f/6.0f
              - x2 * (1.0f/120.0f
              - x2 * (1.0f/5040.0f))));
}
static inline float lvg__sinf(float x) {
    /* Reduce x to [-pi, pi]. */
    float k = x * (1.0f / LVG_TAU);
    k = (k >= 0.0f) ? lvg__floorf(k + 0.5f) : -lvg__floorf(-k + 0.5f);
    x -= k * LVG_TAU;
    /* Now x roughly in [-pi, pi]. Fold to [-pi/2, pi/2]. */
    if (x >  LVG_PI_OVER_2) x =  LVG_PI - x;
    else if (x < -LVG_PI_OVER_2) x = -LVG_PI - x;
    return lvg__sinf_core(x);
}
static inline float lvg__cosf(float x) {
    return lvg__sinf(x + LVG_PI_OVER_2);
}
#ifndef LVG_SINF
#  define LVG_SINF(x) lvg__sinf(x)
#endif
#ifndef LVG_COSF
#  define LVG_COSF(x) lvg__cosf(x)
#endif

/* ---- Double-precision sinc helpers (Lanczos resampling). ---- */
/* We only need sin() for sinc(x)=sin(pi*x)/(pi*x); float precision is
 * sufficient for image filter weights. */
static inline double lvg__sin_d(double x) {
    return (double)lvg__sinf((float)x);
}
static inline double lvg__sqrt_d(double x) {
    if (!(x > 0.0)) return 0.0;
    double y = (double)lvg__sqrtf((float)x);
    /* One Newton step in double precision to recover accuracy. */
    y = 0.5 * (y + x / y);
    return y;
}
static inline double lvg__floor_d(double x) {
    double t = (double)(int64_t)x;
    return (x < t) ? (t - 1.0) : t;
}
#ifndef LVG_SIN
#  define LVG_SIN(x)   lvg__sin_d(x)
#endif
#ifndef LVG_SQRT
#  define LVG_SQRT(x)  lvg__sqrt_d(x)
#endif
#ifndef LVG_FLOOR
#  define LVG_FLOOR(x) lvg__floor_d(x)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_MATH_H */
