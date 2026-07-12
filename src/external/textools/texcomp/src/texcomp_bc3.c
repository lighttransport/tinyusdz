/*
 * TinyEXR texcomp - BC3 (DXT5) encoder
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "texcomp.h"
#include "texcomp_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

tc_result tc_bc3_compress_rgba8(const uint8_t *rgba, uint32_t width,
                                uint32_t height, size_t stride,
                                const tc_bc3_options *opt, uint8_t *out_bc3,
                                size_t out_size) {
    uint32_t bx, by, x, y, xx, yy;
    uint8_t block[16][4];
    uint8_t alpha[16];
    size_t need, off = 0;
    (void)opt;

    if (!rgba || !out_bc3 || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride < (size_t)width * 4u) return TC_ERROR_INVALID_ARGUMENT;
    need = tc_bc3_compressed_size(width, height);
    if (!need || out_size < need) return TC_ERROR_INVALID_ARGUMENT;

    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            for (yy = 0; yy < 4; ++yy) {
                y = by + yy;
                if (y >= height) y = height - 1u;
                for (xx = 0; xx < 4; ++xx) {
                    const uint8_t *src;
                    x = bx + xx;
                    if (x >= width) x = width - 1u;
                    src = rgba + (size_t)y * stride + (size_t)x * 4u;
                    block[yy * 4u + xx][0] = src[0];
                    block[yy * 4u + xx][1] = src[1];
                    block[yy * 4u + xx][2] = src[2];
                    block[yy * 4u + xx][3] = src[3];
                    alpha[yy * 4u + xx] = src[3];
                }
            }
            /* DXT5: 8-byte BC4 alpha block followed by the 8-byte colour block
             * (always 4-colour mode, so no endpoint-order constraint). */
            tc_encode_bc4_block(alpha, out_bc3 + off);
            tc_encode_bc1_color_block(block, 0, out_bc3 + off + 8u);
            off += 16u;
        }
    }
    return TC_SUCCESS;
}

/* --- decode ------------------------------------------------------------- */

tc_result tc_bc3_decompress_rgba8(const uint8_t *bc3, uint32_t width,
                                  uint32_t height, size_t stride,
                                  uint8_t *out_rgba, size_t out_size) {
    uint32_t bxc, bx, by, xx, yy;
    if (!bc3 || !out_rgba || !width || !height) return TC_ERROR_INVALID_ARGUMENT;
    if (stride < (size_t)width * 4u) return TC_ERROR_INVALID_ARGUMENT;
    if (out_size < (size_t)(height - 1u) * stride + (size_t)width * 4u)
        return TC_ERROR_INVALID_ARGUMENT;
    bxc = (width + 3u) / 4u;
    for (by = 0; by < height; by += 4u)
        for (bx = 0; bx < width; bx += 4u) {
            uint8_t px[16][4], alpha[16];
            /* DXT5 block: 8-byte BC4 alpha, then the 8-byte colour block (which
             * is always 4-colour in BC3 -- hence dxt1 = 0). */
            size_t bi = ((size_t)(by / 4u) * bxc + bx / 4u) * 16u;
            tc_decode_bc4_block(bc3 + bi, alpha);
            tc_decode_bc1_color_block(bc3 + bi + 8u, 0, px);
            for (yy = 0; yy < 4u && by + yy < height; ++yy)
                for (xx = 0; xx < 4u && bx + xx < width; ++xx) {
                    uint8_t *d = out_rgba + (size_t)(by + yy) * stride +
                                 (size_t)(bx + xx) * 4u;
                    uint32_t t = yy * 4u + xx;
                    d[0] = px[t][0];
                    d[1] = px[t][1];
                    d[2] = px[t][2];
                    d[3] = alpha[t];
                }
        }
    return TC_SUCCESS;
}
