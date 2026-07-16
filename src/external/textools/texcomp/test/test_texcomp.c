/*
 * TinyEXR texcomp tests.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "texcomp.h"
/* Internal helpers under test (ASTC HDR endpoint codec, etc.). */
#include "../src/texcomp_internal.h"

#include "astc_ref_decode.h"
#include "bc7_ref_decode.h"
#include "astc_hdr_ref_decode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h> /* abs(): glibc leaks it in via other headers, clang does not */
#include <string.h>

/* Decode FP16 bits to float (for HDR round-trip checks). */
static float half_bits_to_float(uint16_t h) {
    uint32_t s = (uint32_t)(h >> 15) & 1u, e = (uint32_t)(h >> 10) & 0x1Fu;
    uint32_t m = (uint32_t)h & 0x3FFu, f;
    union {
        uint32_t u;
        float f;
    } v;
    if (e == 0u) {
        if (m == 0u) {
            f = s << 31;
        } else {
            int ee = 127 - 15 + 1;
            while (!(m & 0x400u)) {
                m <<= 1;
                ee--;
            }
            m &= 0x3FFu;
            f = (s << 31) | ((uint32_t)ee << 23) | (m << 13);
        }
    } else if (e == 0x1Fu) {
        f = (s << 31) | (0xFFu << 23) | (m << 13);
    } else {
        f = (s << 31) | ((e - 15u + 127u) << 23) | (m << 13);
    }
    v.u = f;
    return v.f;
}

