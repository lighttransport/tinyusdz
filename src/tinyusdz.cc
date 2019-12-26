#include <cassert>
#include <fstream>
#include <vector>

#include "integerCoding.h"
#include "lz4-compression.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"

#include <iostream>  // dbg

namespace tinyusdz {

namespace {

constexpr size_t kSectionNameMaxLength = 15;

// -- from USD ----------------------------------------------------------------

//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.

// Index base class.  Used to index various tables.  Deriving adds some
// type-safety so we don't accidentally use one kind of index with the wrong
// kind of table.
struct Index {
  Index() : value(~0) {}
  explicit Index(uint32_t value) : value(value) {}
  bool operator==(const Index &other) const { return value == other.value; }
  bool operator!=(const Index &other) const { return !(*this == other); }
  uint32_t value;
};

enum class TypeEnum : int32_t;

// Value in file representation.  Consists of a 2 bytes of type information
// (type enum value, array bit, and inlined-value bit) and 6 bytes of data.
// If possible, we attempt to store certain values directly in the local
// data, such as ints, floats, enums, and special-case values of other types
// (zero vectors, identity matrices, etc).  For values that aren't stored
// inline, the 6 data bytes are the offset from the start of the file to the
// value's location.
struct ValueRep {
  friend class CrateFile;

  ValueRep() = default;

  explicit constexpr ValueRep(uint64_t data) : data(data) {}

  constexpr ValueRep(TypeEnum t, bool isInlined, bool isArray, uint64_t payload)
      : data(_Combine(t, isInlined, isArray, payload)) {}

  static const uint64_t _IsArrayBit = 1ull << 63;
  static const uint64_t _IsInlinedBit = 1ull << 62;
  static const uint64_t _IsCompressedBit = 1ull << 61;

  static const uint64_t _PayloadMask = ((1ull << 48) - 1);

  inline bool IsArray() const { return data & _IsArrayBit; }
  inline void SetIsArray() { data |= _IsArrayBit; }

  inline bool IsInlined() const { return data & _IsInlinedBit; }
  inline void SetIsInlined() { data |= _IsInlinedBit; }

  inline bool IsCompressed() const { return data & _IsCompressedBit; }
  inline void SetIsCompressed() { data |= _IsCompressedBit; }

  inline TypeEnum GetType() const {
    return static_cast<TypeEnum>((data >> 48) & 0xFF);
  }
  inline void SetType(TypeEnum t) {
    data &= ~(0xFFull << 48);                  // clear type byte in data.
    data |= (static_cast<uint64_t>(t) << 48);  // set it.
  }

  inline uint64_t GetPayload() const { return data & _PayloadMask; }

  inline void SetPayload(uint64_t payload) {
    data &= ~_PayloadMask;  // clear existing payload.
    data |= payload & _PayloadMask;
  }

  inline uint64_t GetData() const { return data; }

  bool operator==(ValueRep other) const { return data == other.data; }
  bool operator!=(ValueRep other) const { return !(*this == other); }

  friend inline size_t hash_value(ValueRep v) {
    return static_cast<size_t>(v.data);
  }

 private:
  static constexpr uint64_t _Combine(TypeEnum t, bool isInlined, bool isArray,
                                     uint64_t payload) {
    return (isArray ? _IsArrayBit : 0) | (isInlined ? _IsInlinedBit : 0) |
           (static_cast<uint64_t>(t) << 48) | (payload & _PayloadMask);
  }

