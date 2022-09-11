// SPDX-License-Identifier: MIT
// Copyright 2022-Present Syoyo Fujita.
//
// Crate(binary format) reader
//
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

#include <unordered_set>

#include "crate-format.hh"
#include "crate-pprint.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "path-util.hh"
#include "pprinter.hh"
#include "prim-types.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"

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

namespace {

template <class Int>
static inline bool ReadCompressedInts(const StreamReader *sr, Int *out,
                                      size_t size) {
  // TODO(syoyo): Error check
  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;
  std::vector<char> compBuffer(Compressor::GetCompressedBufferSize(size));
  uint64_t compSize;
  if (!sr->read8(&compSize)) {
    return false;
  }

  if (!sr->read(size_t(compSize), size_t(compSize),
                reinterpret_cast<uint8_t *>(compBuffer.data()))) {
    return false;
  }
  std::string err;
  bool ret = Compressor::DecompressFromBuffer(
      compBuffer.data(), size_t(compSize), out, size, &err);
  (void)err;

  return ret;
}

static inline bool ReadIndices(const StreamReader *sr,
                               std::vector<crate::Index> *indices) {
  // TODO(syoyo): Error check
  uint64_t n;
  if (!sr->read8(&n)) {
    return false;
  }

  DCOUT("ReadIndices: n = " << n);

  indices->resize(size_t(n));
  size_t datalen = size_t(n) * sizeof(crate::Index);

  if (datalen != sr->read(datalen, datalen,
                          reinterpret_cast<uint8_t *>(indices->data()))) {
    return false;
  }

  return true;
}

} // namespace

//
// --
//
CrateReader::CrateReader(StreamReader *sr, const CrateReaderConfig &config) : _sr(sr) {
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

}

CrateReader::~CrateReader() {}

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
  if (index.value <= _fields.size()) {
    return _fields[index.value];
  } else {
    return nonstd::nullopt;
  }
}

const nonstd::optional<value::token> CrateReader::GetToken(
    crate::Index token_index) const {
  if (token_index.value <= _tokens.size()) {
    return _tokens[token_index.value];
  } else {
    return nonstd::nullopt;
  }
}

// Get string token from string index.
const nonstd::optional<value::token> CrateReader::GetStringToken(
    crate::Index string_index) const {
  if (string_index.value <= _string_indices.size()) {
    crate::Index s_idx = _string_indices[string_index.value];
    return GetToken(s_idx);
  } else {
    PUSH_ERROR("String index out of range: " +
               std::to_string(string_index.value));
    return value::token();
  }
}

nonstd::optional<Path> CrateReader::GetPath(crate::Index index) const {
  if (index.value <= _paths.size()) {
    // ok
  } else {
    return nonstd::nullopt;
  }

  return _paths[index.value];
}

nonstd::optional<Path> CrateReader::GetElementPath(crate::Index index) const {
  if (index.value <= _elemPaths.size()) {
    // ok
  } else {
    return nonstd::nullopt;
  }

  return _elemPaths[index.value];
}

nonstd::optional<std::string> CrateReader::GetPathString(
    crate::Index index) const {
  if (index.value <= _paths.size()) {
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
  (*i) = crate::Index(value);
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
    return true;
  }

  PUSH_ERROR("Invalid StringIndex.");
  return false;
}

nonstd::optional<std::string> CrateReader::GetSpecString(
    crate::Index index) const {
  if (index.value <= _specs.size()) {
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

  DCOUT("ValueRep value = " << rep->GetData());

  return true;
}

template <typename T>
bool CrateReader::ReadIntArray(bool is_compressed, std::vector<T> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        // PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        //_err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(T) * length, sizeof(T) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      //_err += "Failed to read integer array data.\n";
      return false;
    }

    return true;

  } else {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        //_err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        //_err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    DCOUT("array.len = " << length);

    d->resize(length);

    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(T) * length;
      // Not stored in compressed.
      // reader.ReadContiguous(odata, osize);
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        PUSH_ERROR("Failed to read uncompressed array data.");
        return false;
      }
      return true;
    }

    return ReadCompressedInts(_sr, d->data(), d->size());
  }
}

