// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Numeric array reading functions for CrateReader (int/float/double/half arrays)
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "crate-reader.hh"

#ifdef __wasi__
#else
#include <thread>
#endif

#include <algorithm>
#include <unordered_set>
#include <stack>

#include "crate-format.hh"
#include "parser-timing.hh"
#include "crate-pprint.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "memory-budget.hh"
#include "path-util.hh"
#include "pprint-meta.hh"
#include "core/prim-spec.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"
#include "str-util.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

#include "common-macros.inc"

namespace tinyusdz {
namespace crate {

#define kTag "[Crate]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK((*memory_manager_), (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_->Release(__nbytes)

#define VERSION_LESS_THAN_0_8_0(__version) ((_version[0] == 0) && (_version[1] < 7))

template <class Int>
bool CrateReader::ReadCompressedInts(Int *out,
                                     size_t num_ints) {
  if (num_ints > _config.maxInts) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of ints {} too large. maxInts is set to {}", num_ints, _config.maxInts));
  }

  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;

  size_t compBufferSize = Compressor::GetCompressedBufferSize(num_ints);

  uint64_t compSize;
  if (!_sr->read8(&compSize)) {
    return false;
  }

  if (compSize > compBufferSize) {
    // Truncate
    // TODO: return error?
    compSize = compBufferSize;
  }

  if (compSize > _sr->size()) {
    return false;
  }

  if (compSize < 4) {
    // Too small
    return false;
  }

  // Track persistent decompression buffer budget (only reserve growth delta,
  // never release — these buffers persist across calls)
  if (_decomp_comp_buffer.size() < compBufferSize) {
    size_t delta = compBufferSize - _decomp_comp_buffer_budget;
    CHECK_MEMORY_USAGE(delta);
    _decomp_comp_buffer_budget = compBufferSize;
    _decomp_comp_buffer.resize(compBufferSize);
  }

  if (!_sr->read(size_t(compSize), size_t(compSize),
                reinterpret_cast<uint8_t *>(_decomp_comp_buffer.data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read compressedInts.");
  }

  // Get working space size for decompression
  size_t workingSpaceSize = Compressor::GetDecompressionWorkingSpaceSize(num_ints);

  if (_decomp_working_buffer.size() < workingSpaceSize) {
    size_t delta = workingSpaceSize - _decomp_working_buffer_budget;
    CHECK_MEMORY_USAGE(delta);
    _decomp_working_buffer_budget = workingSpaceSize;
    _decomp_working_buffer.resize(workingSpaceSize);
  }

  bool ret = Compressor::DecompressFromBuffer(
      _decomp_comp_buffer.data(), size_t(compSize), out, num_ints, &_err,
      _decomp_working_buffer.data());

  return ret;
}

template <typename T>
bool CrateReader::ReadIntArray(bool is_compressed, std::vector<T> *d) {

  size_t length{0}; // uncompressed array elements.
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }

    DCOUT("array.len = " << n);
    length = size_t(n);
  }

  DCOUT("array.len = " << length);
  if (length == 0) {
    d->clear();
    return true;
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too large array elements.");
  }

  CHECK_MEMORY_USAGE(sizeof(T) * length);

  d->resize(length);

  if (!is_compressed) {

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(T) * length, sizeof(T) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read integer array data.");
    }

    return true;

  } else {

    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(T) * length;
      // Not stored in compressed for smaller data
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read uncompressed integer array data.");
      }
      return true;
    }

    return ReadCompressedInts(d->data(), d->size());
  }
}

