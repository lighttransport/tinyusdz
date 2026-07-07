/*
 * TinyEXR texcomp - "uni": a compact universal transcodable texture.
 *
 * Each 4x4 block is stored in a canonical single-subset form -- two RGBA8
 * endpoints plus 16 four-bit interpolation weights (16 bytes/block, the
 * UASTC-spirit intermediate). Encode once, then transcode cheaply at load to a
 * frequently-used GPU block format by re-packing the endpoints/weights, with no
 * decode+re-encode search:
 *   - BC7 (mode 6, single-subset RGBA) -- near-lossless w.r.t. the intermediate.
 *   - BC1 (DXT1) -- endpoints to RGB565, weights to 2-bit indices.
 * The single-subset line is the natural shared structure of the BC family; the
 * ASTC/ETC mobile targets need the full UASTC block layout and are future work.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "texcomp.h"

#include <stdlib.h>
#include <string.h>

/* uni block: ep0[4] ep1[4] then 16*4-bit weights (little-endian nibbles). */
#define TC_UNI_BLOCK_BYTES 16u

size_t tc_uni_compressed_size(uint32_t width, uint32_t height) {
    size_t bx = ((size_t)width + 3u) >> 2, by = ((size_t)height + 3u) >> 2;
    return bx * by * TC_UNI_BLOCK_BYTES;
}

static int tc_uni_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- encode: bounding-box endpoints + projected 4-bit weights ---- */

tc_result tc_uni_compress_rgba8(const uint8_t *rgba, uint32_t width,
                                uint32_t height, size_t stride, uint8_t *out,
                                size_t out_size) {
    uint32_t bx, by, x, y, xx, yy, i, c;
    size_t need = tc_uni_compressed_size(width, height), off = 0;
    if (!rgba || !out || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride < (size_t)width * 4u || out_size < need) return TC_ERROR_INVALID_ARGUMENT;

    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            uint8_t blk[16][4];
            uint8_t ep0[4], ep1[4];
            int d[4], dd = 0;
            for (yy = 0; yy < 4; ++yy) {
                y = by + yy; if (y >= height) y = height - 1u;
                for (xx = 0; xx < 4; ++xx) {
                    x = bx + xx; if (x >= width) x = width - 1u;
                    memcpy(blk[yy * 4 + xx], rgba + (size_t)y * stride + (size_t)x * 4u, 4);
                }
            }
            /* bounding box (per-channel min/max) as the endpoint line */
            for (c = 0; c < 4; ++c) { ep0[c] = 255; ep1[c] = 0; }
            for (i = 0; i < 16; ++i)
                for (c = 0; c < 4; ++c) {
                    if (blk[i][c] < ep0[c]) ep0[c] = blk[i][c];
                    if (blk[i][c] > ep1[c]) ep1[c] = blk[i][c];
                }
            for (c = 0; c < 4; ++c) { d[c] = (int)ep1[c] - (int)ep0[c]; dd += d[c] * d[c]; }
            /* weights: project each texel onto the line, quantise to 4 bits */
            memcpy(out + off, ep0, 4);
            memcpy(out + off + 4, ep1, 4);
            memset(out + off + 8, 0, 8);
            for (i = 0; i < 16; ++i) {
                int t, w, num = 0;
                if (dd > 0) {
                    for (c = 0; c < 4; ++c) num += ((int)blk[i][c] - (int)ep0[c]) * d[c];
                    t = (num * 15 + dd / 2) / dd;
                } else t = 0;
                w = tc_uni_clampi(t, 0, 15);
                out[off + 8 + (i >> 1)] |= (uint8_t)(w << ((i & 1) * 4));
            }
            off += TC_UNI_BLOCK_BYTES;
        }
    }
    return TC_SUCCESS;
}

static void tc_uni_block_endpoints(const uint8_t *b, uint8_t ep0[4], uint8_t ep1[4]) {
    memcpy(ep0, b, 4);
    memcpy(ep1, b + 4, 4);
}
static int tc_uni_weight(const uint8_t *b, int i) {
    return (b[8 + (i >> 1)] >> ((i & 1) * 4)) & 0xf;
}