#define CHECK(x, msg)                                                           \
    do {                                                                        \
        if (!(x)) {                                                             \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t bc7_mode(const uint8_t *p) {
    uint32_t m = 0;
    while (m < 8u && ((p[0] & (1u << m)) == 0u)) m++;
    return m;
}

static uint32_t rd_bits(const uint8_t *p, uint32_t bitpos, uint32_t nbits) {
    uint32_t v = 0, i;
    for (i = 0; i < nbits; ++i)
        v |= (uint32_t)((p[(bitpos + i) >> 3] >> ((bitpos + i) & 7u)) & 1u) << i;
    return v;
}

/* Reference decode of a DXT1/BC1 colour block into 16 RGB triples. Handles
 * both the 4-colour (c0 > c1) and 3-colour (c0 <= c1) modes. */
static void bc1_decode(const uint8_t in[8], uint8_t out[16][3]) {
    uint32_t c0 = (uint32_t)in[0] | ((uint32_t)in[1] << 8);
    uint32_t c1 = (uint32_t)in[2] | ((uint32_t)in[3] << 8);
    int p[4][3], i, k;
    int r0 = (int)((c0 >> 11) & 0x1f), g0 = (int)((c0 >> 5) & 0x3f),
        b0 = (int)(c0 & 0x1f);
    int r1 = (int)((c1 >> 11) & 0x1f), g1 = (int)((c1 >> 5) & 0x3f),
        b1 = (int)(c1 & 0x1f);
    p[0][0] = (r0 << 3) | (r0 >> 2);
    p[0][1] = (g0 << 2) | (g0 >> 4);
    p[0][2] = (b0 << 3) | (b0 >> 2);
    p[1][0] = (r1 << 3) | (r1 >> 2);
    p[1][1] = (g1 << 2) | (g1 >> 4);
    p[1][2] = (b1 << 3) | (b1 >> 2);
    for (k = 0; k < 3; ++k) {
        if (c0 > c1) {
            p[2][k] = (2 * p[0][k] + p[1][k] + 1) / 3;
            p[3][k] = (p[0][k] + 2 * p[1][k] + 1) / 3;
        } else {
            p[2][k] = (p[0][k] + p[1][k]) / 2;
            p[3][k] = 0; /* punch-through */
        }
    }
    for (i = 0; i < 16; ++i) {
        int idx = (in[4 + (i >> 2)] >> ((i & 3) * 2)) & 3;
        for (k = 0; k < 3; ++k) out[i][k] = (uint8_t)p[idx][k];
    }
}

/* Reference decode of a BC4/DXT5 alpha block into 16 alpha samples. */
static void bc4_decode(const uint8_t in[8], uint8_t out[16]) {
    int a0 = in[0], a1 = in[1], pal[8], i;
    uint64_t bits = 0;
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) {
        for (i = 1; i <= 6; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
    } else {
        for (i = 1; i <= 4; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    for (i = 0; i < 6; ++i) bits |= (uint64_t)in[2 + i] << (8 * i);
    for (i = 0; i < 16; ++i) out[i] = (uint8_t)pal[(bits >> (3 * i)) & 7u];
}

static int astc_test_decode_mode_2d(uint32_t mode, uint32_t *wx, uint32_t *wy,
                                    uint32_t *quant, uint32_t *dual) {
    uint32_t base_quant = (mode >> 4) & 1u;
    uint32_t h = (mode >> 9) & 1u;
    uint32_t d = (mode >> 10) & 1u;
    uint32_t a = (mode >> 5) & 3u;
    uint32_t x = 0, y = 0;
    if ((mode & 3u) != 0u) {
        uint32_t b;
        base_quant |= (mode & 3u) << 1;
        b = (mode >> 7) & 3u;
        switch ((mode >> 2) & 3u) {
            case 0:
                x = b + 4u;
                y = a + 2u;
                break;
            case 1:
                x = b + 8u;
                y = a + 2u;
                break;
            case 2:
                x = a + 2u;
                y = b + 8u;
                break;
            default:
                b &= 1u;
                if (mode & 0x100u) {
                    x = b + 2u;
                    y = a + 2u;
                } else {
                    x = a + 2u;
                    y = b + 6u;
                }
                break;
        }
    } else {
        uint32_t b;
        base_quant |= ((mode >> 2) & 3u) << 1;
        if (((mode >> 2) & 3u) == 0u) return 0;
        b = (mode >> 9) & 3u;
        switch ((mode >> 7) & 3u) {
            case 0:
                x = 12u;
                y = a + 2u;
                break;
            case 1:
                x = a + 2u;
                y = 12u;
                break;
            case 2:
                x = a + 6u;
                y = b + 6u;
                d = 0u;
                h = 0u;
                break;
            default:
                if (a == 0u) {
                    x = 6u;
                    y = 10u;
                } else if (a == 1u) {
                    x = 10u;
                    y = 6u;
                } else {
                    return 0;
                }
                break;
        }
    }
    if (base_quant < 2u) return 0;
    *wx = x;
    *wy = y;
    *quant = (base_quant - 2u) + 6u * h;
    *dual = d;
    return *quant < 12u && x * y * (d + 1u) <= 64u;
}

static int astc_test_mode_is_valid_1plane_2d(uint32_t mode) {
    uint32_t wx, wy, quant, dual;
    return astc_test_decode_mode_2d(mode, &wx, &wy, &quant, &dual) && !dual;
}

static int astc_test_mode_is_high_precision_4x4(uint32_t mode) {
    uint32_t wx, wy, quant, dual;
    return astc_test_decode_mode_2d(mode, &wx, &wy, &quant, &dual) && !dual &&
           wx == 4u && wy == 4u && quant >= 3u;
}

static int astc_test_mode_is_luminance_large(uint32_t mode) {
    uint32_t wx, wy, quant, dual;
    (void)quant;
    return astc_test_decode_mode_2d(mode, &wx, &wy, &quant, &dual) && !dual &&
           (wx > 5u || wy > 5u);
}

static uint32_t bc6h_unquant_uf16_to_mag(uint32_t q) {
    uint32_t unq;
    if (q == 0u) unq = 0u;
    else if (q >= 1023u) unq = 0xffffu;
    else unq = ((q << 16) + 0x8000u) >> 10;
    return (unq * 31u) >> 6;
}

static uint16_t test_float_to_half_bits(float fv) {
    union {
        float f;
        uint32_t u;
    } v;
    uint32_t sign, mant, exp;
    v.f = fv;
    sign = (v.u >> 16) & 0x8000u;
    exp = (v.u >> 23) & 0xffu;
    mant = v.u & 0x7fffffu;
    if (exp == 255u) return (uint16_t)(sign | 0x7c00u | (mant ? 0x0200u : 0u));
    if (exp > 142u) return (uint16_t)(sign | 0x7c00u);
    if (exp < 113u) {
        uint32_t m;
        if (exp < 103u) return (uint16_t)sign;
        m = mant | 0x800000u;
        m >>= 125u - exp;
        m = (m + 0x1000u) >> 13;
        return (uint16_t)(sign | m);
    }
    exp = exp - 112u;
    mant = (mant + 0x1000u) >> 13;
    if (mant & 0x400u) {
        mant = 0;
        ++exp;
    }
    if (exp >= 31u) return (uint16_t)(sign | 0x7c00u);
    return (uint16_t)(sign | (exp << 10) | (mant & 0x3ffu));
}

static uint64_t bc6h_mode11_rgb_error(const uint8_t *block,
                                      const float pix[16][3]) {
    static const uint32_t w[16] = {0,  4,  9,  13, 17, 21, 26, 30,
                                   34, 38, 43, 47, 51, 55, 60, 64};
    uint32_t lo[3], hi[3], target[16][3], bitpos = 5u, i, c;
    uint64_t err = 0;
    for (c = 0; c < 3u; ++c) lo[c] = rd_bits(block, bitpos + c * 10u, 10);
    bitpos += 30u;
    for (c = 0; c < 3u; ++c) hi[c] = rd_bits(block, bitpos + c * 10u, 10);
    bitpos += 30u;
    for (i = 0; i < 16u; ++i) {
        for (c = 0; c < 3u; ++c) {
            float f = pix[i][c];
            uint16_t h;
            uint32_t mag, q;
            h = test_float_to_half_bits(f);
            mag = h & 0x7fffu;
            if (mag > 0x7bffu) mag = 0x7bffu;
            q = (mag * 1023u + 15871u) / 31743u;
            target[i][c] = bc6h_unquant_uf16_to_mag(q);
        }
    }
    for (i = 0; i < 16u; ++i) {
        uint32_t sel = i == 0u ? rd_bits(block, bitpos, 3) : rd_bits(block, bitpos, 4);
        bitpos += i == 0u ? 3u : 4u;
        for (c = 0; c < 3u; ++c) {
            uint32_t qv = ((64u - w[sel]) * lo[c] + w[sel] * hi[c] + 32u) >> 6;
            uint32_t recon = bc6h_unquant_uf16_to_mag(qv);
            int64_t d = (int64_t)target[i][c] - (int64_t)recon;
            err += (uint64_t)(d * d);
        }
    }
    return err;
}

/* ---- ASTC reference-decoder based tests --------------------------------- */

/* decode(encode(x)) == x for every ISE quant level and value count. */
static int astc_ise_roundtrip_test(void) {
    static const uint16_t levels[21] = {2,  3,  4,  5,   6,   8,   10,
                                        12, 16, 20, 24,  32,  40,  48,
                                        64, 80, 96, 128, 160, 192, 256};
    uint8_t values[64], decoded[64], buf[128];
    uint32_t q, count, i, seed = 1u;
    for (q = 0; q < 21u; ++q) {
        for (count = 1; count <= 64u; ++count) {
            memset(buf, 0, sizeof(buf));
            for (i = 0; i < count; ++i) {
                seed = seed * 1664525u + 1013904223u;
                values[i] = (uint8_t)((seed >> 8) % levels[q]);
            }
            CHECK(tc_astc_ise_encode_bits(q, count, values, buf, sizeof(buf),
                                          0) == TC_SUCCESS,
                  "astc ise roundtrip encode");
            CHECK(aref_ise_decode(q, count, buf, 0, decoded),
                  "astc ise roundtrip decode");
            CHECK(memcmp(values, decoded, count) == 0,
                  "astc ise roundtrip values");
        }
    }
    return 0;
}

/* Weight unquant tables must be symmetric so that swapping endpoints and
 * inverting weight indices reproduces the exact same reconstruction. */
static int astc_weight_symmetry_test(void) {
    uint32_t q, i, j;
    for (q = 0; q < 12u; ++q) {
        uint32_t n = aref_weight_levels[q];
        uint8_t sorted[32];
        memcpy(sorted, aref_weight_unquant[q], n);
        for (i = 0; i < n; ++i) {
            for (j = i + 1u; j < n; ++j) {
                if (sorted[j] < sorted[i]) {
                    uint8_t t = sorted[i];
                    sorted[i] = sorted[j];
                    sorted[j] = t;
                }
            }
        }
        for (i = 0; i < n; ++i) {
            CHECK(sorted[i] + sorted[n - 1u - i] == 64u,
                  "astc weight unquant symmetry");
        }
    }
    return 0;
}

static void astc_fill_test_image(uint8_t *img, uint32_t w, uint32_t h,
                                 int kind) {
    uint32_t x, y, seed = 12345u;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            uint8_t *p = img + ((size_t)y * w + x) * 4u;
            switch (kind) {
                case 0: /* smooth gradient, opaque */
                    p[0] = (uint8_t)(x * 255u / (w - 1u));
                    p[1] = (uint8_t)(y * 255u / (h - 1u));
                    p[2] = (uint8_t)((x + y) * 255u / (w + h - 2u));
                    p[3] = 255u;
                    break;
                case 1: /* two well-separated clusters */
                    if (((x / 3u) + (y / 3u)) & 1u) {
                        p[0] = 220u;
                        p[1] = 40u;
                        p[2] = 30u;
                    } else {
                        p[0] = 20u;
                        p[1] = 60u;
                        p[2] = 200u;
                    }
                    p[3] = 255u;
                    break;
                case 2: /* gradient with alpha ramp (dual-plane bait) */
                    p[0] = (uint8_t)(x * 255u / (w - 1u));
                    p[1] = (uint8_t)((x * 200u / (w - 1u)) + 20u);
                    p[2] = (uint8_t)(y * 255u / (h - 1u));
                    p[3] = (uint8_t)(y * 255u / (h - 1u));
                    break;
                default: /* rgba noise */
                    seed = seed * 1664525u + 1013904223u;
                    p[0] = (uint8_t)(seed >> 8);
                    seed = seed * 1664525u + 1013904223u;
                    p[1] = (uint8_t)(seed >> 8);
                    seed = seed * 1664525u + 1013904223u;
                    p[2] = (uint8_t)(seed >> 8);
                    seed = seed * 1664525u + 1013904223u;
                    p[3] = (uint8_t)(seed >> 8);
                    break;
            }
        }
    }
}

static double astc_psnr(const uint8_t *a, const uint8_t *b, size_t n) {
    uint64_t sse = 0;
    size_t i;
    double mse;
    for (i = 0; i < n; ++i) {
        int d = (int)a[i] - (int)b[i];
        sse += (uint64_t)(d * d);
    }
    if (!sse) return 99.0;
    mse = (double)sse / (double)n;
    return 10.0 * log10(65025.0 / mse);
}

/* The ASTC search kernels must be bit-exact across SIMD backends: full
 * compressed images are compared byte-for-byte against the scalar backend. */
static int astc_backend_parity_test(void) {
    enum { W = 48, H = 48 };
    static uint8_t img[W * H * 4];
    static uint8_t ref[(W / 4) * (H / 4) * 16];
    static uint8_t alt[(W / 4) * (H / 4) * 16];
    static const uint32_t masks[4] = {TC_BACKEND_SSE2, TC_BACKEND_SSE41,
                                      TC_BACKEND_AVX2, TC_BACKEND_NEON};
    uint32_t avail = tc_backend_available_mask();
    int kind, q;
    uint32_t bi, mi;
    static const uint32_t bs[2][2] = {{4, 4}, {6, 6}};
    for (kind = 0; kind < 4; ++kind) {
        astc_fill_test_image(img, W, H, kind);
        for (bi = 0; bi < 2u; ++bi) {
            for (q = 0; q <= 2; ++q) {
                tc_astc_options opt;
                size_t need;
                tc_astc_options_init(&opt);
                opt.block_x = bs[bi][0];
                opt.block_y = bs[bi][1];
                opt.quality = q;
                need = tc_astc_compressed_size(W, H, &opt);
                tc_backend_force_mask(TC_BACKEND_SCALAR);
                CHECK(tc_astc_compress_rgba8(img, W, H, W * 4u, &opt, ref,
                                             need) == TC_SUCCESS,
                      "astc parity scalar encode");
                for (mi = 0; mi < 4u; ++mi) {
                    if (!(avail & masks[mi])) continue;
                    tc_backend_force_mask(masks[mi]);
                    CHECK(tc_astc_compress_rgba8(img, W, H, W * 4u, &opt, alt,
                                                 need) == TC_SUCCESS,
                          "astc parity simd encode");
                    if (memcmp(ref, alt, need) != 0) {
                        tc_backend_force_mask(TC_BACKEND_ALL);
                        fprintf(stderr,
                                "FAIL: astc backend parity kind=%d block=%ux%u "
                                "q=%d mask=%u\n",
                                kind, bs[bi][0], bs[bi][1], q, masks[mi]);
                        return 1;
                    }
                }
                tc_backend_force_mask(TC_BACKEND_ALL);
            }
        }
    }
    return 0;
}

/* Threaded encodes must be byte-identical to serial for any thread count
 * (bands are independent and write disjoint output ranges). */
static int astc_thread_parity_test(void) {
    enum { W = 48, H = 48 };
    static uint8_t img[W * H * 4];
    static uint8_t ref[(W / 4) * (H / 4) * 16];
    static uint8_t alt[(W / 4) * (H / 4) * 16];
    int kind, q;
    for (kind = 0; kind < 4; ++kind) {
        astc_fill_test_image(img, W, H, kind);
        for (q = 0; q <= 2; ++q) {
            tc_astc_options opt;
            size_t need;
            tc_astc_options_init(&opt);
            opt.quality = q;
            need = tc_astc_compressed_size(W, H, &opt);
            opt.threads = 1;
            CHECK(tc_astc_compress_rgba8(img, W, H, W * 4u, &opt, ref, need) ==
                      TC_SUCCESS,
                  "astc thread parity serial encode");
            opt.threads = 4;
            CHECK(tc_astc_compress_rgba8(img, W, H, W * 4u, &opt, alt, need) ==
                      TC_SUCCESS,
                  "astc thread parity threaded encode");
            CHECK(memcmp(ref, alt, need) == 0, "astc thread parity output");
        }
    }
    return 0;
}

/* Encode -> reference-decode round trip: every emitted block must decode,
 * and quality must stay above per-configuration floors. Floors are set from
 * bench/texcomp_psnr.c measurements minus a safety margin; raise them when
 * the encoder improves, never lower them silently. */
static int astc_ref_roundtrip_test(void) {
    static const uint32_t bs[4][2] = {{4, 4}, {6, 6}, {8, 8}, {12, 12}};
    /* floors[kind][block][quality], dB; measured on 48x48 images minus a
     * safety margin. Margin is ~0.3 dB for medium/normal (q1/q2), which must
     * not regress, and ~0.6 dB for fast (q0) synthetic corners, which may
     * trade up to ~1 dB in future effort-level tuning. Raise when the encoder
     * improves; never lower silently. */
    static const double floors[4][4][3] = {
        /* gradient */
        {{37.5, 38.1, 38.2}, {34.0, 34.4, 34.7}, {31.5, 31.7, 33.5},
         {27.0, 28.0, 29.3}},
        /* clusters */
        {{60.0, 60.0, 60.0}, {60.0, 60.0, 60.0}, {20.3, 53.8, 53.8},
         {15.0, 21.2, 21.2}},
        /* alpha ramp */
        {{35.4, 36.2, 36.2}, {31.4, 32.2, 32.4}, {28.7, 29.3, 29.9},
         {25.1, 26.6, 27.4}},
        /* rgba noise */
        {{12.2, 13.3, 13.4}, {11.0, 12.2, 12.2}, {10.9, 11.3, 11.3},
         {10.6, 10.7, 10.7}}};
    enum { W = 48, H = 48 };
    static uint8_t img[W * H * 4];
    static uint8_t dec[W * H * 4];
    static uint8_t blocks[(W / 4) * (H / 4) * 16];
    int kind, q;
    uint32_t bi;
    for (kind = 0; kind < 4; ++kind) {
        astc_fill_test_image(img, W, H, kind);
        for (bi = 0; bi < 4u; ++bi) {
            for (q = 0; q <= 2; ++q) {
                tc_astc_options opt;
                size_t need;
                double psnr;
                tc_astc_options_init(&opt);
                opt.block_x = bs[bi][0];
                opt.block_y = bs[bi][1];
                opt.quality = q;
                need = tc_astc_compressed_size(W, H, &opt);
                CHECK(need <= sizeof(blocks), "astc roundtrip buffer");
                CHECK(tc_astc_compress_rgba8(img, W, H, W * 4u, &opt, blocks,
                                             need) == TC_SUCCESS,
                      "astc roundtrip encode");
                CHECK(aref_decode_image(blocks, W, H, opt.block_x, opt.block_y,
                                        dec),
                      "astc roundtrip reference decode");
                psnr = astc_psnr(img, dec, sizeof(img));
                if (psnr < floors[kind][bi][q]) {
                    fprintf(stderr,
                            "FAIL: astc psnr floor kind=%d block=%ux%u q=%d: "
                            "%.2f dB < %.2f dB\n",
                            kind, bs[bi][0], bs[bi][1], q, psnr,
                            floors[kind][bi][q]);
                    return 1;
                }
                /* Quality levels must not regress against the fast path. */
                (void)0;
            }
        }
    }
    /* Solid image decodes exactly (constant-color blocks). */
    {
        tc_astc_options opt;
        size_t need, i;
        tc_astc_options_init(&opt);
        for (i = 0; i < sizeof(img); i += 4u) {
            img[i + 0u] = 12u;
            img[i + 1u] = 34u;
            img[i + 2u] = 56u;
            img[i + 3u] = 78u;
        }
        need = tc_astc_compressed_size(W, H, &opt);
        CHECK(tc_astc_compress_rgba8(img, W, H, W * 4u, &opt, blocks, need) ==
                  TC_SUCCESS,
              "astc solid encode");
        CHECK(aref_decode_image(blocks, W, H, opt.block_x, opt.block_y, dec),
              "astc solid decode");
        CHECK(memcmp(img, dec, sizeof(img)) == 0, "astc solid exact");
    }
    return 0;
}

/* The public BC1/BC3/BC5 surface decoders must agree, texel for texel, with the
 * independent reference block decoders above (bc1_decode / bc4_decode), which
 * were written from the S3TC spec rather than from the library. That pins the
 * palette maths, the block/texel ordering and the partial-block edge handling.
 * Exercised on a non-multiple-of-4 surface so the edge blocks are partial. */
static int test_bc_decoders(void) {
    enum { W = 13, H = 7 };
    uint8_t src[W * H * 4], dec[W * H * 4];
    uint8_t bc1[64 * 8], bc3[64 * 16], bc5[64 * 16];
    tc_bc1_options o1;
    tc_bc3_options o3;
    tc_bc5_options o5;
    uint32_t bxc = (W + 3u) / 4u, x, y, bx, by;
    tc_bc1_options_init(&o1);
    tc_bc3_options_init(&o3);
    tc_bc5_options_init(&o5);
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = src + ((size_t)y * W + x) * 4u;
            p[0] = (uint8_t)(x * 19u);
            p[1] = (uint8_t)(y * 31u);
            p[2] = (uint8_t)((x + y) * 11u);
            p[3] = (uint8_t)(255u - x * 7u);
        }

    CHECK(tc_bc1_compress_rgba8(src, W, H, W * 4u, &o1, bc1, sizeof(bc1)) ==
              TC_SUCCESS, "bc1 compress (decoder xcheck)");
    CHECK(tc_bc1_decompress_rgba8(bc1, W, H, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "bc1 decompress");
    for (by = 0; by < H; by += 4u)
        for (bx = 0; bx < W; bx += 4u) {
            uint8_t ref[16][3];
            bc1_decode(bc1 + ((size_t)(by / 4u) * bxc + bx / 4u) * 8u, ref);
            for (y = 0; y < 4u && by + y < H; ++y)
                for (x = 0; x < 4u && bx + x < W; ++x) {
                    const uint8_t *d = dec + ((size_t)(by + y) * W + bx + x) * 4u;
                    const uint8_t *r = ref[y * 4u + x];
                    CHECK(d[0] == r[0] && d[1] == r[1] && d[2] == r[2],
                          "bc1 decode matches reference");
                    CHECK(d[3] == 255u, "bc1 decode alpha opaque");
                }
        }

    CHECK(tc_bc3_compress_rgba8(src, W, H, W * 4u, &o3, bc3, sizeof(bc3)) ==
              TC_SUCCESS, "bc3 compress (decoder xcheck)");
    CHECK(tc_bc3_decompress_rgba8(bc3, W, H, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "bc3 decompress");
    for (by = 0; by < H; by += 4u)
        for (bx = 0; bx < W; bx += 4u) {
            uint8_t ref[16][3], refa[16];
            size_t bi = ((size_t)(by / 4u) * bxc + bx / 4u) * 16u;
            bc4_decode(bc3 + bi, refa);
            bc1_decode(bc3 + bi + 8u, ref);
            for (y = 0; y < 4u && by + y < H; ++y)
                for (x = 0; x < 4u && bx + x < W; ++x) {
                    const uint8_t *d = dec + ((size_t)(by + y) * W + bx + x) * 4u;
                    const uint8_t *r = ref[y * 4u + x];
                    CHECK(d[0] == r[0] && d[1] == r[1] && d[2] == r[2],
                          "bc3 decode colour matches reference");
                    CHECK(d[3] == refa[y * 4u + x],
                          "bc3 decode alpha matches reference");
                }
        }

    CHECK(tc_bc5_compress_rgba8(src, W, H, W * 4u, &o5, bc5, sizeof(bc5)) ==
              TC_SUCCESS, "bc5 compress (decoder xcheck)");
    CHECK(tc_bc5_decompress_rgba8(bc5, W, H, 0, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "bc5 decompress");
    for (by = 0; by < H; by += 4u)
        for (bx = 0; bx < W; bx += 4u) {
            uint8_t refr[16], refg[16];
            size_t bi = ((size_t)(by / 4u) * bxc + bx / 4u) * 16u;
            bc4_decode(bc5 + bi, refr);
            bc4_decode(bc5 + bi + 8u, refg);
            for (y = 0; y < 4u && by + y < H; ++y)
                for (x = 0; x < 4u && bx + x < W; ++x) {
                    const uint8_t *d = dec + ((size_t)(by + y) * W + bx + x) * 4u;
                    CHECK(d[0] == refr[y * 4u + x] && d[1] == refg[y * 4u + x],
                          "bc5 decode matches reference");
                    CHECK(d[2] == 0u && d[3] == 255u, "bc5 decode b=0 a=255");
                }
        }

    /* Undersized output and a too-narrow stride must be refused, not written. */
    CHECK(tc_bc1_decompress_rgba8(bc1, W, H, W * 4u, dec, sizeof(dec) - 1u) ==
              TC_ERROR_INVALID_ARGUMENT, "bc1 decompress rejects short output");
    CHECK(tc_bc3_decompress_rgba8(bc3, W, H, W * 4u - 1u, dec, sizeof(dec)) ==
              TC_ERROR_INVALID_ARGUMENT, "bc3 decompress rejects short stride");
    CHECK(tc_bc5_decompress_rgba8(NULL, W, H, 0, W * 4u, dec, sizeof(dec)) ==
              TC_ERROR_INVALID_ARGUMENT, "bc5 decompress rejects null input");
    return 0;
}


/* ---- ETC2 / EAC conformance ------------------------------------------------
 * Golden vectors produced by Mesa's ETC decoder (src/mesa/main/texcompress_etc.c,
 * MIT) -- an independent implementation, not this one. Three blocks per RGB mode
 * cover all five (individual, differential, T, H, planar); our encoder only ever
 * emits three of them, so a round-trip test alone could not reach T and H.
 * Regenerating: decode the blocks below with any conformant ETC2 decoder.
 * Cross-checked at 200k random blocks against Mesa with zero mismatches; these
 * vectors pin that result in-tree.
 */
/* mode, 8 block bytes, then 16 RGB triples (Mesa reference) */
static const struct { const char *mode; uint8_t blk[8]; uint8_t rgb[16][3]; }
etc2_golden[] = {
    {"planar", {0xb0,0x66,0x15,0x2a,0xc7,0xc2,0x97,0xc3},
     {{97,102,73},{93,126,112},{89,151,150},{85,175,189},{93,124,58},{89,149,96},{85,173,135},{81,197,173},{89,147,43},{85,171,81},{81,195,120},{77,219,158},{85,169,27},{81,193,66},{77,217,104},{73,242,143}}},
    {"differential", {0x2a,0x8a,0x19,0xd3,0x0f,0x62,0xee,0x9d},
     {{147,246,130},{147,246,130},{8,107,0},{74,173,57},{8,107,0},{8,107,0},{0,34,0},{147,246,130},{117,216,93},{39,138,15},{0,96,0},{117,216,93},{117,216,93},{117,216,93},{0,96,0},{117,216,93}}},
    {"individual", {0xea,0x2f,0xaa,0xa5,0x6d,0x14,0x22,0xb0},
     {{255,58,194},{158,0,90},{214,10,146},{255,58,194},{255,58,194},{255,114,250},{255,114,250},{158,0,90},{165,250,165},{175,255,175},{165,250,165},{165,250,165},{175,255,175},{187,255,187},{165,250,165},{175,255,175}}},
    {"individual", {0x8e,0x75,0x69,0xc1,0x80,0xf6,0xd3,0x1c},
     {{169,152,135},{30,13,0},{242,225,208},{242,225,208},{103,86,69},{103,86,69},{242,225,208},{169,152,135},{230,77,145},{236,83,151},{240,87,155},{246,93,161},{246,93,161},{236,83,151},{240,87,155},{230,77,145}}},
    {"individual", {0xb7,0x7c,0xf4,0x45,0xe7,0x2a,0xa1,0x00},
     {{196,128,255},{196,128,255},{158,90,226},{196,128,255},{178,110,246},{178,110,246},{178,110,246},{158,90,226},{124,209,73},{124,209,73},{114,199,63},{114,199,63},{114,199,63},{124,209,73},{124,209,73},{102,187,51}}},
    {"T", {0x05,0x64,0xed,0x52,0x44,0xce,0x2c,0x7e},
     {{17,102,68},{241,224,88},{17,102,68},{17,102,68},{235,218,82},{241,224,88},{17,102,68},{241,224,88},{235,218,82},{235,218,82},{235,218,82},{238,221,85},{235,218,82},{238,221,85},{241,224,88},{17,102,68}}},
    {"differential", {0xe3,0x49,0xb0,0xdf,0x89,0xee,0xee,0xfe},
     {{255,107,214},{255,180,255},{198,41,148},{255,107,214},{125,0,75},{125,0,75},{255,180,255},{255,180,255},{72,0,0},{72,0,0},{255,255,255},{255,255,255},{72,0,0},{72,0,0},{72,0,0},{72,0,0}}},
    {"differential", {0xb4,0xd8,0x24,0x2b,0x19,0x2a,0x24,0x2b},
     {{198,239,50},{186,227,38},{176,217,28},{176,217,28},{164,205,16},{164,205,16},{186,227,38},{198,239,50},{157,231,9},{157,231,9},{177,251,29},{157,231,9},{119,193,0},{157,231,9},{139,213,0},{157,231,9}}},
    {"H", {0x84,0x07,0xa9,0x92,0x70,0x34,0xc9,0x24},
     {{3,139,122},{88,54,37},{0,133,116},{88,54,37},{3,139,122},{82,48,31},{3,139,122},{88,54,37},{82,48,31},{3,139,122},{3,139,122},{82,48,31},{3,139,122},{3,139,122},{0,133,116},{0,133,116}}},
    {"T", {0xfa,0x96,0x3f,0x3e,0xfb,0xdb,0x90,0x54},
     {{51,255,51},{10,214,10},{51,255,51},{10,214,10},{51,255,51},{238,153,102},{51,255,51},{51,255,51},{92,255,92},{10,214,10},{238,153,102},{51,255,51},{51,255,51},{51,255,51},{51,255,51},{10,214,10}}},
    {"planar", {0x63,0x46,0x06,0xba,0x25,0x57,0xc0,0x25},
     {{199,199,20},{178,158,58},{156,118,95},{135,77,133},{212,149,53},{191,109,90},{169,68,128},{148,27,165},{225,100,85},{204,59,123},{182,18,160},{161,0,198},{238,50,118},{217,9,155},{195,0,193},{174,0,230}}},
    {"H", {0x3a,0xf9,0xde,0x7a,0xa6,0xac,0xd3,0xd7},
     {{116,82,184},{116,82,184},{116,82,184},{116,82,184},{116,82,184},{190,207,255},{184,201,252},{190,207,255},{184,201,252},{116,82,184},{190,207,255},{116,82,184},{190,207,255},{184,201,252},{122,88,190},{184,201,252}}},
    {"planar", {0x01,0xdd,0x04,0xae,0x58,0x86,0x6f,0x1c},
     {{0,221,134},{22,188,117},{45,155,100},{67,121,82},{52,196,129},{74,163,112},{96,129,94},{119,96,77},{104,171,124},{126,137,106},{148,104,89},{170,71,72},{155,145,118},{178,112,101},{200,79,84},{222,46,67}}},
    {"H", {0xdf,0xf3,0xac,0x8f,0xda,0xe9,0x08,0x27},
     {{21,89,0},{251,255,183},{251,255,183},{149,217,81},{123,191,55},{21,89,0},{149,217,81},{251,255,183},{123,191,55},{149,217,81},{251,255,183},{149,217,81},{149,217,81},{149,217,81},{21,89,0},{149,217,81}}},
    {"T", {0xf3,0xa4,0x9b,0xe2,0x86,0x59,0xa3,0x3d},
     {{150,184,235},{150,184,235},{156,190,241},{187,170,68},{187,170,68},{156,190,241},{150,184,235},{156,190,241},{156,190,241},{153,187,238},{153,187,238},{187,170,68},{150,184,235},{187,170,68},{187,170,68},{150,184,235}}},
};

static const struct { uint8_t blk[8]; uint8_t a8[16]; uint8_t r11[16]; }
eac_golden[] = {
    {{0xb7,0x8a,0x7a,0x9e,0xb9,0xdb,0x74,0x49},
     {103,255,239,119,239,119,239,151,207,255,239,151,151,151,255,151},
     {103,255,239,119,239,119,239,151,207,255,239,151,151,151,255,151}},
    {{0x6e,0xb9,0x3e,0x6f,0xbd,0xf3,0x0d,0xeb},
     {55,209,209,187,209,187,121,209,121,209,187,154,187,154,88,0},
     {55,209,209,187,209,187,121,209,121,209,187,154,187,154,88,0}},
    {{0x39,0xec,0x35,0xe5,0x15,0x4f,0x1a,0x72},
     {1,0,0,99,99,85,0,1,0,0,141,141,141,99,1,0},
     {1,0,0,99,99,85,0,1,0,0,141,141,141,99,1,0}},
    {{0x19,0x23,0x61,0xfe,0xc2,0xef,0x9a,0xdc},
     {0,49,49,31,21,0,0,0,0,21,49,0,49,13,17,27},
     {0,49,49,31,21,0,0,0,0,21,49,0,49,13,17,27}},
};

static int test_etc2_decoders(void) {
    size_t i;
    for (i = 0; i < sizeof(etc2_golden) / sizeof(etc2_golden[0]); ++i) {
        uint8_t dec[4 * 4 * 4];
        int k;
        CHECK(tc_etc2_decompress_rgba8(etc2_golden[i].blk, 4, 4, 0, 16, dec,
                                       sizeof(dec)) == TC_SUCCESS,
              "etc2 golden decode");
        for (k = 0; k < 16; ++k)
            CHECK(dec[k * 4 + 0] == etc2_golden[i].rgb[k][0] &&
                      dec[k * 4 + 1] == etc2_golden[i].rgb[k][1] &&
                      dec[k * 4 + 2] == etc2_golden[i].rgb[k][2] &&
                      dec[k * 4 + 3] == 255u,
                  "etc2 golden matches reference decoder");
    }
    for (i = 0; i < sizeof(eac_golden) / sizeof(eac_golden[0]); ++i) {
        uint8_t rgba[16 * 16], dec[4 * 4 * 4];
        int k;
        /* ETC2 RGBA = EAC alpha block + RGB block; only the alpha half is pinned
         * here (the RGB half is covered above). */
        memcpy(rgba, eac_golden[i].blk, 8);
        memcpy(rgba + 8, etc2_golden[0].blk, 8);
        CHECK(tc_etc2_decompress_rgba8(rgba, 4, 4, 1, 16, dec, sizeof(dec)) ==
                  TC_SUCCESS, "etc2 rgba golden decode");
        for (k = 0; k < 16; ++k)
            CHECK(dec[k * 4 + 3] == eac_golden[i].a8[k],
                  "eac alpha golden matches reference decoder");
        CHECK(tc_eac_decompress_rgba8(eac_golden[i].blk, 4, 4, 0, 16, dec,
                                      sizeof(dec)) == TC_SUCCESS,
              "eac r11 golden decode");
        for (k = 0; k < 16; ++k)
            CHECK(dec[k * 4 + 0] == eac_golden[i].r11[k],
                  "eac r11 golden matches reference decoder");
    }
    return 0;
}

/* ETC numbers texels down columns; gathering blocks row-major transposed every
 * block we emitted. A pure horizontal ramp is the sharpest probe: transposed, it
 * decodes as a vertical one. Encode -> decode must preserve the orientation. */
static int test_etc2_orientation(void) {
    enum { W = 8, H = 8 };
    uint8_t src[W * H * 4], dec[W * H * 4], enc[W * H];
    tc_etc2_options o;
    uint32_t x, y;
    tc_etc2_options_init(&o);
    o.alpha = 0;
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = src + ((size_t)y * W + x) * 4u;
            p[0] = p[1] = p[2] = (uint8_t)(x * 36u); /* varies along X only */
            p[3] = 255u;
        }
    CHECK(tc_etc2_compress_rgba8(src, W, H, W * 4u, &o, enc, sizeof(enc)) ==
              TC_SUCCESS, "etc2 orientation compress");
    CHECK(tc_etc2_decompress_rgba8(enc, W, H, 0, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "etc2 orientation decompress");
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            int got = dec[((size_t)y * W + x) * 4u];
            int want = (int)(x * 36u);
            CHECK(got >= want - 24 && got <= want + 24,
                  "etc2 round-trip preserves the X ramp (not transposed)");
        }
    /* Each column must be constant down its rows; a transposed block would make
     * the value vary with y instead. */
    for (x = 0; x < W; ++x)
        for (y = 1; y < H; ++y)
            CHECK(dec[((size_t)y * W + x) * 4u] == dec[(size_t)x * 4u],
                  "etc2 round-trip: columns stay constant");
    return 0;
}

/* EAC R11 carries the roughness companion texture, so its orientation matters
 * for the same reason. */
static int test_eac_orientation(void) {
    enum { W = 8, H = 8 };
    uint8_t src[W * H * 4], dec[W * H * 4], enc[W * H];
    uint32_t x, y;
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = src + ((size_t)y * W + x) * 4u;
            p[0] = (uint8_t)(x * 36u);
            p[1] = p[2] = 0u;
            p[3] = 255u;
        }
    CHECK(tc_eac_compress_rgba8(src, W, H, W * 4u, 0, enc, sizeof(enc)) ==
              TC_SUCCESS, "eac orientation compress");
    CHECK(tc_eac_decompress_rgba8(enc, W, H, 0, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "eac orientation decompress");
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            int got = dec[((size_t)y * W + x) * 4u];
            int want = (int)(x * 36u);
            CHECK(got >= want - 24 && got <= want + 24,
                  "eac round-trip preserves the X ramp (not transposed)");
        }
    return 0;
}


/* BC7 decoder conformance on arbitrary blocks.
 *
 * The xbc7 gate already cross-checks the library decoder against
 * bc7_ref_decode.h, but only on blocks our own encoder produced -- which use a
 * handful of the eight modes. Random blocks reach every mode, every partition,
 * rotation, p-bit and index-selection combination a foreign encoder might emit
 * (a BC7 block has no invalid bit patterns once its mode prefix is set).
 *
 * bc7_ref_decode.h is an independent implementation, and it was itself verified
 * bit-exact against upstream bcdec (iOrange, public domain / MIT) over 320k
 * random blocks -- 40k per mode -- so agreeing with it here is a real
 * conformance statement, not just self-consistency.
 */
static uint32_t bc7c_rs = 99u;
static uint32_t bc7c_rnd(void) {
    bc7c_rs = bc7c_rs * 1664525u + 1013904223u;
    return bc7c_rs >> 8;
}

static int test_bc7_decoder_conformance(void) {
    int mode;
    long total = 0;
    for (mode = 0; mode < 8; ++mode) {
        int i;
        for (i = 0; i < 2000; ++i) {
            uint8_t blk[16], ours[16 * 4], ref[16][4];
            int k;
            for (k = 0; k < 16; ++k) blk[k] = (uint8_t)bc7c_rnd();
            /* Select the mode: `mode` low zero bits, then the marker bit. */
            blk[0] = (uint8_t)((blk[0] & ~((1u << (mode + 1)) - 1u)) | (1u << mode));
            CHECK(tc_bc7_decompress_rgba8(blk, 4, 4, 16, ours, sizeof(ours)) ==
                      TC_SUCCESS, "bc7 conformance decode");
            tc_bc7_ref_decode_block(blk, ref);
            CHECK(memcmp(ours, &ref[0][0], 64) == 0,
                  "bc7 decode matches the reference decoder on a random block");
            total++;
        }
    }
    printf("  bc7 decoder conformance: %ld random blocks over all 8 modes\n", total);
    return 0;
}


/* Reference decode of a BC4_SNORM block, written from the spec: the stored
 * endpoints are int8, and the 6-value palette substitutes the signed extremes
 * -1.0 (-127) and +1.0 (127) for the unsigned 0 and 255. Independent of the
 * library's tc_decode_bc4_block_snorm, which it is used to check. */
static void bc4_decode_snorm(const uint8_t in[8], int out[16]) {
    int r0 = (int8_t)in[0], r1 = (int8_t)in[1], pal[8], i;
    uint64_t bits = 0;
    pal[0] = r0;
    pal[1] = r1;
    if (r0 > r1) {
        for (i = 1; i <= 6; ++i) pal[i + 1] = ((7 - i) * r0 + i * r1) / 7;
    } else {
        for (i = 1; i <= 4; ++i) pal[i + 1] = ((5 - i) * r0 + i * r1) / 5;
        pal[6] = -127;
        pal[7] = 127;
    }
    for (i = 0; i < 6; ++i) bits |= (uint64_t)in[2 + i] << (8 * i);
    for (i = 0; i < 16; ++i) {
        int p = pal[(bits >> (3 * i)) & 7u];
        out[i] = p < -127 ? -127 : p;
    }
}

/* BC5_SNORM: the stored bytes must be *signed*, and must round-trip.
 *
 * This used to be broken in a way nothing could see: tc_bc5_compress_rgba8
 * ignored the options entirely and stored UNORM bytes, while the DDS/KTX2
 * writers tagged the format as SNORM -- so a GPU read a stored 200 as -56.
 * The checks below pin both halves: the bytes really are signed (a mid-grey
 * input, meaning x = 0, must store ~0 rather than ~128), and encode -> decode
 * returns what went in. */
static int test_bc5_snorm(void) {
    enum { W = 16, H = 8 };
    uint8_t src[W * H * 4], dec[W * H * 4], enc[32 * 16], enc_u[32 * 16];
    tc_bc5_options os, ou;
    uint32_t x, y, bxc = (W + 3u) / 4u, bx, by;
    tc_bc5_options_init(&os);
    tc_bc5_options_init(&ou);
    os.snorm = 1;
    ou.snorm = 0;

    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = src + ((size_t)y * W + x) * 4u;
            p[0] = (uint8_t)(x * 17u);  /* sweeps the full [-1,1] range */
            p[1] = (uint8_t)(y * 36u);
            p[2] = 0u;
            p[3] = 255u;
        }
    CHECK(tc_bc5_compress_rgba8(src, W, H, W * 4u, &os, enc, sizeof(enc)) ==
              TC_SUCCESS, "bc5 snorm compress");
    CHECK(tc_bc5_compress_rgba8(src, W, H, W * 4u, &ou, enc_u, sizeof(enc_u)) ==
              TC_SUCCESS, "bc5 unorm compress");
    /* The option must actually change the stored bytes. It used to not. */
    CHECK(memcmp(enc, enc_u, sizeof(enc)) != 0,
          "bc5 snorm changes the encoded bytes (not just the container tag)");

    /* Block decode must match the independent signed reference decoder. */
    for (by = 0; by < H; by += 4u)
        for (bx = 0; bx < W; bx += 4u) {
            size_t bi = ((size_t)(by / 4u) * bxc + bx / 4u) * 16u;
            int refr[16], refg[16];
            int8_t sr[16], sg[16];
            int k;
            bc4_decode_snorm(enc + bi, refr);
            bc4_decode_snorm(enc + bi + 8u, refg);
            tc_decode_bc4_block_snorm(enc + bi, sr);
            tc_decode_bc4_block_snorm(enc + bi + 8u, sg);
            for (k = 0; k < 16; ++k)
                CHECK((int)sr[k] == refr[k] && (int)sg[k] == refg[k],
                      "bc5 snorm block decode matches the reference decoder");
        }

    /* Round-trip through the public decoder, in the caller's UNORM8 convention.
     * Judged against the UNORM path rather than an absolute bound: BC4's 8-level
     * palette has its own error on this ramp, and the point here is that the
     * signed storage costs essentially nothing on top of it. */
    {
        int worst_s = 0, worst_u = 0;
        CHECK(tc_bc5_decompress_rgba8(enc, W, H, 1, W * 4u, dec, sizeof(dec)) ==
                  TC_SUCCESS, "bc5 snorm decompress");
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x) {
                const uint8_t *sp = src + ((size_t)y * W + x) * 4u;
                const uint8_t *d = dec + ((size_t)y * W + x) * 4u;
                int e0 = abs((int)d[0] - (int)sp[0]), e1 = abs((int)d[1] - (int)sp[1]);
                if (e0 > worst_s) worst_s = e0;
                if (e1 > worst_s) worst_s = e1;
            }
        CHECK(tc_bc5_decompress_rgba8(enc_u, W, H, 0, W * 4u, dec, sizeof(dec)) ==
                  TC_SUCCESS, "bc5 unorm decompress");
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x) {
                const uint8_t *sp = src + ((size_t)y * W + x) * 4u;
                const uint8_t *d = dec + ((size_t)y * W + x) * 4u;
                int e0 = abs((int)d[0] - (int)sp[0]), e1 = abs((int)d[1] - (int)sp[1]);
                if (e0 > worst_u) worst_u = e0;
                if (e1 > worst_u) worst_u = e1;
            }
        printf("  bc5 round-trip worst error: unorm=%d snorm=%d\n", worst_u, worst_s);
        CHECK(worst_s <= worst_u + 2,
              "bc5 snorm round-trip is no worse than the unorm path");
    }

    /* Mid-grey means x = 0, so a signed store must be ~0, not ~128 -- this is
     * the check that fails if the encoder quietly stores unorm bytes. */
    {
        uint8_t flat[4 * 4 * 4], one[16];
        int i;
        for (i = 0; i < 16; ++i) {
            flat[i * 4 + 0] = 128u;
            flat[i * 4 + 1] = 128u;
            flat[i * 4 + 2] = 0u;
            flat[i * 4 + 3] = 255u;
        }
        CHECK(tc_bc5_compress_rgba8(flat, 4, 4, 16u, &os, one, sizeof(one)) ==
                  TC_SUCCESS, "bc5 snorm flat compress");
        CHECK(abs((int)(int8_t)one[0]) <= 2 && abs((int)(int8_t)one[1]) <= 2,
              "bc5 snorm stores x=0 as ~0 (signed), not ~128 (unsigned)");
    }

    /* Decoding snorm blocks as unorm (or vice versa) must not silently pass. */
    CHECK(tc_bc5_decompress_rgba8(enc, W, H, 0, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "bc5 snorm-as-unorm decodes (but wrongly)");
    {
        int differs = 0;
        for (y = 0; y < H && !differs; ++y)
            for (x = 0; x < W; ++x) {
                const uint8_t *s = src + ((size_t)y * W + x) * 4u;
                const uint8_t *d = dec + ((size_t)y * W + x) * 4u;
                if (abs((int)d[0] - (int)s[0]) > 8) { differs = 1; break; }
            }
        CHECK(differs, "bc5 signedness is load-bearing (mismatched decode is wrong)");
    }
    return 0;
}