bool CrateReader::ReadHalfArray(bool is_compressed,
                                std::vector<value::half> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
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

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(uint16_t) * length, sizeof(uint16_t) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read half array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
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

  DCOUT("array.len = " << length);

  d->resize(length);

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
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadHalfArray.\n";
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      float f = float(ints[i]);
      value::half h = float_to_half_full(f);
      (*d)[i] = h;
    }
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadHalfArray.\n";
      return false;
    }

    std::vector<value::half> lut(lutSize);
    if (!_sr->read(sizeof(value::half) * lutSize, sizeof(value::half) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadHalfArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
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

bool CrateReader::ReadFloatArray(bool is_compressed, std::vector<float> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
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

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(float) * length, sizeof(float) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read float array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
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

  DCOUT("array.len = " << length);

  d->resize(length);

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
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadFloatArray.\n";
      return false;
    }
    std::copy(ints.begin(), ints.end(), d->data());
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadFloatArray.\n";
      return false;
    }

    std::vector<float> lut(lutSize);
    if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadFloatArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
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

bool CrateReader::ReadDoubleArray(bool is_compressed, std::vector<double> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
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

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read double array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
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

  DCOUT("array.len = " << length);

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
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
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

    std::vector<double> lut(lutSize);
    if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadDoubleArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
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
  size_t values_offset = _sr->tell();

  crate::CrateValue times_value;
  if (!UnpackValueRep(times_rep, &times_value)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of TimeSample's `times` element.");
  }

  // must be an array of double.
  DCOUT("TimeSample times:" << times_value.type_name());

  if (auto pv = times_value.get_value<std::vector<double>>()) {
    d->times = pv.value();
    DCOUT("`times` = " << d->times);
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

  if (d->times.size() != num_values) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "# of `times` elements and # of values in Crate differs.");
  }

  for (size_t i = 0; i < num_values; i++) {

    crate::ValueRep rep;
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read ValueRep for TimeSample' value element.");
    }

    size_t next_vrep_loc = _sr->tell();

    ///
    /// Type check of the content of `value` will be done at ReconstructPrim() in usdc-reader.cc.
    ///
    crate::CrateValue value;
    if (!UnpackValueRep(rep, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of TimeSample's value element.");
    }

    d->values.emplace_back(std::move(value.get_raw()));

    // UnpackValueRep() will change StreamReader's read position.
    // Revert to next ValueRep location here.
    _sr->seek_set(next_vrep_loc);
  }

  // Move to next location.
  // sizeof(uint64) = sizeof(ValueRep)
  if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek over TimeSamples's values.");
  }


  return true;
}

