/*
 * TinyEXR texcomp - ETC2 / EAC decoder
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Written from the ETC2/EAC block definitions in the OpenGL ES 3.0 spec
 * (Khronos), covering all five RGB modes (individual, differential, T, H,
 * planar) plus EAC alpha (8-bit) and EAC R11/RG11 (11-bit).
 *
 * ETC numbers the texels of a block down columns first -- texel i is at
 * (x = i / 4, y = i % 4) -- and every field below (selector bit positions, the
 * flip axis, the planar axes) follows that convention. The decoders here emit
 * row-major RGBA8, so the transposition happens once, on output.
 */
#include "texcomp.h"
#include "texcomp_internal.h"

#include <string.h>

/* Intensity modifier table, indexed by [codeword][selector]. */
static const int32_t tc_etc_mod[8][4] = {
    {2, 8, -2, -8},       {5, 17, -5, -17},   {9, 29, -9, -29},
    {13, 42, -13, -42},   {18, 60, -18, -60}, {24, 80, -24, -80},
    {33, 106, -33, -106}, {47, 183, -47, -183}};

/* T/H-mode distance table. */
static const int32_t tc_etc_dist[8] = {3, 6, 11, 16, 23, 32, 41, 64};

/* EAC modifier table, indexed by [table][selector]. */
static const int32_t tc_eac_mod[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14},   {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8, -13, 1, 4, 7, 12},   {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11},   {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10},   {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},    {-2, -5, -8, -10, 1, 4, 7, 9},
    {-2, -4, -8, -10, 1, 3, 7, 9},    {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},    {-1, -2, -3, -10, 0, 1, 2, 9},
    {-4, -6, -8, -9, 3, 5, 7, 8},     {-3, -5, -7, -9, 2, 4, 6, 8}};

static uint8_t tc_etc_clamp255(int32_t v) {
    if (v < 0) return 0u;
    if (v > 255) return 255u;
    return (uint8_t)v;
}

static uint8_t tc_etc_ext4(uint32_t v) { return (uint8_t)((v << 4) | v); }
static uint8_t tc_etc_ext5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static uint8_t tc_etc_ext6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }
static uint8_t tc_etc_ext7(uint32_t v) { return (uint8_t)((v << 1) | (v >> 6)); }

/* Sign-extend a 3-bit two's-complement field. */
static int32_t tc_etc_s3(uint32_t v) {
    return (v & 4u) ? (int32_t)v - 8 : (int32_t)v;
}

/* Raw 2-bit selector for texel (x, y): the LSB plane is the low 16 bits of the
 * index word (bytes 6-7) and the MSB plane the high 16 (bytes 4-5), each
 * indexed by the column-major texel number. */
static uint32_t tc_etc_sel(const uint8_t b[8], uint32_t x, uint32_t y) {
    uint32_t i = x * 4u + y;
    uint32_t lsb = ((uint32_t)b[7 - (i >> 3)] >> (i & 7u)) & 1u;
    uint32_t msb = ((uint32_t)b[5 - (i >> 3)] >> (i & 7u)) & 1u;
    return (msb << 1) | lsb;
}

/* Decode an 8-byte ETC2 RGB block to 16 row-major RGBA8 texels (alpha 255).
 * The RGB8A1 (punch-through) variant is a different format with its own
 * VkFormats; it is not mapped by the reader and is deliberately not handled
 * here rather than carried as an untested code path. */
