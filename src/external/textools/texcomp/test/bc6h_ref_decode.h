/*
 * TinyEXR texcomp - BC6H reference block decode (test shim).
 *
 * The bcdec port that used to live here is now the library's BC6H decoder
 * (src/texcomp_bc6h_decode.c), so there is a single implementation rather than
 * two copies that can drift apart. This header keeps the entry point the BC6H
 * gates were written against. The gates' purpose is unchanged: they validate the
 * in-house BC6H *encoder* against a decoder of independent lineage (bcdec).
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TC_BC6H_REF_DECODE_H_
#define TC_BC6H_REF_DECODE_H_

#include "texcomp.h"

#include <stdint.h>

/* Decode one BC6H block to 16 texels of FP16 RGB (row-major). */
static void tc_bc6h_ref_decode_block(const uint8_t blk[16], int is_signed,
                                     uint16_t out[16][3]) {
    tc_bc6h_decompress_rgb16f(blk, 4u, 4u, is_signed, 4u * 3u * sizeof(uint16_t),
                              &out[0][0], 16u * 3u * sizeof(uint16_t));
}

#endif /* TC_BC6H_REF_DECODE_H_ */
