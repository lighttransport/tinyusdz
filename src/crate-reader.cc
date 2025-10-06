// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Crate(binary format) reader
//
//
// TODO:
// - [] Unify BuildDecompressedPathsImpl and BuildNodeHierarchy

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
#include "pprinter.hh"
#include "prim-types.hh"
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

//constexpr auto kTypeName = "typeName";
//constexpr auto kToken = "Token";
//constexpr auto kDefault = "default";

#define kTag "[Crate]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK(memory_manager_, (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_.Release(__nbytes)



#define VERSION_LESS_THAN_0_8_0(__version) ((_version[0] == 0) && (_version[1] < 7))

//
// --
//
CrateReader::CrateReader(StreamReader *sr, const CrateReaderConfig &config) 
    : _sr(sr), memory_manager_(config.maxMemoryBudget), _impl(nullptr) {
  _config = config;
  if (_config.numThreads == -1) {
#if defined(__wasi__)
#else
    _config.numThreads = (std::max)(1, int(std::thread::hardware_concurrency()));
    PUSH_WARN("# of thread to use: " << std::to_string(_config.numThreads));
#endif
  }


#if defined(__wasi__)
  PUSH_WARN("Threading is disabled for WASI build.");
  _config.numThreads = 1;
#else

  // Limit to 1024 threads.
  _config.numThreads = (std::min)(1024, _config.numThreads);
#endif

  //_impl = new Impl();

}

CrateReader::~CrateReader() {
  //delete _impl;
  //_impl = nullptr;
}

bool CrateReader::ReportProgress(float progress) {
  // Check if callback exists and is callable
  if (!_progress_callback) {
    return true;  // No callback, continue parsing
  }
  
  // Clamp progress to [0.0, 1.0]
  progress = std::max(0.0f, std::min(1.0f, progress));
  
  // Call the callback and return its result
  return _progress_callback(progress, _progress_userptr);
}

std::string CrateReader::GetError() { return _err; }

std::string CrateReader::GetWarning() { return _warn; }

bool CrateReader::HasField(const std::string &key) const {
  // Simple linear search
  for (const auto &field : _fields) {
    if (auto fv = GetToken(field.token_index)) {
      if (fv.value().str().compare(key) == 0) {
        return true;
      }
    }
  }
  return false;
}

nonstd::optional<crate::Field> CrateReader::GetField(crate::Index index) const {

  if (index.value < _fields.size()) {
    return _fields[index.value];
  } else {
    return nonstd::nullopt;
  }
}

const nonstd::optional<value::token> CrateReader::GetToken(
    crate::Index token_index) const {
  if (token_index.value < _tokens.size()) {
    return _tokens[token_index.value];
  } else {
    return nonstd::nullopt;
  }
}

// Get string token from string index.
const nonstd::optional<value::token> CrateReader::GetStringToken(
    crate::Index string_index) const {

  if (string_index.value < _string_indices.size()) {
    crate::Index s_idx = _string_indices[string_index.value];
    return GetToken(s_idx);
  } else {
    PUSH_ERROR("String index out of range: " +
               std::to_string(string_index.value));
    return value::token();
  }
}

nonstd::optional<Path> CrateReader::GetPath(crate::Index index) const {

  if (index.value < _paths.size()) {
    // ok
  } else {
    return nonstd::nullopt;
  }

  return _paths[index.value];
}

nonstd::optional<Path> CrateReader::GetElementPath(crate::Index index) const {
  if (index.value < _elemPaths.size()) {
    // ok
  } else {
    return nonstd::nullopt;
  }

  return _elemPaths[index.value];
}

nonstd::optional<std::string> CrateReader::GetPathString(
    crate::Index index) const {
  if (index.value < _paths.size()) {
    // ok
  } else {
    return nonstd::nullopt;
  }

  const Path &p = _paths[index.value];

  return p.full_path_name();
}

bool CrateReader::ReadIndex(crate::Index *i) {
  // string is serialized as StringIndex
  uint32_t value;
  if (!_sr->read4(&value)) {
    PUSH_ERROR("Failed to read Index");
    return false;
  }

  CHECK_MEMORY_USAGE(sizeof(uint32_t));

  (*i) = crate::Index(value);
  return true;
}

bool CrateReader::ReadIndices(std::vector<crate::Index> *indices) {
  uint64_t n;
  if (!_sr->read8(&n)) {
    return false;
  }

  if (n > _config.maxNumIndices) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many indices.");
  }

  if (n == 0) {
    return true;
  }

  DCOUT("ReadIndices: n = " << n);

  size_t datalen = size_t(n) * sizeof(crate::Index);

  if (datalen > _sr->size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Indices data exceeds USDC size.");
  }

  CHECK_MEMORY_USAGE(datalen);

  indices->resize(size_t(n));

  if (datalen != _sr->read(datalen, datalen,
                          reinterpret_cast<uint8_t *>(indices->data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Indices array.");
  }

  return true;
}

bool CrateReader::ReadString(std::string *s) {
  // string is serialized as StringIndex
  crate::Index string_index;
  if (!ReadIndex(&string_index)) {
    PUSH_ERROR("Failed to read Index for string data.");
    return false;
  }

  if (auto tok = GetStringToken(string_index)) {
    (*s) = tok.value().str();
    CHECK_MEMORY_USAGE(s->size());
    return true;
  }


  PUSH_ERROR("Invalid StringIndex.");
  return false;
}

nonstd::optional<std::string> CrateReader::GetSpecString(
    crate::Index index) const {
  if (index.value < _specs.size()) {
    // ok
  } else {
    return nonstd::nullopt;
  }

  const crate::Spec &spec = _specs[index.value];

  if (auto pathv = GetPathString(spec.path_index)) {
    std::string path_str = pathv.value();
    std::string specty_str = to_string(spec.spec_type);

    return "[Spec] path: " + path_str +
           ", fieldset id: " + std::to_string(spec.fieldset_index.value) +
           ", spec_type: " + specty_str;
  }

  return nonstd::nullopt;
}

bool CrateReader::ReadValueRep(crate::ValueRep *rep) {
  if (!_sr->read8(reinterpret_cast<uint64_t *>(rep))) {
    PUSH_ERROR("Failed to read ValueRep.");
    return false;
  }

  CHECK_MEMORY_USAGE(sizeof(uint64_t));

  DCOUT("ValueRep value = " << rep->GetData());

  return true;
}

template <class Int>
bool CrateReader::ReadCompressedInts(Int *out,
                                     size_t num_ints) {
  if (num_ints > _config.maxInts) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of ints {} too large. maxInts is set to {}", num_ints, _config.maxInts));
  }

  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;


  // TODO: Read compressed data from _sr directly
  size_t compBufferSize = Compressor::GetCompressedBufferSize(num_ints);
  CHECK_MEMORY_USAGE(compBufferSize);

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

  std::vector<char> compBuffer;
  compBuffer.resize(compBufferSize);
  if (!_sr->read(size_t(compSize), size_t(compSize),
                reinterpret_cast<uint8_t *>(compBuffer.data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read compressedInts.");
  }

  bool ret = Compressor::DecompressFromBuffer(
      compBuffer.data(), size_t(compSize), out, num_ints, &_err);

  REDUCE_MEMORY_USAGE(compBufferSize);

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
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        _err += "Failed to read compressed ints in ReadHalfArray.\n";
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        float f = float(ints[i]);
        value::half h = value::float_to_half_full(f);
        (*d)[i] = h;
      }
    } else if (code == 't') {
      // Lookup table & indexes.
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadHalfArray.\n";
        return false;
      }

      std::vector<value::half> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(value::half) * lutSize, sizeof(value::half) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        _err += "Failed to read lut table in ReadHalfArray.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        _err += "Failed to read lut indices in ReadHalfArray.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        *o++ = lut[index];
      }
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
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        _err += "Failed to read compressed ints in ReadFloatArray.\n";
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        d->data()[i] = float(ints[i]);
      }
    } else if (code == 't') {
      // Lookup table & indexes.
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadFloatArray.\n";
        return false;
      }

      std::vector<float> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        _err += "Failed to read lut table in ReadFloatArray.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        _err += "Failed to read lut indices in ReadFloatArray.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        *o++ = lut[index];
      }
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
      std::vector<int32_t> ints;
      ints.resize(length);
      if (!ReadCompressedInts(ints.data(), ints.size())) {
        _err += "Failed to read compressed ints in ReadDoubleArray.\n";
        return false;
      }
      std::copy(ints.begin(), ints.end(), d->data());
    } else if (code == 't') {
      // Lookup table & indexes.
      uint32_t lutSize;
      if (!_sr->read4(&lutSize)) {
        _err += "Failed to read lutSize in ReadDoubleArray.\n";
        return false;
      }

      std::vector<double> lut;
      lut.resize(lutSize);
      if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                     reinterpret_cast<uint8_t *>(lut.data()))) {
        _err += "Failed to read lut table in ReadDoubleArray.\n";
        return false;
      }

      std::vector<uint32_t> indexes;
      indexes.resize(length);
      if (!ReadCompressedInts(indexes.data(), indexes.size())) {
        _err += "Failed to read lut indices in ReadDoubleArray.\n";
        return false;
      }

      auto o = d->data();
      for (auto index : indexes) {
        *o++ = lut[index];
      }
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

  if (!is_compressed && _config.use_mmap) {
    // Use TypedArray view mode - no allocation, just point to mmap'd data
    uint64_t current_pos = _sr->tell();
    const uint8_t* data_ptr = _sr->data() + current_pos;
    
    // Create a view over the mmap'd data
    *d = TypedArray<float>(const_cast<float*>(reinterpret_cast<const float*>(data_ptr)), length, true);
    
    // Advance the stream position
    if (!_sr->seek_from_current(int64_t(sizeof(float) * length))) {
      _err += "Failed to advance stream position.\n";
      return false;
    }
    
    return true;
  } else {
    // Fall back to regular allocation for compressed data or when mmap is disabled
    d->resize(length);
    
    if (!is_compressed) {
      if (!_sr->read(sizeof(float) * length, sizeof(float) * length,
                     reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read float array data.\n";
        return false;
      }
      return true;
    } else {
      // Handle compressed data - same as original implementation
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
        std::vector<int32_t> ints;
        ints.resize(length);
        if (!ReadCompressedInts(ints.data(), ints.size())) {
          _err += "Failed to read compressed ints in ReadFloatArrayTyped.\n";
          return false;
        }
        for (size_t i = 0; i < length; i++) {
          d->data()[i] = float(ints[i]);
        }
      } else if (code == 't') {
        uint32_t lutSize;
        if (!_sr->read4(&lutSize)) {
          _err += "Failed to read lutSize in ReadFloatArrayTyped.\n";
          return false;
        }

        std::vector<float> lut;
        lut.resize(lutSize);
        if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                       reinterpret_cast<uint8_t *>(lut.data()))) {
          _err += "Failed to read lut table in ReadFloatArrayTyped.\n";
          return false;
        }

        std::vector<uint32_t> indexes;
        indexes.resize(length);
        if (!ReadCompressedInts(indexes.data(), indexes.size())) {
          _err += "Failed to read lut indices in ReadFloatArrayTyped.\n";
          return false;
        }

        auto o = d->data();
        for (auto index : indexes) {
          *o++ = lut[index];
        }
      } else {
        _err += "Invalid code. Data is corrupted\n";
        return false;
      }
      return true;
    }
  }
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

  if (!is_compressed && _config.use_mmap) {
    // Use TypedArray view mode - no allocation, just point to mmap'd data
    uint64_t current_pos = _sr->tell();
    const uint8_t* data_ptr = _sr->data() + current_pos;
    
    // Create a view over the mmap'd data
    *d = TypedArray<double>(const_cast<double*>(reinterpret_cast<const double*>(data_ptr)), length, true);
    
    // Advance the stream position
    if (!_sr->seek_from_current(int64_t(sizeof(double) * length))) {
      _err += "Failed to advance stream position.\n";
      return false;
    }
    
    return true;
  } else {
    // Fall back to regular allocation for compressed data or when mmap is disabled
    d->resize(length);
    
    if (!is_compressed) {
      if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                     reinterpret_cast<uint8_t *>(d->data()))) {
        _err += "Failed to read double array data.\n";
        return false;
      }
      return true;
    } else {
      // Handle compressed data - same as original implementation
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
        std::vector<int64_t> ints;
        ints.resize(length);
        if (!ReadCompressedInts(ints.data(), ints.size())) {
          _err += "Failed to read compressed ints in ReadDoubleArrayTyped.\n";
          return false;
        }
        std::copy(ints.begin(), ints.end(), d->data());
      } else if (code == 't') {
        uint32_t lutSize;
        if (!_sr->read4(&lutSize)) {
          _err += "Failed to read lutSize in ReadDoubleArrayTyped.\n";
          return false;
        }

        std::vector<double> lut;
        lut.resize(lutSize);
        if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                       reinterpret_cast<uint8_t *>(lut.data()))) {
          _err += "Failed to read lut table in ReadDoubleArrayTyped.\n";
          return false;
        }

        std::vector<uint32_t> indexes;
        indexes.resize(length);
        if (!ReadCompressedInts(indexes.data(), indexes.size())) {
          _err += "Failed to read lut indices in ReadDoubleArrayTyped.\n";
          return false;
        }

        auto o = d->data();
        for (auto index : indexes) {
          *o++ = lut[index];
        }
      } else {
        _err += "Invalid code. Data is corrupted\n";
        return false;
      }
      return true;
    }
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

  if (!is_compressed && _config.use_mmap) {
    // Use TypedArray view mode - no allocation, just point to mmap'd data
    uint64_t current_pos = _sr->tell();
    const uint8_t* data_ptr = _sr->data() + current_pos;
    
    // Create a view over the mmap'd data
    *d = TypedArray<T>(const_cast<T*>(reinterpret_cast<const T*>(data_ptr)), length, true);
    
    // Advance the stream position
    if (!_sr->seek_from_current(int64_t(sizeof(T) * length))) {
      _err += "Failed to advance stream position.\n";
      return false;
    }
    
    return true;
  } else {
    // Fall back to regular allocation for compressed data or when mmap is disabled
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
}

// Explicit template instantiations for common integer types
template bool CrateReader::ReadIntArrayTyped<int32_t>(bool, TypedArray<int32_t>*);
template bool CrateReader::ReadIntArrayTyped<uint32_t>(bool, TypedArray<uint32_t>*);
template bool CrateReader::ReadIntArrayTyped<int64_t>(bool, TypedArray<int64_t>*);
template bool CrateReader::ReadIntArrayTyped<uint64_t>(bool, TypedArray<uint64_t>*);

bool CrateReader::ReadDoubleVector(std::vector<double> *d) {
  size_t length;

  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read the number of array elements.\n";
    return false;
  }

  length = size_t(n);

  if (length == 0) {
    d->clear();
    return true;
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(length * sizeof(double));

  d->resize(length);

  // TODO(syoyo): Zero-copy
  if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                 reinterpret_cast<uint8_t *>(d->data()))) {
    _err += "Failed to read double vector data.\n";
    return false;
  }

  return true;
}

bool CrateReader::ReadTimeSamples(value::TimeSamples *d) {

  // Layout
  //
  // - `times`(double[])
  // - NumValueReps(int64)
  // - ArrayOfValueRep
  //

  // TODO(syoyo): Deferred loading of TimeSamples?(See USD's implementation for details)

  DCOUT("ReadTimeSamples: offt before tell = " << _sr->tell());

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usd/crateFile.cpp for details.
  int64_t offset{0};
  if (!_sr->read8(&offset)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the offset for value in Dictionary.");
    return false;
  }

  DCOUT("TimeSample times value offset = " << offset);
  DCOUT("TimeSample tell = " << _sr->tell());

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek to TimeSample times. Invalid offset value: " +
            std::to_string(offset));
  }

  // TODO(syoyo): Deduplicate times?

  crate::ValueRep times_rep{0};
  if (!ReadValueRep(&times_rep)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for TimeSample' `times` element.");
  }

  // Save offset
  auto values_offset = _sr->tell();

  // TODO: Enable Check if  type `double[]`
#if 0
  if (times_rep.GetType() == crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR) {
    // ok
  } else if ((times_rep.GetType() == crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBOLE) && times_rep.IsArray()) {
    // ok
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("`times` value must be type `double[]`, but got type `{}`", times_rep.GetTypeName()));
  }
#endif

  crate::CrateValue times_value;
  if (!UnpackValueRep(times_rep, &times_value)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of TimeSample's `times` element.");
  }

  // must be an array of double.
  DCOUT("TimeSample times:" << times_value.type_name());

  std::vector<double> times;
  if (auto pv = times_value.get_value<std::vector<double>>()) {
    times = pv.value();
    DCOUT("`times` = " << times);
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("`times` in TimeSamples must be type `double[]`, but got type `{}`", times_value.type_name()));
  }

  //
  // Parse values(elements) of TimeSamples.
  //

  // seek position will be changed in `_UnpackValueRep`, so revert it.
  if (!_sr->seek_set(values_offset)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek to TimeSamples values.");
  }

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // crateFile.cpp for details.
  if (!_sr->read8(&offset)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the offset for value in TimeSamples.");
    return false;
  }

  DCOUT("TimeSample value offset = " << offset);
  DCOUT("TimeSample tell = " << _sr->tell());

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek to TimeSample values. Invalid offset value: " + std::to_string(offset));
  }

  uint64_t num_values{0};
  if (!_sr->read8(&num_values)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of values from TimeSamples.");
    return false;
  }

  DCOUT("Number of values = " << num_values);

  if (times.size() != num_values) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "# of `times` elements and # of values in Crate differs.");
  }

  if (num_values == 0) {
    return true;
  }

  // Read all ValueReps first
  auto vrep_start_offset = _sr->tell();
  std::vector<crate::ValueRep> value_reps(num_values);
  for (size_t i = 0; i < num_values; i++) {
    if (!ReadValueRep(&value_reps[i])) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for TimeSample' value element.");
    }
  }

  // Check if all samples have the same type (homogeneous)
  //bool is_homogeneous = true;
  auto first_type = value_reps[0].GetType();
  bool first_is_array = value_reps[0].IsArray();
  for (size_t i = 1; i < num_values; i++) {
    if (value_reps[i].GetType() != first_type || value_reps[i].IsArray() != first_is_array) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Types in TimeSamples' ValueRep isn't the same.");
      //is_homogeneous = false;
    }
  }