  uint64_t data;
};

// ----------------------------------------------------------------------------

struct Field {
  Index token_index;
  ValueRep value_rep;
};

//
// Describes connection of a path(i.e. node) and field(e.g. vertex data)
//
struct Spec {
  Index path_index;
  Index fieldset_index;
  uint32_t spec_type;  // SdfSpecType enum?
};

struct Section {
  Section() { memset(this, 0, sizeof(*this)); }
  Section(char const *name, int64_t start, int64_t size);
  char name[kSectionNameMaxLength + 1];
  int64_t start, size;
};

struct TableOfContents {
  // Section const *GetSection(SectionName) const;
  // int64_t GetMinimumSectionStart() const;
  std::vector<Section> sections;
};

template <class Int>
static inline void _ReadCompressedInts(const StreamReader *sr, Int *out,
                                       size_t size) {
  // TODO(syoyo): Error check
  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;
  std::vector<char> compBuffer(Compressor::GetCompressedBufferSize(size));
  uint64_t compSize;
  sr->read8(&compSize);

  sr->read(compSize, compSize, reinterpret_cast<uint8_t *>(compBuffer.data()));
  Compressor::DecompressFromBuffer(compBuffer.data(), compSize, out, size);
}

static inline bool ReadIndices(const StreamReader *sr,
                               std::vector<Index> *indices) {
  // TODO(syoyo): Error check
  uint64_t n;
  if (!sr->read8(&n)) {
    return false;
  }

  std::cout << "ReadIndices: n = " << n << "\n";

  indices->resize(n);
  size_t datalen = n * sizeof(Index);

  if (datalen != sr->read(datalen, datalen,
                          reinterpret_cast<uint8_t *>(indices->data()))) {
    return false;
  }

  return true;
}

class Parser {
 public:
  Parser(StreamReader *sr) : _sr(sr) {}

  bool ReadBootStrap();
  bool ReadTOC();

  // Read known sections
  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();

  ///
  /// Read TOC section
  ///
  bool ReadSection(Section *s);

  // TODO PrefetchStructuralSections

  std::string GetError() { return _err; }

 private:
  bool ReadCompressedPaths();

  const StreamReader *_sr = nullptr;
  std::string _err;

  // Header(bootstrap)
  uint8_t _version[3] = {0, 0, 0};

  TableOfContents _toc;

  int64_t _toc_offset{0};

  // index to _toc.sections
  int64_t _tokens_index{-1};
  int64_t _paths_index{-1};
  int64_t _strings_index{-1};
  int64_t _fields_index{-1};
  int64_t _fieldsets_index{-1};
  int64_t _specs_index{-1};