bool CrateReader::ReadHalfArray(bool is_compressed,
                                std::vector<value::half> *d) {
  size_t length;
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Too many array elements {}.", length));
  }

  CHECK_MEMORY_USAGE(length * sizeof(uint16_t));

  d->resize(length);

  if (!is_compressed) {


    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(uint16_t) * length, sizeof(uint16_t) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read half array data.\n";
      return false;
    }

    return true;
  } else {

    //
    // compressed data is represented by integers or look-up table.
    //

    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(uint16_t) * length;
      // Not stored in compressed.
      // reader.ReadContiguous(odata, osize);
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read uncompressed array data.\n";
        return false;
      }
      return true;
    }

    // Read the code
    char code;
    if (!_sr->read1(&code)) {
      _err += "Failed to read the code.\n";
      return false;
    }

    if (code == 'i') {
      // Compressed integers.
      size_t tmp_bytes = sizeof(int32_t) * length;
      CHECK_MEMORY_USAGE(tmp_bytes);
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(tmp_bytes);
        _err += "Failed to read compressed ints in ReadHalfArray.\n";
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        float f = float(ints[i]);
        value::half h = value::float_to_half_full(f);
        (*d)[i] = h;
      }
      REDUCE_MEMORY_USAGE(tmp_bytes);
    } else if (code == 't') {
      // Lookup table & indexes.
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadHalfArray.\n";
        return false;
      }

      if (lutSize > _config.maxArrayElements) {
        _err += "LUT size too large in ReadHalfArray.\n";
        return false;
      }

      size_t lut_bytes = sizeof(value::half) * lutSize;
      size_t idx_bytes = sizeof(uint32_t) * length;
      CHECK_MEMORY_USAGE(lut_bytes + idx_bytes);

      std::vector<value::half> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(value::half) * lutSize, sizeof(value::half) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut table in ReadHalfArray.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut indices in ReadHalfArray.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        if (index >= lutSize) {
          REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
          _err += "LUT index out of bounds in ReadHalfArray.\n";
          return false;
        }
        *o++ = lut[index];
      }
      REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
    } else {
      _err += "Invalid code. Data is currupted\n";
      return false;
    }

    return true;
  }

}

bool CrateReader::ReadFloatArray(bool is_compressed, std::vector<float> *d) {

  size_t length;
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(float));

  d->resize(length);

  if (!is_compressed) {

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(float) * length, sizeof(float) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read float array data.\n";
      return false;
    }

    return true;
  } else {

    //
    // compressed data is represented by integers or look-up table.
    //

    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(float) * length;
      // Not stored in compressed.
      // reader.ReadContiguous(odata, osize);
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read uncompressed array data.\n";
        return false;
      }
      return true;
    }

    // Read the code
    char code;
    if (!_sr->read1(&code)) {
      _err += "Failed to read the code.\n";
      return false;
    }

    if (code == 'i') {
      // Compressed integers.
      size_t tmp_bytes = sizeof(int32_t) * length;
      CHECK_MEMORY_USAGE(tmp_bytes);
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(tmp_bytes);
        _err += "Failed to read compressed ints in ReadFloatArray.\n";
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        d->data()[i] = float(ints[i]);
      }
      REDUCE_MEMORY_USAGE(tmp_bytes);
    } else if (code == 't') {
      // Lookup table & indexes.
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadFloatArray.\n";
        return false;
      }

      if (lutSize > _config.maxArrayElements) {
        _err += "LUT size too large in ReadFloatArray.\n";
        return false;
      }

      size_t lut_bytes = sizeof(float) * lutSize;
      size_t idx_bytes = sizeof(uint32_t) * length;
      CHECK_MEMORY_USAGE(lut_bytes + idx_bytes);

      std::vector<float> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut table in ReadFloatArray.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut indices in ReadFloatArray.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        if (index >= lutSize) {
          REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
          _err += "LUT index out of bounds in ReadFloatArray.\n";
          return false;
        }
        *o++ = lut[index];
      }
      REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
    } else {
      _err += "Invalid code. Data is currupted\n";
      return false;
    }

    return true;
  }

}

