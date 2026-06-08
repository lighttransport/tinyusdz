/*
 * TinyEXR - CPU feature detection + SIMD dispatch table.
 *
 * Runtime CPUID detection (x86) chooses the best available kernel; a scalar
 * implementation is always present as a fallback. NEON is selected at compile
 * time on ARM. The dispatch model follows fpng's CPUID approach.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "exr_internal.h"

#define CAP_F16C (1u << 8)

#if defined(EXR_X86)
#if defined(_MSC_VER)
#include <intrin.h>
static void cpuidex(int out[4], int leaf, int subleaf) {
    __cpuidex(out, leaf, subleaf);
}
static unsigned long long xgetbv0(void) { return _xgetbv(0); }
#else
#include <cpuid.h>
static void cpuidex(int out[4], int leaf, int subleaf) {
    unsigned int a, b, c, d;
    if (!__get_cpuid_count((unsigned int)leaf, (unsigned int)subleaf, &a, &b, &c, &d)) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    out[0] = (int)a; out[1] = (int)b; out[2] = (int)c; out[3] = (int)d;
}
static unsigned long long xgetbv0(void) {
    unsigned int eax, edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return ((unsigned long long)edx << 32) | eax;
}
#endif

static uint32_t detect_caps(void) {
    uint32_t caps = 0;
    int r[4];
    int max_leaf;
    int have_avx = 0, have_osxsave = 0;
    cpuidex(r, 0, 0);
    max_leaf = r[0];
    if (max_leaf < 1) return 0;
    cpuidex(r, 1, 0);
    if (r[3] & (1 << 26)) caps |= EXR_SIMD_SSE2;   /* EDX.26 */
    if (r[2] & (1 << 19)) caps |= EXR_SIMD_SSE41;  /* ECX.19 */
    have_osxsave = (r[2] & (1 << 27)) != 0;        /* ECX.27 */
    have_avx = (r[2] & (1 << 28)) != 0;            /* ECX.28 */
    {
        int os_ymm = 0;
        if (have_osxsave) os_ymm = (xgetbv0() & 0x6) == 0x6;
        if (have_avx && os_ymm) {
            if (r[2] & (1 << 29)) caps |= CAP_F16C; /* ECX.29 */
            if (max_leaf >= 7) {
                cpuidex(r, 7, 0);
                if (r[1] & (1 << 5)) caps |= EXR_SIMD_AVX2; /* EBX.5 */
            }
        }
    }
    return caps;
}
#endif /* EXR_X86 */

static uint32_t cpu_caps_cached(void) {
    static int ready = 0;
    static uint32_t caps = 0;
    if (!ready) {
#if defined(EXR_X86)
        caps = detect_caps();
#elif defined(EXR_NEON)
        caps = EXR_SIMD_NEON;
#else
        caps = 0;
#endif
        ready = 1;
    }
    return caps;
}

uint32_t exr_cpu_caps(void) { return cpu_caps_cached(); }

/* The dispatch table (scalar by default; init upgrades it). */
exr_simd_vtbl exr_simd = {0, 0, 0};

void exr_simd_init(void) {
    static int done = 0;
    uint32_t caps;
    if (done) return;
    caps = cpu_caps_cached();

    exr_simd.half_to_float = exr_half_to_float_scalar;
    exr_simd.float_to_half = exr_float_to_half_scalar;
    exr_simd.interleave = exr_interleave_scalar;

#if defined(EXR_X86)
    if (caps & EXR_SIMD_SSE2) exr_simd.interleave = exr_interleave_sse2;
    if (caps & EXR_SIMD_AVX2) exr_simd.interleave = exr_interleave_avx2;
    if (caps & CAP_F16C) {
        exr_simd.half_to_float = exr_half_to_float_f16c;
        exr_simd.float_to_half = exr_float_to_half_f16c;
    }
#endif
#if defined(EXR_NEON)
    exr_simd.interleave = exr_interleave_neon;
#endif
    done = 1;
}

void exr_simd_force(int level) {
    exr_simd.half_to_float = exr_half_to_float_scalar;
    exr_simd.float_to_half = exr_float_to_half_scalar;
    exr_simd.interleave = exr_interleave_scalar;
#if defined(EXR_X86)
    {
        uint32_t caps = cpu_caps_cached();
        if (level >= 1 && (caps & EXR_SIMD_SSE2))
            exr_simd.interleave = exr_interleave_sse2;
        if (level >= 2 && (caps & EXR_SIMD_AVX2))
            exr_simd.interleave = exr_interleave_avx2;
        if (level >= 2 && (caps & CAP_F16C)) {
            exr_simd.half_to_float = exr_half_to_float_f16c;
            exr_simd.float_to_half = exr_float_to_half_f16c;
        }
    }
#elif defined(EXR_NEON)
    if (level >= 1) exr_simd.interleave = exr_interleave_neon;
#else
    (void)level;
#endif
}

uint32_t exr_simd_capabilities(void) {
    return cpu_caps_cached() &
           (EXR_SIMD_SSE2 | EXR_SIMD_SSE41 | EXR_SIMD_AVX2 | EXR_SIMD_NEON);
}

const char *exr_simd_info(void) {
    uint32_t c = cpu_caps_cached();
#if defined(EXR_NEON)
    (void)c;
    return "neon";
#else
    if (c & EXR_SIMD_AVX2) return (c & CAP_F16C) ? "avx2+f16c" : "avx2";
    if (c & EXR_SIMD_SSE41) return "sse4.1";
    if (c & EXR_SIMD_SSE2) return "sse2";
    return "scalar";
#endif
}