  std::vector<std::string> _tokens;
  std::vector<Index> _string_indices;
  std::vector<Field> _fields;
  std::vector<Index> _fieldset_indices;
  std::vector<Spec> _specs;
};

bool Parser::ReadCompressedPaths() {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numPaths;
  if (!_sr->read8(&numPaths)) {
    _err += "Failed to read the number of paths.\n";
    return false;
  }

  pathIndexes.resize(numPaths);
  elementTokenIndexes.resize(numPaths);
  jumps.resize(numPaths);

  // Create temporary space for decompressing.
  std::vector<char> compBuffer(
      Usd_IntegerCompression::GetCompressedBufferSize(numPaths));
  std::vector<char> workingSpace(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(numPaths));

  // pathIndexes.
  {
    uint64_t pathIndexesSize;
    if (!_sr->read8(&pathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (pathIndexesSize !=
        _sr->read(pathIndexesSize, pathIndexesSize,
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read pathIndexes data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), pathIndexesSize, pathIndexes.data(), numPaths, &err,
        workingSpace.data());
    if (!err.empty()) {
      _err += err;
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
        _sr->read(elementTokenIndexesSize, elementTokenIndexesSize,
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read elementTokenIndexes data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), elementTokenIndexesSize, elementTokenIndexes.data(),
        numPaths, &err, workingSpace.data());

    if (!err.empty()) {
      _err += err;
      return false;
    }
  }

  // jumps.
  {
    uint64_t jumpsSize;
    if (!_sr->read8(&jumpsSize)) {
      _err += "Failed to read jumpsSize.\n";
      return false;
    }

    if (jumpsSize !=
        _sr->read(jumpsSize, jumpsSize,
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read jumps data.\n";
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(compBuffer.data(), jumpsSize,
                                                 jumps.data(), numPaths, &err,
                                                 workingSpace.data());

    if (!err.empty()) {
      _err += err;
      return false;
    }
  }

#if 0  // TODO
    // Now build the paths.
    _BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, 0,
                                SdfPath(), dispatcher);

    //dispatcher.Wait();
#endif

  for (uint32_t item : pathIndexes) {
    std::cout << "pathIndexes " << item << "\n";
  }

  for (uint32_t item : elementTokenIndexes) {
    std::cout << "elementTokenIndexes " << item << "\n";
  }

  for (uint32_t item : jumps) {
    std::cout << "jumps " << item << "\n";
  }

  return true;
}

bool Parser::ReadSection(Section *s) {
  size_t name_len = kSectionNameMaxLength + 1;

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

bool Parser::ReadTokens() {
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

  const Section &s = _toc.sections[_tokens_index];
  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `TOKENS` section.\n";
    return false;
  }

  std::cout << "s.start = " << s.start << "\n";

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

  std::cout << "# of tokens = " << n
            << ", uncompressedSize = " << uncompressedSize
            << ", compressedSize = " << compressedSize << "\n";

  std::vector<char> chars(uncompressedSize);
  std::vector<char> compressed(compressedSize);

  if (compressedSize !=
      _sr->read(compressedSize, compressedSize,
                reinterpret_cast<uint8_t *>(compressed.data()))) {
    _err += "Failed to read compressed data at `TOKENS` section.\n";
    return false;
  }

  if (uncompressedSize != LZ4Compression::DecompressFromBuffer(
                              compressed.data(), chars.data(), compressedSize,
                              uncompressedSize, &_err)) {
    return false;
  }

  // Split null terminated string into _tokens.
  const char *p = chars.data();
  const char *pe = chars.data() + chars.size();
  for (size_t i = 0; i < n; i++) {
    // TODO(syoyo): Range check
    std::string token = std::string(p, strlen(p));
    p += strlen(p) + 1;
    assert(p <= pe);

    std::cout << "token[" << i << "] = " << token << "\n";
    _tokens.push_back(token);
  }

  return true;
}

bool Parser::ReadStrings() {
  if ((_strings_index < 0) ||
      (_strings_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `STRINGS` section.\n";
    return false;
  }

  const Section &s = _toc.sections[_strings_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `STRINGS` section.\n";
    return false;
  }

  if (!ReadIndices(_sr, &_string_indices)) {
    _err += "Failed to read StringIndex array.\n";
    return false;
  }

  for (size_t i = 0; i < _string_indices.size(); i++) {
    std::cout << "StringIndex[" << i << "] = " << _string_indices[i].value
              << "\n";
  }

  return true;
}

bool Parser::ReadFields() {
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

  const Section &s = _toc.sections[_fields_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `FIELDS` section.\n";
    return false;
  }

  uint64_t num_fields;
  if (!_sr->read8(&num_fields)) {
    _err += "Failed to read # of fields at `FIELDS` section.\n";
    return false;
  }

  _fields.resize(num_fields);

  // indices
  {
    std::vector<char> comp_buffer(
        Usd_IntegerCompression::GetCompressedBufferSize(num_fields));
    // temp buffer for decompress
    std::vector<uint32_t> tmp;
    tmp.resize(num_fields);

    uint64_t fields_size;
    if (!_sr->read8(&fields_size)) {
      _err += "Failed to read field legnth at `FIELDS` section.\n";
      return false;
    }

    if (fields_size !=
        _sr->read(fields_size, fields_size,
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read field data at `FIELDS` section.\n";
      return false;
    }

    std::string err;
    std::cout << "fields_size = " << fields_size
              << ", tmp.size = " << tmp.size() << ", num_fieds = " << num_fields
              << "\n";
    Usd_IntegerCompression::DecompressFromBuffer(
        comp_buffer.data(), fields_size, tmp.data(), num_fields, &err);

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

    std::vector<char> comp_buffer(reps_size);

    if (reps_size !=
        _sr->read(reps_size, reps_size,
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read reps data at `FIELDS` section.\n";
      return false;
    }

    // reps datasize = LZ4 compressed. uncompressed size = num_fields * 8 bytes
    std::vector<uint64_t> reps_data;
    reps_data.resize(num_fields);

    size_t uncompressed_size = num_fields * sizeof(uint64_t);

    if (uncompressed_size != LZ4Compression::DecompressFromBuffer(
                                 comp_buffer.data(),
                                 reinterpret_cast<char *>(reps_data.data()),
                                 reps_size, uncompressed_size, &_err)) {
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].value_rep = ValueRep(reps_data[i]);
    }
  }

  return true;
}

bool Parser::ReadFieldSets() {
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

  const Section &s = _toc.sections[_fieldsets_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `FIELDSETS` section.\n";
    return false;
  }

  uint64_t num_fieldsets;
  if (!_sr->read8(&num_fieldsets)) {
    _err += "Failed to read # of fieldsets at `FIELDSETS` section.\n";
    return false;
  }

  _fieldset_indices.resize(num_fieldsets);

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(
      Usd_IntegerCompression::GetCompressedBufferSize(num_fieldsets));

  std::vector<uint32_t> tmp(num_fieldsets);
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(num_fieldsets));

  uint64_t fsets_size;
  if (!_sr->read8(&fsets_size)) {
    _err += "Failed to read fieldsets size at `FIELDSETS` section.\n";
    return false;
  }

  std::cout << "num_fieldsets = " << num_fieldsets
            << ", fsets_size = " << fsets_size
            << ", comp_buffer.size = " << comp_buffer.size() << "\n";

  assert(fsets_size < comp_buffer.size());

  if (fsets_size !=
      _sr->read(fsets_size, fsets_size,
                reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
    _err += "Failed to read fieldsets data at `FIELDSETS` section.\n";
    return false;
  }

  std::string err;
  Usd_IntegerCompression::DecompressFromBuffer(comp_buffer.data(), fsets_size,
                                               tmp.data(), num_fieldsets, &err,
                                               working_space.data());

  if (!err.empty()) {
    _err += err;
    return false;
  }

  for (size_t i = 0; i != num_fieldsets; ++i) {
    _fieldset_indices[i].value = tmp[i];
  }

  return true;
}

bool Parser::ReadSpecs() {
  if ((_specs_index < 0) || (_specs_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `SPECS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[_specs_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `SPECS` section.\n";
    return false;
  }

  uint64_t num_specs;
  if (!_sr->read8(&num_specs)) {
    _err += "Failed to read # of specs size at `SPECS` section.\n";
    return false;
  }

  std::cout << "num_specs " << num_specs << "\n";

  _specs.resize(num_specs);

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(
      Usd_IntegerCompression::GetCompressedBufferSize(num_specs));

  std::vector<uint32_t> tmp(num_specs);
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(num_specs));

  // path indices
  {
    uint64_t path_indexes_size;
    if (!_sr->read8(&path_indexes_size)) {
      _err += "Failed to read path indexes size at `SPECS` section.\n";
      return false;
    }

    assert(path_indexes_size < comp_buffer.size());

    if (path_indexes_size !=
        _sr->read(path_indexes_size, path_indexes_size,
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read path indexes data at `SPECS` section.\n";
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      _specs[i].path_index.value = tmp[i];
    }
  }

  // fieldset indices
  {
    uint64_t fset_indexes_size;
    if (!_sr->read8(&fset_indexes_size)) {
      _err += "Failed to read fieldset indexes size at `SPECS` section.\n";
      return false;
    }

    assert(fset_indexes_size < comp_buffer.size());

    if (fset_indexes_size !=
        _sr->read(fset_indexes_size, fset_indexes_size,
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read fieldset indexes data at `SPECS` section.\n";
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      _specs[i].fieldset_index.value = tmp[i];
    }
  }

  // spec types
  {
    uint64_t spectype_size;
    if (!_sr->read8(&spectype_size)) {
      _err += "Failed to read spectype size at `SPECS` section.\n";
      return false;
    }

    assert(spectype_size < comp_buffer.size());

    if (spectype_size !=
        _sr->read(spectype_size, spectype_size,
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read spectype data at `SPECS` section.\n";
      return false;
    }

    // TODO(syoyo): convert to SdfSpecType
    for (size_t i = 0; i != num_specs; ++i) {
      _specs[i].spec_type = tmp[i];
    }
  }

  return true;
}

bool Parser::ReadPaths() {
  if ((_paths_index < 0) || (_paths_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `PATHS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const Section &s = _toc.sections[_paths_index];

  if (!_sr->seek_set(s.start)) {
    _err += "Failed to move to `PATHS` section.\n";
    return false;
  }

  // # of paths.
  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read # of paths.\n";
    return false;
  }

  if (!ReadCompressedPaths()) {
    _err += "Failed to read compressed paths.\n";
    return false;
  }

  std::cout << "# of paths " << n << "\n";

  return true;
}

bool Parser::ReadBootStrap() {
  // parse header.
  uint8_t magic[8];
  if (8 != _sr->read(/* req */ 8, /* dst len */ 8, magic)) {
    _err += "Failed to read magic number.\n";
    return false;
  }

  if (memcmp(magic, "PXR-USDC", 8)) {
    _err += "Invalid magic number.\n";
    return false;
  }

  // parse version(first 3 bytes from 8 bytes)
  uint8_t version[8];
  if (8 != _sr->read(8, 8, version)) {
    _err += "Failed to read magic number.\n";
    return false;
  }

  std::cout << "version = " << int(version[0]) << "." << int(version[1]) << "."
            << int(version[2]) << "\n";

  _version[0] = version[0];
  _version[1] = version[1];
  _version[2] = version[2];

  // We only support version 0.4.0 or later.
  if ((version[0] == 0) && (version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(version[0]) + "." + std::to_string(version[1]) +
            "." + std::to_string(version[2]) + "\n";
    return false;
  }

  _toc_offset = 0;
  if (!_sr->read8(&_toc_offset)) {
    _err += "Failed to read TOC offset.\n";
    return false;
  }

  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    _err += "Invalid TOC offset value: " + std::to_string(_toc_offset) +
            ", filesize = " + std::to_string(_sr->size()) + ".\n";
    return false;
  }

  std::cout << "toc offset = " << _toc_offset << "\n";

  return true;
}

bool Parser::ReadTOC() {
  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    _err += "Invalid toc offset\n";
    return false;
  }

  if (!_sr->seek_set(_toc_offset)) {
    _err += "Failed to move to TOC offset\n";
    return false;
  }

  // read # of sections.
  uint64_t num_sections{0};
  if (!_sr->read8(&num_sections)) {
    _err += "Failed to read TOC(# of sections)\n";
    return false;
  }

  std::cout << "toc sections = " << num_sections << "\n";

  _toc.sections.resize(num_sections);

  for (size_t i = 0; i < num_sections; i++) {
    if (!ReadSection(&_toc.sections[i])) {
      _err += "Failed to read TOC section at " + std::to_string(i) + "\n";
      return false;
    }
    std::cout << "section[" << i << "] name = " << _toc.sections[i].name
              << ", start = " << _toc.sections[i].start
              << ", size = " << _toc.sections[i].size << "\n";

    // find index
    if (0 == strncmp(_toc.sections[i].name, "TOKENS", kSectionNameMaxLength)) {
      _tokens_index = i;
    } else if (0 == strncmp(_toc.sections[i].name, "STRINGS",
                            kSectionNameMaxLength)) {
      _strings_index = i;
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDS",
                            kSectionNameMaxLength)) {
      _fields_index = i;
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDSETS",
                            kSectionNameMaxLength)) {
      _fieldsets_index = i;
    } else if (0 ==
               strncmp(_toc.sections[i].name, "SPECS", kSectionNameMaxLength)) {
      _specs_index = i;
    } else if (0 ==
               strncmp(_toc.sections[i].name, "PATHS", kSectionNameMaxLength)) {
      _paths_index = i;
    }
  }

  return true;
}

}  // namespace

bool LoadUSDCFromFile(const std::string &filename, std::string *err,
                      const USDCLoadOptions &options) {
  std::vector<uint8_t> data;
  {
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      if (err) {
        (*err) = "File not found or cannot open file : " + filename;
      }
      return false;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (int64_t(sz) < 0) {
      // Looks reading directory, not a file.
      if (err) {
        (*err) += "Looks like filename is a directory : \"" + filename + "\"\n";
      }
      return false;
    }

    if (sz < (11 * 8)) {
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a USDC : \"" +
            filename + "\"\n";
      }
      return false;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  bool swap_endian = false;  // @FIXME

  StreamReader sr(data.data(), data.size(), swap_endian);

  Parser parser(&sr);

  if (!parser.ReadBootStrap()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadTOC()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  // Read known sections

  if (!parser.ReadPaths()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadTokens()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadStrings()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadFields()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadFieldSets()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  if (!parser.ReadSpecs()) {
    if (err) {
      (*err) = parser.GetError();
    }
    return false;
  }

  // TODO(syoyo): Read unknown sections
  return true;
}

static_assert(sizeof(Field) == 16, "");
static_assert(sizeof(Spec) == 12, "");

}  // namespace tinyusdz
