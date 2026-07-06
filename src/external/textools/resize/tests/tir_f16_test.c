/*
 * tir - exhaustive f16 <-> f32 converter test.
 *
 * The SIMD f16->f32 kernels (F16C vcvtph2ps, NEON FCVT) once force-quieted
 * signaling NaNs, while the scalar reference preserves the mantissa. This
 * walks all 65536 half bit patterns through every available SIMD level and
 * requires bit-identical results to the scalar reference, both directions.
 *
 * It links the internal vtable (tir__k / tir__f16_to_f32_sc) directly because
 * the divergence is invisible through the public resize pipeline -- a FIR
 * multiply quiets an sNaN operand anyway -- so a plain resize can't observe it.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tir_internal.h"

static int g_fail = 0;

static const char *lvl_name(tir_simd_level l) {
    switch (l) {
        case TIR_SIMD_SCALAR: return "scalar";
        case TIR_SIMD_SSE2:   return "sse2";
        case TIR_SIMD_SSE41:  return "sse4.1";
        case TIR_SIMD_AVX2:   return "avx2";
        case TIR_SIMD_NEON:   return "neon";
        case TIR_SIMD_SVE:    return "sve";
        default:              return "?";
    }
}

int main(void) {
    enum { N = 65536 };
    uint16_t *h = (uint16_t *)malloc(N * sizeof(uint16_t));
    float *ref = (float *)malloc(N * sizeof(float));
    float *cur = (float *)malloc(N * sizeof(float));
    uint16_t *rt_ref = (uint16_t *)malloc(N * sizeof(uint16_t));
    uint16_t *rt_cur = (uint16_t *)malloc(N * sizeof(uint16_t));
    uint32_t avail = tir_simd_available();
    int i, l;

    for (i = 0; i < N; ++i) h[i] = (uint16_t)i;
    tir__f16_to_f32_sc(ref, h, N);      /* scalar oracle: every half -> f32 */
    tir__f32_to_f16_sc(rt_ref, ref, N); /* scalar oracle: those f32 -> f16 */

    for (l = 0; l <= TIR_SIMD_SVE; ++l) {
        int fdiff = 0, rdiff = 0, first = -1;
        if (l != TIR_SIMD_SCALAR && !(avail & (1u << l))) continue;
        if (tir_simd_force((tir_simd_level)l) != TIR_SUCCESS) continue;

        tir__k.f16_to_f32(cur, h, N);
        for (i = 0; i < N; ++i) {
            uint32_t a, b;
            memcpy(&a, &cur[i], 4);
            memcpy(&b, &ref[i], 4);
            if (a != b) {
                if (first < 0) first = i;
                fdiff++;
            }
        }
        tir__k.f32_to_f16(rt_cur, ref, N);
        for (i = 0; i < N; ++i)
            if (rt_cur[i] != rt_ref[i]) rdiff++;

        if (fdiff || rdiff) {
            g_fail++;
            fprintf(stderr,
                    "FAIL %-6s: f16->f32 %d/%d differ (first half=0x%04X), "
                    "f32->f16 %d/%d differ\n",
                    lvl_name((tir_simd_level)l), fdiff, N,
                    first >= 0 ? (unsigned)h[first] : 0u, rdiff, N);
        } else {
            fprintf(stderr, "  %-6s: all %d half codes bit-exact (both ways)\n",
                    lvl_name((tir_simd_level)l), N);
        }
    }

    free(h);
    free(ref);
    free(cur);
    free(rt_ref);
    free(rt_cur);
    printf("tir f16 test: %s\n", g_fail ? "FAILED" : "OK");
    return g_fail ? 1 : 0;
}