bool CrateReader::ReadDoubleArray(bool is_compressed, std::vector<double> *d) {

  size_t length;
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

  if (length == 0) {
    d->clear();
    return true;
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(double));

  d->resize(length);

  if (!is_compressed) {

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read double array data.\n";
      return false;
    }

    return true;
  } else {

    //
    // compressed data is represented by integers or look-up table.
    //

    d->resize(length);

    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(double) * length;
      // Not stored in compressed.
      // reader.ReadContiguous(odata, osize);
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read uncompressed array data.\n";
        return false;
      }
      return true;
    }

    // Read the code
    char code;
    if (!_sr->read1(&code)) {
      _err += "Failed to read the code.\n";
      return false;
    }

    if (code == 'i') {
      // Compressed integers.
      size_t tmp_bytes = sizeof(int32_t) * length;
      CHECK_MEMORY_USAGE(tmp_bytes);
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(tmp_bytes);
        _err += "Failed to read compressed ints in ReadDoubleArray.\n";
        return false;
      }
      std::copy(ints.begin(), ints.end(), d->data());
      REDUCE_MEMORY_USAGE(tmp_bytes);
    } else if (code == 't') {
      // Lookup table & indexes.
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadDoubleArray.\n";
        return false;
      }

      if (lutSize > _config.maxArrayElements) {
        _err += "LUT size too large in ReadDoubleArray.\n";
        return false;
      }

      size_t lut_bytes = sizeof(double) * lutSize;
      size_t idx_bytes = sizeof(uint32_t) * length;
      CHECK_MEMORY_USAGE(lut_bytes + idx_bytes);

      std::vector<double> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut table in ReadDoubleArray.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut indices in ReadDoubleArray.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        if (index >= lutSize) {
          REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
          _err += "LUT index out of bounds in ReadDoubleArray.\n";
          return false;
        }
        *o++ = lut[index];
      }
      REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
    } else {
      _err += "Invalid code. Data is currupted\n";
      return false;
    }

    return true;
  }
}

// TypedArray version with mmap support for float arrays
bool CrateReader::ReadFloatArrayTyped(bool is_compressed, TypedArray<float> *d) {
  size_t length;
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(float));

  d->resize(length);

  if (!is_compressed) {
    if (!_sr->read(sizeof(float) * length, sizeof(float) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read float array data.\n";
      return false;
    }
    return true;
  } else {
    // Handle compressed data
    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(float) * length;
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read uncompressed array data.\n";
        return false;
      }
      return true;
    }

    char code;
    if (!_sr->read1(&code)) {
      _err += "Failed to read the code.\n";
      return false;
    }

    if (code == 'i') {
      size_t tmp_bytes = sizeof(int32_t) * length;
      CHECK_MEMORY_USAGE(tmp_bytes);
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(tmp_bytes);
        _err += "Failed to read compressed ints in ReadFloatArrayTyped.\n";
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        d->data()[i] = float(ints[i]);
      }
      REDUCE_MEMORY_USAGE(tmp_bytes);
    } else if (code == 't') {
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadFloatArrayTyped.\n";
        return false;
      }

      if (lutSize > _config.maxArrayElements) {
        _err += "LUT size too large in ReadFloatArrayTyped.\n";
        return false;
      }

      size_t lut_bytes = sizeof(float) * lutSize;
      size_t idx_bytes = sizeof(uint32_t) * length;
      CHECK_MEMORY_USAGE(lut_bytes + idx_bytes);

      std::vector<float> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut table in ReadFloatArrayTyped.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut indices in ReadFloatArrayTyped.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        if (index >= lutSize) {
          REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
          _err += "LUT index out of bounds in ReadFloatArrayTyped.\n";
          return false;
        }
        *o++ = lut[index];
      }
      REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
    } else {
      _err += "Invalid code. Data is corrupted\n";
      return false;
    }
    return true;
  }
}

// TypedArray version with mmap support for float arrays
bool CrateReader::ReadFloat2ArrayTyped(TypedArray<value::float2> *d) {
  size_t length;
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(value::float2));

  d->resize(length);

  if (!_sr->read(sizeof(value::float2) * length, sizeof(value::float2) * length,
                 reinterpret_cast<uint8_t *>(d->data()))) {
    _err += "Failed to read float2 array data.\n";
    return false;
  }
  return true;
}