static void tc_decode_etc2_rgb_block(const uint8_t b[8], uint8_t px[16][4]) {
    uint32_t flip = b[3] & 1u;
    uint32_t differential = (b[3] >> 1) & 1u;
    int32_t r = (int32_t)(b[0] >> 3), dr = tc_etc_s3(b[0] & 7u);
    int32_t g = (int32_t)(b[1] >> 3), dg = tc_etc_s3(b[1] & 7u);
    int32_t bl = (int32_t)(b[2] >> 3), db = tc_etc_s3(b[2] & 7u);
    uint32_t x, y;

    if (differential && (r + dr < 0 || r + dr > 31)) {
        /* T mode: two base colours, the second offset by +/- a distance. */
        uint32_t r1 = (((uint32_t)b[0] >> 1) & 0xCu) | ((uint32_t)b[0] & 3u);
        uint8_t c0[3], c1[3];
        uint32_t d = ((((uint32_t)b[3] >> 1) & 6u) | ((uint32_t)b[3] & 1u));
        int32_t dist = tc_etc_dist[d];
        uint8_t pal[4][3];
        int32_t k;
        c0[0] = tc_etc_ext4(r1);
        c0[1] = tc_etc_ext4((uint32_t)b[1] >> 4);
        c0[2] = tc_etc_ext4((uint32_t)b[1] & 0xFu);
        c1[0] = tc_etc_ext4((uint32_t)b[2] >> 4);
        c1[1] = tc_etc_ext4((uint32_t)b[2] & 0xFu);
        c1[2] = tc_etc_ext4((uint32_t)b[3] >> 4);
        for (k = 0; k < 3; ++k) {
            pal[0][k] = c0[k];
            pal[1][k] = tc_etc_clamp255((int32_t)c1[k] + dist);
            pal[2][k] = c1[k];
            pal[3][k] = tc_etc_clamp255((int32_t)c1[k] - dist);
        }
        for (x = 0; x < 4u; ++x)
            for (y = 0; y < 4u; ++y) {
                uint32_t s = tc_etc_sel(b, x, y);
                uint8_t *o = px[y * 4u + x];
                o[0] = pal[s][0];
                o[1] = pal[s][1];
                o[2] = pal[s][2];
                o[3] = 255u;
            }
        return;
    }
    if (differential && (g + dg < 0 || g + dg > 31)) {
        /* H mode: two base colours, each offset by +/- the same distance. */
        uint32_t r0 = ((uint32_t)b[0] >> 3) & 0xFu;
        uint32_t g0 = (((uint32_t)b[0] & 7u) << 1) | (((uint32_t)b[1] >> 4) & 1u);
        uint32_t b0 = ((uint32_t)b[1] & 8u) | ((((uint32_t)b[1] & 3u) << 1)) |
                      (((uint32_t)b[2] >> 7) & 1u);
        uint32_t r1 = ((uint32_t)b[2] >> 3) & 0xFu;
        uint32_t g1 = (((uint32_t)b[2] & 7u) << 1) | ((uint32_t)b[3] >> 7);
        uint32_t b1 = ((uint32_t)b[3] >> 3) & 0xFu;
        uint8_t c0[3], c1[3];
        uint32_t d;
        int32_t dist, k;
        uint8_t pal[4][3];
        c0[0] = tc_etc_ext4(r0); c0[1] = tc_etc_ext4(g0); c0[2] = tc_etc_ext4(b0);
        c1[0] = tc_etc_ext4(r1); c1[1] = tc_etc_ext4(g1); c1[2] = tc_etc_ext4(b1);
        /* Only two distance bits are stored; the low bit is recovered from an
         * ordering comparison of the two base colours (H mode cannot encode
         * which base comes first, so the comparison is free information). */
        d = ((uint32_t)b[3] & 4u) | (((uint32_t)b[3] & 1u) << 1);
        if (((uint32_t)c0[0] << 16 | (uint32_t)c0[1] << 8 | c0[2]) >=
            ((uint32_t)c1[0] << 16 | (uint32_t)c1[1] << 8 | c1[2]))
            d |= 1u;
        dist = tc_etc_dist[d];
        for (k = 0; k < 3; ++k) {
            pal[0][k] = tc_etc_clamp255((int32_t)c0[k] + dist);
            pal[1][k] = tc_etc_clamp255((int32_t)c0[k] - dist);
            pal[2][k] = tc_etc_clamp255((int32_t)c1[k] + dist);
            pal[3][k] = tc_etc_clamp255((int32_t)c1[k] - dist);
        }
        for (x = 0; x < 4u; ++x)
            for (y = 0; y < 4u; ++y) {
                uint32_t s = tc_etc_sel(b, x, y);
                uint8_t *o = px[y * 4u + x];
                o[0] = pal[s][0];
                o[1] = pal[s][1];
                o[2] = pal[s][2];
                o[3] = 255u;
            }
        return;
    }
    if (differential && (bl + db < 0 || bl + db > 31)) {
        /* Planar mode: a bilinear ramp through three colours O, H, V. */
        uint32_t ro = ((uint32_t)b[0] >> 1) & 0x3Fu;
        uint32_t go = (((uint32_t)b[0] & 1u) << 6) | (((uint32_t)b[1] >> 1) & 0x3Fu);
        uint32_t bo = (((uint32_t)b[1] & 1u) << 5) | ((uint32_t)b[2] & 0x18u) |
                      (((uint32_t)b[2] & 3u) << 1) | (((uint32_t)b[3] >> 7) & 1u);
        uint32_t rh = (((uint32_t)b[3] >> 1) & 0x3Eu) | ((uint32_t)b[3] & 1u);
        uint32_t gh = (uint32_t)b[4] >> 1;
        uint32_t bh = (((uint32_t)b[4] & 1u) << 5) | ((uint32_t)b[5] >> 3);
        uint32_t rv = (((uint32_t)b[5] & 7u) << 3) | ((uint32_t)b[6] >> 5);
        uint32_t gv = (((uint32_t)b[6] & 0x1Fu) << 2) | ((uint32_t)b[7] >> 6);
        uint32_t bv = (uint32_t)b[7] & 0x3Fu;
        int32_t r0 = tc_etc_ext6(ro), g0 = tc_etc_ext7(go), b0 = tc_etc_ext6(bo);
        int32_t r1 = tc_etc_ext6(rh), g1 = tc_etc_ext7(gh), b1 = tc_etc_ext6(bh);
        int32_t r2 = tc_etc_ext6(rv), g2 = tc_etc_ext7(gv), b2 = tc_etc_ext6(bv);
        for (x = 0; x < 4u; ++x)
            for (y = 0; y < 4u; ++y) {
                uint8_t *o = px[y * 4u + x];
                o[0] = tc_etc_clamp255(
                    ((int32_t)x * (r1 - r0) + (int32_t)y * (r2 - r0) + 4 * r0 + 2) >> 2);
                o[1] = tc_etc_clamp255(
                    ((int32_t)x * (g1 - g0) + (int32_t)y * (g2 - g0) + 4 * g0 + 2) >> 2);
                o[2] = tc_etc_clamp255(
                    ((int32_t)x * (b1 - b0) + (int32_t)y * (b2 - b0) + 4 * b0 + 2) >> 2);
                o[3] = 255u;
            }
        return;
    }

    /* ETC1: two subblocks, split left/right (flip 0) or top/bottom (flip 1). */
    {
        uint8_t base[2][3];
        uint32_t cw[2];
        if (differential) {
            base[0][0] = tc_etc_ext5((uint32_t)r);
            base[0][1] = tc_etc_ext5((uint32_t)g);
            base[0][2] = tc_etc_ext5((uint32_t)bl);
            base[1][0] = tc_etc_ext5((uint32_t)(r + dr));
            base[1][1] = tc_etc_ext5((uint32_t)(g + dg));
            base[1][2] = tc_etc_ext5((uint32_t)(bl + db));
        } else {
            base[0][0] = tc_etc_ext4((uint32_t)b[0] >> 4);
            base[0][1] = tc_etc_ext4((uint32_t)b[1] >> 4);
            base[0][2] = tc_etc_ext4((uint32_t)b[2] >> 4);
            base[1][0] = tc_etc_ext4((uint32_t)b[0] & 0xFu);
            base[1][1] = tc_etc_ext4((uint32_t)b[1] & 0xFu);
            base[1][2] = tc_etc_ext4((uint32_t)b[2] & 0xFu);
        }
        cw[0] = ((uint32_t)b[3] >> 5) & 7u;
        cw[1] = ((uint32_t)b[3] >> 2) & 7u;
        for (x = 0; x < 4u; ++x)
            for (y = 0; y < 4u; ++y) {
                uint32_t sub = flip ? (y >> 1) : (x >> 1);
                uint32_t s = tc_etc_sel(b, x, y);
                uint8_t *o = px[y * 4u + x];
                int32_t m = tc_etc_mod[cw[sub]][s];
                o[0] = tc_etc_clamp255((int32_t)base[sub][0] + m);
                o[1] = tc_etc_clamp255((int32_t)base[sub][1] + m);
                o[2] = tc_etc_clamp255((int32_t)base[sub][2] + m);
                o[3] = 255u;
            }
    }
}