tc_result tc_uni_decompress_rgba8(const uint8_t *uni, uint32_t width,
                                  uint32_t height, size_t stride, uint8_t *out,
                                  size_t out_size) {
    uint32_t bx, by, xx, yy, i, c;
    size_t off = 0;
    if (!uni || !out || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride < (size_t)width * 4u || out_size < (size_t)height * stride)
        return TC_ERROR_INVALID_ARGUMENT;
    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            const uint8_t *b = uni + off;
            uint8_t ep0[4], ep1[4];
            tc_uni_block_endpoints(b, ep0, ep1);
            for (yy = 0; yy < 4; ++yy) {
                uint32_t y = by + yy;
                if (y >= height) { continue; }
                for (xx = 0; xx < 4; ++xx) {
                    uint32_t x = bx + xx;
                    uint8_t *p;
                    int w;
                    if (x >= width) continue;
                    i = yy * 4 + xx; w = tc_uni_weight(b, (int)i);
                    p = out + (size_t)y * stride + (size_t)x * 4u;
                    for (c = 0; c < 4; ++c)
                        p[c] = (uint8_t)(((15 - w) * ep0[c] + w * ep1[c] + 7) / 15);
                }
            }
            off += TC_UNI_BLOCK_BYTES;
        }
    }
    return TC_SUCCESS;
}

/* ---- transcode: uni -> BC7 (mode 6) ---- */

static void tc_uni_put_bits(uint8_t *out, uint32_t *pos, uint32_t val, uint32_t n) {
    uint32_t k;
    for (k = 0; k < n; ++k) {
        if (val & (1u << k)) out[(*pos) >> 3] |= (uint8_t)(1u << ((*pos) & 7u));
        ++(*pos);
    }
}

tc_result tc_uni_transcode_bc7(const uint8_t *uni, uint32_t width,
                               uint32_t height, uint8_t *out, size_t out_size) {
    size_t nblocks = tc_uni_compressed_size(width, height) / TC_UNI_BLOCK_BYTES, k;
    if (!uni || !out || out_size < nblocks * 16u) return TC_ERROR_INVALID_ARGUMENT;
    for (k = 0; k < nblocks; ++k) {
        const uint8_t *b = uni + k * TC_UNI_BLOCK_BYTES;
        uint8_t *o = out + k * 16u;
        uint8_t ep0[4], ep1[4], q0[4], q1[4];
        uint8_t p0, p1;
        int w[16], i, c, anchor0;
        uint32_t pos = 0;
        tc_uni_block_endpoints(b, ep0, ep1);
        for (i = 0; i < 16; ++i) w[i] = tc_uni_weight(b, i);
        /* mode 6 anchor is index 0: its MSB must be 0. Swap+invert if needed. */
        if (w[0] & 8) {
            uint8_t t[4];
            memcpy(t, ep0, 4); memcpy(ep0, ep1, 4); memcpy(ep1, t, 4);
            for (i = 0; i < 16; ++i) w[i] = 15 - w[i];
        }
        /* one p-bit per endpoint, chosen from alpha (keeps alpha exact); 7-bit
         * quantise the rest against that p-bit (<=1 LSB error). */
        p0 = (uint8_t)(ep0[3] & 1u);
        p1 = (uint8_t)(ep1[3] & 1u);
        for (c = 0; c < 4; ++c) {
            q0[c] = (uint8_t)tc_uni_clampi(((int)ep0[c] - p0 + 1) >> 1, 0, 127);
            q1[c] = (uint8_t)tc_uni_clampi(((int)ep1[c] - p1 + 1) >> 1, 0, 127);
        }
        memset(o, 0, 16);
        tc_uni_put_bits(o, &pos, 1u << 6, 7);         /* mode 6 marker */
        for (c = 0; c < 4; ++c) {                      /* R,G,B,A: lo then hi, 7b */
            tc_uni_put_bits(o, &pos, q0[c], 7);
            tc_uni_put_bits(o, &pos, q1[c], 7);
        }
        tc_uni_put_bits(o, &pos, p0, 1);
        tc_uni_put_bits(o, &pos, p1, 1);
        anchor0 = w[0];
        tc_uni_put_bits(o, &pos, (uint32_t)anchor0, 3); /* anchor: 3 bits */
        for (i = 1; i < 16; ++i) tc_uni_put_bits(o, &pos, (uint32_t)w[i], 4);
    }
    return TC_SUCCESS;
}

/* ---- transcode: uni -> BC1 (DXT1) ---- */

