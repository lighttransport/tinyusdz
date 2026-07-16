/*
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * ASTC HDR reference block decode (test shim).
 *
 * The pure-C HDR decoder that used to live here is now the library's, in
 * src/texcomp_astc_decode.c, where it reuses that file's block-mode / ISE /
 * partition / infill machinery instead of the parallel copy in astc_ref_decode.h.
 * This header keeps the entry point the HDR tests and the astcenc cross-check
 * were written against; their purpose is unchanged -- texcomp-astc-hdr-gate
 * still validates the decoder, block for block, against astcenc's conformant
 * HDR decoder.
 */
#ifndef TC_ASTC_HDR_REF_DECODE_H
#define TC_ASTC_HDR_REF_DECODE_H

#include "texcomp.h"

#include <stdint.h>

/* Decode one ASTC HDR block to bx*by float RGBA texels. 1 on success. */
static int ahref_decode_block_hdr(const uint8_t block[16], uint32_t bx,
                                  uint32_t by, float *out_rgba) {
    return tc_astc_hdr_decompress_rgbaf(block, bx, by, bx, by,
                                        (size_t)bx * 4u * sizeof(float),
                                        out_rgba,
                                        (size_t)bx * by * 4u * sizeof(float)) ==
           TC_SUCCESS;
}

#endif /* TC_ASTC_HDR_REF_DECODE_H */
