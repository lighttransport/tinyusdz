/*
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure-C ASTC HDR reference decoder -- test/validation only. Decodes the HDR
 * blocks the texcomp HDR encoder emits (FP16 void-extent + CEM 11 single-
 * subset, dual-plane and 2-subset) to float RGBA, so texcomp-test can validate
 * HDR round-trips without the C++ deps/astcenc. Reuses the block-mode / weight /
 * ISE / partition machinery from astc_ref_decode.h; the CEM 11 endpoint unpack
 * (tc_astc_cem11_unpack) and LNS->sf16 conversion (tc_astc_lns16_to_sf16) come
 * from the library and are themselves cross-checked against deps/astcenc in the
 * astc-hdr gate. Only CEM 11 is handled (the HDR encoder emits no other CEM).
 */
#ifndef TC_ASTC_HDR_REF_DECODE_H
#define TC_ASTC_HDR_REF_DECODE_H

#include "astc_ref_decode.h"
#include "../src/texcomp_internal.h"

#include <stdint.h>
#include <string.h>

/* IEEE half (sf16 bit pattern) -> float. */
static float ahref_half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1fu;
    uint32_t man = h & 0x3ffu;
    uint32_t bits;
    float f;
    if (exp == 0u) {
        if (man == 0u) {
            bits = sign; /* +/-0 */
        } else {
            /* subnormal: normalize */
            int e = -1;
            do {
                ++e;
                man <<= 1;
            } while (!(man & 0x400u));
            man &= 0x3ffu;
            bits = sign | ((uint32_t)(127 - 15 - e) << 23) | (man << 13);
        }
    } else if (exp == 0x1fu) {
        bits = sign | 0x7f800000u | (man << 13); /* inf / nan */
    } else {
        bits = sign | ((exp + (127u - 15u)) << 23) | (man << 13);
    }
    memcpy(&f, &bits, 4u);
    return f;
}

/* Decode one HDR ASTC block (footprint bx x by) to bx*by RGBA floats (row
 * major). Returns 1 on success, 0 on any block this decoder does not handle. */
static int ahref_decode_block_hdr(const uint8_t block[16], uint32_t bx,
                                  uint32_t by, float *out_rgba) {
    uint32_t mode = aref_rd_bits(block, 0, 11);
    uint32_t wx, wy, wquant, dual;
    uint32_t part_count, weight_count, weight_bits;
    uint32_t pindex = 0, color_start, ccs = 3u, ccs_bits = 0;
    uint32_t cems[4] = {0, 0, 0, 0};
    int lns0[4][3], lns1[4][3]; /* per-subset LNS endpoints */
    uint8_t rev[16], wsyms[64], wgrid[2][64], csyms[18], vals[18];
    uint32_t vcount = 0, used, avail, i, x, y;
    int cq = -1, small_block;

    if ((mode & 0x1ffu) == 0x1fcu) { /* void-extent (constant colour) */
        float px[4];
        uint32_t c;
        if (!(mode & (1u << 9))) return 0; /* LDR void-extent: not ours */
        for (c = 0; c < 4u; ++c)
            px[c] = ahref_half_to_float((uint16_t)(block[8u + c * 2u] |
                                                   (block[9u + c * 2u] << 8)));
        for (i = 0; i < bx * by; ++i) memcpy(out_rgba + i * 4u, px, 4u * 4u);
        return 1;
    }

    if (!aref_decode_block_mode_2d(mode, &wx, &wy, &wquant, &dual)) return 0;
    if (wx > bx || wy > by) return 0;
    part_count = aref_rd_bits(block, 11, 2) + 1u;
    if (dual && part_count == 4u) return 0;
    weight_count = wx * wy * (dual ? 2u : 1u);
    if (weight_count > 64u) return 0;
    weight_bits = aref_ise_bitcount(weight_count, wquant);
    if (weight_bits < 24u || weight_bits > 96u) return 0;

    for (i = 0; i < 16u; ++i) rev[i] = aref_bitrev8(block[15u - i]);
    if (!aref_ise_decode(wquant, weight_count, rev, 0, wsyms)) return 0;
    for (i = 0; i < wx * wy; ++i) {
        if (dual) {
            wgrid[0][i] = aref_weight_unquant[wquant][wsyms[i * 2u]];
            wgrid[1][i] = aref_weight_unquant[wquant][wsyms[i * 2u + 1u]];
        } else {
            wgrid[0][i] = aref_weight_unquant[wquant][wsyms[i]];
        }
    }

    if (part_count == 1u) {
        cems[0] = aref_rd_bits(block, 13, 4);
        color_start = 17u;
    } else {
        uint32_t cf = aref_rd_bits(block, 23, 6);
        pindex = aref_rd_bits(block, 13, 10);
        color_start = 29u;
        if ((cf & 3u) != 0u) return 0; /* HDR encoder only emits all-same CEM */
        for (i = 0; i < part_count; ++i) cems[i] = cf >> 2;
    }
    for (i = 0; i < part_count; ++i)
        if (cems[i] != 11u && cems[i] != 7u) return 0; /* CEM 7 / 11 only */

    if (dual) {
        ccs_bits = 2u;
        if (weight_bits + 2u > 128u - color_start) return 0;
        ccs = aref_rd_bits(block, 128u - weight_bits - 2u, 2);
    }

    vcount = part_count * (cems[0] == 7u ? 4u : 6u); /* CEM 7=4, CEM 11=6 */
    used = color_start + weight_bits + ccs_bits;
    if (used > 128u) return 0;
    avail = 128u - used;
    for (i = 20u; i >= 4u; --i) {
        if (aref_ise_bitcount(vcount, i) <= avail) {
            cq = (int)i;
            break;
        }
        if (i == 4u) break;
    }
    if (cq < 0) return 0;
    if (!aref_ise_decode((uint32_t)cq, vcount, block, color_start, csyms))
        return 0;
    for (i = 0; i < vcount; ++i)
        vals[i] = (cq == 20) ? csyms[i] : aref_color_unquant[cq - 4][csyms[i]];

    {
        uint32_t off = 0;
        for (i = 0; i < part_count; ++i) {
            if (cems[i] == 7u)
                tc_astc_cem7_unpack(vals + off, lns0[i], lns1[i]);
            else
                tc_astc_cem11_unpack(vals + off, lns0[i], lns1[i]);
            off += (cems[i] == 7u) ? 4u : 6u;
        }
    }

    small_block = bx * by < 32u;
    for (y = 0; y < by; ++y)
        for (x = 0; x < bx; ++x) {
            uint32_t part =
                part_count == 1u ? 0u
                                 : aref_select_partition(pindex, x, y,
                                                         part_count, small_block);
            uint32_t w0 = aref_infill_weight(wgrid[0], wx, wy, bx, by, x, y);
            uint32_t w1 =
                dual ? aref_infill_weight(wgrid[1], wx, wy, bx, by, x, y) : w0;
            uint32_t c;
            float *o = out_rgba + (y * bx + x) * 4u;
            if (part >= part_count) part = part_count - 1u;
            for (c = 0; c < 3u; ++c) {
                uint32_t w = (dual && c == ccs) ? w1 : w0;
                int rec = (lns0[part][c] * (int)(64u - w) + lns1[part][c] * (int)w +
                           32) >> 6;
                o[c] = ahref_half_to_float(tc_astc_lns16_to_sf16(rec));
            }
            o[3] = 1.0f; /* HDR CEM 11 has no alpha; astcenc yields 1.0 */
        }
    return 1;
}

#endif /* TC_ASTC_HDR_REF_DECODE_H */