/* EAC selector for texel (x, y): 3 bits each, packed MSB-first from bit 47 of
 * the 48-bit index field (bytes 2..7), in the same column-major texel order. */
static uint32_t tc_eac_sel(const uint8_t b[8], uint32_t x, uint32_t y) {
    uint32_t i = x * 4u + y;
    uint32_t bit = 45u - 3u * i; /* low bit position within the 48-bit field */
    uint64_t w = 0;
    uint32_t k;
    for (k = 2u; k < 8u; ++k) w = (w << 8) | (uint64_t)b[k];
    return (uint32_t)((w >> bit) & 7u);
}

/* EAC alpha (the 8-bit variant used by ETC2 RGBA). */
static void tc_decode_eac_a8_block(const uint8_t b[8], uint8_t v[16]) {
    int32_t base = (int32_t)b[0];
    int32_t mul = (int32_t)(b[1] >> 4);
    uint32_t tab = (uint32_t)(b[1] & 0xFu);
    uint32_t x, y;
    /* Unlike R11, the alpha variant applies the multiplier as stored: a zero
     * multiplier yields a constant block rather than selecting a unit step. */
    for (x = 0; x < 4u; ++x)
        for (y = 0; y < 4u; ++y) {
            int32_t m = tc_eac_mod[tab][tc_eac_sel(b, x, y)];
            v[y * 4u + x] = tc_etc_clamp255(base + m * mul);
        }
}

