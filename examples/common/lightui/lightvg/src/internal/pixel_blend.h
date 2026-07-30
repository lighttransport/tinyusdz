/*
 * src/internal/pixel_blend.h — Shared ARGB32 Porter-Duff blend primitives
 *
 * Private header. Only sources under src/ may include it. These symbols
 * are NOT part of the public API and may change across minor versions.
 *
 * The bodies are the same SWAR-based formulas that lived previously inside
 * lightvg/src/canvas.c. They are factored here so the canvas rasterizer, scene
 * compositor, and glyph-blending path can share one implementation.
 *
 * Pixel layout assumed throughout: 0xAARRGGBB, non-premultiplied.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LVG_INTERNAL_PIXEL_BLEND_H
#define LVG_INTERNAL_PIXEL_BLEND_H

#include <stdint.h>

/*
 * Optional SIMD, mirroring the knobs used by lightvg/src/canvas.c. Undefined here
 * means "scalar only"; when the including TU is built with
 * -DLVG_USE_SSE2=1 or -DLVG_USE_NEON=1 the SIMD fast paths compile in.
 */
#ifndef LVG_USE_SSE2
#define LVG_USE_SSE2 0
#endif
#ifndef LVG_USE_NEON
#define LVG_USE_NEON 0
#endif
#if LVG_USE_SSE2
#include <emmintrin.h>
#endif
#if LVG_USE_NEON
#include <arm_neon.h>
#endif

/* SWAR byte-lane constants for ARGB pixels packed as 0xAARRGGBB:
 *   LVG_RB_MASK  keeps the red and blue bytes in a 32-bit word, leaving
 *                8-bit headroom in the middle of each lane for multiply.
 *   LVG_RB_HALF  adds 128 into each lane, as rounding bias for div-by-255. */
#define LVG_RB_MASK 0x00FF00FFu
#define LVG_RB_HALF 0x00800080u

/* Fast, exact division by 255 for x in [0, 65535].
 *   (x + 128 + ((x + 128) >> 8)) >> 8
 * Equivalent to (x + 127.5) / 255 rounded, without any DIV instruction. */
static inline uint32_t lvg_px_div255(uint32_t x)
{
    uint32_t t = x + 128u;
    return (t + (t >> 8)) >> 8;
}

/*
 * Alpha-blend src over dst using SRC_OVER.
 *
 * Both colours are non-premultiplied 0xAARRGGBB. The simplified blend
 *   out = (src * sa + dst * (255 - sa)) / 255
 * is exact when dst is opaque (da == 255), which is the common case for a
 * display surface. For general translucent destinations the full Porter-Duff
 * formula is used.
 */
static inline uint32_t lvg_px_blend_over(uint32_t dst, uint32_t src)
{
    uint32_t sa = src >> 24;
    if (sa == 0)   return dst;
    if (sa == 255) return src;

    uint32_t da     = dst >> 24;
    uint32_t inv_sa = 255u - sa;

    uint32_t out_a;
    uint32_t out_r, out_g, out_b;

    if (da == 255) {
        /* Fast path: opaque destination. SWAR R/B in one 32-bit op and
         * A/G in another — halves the per-pixel work vs four scalar
         * divides. */
        uint32_t src_rb = src & LVG_RB_MASK;
        uint32_t dst_rb = dst & LVG_RB_MASK;
        uint32_t src_ag = (src >> 8) & LVG_RB_MASK;
        uint32_t dst_ag = (dst >> 8) & LVG_RB_MASK;

        uint32_t rb = src_rb * sa + dst_rb * inv_sa + LVG_RB_HALF;
        uint32_t ag = src_ag * sa + dst_ag * inv_sa + LVG_RB_HALF;
        rb = ((rb + ((rb >> 8) & LVG_RB_MASK)) >> 8) & LVG_RB_MASK;
        ag = ((ag + ((ag >> 8) & LVG_RB_MASK)) >> 8) & LVG_RB_MASK;
        return 0xFF000000u | rb | (ag << 8);
    }

    /* General Porter-Duff SRC_OVER */
    uint32_t da_inv_sa = lvg_px_div255(da * inv_sa);
    out_a = sa + da_inv_sa;
    if (out_a == 0) return 0;
    uint32_t half = out_a >> 1;
    out_r = (((src >> 16) & 0xFF) * sa + ((dst >> 16) & 0xFF) * da_inv_sa + half) / out_a;
    out_g = (((src >>  8) & 0xFF) * sa + ((dst >>  8) & 0xFF) * da_inv_sa + half) / out_a;
    out_b = (( src        & 0xFF) * sa + ( dst        & 0xFF) * da_inv_sa + half) / out_a;

    return ((out_a & 0xFF) << 24) |
           ((out_r & 0xFF) << 16) |
           ((out_g & 0xFF) <<  8) |
            (out_b & 0xFF);
}

