// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LZ4 compression utilities for USDC format

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lightusd {
namespace v1 {

/// LZ4 compression/decompression utilities
/// Based on USD's TfFastCompression and TinyUSDZ's LZ4Compression
class LZ4Compression {
public:
    /// Return the largest input buffer size that can be compressed.
    static size_t GetMaxInputSize();

    /// Return the largest possible compressed size for the given inputSize.
    static size_t GetCompressedBufferSize(size_t inputSize);

    /// Compress data to buffer.
    /// @param input Pointer to input data
    /// @param output Pointer to output buffer (must be at least GetCompressedBufferSize bytes)
    /// @param inputSize Size of input data
    /// @param err Optional error string
    /// @return Number of bytes written to output, or 0 on error
    static size_t CompressToBuffer(const char* input, char* output,
                                   size_t inputSize, std::string* err = nullptr);

    /// Decompress data from buffer.
    /// @param compressed Pointer to compressed data
    /// @param output Pointer to output buffer
    /// @param compressedSize Size of compressed data
    /// @param maxOutputSize Maximum size of output buffer
    /// @param err Optional error string
    /// @return Number of bytes written to output, or 0 on error
    static size_t DecompressFromBuffer(const char* compressed, char* output,
                                        size_t compressedSize, size_t maxOutputSize,
                                        std::string* err = nullptr);
};

} // namespace v1
} // namespace lightusd