#if 0
  // Check if it's a common type that benefits from typed storage
  bool use_typed_path = false;
  if (is_homogeneous) {
    auto type_id = static_cast<crate::CrateDataTypeId>(first_type);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
    switch (type_id) {
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:
        // POD scalar and array types - use typed/POD path
        use_typed_path = true;
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D:
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D:
        // POD matrix types
        use_typed_path = true;
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING:
        // Non-POD but use typed path for arrays
        use_typed_path = first_is_array;
        break;
      default:
        // All other types don't use typed storage
        use_typed_path = false;
        break;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  }

  if (use_typed_path) {
    DCOUT("Using typed TimeSamples path for type: " << first_type);
    auto type_id = static_cast<crate::CrateDataTypeId>(first_type);
    bool success = false;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
    switch (type_id) {
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<int32_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<int32_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<uint32_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<uint32_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<int64_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<int64_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<uint64_t>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<uint64_t>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<float>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<float>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<double>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<double>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::half>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::half>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<std::string>>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::matrix2d>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::matrix2d>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::matrix3d>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::matrix3d>(times, value_reps, vrep_start_offset, d);
        }
        break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D:
        if (first_is_array) {
          success = CrateTypedTimeSamples<std::vector<value::matrix4d>>(times, value_reps, vrep_start_offset, d);
        } else {
          success = CrateTypedTimeSamples<value::matrix4d>(times, value_reps, vrep_start_offset, d);
        }
        break;
      default:
        // Other types not handled by typed storage
        success = false;
        break;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    if (success) {
      // Move to next location.
      // sizeof(uint64) = sizeof(ValueRep)
      _sr->seek_set(values_offset);
      if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek over TimeSamples's values.");
      }
      return true;
    }

    // Fall back to standard path if typed path fails
    DCOUT("Typed path failed, falling back to standard path");
  }
#endif

  // Rewind to ValueReps start
  _sr->seek_set(vrep_start_offset);

#if 0
  for (size_t i = 0; i < num_values; i++) {
    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for TimeSample' value element.");
    }

    ///
    /// Type check of the content of `value` will be done at ReconstructPrim() in usdc-reader.cc.
    ///
    crate::CrateValue value;
    uint64_t value_offset = rep.GetPayload();
    if (!UnpackValueRepForTimeSamples(rep, value_offset, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of TimeSample's value element.");
    }

    d->add_sample(times[i], value.get_raw());
  }

#else
  if (!UnpackValueRepsToTimeSamples(times, value_reps, d)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack TimeSamples's values.");
    return false;
  }
#endif

  // Move to next location.
  // sizeof(uint64) = sizeof(ValueRep)
  _sr->seek_set(values_offset);
  if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek over TimeSamples's values.");
  }

  // Move to next location.
  // sizeof(uint64) = sizeof(ValueRep)
  _sr->seek_set(values_offset);
  if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek over TimeSamples's values.");
  }


  return true;
}

// Helper template to check if a type is POD (trivial and standard layout)
template<typename T>
struct is_pod_type : std::integral_constant<bool,
    std::is_trivial<T>::value && std::is_standard_layout<T>::value> {};

// Helper to add sample - POD version
template<typename T>
typename std::enable_if<is_pod_type<T>::value, bool>::type
add_sample_to_timesamples(value::TimeSamples *d, double time, const T& val, std::string *err) {
  if (d->is_using_pod()) {
    return d->add_sample_pod<T>(time, val, err);
  } else {
    return d->add_sample(time, value::Value(val), err);
  }
}

// Helper to add sample - non-POD version
template<typename T>
typename std::enable_if<!is_pod_type<T>::value, bool>::type
add_sample_to_timesamples(value::TimeSamples *d, double time, const T& val, std::string *err) {
  return d->add_sample(time, value::Value(val), err);
}

// TODO: Use pod path for array type.
template<typename T>
bool add_sample_to_timesamples(value::TimeSamples *d, double time, const std::vector<T>& val, std::string *err) {
  return d->add_sample(time, value::Value(val), err);
}

// Helper to add blocked sample - POD version
template<typename T>
typename std::enable_if<is_pod_type<T>::value, bool>::type
add_blocked_sample_to_timesamples(value::TimeSamples *d, double time, std::string *err) {
  if (d->is_using_pod()) {
    return d->add_blocked_sample_pod<T>(time, err);
  } else {
    return d->add_blocked_sample(time, value::Value(T{}), err);
  }
}

// Helper to add blocked sample - non-POD version
template<typename T>
typename std::enable_if<!is_pod_type<T>::value, bool>::type
add_blocked_sample_to_timesamples(value::TimeSamples *d, double time, std::string *err) {
  return d->add_blocked_sample(time, value::Value(T{}), err);
}

bool CrateReader::UnpackTimeSampleValue_BOOL(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      // Compressed, inlined or array types are not blocked values
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    // Just add a blocked sample
    if (!add_blocked_sample_to_timesamples<int32_t>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid inlined ValueRep in TimeSamples.");
    }
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

     bool val = data ? true : false;

    if (!add_sample_to_timesamples<bool>(&dst, t, val, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  if (rep.IsArray()) {
    // bool is encoded as 8bit value
    std::vector<bool> v;
          if (rep.GetPayload() == 0) { // empty array
            if (!add_sample_to_timesamples<bool>(&dst, t, v, &_err)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
            }
            return true;
          }

          std::vector<uint8_t> tmp_v;
          if (!ReadArray(&tmp_v)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read bool array.");
          }


          v.resize(tmp_v.size());
          for (size_t i = 0; i < tmp_v.size(); i++) {
            v[i] = tmp_v[i] ? true : false;
          }

          if (!add_sample_to_timesamples<std::vector<bool>>(&dst, t, v, &_err)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
          }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for boolean is invalid.");

  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_INT32(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      // Compressed, inlined or array types are not blocked values
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    // Just add a blocked sample
    if (!add_blocked_sample_to_timesamples<int32_t>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_INT) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid inlined ValueRep in TimeSamples.");
    }
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

     int32_t val;
     memcpy(&val, &data, sizeof(int32_t));

    if (!add_sample_to_timesamples<int32_t>(&dst, t, val, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  }

  if (rep.IsArray()) {
    std::vector<int32_t> v;
          if (rep.GetPayload() == 0) { // empty array
            if (!add_sample_to_timesamples<int32_t>(&dst, t, v, &_err)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
            }
            return true;
          }
          if (!ReadIntArray(rep.IsCompressed(), &v)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Int array.");
          }

          if (v.empty()) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty int array.");
            return false;
          }

          if (!add_sample_to_timesamples<std::vector<int32_t>>(&dst, t, v, &_err)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
          }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for int32_t is invalid.");

  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      // Compressed, inlined or array types are not blocked values
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    // Just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid inlined ValueRep in TimeSamples.");
    }
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

     float val;
     memcpy(&val, &data, sizeof(float));

    if (!add_sample_to_timesamples<float>(&dst, t, val, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

    return true;
  }

  if (rep.IsArray()) {
    std::vector<float> v;
          if (rep.GetPayload() == 0) { // empty array
            if (!add_sample_to_timesamples<float>(&dst, t, v, &_err)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
            }
            return true;
          }
          if (!ReadFloatArray(rep.IsCompressed(), &v)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Int array.");
          }

          DCOUT("timeSamples.FLOAT " << value::print_array_snipped(v));

          if (v.empty()) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty int array.");
            return false;
          }

          if (!add_sample_to_timesamples<std::vector<float>>(&dst, t, v, &_err)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
          }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for float is invalid.");

  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT2(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      // Compressed, inlined or array types are not blocked values
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    // Just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      // Compressed or array types are not inlined
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid inlined ValueRep in TimeSamples.");
    }
    uint32_t data = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

        // Value is represented in int8
        int8_t vdata[2];
        memcpy(&vdata, &data, 2);

        value::float2 v;
        v[0] = float(vdata[0]);
        v[1] = float(vdata[1]);

        DCOUT("value.float2 = " << v);

    if (!add_sample_to_timesamples<value::float2>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }

    return true;
  }

  if (rep.IsArray()) {

    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed float2 not supported for TimeSamples.");
    }

    std::vector<value::float2> v;
          if (rep.GetPayload() == 0) { // empty array
            if (!add_sample_to_timesamples<value::float2>(&dst, t, v, &_err)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
            }
            return true;
          }
          if (!ReadArray(&v)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read vec2 array.");
          }

          DCOUT("timeSamples.FLOAT " << value::print_array_snipped(v));

          if (!add_sample_to_timesamples<std::vector<value::float2>>(&dst, t, v, &_err)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
          }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for float is invalid.");

  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_QUATF(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    // Blocked value
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      // Compressed, inlined or array types are not blocked values
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    // Just add a blocked sample
    if (!add_blocked_sample_to_timesamples<float>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  // just in case
  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  DCOUT("rep " << to_string(rep));

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined quatf is not allowed.");
  }

  if (rep.IsArray()) {

    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed quatf not supported for TimeSamples.");
    }

    std::vector<value::quatf> v;
          if (rep.GetPayload() == 0) { // empty array
            if (!add_sample_to_timesamples<value::quatf>(&dst, t, v, &_err)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
            }
            return true;
          }
          if (!ReadArray(&v)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quatf array.");
          }

          DCOUT("timeSamples.QUATF " << value::print_array_snipped(v));

          if (!add_sample_to_timesamples<std::vector<value::quatf>>(&dst, t, v, &_err)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
          }

  } else {
    // Non-array value is not supported

    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for quatf is invalid.");

  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT3(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::float3>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined float3 is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed float3 not supported for TimeSamples.");
    }

    std::vector<value::float3> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::float3>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read float3 array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::float3>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for float3 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_FLOAT4(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::float4>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined float4 is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed float4 not supported for TimeSamples.");
    }

    std::vector<value::float4> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::float4>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read float4 array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::float4>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for float4 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE2(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::double2>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined double2 is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed double2 not supported for TimeSamples.");
    }

    std::vector<value::double2> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::double2>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double2 array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::double2>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for double2 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE3(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::double3>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined double3 is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed double3 not supported for TimeSamples.");
    }

    std::vector<value::double3> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::double3>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double3 array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::double3>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for double3 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE4(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::double4>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined double4 is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed double4 not supported for TimeSamples.");
    }

    std::vector<value::double4> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::double4>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double4 array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::double4>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for double4 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_QUATH(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::quath>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined quath is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed quath not supported for TimeSamples.");
    }

    std::vector<value::quath> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::quath>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quath array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::quath>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for quath is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_QUATD(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::quatd>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined quatd is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed quatd not supported for TimeSamples.");
    }

    std::vector<value::quatd> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::quatd>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read quatd array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::quatd>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for quatd is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_MATRIX2D(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::matrix2d>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined matrix2d is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed matrix2d not supported for TimeSamples.");
    }

    std::vector<value::matrix2d> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::matrix2d>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read matrix2d array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::matrix2d>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for matrix2d is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_MATRIX3D(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::matrix3d>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined matrix3d is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed matrix3d not supported for TimeSamples.");
    }

    std::vector<value::matrix3d> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::matrix3d>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read matrix3d array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::matrix3d>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for matrix3d is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_MATRIX4D(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<value::matrix4d>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined matrix4d is not allowed.");
  }

  if (rep.IsArray()) {
    if (rep.IsCompressed()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed matrix4d not supported for TimeSamples.");
    }

    std::vector<value::matrix4d> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<value::matrix4d>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadArray(&v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read matrix4d array.");
    }

    if (!add_sample_to_timesamples<std::vector<value::matrix4d>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for matrix4d is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_UINT32(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<uint32_t>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    if (rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid inlined ValueRep in TimeSamples.");
    }
    uint32_t val = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

    if (!add_sample_to_timesamples<uint32_t>(&dst, t, val, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
    return true;
  }

  if (rep.IsArray()) {
    std::vector<uint32_t> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<uint32_t>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadIntArray(rep.IsCompressed(), &v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read uint32 array.");
    }

    if (!add_sample_to_timesamples<std::vector<uint32_t>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for uint32 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_INT64(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<int64_t>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined int64 is not allowed.");
  }

  if (rep.IsArray()) {
    std::vector<int64_t> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<int64_t>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadIntArray(rep.IsCompressed(), &v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read int64 array.");
    }

    if (!add_sample_to_timesamples<std::vector<int64_t>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for int64 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_UINT64(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<uint64_t>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined uint64 is not allowed.");
  }

  if (rep.IsArray()) {
    std::vector<uint64_t> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<uint64_t>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadIntArray(rep.IsCompressed(), &v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read uint64 array.");
    }

    if (!add_sample_to_timesamples<std::vector<uint64_t>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for uint64 is invalid.");
  }

  return true;
}

bool CrateReader::UnpackTimeSampleValue_DOUBLE(double t, const crate::ValueRep &rep, value::TimeSamples &dst) {

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) == crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK) {
    if (rep.IsInlined() || rep.IsCompressed() || rep.IsArray()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid blocked ValueRep in TimeSamples.");
    }
    if (!add_blocked_sample_to_timesamples<double>(&dst, t, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add blocked sample to TimeSamples.");
    }
    return true;
  }

  if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid ValueRep type in TimeSamples.");
  }

  if (rep.IsInlined()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Inlined double is not allowed.");
  }

  if (rep.IsArray()) {
    std::vector<double> v;
    if (rep.GetPayload() == 0) {
      if (!add_sample_to_timesamples<std::vector<double>>(&dst, t, v, &_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
      }
      return true;
    }
    if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read double array.");
    }

    if (!add_sample_to_timesamples<std::vector<double>>(&dst, t, v, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to add sample to TimeSamples.");
    }
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Non-array value for double is invalid.");
  }

  return true;
}


bool CrateReader::UnpackValueRepsToTimeSamples(const std::vector<double> &times,
                                    const std::vector<crate::ValueRep> &vreps,  // value_reps unused
                                    /* uint64_t vrep_start_offset, */
                                    value::TimeSamples *d) {

  if (times.size() != vreps.size()) {
    return false;
  }

  if (times.empty()) {
    return false;
  }

  crate::CrateDataTypeId crate_type_id = static_cast<crate::CrateDataTypeId>(vreps[0].GetType());
  bool crate_is_array = vreps[0].IsArray();

  DCOUT("UnpackValueRepsToTimeSamples");

#define HANDLE_INIT_TYPE_CASE(ctype, is_array, VTYPE) \
  case crate::CrateDataTypeId::ctype: { \
    if (is_array) { \
      if (!d->init(value::TypeTraits<std::vector<VTYPE>>::type_id())) { \
        PUSH_ERROR_AND_RETURN(fmt::format("TimeSamples already initialized with different type. type_id = {}[]({}[]) timeSamples.type_id = {}, crate_type = {}[]", value::TypeTraits<std::vector<VTYPE>>::type_id(), value::TypeTraits<std::vector<VTYPE>>::type_name(), d->type_id(), GetCrateDataTypeName(crate_type_id))); \
      } \
    } else { \
      if (!d->init(value::TypeTraits<VTYPE>::type_id())) { \
        PUSH_ERROR_AND_RETURN(fmt::format("TimeSamples already initialized with different type. type_id = {}({}) timeSamples.type_id = {}, crate_type = {}", value::TypeTraits<VTYPE>::type_id(), value::TypeTraits<VTYPE>::type_name(), d->type_id(), GetCrateDataTypeName(crate_type_id))); \
      } \
    } \
    break; }

#define HANDLE_INIT_VECTOR_TYPE_CASE(ctype, VTYPE) \
  case crate::CrateDataTypeId::ctype: { \
    if (!d->init(value::TypeTraits<std::vector<VTYPE>>::type_id())) { \
      PUSH_ERROR_AND_RETURN(fmt::format("TimeSamples already initialized with different type. type_id = {}({}) timeSamples.type_id = {}, crate_type = {}", value::TypeTraits<VTYPE>::type_id(), value::TypeTraits<VTYPE>::type_name(), d->type_id(), GetCrateDataTypeName(crate_type_id))); \
    } \
    break; }

  switch (crate_type_id) {
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_BOOL, crate_is_array, bool)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_UCHAR, crate_is_array, uint8_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_INT, crate_is_array, int32_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_UINT, crate_is_array, uint32_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_INT64, crate_is_array, int64_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_UINT64, crate_is_array, uint64_t)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_FLOAT, crate_is_array, float)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_DOUBLE, crate_is_array, double)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_HALF, crate_is_array, value::half)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_STRING, crate_is_array, std::string)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_TOKEN, crate_is_array, value::token)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_ASSET_PATH, crate_is_array, value::AssetPath)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_MATRIX2D, crate_is_array, value::matrix2d)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_MATRIX3D, crate_is_array, value::matrix3d)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_MATRIX4D, crate_is_array, value::matrix4d)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_QUATD, crate_is_array, value::quatd)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_QUATF, crate_is_array, value::quatf)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_QUATH, crate_is_array, value::quath)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2D, crate_is_array, value::double2)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2F, crate_is_array, value::float2)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2H, crate_is_array, value::half2)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC2I, crate_is_array, value::int2)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3D, crate_is_array, value::double3)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3F, crate_is_array, value::float3)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3H, crate_is_array, value::half3)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC3I, crate_is_array, value::int3)

    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4D, crate_is_array, value::double4)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4F, crate_is_array, value::float4)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4H, crate_is_array, value::half4)
    HANDLE_INIT_TYPE_CASE(CRATE_DATA_TYPE_VEC4I, crate_is_array, value::int4)


    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_DOUBLE_VECTOR, double)
    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_STRING_VECTOR, std::string)
    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_TOKEN_VECTOR, value::token)
    HANDLE_INIT_VECTOR_TYPE_CASE(CRATE_DATA_TYPE_PATH_VECTOR, Path)

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP:

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER: 
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY:

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD: 
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK: 
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE:
    case crate::CrateDataTypeId::NumDataTypes:
      PUSH_ERROR_AND_RETURN(fmt::format("Unsupported or unimplemented type for TimeSamples. ty = {}, is_array = {}",
        GetCrateDataTypeName(crate_type_id), vreps[0].IsArray()));

  }