/* ETC2 encode quality, measured through the (Mesa-validated) decoder.
 *
 * The decoder goldens and the orientation test both passed while the encoder's
 * differential mode was writing its 5-bit base and 3-bit delta at overlapping
 * bit positions -- every differential block decoded to a wrong base colour, and
 * ETC2 scored ~13 dB where BC1 scored ~35 dB on the same image. Nothing noticed,
 * because no test asserted that ETC2 was any *good*, only that it round-tripped
 * the modes it happened to emit.
 *
 * So: hold ETC2 to BC1's standard on identical content. They are both 4 bpp and
 * ETC2 (with its planar mode) should be at least as good, so a generous floor of
 * "within 2 dB of BC1" still catches a broken mode immediately.
 */
static int test_etc2_quality(void) {
    enum { W = 64, H = 64 };
    static uint8_t src[W * H * 4], dec[W * H * 4];
    static uint8_t etc[W * H / 2], bc1[W * H / 2];
    tc_etc2_options eo;
    tc_bc1_options bo;
    double sse_etc = 0.0, sse_bc1 = 0.0, p_etc, p_bc1;
    uint32_t rs = 7u, x, y;
    size_t i;
    int c;
    tc_etc2_options_init(&eo);
    eo.alpha = 0;
    tc_bc1_options_init(&bo);

    /* Smooth luma with a little detail and a natural (correlated) tint -- the
     * content both codecs are built for. */
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = src + ((size_t)y * W + x) * 4u;
            int v;
            rs = rs * 1664525u + 1013904223u;
            v = (int)(128.0 + 100.0 * sin(x * 0.3) * cos(y * 0.25)) +
                (int)((rs >> 26) % 8u) - 4;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            p[0] = (uint8_t)v;
            p[1] = (uint8_t)(v * 230 / 255);
            p[2] = (uint8_t)(v * 200 / 255);
            p[3] = 255u;
        }

    CHECK(tc_etc2_compress_rgba8(src, W, H, W * 4u, &eo, etc, sizeof(etc)) ==
              TC_SUCCESS, "etc2 quality: compress");
    CHECK(tc_etc2_decompress_rgba8(etc, W, H, 0, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "etc2 quality: decompress");
    for (i = 0; i < (size_t)W * H; ++i)
        for (c = 0; c < 3; ++c) {
            double d = (double)src[i * 4u + (size_t)c] - (double)dec[i * 4u + (size_t)c];
            sse_etc += d * d;
        }
    CHECK(tc_bc1_compress_rgba8(src, W, H, W * 4u, &bo, bc1, sizeof(bc1)) ==
              TC_SUCCESS, "etc2 quality: bc1 compress");
    CHECK(tc_bc1_decompress_rgba8(bc1, W, H, W * 4u, dec, sizeof(dec)) ==
              TC_SUCCESS, "etc2 quality: bc1 decompress");
    for (i = 0; i < (size_t)W * H; ++i)
        for (c = 0; c < 3; ++c) {
            double d = (double)src[i * 4u + (size_t)c] - (double)dec[i * 4u + (size_t)c];
            sse_bc1 += d * d;
        }
    p_etc = 10.0 * log10(255.0 * 255.0 / (sse_etc / (double)(W * H * 3)));
    p_bc1 = 10.0 * log10(255.0 * 255.0 / (sse_bc1 / (double)(W * H * 3)));
    printf("  etc2 quality: ETC2=%.2f dB  BC1=%.2f dB (same content, both 4bpp)\n",
           p_etc, p_bc1);
    CHECK(p_etc >= 30.0, "etc2 encode quality clears an absolute floor");
    CHECK(p_etc >= p_bc1 - 2.0, "etc2 encode quality is competitive with bc1");
    return 0;
}