/*
 * Constant-colour row blend: composite `color` over `n` pixels at `dst`
 * using SRC_OVER.  Mirrors the body that previously lived inline in
 * canvas.c's fill_rect translucent path.
 *
 *   - Alpha 0   : no-op
 *   - Alpha 255 : plain memset-style copy (caller may prefer its own fill)
 *   - 0 < A < 255 : SWAR scalar, with an SSE2 4-at-a-time path that
 *                   engages when all four destination alphas are 0xFF.
 */
static inline void lvg_px_blend_over_constant_row(uint32_t *dst,
                                                  uint32_t color,
                                                  int n)
{
    if (n <= 0) return;
    uint32_t sa = color >> 24;
    if (sa == 0) return;
    if (sa == 255) {
        for (int i = 0; i < n; i++) dst[i] = color;
        return;
    }

    const uint32_t inv_sa    = 255u - sa;
    const uint32_t src_rb_sa = (color & LVG_RB_MASK) * sa + LVG_RB_HALF;
    const uint32_t src_ag_sa = ((color >> 8) & LVG_RB_MASK) * sa + LVG_RB_HALF;
    int i = 0;

#if LVG_USE_SSE2
    {
        const __m128i zero       = _mm_setzero_si128();
        const __m128i inv_sa_v16 = _mm_set1_epi16((short)inv_sa);
        const uint32_t cB = color & 0xFFu;
        const uint32_t cG = (color >>  8) & 0xFFu;
        const uint32_t cR = (color >> 16) & 0xFFu;
        /* Per-channel (src_c * sa + 128) in BGRA memory order, doubled
         * across the two pixels each 8->16 unpack produces. */
        const __m128i src_bias = _mm_set_epi16(
            0, (short)(cR * sa + 128), (short)(cG * sa + 128), (short)(cB * sa + 128),
            0, (short)(cR * sa + 128), (short)(cG * sa + 128), (short)(cB * sa + 128));
        const __m128i alpha_ff = _mm_set1_epi32((int)0xFF000000);

        for (; i + 4 <= n; i += 4) {
            __m128i px = _mm_loadu_si128((const __m128i *)(dst + i));
            /* All four destination alphas must be 0xFF for the SIMD
             * path; otherwise fall back to scalar blend per pixel. */
            __m128i a_only = _mm_and_si128(px, alpha_ff);
            if (_mm_movemask_epi8(_mm_cmpeq_epi8(a_only, alpha_ff)) != 0xFFFF) {
                for (int j = 0; j < 4; j++)
                    dst[i + j] = lvg_px_blend_over(dst[i + j], color);
                continue;
            }
            __m128i lo = _mm_unpacklo_epi8(px, zero);
            __m128i hi = _mm_unpackhi_epi8(px, zero);
            lo = _mm_add_epi16(_mm_mullo_epi16(lo, inv_sa_v16), src_bias);
            hi = _mm_add_epi16(_mm_mullo_epi16(hi, inv_sa_v16), src_bias);
            lo = _mm_srli_epi16(_mm_add_epi16(lo, _mm_srli_epi16(lo, 8)), 8);
            hi = _mm_srli_epi16(_mm_add_epi16(hi, _mm_srli_epi16(hi, 8)), 8);
            __m128i res = _mm_packus_epi16(lo, hi);
            _mm_storeu_si128((__m128i *)(dst + i),
                             _mm_or_si128(res, alpha_ff));
        }
    }
#endif

    for (; i < n; i++) {
        uint32_t d = dst[i];
        if ((d >> 24) == 255u) {
            uint32_t dst_rb = d & LVG_RB_MASK;
            uint32_t dst_ag = (d >> 8) & LVG_RB_MASK;
            uint32_t rb = src_rb_sa + dst_rb * inv_sa;
            uint32_t ag = src_ag_sa + dst_ag * inv_sa;
            rb = ((rb + ((rb >> 8) & LVG_RB_MASK)) >> 8) & LVG_RB_MASK;
            ag = ((ag + ((ag >> 8) & LVG_RB_MASK)) >> 8) & LVG_RB_MASK;
            dst[i] = 0xFF000000u | rb | (ag << 8);
        } else {
            dst[i] = lvg_px_blend_over(d, color);
        }
    }
}

#endif /* LVG_INTERNAL_PIXEL_BLEND_H */
