// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Crate(binary format) reader
//
//
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
#include "pprint-meta.hh"
#include "core/prim-spec.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"
#include "str-util.hh"
#include "safe-arithmetic.hh"

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
  MEMORY_BUDGET_CHECK((*memory_manager_), (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_->Release(__nbytes)



#define VERSION_LESS_THAN_0_8_0(__version) ((_version[0] == 0) && (_version[1] < 7))

///
/// Safe size computation: uint64_t n * sizeof(T) -> size_t
/// Returns true on success, false on overflow.
///
template <typename T>
bool SafeSizeForN(uint64_t n, size_t* out) {
  return safe::n_to_size<T>(n, out);
}

//
// --
//
CrateReader::CrateReader(StreamReader *sr, const CrateReaderConfig &config) 
    : _sr(sr), owned_memory_manager_(config.maxMemoryBudget), memory_manager_(&owned_memory_manager_), _impl(nullptr) {
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
  // All dedup array caches now store indices (size_t) instead of TypedArray objects
  // No manual cleanup needed - the actual array data is owned by TimeSamples

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

  size_t datalen;
  if (!SafeSizeForN<crate::Index>(n, &datalen)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in ReadIndices: n * sizeof(crate::Index)");
  }

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

// Numeric array reading functions moved to crate-reader-arrays.cc

bool CrateReader::ReadDoubleVector(std::vector<double> *d) {
  size_t length;

  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read the number of array elements.\n";
    return false;
  }

#if SIZE_MAX < UINT64_MAX
  if (n > static_cast<uint64_t>(SIZE_MAX)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Array element count exceeds addressable memory");
  }
#endif
  length = size_t(n);

  if (length == 0) {
    d->clear();
    return true;
  }

  if (length > _config.maxArrayElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many array elements.");
  }

  {
    size_t byte_count;
    if (!safe::mul(length, sizeof(double), &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

  d->resize(length);

  // TODO(syoyo): Zero-copy
  if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                 reinterpret_cast<uint8_t *>(d->data()))) {
    _err += "Failed to read double vector data.\n";
    return false;
  }

  return true;
}

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

    {
      size_t byte_count;
      if (!safe::n_to_size<crate::Index>(n, &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
    }

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      PUSH_ERROR("Failed to read STRING_VECTOR data.");
      return false;
    }

    // reconstruct
    {
      size_t byte_count;
      if (!safe::mul(size_t(n), sizeof(void *), &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
    }
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

  {
    size_t byte_count;
    if (!safe::n_to_size<LayerOffset>(n, &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

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

    {
      size_t byte_count;
      if (!safe::n_to_size<crate::Index>(n, &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
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

    if (n > _config.maxArrayElements) {
      _err += "Too many ListOp elements.\n";
      return false;
    }

    {
      size_t byte_count;
      if (!safe::n_to_size<crate::Index>(n, &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
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

    {
      size_t byte_count;
      if (!safe::n_to_size<crate::Index>(n, &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
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

    {
      size_t byte_count;
      if (!safe::n_to_size<crate::Index>(n, &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
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

  size_t Reference_size;
  if (!SafeSizeForN<Reference>(n, &Reference_size)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in CHECK_MEMORY_USAGE");
  }
  CHECK_MEMORY_USAGE(Reference_size);

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

  size_t Payload_size;
  if (!SafeSizeForN<Payload>(n, &Payload_size)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in CHECK_MEMORY_USAGE");
  }
  CHECK_MEMORY_USAGE(Payload_size);

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

#if SIZE_MAX < UINT64_MAX
  if (n > static_cast<uint64_t>(SIZE_MAX)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Array element count exceeds addressable memory");
  }
#endif

  {
    size_t byte_count;
    if (!safe::n_to_size<T>(n, &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

  d->resize(size_t(n));
  if (!_sr->read(sizeof(T) * n, sizeof(T) * size_t(n), reinterpret_cast<uint8_t *>(d->data()))) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read array data");
  }

  return true;
}

// Explicit instantiations for types used in timesamples
template bool CrateReader::ReadArray<unsigned char>(std::vector<unsigned char>*);
template bool CrateReader::ReadArray<value::AssetPath>(std::vector<value::AssetPath>*);
template bool CrateReader::ReadArray<value::matrix2d>(std::vector<value::matrix2d>*);
template bool CrateReader::ReadArray<value::matrix3d>(std::vector<value::matrix3d>*);
template bool CrateReader::ReadArray<value::matrix4d>(std::vector<value::matrix4d>*);
// Vector type instantiations needed by crate-reader-timesamples.cc
template bool CrateReader::ReadArray<value::int2>(std::vector<value::int2>*);
template bool CrateReader::ReadArray<value::int3>(std::vector<value::int3>*);
template bool CrateReader::ReadArray<value::int4>(std::vector<value::int4>*);
template bool CrateReader::ReadArray<value::half2>(std::vector<value::half2>*);
template bool CrateReader::ReadArray<value::half3>(std::vector<value::half3>*);
template bool CrateReader::ReadArray<value::half4>(std::vector<value::half4>*);
template bool CrateReader::ReadArray<value::float2>(std::vector<value::float2>*);
template bool CrateReader::ReadArray<value::float3>(std::vector<value::float3>*);
template bool CrateReader::ReadArray<value::float4>(std::vector<value::float4>*);
template bool CrateReader::ReadArray<value::double2>(std::vector<value::double2>*);
template bool CrateReader::ReadArray<value::double3>(std::vector<value::double3>*);
template bool CrateReader::ReadArray<value::double4>(std::vector<value::double4>*);
template bool CrateReader::ReadArray<value::quatf>(std::vector<value::quatf>*);
template bool CrateReader::ReadArray<value::quath>(std::vector<value::quath>*);
template bool CrateReader::ReadArray<value::quatd>(std::vector<value::quatd>*);
// String type instantiation needed by crate-reader-timesamples.cc
template bool CrateReader::ReadArray<std::string>(std::vector<std::string>*);

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

// Explicit template instantiations for ReadListOp (used in crate-reader-values.cc)
template bool CrateReader::ReadListOp<Payload>(ListOp<Payload>*);
template bool CrateReader::ReadListOp<Reference>(ListOp<Reference>*);
template bool CrateReader::ReadListOp<int32_t>(ListOp<int32_t>*);
template bool CrateReader::ReadListOp<int64_t>(ListOp<int64_t>*);
template bool CrateReader::ReadListOp<uint32_t>(ListOp<uint32_t>*);
template bool CrateReader::ReadListOp<uint64_t>(ListOp<uint64_t>*);
template bool CrateReader::ReadListOp<Path>(ListOp<Path>*);
template bool CrateReader::ReadListOp<std::string>(ListOp<std::string>*);
template bool CrateReader::ReadListOp<Token>(ListOp<Token>*);

// Value unpacking (UnpackValueRep, etc.) moved to crate-reader-values.cc

// Path decompression & hierarchy building moved to crate-reader-paths.cc

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

  // At least min size should be 4 both for compress and uncompress.
  if (uncompressedSize < 4) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "uncompressedSize too small or zero bytes.");
  }

  // Guard against OOM: reject absurdly large uncompressed sizes before allocating.
  if (uncompressedSize > uint64_t(_config.maxMemoryBudget)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("uncompressedSize {} exceeds memory budget {}.", uncompressedSize, _config.maxMemoryBudget));
  }

  // Must be larger than len(';-)') + all empty string case.
  // 3 = ';-)'
  // num_tokens = '\0' delimiter
  // Subtraction safe: uncompressedSize >= 4 checked above.
  if (num_tokens > uncompressedSize - 3) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`TOKENS` section corrupted.");
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
  if (bufSize > (std::numeric_limits<uint64_t>::max)() - 128) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "bufSize overflow in addition.");
  }
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

    if (len > _config.maxTokenLength) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Token string length {} exceeds limit {}. Increase CrateReaderConfig.maxTokenLength to allow longer tokens.", len, _config.maxTokenLength));
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
    if (num_fields > (std::numeric_limits<int32_t>::max)() / sizeof(uint32_t)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Too many fields in `FIELDS` section.");
    }
  }

  {
    size_t byte_count;
    if (!safe::n_to_size<Field>(num_fields, &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

  _fields.resize(static_cast<size_t>(num_fields));

  // indices
  {

    {
      size_t byte_count;
      if (!safe::n_to_size<uint32_t>(num_fields, &byte_count)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
      }
      CHECK_MEMORY_USAGE(byte_count);
    }

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

  {
    size_t byte_count;
    if (!safe::n_to_size<uint32_t>(num_fieldsets, &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

  _fieldset_indices.resize(static_cast<size_t>(num_fieldsets));

  // Create temporary space for decompressing.
  size_t compBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_fieldsets));

  CHECK_MEMORY_USAGE(compBufferSize);

  {
    size_t byte_count;
    if (!safe::mul(sizeof(uint32_t), size_t(num_fieldsets), &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }
  std::vector<uint32_t> tmp;
  tmp.resize(static_cast<size_t>(num_fieldsets));

  size_t workBufferSize = Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_fieldsets));

  CHECK_MEMORY_USAGE(workBufferSize);

  // Optimized implementation: reuse buffers across calls
  if (_decomp_comp_buffer.size() < compBufferSize) {
    _decomp_comp_buffer.resize(compBufferSize);
  }
  if (_decomp_working_buffer.size() < workBufferSize) {
    _decomp_working_buffer.resize(workBufferSize);
  }
  std::vector<char> &comp_buffer = _decomp_comp_buffer;
  std::vector<char> &working_space = _decomp_working_buffer;

  uint64_t fsets_size;
  if (!_sr->read8(&fsets_size)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read fieldsets size at `FIELDSETS` section.");
  }

  DCOUT("num_fieldsets = " << num_fieldsets << ", fsets_size = " << fsets_size
                           << ", comp_buffer.size = " << comp_buffer.size());

  if (fsets_size > comp_buffer.size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "fsets_size exceeds comp_buffer size (corrupted USDC).");
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

  if (!BuildFieldSetBoundaryIndex()) {
    return false;
  }

  return true;
}

bool CrateReader::BuildFieldSetBoundaryIndex() {
  static constexpr uint32_t kInvalidFieldSetEnd = ~0u;

  _fieldset_end_indices.clear();
  _fieldset_start_indices.clear();

  _fieldset_end_indices.resize(_fieldset_indices.size(), kInvalidFieldSetEnd);
  _fieldset_start_indices.reserve((_fieldset_indices.size() / 2) + 1);

  size_t start = 0;
  while (start < _fieldset_indices.size()) {
    size_t end = start;
    while ((end < _fieldset_indices.size()) &&
           (_fieldset_indices[end] != crate::Index())) {
      ++end;
    }

    if (end >= _fieldset_indices.size()) {
      PUSH_ERROR("Corrupted fieldset data: missing terminator.");
      return false;
    }

#if SIZE_MAX > UINT32_MAX
    if ((start > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) ||
        (end > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))) {
      PUSH_ERROR("Fieldset boundary index overflow.");
      return false;
    }
#endif

    const uint32_t start_u32 = static_cast<uint32_t>(start);
    const uint32_t end_u32 = static_cast<uint32_t>(end);
    _fieldset_start_indices.push_back(start_u32);
    _fieldset_end_indices[start_u32] = end_u32;

    start = end + 1;
  }

  return true;
}

bool CrateReader::BuildLiveFieldSets() {
  // Report progress (80%)
  if (!ReportProgress(0.8f)) {
    PUSH_ERROR("Parsing cancelled by progress callback.");
    return false;
  }

  if (_fieldset_end_indices.size() != _fieldset_indices.size()) {
    if (!BuildFieldSetBoundaryIndex()) {
      return false;
    }
  }

  _live_fieldsets.clear();
  _live_fieldsets.reserve(_fieldset_start_indices.size());

  for (uint32_t start_idx : _fieldset_start_indices) {
    if (start_idx >= _fieldset_end_indices.size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Fieldset start_idx {} out of range (end_indices.size = {}).", start_idx, _fieldset_end_indices.size()));
    }
    const uint32_t end_idx = _fieldset_end_indices[start_idx];

    if (end_idx < start_idx) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid fieldset: end_idx {} < start_idx {}.", end_idx, start_idx));
    }

    auto emplaced =
        _live_fieldsets.emplace(crate::Index(start_idx), FieldValuePairVector{});
    auto &pairs = emplaced.first->second;

    size_t range_size = static_cast<size_t>(end_idx - start_idx);
    if (range_size > _fields.size()) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Fieldset range {} exceeds total fields count {}.", range_size, _fields.size()));
    }
    pairs.resize(range_size);
    DCOUT("range size = " << range_size);
    // TODO(syoyo): Parallelize.
    for (uint32_t idx = start_idx, i = 0; idx < end_idx; ++idx, ++i) {
      if (_fieldset_indices[idx].value >= _fields.size()) {
        PUSH_ERROR("Invalid live field set data.");
        return false;
      }

      DCOUT("fieldIndex = " << (_fieldset_indices[idx].value));
      auto const &field = _fields[_fieldset_indices[idx].value];
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

bool CrateReader::DecodeFieldSet(crate::Index fieldset_index,
                                 FieldValuePairVector *pairs) {
  static constexpr uint32_t kInvalidFieldSetEnd = ~0u;

  if (!pairs) {
    PUSH_ERROR("`pairs` argument is nullptr.");
    return false;
  }

  pairs->clear();

  if (_fieldset_end_indices.size() != _fieldset_indices.size()) {
    if (!BuildFieldSetBoundaryIndex()) {
      return false;
    }
  }

  if (fieldset_index.value >= _fieldset_end_indices.size()) {
    PUSH_ERROR("FieldSet id out of range: " +
               std::to_string(fieldset_index.value));
    return false;
  }

  const uint32_t fs_end = _fieldset_end_indices[fieldset_index.value];
  if (fs_end == kInvalidFieldSetEnd) {
    PUSH_ERROR("FieldSet id does not point to a fieldset start: " +
               std::to_string(fieldset_index.value));
    return false;
  }
  if (fs_end < fieldset_index.value) {
    PUSH_ERROR("Corrupted fieldset boundary.");
    return false;
  }

  size_t fs_range_size = static_cast<size_t>(fs_end - fieldset_index.value);
  if (fs_range_size > _fields.size()) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("FieldSet range {} exceeds total fields count {}.", fs_range_size, _fields.size()));
  }
  pairs->resize(fs_range_size);

  for (uint32_t idx = fieldset_index.value, i = 0; idx < fs_end; ++idx, ++i) {
    if (_fieldset_indices[idx].value >= _fields.size()) {
      PUSH_ERROR("Invalid field index in fieldset.");
      return false;
    }

    const auto &field = _fields[_fieldset_indices[idx].value];
    if (auto tokv = GetToken(field.token_index)) {
      (*pairs)[i].first = tokv.value().str();
      if (!UnpackValueRep(field.value_rep, &(*pairs)[i].second)) {
        PUSH_ERROR("DecodeFieldSet: Failed to unpack field '" << tokv.value().str()
                   << "' ValueRep : " << field.value_rep.GetStringRepr());
        return false;
      }
    } else {
      PUSH_ERROR("Invalid token index.");
      return false;
    }
  }

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

  {
    size_t byte_count;
    if (!safe::n_to_size<Spec>(num_specs, &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

  _specs.resize(static_cast<size_t>(num_specs));

  // TODO: Memory size check

  // Create temporary space for decompressing.
  size_t compBufferSize= Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_specs));

  CHECK_MEMORY_USAGE(compBufferSize);
  {
    size_t byte_count;
    if (!safe::n_to_size<uint32_t>(num_specs, &byte_count)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow in array size computation");
    }
    CHECK_MEMORY_USAGE(byte_count);
  }

  std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));

  size_t workBufferSize= Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_specs));

  CHECK_MEMORY_USAGE(workBufferSize);

  // Optimized implementation: reuse buffers across calls
  if (_decomp_comp_buffer.size() < compBufferSize) {
    _decomp_comp_buffer.resize(compBufferSize);
  }
  if (_decomp_working_buffer.size() < workBufferSize) {
    _decomp_working_buffer.resize(workBufferSize);
  }
  std::vector<char> &comp_buffer = _decomp_comp_buffer;
  std::vector<char> &working_space = _decomp_working_buffer;

  // path indices
  {
    uint64_t path_indexes_size;
    if (!_sr->read8(&path_indexes_size)) {
      PUSH_ERROR("Failed to read path indexes size at `SPECS` section.");
      return false;
    }

    if (path_indexes_size > comp_buffer.size()) {
      PUSH_ERROR("path_indexes_size exceeds comp_buffer size (corrupted USDC). "
                 "path_indexes_size: " + std::to_string(path_indexes_size) +
                 ", buffer size: " + std::to_string(comp_buffer.size()));
      return false;
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
      PUSH_ERROR("fset_indexes_size exceeds comp_buffer size (corrupted USDC).");
      return false;
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
      PUSH_ERROR("spectype_size exceeds comp_buffer size (corrupted USDC).");
      return false;
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

  // Clear dedup map to prevent stale entries from previous file loads
  // This ensures each file starts with a clean dedup state
  // NOTE: This is NOT thread-safe - concurrent parsing requires external synchronization
  clear_all_timesamples_dedup_entries();

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

  // AOUSD Core Spec 16.3: Current crate version is 0.13.0
  // Support versions 0.4.0 through 0.13.x
  if ((version[0] == 0) && (version[1] <= 13)) {
    // ok
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Unsupported crate version {}.{}.{}. TinyUSDZ supports version 0.4.0 through 0.13.x",
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

  size_t toc_bytes{0};
  if (!safe::mul(num_sections, sizeof(Section), &toc_bytes)) {
    PUSH_ERROR("Integer overflow in TOC sections size computation");
  }
  CHECK_MEMORY_USAGE(toc_bytes);

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

    // Guard against signed integer overflow before casting to size_t.
    if (_toc.sections[i].size > 0 &&
        _toc.sections[i].start > (std::numeric_limits<int64_t>::max)() - _toc.sections[i].size) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Section start + size overflows int64."));
    }
    size_t end_offset = size_t(_toc.sections[i].start + _toc.sections[i].size);
    if (sizeof(void *) == 4) { // 32bit
      if (end_offset > size_t((std::numeric_limits<int32_t>::max)())) {
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