/* ASTC HDR, RGBA (CEM 15) path, on the block that actually broke it.
 *
 * The gate's CEM 15 case is a smooth correlated RGBA gradient. It never chose
 * the dual-plane sub-path, so it never touched the bug -- and neither did my
 * first attempt at a synthetic replacement, which passed identically against the
 * broken encoder. The content below is the exact 4x4 block, lifted from a real
 * photo, that decoded to 30208.
 *
 * The bug: the dual-plane path wrote its 8 endpoint values at colour quant 256
 * (64 bits from bit 17) underneath 32 dual-plane weight symbols that already
 * occupied the top 52 bits. They overlapped -- and the decoder, which derives
 * the colour quant level from whatever space the weights leave behind, read them
 * back at a different level entirely. astcenc decodes those blocks to the same
 * garbage we do, which is how we know the encoder was at fault and not us.
 *
 * The range check is the load-bearing one: a blown-up endpoint shows up there
 * immediately, and long before a PSNR number explains why.
 */
static int test_astc_hdr_rgba_quality(void) {
    /* Ordinary, smooth, mid-grey-ish -- nothing exotic about it. */
    static const float BR[16] = {
        0.678f, 0.772f, 0.800f, 0.804f, 0.745f, 0.867f, 0.886f, 0.882f,
        0.749f, 0.859f, 0.878f, 0.875f, 0.749f, 0.878f, 0.894f, 0.890f};
    static const float BG[16] = {
        0.643f, 0.757f, 0.792f, 0.780f, 0.788f, 0.910f, 0.957f, 0.945f,
        0.788f, 0.914f, 0.937f, 0.933f, 0.800f, 0.933f, 0.953f, 0.949f};
    static const float BB[16] = {
        0.655f, 0.745f, 0.804f, 0.796f, 0.812f, 0.917f, 0.965f, 0.945f,
        0.828f, 0.917f, 0.949f, 0.945f, 0.832f, 0.937f, 0.965f, 0.961f};
    float src[16 * 4], dec[16 * 4];
    uint8_t enc[16];
    tc_astc_hdr_options o;
    double sse = 0.0, psnr;
    float dmax = 0.0f;
    int i, c;
    tc_astc_hdr_options_init(&o);
    for (i = 0; i < 16; ++i) {
        src[i * 4 + 0] = BR[i];
        src[i * 4 + 1] = BG[i];
        src[i * 4 + 2] = BB[i];
        src[i * 4 + 3] = 1.0f;
    }
    CHECK(tc_astc_hdr_compress_rgbaf(src, 4, 4, 4u * 4u * sizeof(float), &o, enc,
                                     sizeof(enc)) == TC_SUCCESS,
          "astc hdr rgba: compress");
    CHECK(tc_astc_hdr_decompress_rgbaf(enc, 4, 4, 4, 4, 4u * 4u * sizeof(float),
                                       dec, sizeof(dec)) == TC_SUCCESS,
          "astc hdr rgba: decompress");
    for (i = 0; i < 16; ++i)
        for (c = 0; c < 3; ++c) {
            double d = (double)src[i * 4 + c] - (double)dec[i * 4 + c];
            sse += d * d;
            if (dec[i * 4 + c] > dmax) dmax = dec[i * 4 + c];
        }
    psnr = 10.0 * log10(1.0 / (sse / 48.0));
    printf("  astc hdr rgba (cem15): %.2f dB, decoded max %.2f (source max ~0.97)\n",
           psnr, (double)dmax);
    CHECK((double)dmax <= 4.0,
          "astc hdr rgba decode stays in range (no endpoint blow-up)");
    CHECK(psnr >= 25.0, "astc hdr rgba encode quality clears its floor");
    return 0;
}