// TypedArray version with mmap support for double arrays
bool CrateReader::ReadDoubleArrayTyped(bool is_compressed, TypedArray<double> *d) {
  size_t length;
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  }

  if (length == 0) {
    d->clear();
    return true;
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(double));

  d->resize(length);

  if (!is_compressed) {
    if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read double array data.\n";
      return false;
    }
    return true;
  } else {
    // Handle compressed data
    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(double) * length;
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read uncompressed array data.\n";
        return false;
      }
      return true;
    }

    char code;
    if (!_sr->read1(&code)) {
      _err += "Failed to read the code.\n";
      return false;
    }

    if (code == 'i') {
      size_t tmp_bytes = sizeof(int64_t) * length;
      CHECK_MEMORY_USAGE(tmp_bytes);
      std::vector<int64_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        REDUCE_MEMORY_USAGE(tmp_bytes);
        _err += "Failed to read compressed ints in ReadDoubleArrayTyped.\n";
        return false;
      }
      std::transform(ints.begin(), ints.end(), d->data(),
                     [](int64_t v) { return static_cast<double>(v); });
      REDUCE_MEMORY_USAGE(tmp_bytes);
    } else if (code == 't') {
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadDoubleArrayTyped.\n";
        return false;
      }

      if (lutSize > _config.maxArrayElements) {
        _err += "LUT size too large in ReadDoubleArrayTyped.\n";
        return false;
      }

      size_t lut_bytes = sizeof(double) * lutSize;
      size_t idx_bytes = sizeof(uint32_t) * length;
      CHECK_MEMORY_USAGE(lut_bytes + idx_bytes);

      std::vector<double> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut table in ReadDoubleArrayTyped.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
        _err += "Failed to read lut indices in ReadDoubleArrayTyped.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        if (index >= lutSize) {
          REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
          _err += "LUT index out of bounds in ReadDoubleArrayTyped.\n";
          return false;
        }
        *o++ = lut[index];
      }
      REDUCE_MEMORY_USAGE(lut_bytes + idx_bytes);
    } else {
      _err += "Invalid code. Data is corrupted\n";
      return false;
    }
    return true;
  }
}

// Template implementation for TypedArray version with mmap support for integer arrays
template <typename T>
bool CrateReader::ReadIntArrayTyped(bool is_compressed, TypedArray<T> *d) {
  size_t length{0};
  // < ver 0.7.0  use 32bit
  if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  }

  if (length == 0) {
    d->clear();
    return true;
  }

  if (length > _config.maxInts) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many int elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(T));

  d->resize(length);

  if (!is_compressed) {
    if (!_sr->read(sizeof(T) * length, sizeof(T) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read int array data.\n";
      return false;
    }
    return true;
  } else {
    // Handle compressed data
    if (!ReadCompressedInts(d->data(), length)) {
      _err += "Failed to read compressed int array.\n";
      return false;
    }
    return true;
  }
}

// Explicit template instantiations
template bool CrateReader::ReadCompressedInts<int32_t>(int32_t*, size_t);
template bool CrateReader::ReadCompressedInts<uint32_t>(uint32_t*, size_t);
template bool CrateReader::ReadCompressedInts<int64_t>(int64_t*, size_t);
template bool CrateReader::ReadCompressedInts<uint64_t>(uint64_t*, size_t);
template bool CrateReader::ReadIntArray<int32_t>(bool, std::vector<int32_t>*);
template bool CrateReader::ReadIntArray<uint32_t>(bool, std::vector<uint32_t>*);
template bool CrateReader::ReadIntArray<int64_t>(bool, std::vector<int64_t>*);
template bool CrateReader::ReadIntArray<uint64_t>(bool, std::vector<uint64_t>*);
template bool CrateReader::ReadIntArrayTyped<int32_t>(bool, TypedArray<int32_t>*);
template bool CrateReader::ReadIntArrayTyped<uint32_t>(bool, TypedArray<uint32_t>*);
template bool CrateReader::ReadIntArrayTyped<int64_t>(bool, TypedArray<int64_t>*);
template bool CrateReader::ReadIntArrayTyped<uint64_t>(bool, TypedArray<uint64_t>*);

} // namespace crate
} // namespace tinyusdz
