// SPDX-License-Identifier: Apache-2.0
// Originally Copyright 2017 Pixar
// Modified for LightUSD
//
// Integer compression utilities for USDC format
// Based on USD's integer coding implementation

#ifndef LIGHTUSD_INTEGER_CODING_HH
#define LIGHTUSD_INTEGER_CODING_HH

#include <cstdint>
#include <cstddef>
#include <string>

namespace lightusd {
namespace v1 {

/// Integer compression for 32-bit integers
class IntegerCompression {
public:
    static size_t GetCompressedBufferSize(size_t numInts);
    static size_t GetDecompressionWorkingSpaceSize(size_t numInts);

    static size_t CompressToBuffer(const int32_t* ints, size_t numInts,
                                   char* compressed, std::string* err);
    static size_t CompressToBuffer(const uint32_t* ints, size_t numInts,
                                   char* compressed, std::string* err);

    static size_t DecompressFromBuffer(const char* compressed, size_t compressedSize,
                                       int32_t* ints, size_t numInts,
                                       std::string* err, char* workingSpace = nullptr);
    static size_t DecompressFromBuffer(const char* compressed, size_t compressedSize,
                                       uint32_t* ints, size_t numInts,
                                       std::string* err, char* workingSpace = nullptr);
};

/// Integer compression for 64-bit integers
class IntegerCompression64 {
public:
    static size_t GetCompressedBufferSize(size_t numInts);
    static size_t GetDecompressionWorkingSpaceSize(size_t numInts);

    static size_t CompressToBuffer(const int64_t* ints, size_t numInts,
                                   char* compressed, std::string* err);
    static size_t CompressToBuffer(const uint64_t* ints, size_t numInts,
                                   char* compressed, std::string* err);

    static size_t DecompressFromBuffer(const char* compressed, size_t compressedSize,
                                       int64_t* ints, size_t numInts,
                                       std::string* err, char* workingSpace = nullptr);
    static size_t DecompressFromBuffer(const char* compressed, size_t compressedSize,
                                       uint64_t* ints, size_t numInts,
                                       std::string* err, char* workingSpace = nullptr);
};

} // namespace v1
} // namespace lightusd

#endif // LIGHTUSD_INTEGER_CODING_HH