#undef HANDLE_INIT_TYPE_CASE
#undef HANDLE_INIT_VECTOR_TYPE_CASE

  for (size_t i = 0; i < vreps.size(); i++) {
    const crate::ValueRep &rep = vreps[i];

    if (!rep.IsInlined()) {
      _sr->seek_set(rep.GetPayload());
    }

    const double curr_time = times[i];

    if (static_cast<crate::CrateDataTypeId>(rep.GetType()) != crate_type_id || rep.IsArray() != crate_is_array) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Inconsistent ValueRep type in TimeSamples.");
    }

    // Dispatch to type-specific unpacker
    if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL) {
      if (!UnpackTimeSampleValue_BOOL(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_INT) {
      if (!UnpackTimeSampleValue_INT32(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT) {
      if (!UnpackTimeSampleValue_UINT32(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64) {
      if (!UnpackTimeSampleValue_INT64(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64) {
      if (!UnpackTimeSampleValue_UINT64(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT) {
      if (!UnpackTimeSampleValue_FLOAT(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE) {
      if (!UnpackTimeSampleValue_DOUBLE(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F) {
      if (!UnpackTimeSampleValue_FLOAT2(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F) {
      if (!UnpackTimeSampleValue_FLOAT3(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F) {
      if (!UnpackTimeSampleValue_FLOAT4(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D) {
      if (!UnpackTimeSampleValue_DOUBLE2(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D) {
      if (!UnpackTimeSampleValue_DOUBLE3(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D) {
      if (!UnpackTimeSampleValue_DOUBLE4(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF) {
      if (!UnpackTimeSampleValue_QUATF(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH) {
      if (!UnpackTimeSampleValue_QUATH(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD) {
      if (!UnpackTimeSampleValue_QUATD(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D) {
      if (!UnpackTimeSampleValue_MATRIX2D(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D) {
      if (!UnpackTimeSampleValue_MATRIX3D(curr_time, rep, *d)) {
        return false;
      }
    } else if (crate_type_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D) {
      if (!UnpackTimeSampleValue_MATRIX4D(curr_time, rep, *d)) {
        return false;
      }
    } else {
      // TODO
      PUSH_ERROR_AND_RETURN(fmt::format("Unimplemented type in TimeSamples: {}", GetCrateDataTypeName(crate_type_id)));
    }

  } 

#if 0
  // Use POD-aware TimeSamples directly for POD types
  // Initialize TimeSamples with the type_id for this type
  if (!d->init(value::TypeTraits<T>::type_id())) {
    // Already initialized with different type - fall back to standard path
    return false;
  }

  // Process each sample
  _sr->seek_set(vrep_start_offset);
  for (size_t i = 0; i < times.size(); i++) {
    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for typed TimeSample' value element.");
    }

    crate::CrateValue value;
    uint64_t value_offset = rep.GetPayload();
    if (!UnpackValueRepForTimeSamples(rep, value_offset, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of typed TimeSample's value element.");
    }

    // Check if this is a "none" (blocked) value
    bool is_blocked = value.get_raw().is_none();

    if (is_blocked) {
      // Handle blocked value using SFINAE helper
      std::string err;
      if (!add_blocked_sample_to_timesamples<T>(d, times[i], &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else if (auto pv = value.get_value<T>()) {
      // Extract typed value and add to TimeSamples using SFINAE helper
      std::string err;
      if (!add_sample_to_timesamples<T>(d, times[i], pv.value(), &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else {
      // Type mismatch - return false to fall back to standard path
      return false;
    }
  }
#endif

  return true;
}

#if 0
template<typename T>
bool CrateReader::CrateTypedTimeSamples(const std::vector<double> &times,
                                         const std::vector<crate::ValueRep> &,  // value_reps unused
                                         uint64_t vrep_start_offset,
                                         value::TimeSamples *d) {
  // Use POD-aware TimeSamples directly for POD types
  // Initialize TimeSamples with the type_id for this type
  if (!d->init(value::TypeTraits<T>::type_id())) {
    // Already initialized with different type - fall back to standard path
    return false;
  }

  // Process each sample
  _sr->seek_set(vrep_start_offset);
  for (size_t i = 0; i < times.size(); i++) {
    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for typed TimeSample' value element.");
    }

    crate::CrateValue value;
    uint64_t value_offset = rep.GetPayload();
    if (!UnpackValueRepForTimeSamples(rep, value_offset, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of typed TimeSample's value element.");
    }

    // Check if this is a "none" (blocked) value
    bool is_blocked = value.get_raw().is_none();

    if (is_blocked) {
      // Handle blocked value using SFINAE helper
      std::string err;
      if (!add_blocked_sample_to_timesamples<T>(d, times[i], &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else if (auto pv = value.get_value<T>()) {
      // Extract typed value and add to TimeSamples using SFINAE helper
      std::string err;
      if (!add_sample_to_timesamples<T>(d, times[i], pv.value(), &err)) {
        if (!err.empty()) {
          _err += err;
        }
        return false;
      }
    } else {
      // Type mismatch - return false to fall back to standard path
      return false;
    }
  }

  return true;
}

// Explicit instantiations for all supported types
// Array types
template bool CrateReader::CrateTypedTimeSamples<std::vector<int32_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<uint32_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<int64_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<uint64_t>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<float>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<double>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::half>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<std::string>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::matrix2d>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::matrix3d>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<std::vector<value::matrix4d>>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);

// Scalar POD types (use PODTimeSamples optimization)
template bool CrateReader::CrateTypedTimeSamples<int32_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<uint32_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<int64_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<uint64_t>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<float>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<double>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::half>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::matrix2d>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::matrix3d>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
template bool CrateReader::CrateTypedTimeSamples<value::matrix4d>(const std::vector<double>&, const std::vector<crate::ValueRep>&, uint64_t, value::TimeSamples*);
#endif

bool CrateReader::ReadStringArray(std::vector<std::string> *d) {
  // array data is not compressed
  auto ReadFn = [this](std::vector<std::string> &result) -> bool {
    uint64_t n{0};
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }

    if (n > _config.maxArrayElements) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
    }

    CHECK_MEMORY_USAGE(size_t(n) * sizeof(crate::Index));

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      PUSH_ERROR("Failed to read STRING_VECTOR data.");
      return false;
    }

    // reconstruct
    CHECK_MEMORY_USAGE(size_t(n) * sizeof(void *));
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      if (auto v = GetStringToken(ivalue[i])) {
        std::string s = v.value().str();
        CHECK_MEMORY_USAGE(s.size());
        result[i] = s;
      } else {
        PUSH_ERROR("Invalid StringIndex.");
      }
    }

    return true;
  };

  std::vector<std::string> items;
  if (!ReadFn(items)) {
    return false;
  }

  (*d) = items;

  return true;
}

bool CrateReader::ReadReference(Reference *d) {

  if (!d) {
    return false;
  }

  // assetPath : string
  // primPath : Path
  // layerOffset : LayerOffset
  // customData : Dict

  std::string assetPath;
  if (!ReadString(&assetPath)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read assetPath in Reference ValueRep.");
  }

  crate::PathIndex index;
  if (!ReadIndex(&index)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read primPath Index in Reference ValueRep.");
  }

  auto path = GetPath(index);
  if (!path) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Path index in Reference ValueRep.");
  }

  LayerOffset layerOffset;
  if (!ReadLayerOffset(&layerOffset)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read LayerOffset in Reference ValueRep.");
  }

  CustomDataType customData;
  if (!ReadCustomData(&customData)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read CustomData(Dict) in Reference ValueRep.");
  }

  d->asset_path = assetPath;
  d->prim_path = path.value();
  d->layerOffset = layerOffset;
  d->customData = customData;

  return true;
}

bool CrateReader::ReadPayload(Payload *d) {

  if (!d) {
    return false;
  }

  // assetPath : string
  // primPath : Path

  std::string assetPath;
  if (!ReadString(&assetPath)) {
    return false;
  }


  crate::PathIndex index;
  if (!ReadIndex(&index)) {
    return false;
  }

  auto path = GetPath(index);
  if (!path) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Path index in Payload ValueRep.");
  }

  // LayerOffset from 0.8.0
  if (VersionGreaterThanOrEqualTo_0_8_0()) {
    LayerOffset layerOffset;
    if (!ReadLayerOffset(&layerOffset)) {
      return false;
    }
    d->layerOffset = layerOffset;
  }

  d->asset_path = assetPath;
  d->prim_path = path.value();

  return true;
}

bool CrateReader::ReadLayerOffset(LayerOffset *d) {
  static_assert(sizeof(LayerOffset) == 8 * 2, "LayerOffset must be 16bytes");

  // double x 2
  if (!_sr->read(sizeof(double), sizeof(double), reinterpret_cast<uint8_t *>(&(d->_offset)))) {
    return false;
  }
  if (!_sr->read(sizeof(double), sizeof(double), reinterpret_cast<uint8_t *>(&(d->_scale)))) {
    return false;
  }

  return true;
}

bool CrateReader::ReadLayerOffsetArray(std::vector<LayerOffset> *d) {
  // array data is not compressed

  uint64_t n{0};
  if (!_sr->read8(&n)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    return false;
  }

  if (n > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  if (n == 0) {
    return true;
  }

  CHECK_MEMORY_USAGE(size_t(n) * sizeof(LayerOffset));

  d->resize(size_t(n));

  if (!_sr->read(size_t(n) * sizeof(LayerOffset),
                 size_t(n) * sizeof(LayerOffset),
                 reinterpret_cast<uint8_t *>(d->data()))) {
    PUSH_ERROR("Failed to read LayerOffset[] data.");
    return false;
  }

  return true;
}

bool CrateReader::ReadPathArray(std::vector<Path> *d) {
  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n{0};
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }

    if (n > _config.maxArrayElements) {
      _err += "Too many Path array elements.\n";
      return false;
    }

    CHECK_MEMORY_USAGE(size_t(n) * sizeof(crate::Index));

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      if (auto pv = GetPath(ivalue[i])) {
        result[i] = pv.value();
      } else {
        PUSH_ERROR("Invalid Index for Path.");
        return false;
      }
    }

    return true;
  };

  std::vector<Path> items;
  if (!ReadFn(items)) {
    _err += "Failed to read Path vector.\n";
    return false;
  }

  (*d) = items;

  return true;
}

bool CrateReader::ReadTokenListOp(ListOp<value::token> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<value::token> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    if (n > _config.maxArrayElements) {
      _err += "Too many ListOp elements.\n";
      return false;
    }

    CHECK_MEMORY_USAGE(size_t(n) * sizeof(crate::Index));

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      if (auto v = GetToken(ivalue[i])) {
        result[i] = v.value();
      } else {
        return false;
      }
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}

bool CrateReader::ReadStringListOp(ListOp<std::string> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<std::string> &result) -> bool {
    uint64_t n{0};
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }


    if (n > _config.maxArrayElements) {
      _err += "Too many ListOp elements.\n";
      return false;
    }

    CHECK_MEMORY_USAGE(size_t(n) * sizeof(crate::Index));

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      if (auto v = GetStringToken(ivalue[i])) {
        result[i] = v.value().str();
      } else {
        return false;
      }
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<std::string> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}

bool CrateReader::ReadPathListOp(ListOp<Path> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    PUSH_ERROR("Failed to read ListOpHeader.");
    return false;
  }

  if (h.IsExplicit()) {
    DCOUT("IsExplicit()");
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n{0};
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }

    if (n > _config.maxArrayElements) {
      _err += "Too many ListOp elements.\n";
      return false;
    }

    CHECK_MEMORY_USAGE(size_t(n) * sizeof(crate::Index));

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      PUSH_ERROR("Failed to read ListOp data..");
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      if (auto pv = GetPath(ivalue[i])) {
        result[i] = pv.value();
      } else {
        PUSH_ERROR("Invalid Index for Path.");
        return false;
      }
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}

template<>
bool CrateReader::ReadArray(std::vector<Reference> *d) {

  if (!d) {
    return false;
  }

  uint64_t n{0};
  if (!_sr->read8(&n)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    return false;
  }

  if (n > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(sizeof(Reference) * n);

  for (size_t i = 0; i < n; i++) {
    Reference p;
    if (!ReadReference(&p)) {
      return false;
    }
    d->emplace_back(p);
  }

  return true;
}

template<>
bool CrateReader::ReadArray(std::vector<Payload> *d) {

  if (!d) {
    return false;
  }

  uint64_t n{0};
  if (VERSION_LESS_THAN_0_8_0(_version)) {
    uint32_t shapesize; // not used
    if (!_sr->read4(&shapesize)) {
      PUSH_ERROR("Failed to read the number of array elements.");
      return false;
    }
    uint32_t _n;
    if (!_sr->read4(&_n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    }
    n = _n;
  } else {
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }
  }

  if (n > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  CHECK_MEMORY_USAGE(sizeof(Payload) * n);

  for (size_t i = 0; i < n; i++) {
    Payload p;
    if (!ReadPayload(&p)) {
      return false;
    }
    d->emplace_back(p);
  }

  return true;
}

// T = int, uint, int64, uint64
template<typename T>
//typename std::enable_if<CrateReader::IsIntType<T>::value, bool>::type
bool CrateReader::ReadArray(std::vector<T> *d) {

  if (!d) {
    return false;
  }

  uint64_t n{0};
  if (VERSION_LESS_THAN_0_8_0(_version)) {
    uint32_t shapesize; // not used
    if (!_sr->read4(&shapesize)) {
      PUSH_ERROR("Failed to read the number of array elements.");
      return false;
    }
    uint32_t _n;
    if (!_sr->read4(&_n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
    }
    n = _n;
  } else {
    if (!_sr->read8(&n)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
      return false;
    }
  }

  if (n > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  if (n == 0) {
    return true;
  }

  CHECK_MEMORY_USAGE(sizeof(T) * size_t(n));

  d->resize(size_t(n));
  if (!_sr->read(sizeof(T) * n, sizeof(T) * size_t(n), reinterpret_cast<uint8_t *>(d->data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read array data");
    return false;
  }

  return true;
}

template<typename T>
bool CrateReader::ReadListOp(ListOp<T> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    PUSH_ERROR("Failed to read ListOpHeader.");
    return false;
  }

  if (h.IsExplicit()) {
    d->ClearAndMakeExplicit();
  }

  //
  // NOTE: array data is not compressed even for Int type
  //

  if (h.HasExplicitItems()) {
    std::vector<T> items;
    if (!ReadArray(&items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<T> items;
    if (!ReadArray(&items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<T> items;
    if (!ReadArray(&items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<T> items;
    if (!ReadArray(&items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<T> items;
    if (!ReadArray(&items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<T> items;
    if (!ReadArray(&items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}


bool CrateReader::ReadVariantSelectionMap(VariantSelectionMap *d) {

  if (!d) {
    return false;
  }

  // map<string, string>

  // n
  // [key, value] * n

  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for VariantsMap data.\n";
    return false;
  }

  if (sz > _config.maxVariantsMapElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "The number of elements for VariantsMap data is too large. Max = " << std::to_string(_config.maxVariantsMapElements) << ", but got " << std::to_string(sz));
  }

  for (size_t i = 0; i < sz; i++) {
    std::string key;
    if (!ReadString(&key)) {
      return false;
    }

    std::string value;
    if (!ReadString(&value)) {
      return false;
    }

    // TODO: Duplicate key check?
    d->emplace(key, value);
  }

  return true;
}

bool CrateReader::ReadCustomData(CustomDataType *d) {
  CustomDataType dict;
  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for Dictionary data.\n";
    return false;
  }

  if (sz > _config.maxDictElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "The number of elements for Dictionary data is too large. Max = " << std::to_string(_config.maxDictElements) << ", but got " << std::to_string(sz));
  }

  DCOUT("# o elements in dict" << sz);

  while (sz--) {
    // key(StringIndex)
    std::string key;

    if (!ReadString(&key)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read key string for Dictionary element.");
    }

    // 8byte for the offset for recursive value. See RecursiveRead() in
    // crateFile.cpp for details.
    int64_t offset{0};
    if (!_sr->read8(&offset)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the offset for value in Dictionary.");
    }

    // -8 to compensate sizeof(offset)
    if (!_sr->seek_from_current(offset - 8)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek. Invalid offset value: " + std::to_string(offset));
    }

    DCOUT("key = " << key);

    crate::ValueRep rep{0};
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read value for Dictionary element.");
    }

    DCOUT("vrep =" << crate::GetCrateDataTypeName(rep.GetType()));

    auto saved_position = _sr->tell();

    crate::CrateValue value;
    if (!UnpackValueRep(rep, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of Dictionary element.");
    }

    if (dict.count(key)) {
      // Duplicated key. maybe ok?
    }
    // CrateValue -> MetaVariable
    MetaVariable var;

    var.set_value(key, value.get_raw());

    dict[key] = var;

    if (!_sr->seek_set(saved_position)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to set seek.");
    }
  }

  (*d) = std::move(dict);
  return true;
}

bool CrateReader::UnpackInlinedValueRep(const crate::ValueRep &rep,
                                        crate::CrateValue *value) {
  if (!rep.IsInlined()) {
    PUSH_ERROR("ValueRep must be inlined value representation.");
    return false;
  }

  const auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
    return false;
  }

  if (rep.IsCompressed()) {
    PUSH_ERROR("Inlinved value must not be compressed.");
    return false;
  }

  if (rep.IsArray()) {
    PUSH_ERROR("Inlined value must not be an array.");
    return false;
  }

  const auto dty = tyRet.value();
  DCOUT(crate::GetCrateDataTypeRepr(dty));

  uint32_t d = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
  DCOUT("d = " << d);

  // TODO(syoyo): Use template SFINE?
  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      value->Set(d ? true : false);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      // AssetPath = TokenIndex for inlined value.
      if (auto v = GetToken(crate::Index(d))) {
        std::string str = v.value().str();

        value::AssetPath assetp(str);
        value->Set(assetp);
        return true;
      } else {
        PUSH_ERROR("Invalid Index for AssetPath.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      if (auto v = GetToken(crate::Index(d))) {
        value::token tok = v.value();

        DCOUT("value.token = " << tok);

        value->Set(tok);

        return true;
      } else {
        PUSH_ERROR("Invalid Index for Token.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      if (auto v = GetStringToken(crate::Index(d))) {
        std::string str = v.value().str();

        DCOUT("value.string = " << str);

        value->Set(str);

        return true;
      } else {
        PUSH_ERROR("Invalid Index for StringToken.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER: {
      if (d >= static_cast<int>(Specifier::Invalid)) {
        _err += "Invalid value for Specifier\n";
        return false;
      }
      Specifier val = static_cast<Specifier>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION: {
      if (d >= static_cast<int>(Permission::Invalid)) {
        _err += "Invalid value for Permission\n";
        return false;
      }
      Permission val = static_cast<Permission>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY: {
      if (d >= static_cast<int>(Variability::Invalid)) {
        _err += "Invalid value for Variability\n";
        return false;
      }
      Variability val = static_cast<Variability>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      uint8_t val;
      memcpy(&val, &d, 1);

      DCOUT("value.uchar = " << val);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      int ival;
      memcpy(&ival, &d, sizeof(int));

      DCOUT("value.int = " << ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      uint32_t val;
      memcpy(&val, &d, sizeof(uint32_t));

      DCOUT("value.uint = " << val);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      // stored as int
      int _ival;
      memcpy(&_ival, &d, sizeof(int));

      DCOUT("value.int = " << _ival);

      int64_t ival = static_cast<int64_t>(_ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {
      // stored as uint32
      uint32_t _ival;
      memcpy(&_ival, &d, sizeof(uint32_t));

      DCOUT("value.int = " << _ival);

      uint64_t ival = static_cast<uint64_t>(_ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      value::half f;
      memcpy(&f, &d, sizeof(value::half));

      DCOUT("value.half = " << f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      float f;
      memcpy(&f, &d, sizeof(float));

      DCOUT("value.float = " << f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      // stored as float
      float _f;
      memcpy(&_f, &d, sizeof(float));

      double f = static_cast<double>(_f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::matrix2d v;
      memset(v.m, 0, sizeof(value::matrix2d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << "\n");

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::matrix3d v;
      memset(v.m, 0, sizeof(value::matrix3d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << ", "
                                    << v.m[2][2] << "\n");

      value->Set(v);

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::matrix4d v;
      memset(v.m, 0, sizeof(value::matrix4d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);
      v.m[3][3] = static_cast<double>(data[3]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << ", "
                                    << v.m[2][2] << ", " << v.m[3][3]);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      // Seems quaternion type is not allowed for Inlined Value.
      PUSH_ERROR("Quaternion type is not allowed for Inlined Value.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::double2 v;
      v[0] = double(data[0]);
      v[1] = double(data[1]);

      DCOUT("value.double2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::float2 v;
      v[0] = float(data[0]);
      v[1] = float(data[1]);

      DCOUT("value.float2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::half3 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[1]));

      DCOUT("value.half2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::int2 v;
      v[0] = int(data[0]);
      v[1] = int(data[1]);

      DCOUT("value.int2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::double3 v;
      v[0] = double(data[0]);
      v[1] = double(data[1]);
      v[2] = double(data[2]);

      DCOUT("value.double3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::float3 v;
      v[0] = float(data[0]);
      v[1] = float(data[1]);
      v[2] = float(data[2]);

      DCOUT("value.float3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::half3 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[1]));
      v[2] = value::float_to_half_full(float(data[2]));

      DCOUT("value.half3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::int3 v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);

      DCOUT("value.int3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::double4 v;
      v[0] = static_cast<double>(data[0]);
      v[1] = static_cast<double>(data[1]);
      v[2] = static_cast<double>(data[2]);
      v[3] = static_cast<double>(data[3]);

      DCOUT("value.doublef = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::float4 v;
      v[0] = static_cast<float>(data[0]);
      v[1] = static_cast<float>(data[1]);
      v[2] = static_cast<float>(data[2]);
      v[3] = static_cast<float>(data[3]);

      DCOUT("value.vec4f = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::half4 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[0]));
      v[2] = value::float_to_half_full(float(data[0]));
      v[3] = value::float_to_half_full(float(data[0]));

      DCOUT("value.vec4h = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::int4 v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);
      v[3] = static_cast<int32_t>(data[3]);

      DCOUT("value.vec4i = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY: {
      // empty dict is allowed
      // TODO: empty(zero value) check?
      //crate::CrateValue::Dictionary dict;
      CustomDataType dict; // use CustomDataType for Dict
      value->Set(dict);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK: {
      // Guess No content for ValueBlock
      value::ValueBlock block;
      value->Set(block);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("ListOp data type `{}` cannot be inlined.",
          crate::GetCrateDataTypeName(dty.dtype_id)));
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Data type `{}` cannot be inlined.",
          crate::GetCrateDataTypeName(dty.dtype_id)));
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Inlined value: " +
          crate::GetCrateDataTypeName(dty.dtype_id));
      return false;
    }
  }

  // Should never reach here.
  return false;
}

#if 0
template<T>
CrateReader::UnpackArrayValue(CrateDataTypeId dty, crate::CrateValue *value_out) {
  uint64_t n;
  if (!_sr->read8(&n)) {
    PUSH_ERROR("Failed to read the number of array elements.");
    return false;
  }

  std::vector<crate::Index> v(static_cast<size_t>(n));
  if (!_sr->read(size_t(n) * sizeof(crate::Index),
                 size_t(n) * sizeof(crate::Index),
                 reinterpret_cast<uint8_t *>(v.data()))) {
    PUSH_ERROR("Failed to read array data.");
    return false;
  }

  return true;
}
#endif

bool CrateReader::UnpackValueRepForTimeSamples(const crate::ValueRep &rep, uint64_t offset, crate::CrateValue *value) {
  if (rep.IsInlined()) {
    return UnpackInlinedValueRep(rep, value);
  }

  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
    return false;
  }

  const auto dty = tyRet.value();

  if (!_sr->seek_set(offset)) {
    PUSH_ERROR("Invalid offset for TimeSamples value.");
    return false;
  }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      if (rep.IsArray()) {
        std::vector<int32_t> v;
        if (rep.GetPayload() == 0) {
          value->Set(v);
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      if (rep.IsArray()) {
        std::vector<uint32_t> v;
        if (rep.GetPayload() == 0) {
          value->Set(v);
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      if (rep.IsArray()) {
        std::vector<int64_t> v;
        if (rep.GetPayload() == 0) {
          value->Set(v);
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int64 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      } else {
        if (rep.IsCompressed()) {
          PUSH_ERROR("Compressed int64 not supported.");
          return false;
        }
        int64_t v;
        if (!_sr->read(sizeof(int64_t), sizeof(int64_t), reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int64 data.");
          return false;
        }
        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) {
          std::vector<float> v;
          value->Set(std::move(v));
          return true;
        }
        std::vector<float> v;
        if (!ReadFloatArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read float array value.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) {
          std::vector<double> v;
          value->Set(std::move(v));
          return true;
        }
        std::vector<double> v;
        if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read double array value.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      } else {
        if (rep.IsCompressed()) {
          PUSH_ERROR("Compressed double not supported.");
          return false;
        }
        double v;
        if (!_sr->read(sizeof(double), sizeof(double), reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double data.");
          return false;
        }
        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed string not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }
        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("String array too large. TinyUSDZ limites it up to {}", _config.maxArrayElements));
        }
        CHECK_MEMORY_USAGE(n * sizeof(crate::Index));
        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index), size_t(n) * sizeof(crate::Index), reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read StringIndex array.");
          return false;
        }
        std::vector<std::string> stringArray(static_cast<size_t>(n));
        for (size_t i = 0; i < n; i++) {
          if (auto stok = GetStringToken(v[i])) {
            stringArray[i] = stok.value().str();
          } else {
            return false;
          }
        }
        value->Set(std::move(stringArray));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed matrix2d not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::matrix2d> v;
        if (rep.GetPayload() == 0) {
          value->Set(v);
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize;
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }
        if (n == 0) {
          value->Set(v);
          return true;
        }
        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large.", n));
        }
        CHECK_MEMORY_USAGE(n * sizeof(value::matrix2d));
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix2d), size_t(n) * sizeof(value::matrix2d), reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix2d array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::matrix2d));
        value::matrix2d v;
        if (!_sr->read(sizeof(value::matrix2d), sizeof(value::matrix2d), reinterpret_cast<uint8_t *>(v.m))) {
          PUSH_ERROR("Failed to read value of `matrix2d` type");
          return false;
        }
        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed matrix3d not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::matrix3d> v;
        if (rep.GetPayload() == 0) {
          value->Set(v);
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize;
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }
        if (n == 0) {
          value->Set(v);
          return true;
        }
        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large.", n));
        }
        CHECK_MEMORY_USAGE(n * sizeof(value::matrix3d));
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix3d), size_t(n) * sizeof(value::matrix3d), reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix3d array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::matrix3d));
        value::matrix3d v;
        if (!_sr->read(sizeof(value::matrix3d), sizeof(value::matrix3d), reinterpret_cast<uint8_t *>(v.m))) {
          PUSH_ERROR("Failed to read value of `matrix3d` type");
          return false;
        }
        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed matrix4d not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::matrix4d> v;
        if (rep.GetPayload() == 0) {
          value->Set(v);
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize;
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }
        if (n == 0) {
          value->Set(v);
          return true;
        }
        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large.", n));
        }
        CHECK_MEMORY_USAGE(n * sizeof(value::matrix4d));
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix4d), size_t(n) * sizeof(value::matrix4d), reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix4d array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::matrix4d));
        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix4d), sizeof(value::matrix4d), reinterpret_cast<uint8_t *>(v.m))) {
          PUSH_ERROR("Failed to read value of `matrix4d` type");
          return false;
        }
        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed token not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }
        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Token array too large. TinyUSDZ limites it up to {}", _config.maxArrayElements));
        }
        CHECK_MEMORY_USAGE(n * sizeof(crate::Index));
        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index), size_t(n) * sizeof(crate::Index), reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }
        std::vector<value::token> tokenArray(static_cast<size_t>(n));
        for (size_t i = 0; i < n; i++) {
          if (auto tok = GetToken(v[i])) {
            tokenArray[i] = tok.value();
          } else {
            return false;
          }
        }
        value->Set(std::move(tokenArray));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) {
          std::vector<value::half> v;
          value->Set(std::move(v));
          return true;
        }
        std::vector<value::half> v;
        if (!ReadHalfArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read half array value.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed half2 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::half2> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read half2 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed half3 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::half3> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read half3 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed half4 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::half4> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read half4 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed float2 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::float2> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read float2 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed float3 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::float3> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read float3 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed float4 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::float4> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read float4 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed double2 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::double2> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read double2 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed double3 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::double3> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read double3 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed double4 not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::double4> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read double4 array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed quath not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::quath> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read quath array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed quatf not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::quatf> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read quatf array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD: {
      if (rep.IsCompressed()) {
        PUSH_ERROR("Compressed quatd not supported for TimeSamples.");
        return false;
      }
      if (rep.IsArray()) {
        std::vector<value::quatd> v;
        if (!ReadArray(&v)) {
          PUSH_ERROR("Failed to read quatd array.");
          return false;
        }
        value->Set(std::move(v));
        return true;
      }
      return false;
    }
    default: {
      PUSH_ERROR(fmt::format("Unsupported type for TimeSamples optimization: {}", crate::GetCrateDataTypeName(dty.dtype_id)));
      return false;
    }
  }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

bool CrateReader::UnpackValueRep(const crate::ValueRep &rep,
                                 crate::CrateValue *value) {
  if (rep.IsInlined()) {
    return UnpackInlinedValueRep(rep, value);
  }

  DCOUT("ValueRep type value = " << rep.GetType());
  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
  }

  const auto dty = tyRet.value();

#define TODO_IMPLEMENT(__dty)                                            \
  {                                                                      \
    PUSH_ERROR("TODO: '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet implemented.");                        \
    return false;                                                        \
  }

#define COMPRESS_UNSUPPORTED_CHECK(__dty)                                     \
  if (rep.IsCompressed()) {                                                   \
    PUSH_ERROR("Compressed [" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet supported.");                               \
    return false;                                                             \
  }

#define NON_ARRAY_UNSUPPORTED_CHECK(__dty)                                   \
  if (!rep.IsArray()) {                                                      \
    PUSH_ERROR("Non array '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet supported.");                              \
    return false;                                                            \
  }

#define ARRAY_UNSUPPORTED_CHECK(__dty)                                      \
  if (rep.IsArray()) {                                                      \
    PUSH_ERROR("Array of '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data type is not yet supported.");                        \
    return false;                                                           \
  }

  // payload is the offset to data.
  uint64_t offset = rep.GetPayload();
  if (!_sr->seek_set(offset)) {
    PUSH_ERROR("Invalid offset.");
    return false;
  }

  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      DCOUT("dtype_id = " << std::to_string(uint32_t(dty.dtype_id)));
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<bool> v;

        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        // bool is encoded as 8bit value.

        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of bool array too large. TinyUSDZ limites it up to {}", _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(uint8_t));

        std::vector<uint8_t> data(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(uint8_t),
                       size_t(n) * sizeof(uint8_t),
                       reinterpret_cast<uint8_t *>(data.data()))) {
          PUSH_ERROR("Failed to read bool array.");
          return false;
        }

        // to std::vector<bool>, whose underlying storage may use 1bit.
        v.resize(size_t(n));
        for (size_t i = 0; i < n; i++) {
          v[i] = data[i] ? true : false;
        }

        value->Set(std::move(v));
        return true;

      } else {
        // non array bool should be inline encoded.
        PUSH_ERROR_AND_RETURN_TAG(kTag, "bool value must be inlined.");
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // AssetPath is encoded as StringIndex for uninlined and array value
      // NOTE: inlined value uses TokenIndex.

      if (rep.IsArray()) {

        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::vector<value::AssetPath>());
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxAssetPathElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of AssetPaths too large. TinyUSDZ limites it up to {}", _config.maxAssetPathElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(crate::Index));

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read StringIndex array.");
          return false;
        }

        std::vector<value::AssetPath> apaths(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto tokv = GetStringToken(v[i])) {
            DCOUT("StringToken[" << i << "] = " << tokv.value());
            apaths[i] = value::AssetPath(tokv.value().str());
          } else {
            return false;
          }
        }

        value->Set(std::move(apaths));
        return true;
      } else {

        CHECK_MEMORY_USAGE(sizeof(crate::Index));

        crate::Index v;
        if (!_sr->read(sizeof(crate::Index), sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read uint64 data.");
          return false;
        }

        DCOUT("StrIndex = " << v);

        if (auto tokv = GetStringToken(v)) {
          DCOUT("StringToken = " << tokv.value());
          value::AssetPath apath(tokv.value().str());
          value->Set(apath);
        } else {
          PUSH_ERROR_AND_RETURN("Invalid StringToken found.");
          return false;
        }

        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {

        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::vector<value::token>());
          return true;
        }

        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Token array too large. TinyUSDZ limits it up to {}", _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(crate::Index));

        std::vector<crate::Index> v;
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<value::token> tokens(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto tokv = GetToken(v[i])) {
            DCOUT("Token[" << i << "] = " << tokv.value());
            tokens[i] = tokv.value();
          } else {
            return false;
          }
        }

        value->Set(tokens);
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("String array too large. TinyUSDZ limites it up to {}", _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(crate::Index));

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<std::string> stringArray(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto stok = GetStringToken(v[i])) {
            stringArray[i] = stok.value().str();
          } else {
            return false;
          }
        }

        DCOUT("stringArray = " << stringArray);

        // TODO: Use token type?
        value->Set(std::move(stringArray));

        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY: {
      PUSH_ERROR("TODO: Specifier/Permission/Variability. isArray "
                 << rep.IsArray() << ", isCompressed " << rep.IsCompressed());
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)
      TODO_IMPLEMENT(dty)
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<int32_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int array.");
          return false;
        }

        DCOUT("IntArray = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<uint32_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint array.");
          return false;
        }

        DCOUT("UIntArray = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      if (rep.IsArray()) {
        std::vector<int64_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int64 array.");
          return false;
        }

        DCOUT("Int64Array = " << v);

        value->Set(std::move(v));
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(int64_t));

        int64_t v;
        if (!_sr->read(sizeof(int64_t), sizeof(int64_t),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int64 data.");
          return false;
        }

        DCOUT("int64 = " << v);

        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {
      if (rep.IsArray()) {
        std::vector<uint64_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint64 array.");
          return false;
        }

        DCOUT("UInt64Array = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(uint64_t));

        uint64_t v;
        if (!_sr->read(sizeof(uint64_t), sizeof(uint64_t),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read uint64 data.");
          return false;
        }

        DCOUT("uint64 = " << v);

        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      if (rep.IsArray()) {
        std::vector<value::half> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        if (!ReadHalfArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read half array value.");
          return false;
        }

        value->Set(std::move(v));

        return true;
      } else {
        PUSH_ERROR("Non-inlined, non-array Half value is invalid.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<float> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        
        std::vector<float> v;
        if (!ReadFloatArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read float array value.");
          return false;
        }

        DCOUT("FloatArray = " << value::print_array_snipped(v));

        value->Set(std::move(v));

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        PUSH_ERROR("Non-inlined, non-array Float value is not supported.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<double> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        
        std::vector<double> v;
        if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("DoubleArray = " << value::print_array_snipped(v));
        value->Set(std::move(v));

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(double));

        double v{0.0};
        if (!_sr->read_double(&v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("Double " << v);

        value->Set(v);

        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::matrix2d> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(v);
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::matrix2d));


        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix2d),
                       size_t(n) * sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix2d array.");
          return false;
        }

        value->Set(std::move(v));

      } else {
        static_assert(sizeof(value::matrix2d) == (8 * 4), "");

        CHECK_MEMORY_USAGE(sizeof(value::matrix2d));

        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix2d), sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix2d` type\n";
          return false;
        }

        DCOUT("value.matrix2d = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::matrix3d> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(v);
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::matrix3d));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix3d),
                       size_t(n) * sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix3d array.");
          return false;
        }

        value->Set(std::move(v));

      } else {
        static_assert(sizeof(value::matrix3d) == (8 * 9), "");

        CHECK_MEMORY_USAGE(sizeof(value::matrix3d));

        value::matrix3d v;
        if (!_sr->read(sizeof(value::matrix3d), sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix3d` type\n";
          return false;
        }

        DCOUT("value.matrix3d = " << v);

        value->Set(v);
      }

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::matrix4d> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(v);
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::matrix4d));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix4d),
                       size_t(n) * sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix4d array.");
          return false;
        }

        value->Set(std::move(v));

      } else {
        static_assert(sizeof(value::matrix4d) == (8 * 16), "");

        CHECK_MEMORY_USAGE(sizeof(value::matrix4d));

        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix4d), sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix4d` type\n";
          return false;
        }

        DCOUT("value.matrix4d = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD: {
      if (rep.IsArray()) {
        std::vector<value::quatd> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(v);
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::quatd));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatd),
                       size_t(n) * sizeof(value::quatd),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(std::move(v));

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(value::quatd));

        value::quatd v;
        if (!_sr->read(sizeof(value::quatd), sizeof(value::quatd),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatd value\n";
          return false;
        }

        DCOUT("Quatd = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF: {
      if (rep.IsArray()) {
        std::vector<value::quatf> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(v);
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::quatf));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatf),
                       size_t(n) * sizeof(value::quatf),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(std::move(v));

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(value::quatf));

        value::quatf v;
        if (!_sr->read(sizeof(value::quatf), sizeof(value::quatf),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatf value\n";
          return false;
        }

        DCOUT("Quatf = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      if (rep.IsArray()) {
        std::vector<value::quath> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(v);
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::quath));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quath),
                       size_t(n) * sizeof(value::quath),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quath array.");
          return false;
        }

        DCOUT("Quath[] = " << v);

        value->Set(std::move(v));

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(value::quath));

        value::quath v;
        if (!_sr->read(sizeof(value::quath), sizeof(value::quath),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quath value\n";
          return false;
        }

        DCOUT("Quath = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          TypedArray<value::double2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          TypedArray<value::double2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::double2));

        TypedArray<value::double2> v;
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::double2>(const_cast<value::double2*>(reinterpret_cast<const value::double2*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::double2))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::double2),
                         size_t(n) * sizeof(value::double2),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read double2 array.");
            return false;
          }
        }

        DCOUT("double2[] = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::double2));
        value::double2 v;
        if (!_sr->read(sizeof(value::double2), sizeof(value::double2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double2 data.");
          return false;
        }

        DCOUT("double2 = " << v);

        value->Set(v);
        return true;
      }
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          TypedArray<value::float2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        if (n == 0) {
          TypedArray<value::float2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::float2));

        TypedArray<value::float2> v;
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::float2>(const_cast<value::float2*>(reinterpret_cast<const value::float2*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::float2))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::float2),
                         size_t(n) * sizeof(value::float2),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read float2 array.");
            return false;
          }
        }

        DCOUT("float2[] = " << value::print_array_snipped(v));
        //TUSDZ_LOG_D("float2[] = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::float2));
        value::float2 v;
        if (!_sr->read(sizeof(value::float2), sizeof(value::float2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float2 data.");
          return false;
        }

        DCOUT("float2 = " << v);

        value->Set(v);
        return true;
      }
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          TypedArray<value::half2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::half2));

        TypedArray<value::half2> v;
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::half2>(const_cast<value::half2*>(reinterpret_cast<const value::half2*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::half2))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::half2),
                         size_t(n) * sizeof(value::half2),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read half2 array.");
            return false;
          }
        }

        DCOUT("half2[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::half2));
        value::half2 v;
        if (!_sr->read(sizeof(value::half2), sizeof(value::half2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half2");
          return false;
        }

        DCOUT("half2 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::int2> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::int2));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int2),
                       size_t(n) * sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int2 array.");
          return false;
        }

        DCOUT("int2[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::int2));
        value::int2 v;
        if (!_sr->read(sizeof(value::int2), sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int2");
          return false;
        }

        DCOUT("int2 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          TypedArray<value::double3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::double3));

        TypedArray<value::double3> v;
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::double3>(const_cast<value::double3*>(reinterpret_cast<const value::double3*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::double3))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::double3),
                         size_t(n) * sizeof(value::double3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read double3 array.");
            return false;
          }
        }

        DCOUT("double3[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::double3));
        value::double3 v;
        if (!_sr->read(sizeof(value::double3), sizeof(value::double3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double3");
          return false;
        }

        DCOUT("double3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::float3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::float3));

#if 0
        //TypedArray<value::float3> v;
        if (!rep.IsCompressed() && _config.use_mmap) {
          TypedArray<value::float3> v;
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::float3>(const_cast<value::float3*>(reinterpret_cast<const value::float3*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::float3))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
          DCOUT("float3f[] = " << value::print_array_snipped(v));
          value->Set(std::move(v));
        } else {
#else
        {
#endif
          // Regular allocation for compressed data or when mmap is disabled
          // TODO: Chunked
          std::vector<value::float3> v;
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::float3),
                         size_t(n) * sizeof(value::float3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read float3 array.");
            return false;
          }
          DCOUT("float3f[] = " << value::print_array_snipped(v));
          value->Set(std::move(v));
        }


      } else {
        CHECK_MEMORY_USAGE(sizeof(value::float3));
        value::float3 v;
        if (!_sr->read(sizeof(value::float3), sizeof(value::float3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float3");
          return false;
        }

        DCOUT("float3 = " << v);

        value->Set(v);
      }

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          //TypedArray<value::half3> empty_v;
          std::vector<value::half3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::half3));

        std::vector<value::half3> v;
#if 0
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::half3>(const_cast<value::half3*>(reinterpret_cast<const value::half3*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::half3))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
#else
        {
#endif
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::half3),
                         size_t(n) * sizeof(value::half3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read half3 array.");
            return false;
          }
        }

        DCOUT("half3[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::half3));
        value::half3 v;
        if (!_sr->read(sizeof(value::half3), sizeof(value::half3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half3");
          return false;
        }

        DCOUT("half3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::int3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::int3));

        std::vector<value::int3> v;
#if 0
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::int3>(const_cast<value::int3*>(reinterpret_cast<const value::int3*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::int3))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
#else
        {
#endif
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::int3),
                         size_t(n) * sizeof(value::int3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read int3 array.");
            return false;
          }
        }

        DCOUT("int3[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::int3));
        value::int3 v;
        if (!_sr->read(sizeof(value::int3), sizeof(value::int3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int3");
          return false;
        }

        DCOUT("int3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::double4> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::double4));

        std::vector<value::double4> v;
#if 0
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::double4>(const_cast<value::double4*>(reinterpret_cast<const value::double4*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::double4))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
#else
        {
#endif
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::double4),
                         size_t(n) * sizeof(value::double4),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read double4 array.");
            return false;
          }
        }

        DCOUT("double4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::double4));
        value::double4 v;
        if (!_sr->read(sizeof(value::double4), sizeof(value::double4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double4");
          return false;
        }

        DCOUT("double4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::float4> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::float4));

        std::vector<value::float4> v;
#if 0
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::float4>(const_cast<value::float4*>(reinterpret_cast<const value::float4*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::float4))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
#else
        {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::float4),
                         size_t(n) * sizeof(value::float4),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read float4 array.");
            return false;
          }
        }
#endif

        DCOUT("float4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::float4));
        value::float4 v;
        if (!_sr->read(sizeof(value::float4), sizeof(value::float4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float4");
          return false;
        }

        DCOUT("float4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          TypedArray<value::half4> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::half4));

        TypedArray<value::half4> v;
        if (!rep.IsCompressed() && _config.use_mmap) {
          // Use TypedArray view mode - no allocation, just point to mmap'd data
          uint64_t current_pos = _sr->tell();
          const uint8_t* data_ptr = _sr->data() + current_pos;
          
          // Create a view over the mmap'd data
          v = TypedArray<value::half4>(const_cast<value::half4*>(reinterpret_cast<const value::half4*>(data_ptr)), static_cast<size_t>(n), true);
          
          // Advance stream reader position
          if (!_sr->seek_set(current_pos + n * sizeof(value::half4))) {
            PUSH_ERROR("Failed to advance stream reader position.");
            return false;
          }
        } else {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::half4),
                         size_t(n) * sizeof(value::half4),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read half4 array.");
            return false;
          }
        }

        DCOUT("half4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::half4));
        value::half4 v;
        if (!_sr->read(sizeof(value::half4), sizeof(value::half4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half4");
          return false;
        }

        DCOUT("half4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::int4> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        CHECK_MEMORY_USAGE(n * sizeof(value::int4));

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int4),
                       size_t(n) * sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int4 array.");
          return false;
        }

        DCOUT("int4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::int4));
        value::int4 v;
        if (!_sr->read(sizeof(value::int4), sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int4");
          return false;
        }

        DCOUT("int4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      ARRAY_UNSUPPORTED_CHECK(dty)

      //crate::CrateValue::Dictionary dict;
      CustomDataType dict;

      if (!ReadCustomData(&dict)) {
        _err += "Failed to read Dictionary value\n";
        return false;
      }

      DCOUT("Dict. nelems = " << dict.size());

      value->Set(dict);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP: {
      ListOp<value::token> lst;

      if (!ReadTokenListOp(&lst)) {
        PUSH_ERROR("Failed to read TokenListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // SdfListOp<class SdfPath>
      // => underliying storage is the array of ListOp[PathIndex]
      ListOp<Path> lst;

      if (!ReadPathListOp(&lst)) {
        PUSH_ERROR("Failed to read PathListOp data.");
        return false;
      }

      value->Set(lst);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP: {
      ListOp<std::string> lst;

      if (!ReadStringListOp(&lst)) {
        PUSH_ERROR("Failed to read StringListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      std::vector<Path> v;
      if (!ReadPathArray(&v)) {
        _err += "Failed to read PathVector value\n";
        return false;
      }

      DCOUT("PathVector = " << to_string(v));

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      // std::vector<Index>
      uint64_t n{0};
      if (!_sr->read8(&n)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }

      if (n > _config.maxArrayElements) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
      }

      CHECK_MEMORY_USAGE(n * sizeof(crate::Index));

      std::vector<crate::Index> indices(static_cast<size_t>(n));
      if (!_sr->read(static_cast<size_t>(n) * sizeof(crate::Index),
                     static_cast<size_t>(n) * sizeof(crate::Index),
                     reinterpret_cast<uint8_t *>(indices.data()))) {
        PUSH_ERROR("Failed to read TokenVector value.");
        return false;
      }

      DCOUT("TokenVector(index) = " << indices);

      std::vector<value::token> tokens(indices.size());
      for (size_t i = 0; i < indices.size(); i++) {
        if (auto tokv = GetToken(indices[i])) {
          tokens[i] = tokv.value();
        } else {
          return false;
        }
      }

      DCOUT("TokenVector = " << tokens);

      value->Set(tokens);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      value::TimeSamples ts;
      if (!ReadTimeSamples(&ts)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read TimeSamples data");
      }

      value->Set(ts);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR: {
      std::vector<double> v;
      if (!ReadDoubleVector(&v)) {
        _err += "Failed to read DoubleVector value\n";
        return false;
      }

      DCOUT("DoubleArray = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      std::vector<std::string> v;
      if (!ReadStringArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read StringVector value");
      }

      DCOUT("StringArray = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      VariantSelectionMap m;
      if (!ReadVariantSelectionMap(&m)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read VariantSelectionMap value");
      }

      DCOUT("VariantSelectionMap = " << print_variantSelectionMap(m, 0));

      value->Set(m);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      // LayerOffset[]

      std::vector<LayerOffset> v;
      if (!ReadLayerOffsetArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read LayerOffsetVector value");
      }

      DCOUT("LayerOffsetVector = " << v);

      value->Set(v);

      return true;

    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // Payload
      Payload v;
      if (!ReadPayload(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Payload value");
      }

      DCOUT("Payload = " << v);

      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP: {
      ListOp<Payload> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read PayloadListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP: {
      ListOp<Reference> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read ReferenceListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP: {
      ListOp<int32_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read IntListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP: {
      ListOp<int64_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read Int64ListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP: {
      ListOp<uint32_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read UIntListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP: {
      ListOp<uint64_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read UInt64ListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK: {
      PUSH_ERROR(
          "ValueBlock must be defined in Inlined ValueRep.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE: {

      crate::ValueRep local_rep{0};
      if (!ReadValueRep(&local_rep)) {
        PUSH_ERROR(
            "Failed to read ValueRep for VALUE type.");
        return false;
      }

      if (unpackRecursionGuard.size() > _config.maxValueRecursion) {
        // To many recursive stacks. We report error
        PUSH_ERROR(
            "Too many recursion when decoding generic VALUE data.");
        return false;
      }

      // TODO: use crate::ValueRep for set container type.
      if (unpackRecursionGuard.count(local_rep.GetData())) {
        // Recursion detected.
        PUSH_ERROR(
            "Corrupted Value data detected.");
        return false;
      } else {
        crate::CrateValue local_val;
        bool ret = UnpackValueRep(local_rep, &local_val);
        if (!ret) {
          return false;
        }

        (*value) = local_val;

        unpackRecursionGuard.erase(local_rep.GetData());
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      ARRAY_UNSUPPORTED_CHECK(dty)

      // 8byte for the offset for recursive value. See RecursiveRead() in
      // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usd/crateFile.cpp for details.
      int64_t local_offset{0};
      if (!_sr->read8(&local_offset)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the offset for value in Dictionary.");
        return false;
      }

      DCOUT("UnregisteredValue  offset = " << local_offset);
      DCOUT("tell = " << _sr->tell());

      // -8 to compensate sizeof(offset)
      if (!_sr->seek_from_current(local_offset - 8)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek to UNREGISTERD_VALUE content. Invalid offset value: " +
                std::to_string(local_offset));
      }

      uint64_t saved_position = _sr->tell();

      crate::ValueRep local_rep{0};
      if (!ReadValueRep(&local_rep)) {
        PUSH_ERROR(
            "Failed to read ValueRep for UNREGISTERED_VALUE type.");
        return false;
      }

      auto local_tyRet = crate::GetCrateDataType(local_rep.GetType());
      if (!local_tyRet) {
        PUSH_ERROR(local_tyRet.error());
        return false;
      }

      const auto local_dty = local_tyRet.value();

      // Should be STRING or DICTIONARY for UNREGISTERED_VALUE.
      if (local_dty.dtype_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING) {
        COMPRESS_UNSUPPORTED_CHECK(local_dty)
        ARRAY_UNSUPPORTED_CHECK(local_dty)

        if (local_rep.IsInlined()) {
          uint32_t local_d = (local_rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
          if (auto v = GetStringToken(crate::Index(local_d))) {
            std::string str = v.value().str();

            DCOUT("UNREGISTERED_VALUE.string = " << str);

            // NOTE: string may contain double-quotes.
            // We remove it at here, but it'd be better not to do it.
            std::string unquoted = unwrap(str);
            value->Set(unquoted);

            if (!_sr->seek_set(saved_position)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to set seek.");
            }
            return true;
          } else {
            PUSH_ERROR("Failed to decode String.");
            return false;
          }
        } else {
          PUSH_ERROR("String value must be inlined.");
          return false;
        }

      } else if (local_dty.dtype_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY) {
        COMPRESS_UNSUPPORTED_CHECK(local_dty)
        ARRAY_UNSUPPORTED_CHECK(local_dty)

        CustomDataType dict;

        if (local_rep.IsInlined()) {
          // empty dict
        }  else{
          if (!ReadCustomData(&dict)) {
            _err += "Failed to read Dictionary value\n";
            return false;
          }
        }
        value->Set(dict);
        if (!_sr->seek_set(saved_position)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to set seek.");
        }
        return true;

      } else {
        PUSH_ERROR_AND_RETURN(fmt::format("UNREGISTERD_VALUE type must be string or dictionary, but got other data type: {}(id {}).", GetCrateDataTypeName(local_dty.dtype_id), local_rep.GetType()));
      }

    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Uninlined value: " +
          crate::GetCrateDataTypeName(dty.dtype_id));
      return false;
    }
  }

#undef TODO_IMPLEMENT
#undef COMPRESS_UNSUPPORTED_CHECK
#undef NON_ARRAY_UNSUPPORTED_CHECK

  // Never should reach here.
  return false;
}

#if defined(TINYUSDZ_CRATE_USE_FOR_BASED_PATH_INDEX_DECODER)
bool CrateReader::BuildDecompressedPathsImpl(
    BuildDecompressedPathsArg *arg) {

  if (!arg) {
    return false;
  }

  Path parentPath = arg->parentPath;
  if (!arg->pathIndexes) {
    return false;
  }
  if (!arg->elementTokenIndexes) {
    return false;
  }
  if (!arg->jumps) {
    return false;
  }
  if (!arg->visit_table) {
    return false;
  }
  auto &pathIndexes = *arg->pathIndexes;
  auto &elementTokenIndexes = *arg->elementTokenIndexes;
  auto &jumps = *arg->jumps;
  auto &visit_table = *arg->visit_table;

  auto rootPath = Path::make_root_path();

  const size_t maxIter = _config.maxPathIndicesDecodeIteration;

  std::stack<size_t> startIndexStack;
  std::stack<size_t> endIndexStack;
  std::stack<Path> parentPathStack;

  size_t nIter = 0;

  size_t startIndex = arg->startIndex;
  size_t endIndex = arg->endIndex;

  while (nIter < maxIter) {

    DCOUT("startIndex = " << startIndex << ", endIdx = " << endIndex);

    for (size_t thisIndex = startIndex; thisIndex < (endIndex + 1); thisIndex++) {
      //auto thisIndex = curIndex++;
      DCOUT("thisIndex = " << thisIndex << ", pathIndexes.size = " << pathIndexes.size());
      if (parentPath.is_empty()) {
        // root node.
        // Assume single root node in the scene.
        DCOUT("paths[" << pathIndexes[thisIndex]
                       << "] is parent. name = " << parentPath.full_path_name());
        parentPath = rootPath;

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR("Index exceeds pathIndexes.size()");
          return false;
        }

        size_t idx = pathIndexes[thisIndex];
        if (idx >= _paths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        if (idx < visit_table.size()) {
          if (visit_table[idx]) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Circular referencing of Path index {}(thisIndex {}) detected. Invalid Paths data.", idx, thisIndex));
          }
        }

        _paths[idx] = parentPath;
        visit_table[idx] = true;
      } else {
        if (thisIndex >= elementTokenIndexes.size()) {
          PUSH_ERROR("Index exceeds elementTokenIndexes.size()");
          return false;
        }
        int32_t _tokenIndex = elementTokenIndexes[thisIndex];
        DCOUT("elementTokenIndex = " << _tokenIndex);
        bool isPrimPropertyPath = _tokenIndex < 0;
        // ~0 returns -2147483648, so cast to uint32
        uint32_t tokenIndex = uint32_t(isPrimPropertyPath ? -_tokenIndex : _tokenIndex);

        DCOUT("tokenIndex = " << tokenIndex << ", _tokens.size = " << _tokens.size());
        if (tokenIndex >= _tokens.size()) {
          PUSH_ERROR("Invalid tokenIndex in BuildDecompressedPathsImpl.");
          return false;
        }
        auto const &elemToken = _tokens[size_t(tokenIndex)];
        DCOUT("elemToken = " << elemToken);
        DCOUT("[" << pathIndexes[thisIndex] << "].append = " << elemToken);

        size_t idx = pathIndexes[thisIndex];
        if (idx >= _paths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        if (idx >= _elemPaths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        if (idx < visit_table.size()) {
          if (visit_table[idx]) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Circular referencing of Path index {}(thisIndex {}) detected. Invalid Paths data.", idx, thisIndex));
          }
        }

        // Reconstruct full path
        _paths[idx] =
            isPrimPropertyPath ? parentPath.AppendProperty(elemToken.str())
                               : parentPath.AppendElement(elemToken.str()); // prim, variantSelection, etc.

        // also set leaf path for 'primChildren' check
        _elemPaths[idx] = Path(elemToken.str(), "");
        //_paths[pathIndexes[thisIndex]].SetLocalPart(elemToken.str());

        visit_table[idx] = true;
      }

      // If we have either a child or a sibling but not both, then just
      // continue to the neighbor.  If we have both then spawn a task for the
      // sibling and do the child ourself.  We think that our path trees tend
      // to be broader more often than deep.

      if (thisIndex >= jumps.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      bool hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
      bool hasSibling = (jumps[thisIndex] >= 0);
      DCOUT("hasChild = " << hasChild << ", hasSibling = " << hasSibling);

      if (hasChild) {
        if (hasSibling) {
          // NOTE(syoyo): This recursive call can be parallelized
          auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);

          if (siblingIndex >= jumps.size()) {
            PUSH_ERROR_AND_RETURN("jump index corrupted.");
          }

          // Find subtree end.
          size_t subtreeStartIdx = siblingIndex;
          size_t subtreeIdx = subtreeStartIdx;

          for (; subtreeIdx < jumps.size(); subtreeIdx++) {

            bool has_child = (jumps[subtreeIdx] > 0) || (jumps[subtreeIdx] == -1);
            bool has_sibling = (jumps[subtreeIdx] >= 0);

            if (has_child || has_sibling) {
              continue;
            }
            break;
          }

          size_t subtreeEndIdx = subtreeIdx;
          if (subtreeEndIdx >= jumps.size()) {
            // Guess corrupted.
            PUSH_ERROR_AND_RETURN("jump indices seems corrupted.");
          }

          DCOUT("subtree startIdx " << subtreeStartIdx << ", subtree endIndex " << subtreeEndIdx);

          if (subtreeEndIdx >= subtreeStartIdx) {

            // index range after traversing subtree
            if (jumps[thisIndex] > 1) {

                // Setup stacks to resume loop from [Cont.]
                startIndexStack.push(thisIndex+1);
                // jumps should be always positive, so no siblingIndex < thisIndex
                endIndexStack.push(siblingIndex-1); // endIndex is inclusive so subtract 1.

                {
                  size_t idx = pathIndexes[thisIndex];
                  if (idx >= _paths.size()) {
                    PUSH_ERROR("Index is out-of-range");
                    return false;
                  }

                  parentPathStack.push(_paths[idx]);
                }
            }

            startIndexStack.push(subtreeStartIdx);
            endIndexStack.push(subtreeEndIdx);

            parentPathStack.push(parentPath);
            DCOUT("stack size: " << startIndexStack.size());

            nIter++;

            break; // goto `(A)`
          }

        }

        // [Cont.]
        size_t idx = pathIndexes[thisIndex];
        if (idx >= _paths.size()) {
          PUSH_ERROR("Index is out-of-range");
          return false;
        }

        parentPath = _paths[idx];

      }
    }

    // (A)

    if (startIndexStack.empty()) {
      break; // end traversal
    }

    startIndex = startIndexStack.top();
    startIndexStack.pop();

    endIndex = endIndexStack.top();
    endIndexStack.pop();

    parentPath = parentPathStack.top();
    parentPathStack.pop();

    nIter++;
  }

  if (nIter >= maxIter) {
    PUSH_ERROR_AND_RETURN("PathIndex tree Too deep.");
  }

  return true;
}
#else
bool CrateReader::BuildDecompressedPathsImpl(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps,
    std::vector<bool> &visit_table,
    size_t curIndex, const Path &_parentPath) {

  Path parentPath = _parentPath;

  bool hasChild = false, hasSibling = false;
  do {
    auto thisIndex = curIndex++;
    DCOUT("thisIndex = " << thisIndex << ", pathIndexes.size = " << pathIndexes.size());
    if (parentPath.is_empty()) {
      // root node.
      // Assume single root node in the scene.
      DCOUT("paths[" << pathIndexes[thisIndex]
                     << "] is parent. name = " << parentPath.full_path_name());
      parentPath = Path::make_root_path();

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR("Index exceeds pathIndexes.size()");
        return false;
      }

      size_t idx = pathIndexes[thisIndex];
      if (idx >= _paths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      if (idx < visit_table.size()) {
        if (visit_table[idx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing of Path index tree detected. Invalid Paths data.");
        }
      }

      _paths[idx] = parentPath;
      visit_table[idx] = true;
    } else {
      if (thisIndex >= elementTokenIndexes.size()) {
        PUSH_ERROR("Index exceeds elementTokenIndexes.size()");
        return false;
      }
      int32_t _tokenIndex = elementTokenIndexes[thisIndex];
      DCOUT("elementTokenIndex = " << _tokenIndex);
      bool isPrimPropertyPath = _tokenIndex < 0;
      // ~0 returns -2147483648, so cast to uint32
      uint32_t tokenIndex = uint32_t(isPrimPropertyPath ? -_tokenIndex : _tokenIndex);

      DCOUT("tokenIndex = " << tokenIndex << ", _tokens.size = " << _tokens.size());
      if (tokenIndex >= _tokens.size()) {
        PUSH_ERROR("Invalid tokenIndex in BuildDecompressedPathsImpl.");
        return false;
      }
      auto const &elemToken = _tokens[size_t(tokenIndex)];
      DCOUT("elemToken = " << elemToken);
      DCOUT("[" << pathIndexes[thisIndex] << "].append = " << elemToken);

      size_t idx = pathIndexes[thisIndex];
      if (idx >= _paths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      if (idx >= _elemPaths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      if (idx < visit_table.size()) {
        if (visit_table[idx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing of Path index tree detected. Invalid Paths data.");
        }
      }

      // Reconstruct full path
      _paths[idx] =
          isPrimPropertyPath ? parentPath.AppendProperty(elemToken.str())
                             : parentPath.AppendElement(elemToken.str()); // prim, variantSelection, etc.

      // also set leaf path for 'primChildren' check
      _elemPaths[idx] = Path(elemToken.str(), "");
      //_paths[pathIndexes[thisIndex]].SetLocalPart(elemToken.str());

      visit_table[idx] = true;
    }

    // If we have either a child or a sibling but not both, then just
    // continue to the neighbor.  If we have both then spawn a task for the
    // sibling and do the child ourself.  We think that our path trees tend
    // to be broader more often than deep.

    if (thisIndex >= jumps.size()) {
      PUSH_ERROR("Index is out-of-range");
      return false;
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);
    DCOUT("hasChild = " << hasChild << ", hasSibling = " << hasSibling);

    DCOUT(fmt::format("hasChild {}, hasSibling {}", hasChild, hasSibling));

    if (hasChild) {
      if (hasSibling) {
        // NOTE(syoyo): This recursive call can be parallelized
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, visit_table,
                                        siblingIndex, parentPath)) {
          return false;
        }
      }

      size_t idx = pathIndexes[thisIndex];
      if (idx >= _paths.size()) {
        PUSH_ERROR("Index is out-of-range");
        return false;
      }

      // Have a child (may have also had a sibling). Reset parent path.
      parentPath = _paths[idx];
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}
#endif

#if defined(TINYUSDZ_CRATE_USE_FOR_BASED_PATH_INDEX_DECODER)
bool CrateReader::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps,
    std::vector<bool> &visit_table, /* inout */
    size_t _curIndex,
    int64_t _parentNodeIndex) {

  (void)elementTokenIndexes;

  std::stack<int64_t> parentNodeIndexStack;
  std::stack<size_t> startIndexStack;
  std::stack<size_t> endIndexStack;

  size_t nIter = 0;
  const size_t maxIter = _config.maxPathIndicesDecodeIteration;

  size_t startIndex = _curIndex;
  size_t endIndex = pathIndexes.size() - 1;
  int64_t parentNodeIndex = _parentNodeIndex;

  // NOTE: Need to indirectly lookup index through pathIndexes[] when accessing
  // `_nodes`
  while (nIter < maxIter) {

    for (size_t thisIndex = startIndex; thisIndex < (endIndex + 1); thisIndex++) {
      if (parentNodeIndex == -1) {
        // root node.
        // Assume single root node in the scene.
        //assert(thisIndex == 0);
        if (thisIndex != 0) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "TODO: Multiple root nodes.");
        }

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
        }

        size_t pathIdx = pathIndexes[thisIndex];
        if (pathIdx >= _paths.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= _nodes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= visit_table.size()) {
          // This should not be happan though
          PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
        }

        if (visit_table[pathIdx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
        }

        _nodes[pathIdx] = Node(parentNodeIndex, _paths[pathIdx]);
        visit_table[pathIdx] = true;

        parentNodeIndex = int64_t(thisIndex);

      } else {
        //if (parentNodeIndex >= int64_t(_nodes.size())) {
        //  PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
        //}

        if (parentNodeIndex >= int64_t(pathIndexes.size())) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
        }

        if (thisIndex >= pathIndexes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
        }

        DCOUT("Hierarchy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                                   << "].add_child = " << pathIndexes[thisIndex]);

        size_t pathIdx = pathIndexes[thisIndex];
        if (pathIdx >= _paths.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= _nodes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (pathIdx >= visit_table.size()) {
          // This should not be happan though
          PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
        }

        if (visit_table[pathIdx]) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
        }


        // Ensure parent is not set yet.
        if (_nodes[pathIdx].GetParent() != -2) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "???: Maybe corrupted path hierarchy?.");
        }

        Node node(parentNodeIndex, _paths[pathIdx]);
        _nodes[pathIdx] = node;

        visit_table[pathIdx] = true;

        if (pathIdx >= _elemPaths.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        //std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
        std::string name = _elemPaths[pathIdx].full_path_name();
        DCOUT("childName = " << name);

        size_t parentNodeIdx = size_t(parentNodeIndex);
        if (parentNodeIdx >= pathIndexes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "ParentNodeIdx out-of-range.");
        }

        size_t parentPathIdx = pathIndexes[parentNodeIdx];
        if (parentPathIdx >= _nodes.size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
        }

        if (!_nodes[parentPathIdx].AddChildren(
            name, pathIdx)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid path index.");
        }
      }

      if (thisIndex >= jumps.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Index is out-of-range");
      }

      bool hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
      bool hasSibling = (jumps[thisIndex] >= 0);

      if (hasChild) {
        if (hasSibling) {
          auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);

          if (siblingIndex >= jumps.size()) {
            PUSH_ERROR_AND_RETURN("jump index corrupted.");
          }

          // Find subtree end.
          size_t subtreeStartIdx = siblingIndex;
          size_t subtreeIdx = subtreeStartIdx;

          for (; subtreeIdx < jumps.size(); subtreeIdx++) {

            bool has_child = (jumps[subtreeIdx] > 0) || (jumps[subtreeIdx] == -1);
            bool has_sibling = (jumps[subtreeIdx] >= 0);

            if (has_child || has_sibling) {
              continue;
            }
            break;
          }

          size_t subtreeEndIdx = subtreeIdx;
          if (subtreeEndIdx >= jumps.size()) {
            // Guess corrupted.
            PUSH_ERROR_AND_RETURN("jump indices seems corrupted.");
          }

          DCOUT("subtree startIdx " << subtreeStartIdx << ", subtree endIndex " << subtreeEndIdx);

          if (subtreeEndIdx >= subtreeStartIdx) {

            // index range after traversing subtree
            if (jumps[thisIndex] > 1) {
                startIndexStack.push(thisIndex+1);
                // jumps should be always positive, so no siblingIndex < thisIndex
                endIndexStack.push(siblingIndex-1); // endIndex is inclusive so subtract 1.
                parentNodeIndexStack.push(int64_t(thisIndex));
            }

            startIndexStack.push(subtreeStartIdx);
            endIndexStack.push(subtreeEndIdx);
            parentNodeIndexStack.push(parentNodeIndex);

            DCOUT("stack size: " << startIndexStack.size());

            nIter++;

            break; // goto `(A)`
          }

        }
        // Have a child (may have also had a sibling). Reset parent node index
        parentNodeIndex = int64_t(thisIndex);
        DCOUT("parentNodeIndex = " << parentNodeIndex);
      }
    }

    // (A)

    if (startIndexStack.empty()) {
      break; // end traversal
    }

    startIndex = startIndexStack.top();
    startIndexStack.pop();

    endIndex = endIndexStack.top();
    endIndexStack.pop();

    parentNodeIndex = parentNodeIndexStack.top();
    parentNodeIndexStack.pop();

    nIter++;
  }

  if (nIter >= maxIter) {
    PUSH_ERROR_AND_RETURN("PathIndex tree Too deep.");
  }

  return true;
}
#else
// TODO(syoyo): Refactor. Code is mostly identical to BuildDecompressedPathsImpl
bool CrateReader::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps,
    std::vector<bool> &visit_table, /* inout */
    size_t curIndex,
    int64_t parentNodeIndex) {
  bool hasChild = false, hasSibling = false;

  // NOTE: Need to indirectly lookup index through pathIndexes[] when accessing
  // `_nodes`
  do {
    auto thisIndex = curIndex++;
    DCOUT("thisIndex = " << thisIndex << ", curIndex = " << curIndex);
    if (parentNodeIndex == -1) {
      // root node.
      // Assume single root node in the scene.
      //assert(thisIndex == 0);
      if (thisIndex != 0) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "TODO: Multiple root nodes.");
      }

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
      }

      size_t pathIdx = pathIndexes[thisIndex];
      if (pathIdx >= _paths.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= _nodes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= visit_table.size()) {
        // This should not be happan though
        PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
      }

      if (visit_table[pathIdx]) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
      }

      Node root(parentNodeIndex, _paths[pathIdx]);

      _nodes[pathIdx] = root;
      visit_table[pathIdx] = true;

      parentNodeIndex = int64_t(thisIndex);

    } else {
      //if (parentNodeIndex >= int64_t(_nodes.size())) {
      //  PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
      //}

      if (parentNodeIndex >= int64_t(pathIndexes.size())) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Parent Index out-of-range.");
      }

      if (thisIndex >= pathIndexes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Index out-of-range.");
      }

      DCOUT("Hierarchy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                                 << "].add_child = " << pathIndexes[thisIndex]);

      size_t pathIdx = pathIndexes[thisIndex];
      if (pathIdx >= _paths.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= _nodes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (pathIdx >= visit_table.size()) {
        // This should not be happan though
        PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] out-of-range.");
      }

      if (visit_table[pathIdx]) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Circular referencing detected. Invalid Prim tree representation.");
      }

      Node node(parentNodeIndex, _paths[pathIdx]);

      // Ensure parent is not set yet.
      if (_nodes[pathIdx].GetParent() != -2) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "???: Maybe corrupted path hierarchy?.");
      }

      _nodes[pathIdx] = node;
      visit_table[pathIdx] = true;

      if (pathIdx >= _elemPaths.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      //std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
      std::string name = _elemPaths[pathIdx].full_path_name();
      DCOUT("childName = " << name);

      size_t parentNodeIdx = size_t(parentNodeIndex);
      if (parentNodeIdx >= pathIndexes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "ParentNodeIdx out-of-range.");
      }

      size_t parentPathIdx = pathIndexes[parentNodeIdx];
      if (parentPathIdx >= _nodes.size()) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "PathIndex out-of-range.");
      }

      if (!_nodes[parentPathIdx].AddChildren(
          name, pathIdx)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid path index.");
      }
    }

    if (thisIndex >= jumps.size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Index is out-of-range");
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps, visit_table,
                                siblingIndex, parentNodeIndex)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent node index
      parentNodeIndex = int64_t(thisIndex);
      DCOUT("parentNodeIndex = " << parentNodeIndex);
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}
#endif

bool CrateReader::ReadCompressedPaths(const uint64_t maxNumPaths) {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numEncodedPaths;
  if (!_sr->read8(&numEncodedPaths)) {
    _err += "Failed to read the number of encoded paths.\n";
    return false;
  }

  DCOUT("maxNumPaths : " << maxNumPaths);
  DCOUT("numEncodedPaths : " << numEncodedPaths);

  // Number of compressed paths could be less than maxNumPaths,
  // but should not be greater.
  if (maxNumPaths < numEncodedPaths) {
    _err += "Size mismatch of numEncodedPaths at `PATHS` section.\n";
    return false;
  }


  // 3 = pathIndex, elementTokenIndex, jump
  CHECK_MEMORY_USAGE(size_t(numEncodedPaths) * sizeof(int32_t) * 3);

  pathIndexes.resize(static_cast<size_t>(numEncodedPaths));
  elementTokenIndexes.resize(static_cast<size_t>(numEncodedPaths));
  jumps.resize(static_cast<size_t>(numEncodedPaths));

  size_t compBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(static_cast<size_t>(numEncodedPaths));
  size_t workspaceBufferSize = Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(static_cast<size_t>(numEncodedPaths));
  CHECK_MEMORY_USAGE(compBufferSize);
  CHECK_MEMORY_USAGE(workspaceBufferSize);

  // Create temporary space for decompressing.
  std::vector<char> compBuffer(compBufferSize);
  std::vector<char> workingSpace(workspaceBufferSize);

  // pathIndexes.
  {
    uint64_t compPathIndexesSize;
    if (!_sr->read8(&compPathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (compPathIndexesSize > compBufferSize) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Compressed PathIndexes size.");
    }

    CHECK_MEMORY_USAGE(size_t(compPathIndexesSize));

    if (compPathIndexesSize !=
        _sr->read(size_t(compPathIndexesSize), size_t(compPathIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read compressed pathIndexes data.\n";
      return false;
    }

    DCOUT("comBuffer.size = " << compBuffer.size());
    DCOUT("compPathIndexesSize = " << compPathIndexesSize);

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(compPathIndexesSize), pathIndexes.data(),
        size_t(numEncodedPaths), &err, workingSpace.data());
    if (!err.empty()) {
      _err += "Failed to decode pathIndexes\n" + err;
      return false;
    }
  }

  // elementTokenIndexes.
  {
    uint64_t compElementTokenIndexesSize;
    if (!_sr->read8(&compElementTokenIndexesSize)) {
      _err += "Failed to read elementTokenIndexesSize.\n";
      return false;
    }

    if (compElementTokenIndexesSize > compBufferSize) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Compressed elementTokenIndexes size.");
    }

    CHECK_MEMORY_USAGE(size_t(compElementTokenIndexesSize));

    if (compElementTokenIndexesSize !=
        _sr->read(size_t(compElementTokenIndexesSize),
                  size_t(compElementTokenIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read elementTokenIndexes data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(compElementTokenIndexesSize),
        elementTokenIndexes.data(), size_t(numEncodedPaths), &err,
        workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode elementTokenIndexes.");
      return false;
    }
  }

  // jumps.
  {
    uint64_t compJumpsSize;
    if (!_sr->read8(&compJumpsSize)) {
      PUSH_ERROR("Failed to read compressed jumpsSize.");
      return false;
    }

    if (compJumpsSize > compBufferSize) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Compressed elementTokenIndexes size.");
    }

    CHECK_MEMORY_USAGE(size_t(compJumpsSize));

    if (compJumpsSize !=
        _sr->read(size_t(compJumpsSize), size_t(compJumpsSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read compressed jumps data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(compJumpsSize), jumps.data(), size_t(numEncodedPaths),
        &err, workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode jumps.");
      return false;
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < pathIndexes.size(); i++) {
    DCOUT("pathIndexes[" << i << "] = " << pathIndexes[i]);
  }

  for (size_t i = 0; i < elementTokenIndexes.size(); i++) {
    std::stringstream ss;
    ss << "elementTokenIndexes[" << i << "] = " << elementTokenIndexes[i];
    int32_t tokIdx = elementTokenIndexes[i];
    if (tokIdx < 0) {
      // Property Path. Need to negate it.
      tokIdx = -tokIdx;
    }
    if (auto tokv = GetToken(crate::Index(uint32_t(tokIdx)))) {
      ss << "(" << tokv.value() << ")";
    }
    ss << "\n";
    DCOUT(ss.str());
  }

  for (size_t i = 0; i < jumps.size(); i++) {
    DCOUT(fmt::format("jumps[{}] = {}", i, jumps[i]));
  }
#endif

  // For circular tree check
  std::vector<bool> visit_table;
  CHECK_MEMORY_USAGE(_paths.size()); // TODO: divide by 8?

  // `_paths` is already initialized just before calling this ReadCompressedPaths
  visit_table.resize(_paths.size());
  for (size_t i = 0; i < visit_table.size(); i++) {
    visit_table[i] = false;
  }

  // Now build the paths.
#if defined(TINYUSDZ_CRATE_USE_FOR_BASED_PATH_INDEX_DECODER)
  BuildDecompressedPathsArg arg;
  arg.pathIndexes = &pathIndexes;
  arg.elementTokenIndexes = &elementTokenIndexes;
  arg.jumps = &jumps;
  arg.visit_table = &visit_table;
  arg.startIndex = 0;
  arg.endIndex = pathIndexes.size() - 1; // or numEncodedPaths - 1
  arg.parentPath = Path();
  if (!BuildDecompressedPathsImpl(&arg)) {
    return false;
  }

#else
  if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, visit_table,
                                  /* curIndex */ 0, Path())) {
    return false;
  }
#endif

  //
  // Ensure decoded numEncodedPaths.
  //
  size_t sumDecodedPaths = 0;
  for (size_t i = 0; i < visit_table.size(); i++) {
    if (visit_table[i]) {
      sumDecodedPaths++;
    }
  }
  if (sumDecodedPaths != numEncodedPaths) {
    PUSH_ERROR_AND_RETURN(fmt::format("Decoded {} paths but numEncodedPaths in Crate is {}. Possible corruption of Crate data.",
      sumDecodedPaths, numEncodedPaths));
  }

  // Now build node hierarchy.

  // Circular referencing check should be done in BuildDecompressedPathsImpl,
  // but do check it again just in case.
  for (size_t i = 0; i < visit_table.size(); i++) {
    visit_table[i] = false;
  }
  if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps, visit_table,
                          /* curIndex */ 0, /* parent node index */ -1)) {
    return false;
  }

  sumDecodedPaths = 0;
  for (size_t i = 0; i < visit_table.size(); i++) {
    if (visit_table[i]) {
      sumDecodedPaths++;
    }
  }
  if (sumDecodedPaths != numEncodedPaths) {
    PUSH_ERROR_AND_RETURN(fmt::format("Decoded {} paths but numEncodedPaths in BuildNodeHierarchy is {}. Possible corruption of Crate data.",
      sumDecodedPaths, numEncodedPaths));
  }

  return true;
}

bool CrateReader::ReadSection(crate::Section *s) {
  size_t name_len = crate::kSectionNameMaxLength + 1;

  if (name_len !=
      _sr->read(name_len, name_len, reinterpret_cast<uint8_t *>(s->name))) {
    _err += "Failed to read section.name.\n";
    return false;
  }

  if (!_sr->read8(&s->start)) {
    _err += "Failed to read section.start.\n";
    return false;
  }

  if (size_t(s->start) > _sr->size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Section start offset exceeds USDC file size.");
  }

  if (!_sr->read8(&s->size)) {
    _err += "Failed to read section.size.\n";
    return false;
  }

  if (size_t(s->start + s->size) > _sr->size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Section end offset exceeds USDC file size.");
  }


  return true;
}

bool CrateReader::ReadTokens() {
  TINYUSDZ_PROFILE_SCOPE("crate-reader", "ReadTokens");
  
  // Report progress (20%)
  if (!ReportProgress(0.2f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  if ((_tokens_index < 0) || (_tokens_index >= int64_t(_toc.sections.size()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid index for `TOKENS` section.");
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Version must be 0.4.0 or later, but got {}.{}.{}",
      _version[0], _version[1], _version[2]));
  }

  const crate::Section &sec = _toc.sections[size_t(_tokens_index)];
  if (!_sr->seek_set(uint64_t(sec.start))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to move to `TOKENS` section.");
    return false;
  }

  if (sec.size < 4) {
     PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("`TOKENS` section data size is zero or too small."));
  }

  DCOUT("sec.start = " << sec.start);
  DCOUT("sec.size = " << sec.size);

  // # of tokens.
  uint64_t num_tokens;
  if (!_sr->read8(&num_tokens)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read # of tokens at `TOKENS` section.");
  }

  DCOUT("# of tokens = " << num_tokens);

  if (num_tokens == 0) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Empty tokens.");
  }

  if (num_tokens > _config.maxNumTokens) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many Tokens.");
  }

  // Tokens are lz4 compressed starting from version 0.4.0

  // Compressed token data.
  uint64_t uncompressedSize;
  if (!_sr->read8(&uncompressedSize)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read uncompressedSize at `TOKENS` section.");
  }

  DCOUT("uncompressedSize = " << uncompressedSize);


  // Must be larger than len(';-)') + all empty string case.
  // 3 = ';-)'
  // num_tokens = '\0' delimiter
  if ((3 + num_tokens) > uncompressedSize) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`TOKENS` section corrupted.");
  }

  // At least min size should be 16 both for compress and uncompress.
  if (uncompressedSize < 4) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "uncompressedSize too small or zero bytes.");
  }

  uint64_t compressedSize;
  if (!_sr->read8(&compressedSize)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read compressedSize at `TOKENS` section.");
  }

  DCOUT("compressedSize = " << compressedSize);

  if (compressedSize < 4) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "compressedSize is too small or zero bytes.");
  }

  if (compressedSize > _sr->size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed data size exceeds input file size.");
  }

  if (size_t(compressedSize) > size_t(sec.size)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed data size exceeds `TOKENS` section size.");
  }

  // To combat with heap-buffer flow in lz4 cuased by corrupted lz4 compressed data,
  // We allocate same size of uncompressedSize(or larger one),
  // And further, extra 128 bytes for safety(LZ4_FAST_DEC_LOOP does 16 bytes stride memcpy)

  uint64_t bufSize = (std::max)(compressedSize, uncompressedSize);
  CHECK_MEMORY_USAGE(bufSize+128);
  CHECK_MEMORY_USAGE(uncompressedSize);


  // dst
  std::vector<char> chars(static_cast<size_t>(uncompressedSize));
  memset(chars.data(), 0, chars.size());

  std::vector<char> compressed(static_cast<size_t>(bufSize + 128));
  memset(compressed.data(), 0, compressed.size());

  if (compressedSize !=
      _sr->read(size_t(compressedSize), size_t(compressedSize),
                reinterpret_cast<uint8_t *>(compressed.data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read compressed data at `TOKENS` section.");
    return false;
  }

  if (uncompressedSize !=
      LZ4Compression::DecompressFromBuffer(compressed.data(), chars.data(),
                                           size_t(compressedSize),
                                           size_t(uncompressedSize), &_err)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to decompress data of Tokens.");
  }

  // Split null terminated string into _tokens.
  const char *ps = chars.data();
  const char *pe = chars.data() + chars.size();
  const char *pcurr = ps;
  size_t nbytes_remain = size_t(chars.size());

  auto my_strnlen = [](const char *s, const size_t max_length) -> size_t {
    if (!s) return 0;

    size_t i = 0;
    for (; i < max_length; i++) {
      if (s[i] == '\0') {
        return i;
      }
    }

    // null character not found.
    return i;
  };

  // TODO(syoyo): Check if input string has exactly `n` tokens(`n` null
  // characters)
  for (size_t i = 0; i < num_tokens; i++) {
    DCOUT("n_remain = " << nbytes_remain);

    size_t len = my_strnlen(pcurr, nbytes_remain);
    DCOUT("len = " << len);

    if ((pcurr + (len+1)) > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    std::string str;
    if (len > 0) {
      str = std::string(pcurr, len);
    } else {
      // Empty string allowed
      str = std::string();
    }

    pcurr += len + 1;  // +1 = '\0'
    nbytes_remain = size_t(pe - pcurr);
    if (pcurr > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    value::token tok(str);
    CHECK_MEMORY_USAGE(sizeof(value::token) + str.size());

    DCOUT("token[" << i << "] = " << tok);
    _tokens.push_back(tok);

    if (nbytes_remain == 0) {
      // reached to the string buffer end.
      break;
    }
  }

  if (_tokens.size() != num_tokens) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("The number of tokens parsed {} does not match the requested one {}", _tokens.size(), num_tokens));
  }

  return true;
}

bool CrateReader::ReadStrings() {
  TINYUSDZ_PROFILE_SCOPE("crate-reader", "ReadStrings");
  
  // Report progress (30%)
  if (!ReportProgress(0.3f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  if ((_strings_index < 0) ||
      (_strings_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `STRINGS` section.\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_strings_index)];

  if (s.size == 0) {
    // empty `STRINGS`?
    return true;
  }

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `STRINGS` section.\n";
    return false;
  }

  // `STRINGS` are not compressed.
  if (!ReadIndices(&_string_indices)) {
    _err += "Failed to read StringIndex array.\n";
    return false;
  }

  for (size_t i = 0; i < _string_indices.size(); i++) {
    DCOUT("StringIndex[" << i << "] = " << _string_indices[i].value);
  }

  return true;
}

bool CrateReader::ReadFields() {
  // Report progress (40%)
  if (!ReportProgress(0.4f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  
  if ((_fields_index < 0) || (_fields_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `FIELDS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_fields_index)];

  if (s.size == 0) {
    // accepts Empty FIELDS size.
    return true;
  }

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDS` section.\n";
    return false;
  }

  uint64_t num_fields;
  if (!_sr->read8(&num_fields)) {
    _err += "Failed to read # of fields at `FIELDS` section.\n";
    return false;
  }

  DCOUT("num_fields = " << num_fields);

  if (num_fields == 0) {
    // Fields may be empty, so OK
    return true;
  }

  if (num_fields > _config.maxNumFields) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many fields in `FIELDS` section.");
  }

  if (sizeof(void *) == 4) {
    // 32bit
    if (num_fields > std::numeric_limits<int32_t>::max() / sizeof(uint32_t)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many fields in `FIELDS` section.");
    }
  }

  CHECK_MEMORY_USAGE(size_t(num_fields) * sizeof(Field));

  _fields.resize(static_cast<size_t>(num_fields));

  // indices
  {

    CHECK_MEMORY_USAGE(size_t(num_fields) * sizeof(uint32_t));

    std::vector<uint32_t> tmp;
    tmp.resize(size_t(num_fields));
    if (!ReadCompressedInts(tmp.data(), size_t(num_fields))) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Field token_index array.");
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].token_index.value = tmp[i];
    }

    REDUCE_MEMORY_USAGE(size_t(num_fields) * sizeof(uint32_t));

  }

  // Value reps(LZ4 compressed)
  {
    uint64_t reps_size; // compressed size
    if (!_sr->read8(&reps_size)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read value reps legnth at `FIELDS` section.");
    }

    if (reps_size > size_t(s.size)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid byte size of Value reps data.");
    }

    if (reps_size > _sr->size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Compressed Value reps size exceeds USDC data.");
    }

    CHECK_MEMORY_USAGE(size_t(reps_size));

    // TODO: Decompress from _sr directly.
    std::vector<char> comp_buffer(static_cast<size_t>(reps_size));

    if (reps_size !=
        _sr->read(size_t(reps_size), size_t(reps_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read reps data at `FIELDS` section.");
    }

    // reps datasize = LZ4 compressed. uncompressed size = num_fields * 8 bytes
    size_t uncompressed_size = size_t(num_fields) * sizeof(uint64_t);
    CHECK_MEMORY_USAGE(uncompressed_size);

    std::vector<uint64_t> reps_data;
    reps_data.resize(static_cast<size_t>(num_fields));


    if (uncompressed_size != LZ4Compression::DecompressFromBuffer(
                                 comp_buffer.data(),
                                 reinterpret_cast<char *>(reps_data.data()),
                                 size_t(reps_size), uncompressed_size, &_err)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Fields ValueRep data.");
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].value_rep = crate::ValueRep(reps_data[i]);
    }

    REDUCE_MEMORY_USAGE(uncompressed_size);
    REDUCE_MEMORY_USAGE(size_t(reps_size)); // comp_buffer
  }

  DCOUT("num_fields = " << num_fields);
  for (size_t i = 0; i < num_fields; i++) {
    if (auto tokv = GetToken(_fields[i].token_index)) {
      DCOUT("field[" << i << "] name = " << tokv.value()
                     << ", value = " << _fields[i].value_rep.GetStringRepr());
    }
  }

  return true;
}

bool CrateReader::ReadFieldSets() {
  // Report progress (50%)
  if (!ReportProgress(0.5f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  
  if ((_fieldsets_index < 0) ||
      (_fieldsets_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `FIELDSETS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_fieldsets_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDSETS` section.\n";
    return false;
  }

  uint64_t num_fieldsets;
  if (!_sr->read8(&num_fieldsets)) {
    _err += "Failed to read # of fieldsets at `FIELDSETS` section.\n";
    return false;
  }

  if (num_fieldsets == 0) {
    // At least 1 FieldIndex(separator(~0)) must exist.
    PUSH_ERROR("`FIELDSETS` is empty.");
    return false;
  }

  if (num_fieldsets > _config.maxNumFieldSets) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Too many FieldSets {}. maxNumFieldSets is set to {}", num_fieldsets, _config.maxNumFieldSets));
  }

  CHECK_MEMORY_USAGE(size_t(num_fieldsets) * sizeof(uint32_t));

  _fieldset_indices.resize(static_cast<size_t>(num_fieldsets));

  // Create temporary space for decompressing.
  size_t compBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_fieldsets));

  CHECK_MEMORY_USAGE(compBufferSize);

  std::vector<char> comp_buffer;
  comp_buffer.resize(compBufferSize);

  CHECK_MEMORY_USAGE(sizeof(uint32_t) * size_t(num_fieldsets));
  std::vector<uint32_t> tmp;
  tmp.resize(static_cast<size_t>(num_fieldsets));

  size_t workBufferSize = Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_fieldsets));

  CHECK_MEMORY_USAGE(workBufferSize);
  std::vector<char> working_space;
  working_space.resize(workBufferSize);

  uint64_t fsets_size;
  if (!_sr->read8(&fsets_size)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read fieldsets size at `FIELDSETS` section.");
  }

  DCOUT("num_fieldsets = " << num_fieldsets << ", fsets_size = " << fsets_size
                           << ", comp_buffer.size = " << comp_buffer.size());

  if (fsets_size > comp_buffer.size()) {
    // Maybe corrupted?
    fsets_size = comp_buffer.size();
  }

  if (fsets_size > _sr->size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "FieldSets compressed data exceeds USDC data.");
  }

  if (fsets_size !=
      _sr->read(size_t(fsets_size), size_t(fsets_size),
                reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read fieldsets data at `FIELDSETS` section.");
  }

  std::string err;
  Usd_IntegerCompression::DecompressFromBuffer(
      comp_buffer.data(), size_t(fsets_size), tmp.data(), size_t(num_fieldsets),
      &err, working_space.data());

  if (!err.empty()) {
    _err += err;
    return false;
  }

  for (size_t i = 0; i != num_fieldsets; ++i) {
    DCOUT("fieldset_index[" << i << "] = " << tmp[i]);
    _fieldset_indices[i].value = tmp[i];
  }

  REDUCE_MEMORY_USAGE(workBufferSize);
  REDUCE_MEMORY_USAGE(compBufferSize);

  return true;
}

bool CrateReader::BuildLiveFieldSets() {
  // Report progress (80%)
  if (!ReportProgress(0.8f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  
  for (auto fsBegin = _fieldset_indices.begin(),
            fsEnd = std::find(fsBegin, _fieldset_indices.end(), crate::Index());
       fsBegin != _fieldset_indices.end();
       fsBegin = fsEnd + 1, fsEnd = std::find(fsBegin, _fieldset_indices.end(),
                                              crate::Index())) {
    auto &pairs = _live_fieldsets[crate::Index(
        uint32_t(fsBegin - _fieldset_indices.begin()))];

    pairs.resize(size_t(fsEnd - fsBegin));
    DCOUT("range size = " << (fsEnd - fsBegin));
    // TODO(syoyo): Parallelize.
    for (size_t i = 0; fsBegin != fsEnd; ++fsBegin, ++i) {
      if (fsBegin->value < _fields.size()) {
        // ok
      } else {
        PUSH_ERROR("Invalid live field set data.");
        return false;
      }

      DCOUT("fieldIndex = " << (fsBegin->value));
      auto const &field = _fields[fsBegin->value];
      if (auto tokv = GetToken(field.token_index)) {
        pairs[i].first = tokv.value().str();

        if (!UnpackValueRep(field.value_rep, &pairs[i].second)) {
          PUSH_ERROR("BuildLiveFieldSets: Failed to unpack ValueRep : "
                     << field.value_rep.GetStringRepr());
          return false;
        }
      } else {
        PUSH_ERROR("Invalid token index.");
      }
    }
  }

  DCOUT("# of live fieldsets = " << _live_fieldsets.size());

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  size_t sum = 0;
  for (const auto &item : _live_fieldsets) {
    DCOUT("livefieldsets[" << item.first.value
                           << "].count = " << item.second.size());
    sum += item.second.size();

    for (size_t i = 0; i < item.second.size(); i++) {
      DCOUT(" [" << i << "] name = " << item.second[i].first);
    }
  }
  DCOUT("Total fields used = " << sum);
#endif

  return true;
}

bool CrateReader::ReadSpecs() {
  // Report progress (60%)
  if (!ReportProgress(0.6f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  
  if ((_specs_index < 0) || (_specs_index >= int64_t(_toc.sections.size()))) {
    PUSH_ERROR("Invalid index for `SPECS` section.");
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    PUSH_ERROR("Version must be 0.4.0 or later, but got " +
               std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
               "." + std::to_string(_version[2]));
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_specs_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    PUSH_ERROR("Failed to move to `SPECS` section.");
    return false;
  }

  uint64_t num_specs;
  if (!_sr->read8(&num_specs)) {
    PUSH_ERROR("Failed to read # of specs size at `SPECS` section.");
    return false;
  }

  if (num_specs > _config.maxNumSpecifiers) {
    PUSH_ERROR("Too many specs in `SPECS` section.");
    return false;
  }

  if (num_specs == 0) {
    // At least 1 Spec(Root Prim '/') must exist.
    PUSH_ERROR("`SPECS` is empty.");
    return false;
  }

  DCOUT("num_specs " << num_specs);

  CHECK_MEMORY_USAGE(size_t(num_specs) * sizeof(Spec));

  _specs.resize(static_cast<size_t>(num_specs));

  // TODO: Memory size check

  // Create temporary space for decompressing.
  size_t compBufferSize= Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_specs));

  CHECK_MEMORY_USAGE(compBufferSize);

  std::vector<char> comp_buffer;
  comp_buffer.resize(compBufferSize);

  CHECK_MEMORY_USAGE(size_t(num_specs) * sizeof(uint32_t)); // tmp

  std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));

  size_t workBufferSize= Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_specs));

  CHECK_MEMORY_USAGE(workBufferSize);
  std::vector<char> working_space;
  working_space.resize(workBufferSize);

  // path indices
  {
    uint64_t path_indexes_size;
    if (!_sr->read8(&path_indexes_size)) {
      PUSH_ERROR("Failed to read path indexes size at `SPECS` section.");
      return false;
    }

    if (path_indexes_size > comp_buffer.size()) {
      // Maybe corrupted?
      path_indexes_size = comp_buffer.size();
    }

    if (path_indexes_size !=
        _sr->read(size_t(path_indexes_size), size_t(path_indexes_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR("Failed to read path indexes data at `SPECS` section.");
      return false;
    }

    std::string err;  // not used
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(path_indexes_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      PUSH_ERROR("Failed to decode pathIndexes at `SPECS` section.");
      return false;
    }

    for (size_t i = 0; i < num_specs; ++i) {
      DCOUT("spec[" << i << "].path_index = " << tmp[i]);
      _specs[i].path_index.value = tmp[i];
    }
  }

  // fieldset indices
  {
    uint64_t fset_indexes_size;
    if (!_sr->read8(&fset_indexes_size)) {
      PUSH_ERROR("Failed to read fieldset indexes size at `SPECS` section.");
      return false;
    }

    if (fset_indexes_size > comp_buffer.size()) {
      // Maybe corrupted?
      fset_indexes_size = comp_buffer.size();
    }

    if (fset_indexes_size !=
        _sr->read(size_t(fset_indexes_size), size_t(fset_indexes_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR("Failed to read fieldset indexes data at `SPECS` section.");
      return false;
    }

    std::string err;  // not used
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(fset_indexes_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      PUSH_ERROR("Failed to decode fieldset indices at `SPECS` section.");
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      DCOUT("specs[" << i << "].fieldset_index = " << tmp[i]);
      _specs[i].fieldset_index.value = tmp[i];
    }
  }

  // spec types
  {
    uint64_t spectype_size;
    if (!_sr->read8(&spectype_size)) {
      PUSH_ERROR("Failed to read spectype size at `SPECS` section.");
      return false;
    }

    if (spectype_size > comp_buffer.size()) {
      // Maybe corrupted?
      spectype_size = comp_buffer.size();
    }

    if (spectype_size !=
        _sr->read(size_t(spectype_size), size_t(spectype_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR("Failed to read spectype data at `SPECS` section.");
      return false;
    }

    std::string err;  // not used.
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(spectype_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      PUSH_ERROR("Failed to decode fieldset indices at `SPECS` section.\n");
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      // std::cout << "spectype = " << tmp[i] << "\n";
      _specs[i].spec_type = static_cast<SpecType>(tmp[i]);
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i != num_specs; ++i) {
    DCOUT("spec[" << i << "].pathIndex  = " << _specs[i].path_index.value
                  << ", fieldset_index = " << _specs[i].fieldset_index.value
                  << ", spec_type = "
                  << tinyusdz::to_string(_specs[i].spec_type));
    if (auto specstr = GetSpecString(crate::Index(uint32_t(i)))) {
      DCOUT("spec[" << i << "] string_repr = " << specstr.value());
    }
  }
#endif

  REDUCE_MEMORY_USAGE(compBufferSize);
  REDUCE_MEMORY_USAGE(workBufferSize);
  REDUCE_MEMORY_USAGE(size_t(num_specs) * sizeof(uint32_t)); // tmp

  return true;
}

bool CrateReader::ReadPaths() {
  TINYUSDZ_PROFILE_SCOPE("crate-reader", "ReadPaths");
  
  // Report progress (70%)
  if (!ReportProgress(0.7f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  if ((_paths_index < 0) || (_paths_index >= int64_t(_toc.sections.size()))) {
    PUSH_ERROR("Invalid index for `PATHS` section.");
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    PUSH_ERROR("Version must be 0.4.0 or later, but got " +
               std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
               "." + std::to_string(_version[2]));
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_paths_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    PUSH_ERROR("Failed to move to `PATHS` section.");
    return false;
  }

  uint64_t num_paths;
  if (!_sr->read8(&num_paths)) {
    PUSH_ERROR("Failed to read # of paths at `PATHS` section.");
    return false;
  }

  if (num_paths == 0) {
    // At least root path exits.
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`PATHS` is empty.");
  }

  if (num_paths > _config.maxNumPaths) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many Paths in `PATHS` section.");
  }

  CHECK_MEMORY_USAGE(size_t(num_paths) * sizeof(Path)); // conservative estimation
  CHECK_MEMORY_USAGE(size_t(num_paths) * sizeof(Path)); // conservative estimation
  CHECK_MEMORY_USAGE(size_t(num_paths) * sizeof(Node)); // conservative estimation

  _paths.resize(static_cast<size_t>(num_paths));
  _elemPaths.resize(static_cast<size_t>(num_paths));
  _nodes.resize(static_cast<size_t>(num_paths));

  if (!ReadCompressedPaths(num_paths)) {
    PUSH_ERROR("Failed to read compressed paths.");
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  DCOUT("# of paths " << _paths.size());

  for (size_t i = 0; i < _paths.size(); i++) {
    DCOUT("path[" << i << "] = " << _paths[i].full_path_name());
  }
#endif

  return true;
}

bool CrateReader::ReadBootStrap() {
  TINYUSDZ_PROFILE_FUNCTION("crate-reader");
  
  // Report initial progress
  if (!ReportProgress(0.0f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }
  
  // parse header.
  uint8_t magic[8];
  if (8 != _sr->read(/* req */ 8, /* dst len */ 8, magic)) {
    PUSH_ERROR("Failed to read magic number.");
    return false;
  }

  if (memcmp(magic, "PXR-USDC", 8)) {
    PUSH_ERROR("Invalid magic number. Expected 'PXR-USDC' but got '" +
               std::string(magic, magic + 8) + "'");
    return false;
  }

  // parse version(first 3 bytes from 8 bytes)
  uint8_t version[8];
  if (8 != _sr->read(8, 8, version)) {
    PUSH_ERROR("Failed to read magic number.");
    return false;
  }

  DCOUT("version = " << int(version[0]) << "." << int(version[1]) << "."
                     << int(version[2]));

  _version[0] = version[0];
  _version[1] = version[1];
  _version[2] = version[2];

  // We only support version 0.4.0 or later.
  if ((version[0] == 0) && (version[1] < 4)) {
    PUSH_ERROR("Version must be 0.4.0 or later, but got " +
               std::to_string(version[0]) + "." + std::to_string(version[1]) +
               "." + std::to_string(version[2]));
    return false;
  }

  // Currently up to 0.9.0
  if ((version[0] == 0) && (version[1] < 10)) {
    // ok
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Unsupported version {}.{}.{}. TinyUSDZ supports version up to 0.9.0",
      _version[0], _version[1], _version[2]));
  }

  _toc_offset = 0;
  if (!_sr->read8(&_toc_offset)) {
    PUSH_ERROR("Failed to read TOC offset.");
    return false;
  }

  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    PUSH_ERROR("Invalid TOC offset value: " + std::to_string(_toc_offset) +
               ", filesize = " + std::to_string(_sr->size()) + ".");
    return false;
  }

  DCOUT("toc offset = " << _toc_offset);

  return true;
}

bool CrateReader::ReadTOC() {
  TINYUSDZ_PROFILE_FUNCTION("crate-reader");
  
  // Report progress (10% after bootstrap)
  if (!ReportProgress(0.1f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }

  DCOUT(fmt::format("Memory budget: {} bytes", _config.maxMemoryBudget));

  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    PUSH_ERROR("Invalid toc offset.");
    return false;
  }

  if (!_sr->seek_set(uint64_t(_toc_offset))) {
    PUSH_ERROR("Failed to move to TOC offset.");
    return false;
  }

  // read # of sections.
  uint64_t num_sections{0};
  if (!_sr->read8(&num_sections)) {
    PUSH_ERROR("Failed to read TOC(# of sections).");
    return false;
  }
  if (num_sections >= _config.maxTOCSections) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of sections {} are too large. maxTOCSections is set to {}", num_sections, _config.maxTOCSections));
  }

  DCOUT("toc sections = " << num_sections);

  _toc.sections.resize(static_cast<size_t>(num_sections));

  CHECK_MEMORY_USAGE(num_sections * sizeof(Section));

  for (size_t i = 0; i < num_sections; i++) {
    if (!ReadSection(&_toc.sections[i])) {
      PUSH_ERROR("Failed to read TOC section at " + std::to_string(i));
      return false;
    }
    DCOUT("section[" << i << "] name = " << _toc.sections[i].name
                     << ", start = " << _toc.sections[i].start
                     << ", size = " << _toc.sections[i].size);

    if (_toc.sections[i].start < 0) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid section start byte offset."));
    }

    if (_toc.sections[i].size <= 0) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid or empty section size."));
    }

    if (size_t(_toc.sections[i].size) > _sr->size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Section size exceeds input USDC data size."));
    }

    if (size_t(_toc.sections[i].start) > _sr->size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Section start byte offset exceeds input USDC data size."));
    }

    // TODO: handle integer overflow.
    size_t end_offset = size_t(_toc.sections[i].start + _toc.sections[i].size);
    if (sizeof(void *) == 4) { // 32bit
      if (end_offset > size_t(std::numeric_limits<int32_t>::max())) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Section end offset exceeds 32bit max."));
      }
    }
    if (end_offset > _sr->size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Section byte offset + size exceeds input USDC data size."));
    }


    if (0 == strncmp(_toc.sections[i].name, "TOKENS",
                     crate::kSectionNameMaxLength)) {
      _tokens_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "STRINGS",
                            crate::kSectionNameMaxLength)) {
      _strings_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDS",
                            crate::kSectionNameMaxLength)) {
      _fields_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDSETS",
                            crate::kSectionNameMaxLength)) {
      _fieldsets_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "SPECS",
                            crate::kSectionNameMaxLength)) {
      _specs_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "PATHS",
                            crate::kSectionNameMaxLength)) {
      _paths_index = int64_t(i);
    }
  }

  DCOUT("TOC read success");
  return true;
}

///
/// Find if a field with (`name`, `tyname`) exists in FieldValuePairVector.
///
bool CrateReader::HasFieldValuePair(const FieldValuePairVector &fvs,
                                    const std::string &name,
                                    const std::string &tyname) {
  for (const auto &fv : fvs) {
    if ((fv.first == name) && (fv.second.type_name() == tyname)) {
      return true;
    }
  }

  return false;
}

///
/// Find if a field with `name`(type can be arbitrary) exists in
/// FieldValuePairVector.
///
bool CrateReader::HasFieldValuePair(const FieldValuePairVector &fvs,
                                    const std::string &name) {
  for (const auto &fv : fvs) {
    if (fv.first == name) {
      return true;
    }
  }

  return false;
}

nonstd::expected<FieldValuePair, std::string>
CrateReader::GetFieldValuePair(const FieldValuePairVector &fvs,
                               const std::string &name,
                               const std::string &tyname) {
  for (const auto &fv : fvs) {
    if ((fv.first == name) && (fv.second.type_name() == tyname)) {
      return fv;
    }
  }

  return nonstd::make_unexpected("FieldValuePair not found with name: `" +
                                 name + "` and specified type: `" + tyname +
                                 "`");
}

nonstd::expected<FieldValuePair, std::string>
CrateReader::GetFieldValuePair(const FieldValuePairVector &fvs,
                               const std::string &name) {
  for (const auto &fv : fvs) {
    if (fv.first == name) {
      return fv;
    }
  }

  return nonstd::make_unexpected("FieldValuePair not found with name: `" +
                                 name + "`");
}


}  // namespace crate
}  // namespace tinyusdz
