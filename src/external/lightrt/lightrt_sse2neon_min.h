/*
 * lightrt_sse2neon_min.h — minimal SSE→NEON shim for the C11 triangle kernel.
 *
 * Scope: ONLY the ~39 SSE/SSE2/SSE4.1 intrinsics that lightrt_c_tri.c uses for
 * its 4-wide (BVH4) leaf + node code. Including this on an ARM NEON target lets
 * that SSE-written code (triangle, curve, point, sphere, quad, bilinear, qtri,
 * and the parametric-patch traversal drivers) compile and run as NEON 4-wide on
 * A64FX, instead of hand-duplicating every kernel. It is NOT a general
 * sse2neon — only what this file needs.
 *
 * Semantics notes:
 *  - Comparisons return a float-domain all-ones / all-zero mask
 *    (vreinterpretq_f32_u32 of the NEON predicate), matching SSE so the
 *    float-domain bitwise ops (_mm_and_ps etc.) and _mm_movemask_ps work.
 *  - _mm_blendv_ps selects per-lane on the mask's top bit in SSE; the kernel
 *    only ever feeds it comparison masks (all-ones/all-zero), for which the
 *    full-bitmask vbslq_f32 is identical.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_SSE2NEON_MIN_H
#define LIGHTRT_SSE2NEON_MIN_H

#include <arm_neon.h>
#include <string.h>

typedef float32x4_t __m128;
typedef int32x4_t __m128i;

#define _MM_HINT_T0 3
static inline void _mm_prefetch(const void *p, int hint) {
    (void)hint;
    __builtin_prefetch(p, 0, 3);
}

/* ---- set / load / store (float) ---- */
static inline __m128 _mm_set1_ps(float a) { return vdupq_n_f32(a); }
static inline __m128 _mm_setzero_ps(void) { return vdupq_n_f32(0.0f); }
static inline __m128 _mm_setr_ps(float a, float b, float c, float d) {
    float v[4] = {a, b, c, d};
    return vld1q_f32(v);
}
static inline __m128 _mm_load_ps(const float *p) { return vld1q_f32(p); }
static inline __m128 _mm_loadu_ps(const float *p) { return vld1q_f32(p); }
static inline void _mm_store_ps(float *p, __m128 a) { vst1q_f32(p, a); }
static inline void _mm_storeu_ps(float *p, __m128 a) { vst1q_f32(p, a); }

/* ---- arithmetic (float) ---- */
static inline __m128 _mm_add_ps(__m128 a, __m128 b) { return vaddq_f32(a, b); }
static inline __m128 _mm_sub_ps(__m128 a, __m128 b) { return vsubq_f32(a, b); }
static inline __m128 _mm_mul_ps(__m128 a, __m128 b) { return vmulq_f32(a, b); }
static inline __m128 _mm_div_ps(__m128 a, __m128 b) { return vdivq_f32(a, b); }
static inline __m128 _mm_min_ps(__m128 a, __m128 b) { return vminq_f32(a, b); }
static inline __m128 _mm_max_ps(__m128 a, __m128 b) { return vmaxq_f32(a, b); }
static inline __m128 _mm_sqrt_ps(__m128 a) { return vsqrtq_f32(a); }
static inline __m128 _mm_fmadd_ps(__m128 a, __m128 b, __m128 c) {
    return vfmaq_f32(c, a, b); /* a*b + c */
}
static inline __m128 _mm_fmsub_ps(__m128 a, __m128 b, __m128 c) {
    return vfmaq_f32(vnegq_f32(c), a, b); /* a*b - c */
}

/* ---- bitwise (float domain) ---- */
static inline __m128 _mm_and_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(
        vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}
static inline __m128 _mm_or_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(
        vorrq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}
static inline __m128 _mm_andnot_ps(__m128 a, __m128 b) {
    /* (~a) & b ; vbicq_u32(x,y) = x & ~y */
    return vreinterpretq_f32_u32(
        vbicq_u32(vreinterpretq_u32_f32(b), vreinterpretq_u32_f32(a)));
}

/* ---- comparisons (float -> float-domain mask) ---- */
static inline __m128 _mm_cmplt_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(vcltq_f32(a, b));
}
static inline __m128 _mm_cmple_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(vcleq_f32(a, b));
}
static inline __m128 _mm_cmpgt_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(vcgtq_f32(a, b));
}
static inline __m128 _mm_cmpge_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(vcgeq_f32(a, b));
}
static inline __m128 _mm_cmpeq_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(vceqq_f32(a, b));
}
static inline __m128 _mm_cmpneq_ps(__m128 a, __m128 b) {
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_f32(a, b)));
}

/* ---- blend / movemask ---- */
static inline __m128 _mm_blendv_ps(__m128 a, __m128 b, __m128 mask) {
    /* select b where mask lane set, a otherwise */
    return vbslq_f32(vreinterpretq_u32_f32(mask), b, a);
}
static inline int _mm_movemask_ps(__m128 a) {
    uint32x4_t s = vshrq_n_u32(vreinterpretq_u32_f32(a), 31); /* 0/1 per lane */
    const uint32_t bits[4] = {1u, 2u, 4u, 8u};
    return (int)vaddvq_u32(vmulq_u32(s, vld1q_u32(bits)));
}

/* ---- integer vector (only what the kernel uses) ---- */
static inline __m128i _mm_set1_epi32(int a) { return vdupq_n_s32(a); }
static inline __m128i _mm_cmpeq_epi32(__m128i a, __m128i b) {
    return vreinterpretq_s32_u32(vceqq_s32(a, b));
}
static inline __m128i _mm_xor_si128(__m128i a, __m128i b) {
    return veorq_s32(a, b);
}
static inline __m128i _mm_loadu_si128(const __m128i *p) {
    int32_t v[4];
    memcpy(v, p, 16);
    return vld1q_s32(v);
}
static inline void _mm_store_si128(__m128i *p, __m128i a) {
    vst1q_s32((int32_t *)p, a);
}
static inline __m128 _mm_castsi128_ps(__m128i a) {
    return vreinterpretq_f32_s32(a);
}
static inline __m128 _mm_cvtepi32_ps(__m128i a) { return vcvtq_f32_s32(a); }

/* byte/short loads for the quantized-triangle decode (tri_load4_u8/u16) */
static inline __m128i _mm_cvtsi32_si128(int v) {
    return vsetq_lane_s32(v, vdupq_n_s32(0), 0);
}
static inline __m128i _mm_cvtepu8_epi32(__m128i a) {
    uint8x16_t b = vreinterpretq_u8_s32(a);
    uint16x8_t w = vmovl_u8(vget_low_u8(b));         /* bytes 0..7 -> u16 */
    uint32x4_t d = vmovl_u16(vget_low_u16(w));       /* bytes 0..3 -> u32 */
    return vreinterpretq_s32_u32(d);
}
static inline __m128i _mm_loadl_epi64(const __m128i *p) {
    int32x2_t lo;
    memcpy(&lo, p, 8);
    return vcombine_s32(lo, vdup_n_s32(0));
}
static inline __m128i _mm_cvtepu16_epi32(__m128i a) {
    uint16x8_t w = vreinterpretq_u16_s32(a);
    return vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(w)));
}

#endif /* LIGHTRT_SSE2NEON_MIN_H */
