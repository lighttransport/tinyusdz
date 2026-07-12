/*
 * TinyEXR texcomp - BC6H decoder
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * A faithful C port of bcdec's `bcdec_bc6h_half` (all 14 BC6H modes: the four
 * one-region modes and the ten two-region modes, unsigned and signed).
 *
 * Ported from bcdec.h (Sergii Kudlai), MIT / public-domain dual license:
 *   https://github.com/iOrange/bcdec  -- see tools/texcomp/NOTICE.md.
 *
 * This was previously test/bc6h_ref_decode.h, used only as the oracle for
 * texcomp-bc6h-gate. It is now the library's BC6H decoder; the gate still
 * validates the (independently written, in-house) encoder against it.
 */
#include "texcomp.h"
#include "texcomp_internal.h"

#include <string.h>

typedef struct {
    uint64_t low, high;
} tc_bc6h_bs;

static int tc_bc6h_rd(tc_bc6h_bs *bs, int n) {
    unsigned int mask = (n >= 32) ? 0xffffffffu : ((1u << n) - 1u);
    unsigned int bits = (unsigned int)(bs->low & mask);
    bs->low >>= n;
    bs->low |= (bs->high & mask) << (64 - n);
    bs->high >>= n;
    return (int)bits;
}

/* Read n bits then bit-reverse them (used by the wide one-region modes). */
static int tc_bc6h_rd_r(tc_bc6h_bs *bs, int n) {
    int bits = tc_bc6h_rd(bs, n), result = 0;
    while (n--) {
        result <<= 1;
        result |= (bits & 1);
        bits >>= 1;
    }
    return result;
}

static int tc_bc6h_sext(int val, int bits) {
    return (val << (32 - bits)) >> (32 - bits);
}

static int tc_bc6h_xform_inv(int val, int a0, int bits, int is_signed) {
    val = (val + a0) & ((1 << bits) - 1);
    if (is_signed) val = tc_bc6h_sext(val, bits);
    return val;
}

static int tc_bc6h_unq(int val, int bits, int is_signed) {
    int unq, s = 0;
    if (!is_signed) {
        if (bits >= 15) unq = val;
        else if (!val) unq = 0;
        else if (val == ((1 << bits) - 1)) unq = 0xFFFF;
        else unq = ((val << 16) + 0x8000) >> bits;
    } else {
        if (bits >= 16) unq = val;
        else {
            if (val < 0) { s = 1; val = -val; }
            if (val == 0) unq = 0;
            else if (val >= ((1 << (bits - 1)) - 1)) unq = 0x7FFF;
            else unq = ((val << 15) + 0x4000) >> (bits - 1);
            if (s) unq = -unq;
        }
    }
    return unq;
}

static uint16_t tc_bc6h_finish(int val, int is_signed) {
    int s;
    if (!is_signed) return (uint16_t)((val * 31) >> 6);
    val = (val < 0) ? -(((-val) * 31) >> 5) : (val * 31) >> 5;
    s = 0;
    if (val < 0) { s = 0x8000; val = -val; }
    return (uint16_t)(s | val);
}

static int tc_bc6h_interp(int a, int b, const int *w, int idx) {
    return (a * (64 - w[idx]) + b * w[idx] + 32) >> 6;
}

