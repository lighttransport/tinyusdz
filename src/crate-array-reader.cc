// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Array reading operations for Crate reader - Implementation

#include "crate-array-reader.hh"
#include "common-macros.inc"
#include "integerCoding.h"
#include "str-util.hh"
#include "tiny-format.hh"

#define kTag "[CrateArrayReader]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK(memory_manager_, (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_.Release(__nbytes)

namespace tinyusdz {
namespace crate {

// Helper functions for type-specific decompression
static size_t DecompressIntegerHelper(const char* compressed, size_t compressedSize,
                                    int32_t* out, size_t length, std::string* err) {
  return Usd_IntegerCompression::DecompressFromBuffer(compressed, compressedSize, out, length, err);
}

static size_t DecompressIntegerHelper(const char* compressed, size_t compressedSize,
                                    uint32_t* out, size_t length, std::string* err) {
  return Usd_IntegerCompression::DecompressFromBuffer(compressed, compressedSize, out, length, err);
}

static size_t DecompressIntegerHelper(const char* compressed, size_t compressedSize,
                                    int64_t* out, size_t length, std::string* err) {
  return Usd_IntegerCompression64::DecompressFromBuffer(compressed, compressedSize, out, length, err);
}

static size_t DecompressIntegerHelper(const char* compressed, size_t compressedSize,
                                    uint64_t* out, size_t length, std::string* err) {
  return Usd_IntegerCompression64::DecompressFromBuffer(compressed, compressedSize, out, length, err);
}

template <typename Int>
bool CrateArrayReader::ReadCompressedInts(Int *out, size_t length) {
  if (!out) {
    PushError("nullptr passed to ReadCompressedInts");
    return false;
  }

  if (length == 0) {
    return true;
  }

  uint64_t compSize;
  if (!_sr->read8(&compSize)) {
    PushError("Failed to read compressed size");
    return false;
  }

  CHECK_MEMORY_USAGE(compSize);

  std::vector<char> compData(static_cast<size_t>(compSize));
  if (!_sr->read(static_cast<size_t>(compSize), static_cast<size_t>(compSize),
                 reinterpret_cast<uint8_t *>(compData.data()))) {
    REDUCE_MEMORY_USAGE(compSize);
    PushError("Failed to read compressed data");
    return false;
  }

  // Decompress the integers
  std::string decomp_err;
  
  // Use template specialization helper for C++14 compatibility
  size_t actualDecompSize = DecompressIntegerHelper(compData.data(), static_cast<size_t>(compSize),
                                                   out, length, &decomp_err);

  if (!decomp_err.empty()) {
    REDUCE_MEMORY_USAGE(compSize);
    PushError(fmt::format("Integer decompression failed: {}", decomp_err));
    return false;
  }

  // Verify we got the expected amount of data
  if (actualDecompSize != length * sizeof(Int)) {
    REDUCE_MEMORY_USAGE(compSize);
    PushError(fmt::format("Decompression size mismatch: expected {}, got {}", 
                         length * sizeof(Int), actualDecompSize));
    return false;
  }

  return true;
}

template <typename T>
bool CrateArrayReader::ReadIntArray(bool is_compressed, std::vector<T> *d) {
  if (!d) {
    PushError("nullptr passed to ReadIntArray");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  size_t length = static_cast<size_t>(n);
  CHECK_MEMORY_USAGE(length * sizeof(T));

  d->resize(length);

  if (is_compressed) {
    if (!ReadCompressedInts(d->data(), d->size())) {
      REDUCE_MEMORY_USAGE(length * sizeof(T));
      PushError("Failed to read compressed integer array");
      return false;
    }
  } else {
    if (!_sr->read(sizeof(T), length * sizeof(T),
                   reinterpret_cast<uint8_t *>(d->data()))) {
      REDUCE_MEMORY_USAGE(length * sizeof(T));
      PushError("Failed to read uncompressed integer array");
      return false;
    }
  }

  return true;
}

template <typename T>
bool CrateArrayReader::ReadIntArrayTyped(bool is_compressed, TypedArray<T> *d) {
  if (!d) {
    PushError("nullptr passed to ReadIntArrayTyped");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read typed array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  size_t length = static_cast<size_t>(n);
  CHECK_MEMORY_USAGE(length * sizeof(T));

  d->resize(length);

  if (is_compressed) {
    if (!ReadCompressedInts(d->data(), length)) {
      REDUCE_MEMORY_USAGE(length * sizeof(T));
      PushError("Failed to read compressed typed integer array");
      return false;
    }
  } else {
    if (!_sr->read(sizeof(T), length * sizeof(T),
                   reinterpret_cast<uint8_t *>(d->data()))) {
      REDUCE_MEMORY_USAGE(length * sizeof(T));
      PushError("Failed to read uncompressed typed integer array");
      return false;
    }
  }

  return true;
}

bool CrateArrayReader::ReadFloatArray(bool is_compressed, std::vector<float> *d) {
  if (!d) {
    PushError("nullptr passed to ReadFloatArray");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read float array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  size_t length = static_cast<size_t>(n);
  CHECK_MEMORY_USAGE(length * sizeof(float));

  d->resize(length);

  if (is_compressed) {
    // Read compression info
    uint8_t method;
    if (!_sr->read1(&method)) {
      REDUCE_MEMORY_USAGE(length * sizeof(float));
      PushError("Failed to read compression method");
      return false;
    }

    if (method == 0) {
      // Integer-based compression
      std::vector<int32_t> ints(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(length * sizeof(float));
        PushError("Failed to read compressed ints in ReadFloatArray");
        return false;
      }

      // Convert integers to floats
      for (size_t i = 0; i < length; ++i) {
        float f;
        memcpy(&f, &ints[i], sizeof(float));
        (*d)[i] = f;
      }
    } else if (method == 1) {
      // LUT-based compression
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        REDUCE_MEMORY_USAGE(length * sizeof(float));
        PushError("Failed to read lutSize in ReadFloatArray");
        return false;
      }

      std::vector<float> lut(lutSize);
      if (!_sr->read(sizeof(float), lutSize * sizeof(float),
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        REDUCE_MEMORY_USAGE(length * sizeof(float));
        PushError("Failed to read lut table in ReadFloatArray");
        return false;
      }

      std::vector<uint32_t> indexes(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        REDUCE_MEMORY_USAGE(length * sizeof(float));
        PushError("Failed to read lut indices in ReadFloatArray");
        return false;
      }

      // Apply LUT
      for (size_t i = 0; i < length; ++i) {
        if (indexes[i] >= lutSize) {
          REDUCE_MEMORY_USAGE(length * sizeof(float));
          PushError(fmt::format("LUT index out of bounds: {} >= {}", indexes[i], lutSize));
          return false;
        }
        (*d)[i] = lut[indexes[i]];
      }
    } else {
      REDUCE_MEMORY_USAGE(length * sizeof(float));
      PushError(fmt::format("Unsupported float compression method: {}", method));
      return false;
    }
  } else {
    // Uncompressed
    if (!_sr->read(sizeof(float), length * sizeof(float),
                   reinterpret_cast<uint8_t *>(d->data()))) {
      REDUCE_MEMORY_USAGE(length * sizeof(float));
      PushError("Failed to read uncompressed float array");
      return false;
    }
  }

  return true;
}

bool CrateArrayReader::ReadDoubleArray(bool is_compressed, std::vector<double> *d) {
  if (!d) {
    PushError("nullptr passed to ReadDoubleArray");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read double array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  size_t length = static_cast<size_t>(n);
  CHECK_MEMORY_USAGE(length * sizeof(double));

  d->resize(length);

  if (is_compressed) {
    // Similar to float compression but with 64-bit integers
    uint8_t method;
    if (!_sr->read1(&method)) {
      REDUCE_MEMORY_USAGE(length * sizeof(double));
      PushError("Failed to read compression method");
      return false;
    }

    if (method == 0) {
      std::vector<int64_t> ints(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(length * sizeof(double));
        PushError("Failed to read compressed ints in ReadDoubleArray");
        return false;
      }

      for (size_t i = 0; i < length; ++i) {
        double f;
        memcpy(&f, &ints[i], sizeof(double));
        (*d)[i] = f;
      }
    } else {
      REDUCE_MEMORY_USAGE(length * sizeof(double));
      PushError(fmt::format("Unsupported double compression method: {}", method));
      return false;
    }
  } else {
    if (!_sr->read(sizeof(double), length * sizeof(double),
                   reinterpret_cast<uint8_t *>(d->data()))) {
      REDUCE_MEMORY_USAGE(length * sizeof(double));
      PushError("Failed to read uncompressed double array");
      return false;
    }
  }

  return true;
}

bool CrateArrayReader::ReadStringArray(std::vector<std::string> *d) {
  if (!d) {
    PushError("nullptr passed to ReadStringArray");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read string array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  size_t length = static_cast<size_t>(n);
  d->resize(length);

  for (size_t i = 0; i < length; ++i) {
    uint64_t strLen;
    if (!_sr->read8(&strLen)) {
      PushError(fmt::format("Failed to read string length at index {}", i));
      return false;
    }

    if (strLen > 0) {
      CHECK_MEMORY_USAGE(strLen);
      (*d)[i].resize(static_cast<size_t>(strLen));
      if (!_sr->read(static_cast<size_t>(strLen), static_cast<size_t>(strLen),
                     reinterpret_cast<uint8_t *>(&(*d)[i][0]))) {
        PushError(fmt::format("Failed to read string data at index {}", i));
        return false;
      }
    } else {
      (*d)[i].clear();
    }
  }

  return true;
}

// Explicit template instantiations
template bool CrateArrayReader::ReadIntArray<int32_t>(bool, std::vector<int32_t>*);
template bool CrateArrayReader::ReadIntArray<uint32_t>(bool, std::vector<uint32_t>*);
template bool CrateArrayReader::ReadIntArray<int64_t>(bool, std::vector<int64_t>*);
template bool CrateArrayReader::ReadIntArray<uint64_t>(bool, std::vector<uint64_t>*);

template bool CrateArrayReader::ReadIntArrayTyped<int32_t>(bool, TypedArray<int32_t>*);
template bool CrateArrayReader::ReadIntArrayTyped<uint32_t>(bool, TypedArray<uint32_t>*);
template bool CrateArrayReader::ReadIntArrayTyped<int64_t>(bool, TypedArray<int64_t>*);
template bool CrateArrayReader::ReadIntArrayTyped<uint64_t>(bool, TypedArray<uint64_t>*);

template bool CrateArrayReader::ReadCompressedInts<int32_t>(int32_t*, size_t);
template bool CrateArrayReader::ReadCompressedInts<uint32_t>(uint32_t*, size_t);
template bool CrateArrayReader::ReadCompressedInts<int64_t>(int64_t*, size_t);
template bool CrateArrayReader::ReadCompressedInts<uint64_t>(uint64_t*, size_t);

} // namespace crate
} // namespace tinyusdz