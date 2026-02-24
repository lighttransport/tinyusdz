/*
 * lusd_lz4.c - LZ4 decompression wrapper for USDC Crate format
 *
 * USDC uses a custom chunked LZ4 block format (matching TfFastCompression):
 *   byte 0:    nChunks (0 = single chunk; 1..127 = that many chunks)
 *   if nChunks == 0:
 *     [compressedSize-1] bytes of LZ4 block data
 *   else for each chunk:
 *     int32_t chunkCompressedSize
 *     [chunkCompressedSize] bytes of LZ4 block data
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_platform.h"
#include <string.h>
#include <stdint.h>

/* Silence warnings inside the vendored LZ4 source. */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-conversion"
#  pragma GCC diagnostic ignored "-Wconversion"
#endif

/* Embed the LZ4 implementation directly (decompressor only). */
#define LZ4_DISABLE_DEPRECATE_WARNINGS
#include "lz4/lz4.c"  /* NOLINT: intentional source-file inclusion */

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

/*
 * lusd__lz4_decompress — decompress data in USDC's chunked LZ4 format.
 *
 * Parameters:
 *   comp      - pointer to compressed data (including the nChunks header byte)
 *   comp_size - total byte count of compressed data
 *   out       - destination buffer
 *   max_out   - capacity of destination buffer (= expected uncompressed size)
 *
 * Returns the number of decompressed bytes written, or 0 on error.
 */
uint64_t lusd__lz4_decompress(const uint8_t* comp,
                               uint64_t       comp_size,
                               uint8_t*       out,
                               uint64_t       max_out)
{
    if (!comp || !out || comp_size < 1 || max_out == 0) return 0;

    int nChunks = (int)(uint8_t)comp[0];
    const uint8_t* src = comp + 1;
    const uint8_t* src_end = comp + comp_size;
    uint64_t total = 0;

    if (nChunks == 0) {
        /* Single chunk: remaining bytes are LZ4 block data */
        uint64_t srcRemain = (uint64_t)(src_end - src);
        if (srcRemain == 0) return 0;
        int nDec = LZ4_decompress_safe(
            (const char*)src,
            (char*)out,
            (int)srcRemain,
            (int)max_out);
        if (nDec < 0) return 0;
        return (uint64_t)nDec;
    }

    /* Multi-chunk: each chunk prefixed by int32_t compressed size */
    uint8_t* dst = out;
    uint64_t dstRemain = max_out;

    for (int c = 0; c < nChunks; c++) {
        if (src + 4 > src_end) return 0;
        int32_t chunkSz = 0;
        memcpy(&chunkSz, src, 4);
        src += 4;
        if (chunkSz <= 0 || (const uint8_t*)src + chunkSz > src_end) return 0;

        uint64_t chunkOut = (dstRemain < (uint64_t)LZ4_MAX_INPUT_SIZE)
                                ? dstRemain
                                : (uint64_t)LZ4_MAX_INPUT_SIZE;
        int nDec = LZ4_decompress_safe(
            (const char*)src,
            (char*)dst,
            chunkSz,
            (int)chunkOut);
        if (nDec <= 0) return 0;

        src      += (uint64_t)chunkSz;
        dst      += (uint64_t)nDec;
        total    += (uint64_t)nDec;
        dstRemain -= (uint64_t)nDec;
        if (dstRemain == 0) break;
    }

    return total;
}