bool CrateReader::ReadStringArray(std::vector<std::string> *d) {
  // array data is not compressed
  auto ReadFn = [this](std::vector<std::string> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      PUSH_ERROR("Failed to read # of elements.");
      return false;
    }

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      PUSH_ERROR("Failed to read STRING_VECTOR data.");
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      if (auto v = GetStringToken(ivalue[i])) {
        result[i] = v.value().str();
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

bool CrateReader::ReadPathArray(std::vector<Path> *d) {
  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

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

bool CrateReader::ReadPathListOp(ListOp<Path> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    PUSH_ERROR("Failed to read ListOpHeader.");
    return false;
  }

  if (h.IsExplicit()) {
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      PUSH_ERROR("Failed to read # of elements in ListOp.");
      return false;
    }

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

    size_t saved_position = _sr->tell();

    crate::CrateValue value;
    if (!UnpackValueRep(rep, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of Dictionary element.");
    }

    if (dict.count(key)) {
      // Duplicated key. maybe ok?
    }
    // CrateValue -> MetaVariable
    MetaVariable var;

    var.Set(value.get_raw());
    var.type = value.type_name();
    var.name = key;
    //var.custom = TODO

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
      // AssetPath = std::string(storage format is TokenIndex).
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
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::half3 v;
      v[0] = float_to_half_full(float(data[0]));
      v[1] = float_to_half_full(float(data[1]));
      v[2] = float_to_half_full(float(data[2]));

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
      v[0] = float_to_half_full(float(data[0]));
      v[1] = float_to_half_full(float(data[0]));
      v[2] = float_to_half_full(float(data[0]));
      v[3] = float_to_half_full(float(data[0]));

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
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
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
      DCOUT("dtype_id = " << to_string(uint32_t(dty.dtype_id)));
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        TODO_IMPLEMENT(dty)
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        // AssetPath = std::string(storage format is TokenIndex).
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n < _config.maxAssetPathElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of AssetPaths too large. TinyUSDZ limites it up to {}", _config.maxAssetPathElements));
        }

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<value::AssetPath> apaths(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto tokv = GetToken(v[i])) {
            DCOUT("Token[" << i << "] = " << tokv.value());
            apaths[i] = value::AssetPath(tokv.value().str());
          } else {
            return false;
          }
        }

        value->Set(apaths);
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<crate::Index> v(static_cast<size_t>(n));
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
        value->Set(stringArray);

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
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int array.");
          return false;
        }

        DCOUT("IntArray = " << v);

        value->Set(v);
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<uint32_t> v;
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint array.");
          return false;
        }

        DCOUT("UIntArray = " << v);

        value->Set(v);
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      if (rep.IsArray()) {
        std::vector<int64_t> v;
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int64 array.");
          return false;
        }

        DCOUT("Int64Array = " << v);

        value->Set(v);
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

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
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint64 array.");
          return false;
        }

        DCOUT("UInt64Array = " << v);

        value->Set(v);
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

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
        if (!ReadHalfArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read half array value.");
          return false;
        }

        value->Set(v);

        return true;
      } else {
        PUSH_ERROR("Non-inlined, non-array Half value is invalid.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      if (rep.IsArray()) {
        std::vector<float> v;
        if (!ReadFloatArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read float array value.");
          return false;
        }

        DCOUT("FloatArray = " << v);

        value->Set(v);

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        PUSH_ERROR("Non-inlined, non-array Float value is not supported.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      if (rep.IsArray()) {
        std::vector<double> v;
        if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("DoubleArray = " << v);
        value->Set(v);

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        double v;
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::matrix2d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix2d),
                       size_t(n) * sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix2d array.");
          return false;
        }

        value->Set(v);

      } else {
        static_assert(sizeof(value::matrix2d) == (8 * 4), "");

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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::matrix3d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix3d),
                       size_t(n) * sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix3d array.");
          return false;
        }

        value->Set(v);

      } else {
        static_assert(sizeof(value::matrix3d) == (8 * 9), "");

        value::matrix4d v;
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::matrix4d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix4d),
                       size_t(n) * sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix4d array.");
          return false;
        }

        value->Set(v);

      } else {
        static_assert(sizeof(value::matrix4d) == (8 * 16), "");

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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::quatd> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatd),
                       size_t(n) * sizeof(value::quatd),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(v);

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::quatf> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatf),
                       size_t(n) * sizeof(value::quatf),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(v);

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::quath> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quath),
                       size_t(n) * sizeof(value::quath),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quath array.");
          return false;
        }

        DCOUT("Quath[] = " << v);

        value->Set(v);

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::double2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double2),
                       size_t(n) * sizeof(value::double2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double2 array.");
          return false;
        }

        DCOUT("double2 = " << v);

        value->Set(v);
        return true;
      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::float2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float2),
                       size_t(n) * sizeof(value::float2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float2 array.");
          return false;
        }

        DCOUT("float2 = " << v);

        value->Set(v);
        return true;
      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::half2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half2),
                       size_t(n) * sizeof(value::half2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half2 array.");
          return false;
        }

        DCOUT("half2 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::int2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int2),
                       size_t(n) * sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int2 array.");
          return false;
        }

        DCOUT("int2 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::double3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double3),
                       size_t(n) * sizeof(value::double3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double3 array.");
          return false;
        }

        DCOUT("double3 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::float3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float3),
                       size_t(n) * sizeof(value::float3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float3 array.");
          return false;
        }

        DCOUT("float3f = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::half3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half3),
                       size_t(n) * sizeof(value::half3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half3 array.");
          return false;
        }

        DCOUT("half3 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::int3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int3),
                       size_t(n) * sizeof(value::int3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int3 array.");
          return false;
        }

        DCOUT("int3 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::double4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double4),
                       size_t(n) * sizeof(value::double4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double4 array.");
          return false;
        }

        DCOUT("double4 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::float4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float4),
                       size_t(n) * sizeof(value::float4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float4 array.");
          return false;
        }

        DCOUT("float4 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::half4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half4),
                       size_t(n) * sizeof(value::half4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half4 array.");
          return false;
        }

        DCOUT("half4 = " << v);
        value->Set(v);

      } else {
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
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::int4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int4),
                       size_t(n) * sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int4 array.");
          return false;
        }

        DCOUT("int4 = " << v);
        value->Set(v);

      } else {
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
      uint64_t n;
      if (!_sr->read8(&n)) {
        PUSH_ERROR("Failed to read TokenVector value.");
        return false;
      }

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
      if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
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
        _err += "Failed to read StringVector value\n";
        return false;
      }

      DCOUT("StringArray = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Inlined value: " +
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

bool CrateReader::BuildDecompressedPathsImpl(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath) {
  bool hasChild = false, hasSibling = false;
  do {
    auto thisIndex = curIndex++;
    if (parentPath.IsEmpty()) {
      // root node.
      // Assume single root node in the scene.
      DCOUT("paths[" << pathIndexes[thisIndex]
                     << "] is parent. name = " << parentPath.full_path_name());
      parentPath = Path::RootPath();
      _paths[pathIndexes[thisIndex]] = parentPath;
    } else {
      int32_t tokenIndex = elementTokenIndexes[thisIndex];
      bool isPrimPropertyPath = tokenIndex < 0;
      tokenIndex = std::abs(tokenIndex);

      DCOUT("tokenIndex = " << tokenIndex);
      if (tokenIndex >= int32_t(_tokens.size())) {
        PUSH_ERROR("Invalid tokenIndex in BuildDecompressedPathsImpl.");
        return false;
      }
      auto const &elemToken = _tokens[size_t(tokenIndex)];
      DCOUT("elemToken = " << elemToken);
      DCOUT("[" << pathIndexes[thisIndex] << "].append = " << elemToken);

      // full path
      _paths[pathIndexes[thisIndex]] =
          isPrimPropertyPath ? parentPath.AppendProperty(elemToken.str())
                             : parentPath.AppendElement(elemToken.str());

      // also set leaf path for 'primChildren' check
      _elemPaths[pathIndexes[thisIndex]] = Path(elemToken.str(), "");
      //_paths[pathIndexes[thisIndex]].SetLocalPart(elemToken.str());
    }

    // If we have either a child or a sibling but not both, then just
    // continue to the neighbor.  If we have both then spawn a task for the
    // sibling and do the child ourself.  We think that our path trees tend
    // to be broader more often than deep.

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        // NOTE(syoyo): This recursive call can be parallelized
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps,
                                        siblingIndex, parentPath)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent path.
      parentPath = _paths[pathIndexes[thisIndex]];
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}

// TODO(syoyo): Refactor
bool CrateReader::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex,
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
      assert(thisIndex == 0);

      Node root(parentNodeIndex, _paths[pathIndexes[thisIndex]]);

      _nodes[pathIndexes[thisIndex]] = root;

      parentNodeIndex = int64_t(thisIndex);

    } else {
      if (parentNodeIndex >= int64_t(_nodes.size())) {
        return false;
      }

      DCOUT("Hierarchy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                                 << "].add_child = " << pathIndexes[thisIndex]);

      Node node(parentNodeIndex, _paths[pathIndexes[thisIndex]]);

      assert(_nodes[size_t(pathIndexes[thisIndex])].GetParent() == -2);

      _nodes[size_t(pathIndexes[thisIndex])] = node;

      //std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
      std::string name = _elemPaths[pathIndexes[thisIndex]].full_path_name();
      DCOUT("childName = " << name);
      _nodes[size_t(pathIndexes[size_t(parentNodeIndex)])].AddChildren(
          name, pathIndexes[thisIndex]);
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps,
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

bool CrateReader::ReadCompressedPaths(const uint64_t ref_num_paths) {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numPaths;
  if (!_sr->read8(&numPaths)) {
    _err += "Failed to read the number of paths.\n";
    return false;
  }

  if (ref_num_paths != numPaths) {
    _err += "Size mismatch of numPaths at `PATHS` section.\n";
    return false;
  }

  DCOUT("numPaths : " << numPaths);

  pathIndexes.resize(static_cast<size_t>(numPaths));
  elementTokenIndexes.resize(static_cast<size_t>(numPaths));
  jumps.resize(static_cast<size_t>(numPaths));

  // Create temporary space for decompressing.
  std::vector<char> compBuffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(numPaths)));
  std::vector<char> workingSpace(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(numPaths)));

  // pathIndexes.
  {
    uint64_t pathIndexesSize;
    if (!_sr->read8(&pathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (pathIndexesSize !=
        _sr->read(size_t(pathIndexesSize), size_t(pathIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read pathIndexes data.\n";
      return false;
    }

    DCOUT("comBuffer.size = " << compBuffer.size());
    DCOUT("pathIndexesSize = " << pathIndexesSize);

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(pathIndexesSize), pathIndexes.data(),
        size_t(numPaths), &err, workingSpace.data());
    if (!err.empty()) {
      _err += "Failed to decode pathIndexes\n" + err;
      return false;
    }
  }

  // elementTokenIndexes.
  {
    uint64_t elementTokenIndexesSize;
    if (!_sr->read8(&elementTokenIndexesSize)) {
      _err += "Failed to read elementTokenIndexesSize.\n";
      return false;
    }

    if (elementTokenIndexesSize !=
        _sr->read(size_t(elementTokenIndexesSize),
                  size_t(elementTokenIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read elementTokenIndexes data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(elementTokenIndexesSize),
        elementTokenIndexes.data(), size_t(numPaths), &err,
        workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode elementTokenIndexes.");
      return false;
    }
  }

  // jumps.
  {
    uint64_t jumpsSize;
    if (!_sr->read8(&jumpsSize)) {
      PUSH_ERROR("Failed to read jumpsSize.");
      return false;
    }

    if (jumpsSize !=
        _sr->read(size_t(jumpsSize), size_t(jumpsSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read jumps data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(jumpsSize), jumps.data(), size_t(numPaths),
        &err, workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode jumps.");
      return false;
    }
  }

  _paths.resize(static_cast<size_t>(numPaths));
  _elemPaths.resize(static_cast<size_t>(numPaths));

  _nodes.resize(static_cast<size_t>(numPaths));

  // Now build the paths.
  if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps,
                                  /* curIndex */ 0, Path())) {
    return false;
  }

  // Now build node hierarchy.
  if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps,
                          /* curIndex */ 0, /* parent node index */ -1)) {
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < pathIndexes.size(); i++) {
    std::cout << "pathIndexes[" << i << "] = " << pathIndexes[i] << "\n";
  }

  for (auto item : elementTokenIndexes) {
    std::cout << "elementTokenIndexes " << item << "\n";
  }

  for (auto item : jumps) {
    std::cout << "jumps " << item << "\n";
  }
#endif

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

  if (!_sr->read8(&s->size)) {
    _err += "Failed to read section.size.\n";
    return false;
  }

  return true;
}

bool CrateReader::ReadTokens() {
  if ((_tokens_index < 0) || (_tokens_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `TOKENS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const crate::Section &sec = _toc.sections[size_t(_tokens_index)];
  if (!_sr->seek_set(uint64_t(sec.start))) {
    _err += "Failed to move to `TOKENS` section.\n";
    return false;
  }

  DCOUT("sec.start = " << sec.start);

  // # of tokens.
  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read # of tokens at `TOKENS` section.\n";
    return false;
  }

  // Tokens are lz4 compressed starting from version 0.4.0

  // Compressed token data.
  uint64_t uncompressedSize;
  if (!_sr->read8(&uncompressedSize)) {
    _err += "Failed to read uncompressedSize at `TOKENS` section.\n";
    return false;
  }

  uint64_t compressedSize;
  if (!_sr->read8(&compressedSize)) {
    _err += "Failed to read compressedSize at `TOKENS` section.\n";
    return false;
  }

  DCOUT("# of tokens = " << n << ", uncompressedSize = " << uncompressedSize
                         << ", compressedSize = " << compressedSize);

  std::vector<char> chars(static_cast<size_t>(uncompressedSize));
  std::vector<char> compressed(static_cast<size_t>(compressedSize));

  if (compressedSize !=
      _sr->read(size_t(compressedSize), size_t(compressedSize),
                reinterpret_cast<uint8_t *>(compressed.data()))) {
    _err += "Failed to read compressed data at `TOKENS` section.\n";
    return false;
  }

  if (uncompressedSize !=
      LZ4Compression::DecompressFromBuffer(compressed.data(), chars.data(),
                                           size_t(compressedSize),
                                           size_t(uncompressedSize), &_err)) {
    _err += "Failed to decompress data of Tokens.\n";
    return false;
  }

  // Split null terminated string into _tokens.
  const char *ps = chars.data();
  const char *pe = chars.data() + chars.size();
  const char *p = ps;
  size_t n_remain = size_t(n);

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
  for (size_t i = 0; i < n; i++) {
    size_t len = my_strnlen(p, n_remain);

    if ((p + len) > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    std::string str;
    if (len > 0) {
      str = std::string(p, len);
    }

    p += len + 1;  // +1 = '\0'
    n_remain = size_t(pe - p);
    assert(p <= pe);
    if (p > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    value::token tok(str);

    DCOUT("token[" << i << "] = " << tok);
    _tokens.push_back(tok);
  }

  return true;
}

bool CrateReader::ReadStrings() {
  if ((_strings_index < 0) ||
      (_strings_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `STRINGS` section.\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_strings_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `STRINGS` section.\n";
    return false;
  }

  if (!ReadIndices(_sr, &_string_indices)) {
    _err += "Failed to read StringIndex array.\n";
    return false;
  }

  for (size_t i = 0; i < _string_indices.size(); i++) {
    DCOUT("StringIndex[" << i << "] = " << _string_indices[i].value);
  }

  return true;
}

bool CrateReader::ReadFields() {
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

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDS` section.\n";
    return false;
  }

  uint64_t num_fields;
  if (!_sr->read8(&num_fields)) {
    _err += "Failed to read # of fields at `FIELDS` section.\n";
    return false;
  }

  _fields.resize(static_cast<size_t>(num_fields));

  // indices
  {
    std::vector<char> comp_buffer(
        Usd_IntegerCompression::GetCompressedBufferSize(
            static_cast<size_t>(num_fields)));
    // temp buffer for decompress
    std::vector<uint32_t> tmp;
    tmp.resize(static_cast<size_t>(num_fields));

    uint64_t fields_size;
    if (!_sr->read8(&fields_size)) {
      _err += "Failed to read field legnth at `FIELDS` section.\n";
      return false;
    }

    if (fields_size !=
        _sr->read(size_t(fields_size), size_t(fields_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read field data at `FIELDS` section.\n";
      return false;
    }

    std::string err;
    DCOUT("fields_size = " << fields_size << ", tmp.size = " << tmp.size()
                           << ", num_fields = " << num_fields);
    Usd_IntegerCompression::DecompressFromBuffer(
        comp_buffer.data(), size_t(fields_size), tmp.data(), size_t(num_fields),
        &err);

    if (!err.empty()) {
      _err += err;
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].token_index.value = tmp[i];
    }
  }

  // Value reps
  {
    uint64_t reps_size;
    if (!_sr->read8(&reps_size)) {
      _err += "Failed to read reps legnth at `FIELDS` section.\n";
      return false;
    }

    std::vector<char> comp_buffer(static_cast<size_t>(reps_size));

    if (reps_size !=
        _sr->read(size_t(reps_size), size_t(reps_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read reps data at `FIELDS` section.\n";
      return false;
    }

    // reps datasize = LZ4 compressed. uncompressed size = num_fields * 8 bytes
    std::vector<uint64_t> reps_data;
    reps_data.resize(static_cast<size_t>(num_fields));

    size_t uncompressed_size = size_t(num_fields) * sizeof(uint64_t);

    if (uncompressed_size != LZ4Compression::DecompressFromBuffer(
                                 comp_buffer.data(),
                                 reinterpret_cast<char *>(reps_data.data()),
                                 size_t(reps_size), uncompressed_size, &_err)) {
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].value_rep = crate::ValueRep(reps_data[i]);
    }
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

  _fieldset_indices.resize(static_cast<size_t>(num_fieldsets));

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_fieldsets)));

  std::vector<uint32_t> tmp(static_cast<size_t>(num_fieldsets));
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_fieldsets)));

  uint64_t fsets_size;
  if (!_sr->read8(&fsets_size)) {
    _err += "Failed to read fieldsets size at `FIELDSETS` section.\n";
    return false;
  }

  DCOUT("num_fieldsets = " << num_fieldsets << ", fsets_size = " << fsets_size
                           << ", comp_buffer.size = " << comp_buffer.size());

  assert(fsets_size < comp_buffer.size());

  if (fsets_size !=
      _sr->read(size_t(fsets_size), size_t(fsets_size),
                reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
    _err += "Failed to read fieldsets data at `FIELDSETS` section.\n";
    return false;
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

  return true;
}

bool CrateReader::BuildLiveFieldSets() {
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

  DCOUT("num_specs " << num_specs);

  _specs.resize(static_cast<size_t>(num_specs));

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_specs)));

  std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_specs)));

  // path indices
  {
    uint64_t path_indexes_size;
    if (!_sr->read8(&path_indexes_size)) {
      PUSH_ERROR("Failed to read path indexes size at `SPECS` section.");
      return false;
    }

    assert(path_indexes_size < comp_buffer.size());

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

    assert(fset_indexes_size < comp_buffer.size());

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

    assert(spectype_size < comp_buffer.size());

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

  return true;
}

