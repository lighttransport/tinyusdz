/*
 * TinyEXR - ARM NEON kernels (byte interleave).
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "exr_internal.h"

#if defined(EXR_NEON)

#include <arm_neon.h>

void exr_interleave_neon(const uint8_t *src, uint8_t *dst, size_t n) {
    size_t half = (n + 1) / 2, n2 = n / 2, i = 0;
    const uint8_t *t1 = src, *t2 = src + half;
    for (; i + 16 <= n2; i += 16) {
        uint8x16x2_t v;
        v.val[0] = vld1q_u8(t1 + i);
        v.val[1] = vld1q_u8(t2 + i);
        vst2q_u8(dst + 2 * i, v);
    }
    for (; i < n2; ++i) {
        dst[2 * i] = t1[i];
        dst[2 * i + 1] = t2[i];
    }
    if (n & 1) dst[n - 1] = t1[n2];
}

#endif /* EXR_NEON */
