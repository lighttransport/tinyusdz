// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LZ4 compression implementation
// Based on USD's TfFastCompression and TinyUSDZ's LZ4Compression

#include "lightusd/lz4_compression.hh"

#include <cstring>
#include <algorithm>

// Include LZ4 implementation (C library)
extern "C" {
#include "lz4/lz4.h"
}

namespace lightusd {
namespace v1 {

size_t LZ4Compression::GetMaxInputSize() {
    return 127 * static_cast<size_t>(LZ4_MAX_INPUT_SIZE);
}

size_t LZ4Compression::GetCompressedBufferSize(size_t inputSize) {
    if (inputSize > GetMaxInputSize()) return 0;

    if (inputSize <= static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) {
        return static_cast<size_t>(LZ4_compressBound(static_cast<int>(inputSize))) + 1;
    }

    size_t nWholeChunks = inputSize / LZ4_MAX_INPUT_SIZE;
    size_t partChunkSz = inputSize % LZ4_MAX_INPUT_SIZE;
    size_t sz = 1 + nWholeChunks *
                    (static_cast<size_t>(LZ4_compressBound(LZ4_MAX_INPUT_SIZE)) + sizeof(int32_t));
    if (partChunkSz) {
        sz += static_cast<size_t>(LZ4_compressBound(static_cast<int>(partChunkSz))) + sizeof(int32_t);
    }
    return sz;
}

size_t LZ4Compression::DecompressFromBuffer(const char* compressedPtr,
                                             char* outputPtr,
                                             size_t compressedSize,
                                             size_t maxOutputSize,
                                             std::string* err) {
    if (compressedSize <= 1) {
        if (err) {
            *err = "Invalid compressedSize";
        }
        return 0;
    }

    // First byte indicates number of chunks
    // 0 = single chunk (data fits in one LZ4 block)
    // 1-127 = multi-chunk (data split across multiple LZ4 blocks)
    int nChunks = static_cast<unsigned char>(*compressedPtr++);
    if (nChunks > 127) {
        if (err) {
            *err = "Too many chunks in LZ4 compressed data";
        }
        return 0;
    }

    size_t consumedCompressedSize = 1;

    if (maxOutputSize < static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) {
        // For small output, nChunks must be 0
        if (nChunks != 0) {
            if (err) {
                *err = "Corrupted LZ4 compressed data (unexpected chunks for small output)";
            }
            return 0;
        }
    }

    if (nChunks == 0) {
        // Single chunk - decompress directly
        int nDecompressed = LZ4_decompress_safe(
            compressedPtr, outputPtr,
            static_cast<int>(compressedSize - 1),
            static_cast<int>(maxOutputSize));

        if (nDecompressed < 0) {
            if (err) {
                *err = "LZ4 decompression failed, error code: " + std::to_string(nDecompressed);
            }
            return 0;
        }
        return static_cast<size_t>(nDecompressed);
    }

    // Multi-chunk decompression
    size_t totalDecompressed = 0;
    for (int i = 0; i < nChunks; ++i) {
        int32_t chunkSize = 0;
        if (consumedCompressedSize + sizeof(chunkSize) > compressedSize) {
            if (err) {
                *err = "Corrupted chunk data (truncated)";
            }
            return 0;
        }

        std::memcpy(&chunkSize, compressedPtr, sizeof(chunkSize));

        if (chunkSize > LZ4_MAX_INPUT_SIZE) {
            if (err) {
                *err = "ChunkSize exceeds LZ4_MAX_INPUT_SIZE";
            }
            return 0;
        }
        if (chunkSize <= 0) {
            if (err) {
                *err = "Invalid ChunkSize";
            }
            return 0;
        }

        consumedCompressedSize += sizeof(chunkSize);
        if (consumedCompressedSize > compressedSize) {
            if (err) {
                *err = "Total chunk size exceeds input compressedSize";
            }
            return 0;
        }

        compressedPtr += sizeof(chunkSize);
        int nDecompressed = LZ4_decompress_safe(
            compressedPtr, outputPtr, chunkSize,
            static_cast<int>(std::min<size_t>(LZ4_MAX_INPUT_SIZE, maxOutputSize)));

        if (nDecompressed <= 0) {
            if (err) {
                *err = "LZ4 chunk decompression failed, error code: " + std::to_string(nDecompressed);
            }
            return 0;
        }

        if (static_cast<size_t>(nDecompressed) > maxOutputSize) {
            if (err) {
                *err = "Decompressed data exceeds output buffer";
            }
            return 0;
        }

        compressedPtr += chunkSize;
        outputPtr += nDecompressed;
        maxOutputSize -= static_cast<size_t>(nDecompressed);
        totalDecompressed += static_cast<size_t>(nDecompressed);
    }

    return totalDecompressed;
}

} // namespace v1
} // namespace lightusd