bool CrateReader::ReadPaths() {
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

  if (!ReadCompressedPaths(num_paths)) {
    PUSH_ERROR("Failed to read compressed paths.");
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "# of paths " << _paths.size() << "\n";

  for (size_t i = 0; i < _paths.size(); i++) {
    std::cout << "path[" << i << "] = " << _paths[i].full_path_name() << "\n";
  }
#endif

  return true;
}

bool CrateReader::ReadBootStrap() {
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

  DCOUT("toc sections = " << num_sections);

  _toc.sections.resize(static_cast<size_t>(num_sections));

  for (size_t i = 0; i < num_sections; i++) {
    if (!ReadSection(&_toc.sections[i])) {
      PUSH_ERROR("Failed to read TOC section at " + std::to_string(i));
      return false;
    }
    DCOUT("section[" << i << "] name = " << _toc.sections[i].name
                     << ", start = " << _toc.sections[i].start
                     << ", size = " << _toc.sections[i].size);

    // find index
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

#if 0
bool CrateReader::ParseAttribute(const FieldValuePairVector &fvs,
                                 PrimAttrib *attr,
                                 const std::string &prop_name) {
  bool success = false;

  DCOUT("fvs.size = " << fvs.size());

  bool has_connection{false};

  Variability variability{Variability::Varying};
  Interpolation interpolation{Interpolation::Invalid};

  // Check if required field exists.
  if (!HasFieldValuePair(fvs, kTypeName, kToken)) {
    PUSH_ERROR(
        "\"typeName\" field with `token` type must exist for Attribute data.");
    return false;
  }

  if (!HasField(kDefault)) {
    PUSH_ERROR("\"default\" field must exist for Attribute data.");
    return false;
  }

  //
  // Parse properties
  //
  for (const auto &fv : fvs) {
    DCOUT("===  fvs.first " << fv.first
                            << ", second: " << fv.second.type_name());
    if ((fv.first == "typeName") && (fv.second.type_name() == "Token")) {
      attr->set_type_name(fv.second.value<value::token>().str());
      DCOUT("typeName: " << attr->type_name());
    } else if (fv.first == "default") {
      // Nothing to do at there. Process `default` in the later
      continue;
    } else if (fv.first == "targetPaths") {
      // e.g. connection to Material.
      const ListOp<Path> paths = fv.second.value<ListOp<Path>>();

      DCOUT("ListOp<Path> = " << to_string(paths));
      // Currently we only support single explicit path.
      if ((paths.GetExplicitItems().size() == 1)) {
        const Path &path = paths.GetExplicitItems()[0];
        (void)path;

        DCOUT("full path: " << path.full_path_name());
        //DCOUT("local path: " << path.local_path_name());

        primvar::PrimVar var;
        var.set_scalar(path);
        attr->set_var(std::move(var));

        has_connection = true;

      } else {
        return false;
      }
    } else if (fv.first == "connectionPaths") {
      // e.g. connection to texture file.
      const ListOp<Path> paths = fv.second.value<ListOp<Path>>();

      DCOUT("ListOp<Path> = " << to_string(paths));

      // Currently we only support single explicit path.
      if ((paths.GetExplicitItems().size() == 1)) {
        const Path &path = paths.GetExplicitItems()[0];
        (void)path;

        DCOUT("full path: " << path.full_path_name());
        //DCOUT("local path: " << path.local_path_name());

        primvar::PrimVar var;
        var.set_scalar(path);
        attr->set_var(std::move(var));

        has_connection = true;

      } else {
        return false;
      }
    } else if ((fv.first == "variablity") &&
               (fv.second.type_name() == "Variability")) {
      variability = fv.second.value<Variability>();
    } else if ((fv.first == "interpolation") &&
               (fv.second.type_name() == "Token")) {
      interpolation =
          InterpolationFromString(fv.second.value<value::token>().str());
    } else {
      DCOUT("TODO: name: " << fv.first
                           << ", type: " << fv.second.type_name());
    }
  }

  attr->variability = variability;
  attr->meta.interpolation = interpolation;

  //
  // Decode value(stored in "default" field)
  //
  const auto fvRet = GetFieldValuePair(fvs, kDefault);
  if (!fvRet) {
    // This code path should not happen. Just in case.
    PUSH_ERROR("`default` field not found.");
    return false;
  }
  const auto fv = fvRet.value();

  auto add1DArraySuffix = [](const std::string &a) -> std::string {
    return a + "[]";
  };

  {
    if (fv.first == "default") {
      attr->name = prop_name;

      DCOUT("fv.second.type_name = " << fv.second.type_name());

#define PROC_SCALAR(__tyname, __ty)                             \
  }                                                             \
  else if (fv.second.type_name() == __tyname) {               \
    auto ret = fv.second.get_value<__ty>();                     \
    if (!ret) {                                                 \
      PUSH_ERROR("Failed to decode " << __tyname << " value."); \
      return false;                                             \
    }                                                           \
    primvar::PrimVar var; \
    var.set_scalar(ret.value()); \
    attr->set_var(std::move(var));                          \
    success = true;

#define PROC_ARRAY(__tyname, __ty)                                  \
  }                                                                 \
  else if (fv.second.type_name() == add1DArraySuffix(__tyname)) { \
    auto ret = fv.second.get_value<std::vector<__ty>>();            \
    if (!ret) {                                                     \
      PUSH_ERROR("Failed to decode " << __tyname << "[] value.");   \
      return false;                                                 \
    }                                                               \
    primvar::PrimVar var; \
    var.set_scalar(ret.value()); \
    attr->set_var(std::move(var));                          \
    success = true;

      if (0) {  // dummy
        PROC_SCALAR(value::kFloat, float)
        PROC_SCALAR(value::kBool, bool)
        PROC_SCALAR(value::kInt, int)
        PROC_SCALAR(value::kFloat2, value::float2)
        PROC_SCALAR(value::kFloat3, value::float3)
        PROC_SCALAR(value::kFloat4, value::float4)
        PROC_SCALAR(value::kHalf2, value::half2)
        PROC_SCALAR(value::kHalf3, value::half3)
        PROC_SCALAR(value::kHalf4, value::half4)
        PROC_SCALAR(value::kToken, value::token)
        PROC_SCALAR(value::kAssetPath, value::AssetPath)

        PROC_SCALAR(value::kMatrix2d, value::matrix2d)
        PROC_SCALAR(value::kMatrix3d, value::matrix3d)
        PROC_SCALAR(value::kMatrix4d, value::matrix4d)

        // It seems `token[]` is defined as `TokenVector` in CrateData.
        // We tret it as scalar
        PROC_SCALAR("TokenVector", std::vector<value::token>)

        // TODO(syoyo): Use constexpr concat
        PROC_ARRAY(value::kInt, int32_t)
        PROC_ARRAY(value::kUInt, uint32_t)
        PROC_ARRAY(value::kFloat, float)
        PROC_ARRAY(value::kFloat2, value::float2)
        PROC_ARRAY(value::kFloat3, value::float3)
        PROC_ARRAY(value::kFloat4, value::float4)
        PROC_ARRAY(value::kToken, value::token)

        PROC_ARRAY(value::kMatrix2d, value::matrix2d)
        PROC_ARRAY(value::kMatrix3d, value::matrix3d)
        PROC_ARRAY(value::kMatrix4d, value::matrix4d)

        PROC_ARRAY(value::kPoint3h, value::point3h)
        PROC_ARRAY(value::kPoint3f, value::point3f)
        PROC_ARRAY(value::kPoint3d, value::point3d)

        PROC_ARRAY(value::kVector3h, value::vector3h)
        PROC_ARRAY(value::kVector3f, value::vector3f)
        PROC_ARRAY(value::kVector3d, value::vector3d)

        PROC_ARRAY(value::kNormal3h, value::normal3h)
        PROC_ARRAY(value::kNormal3f, value::normal3f)
        PROC_ARRAY(value::kNormal3d, value::normal3d)

        // PROC_ARRAY("Vec2fArray", value::float2)
        // PROC_ARRAY("Vec3fArray", value::float3)
        // PROC_ARRAY("Vec4fArray", value::float4)
        // PROC_ARRAY("IntArray", int)
        // PROC_ARRAY(kTokenArray, value::token)

      } else {
        PUSH_ERROR("TODO: " + fv.second.type_name());
      }
    }
  }

  if (!success && has_connection) {
    // Attribute has a connection(has a path and no `default` field)
    success = true;
  }

  return success;
}
#endif


}  // namespace crate
}  // namespace tinyusdz