/* Decode one BC6H block to 16 texels of FP16 RGB (row-major). */
void tc_bc6h_decode_block_half(const uint8_t blk[16], int is_signed,
                                     uint16_t out[16][3]) {
    static const signed char abits[4][14] = {
        {10, 7, 11, 11, 11, 9, 8, 8, 8, 6, 10, 11, 12, 16},  /* W  */
        {5, 6, 5, 4, 4, 5, 6, 5, 5, 6, 10, 9, 8, 4},         /* dR */
        {5, 6, 4, 5, 4, 5, 5, 6, 5, 6, 10, 9, 8, 4},         /* dG */
        {5, 6, 4, 4, 5, 5, 5, 5, 6, 6, 10, 9, 8, 4}          /* dB */
    };
    static const unsigned char psets[32][4][4] = {
        {{128, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 1, 129}},
        {{128, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 129}},
        {{128, 1, 1, 1}, {0, 1, 1, 1}, {0, 1, 1, 1}, {0, 1, 1, 129}},
        {{128, 0, 0, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 1, 129}},
        {{128, 0, 1, 1}, {0, 1, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 1}, {0, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 1}, {0, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 129}},
        {{128, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}, {0, 1, 1, 129}},
        {{128, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 0}, {1, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {1, 1, 1, 129}},
        {{128, 0, 0, 0}, {1, 0, 0, 0}, {1, 1, 1, 0}, {1, 1, 1, 129}},
        {{128, 1, 129, 1}, {0, 0, 0, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {{128, 0, 0, 0}, {0, 0, 0, 0}, {129, 0, 0, 0}, {1, 1, 1, 0}},
        {{128, 1, 129, 1}, {0, 0, 1, 1}, {0, 0, 0, 1}, {0, 0, 0, 0}},
        {{128, 0, 129, 1}, {0, 0, 0, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {{128, 0, 0, 0}, {1, 0, 0, 0}, {129, 1, 0, 0}, {1, 1, 1, 0}},
        {{128, 0, 0, 0}, {0, 0, 0, 0}, {129, 0, 0, 0}, {1, 1, 0, 0}},
        {{128, 1, 1, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 0, 129}},
        {{128, 0, 129, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 0}},
        {{128, 0, 0, 0}, {1, 0, 0, 0}, {129, 0, 0, 0}, {1, 1, 0, 0}},
        {{128, 1, 129, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}},
        {{128, 0, 129, 1}, {0, 1, 1, 0}, {0, 1, 1, 0}, {1, 1, 0, 0}},
        {{128, 0, 0, 1}, {0, 1, 1, 1}, {129, 1, 1, 0}, {1, 0, 0, 0}},
        {{128, 0, 0, 0}, {1, 1, 1, 1}, {129, 1, 1, 1}, {0, 0, 0, 0}},
        {{128, 1, 129, 1}, {0, 0, 0, 1}, {1, 0, 0, 0}, {1, 1, 1, 0}},
        {{128, 0, 129, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 0, 0}}
    };
    static const int w3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
    static const int w4[16] = {0,  4,  9,  13, 17, 21, 26, 30,
                               34, 38, 43, 47, 51, 55, 60, 64};
    tc_bc6h_bs bs;
    int mode, partition = 0, nparts, i, j, ab0;
    int r[4] = {0, 0, 0, 0}, g[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0};
    const int *weights;

    bs.low = (uint64_t)blk[0] | ((uint64_t)blk[1] << 8) |
             ((uint64_t)blk[2] << 16) | ((uint64_t)blk[3] << 24) |
             ((uint64_t)blk[4] << 32) | ((uint64_t)blk[5] << 40) |
             ((uint64_t)blk[6] << 48) | ((uint64_t)blk[7] << 56);
    bs.high = (uint64_t)blk[8] | ((uint64_t)blk[9] << 8) |
              ((uint64_t)blk[10] << 16) | ((uint64_t)blk[11] << 24) |
              ((uint64_t)blk[12] << 32) | ((uint64_t)blk[13] << 40) |
              ((uint64_t)blk[14] << 48) | ((uint64_t)blk[15] << 56);

    mode = tc_bc6h_rd(&bs, 2);
    if (mode > 1) mode |= (tc_bc6h_rd(&bs, 3) << 2);

    switch (mode) {
    case 0x00: /* 10.555 x3 */
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 5);
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 0;
        break;
    case 0x01: /* 7.666 x3 */
        g[2] |= tc_bc6h_rd(&bs, 1) << 5;
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        g[3] |= tc_bc6h_rd(&bs, 1) << 5;
        r[0] |= tc_bc6h_rd(&bs, 7);
        b[3] |= tc_bc6h_rd(&bs, 1);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[0] |= tc_bc6h_rd(&bs, 7);
        b[2] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[0] |= tc_bc6h_rd(&bs, 7);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        b[3] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[1] |= tc_bc6h_rd(&bs, 6);
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 6);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 6);
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 6);
        r[3] |= tc_bc6h_rd(&bs, 6);
        partition = tc_bc6h_rd(&bs, 5);
        mode = 1;
        break;
    case 0x02: /* 11.555 / 11.444 / 11.444 */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 5);
        r[0] |= tc_bc6h_rd(&bs, 1) << 10;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 4);
        g[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[3] |= tc_bc6h_rd(&bs, 1);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 4);
        b[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 2;
        break;
    case 0x06: /* 11.444 / 11.555 / 11.444 */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 4);
        r[0] |= tc_bc6h_rd(&bs, 1) << 10;
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 5);
        g[0] |= tc_bc6h_rd(&bs, 1) << 10;
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 4);
        b[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 4);
        b[3] |= tc_bc6h_rd(&bs, 1);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 4);
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 3;
        break;
    case 0x0a: /* 11.444 / 11.444 / 11.555 */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 4);
        r[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 4);
        g[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[3] |= tc_bc6h_rd(&bs, 1);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 5);
        b[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 4);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 4);
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 4;
        break;
    case 0x0e: /* 9.555 x3 */
        r[0] |= tc_bc6h_rd(&bs, 9);
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[0] |= tc_bc6h_rd(&bs, 9);
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[0] |= tc_bc6h_rd(&bs, 9);
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[1] |= tc_bc6h_rd(&bs, 5);
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 5;
        break;
    case 0x12: /* 8.666 / 8.555 / 8.555 */
        r[0] |= tc_bc6h_rd(&bs, 8);
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[0] |= tc_bc6h_rd(&bs, 8);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[0] |= tc_bc6h_rd(&bs, 8);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[1] |= tc_bc6h_rd(&bs, 6);
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 6);
        r[3] |= tc_bc6h_rd(&bs, 6);
        partition = tc_bc6h_rd(&bs, 5);
        mode = 6;
        break;
    case 0x16: /* 8.555 / 8.666 / 8.555 */
        r[0] |= tc_bc6h_rd(&bs, 8);
        b[3] |= tc_bc6h_rd(&bs, 1);
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[0] |= tc_bc6h_rd(&bs, 8);
        g[2] |= tc_bc6h_rd(&bs, 1) << 5;
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[0] |= tc_bc6h_rd(&bs, 8);
        g[3] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[1] |= tc_bc6h_rd(&bs, 5);
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 6);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 7;
        break;
    case 0x1a: /* 8.555 / 8.555 / 8.666 */
        r[0] |= tc_bc6h_rd(&bs, 8);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[0] |= tc_bc6h_rd(&bs, 8);
        b[2] |= tc_bc6h_rd(&bs, 1) << 5;
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[0] |= tc_bc6h_rd(&bs, 8);
        b[3] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[1] |= tc_bc6h_rd(&bs, 5);
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 6);
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        r[3] |= tc_bc6h_rd(&bs, 5);
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        partition = tc_bc6h_rd(&bs, 5);
        mode = 8;
        break;
    case 0x1e: /* 6.666 x3 (texcomp's two-region "mode 9") */
        r[0] |= tc_bc6h_rd(&bs, 6);
        g[3] |= tc_bc6h_rd(&bs, 1) << 4;
        b[3] |= tc_bc6h_rd(&bs, 1);
        b[3] |= tc_bc6h_rd(&bs, 1) << 1;
        b[2] |= tc_bc6h_rd(&bs, 1) << 4;
        g[0] |= tc_bc6h_rd(&bs, 6);
        g[2] |= tc_bc6h_rd(&bs, 1) << 5;
        b[2] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 2;
        g[2] |= tc_bc6h_rd(&bs, 1) << 4;
        b[0] |= tc_bc6h_rd(&bs, 6);
        g[3] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 3;
        b[3] |= tc_bc6h_rd(&bs, 1) << 5;
        b[3] |= tc_bc6h_rd(&bs, 1) << 4;
        r[1] |= tc_bc6h_rd(&bs, 6);
        g[2] |= tc_bc6h_rd(&bs, 4);
        g[1] |= tc_bc6h_rd(&bs, 6);
        g[3] |= tc_bc6h_rd(&bs, 4);
        b[1] |= tc_bc6h_rd(&bs, 6);
        b[2] |= tc_bc6h_rd(&bs, 4);
        r[2] |= tc_bc6h_rd(&bs, 6);
        r[3] |= tc_bc6h_rd(&bs, 6);
        partition = tc_bc6h_rd(&bs, 5);
        mode = 9;
        break;
    case 0x03: /* one-region 10.10 x3 (texcomp's "mode 11") */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 10);
        g[1] |= tc_bc6h_rd(&bs, 10);
        b[1] |= tc_bc6h_rd(&bs, 10);
        mode = 10;
        break;
    case 0x07: /* one-region 11.9 x3 */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 9);
        r[0] |= tc_bc6h_rd(&bs, 1) << 10;
        g[1] |= tc_bc6h_rd(&bs, 9);
        g[0] |= tc_bc6h_rd(&bs, 1) << 10;
        b[1] |= tc_bc6h_rd(&bs, 9);
        b[0] |= tc_bc6h_rd(&bs, 1) << 10;
        mode = 11;
        break;
    case 0x0b: /* one-region 12.8 x3 */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 8);
        r[0] |= tc_bc6h_rd_r(&bs, 2) << 10;
        g[1] |= tc_bc6h_rd(&bs, 8);
        g[0] |= tc_bc6h_rd_r(&bs, 2) << 10;
        b[1] |= tc_bc6h_rd(&bs, 8);
        b[0] |= tc_bc6h_rd_r(&bs, 2) << 10;
        mode = 12;
        break;
    case 0x0f: /* one-region 16.4 x3 */
        r[0] |= tc_bc6h_rd(&bs, 10);
        g[0] |= tc_bc6h_rd(&bs, 10);
        b[0] |= tc_bc6h_rd(&bs, 10);
        r[1] |= tc_bc6h_rd(&bs, 4);
        r[0] |= tc_bc6h_rd_r(&bs, 6) << 10;
        g[1] |= tc_bc6h_rd(&bs, 4);
        g[0] |= tc_bc6h_rd_r(&bs, 6) << 10;
        b[1] |= tc_bc6h_rd(&bs, 4);
        b[0] |= tc_bc6h_rd_r(&bs, 6) << 10;
        mode = 13;
        break;
    default: /* reserved -> zero */
        for (i = 0; i < 16; ++i) out[i][0] = out[i][1] = out[i][2] = 0;
        return;
    }

    nparts = (mode >= 10) ? 0 : 1;
    ab0 = abits[0][mode];
    if (is_signed) {
        r[0] = tc_bc6h_sext(r[0], ab0);
        g[0] = tc_bc6h_sext(g[0], ab0);
        b[0] = tc_bc6h_sext(b[0], ab0);
    }
    if ((mode != 9 && mode != 10) || is_signed) {
        for (i = 1; i < (nparts + 1) * 2; ++i) {
            r[i] = tc_bc6h_sext(r[i], abits[1][mode]);
            g[i] = tc_bc6h_sext(g[i], abits[2][mode]);
            b[i] = tc_bc6h_sext(b[i], abits[3][mode]);
        }
    }
    if (mode != 9 && mode != 10) {
        for (i = 1; i < (nparts + 1) * 2; ++i) {
            r[i] = tc_bc6h_xform_inv(r[i], r[0], ab0, is_signed);
            g[i] = tc_bc6h_xform_inv(g[i], g[0], ab0, is_signed);
            b[i] = tc_bc6h_xform_inv(b[i], b[0], ab0, is_signed);
        }
    }
    for (i = 0; i < (nparts + 1) * 2; ++i) {
        r[i] = tc_bc6h_unq(r[i], ab0, is_signed);
        g[i] = tc_bc6h_unq(g[i], ab0, is_signed);
        b[i] = tc_bc6h_unq(b[i], ab0, is_signed);
    }

    weights = (mode >= 10) ? w4 : w3;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j) {
            int pset = (mode >= 10) ? (((i | j) ? 0 : 128)) : psets[partition][i][j];
            int ibits = (mode >= 10) ? 4 : 3;
            int idx, ep;
            if (pset & 0x80) ibits--;
            pset &= 0x01;
            idx = tc_bc6h_rd(&bs, ibits);
            ep = pset * 2;
            out[i * 4 + j][0] =
                tc_bc6h_finish(tc_bc6h_interp(r[ep], r[ep + 1], weights, idx), is_signed);
            out[i * 4 + j][1] =
                tc_bc6h_finish(tc_bc6h_interp(g[ep], g[ep + 1], weights, idx), is_signed);
            out[i * 4 + j][2] =
                tc_bc6h_finish(tc_bc6h_interp(b[ep], b[ep + 1], weights, idx), is_signed);
        }
}