/* EAC R11 (unsigned): the same layout reconstructed at 11 bits. */
static void tc_decode_eac_r11_block(const uint8_t b[8], uint16_t v[16]) {
    int32_t base = (int32_t)b[0] * 8 + 4;
    int32_t mul = (int32_t)(b[1] >> 4);
    uint32_t tab = (uint32_t)(b[1] & 0xFu);
    uint32_t x, y;
    for (x = 0; x < 4u; ++x)
        for (y = 0; y < 4u; ++y) {
            int32_t m = tc_eac_mod[tab][tc_eac_sel(b, x, y)];
            int32_t r = (mul == 0) ? base + m : base + m * mul * 8;
            if (r < 0) r = 0;
            if (r > 2047) r = 2047;
            v[y * 4u + x] = (uint16_t)r;
        }
}

static uint8_t tc_eac_r11_to_u8(uint16_t v) {
    return (uint8_t)(((uint32_t)v * 255u + 1023u) / 2047u);
}

/* ---- public surface decoders ------------------------------------------- */

tc_result tc_etc2_decompress_rgba8(const uint8_t *etc2, uint32_t width,
                                   uint32_t height, int alpha, size_t stride,
                                   uint8_t *out_rgba, size_t out_size) {
    uint32_t bxc, bx, by, xx, yy;
    size_t bsz = alpha ? 16u : 8u;
    if (!etc2 || !out_rgba || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride < (size_t)width * 4u) return TC_ERROR_INVALID_ARGUMENT;
    if (out_size < (size_t)(height - 1u) * stride + (size_t)width * 4u)
        return TC_ERROR_INVALID_ARGUMENT;
    bxc = (width + 3u) / 4u;
    for (by = 0; by < height; by += 4u)
        for (bx = 0; bx < width; bx += 4u) {
            uint8_t px[16][4], a[16];
            const uint8_t *blk = etc2 + ((size_t)(by / 4u) * bxc + bx / 4u) * bsz;
            if (alpha) {
                /* ETC2 RGBA: an EAC alpha block, then the RGB block. */
                tc_decode_eac_a8_block(blk, a);
                tc_decode_etc2_rgb_block(blk + 8u, px);
            } else {
                tc_decode_etc2_rgb_block(blk, px);
            }
            for (yy = 0; yy < 4u && by + yy < height; ++yy)
                for (xx = 0; xx < 4u && bx + xx < width; ++xx) {
                    uint8_t *d = out_rgba + (size_t)(by + yy) * stride +
                                 (size_t)(bx + xx) * 4u;
                    uint32_t t = yy * 4u + xx;
                    d[0] = px[t][0];
                    d[1] = px[t][1];
                    d[2] = px[t][2];
                    d[3] = alpha ? a[t] : 255u;
                }
        }
    return TC_SUCCESS;
}

tc_result tc_eac_decompress_rgba8(const uint8_t *eac, uint32_t width,
                                  uint32_t height, int rg11, size_t stride,
                                  uint8_t *out_rgba, size_t out_size) {
    uint32_t bxc, bx, by, xx, yy;
    size_t bsz = rg11 ? 16u : 8u;
    if (!eac || !out_rgba || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride < (size_t)width * 4u) return TC_ERROR_INVALID_ARGUMENT;
    if (out_size < (size_t)(height - 1u) * stride + (size_t)width * 4u)
        return TC_ERROR_INVALID_ARGUMENT;
    bxc = (width + 3u) / 4u;
    for (by = 0; by < height; by += 4u)
        for (bx = 0; bx < width; bx += 4u) {
            uint16_t r[16], g[16];
            const uint8_t *blk = eac + ((size_t)(by / 4u) * bxc + bx / 4u) * bsz;
            tc_decode_eac_r11_block(blk, r);
            if (rg11) tc_decode_eac_r11_block(blk + 8u, g);
            for (yy = 0; yy < 4u && by + yy < height; ++yy)
                for (xx = 0; xx < 4u && bx + xx < width; ++xx) {
                    uint8_t *d = out_rgba + (size_t)(by + yy) * stride +
                                 (size_t)(bx + xx) * 4u;
                    uint32_t t = yy * 4u + xx;
                    d[0] = tc_eac_r11_to_u8(r[t]);
                    d[1] = rg11 ? tc_eac_r11_to_u8(g[t]) : 0u;
                    d[2] = 0u;
                    d[3] = 255u;
                }
        }
    return TC_SUCCESS;
}