static uint16_t tc_uni_to565(const uint8_t *rgb) {
    return (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
}

tc_result tc_uni_transcode_bc1(const uint8_t *uni, uint32_t width,
                               uint32_t height, uint8_t *out, size_t out_size) {
    size_t nblocks = tc_uni_compressed_size(width, height) / TC_UNI_BLOCK_BYTES, k;
    /* 4-bit weight -> BC1 2-bit index (t in 4 steps): {0,1/3,2/3,1}->{0,2,3,1} */
    static const uint8_t w2idx[16] = {0, 0, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 1, 1, 1, 1};
    if (!uni || !out || out_size < nblocks * 8u) return TC_ERROR_INVALID_ARGUMENT;
    for (k = 0; k < nblocks; ++k) {
        const uint8_t *b = uni + k * TC_UNI_BLOCK_BYTES;
        uint8_t *o = out + k * 8u;
        uint8_t ep0[4], ep1[4];
        uint16_t c0, c1;
        int w[16], i, swap = 0;
        uint32_t idx = 0;
        tc_uni_block_endpoints(b, ep0, ep1);
        for (i = 0; i < 16; ++i) w[i] = tc_uni_weight(b, i);
        c0 = tc_uni_to565(ep0);
        c1 = tc_uni_to565(ep1);
        /* 4-colour (opaque) mode needs c0 > c1; swap endpoints + invert weights. */
        if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; swap = 1; }
        else if (c0 == c1) { /* degenerate: nudge to stay 4-colour */
            if (c1 > 0) c1--; else c0 = 1;
        }
        o[0] = (uint8_t)(c0 & 0xff); o[1] = (uint8_t)(c0 >> 8);
        o[2] = (uint8_t)(c1 & 0xff); o[3] = (uint8_t)(c1 >> 8);
        for (i = 0; i < 16; ++i) {
            int ww = swap ? 15 - w[i] : w[i];
            idx |= (uint32_t)w2idx[ww] << (i * 2);
        }
        o[4] = (uint8_t)(idx & 0xff); o[5] = (uint8_t)((idx >> 8) & 0xff);
        o[6] = (uint8_t)((idx >> 16) & 0xff); o[7] = (uint8_t)((idx >> 24) & 0xff);
    }
    return TC_SUCCESS;
}

/* ---- mobile targets: ASTC 4x4 and ETC2 ----
 * The BC family shares uni's endpoint-line, so those transcode by direct
 * re-pack. ASTC's ISE weight/endpoint layout and ETC's base+modifier structure
 * do NOT, so cheap bit-repack isn't possible from this single-line intermediate
 * (that is exactly what Basis's purpose-built UASTC block solves). These are
 * "re-encode" transcoders instead: decode the intermediate, then encode the
 * mobile block with the tested encoder -- conformant output, still one shipped
 * artifact, but a higher transcode-time cost than the BC path. ASTC routes
 * through the single-subset UASTC LDR path, which matches uni's structure so
 * fidelity is preserved. */

tc_result tc_uni_transcode_astc(const uint8_t *uni, uint32_t width,
                                uint32_t height, uint8_t *out, size_t out_size) {
    uint8_t *rgba;
    tc_astc_options ao;
    tc_result r;
    size_t n = (size_t)width * height * 4u;
    if (!uni || !out || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    rgba = (uint8_t *)malloc(n);
    if (!rgba) return TC_ERROR_OUT_OF_MEMORY;
    r = tc_uni_decompress_rgba8(uni, width, height, (size_t)width * 4u, rgba, n);
    if (r == TC_SUCCESS) {
        tc_astc_options_init(&ao);
        ao.block_x = 4u; ao.block_y = 4u; ao.uastc = 1; /* single-subset LDR */
        r = tc_astc_compress_rgba8(rgba, width, height, (size_t)width * 4u, &ao,
                                   out, out_size);
    }
    free(rgba);
    return r;
}

tc_result tc_uni_transcode_etc2(const uint8_t *uni, uint32_t width,
                                uint32_t height, int alpha, uint8_t *out,
                                size_t out_size) {
    uint8_t *rgba;
    tc_etc2_options eo;
    tc_result r;
    size_t n = (size_t)width * height * 4u;
    if (!uni || !out || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    rgba = (uint8_t *)malloc(n);
    if (!rgba) return TC_ERROR_OUT_OF_MEMORY;
    r = tc_uni_decompress_rgba8(uni, width, height, (size_t)width * 4u, rgba, n);
    if (r == TC_SUCCESS) {
        tc_etc2_options_init(&eo);
        eo.alpha = alpha ? 1 : 0;
        r = tc_etc2_compress_rgba8(rgba, width, height, (size_t)width * 4u, &eo,
                                   out, out_size);
    }
    free(rgba);
    return r;
}