/* ---- public surface decoders ------------------------------------------- */

/* IEEE half -> float: BC6H's natural output is FP16, so the float paths below
 * convert once, on output. Handles normals, subnormals, inf/NaN and sign. */
static float tc_bc6h_h2f(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t man = h & 0x3FFu;
    uint32_t bits;
    float f;
    if (exp == 0u) {
        if (man == 0u) {
            bits = sign; /* +/- zero */
        } else {
            /* Subnormal: renormalise into a float32 normal. */
            uint32_t e = 127u - 15u + 1u;
            while (!(man & 0x400u)) {
                man <<= 1;
                --e;
            }
            man &= 0x3FFu;
            bits = sign | (e << 23) | (man << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (man << 13); /* inf / NaN */
    } else {
        bits = sign | ((exp + 127u - 15u) << 23) | (man << 13);
    }
    memcpy(&f, &bits, sizeof(f));
    return f;
}

tc_result tc_bc6h_decompress_rgb16f(const uint8_t *bc6h, uint32_t width,
                                    uint32_t height, int is_signed,
                                    size_t stride_bytes, uint16_t *out_rgb,
                                    size_t out_size) {
    uint32_t bxc, bx, by, xx, yy;
    size_t row = (size_t)width * 3u * sizeof(uint16_t);
    if (!bc6h || !out_rgb || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride_bytes < row) return TC_ERROR_INVALID_ARGUMENT;
    if (out_size < (size_t)(height - 1u) * stride_bytes + row)
        return TC_ERROR_INVALID_ARGUMENT;
    bxc = (width + 3u) / 4u;
    for (by = 0; by < height; by += 4u)
        for (bx = 0; bx < width; bx += 4u) {
            uint16_t px[16][3];
            size_t bi = ((size_t)(by / 4u) * bxc + bx / 4u) * 16u;
            tc_bc6h_decode_block_half(bc6h + bi, is_signed, px);
            for (yy = 0; yy < 4u && by + yy < height; ++yy)
                for (xx = 0; xx < 4u && bx + xx < width; ++xx) {
                    uint16_t *d = (uint16_t *)((uint8_t *)out_rgb +
                                               (size_t)(by + yy) * stride_bytes) +
                                  (size_t)(bx + xx) * 3u;
                    const uint16_t *s = px[yy * 4u + xx];
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                }
        }
    return TC_SUCCESS;
}

tc_result tc_bc6h_decompress_rgbaf(const uint8_t *bc6h, uint32_t width,
                                   uint32_t height, int is_signed,
                                   size_t stride_bytes, float *out_rgba,
                                   size_t out_size) {
    uint32_t bxc, bx, by, xx, yy;
    size_t row = (size_t)width * 4u * sizeof(float);
    if (!bc6h || !out_rgba || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride_bytes < row) return TC_ERROR_INVALID_ARGUMENT;
    if (out_size < (size_t)(height - 1u) * stride_bytes + row)
        return TC_ERROR_INVALID_ARGUMENT;
    bxc = (width + 3u) / 4u;
    for (by = 0; by < height; by += 4u)
        for (bx = 0; bx < width; bx += 4u) {
            uint16_t px[16][3];
            size_t bi = ((size_t)(by / 4u) * bxc + bx / 4u) * 16u;
            tc_bc6h_decode_block_half(bc6h + bi, is_signed, px);
            for (yy = 0; yy < 4u && by + yy < height; ++yy)
                for (xx = 0; xx < 4u && bx + xx < width; ++xx) {
                    float *d = (float *)((uint8_t *)out_rgba +
                                         (size_t)(by + yy) * stride_bytes) +
                               (size_t)(bx + xx) * 4u;
                    const uint16_t *s = px[yy * 4u + xx];
                    d[0] = tc_bc6h_h2f(s[0]);
                    d[1] = tc_bc6h_h2f(s[1]);
                    d[2] = tc_bc6h_h2f(s[2]);
                    d[3] = 1.0f; /* BC6H carries no alpha */
                }
        }
    return TC_SUCCESS;
}