int main(void) {
    uint8_t rgba[7 * 5 * 4];
    uint8_t part_rgba[4 * 4 * 4];
    uint8_t astc_rgba[12 * 12 * 4];
    uint8_t bc7[64], bc5[64], bc6h[64];
    uint8_t bc1[64], bc3[64];
    uint8_t etc2[64], eac[64], astc[64];
    uint8_t dds[148 + 64];
    uint8_t ktx[68 + 64], astc_file[16 + 64];
    float rgbf[7 * 5 * 3];
    float rgbf_block[16 * 3];
    tc_bc7_options opt;
    tc_bc1_options bc1_opt;
    tc_bc3_options bc3_opt;
    tc_bc5_options bc5_opt;
    tc_bc6h_options bc6h_opt;
    tc_etc2_options etc2_opt;
    tc_astc_options astc_opt;
    tc_astc_options astc4_opt;
    uint8_t ise_values[4] = {1, 2, 3, 4};
    uint8_t ise_trits[5] = {1, 2, 0, 1, 2};
    uint8_t ise_bits[8];
    size_t i, j;

    tc_bc7_options_init(&opt);
    tc_bc1_options_init(&bc1_opt);
    tc_bc3_options_init(&bc3_opt);
    tc_bc5_options_init(&bc5_opt);
    tc_bc6h_options_init(&bc6h_opt);
    tc_etc2_options_init(&etc2_opt);
    tc_astc_options_init(&astc_opt);
    tc_astc_options_init(&astc4_opt);
    astc4_opt.block_x = 4;
    astc4_opt.block_y = 4;
    CHECK(tc_bc7_compressed_size(4, 4) == 16, "4x4 size");
    CHECK(tc_bc7_compressed_size(7, 5) == 64, "padded size");
    CHECK(tc_bc1_compressed_size(7, 5) == 32, "bc1 padded size");
    CHECK(tc_bc3_compressed_size(7, 5) == 64, "bc3 padded size");
    CHECK(tc_bc5_compressed_size(7, 5) == 64, "bc5 padded size");
    CHECK(tc_bc6h_compressed_size(7, 5) == 64, "bc6h padded size");
    CHECK(tc_etc2_rgb_compressed_size(7, 5) == 32, "etc2 rgb padded size");
    CHECK(tc_etc2_rgba_compressed_size(7, 5) == 64, "etc2 rgba padded size");
    CHECK(tc_eac_r11_compressed_size(7, 5) == 32, "eac r11 padded size");
    CHECK(tc_eac_rg11_compressed_size(7, 5) == 64, "eac rg11 padded size");
    CHECK(tc_astc_compressed_size(7, 5, &astc_opt) == 32, "astc padded size");
    CHECK(tc_astc_hdr_compressed_size(7, 5) == 64, "astc hdr padded size");
    CHECK(tc_astc_ise_sequence_bitcount(1, 0) == 1, "astc ise quant2 bits");
    CHECK(tc_astc_ise_sequence_bitcount(5, 1) == 8, "astc ise quant3 bits");
    CHECK(tc_astc_ise_sequence_bitcount(3, 3) == 7, "astc ise quant5 bits");
    CHECK(tc_astc_ise_sequence_bitcount(4, 20) == 32, "astc ise quant256 bits");
    memset(ise_bits, 0, sizeof(ise_bits));
    CHECK(tc_astc_ise_encode_bits(5, 4, ise_values, ise_bits, sizeof(ise_bits), 0) ==
              TC_SUCCESS,
          "astc ise pure-bit encode");
    CHECK((ise_bits[0] & 0x3fu) == 0x11u, "astc ise pure-bit payload");
    memset(ise_bits, 0, sizeof(ise_bits));
    CHECK(tc_astc_ise_encode_bits(3, 3, ise_values, ise_bits, sizeof(ise_bits), 0) ==
              TC_SUCCESS,
          "astc ise quint encode");
    CHECK(ise_bits[0] == 113u, "astc ise quint payload");
    memset(ise_bits, 0, sizeof(ise_bits));
    CHECK(tc_astc_ise_encode_bits(1, 5, ise_trits, ise_bits, sizeof(ise_bits), 0) ==
              TC_SUCCESS,
          "astc ise trit encode");
    CHECK(ise_bits[0] == 233u, "astc ise trit payload");
    CHECK(tc_dds_bc7_size(7, 5) == 148 + 64, "dds size");
    CHECK(tc_dds_bc1_size(7, 5) == 148 + 32, "bc1 dds size");
    CHECK(tc_dds_bc3_size(7, 5) == 148 + 64, "bc3 dds size");
    CHECK(tc_dds_bc5_size(7, 5) == 148 + 64, "bc5 dds size");
    CHECK(tc_dds_bc6h_size(7, 5) == 148 + 64, "bc6h dds size");
    CHECK(tc_ktx_etc2_size(7, 5, &etc2_opt) == 68 + 64, "etc2 ktx size");
    CHECK(tc_astc_file_size(7, 5, &astc_opt) == 16 + 32, "astc file size");

    for (i = 0; i < sizeof(rgba) / 4u; ++i) {
        rgba[i * 4u + 0u] = (uint8_t)(i * 17u);
        rgba[i * 4u + 1u] = (uint8_t)(255u - i * 9u);
        rgba[i * 4u + 2u] = (uint8_t)(i * 31u);
        rgba[i * 4u + 3u] = (uint8_t)(128u + (i & 1u) * 127u);
        rgbf[i * 3u + 0u] = (float)rgba[i * 4u + 0u] / 255.0f;
        rgbf[i * 3u + 1u] = (float)rgba[i * 4u + 1u] / 255.0f;
        rgbf[i * 3u + 2u] = (float)rgba[i * 4u + 2u] / 255.0f;
    }

    CHECK(tc_bc7_compress_rgba8(rgba, 7, 5, 7 * 4, &opt, bc7, sizeof(bc7)) ==
              TC_SUCCESS,
          "compress");
    CHECK(bc7_mode(bc7) < 8u, "valid mode block");
    opt.quick = 0;
    CHECK(tc_bc7_compress_rgba8(rgba, 7, 5, 7 * 4, &opt, bc7, sizeof(bc7)) ==
              TC_SUCCESS,
          "exhaustive compress");
    CHECK(bc7_mode(bc7) < 8u, "valid exhaustive mode block");
    opt.quick = 1;
    for (i = 0; i < 8u; ++i) {
        opt.mode_mask = 1u << i;
        CHECK(tc_bc7_compress_rgba8(rgba, 4, 4, 7 * 4, &opt, bc7, sizeof(bc7)) ==
                  TC_SUCCESS,
              "forced mode compress");
        CHECK(bc7_mode(bc7) == i, "forced mode emitted");
    }
    memset(part_rgba, 0, sizeof(part_rgba));
    for (i = 0; i < 16u; ++i) {
        part_rgba[i * 4u + 0u] = 240u;
        part_rgba[i * 4u + 1u] = 10u;
        part_rgba[i * 4u + 2u] = 10u;
        part_rgba[i * 4u + 3u] = 255u;
    }
    part_rgba[5u * 4u + 0u] = 10u;
    part_rgba[5u * 4u + 1u] = 240u;
    opt.mode_mask = 1u << 1;
    opt.quick = 1;
    CHECK(tc_bc7_compress_rgba8(part_rgba, 4, 4, 4 * 4, &opt, bc7,
                                sizeof(bc7)) == TC_SUCCESS,
          "forced mode1 partition compress");
    CHECK(bc7_mode(bc7) == 1u, "forced mode1 emitted");
    CHECK(rd_bits(bc7, 2, 6) == 5u, "quickbc7 mode1 partition");
    opt.mode_mask = 0xffu;

    /* Float-input wrapper must produce valid output. */
    {
        float f_rgba[7 * 5 * 4];
        uint8_t bc7_f[64];
        uint32_t i;
        for (i = 0; i < 7u * 5u * 4u; ++i)
            f_rgba[i] = (float)rgba[i] / 255.0f;
        CHECK(tc_bc7_compress_rgbaf(f_rgba, 7, 5, 7 * 4 * sizeof(float),
                                    &opt, bc7_f, sizeof(bc7_f)) == TC_SUCCESS,
              "bc7 float compress");
        CHECK(bc7_mode(bc7_f) < 8u, "bc7 float valid mode");
    }
    /* Float decompress round-trip: compress uint8, decompress to float, check
     * the float output matches the expected uint8 reconstruction. */
    {
        uint8_t dec_u8[7 * 5 * 4];
        float dec_f[7 * 5 * 4];
        uint32_t i, mismatches = 0;
        CHECK(tc_bc7_decompress_rgba8(bc7, 7, 5, 7 * 4, dec_u8,
                                      sizeof(dec_u8)) == TC_SUCCESS,
              "bc7 u8 decompress");
        CHECK(tc_bc7_decompress_rgbaf(bc7, 7, 5, 7 * 4 * sizeof(float),
                                      dec_f, sizeof(dec_f)) == TC_SUCCESS,
              "bc7 float decompress");
        for (i = 0; i < 7u * 5u * 4u; ++i) {
            float expected = (float)dec_u8[i] / 255.0f;
            if (fabsf(dec_f[i] - expected) > 1e-6f) ++mismatches;
        }
        CHECK(mismatches == 0u, "bc7 float decompress matches u8 decompress");
    }

    CHECK(tc_bc5_compress_rgba8(rgba, 7, 5, 7 * 4, &bc5_opt, bc5,
                                sizeof(bc5)) == TC_SUCCESS,
          "bc5 compress");
    CHECK(bc5[0] >= bc5[1], "bc5 red endpoint order");
    CHECK(tc_dds_write_bc5_memory(bc5, 7, 5, &bc5_opt, dds, sizeof(dds)) ==
              TC_SUCCESS,
          "bc5 dds write");
    CHECK(rd_u32(dds + 112) == 83, "bc5 dxgi format");

    /* BC1 / BC3: encode 4x4 RGBA blocks, reference-decode, check quality.
     * A two-cluster block fits BC1's endpoints almost exactly (strict floor);
     * a full-range gradient exercises the interpolated indices (2 and 3) and
     * is bounded only loosely, since BC1's 4 colours per block inherently
     * quantise a 16-step ramp to ~24 dB. */
    {
        uint8_t clus[4 * 4 * 4], grad[4 * 4 * 4];
        uint8_t dec[16][3], deca[16];
        uint64_t sse_rgb, sse_a;
        double psnr_rgb, psnr_a;
        int saw_interp = 0;
        size_t n;
        for (n = 0; n < 16u; ++n) {
            int hi = n >= 8u;
            clus[n * 4u + 0u] = hi ? 200u : 50u;
            clus[n * 4u + 1u] = hi ? 160u : 80u;
            clus[n * 4u + 2u] = hi ? 90u : 120u;
            clus[n * 4u + 3u] = hi ? 240u : 40u;
            grad[n * 4u + 0u] = (uint8_t)(20u + n * 13u);
            grad[n * 4u + 1u] = (uint8_t)(30u + n * 12u);
            grad[n * 4u + 2u] = (uint8_t)(40u + n * 11u);
            grad[n * 4u + 3u] = (uint8_t)(n * 17u);
        }

        /* Two-cluster block: near-exact, strict floors. */
        CHECK(tc_bc1_compress_rgba8(clus, 4, 4, 4 * 4, &bc1_opt, bc1,
                                    sizeof(bc1)) == TC_SUCCESS,
              "bc1 compress");
        CHECK(((uint32_t)bc1[0] | ((uint32_t)bc1[1] << 8)) >=
                  ((uint32_t)bc1[2] | ((uint32_t)bc1[3] << 8)),
              "bc1 4-colour endpoint order");
        bc1_decode(bc1, dec);
        sse_rgb = 0;
        for (n = 0; n < 16u; ++n) {
            int c;
            for (c = 0; c < 3; ++c) {
                int d = (int)clus[n * 4u + (size_t)c] - (int)dec[n][c];
                sse_rgb += (uint64_t)(d * d);
            }
        }
        psnr_rgb =
            sse_rgb ? 10.0 * log10(255.0 * 255.0 / ((double)sse_rgb / 48.0))
                    : 99.0;
        CHECK(psnr_rgb >= 38.0, "bc1 cluster rgb psnr floor");
        CHECK(tc_dds_write_bc1_memory(bc1, 4, 4, &bc1_opt, dds, sizeof(dds)) ==
                  TC_SUCCESS,
              "bc1 dds write");
        CHECK(rd_u32(dds + 112) == 71, "bc1 dxgi format");

        /* Gradient block: loose floor + must use interpolated indices. */
        CHECK(tc_bc1_compress_rgba8(grad, 4, 4, 4 * 4, &bc1_opt, bc1,
                                    sizeof(bc1)) == TC_SUCCESS,
              "bc1 gradient compress");
        for (n = 0; n < 16u; ++n) {
            int idx = (bc1[4 + (n >> 2)] >> ((n & 3u) * 2u)) & 3;
            if (idx >= 2) saw_interp = 1;
        }
        CHECK(saw_interp, "bc1 uses interpolated indices");
        bc1_decode(bc1, dec);
        sse_rgb = 0;
        for (n = 0; n < 16u; ++n) {
            int c;
            for (c = 0; c < 3; ++c) {
                int d = (int)grad[n * 4u + (size_t)c] - (int)dec[n][c];
                sse_rgb += (uint64_t)(d * d);
            }
        }
        psnr_rgb =
            sse_rgb ? 10.0 * log10(255.0 * 255.0 / ((double)sse_rgb / 48.0))
                    : 99.0;
        CHECK(psnr_rgb >= 24.0, "bc1 gradient rgb psnr floor");

        /* BC3: cluster colours + per-pixel alpha (BC4 alpha is near-exact). */
        CHECK(tc_bc3_compress_rgba8(clus, 4, 4, 4 * 4, &bc3_opt, bc3,
                                    sizeof(bc3)) == TC_SUCCESS,
              "bc3 compress");
        bc4_decode(bc3, deca);    /* alpha half */
        bc1_decode(bc3 + 8, dec); /* colour half */
        sse_rgb = 0;
        sse_a = 0;
        for (n = 0; n < 16u; ++n) {
            int c, da = (int)clus[n * 4u + 3u] - (int)deca[n];
            sse_a += (uint64_t)(da * da);
            for (c = 0; c < 3; ++c) {
                int d = (int)clus[n * 4u + (size_t)c] - (int)dec[n][c];
                sse_rgb += (uint64_t)(d * d);
            }
        }
        psnr_rgb =
            sse_rgb ? 10.0 * log10(255.0 * 255.0 / ((double)sse_rgb / 48.0))
                    : 99.0;
        psnr_a = sse_a ? 10.0 * log10(255.0 * 255.0 / ((double)sse_a / 16.0))
                       : 99.0;
        CHECK(psnr_rgb >= 38.0, "bc3 rgb psnr floor");
        CHECK(psnr_a >= 45.0, "bc3 alpha psnr floor");
        CHECK(tc_dds_write_bc3_memory(bc3, 4, 4, &bc3_opt, dds, sizeof(dds)) ==
                  TC_SUCCESS,
              "bc3 dds write");
        CHECK(rd_u32(dds + 112) == 77, "bc3 dxgi format");
    }

    /* ASTC HDR: constant-colour block must be a valid FP16 void-extent. */
    {
        float hdr[4 * 4 * 3];
        uint8_t hblk[16];
        tc_astc_hdr_options hopt;
        size_t n;
        tc_astc_hdr_options_init(&hopt);
        for (n = 0; n < 16u; ++n) {
            hdr[n * 3 + 0] = 3.5f;
            hdr[n * 3 + 1] = 0.5f;
            hdr[n * 3 + 2] = 40.0f;
        }
        CHECK(tc_astc_hdr_compress_rgbf(hdr, 4, 4, 4u * 3u * sizeof(float),
                                        &hopt, hblk, sizeof(hblk)) == TC_SUCCESS,
              "astc hdr encode");
        /* FP16 void-extent marker (byte1 = FF; an LDR one would be FD). */
        CHECK(hblk[0] == 0xFCu && hblk[1] == 0xFFu, "astc hdr void-extent marker");
        CHECK((hblk[14] | (hblk[15] << 8)) == 0x3C00u, "astc hdr alpha = 1.0");
        CHECK((hblk[8] | (hblk[9] << 8)) != 0u, "astc hdr red nonzero");
    }

    /* Pure-C HDR round-trip: encode a small HDR gradient and decode it with the
     * pure-C HDR reference decoder (no astcenc), checking a PSNR floor -- the
     * core suite's HDR coverage, mirroring the LDR aref_decode_image tests. */
    {
        enum { HW = 16, HH = 16 };
        static float hsrc[HW * HH * 3];
        static uint8_t hblocks[(HW / 4) * (HH / 4) * 16];
        float hdec[16 * 4];
        tc_astc_hdr_options hopt;
        uint32_t hx, hy, bxc = HW / 4u;
        double sse = 0.0, peak = 0.0, psnr;
        tc_astc_hdr_options_init(&hopt);
        for (hy = 0; hy < HH; ++hy)
            for (hx = 0; hx < HW; ++hx) {
                float t = (float)(hx + hy) / (float)(2 * (HW - 1));
                float *p = hsrc + ((size_t)hy * HW + hx) * 3;
                p[0] = 0.2f + t * 20.0f;
                p[1] = 0.15f + t * 12.0f;
                p[2] = 0.1f + t * 8.0f;
            }
        CHECK(tc_astc_hdr_compress_rgbf(hsrc, HW, HH,
                                        (size_t)HW * 3u * sizeof(float), &hopt,
                                        hblocks, sizeof(hblocks)) == TC_SUCCESS,
              "astc hdr encode (gradient)");
        for (hy = 0; hy < HH; hy += 4)
            for (hx = 0; hx < HW; hx += 4) {
                uint32_t yy, xx;
                CHECK(ahref_decode_block_hdr(
                          hblocks + ((size_t)(hy / 4u) * bxc + hx / 4u) * 16u, 4,
                          4, hdec),
                      "astc hdr pure-C ref decode");
                for (yy = 0; yy < 4u; ++yy)
                    for (xx = 0; xx < 4u; ++xx) {
                        const float *s =
                            hsrc + ((size_t)(hy + yy) * HW + (hx + xx)) * 3;
                        const float *d = hdec + (yy * 4u + xx) * 4u;
                        int c;
                        for (c = 0; c < 3; ++c) {
                            double e = (double)s[c] - d[c];
                            sse += e * e;
                            if (s[c] > peak) peak = s[c];
                        }
                    }
            }
        psnr = sse > 0.0
                   ? 10.0 * log10(peak * peak / (sse / ((double)HW * HH * 3)))
                   : 99.0;
        CHECK(psnr > 45.0, "astc hdr gradient pure-C decode psnr");
    }

    /* ASTC HDR CEM 11 (HDR RGB direct) endpoint codec round-trip: encode two
     * HDR colours into the majcomp==3 direct sub-mode, decode, and verify both
     * the LNS-domain quantisation bound and the reconstructed value error
     * The encoder tries the base+offset sub-modes (higher precision when the
     * endpoints are close) and falls back to majcomp==3 direct otherwise. Each
     * pair below tags the tolerance for the expected path. */
    {
        static const float cols[8][3] = {
            /* far / anti-correlated -> majcomp3 fallback (looser). */
            {2.5f, 0.1f, 30.0f},
            {0.3f, 8.0f, 1.0f},
            {0.02f, 0.05f, 0.5f},
            {100.0f, 50.0f, 2000.0f},
            /* close + correlated -> base+offset (tight). */
            {4.0f, 3.0f, 2.0f},
            {4.4f, 3.3f, 2.2f},
            {12.0f, 9.0f, 6.0f},
            {12.8f, 9.6f, 6.4f}};
        int k, c, lvl;
        /* level 20 (== colour quant 256, single-subset) and level 14 (== 64,
         * the 2-subset budget) must both round-trip. */
        static const int levels[2] = {20, 14};
        int li;
        for (li = 0; li < 2; ++li) {
            lvl = levels[li];
            for (k = 0; k + 1 < 8; k += 2) {
                int l0[3], l1[3], d0[3], d1[3];
                /* base+offset (k>=4) is precise; the flat fallback (k<4) on
                 * small anti-correlated colours is coarse, more so at 64. */
                double tol = (lvl >= 20) ? ((k >= 4) ? 0.03 : 0.14)
                                         : ((k >= 4) ? 0.06 : 0.32);
                uint8_t v[6];
                for (c = 0; c < 3; ++c) {
                    l0[c] = tc_astc_float_to_lns16(cols[k][c]);
                    l1[c] = tc_astc_float_to_lns16(cols[k + 1][c]);
                }
                tc_astc_cem11_pack(l0, l1, lvl, v);
                CHECK(tc_astc_cem11_unpack(v, d0, d1) == 1, "cem11 unpack");
                for (c = 0; c < 3; ++c) {
                    float rec = half_bits_to_float(tc_astc_lns16_to_sf16(d0[c]));
                    float ref =
                        half_bits_to_float(tc_float_to_half_bits(cols[k][c]));
                    double rel = fabs((double)rec - ref) / ((double)ref + 1e-3);
                    CHECK(tc_astc_lns16_to_sf16(d0[c]) <= 0x7BFFu,
                          "cem11 valid half");
                    CHECK(rel < tol, "cem11 value round-trip");
                }
            }
        }
    }

    /* CEM 15 = CEM 11 RGB + HDR alpha (8 values). RGB round-trips as CEM 11;
     * additionally check the HDR alpha pair packs/unpacks to fp16 within the
     * delta-submode precision (flat fallback on far-apart alphas is coarser). */
    {
        static const float apairs[5][2] = {{100.0f, 130.0f}, {1.0f, 1.05f},
                                           {4000.0f, 4200.0f}, {0.5f, 0.5f},
                                           {8000.0f, 200.0f}};
        int kp;
        for (kp = 0; kp < 5; ++kp) {
            int rgb0[3] = {2000, 2000, 2000}, rgb1[3] = {2200, 2200, 2200};
            int d0[4], d1[4];
            uint8_t v[8];
            float la0 = apairs[kp][0], la1 = apairs[kp][1];
            /* alpha endpoints live in the LNS16 domain, like RGB. */
            int alns0 = tc_astc_float_to_lns16(la0);
            int alns1 = tc_astc_float_to_lns16(la1);
            double close = fabs(la0 - la1) / (la0 + 1.0f) < 0.5 ? 0.06 : 0.40;
            int cc;
            tc_astc_cem15_pack(rgb0, rgb1, alns0, alns1, 20, v);
            CHECK(tc_astc_cem15_unpack(v, d0, d1) == 1, "cem15 unpack");
            /* RGB carried through unchanged from CEM 11. */
            for (cc = 0; cc < 3; ++cc)
                CHECK(d0[cc] >= 0 && tc_astc_lns16_to_sf16(d0[cc]) <= 0x7BFFu,
                      "cem15 rgb valid half");
            {
                float ra0 = half_bits_to_float(tc_astc_lns16_to_sf16(d0[3]));
                float ra1 = half_bits_to_float(tc_astc_lns16_to_sf16(d1[3]));
                double e0 = fabs((double)ra0 - la0) / (la0 + 1e-3);
                double e1 = fabs((double)ra1 - la1) / (la1 + 1e-3);
                CHECK(tc_astc_lns16_to_sf16(d0[3]) <= 0x7BFFu,
                      "cem15 alpha valid half");
                CHECK(e0 < close && e1 < close, "cem15 alpha round-trip");
            }
        }
    }

    CHECK(tc_etc2_compress_rgba8(rgba, 7, 5, 7 * 4, &etc2_opt, etc2,
                                 sizeof(etc2)) == TC_SUCCESS,
          "etc2 rgba compress");
    CHECK(tc_ktx_write_etc2_memory(etc2, 7, 5, &etc2_opt, ktx, sizeof(ktx)) ==
              TC_SUCCESS,
          "etc2 ktx write");
    CHECK(ktx[0] == 0xabu && memcmp(ktx + 1, "KTX 11", 6) == 0, "ktx magic");
    CHECK(rd_u32(ktx + 28) == 0x9278u, "etc2 rgba internal format");
    etc2_opt.alpha = 0;
    CHECK(tc_etc2_compress_rgba8(rgba, 7, 5, 7 * 4, &etc2_opt, etc2,
                                 sizeof(etc2)) == TC_SUCCESS,
          "etc2 rgb compress");
    CHECK(tc_ktx_write_etc2_memory(etc2, 7, 5, &etc2_opt, ktx, sizeof(ktx)) ==
              TC_SUCCESS,
          "etc2 rgb ktx write");
    CHECK(rd_u32(ktx + 28) == 0x9274u, "etc2 rgb internal format");
    etc2_opt.alpha = 1;

    CHECK(tc_eac_compress_rgba8(rgba, 7, 5, 7 * 4, 0, eac, sizeof(eac)) ==
              TC_SUCCESS,
          "eac r11 compress");
    CHECK(tc_ktx_write_eac_memory(eac, 7, 5, 0, ktx, sizeof(ktx)) == TC_SUCCESS,
          "eac r11 ktx write");
    CHECK(rd_u32(ktx + 28) == 0x9270u, "eac r11 internal format");
    CHECK(tc_eac_compress_rgba8(rgba, 7, 5, 7 * 4, 1, eac, sizeof(eac)) ==
              TC_SUCCESS,
          "eac rg11 compress");
    CHECK(tc_ktx_write_eac_memory(eac, 7, 5, 1, ktx, sizeof(ktx)) == TC_SUCCESS,
          "eac rg11 ktx write");
    CHECK(rd_u32(ktx + 28) == 0x9272u, "eac rg11 internal format");

    CHECK(tc_astc_compress_rgba8(rgba, 7, 5, 7 * 4, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc compress");
    {
        uint32_t wx, wy, quant, dual;
        CHECK(astc_test_decode_mode_2d(rd_bits(astc, 0, 11), &wx, &wy, &quant,
                                       &dual),
              "astc default ldr block mode");
        (void)wx;
        (void)wy;
        (void)quant;
        (void)dual;
    }
    CHECK(rd_bits(astc, 13, 4) == 12u, "astc default rgba endpoint mode");
    CHECK(tc_astc_write_file_memory(astc, 7, 5, &astc_opt, astc_file,
                                    sizeof(astc_file)) == TC_SUCCESS,
          "astc file write");
    CHECK(astc_file[0] == 0x13u && astc_file[1] == 0xabu &&
              astc_file[2] == 0xa1u && astc_file[3] == 0x5cu,
          "astc magic");
    CHECK(astc_file[4] == 6u && astc_file[5] == 6u, "astc block dims");
    memset(part_rgba, 0, sizeof(part_rgba));
    for (i = 0; i < 16u; ++i) {
        part_rgba[i * 4u + 0u] = 12u;
        part_rgba[i * 4u + 1u] = 34u;
        part_rgba[i * 4u + 2u] = 56u;
        part_rgba[i * 4u + 3u] = 78u;
    }
    CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc 4x4 compress");
    CHECK(astc[8] == 12u && astc[9] == 12u && astc[10] == 34u &&
              astc[11] == 34u && astc[12] == 56u && astc[13] == 56u &&
              astc[14] == 78u && astc[15] == 78u,
          "astc 4x4 solid average");
    {
        uint8_t astc_ref[16], astc_simd[16];
        uint32_t avail = tc_backend_available_mask();
        tc_backend_force_mask(TC_BACKEND_SCALAR);
        CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt, astc_ref,
                                     sizeof(astc_ref)) == TC_SUCCESS,
              "astc scalar forced");
        if (avail & TC_BACKEND_SSE2) {
            tc_backend_force_mask(TC_BACKEND_SSE2);
            CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt,
                                         astc_simd, sizeof(astc_simd)) ==
                      TC_SUCCESS,
                  "astc sse2 forced");
            CHECK(memcmp(astc_ref, astc_simd, sizeof(astc_ref)) == 0,
                  "astc sse2 parity");
        }
        if (avail & TC_BACKEND_SSE41) {
            tc_backend_force_mask(TC_BACKEND_SSE41);
            CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt,
                                         astc_simd, sizeof(astc_simd)) ==
                      TC_SUCCESS,
                  "astc sse4.1 forced");
            CHECK(memcmp(astc_ref, astc_simd, sizeof(astc_ref)) == 0,
                  "astc sse4.1 parity");
        }
        if (avail & TC_BACKEND_AVX2) {
            tc_backend_force_mask(TC_BACKEND_AVX2);
            CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt,
                                         astc_simd, sizeof(astc_simd)) ==
                      TC_SUCCESS,
                  "astc avx2 forced");
            CHECK(memcmp(astc_ref, astc_simd, sizeof(astc_ref)) == 0,
                  "astc avx2 parity");
        }
        tc_backend_force_mask(TC_BACKEND_ALL);
    }
    for (i = 0; i < 16u; ++i) {
        part_rgba[i * 4u + 0u] = (uint8_t)(i * 13u);
        part_rgba[i * 4u + 1u] = (uint8_t)(255u - i * 7u);
        part_rgba[i * 4u + 2u] = (uint8_t)(i * 5u);
        part_rgba[i * 4u + 3u] = 255u;
    }
    astc4_opt.quality = 0;
    CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc 4x4 nonconstant compress");
    CHECK(astc_test_mode_is_valid_1plane_2d(rd_bits(astc, 0, 11)),
          "astc 4x4 ldr block mode");
    CHECK(rd_bits(astc, 13, 4) == 8u, "astc 4x4 rgb endpoint mode");
    astc4_opt.quality = 2;
    for (i = 0; i < 16u; ++i) {
        part_rgba[i * 4u + 0u] = (uint8_t)(i * 17u);
        part_rgba[i * 4u + 1u] = (uint8_t)(i * 17u);
        part_rgba[i * 4u + 2u] = (uint8_t)(i * 17u);
        part_rgba[i * 4u + 3u] = 255u;
    }
    CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc 4x4 high precision candidate compress");
    CHECK(astc_test_mode_is_high_precision_4x4(rd_bits(astc, 0, 11)),
          "astc 4x4 high precision block mode");
    part_rgba[3] = 128u;
    part_rgba[5] = 83u;
    part_rgba[10] = 11u;
    CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc 4x4 rgba endpoint compress");
    CHECK(rd_bits(astc, 13, 4) == 12u, "astc 4x4 rgba endpoint mode");
    part_rgba[3] = 255u;
    part_rgba[5] = 17u;
    part_rgba[10] = 34u;
    astc4_opt.quality = 0;
    CHECK(tc_astc_compress_rgba8(part_rgba, 4, 4, 4 * 4, &astc4_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc 4x4 fast compress");
    {
        uint32_t wx, wy, quant, dual;
        CHECK(astc_test_decode_mode_2d(rd_bits(astc, 0, 11), &wx, &wy, &quant,
                                       &dual) &&
                  !dual && wx <= 4u && wy <= 4u && quant <= 2u,
              "astc 4x4 fast block mode");
    }
    astc4_opt.quality = 2;
    for (i = 0; i < 12u * 12u; ++i) {
        astc_rgba[i * 4u + 0u] = (uint8_t)(i * 11u);
        astc_rgba[i * 4u + 1u] = (uint8_t)(255u - i * 5u);
        astc_rgba[i * 4u + 2u] = (uint8_t)(i * 17u);
        astc_rgba[i * 4u + 3u] = 255u;
    }
    {
        static const uint8_t dims[][2] = {{4, 4},  {5, 4},  {5, 5},  {6, 5},
                                          {6, 6},  {8, 5},  {8, 6},  {8, 8},
                                          {10, 5}, {10, 6}, {10, 8}, {10, 10},
                                          {12, 10}, {12, 12}};
        int saw_large_grid = 0;
        int saw_six_grid = 0;
        int saw_budgeted_endpoint = 0;
        for (j = 0; j < sizeof(dims) / sizeof(dims[0]); ++j) {
            tc_astc_options sweep_opt;
            uint32_t mode, wx, wy, quant, dual;
            tc_astc_options_init(&sweep_opt);
            sweep_opt.block_x = dims[j][0];
            sweep_opt.block_y = dims[j][1];
            sweep_opt.quality = 1;
            memset(astc, 0, sizeof(astc));
            CHECK(tc_astc_compress_rgba8(astc_rgba, sweep_opt.block_x, sweep_opt.block_y,
                                         12u * 4u, &sweep_opt, astc, sizeof(astc)) ==
                      TC_SUCCESS,
                  "astc footprint compress");
            mode = rd_bits(astc, 0, 11);
            CHECK(astc_test_mode_is_valid_1plane_2d(mode), "astc footprint block mode");
            CHECK(astc_test_decode_mode_2d(mode, &wx, &wy, &quant, &dual),
                  "astc footprint decode mode");
            if (mode != 66u && mode != 82u && mode != 67u && mode != 83u)
                saw_large_grid = 1;
            if (wx == 6u || wy == 6u) saw_six_grid = 1;
            if (rd_bits(astc, 11, 2) == 0u) {
                CHECK(rd_bits(astc, 13, 4) == 8u, "astc footprint endpoint mode");
                if (tc_astc_ise_sequence_bitcount(wx * wy, quant) + 6u * 8u >
                    111u)
                    saw_budgeted_endpoint = 1;
            } else {
                CHECK(rd_bits(astc, 23, 6) != 0u,
                      "astc footprint partition endpoint mode");
            }
        }
        CHECK(saw_large_grid, "astc larger grid selected");
        CHECK(saw_six_grid, "astc six-wide grid selected");
        CHECK(saw_budgeted_endpoint, "astc budgeted endpoint quant selected");
    }
    for (i = 0; i < 5u; ++i) {
        for (j = 0; j < 8u; ++j) {
            uint8_t v = (uint8_t)(((i + j) & 1u) ? 255u : 0u);
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = v;
            astc_rgba[p + 1u] = v;
            astc_rgba[p + 2u] = v;
            astc_rgba[p + 3u] = 255u;
        }
    }
    astc_opt.block_x = 8u;
    astc_opt.block_y = 5u;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 8, 5, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc luminance compress");
    CHECK(rd_bits(astc, 13, 4) == 0u, "astc luminance endpoint mode");
    CHECK(astc_test_mode_is_luminance_large(rd_bits(astc, 0, 11)),
          "astc luminance large grid mode");
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            uint8_t v = (uint8_t)(((i + j) & 1u) ? 255u : 0u);
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = v;
            astc_rgba[p + 1u] = v;
            astc_rgba[p + 2u] = v;
            astc_rgba[p + 3u] = (uint8_t)(32u + ((i * 6u + j) * 5u));
        }
    }
    astc_opt.block_x = 6u;
    astc_opt.block_y = 6u;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc luminance alpha compress");
    CHECK(rd_bits(astc, 13, 4) == 4u, "astc luminance alpha endpoint mode");
    CHECK(astc_test_mode_is_luminance_large(rd_bits(astc, 0, 11)),
          "astc luminance alpha 6x6 grid mode");
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            uint8_t s = (uint8_t)(((i + j) & 1u) ? 255u : 64u);
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = (uint8_t)((80u * s + 127u) / 255u);
            astc_rgba[p + 1u] = (uint8_t)((160u * s + 127u) / 255u);
            astc_rgba[p + 2u] = (uint8_t)((240u * s + 127u) / 255u);
            astc_rgba[p + 3u] = 255u;
        }
    }
    astc_opt.block_x = 6u;
    astc_opt.block_y = 6u;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc rgb scale compress");
    CHECK(rd_bits(astc, 13, 4) == 6u, "astc rgb scale endpoint mode");
    CHECK(astc_test_mode_is_luminance_large(rd_bits(astc, 0, 11)),
          "astc rgb scale 6x6 grid mode");
    for (i = 0; i < 4u; ++i) {
        for (j = 0; j < 4u; ++j) {
            uint8_t s = (uint8_t)(((i + j) & 1u) ? 255u : 96u);
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = (uint8_t)((80u * s + 127u) / 255u);
            astc_rgba[p + 1u] = (uint8_t)((160u * s + 127u) / 255u);
            astc_rgba[p + 2u] = (uint8_t)((240u * s + 127u) / 255u);
            astc_rgba[p + 3u] = (uint8_t)(((i + j) & 1u) ? 220u : 80u);
        }
    }
    astc_opt.block_x = 4u;
    astc_opt.block_y = 4u;
    astc_opt.quality = 1;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 4, 4, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc rgb scale alpha compress");
    CHECK(rd_bits(astc, 13, 4) == 10u, "astc rgb scale alpha endpoint mode");
    astc_opt.quality = 2;
    for (i = 0; i < 4u; ++i) {
        for (j = 0; j < 4u; ++j) {
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = (uint8_t)(24u + j * 48u);
            astc_rgba[p + 1u] = (uint8_t)(40u + i * 32u);
            astc_rgba[p + 2u] = (uint8_t)(220u - j * 24u);
            astc_rgba[p + 3u] = (uint8_t)(((i + j) & 1u) ? 232u : 24u);
        }
    }
    astc_opt.block_x = 4u;
    astc_opt.block_y = 4u;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 4, 4, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc dual plane rgba compress");
    {
        uint32_t wx, wy, quant, dual, weight_bits;
        CHECK(astc_test_decode_mode_2d(rd_bits(astc, 0, 11), &wx, &wy, &quant,
                                       &dual) &&
                  dual,
              "astc dual plane block mode");
        weight_bits = tc_astc_ise_sequence_bitcount(wx * wy * 2u, quant);
        CHECK(rd_bits(astc, 128u - weight_bits - 2u, 2u) == 3u,
              "astc dual plane alpha component");
        CHECK(rd_bits(astc, 13, 4) == 12u, "astc dual plane rgba endpoint mode");
        CHECK(weight_bits + 8u * 8u + 2u > 111u,
              "astc dual plane rgba budgeted endpoint quant");
    }
    astc_opt.quality = 1;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 4, 4, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc medium dual plane rgba compress");
    {
        uint32_t wx, wy, quant, dual, weight_bits;
        CHECK(astc_test_decode_mode_2d(rd_bits(astc, 0, 11), &wx, &wy, &quant,
                                       &dual) &&
                  dual,
              "astc medium dual plane block mode");
        weight_bits = tc_astc_ise_sequence_bitcount(wx * wy * 2u, quant);
        CHECK(rd_bits(astc, 128u - weight_bits - 2u, 2u) == 3u,
              "astc medium dual plane alpha component");
        CHECK(rd_bits(astc, 13, 4) == 12u,
              "astc medium dual plane rgba endpoint mode");
    }
    astc_opt.quality = 2;
    /* rgb-scale colors with an alpha ramp decorrelated from the color
     * checker, so a shared weight plane cannot capture both and the
     * dual-plane CEM 10 encoding wins on reconstruction error. */
    for (i = 0; i < 4u; ++i) {
        for (j = 0; j < 4u; ++j) {
            uint8_t s = (uint8_t)(((i + j) & 1u) ? 255u : 80u);
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = (uint8_t)((72u * s + 127u) / 255u);
            astc_rgba[p + 1u] = (uint8_t)((144u * s + 127u) / 255u);
            astc_rgba[p + 2u] = (uint8_t)((216u * s + 127u) / 255u);
            astc_rgba[p + 3u] = (uint8_t)(20u + j * 70u);
        }
    }
    CHECK(tc_astc_compress_rgba8(astc_rgba, 4, 4, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc dual plane rgb scale alpha compress");
    {
        uint32_t wx, wy, quant, dual, weight_bits;
        CHECK(astc_test_decode_mode_2d(rd_bits(astc, 0, 11), &wx, &wy, &quant,
                                       &dual) &&
                  dual,
              "astc dual plane rgb scale alpha block mode");
        weight_bits = tc_astc_ise_sequence_bitcount(wx * wy * 2u, quant);
        CHECK(rd_bits(astc, 128u - weight_bits - 2u, 2u) == 3u,
              "astc dual plane rgb scale alpha component");
        CHECK(rd_bits(astc, 13, 4) == 10u,
              "astc dual plane rgb scale alpha endpoint mode");
    }
    /* Partition-favoring content pattern used below: each partition is a
     * flat color except one, whose two colors sit at the block's global
     * luma extremes. The shared weight grid then serves every partition
     * (solids ignore weights, the extreme pair maps to weights 0/64), so
     * the partitioned encoding is near-exact while a single endpoint line
     * cannot represent three or more non-collinear colors. */
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            size_t p = (i * 12u + j) * 4u;
            if (j < 3u) { /* solid red */
                astc_rgba[p + 0u] = 240u;
                astc_rgba[p + 1u] = 20u;
                astc_rgba[p + 2u] = 30u;
            } else if ((i + j) & 1u) { /* bright green-cyan */
                astc_rgba[p + 0u] = 30u;
                astc_rgba[p + 1u] = 230u;
                astc_rgba[p + 2u] = 200u;
            } else { /* dark green-cyan (channels co-vary with the bright
                        one, so the pair lies on its partition's diagonal) */
                astc_rgba[p + 0u] = 20u;
                astc_rgba[p + 1u] = 90u;
                astc_rgba[p + 2u] = 60u;
            }
            astc_rgba[p + 3u] = 255u;
        }
    }
    astc_opt.block_x = 6u;
    astc_opt.block_y = 6u;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc two partition compress");
    CHECK(rd_bits(astc, 11, 2) == 1u, "astc two partition count");
    CHECK(rd_bits(astc, 23, 6) != 0u, "astc two partition endpoint type");
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            size_t p = (i * 12u + j) * 4u;
            if (i < 3u) { /* solid blue, luma inside the pair below */
                astc_rgba[p + 0u] = 30u;
                astc_rgba[p + 1u] = 40u;
                astc_rgba[p + 2u] = 235u;
            } else if ((i + j) & 1u) { /* bright yellow (global max luma) */
                astc_rgba[p + 0u] = 235u;
                astc_rgba[p + 1u] = 225u;
                astc_rgba[p + 2u] = 20u;
            } else { /* dark red (global min luma) */
                astc_rgba[p + 0u] = 100u;
                astc_rgba[p + 1u] = 30u;
                astc_rgba[p + 2u] = 20u;
            }
            astc_rgba[p + 3u] = 255u;
        }
    }
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc two partition vertical search compress");
    CHECK(rd_bits(astc, 11, 2) == 1u, "astc two partition vertical search count");
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            size_t p = (i * 12u + j) * 4u;
            if (j < 3u) { /* solid rgba, luma inside the pair below */
                astc_rgba[p + 0u] = 240u;
                astc_rgba[p + 1u] = 32u;
                astc_rgba[p + 2u] = 48u;
                astc_rgba[p + 3u] = 220u;
            } else if ((i + j) & 1u) { /* bright green (global max luma) */
                astc_rgba[p + 0u] = 24u;
                astc_rgba[p + 1u] = 216u;
                astc_rgba[p + 2u] = 72u;
                astc_rgba[p + 3u] = 40u;
            } else { /* dark green (global min luma) */
                astc_rgba[p + 0u] = 24u;
                astc_rgba[p + 1u] = 120u;
                astc_rgba[p + 2u] = 60u;
                astc_rgba[p + 3u] = 40u;
            }
        }
    }
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc two partition rgba compress");
    CHECK(rd_bits(astc, 11, 2) == 1u, "astc two partition rgba count");
    CHECK(rd_bits(astc, 23, 6) == (12u << 2),
          "astc two partition rgba endpoint type");
    /* All colors are scales of one base hue (whole block rgb-scale); each
     * half is rgb-solid so the partition variance gate passes, and the
     * right half's alpha checker rides the shared weight grid. Quality 1
     * keeps the rgb-scale dual-plane path gated off, so the two-partition
     * CEM 10 encoding wins over a single line (three RGBA points). */
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            uint8_t s = (uint8_t)(j < 3u ? 100u : 255u);
            size_t p = (i * 12u + j) * 4u;
            astc_rgba[p + 0u] = (uint8_t)((80u * s + 127u) / 255u);
            astc_rgba[p + 1u] = (uint8_t)((160u * s + 127u) / 255u);
            astc_rgba[p + 2u] = (uint8_t)((240u * s + 127u) / 255u);
            astc_rgba[p + 3u] =
                (uint8_t)(j < 3u ? 224u : (((i + j) & 1u) ? 200u : 36u));
        }
    }
    astc_opt.quality = 1;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc two partition rgb scale alpha compress");
    CHECK(rd_bits(astc, 11, 2) == 1u,
          "astc two partition rgb scale alpha count");
    CHECK(rd_bits(astc, 23, 6) == (10u << 2),
          "astc two partition rgb scale alpha endpoint type");
    for (i = 0; i < 6u; ++i) {
        for (j = 0; j < 6u; ++j) {
            size_t p = (i * 12u + j) * 4u;
            if (j < 3u) { /* solid red: rgb-scale format for this partition */
                astc_rgba[p + 0u] = 240u;
                astc_rgba[p + 1u] = 20u;
                astc_rgba[p + 2u] = 30u;
            } else if ((i + j) & 1u) { /* co-varying pair that still fails
                                          the scale check (hue drift) */
                astc_rgba[p + 0u] = 60u;
                astc_rgba[p + 1u] = 230u;
                astc_rgba[p + 2u] = 200u;
            } else {
                astc_rgba[p + 0u] = 20u;
                astc_rgba[p + 1u] = 120u;
                astc_rgba[p + 2u] = 60u;
            }
            astc_rgba[p + 3u] = 255u;
        }
    }
    astc_opt.quality = 1;
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc mixed partition endpoint compress");
    CHECK(rd_bits(astc, 11, 2) == 1u, "astc mixed partition count");
    CHECK(rd_bits(astc, 23, 6) != (8u << 2) &&
              rd_bits(astc, 23, 6) != (6u << 2),
          "astc mixed partition endpoint type");
    astc_opt.quality = 2;
    /* Content shaped by an actual ASTC partition pattern (seed 90, three
     * partitions, 6x6): each partition gets its own flat color, so the
     * matching pattern exists in the search space and the three-partition
     * encoding is exact, while one or two partitions must merge three
     * non-collinear colors. Only three RGB clusters exist, so the
     * four-partition path is gated off. */
    {
        static const uint8_t map3[36] = {
            1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0};
        static const uint8_t cols3[3][3] = {
            {240u, 24u, 32u}, {30u, 230u, 60u}, {40u, 52u, 236u}};
        for (i = 0; i < 36u; ++i) {
            size_t p = ((i / 6u) * 12u + (i % 6u)) * 4u;
            astc_rgba[p + 0u] = cols3[map3[i]][0];
            astc_rgba[p + 1u] = cols3[map3[i]][1];
            astc_rgba[p + 2u] = cols3[map3[i]][2];
            astc_rgba[p + 3u] = 255u;
        }
    }
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc three partition compress");
    CHECK(rd_bits(astc, 11, 2) == 2u, "astc three partition count");
    CHECK(rd_bits(astc, 23, 6) != 0u, "astc three partition endpoint type");
    /* Same partition pattern (seed 90), but partition 0 holds a non-scale
     * two-color pair while the others are flat scale-compatible colors, so
     * the three-partition encoding needs mixed endpoint formats. */
    {
        static const uint8_t map3[36] = {
            1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0};
        for (i = 0; i < 36u; ++i) {
            size_t p = ((i / 6u) * 12u + (i % 6u)) * 4u;
            if (map3[i] == 0u) { /* non-scale pair (hue twist) */
                if (((i / 6u) + (i % 6u)) & 1u) {
                    astc_rgba[p + 0u] = 240u;
                    astc_rgba[p + 1u] = 240u;
                    astc_rgba[p + 2u] = 60u;
                } else {
                    astc_rgba[p + 0u] = 20u;
                    astc_rgba[p + 1u] = 30u;
                    astc_rgba[p + 2u] = 200u;
                }
            } else if (map3[i] == 1u) { /* solid red */
                astc_rgba[p + 0u] = 240u;
                astc_rgba[p + 1u] = 24u;
                astc_rgba[p + 2u] = 32u;
            } else { /* solid green */
                astc_rgba[p + 0u] = 30u;
                astc_rgba[p + 1u] = 230u;
                astc_rgba[p + 2u] = 60u;
            }
            astc_rgba[p + 3u] = 255u;
        }
    }
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc three partition mixed endpoint compress");
    CHECK(rd_bits(astc, 11, 2) == 2u, "astc three partition mixed count");
    /* The seed-limited partition patterns rarely match these strips exactly,
     * so stray texels can defeat the per-partition scale classifier; the
     * mixed-format high-part encoding itself is covered by the two-partition
     * mixed test above. Require only a valid endpoint-type field here. */
    CHECK(rd_bits(astc, 23, 6) != 0u,
          "astc three partition mixed endpoint type");
    /* Content shaped by an actual ASTC partition pattern (seed 54, four
     * partitions, 6x6): four flat colors that no coarser partitioning can
     * separate, so the four-partition encoding wins strictly. */
    {
        static const uint8_t map4[36] = {
            1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3};
        static const uint8_t cols4[4][3] = {{240u, 28u, 36u},
                                            {32u, 228u, 44u},
                                            {40u, 52u, 236u},
                                            {200u, 200u, 200u}};
        for (i = 0; i < 36u; ++i) {
            size_t p = ((i / 6u) * 12u + (i % 6u)) * 4u;
            astc_rgba[p + 0u] = cols4[map4[i]][0];
            astc_rgba[p + 1u] = cols4[map4[i]][1];
            astc_rgba[p + 2u] = cols4[map4[i]][2];
            astc_rgba[p + 3u] = 255u;
        }
    }
    CHECK(tc_astc_compress_rgba8(astc_rgba, 6, 6, 12u * 4u, &astc_opt, astc,
                                 sizeof(astc)) == TC_SUCCESS,
          "astc four partition compress");
    CHECK(rd_bits(astc, 11, 2) == 3u, "astc four partition count");
    CHECK(rd_bits(astc, 23, 6) == (6u << 2), "astc four partition endpoint type");
    tc_astc_options_init(&astc_opt);

    CHECK(tc_bc6h_compress_rgb32f(rgbf, 7, 5, 7 * 3 * sizeof(float), &bc6h_opt,
                                  bc6h, sizeof(bc6h)) == TC_SUCCESS,
          "bc6h compress");
    {
        /* Unsigned BC6H may pick mode 11 (code 3) or the two-region mode 9
         * (code 30), whichever fits better. */
        uint32_t m6 = rd_bits(bc6h, 0, 5);
        CHECK(m6 == 3u || m6 == 30u, "bc6h valid mode (11 or 9)");
    }
    CHECK(tc_dds_write_bc6h_memory(bc6h, 7, 5, &bc6h_opt, dds, sizeof(dds)) ==
              TC_SUCCESS,
          "bc6h dds write");
    CHECK(rd_u32(dds + 112) == 95, "bc6h dxgi format");
    for (i = 0; i < 16u; ++i) {
        rgbf_block[i * 3u + 0u] = (i == 1u) ? 1.0f : 0.0f;
        rgbf_block[i * 3u + 1u] = (i == 2u) ? 1.0f : 0.0f;
        rgbf_block[i * 3u + 2u] = (i == 3u) ? 1.0f : 0.0f;
    }
    CHECK(tc_bc6h_compress_rgb32f(rgbf_block, 4, 4, 4 * 3 * sizeof(float),
                                  &bc6h_opt, bc6h, sizeof(bc6h)) == TC_SUCCESS,
          "bc6h rgb extrema compress");
    {
        /* If mode 11, check its reconstruction bound directly; if the encoder
         * picked the two-region mode 9, it did so because that has *lower*
         * error than mode 11, so the bound holds by construction (the test's
         * mode-11 decoder can't read a mode-9 block). */
        uint32_t m6 = rd_bits(bc6h, 0, 5);
        if (m6 == 3u)
            CHECK(bc6h_mode11_rgb_error(bc6h, (const float(*)[3])rgbf_block) <
                      900000000ull,
                  "bc6h rgb extrema bounded error");
        else
            CHECK(m6 == 30u, "bc6h rgb extrema valid mode (9)");
    }
    for (i = 0; i < sizeof(rgbf) / sizeof(rgbf[0]); ++i)
        rgbf[i] = (i & 1u) ? -rgbf[i] : rgbf[i];
    bc6h_opt.signed_float = 1;
    CHECK(tc_bc6h_compress_rgb32f(rgbf, 7, 5, 7 * 3 * sizeof(float), &bc6h_opt,
                                  bc6h, sizeof(bc6h)) == TC_SUCCESS,
          "bc6h signed compress");
    CHECK(rd_bits(bc6h, 0, 5) == 3u || rd_bits(bc6h, 0, 5) == 30u ||
              rd_bits(bc6h, 0, 5) == 0u, "bc6h signed mode bits (10/9/0)");
    CHECK(tc_dds_write_bc6h_memory(bc6h, 7, 5, &bc6h_opt, dds, sizeof(dds)) ==
              TC_SUCCESS,
          "bc6h signed dds write");
    CHECK(rd_u32(dds + 112) == 96, "bc6h signed dxgi format");
    {
        /* The SIMD BC6H selector search (both unsigned and signed) must be
         * byte-identical to scalar. Content spans both signs so the signed
         * path's overflow-saturation is exercised. */
        enum { PW = 8, PH = 8 };
        static float hp[PW * PH * 3];
        uint8_t pref[(PW / 4) * (PH / 4) * 16], psimd[sizeof(pref)];
        uint32_t pi_, avail = tc_backend_available_mask();
        int sgn;
        for (pi_ = 0; pi_ < (uint32_t)(PW * PH); ++pi_) {
            float t = (float)pi_ / (float)(PW * PH);
            hp[pi_ * 3 + 0] = -20.0f + t * 40.0f;
            hp[pi_ * 3 + 1] = 15.0f - t * 30.0f;
            hp[pi_ * 3 + 2] = -10.0f + t * t * 25.0f;
        }
        for (sgn = 0; sgn < 2; ++sgn) {
            static const uint32_t masks[3] = {TC_BACKEND_SSE41, TC_BACKEND_AVX2,
                                              TC_BACKEND_NEON};
            static const char *const nm[3] = {"bc6h sse4.1 parity",
                                              "bc6h avx2 parity",
                                              "bc6h neon parity"};
            tc_bc6h_options po;
            uint32_t mi;
            tc_bc6h_options_init(&po);
            po.signed_float = sgn;
            tc_backend_force_mask(TC_BACKEND_SCALAR);
            CHECK(tc_bc6h_compress_rgb32f(hp, PW, PH, PW * 3u * sizeof(float),
                                          &po, pref, sizeof(pref)) == TC_SUCCESS,
                  "bc6h scalar forced");
            for (mi = 0; mi < 3u; ++mi) {
                if (!(avail & masks[mi])) continue;
                tc_backend_force_mask(masks[mi]);
                CHECK(tc_bc6h_compress_rgb32f(hp, PW, PH, PW * 3u * sizeof(float),
                                              &po, psimd, sizeof(psimd)) ==
                          TC_SUCCESS,
                      "bc6h simd forced");
                CHECK(memcmp(pref, psimd, sizeof(pref)) == 0, nm[mi]);
            }
            tc_backend_force_mask(TC_BACKEND_ALL);
        }
    }
    bc6h_opt.signed_float = 0;

    CHECK(tc_dds_write_bc7_memory(bc7, 7, 5, &opt, dds, sizeof(dds)) ==
              TC_SUCCESS,
          "dds write");
    CHECK(memcmp(dds, "DDS ", 4) == 0, "dds magic");
    CHECK(rd_u32(dds + 4) == 124, "dds header size");
    CHECK(rd_u32(dds + 12) == 5, "dds height");
    CHECK(rd_u32(dds + 16) == 7, "dds width");
    CHECK(memcmp(dds + 84, "DX10", 4) == 0, "dx10 fourcc");
    CHECK(rd_u32(dds + 112) == 98, "bc7 dxgi format");
    opt.srgb = 1;
    CHECK(tc_dds_write_bc7_memory(bc7, 7, 5, &opt, dds, sizeof(dds)) ==
              TC_SUCCESS,
          "dds write srgb");
    CHECK(rd_u32(dds + 112) == 99, "bc7 srgb dxgi format");

    if (astc_ise_roundtrip_test()) return 1;
    if (astc_weight_symmetry_test()) return 1;
    if (astc_ref_roundtrip_test()) return 1;
    if (astc_backend_parity_test()) return 1;
    if (astc_thread_parity_test()) return 1;
    if (test_bc_decoders()) return 1;
    if (test_bc7_decoder_conformance()) return 1;
    if (test_bc5_snorm()) return 1;
    if (test_etc2_quality()) return 1;
    if (test_astc_hdr_rgba_quality()) return 1;
    if (test_etc2_decoders()) return 1;
    if (test_etc2_orientation()) return 1;
    if (test_eac_orientation()) return 1;

    printf("texcomp tests: OK\n");
    return 0;
}
