#include <algorithm>
#include <cassert>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

#include "integerCoding.h"
#include "lz4-compression.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"

#include <iostream>  // dbg

namespace tinyusdz {

float half_to_float(float16 h) {
  static const FP32 magic = {113 << 23};
  static const unsigned int shifted_exp = 0x7c00
                                          << 13;  // exponent mask after shift
  FP32 o;

  o.u = (h.u & 0x7fffU) << 13U;           // exponent/mantissa bits
  unsigned int exp_ = shifted_exp & o.u;  // just the exponent
  o.u += (127 - 15) << 23;                // exponent adjust

  // handle exponent special cases
  if (exp_ == shifted_exp)    // Inf/NaN?
    o.u += (128 - 16) << 23;  // extra exp adjust
  else if (exp_ == 0)         // Zero/Denormal?
  {
    o.u += 1 << 23;  // extra exp adjust
    o.f -= magic.f;  // renormalize
  }

  o.u |= (h.u & 0x8000U) << 16U;  // sign bit
  return o.f;
}

float16 float_to_half_full(float _f) {
  FP32 f;
  f.f = _f;
  float16 o = {0};

  // Based on ISPC reference code (with minor modifications)
  if (f.s.Exponent == 0)  // Signed zero/denormal (which will underflow)
    o.s.Exponent = 0;
  else if (f.s.Exponent == 255)  // Inf or NaN (all exponent bits set)
  {
    o.s.Exponent = 31;
    o.s.Mantissa = f.s.Mantissa ? 0x200 : 0;  // NaN->qNaN and Inf->Inf
  } else                                      // Normalized number
  {
    // Exponent unbias the single, then bias the halfp
    int newexp = f.s.Exponent - 127 + 15;
    if (newexp >= 31)  // Overflow, return signed infinity
      o.s.Exponent = 31;
    else if (newexp <= 0)  // Underflow
    {
      if ((14 - newexp) <= 24)  // Mantissa might be non-zero
      {
        unsigned int mant = f.s.Mantissa | 0x800000;  // Hidden 1 bit
        o.s.Mantissa = mant >> (14 - newexp);
        if ((mant >> (13 - newexp)) & 1)  // Check for rounding
          o.u++;  // Round, might overflow into exp bit, but this is OK
      }
    } else {
      o.s.Exponent = static_cast<unsigned int>(newexp);
      o.s.Mantissa = f.s.Mantissa >> 13;
      if (f.s.Mantissa & 0x1000)  // Check for rounding
        o.u++;                    // Round, might overflow to inf, this is OK
    }
  }

  o.s.Sign = f.s.Sign;
  return o;
}

namespace {

constexpr size_t kMinCompressedArraySize = 16;
constexpr size_t kSectionNameMaxLength = 15;

float to_float(uint16_t h) {
  float16 f;
  f.u = h;
  return half_to_float(f);
}

const ValueType &GetValueType(int32_t type_id) {
  static std::map<uint32_t, ValueType> table;
  std::cout << "type_id = " << type_id << "\n";
  if (table.size() == 0) {
    // Register data types
    // NOTE(syoyo): We can use C++11 template to create compile-time table for
    // data types, but this way(use std::map) is easier to read and maintain, I
    // think.

    // reference: crateDataTypes.h

#define ADD_VALUE_TYPE(NAME_STR, TYPE_ID, SUPPORTS_ARRAY)          \
  {                                                                \
    assert(table.count(TYPE_ID) == 0);                             \
    table[TYPE_ID] = ValueType(NAME_STR, TYPE_ID, SUPPORTS_ARRAY); \
  }

    ADD_VALUE_TYPE("InvaldOrUnsupported", 0, false);

    // Array types.
    ADD_VALUE_TYPE("Bool", VALUE_TYPE_BOOL, true);

    ADD_VALUE_TYPE("UChar", VALUE_TYPE_UCHAR, true);
    ADD_VALUE_TYPE("Int", VALUE_TYPE_INT, true);
    ADD_VALUE_TYPE("UInt", VALUE_TYPE_UINT, true);
    ADD_VALUE_TYPE("Int64", VALUE_TYPE_INT64, true);
    ADD_VALUE_TYPE("UInt64", VALUE_TYPE_UINT64, true);

    ADD_VALUE_TYPE("Half", VALUE_TYPE_HALF, true);
    ADD_VALUE_TYPE("Float", VALUE_TYPE_FLOAT, true);
    ADD_VALUE_TYPE("Double", VALUE_TYPE_DOUBLE, true);

    ADD_VALUE_TYPE("String", VALUE_TYPE_STRING, true);
    ADD_VALUE_TYPE("Token", VALUE_TYPE_TOKEN, true);
    ADD_VALUE_TYPE("AssetPath", VALUE_TYPE_ASSET_PATH, true);

    ADD_VALUE_TYPE("Quatd", VALUE_TYPE_QUATD, true);
    ADD_VALUE_TYPE("Quatf", VALUE_TYPE_QUATF, true);
    ADD_VALUE_TYPE("Quath", VALUE_TYPE_QUATH, true);

    ADD_VALUE_TYPE("Vec2d", VALUE_TYPE_VEC2D, true);
    ADD_VALUE_TYPE("Vec2f", VALUE_TYPE_VEC2F, true);
    ADD_VALUE_TYPE("Vec2h", VALUE_TYPE_VEC2H, true);
    ADD_VALUE_TYPE("Vec2i", VALUE_TYPE_VEC2I, true);

    ADD_VALUE_TYPE("Vec3d", VALUE_TYPE_VEC3D, true);
    ADD_VALUE_TYPE("Vec3f", VALUE_TYPE_VEC3F, true);
    ADD_VALUE_TYPE("Vec3h", VALUE_TYPE_VEC3H, true);
    ADD_VALUE_TYPE("Vec3i", VALUE_TYPE_VEC3I, true);

    ADD_VALUE_TYPE("Vec4d", VALUE_TYPE_VEC4D, true);
    ADD_VALUE_TYPE("Vec4f", VALUE_TYPE_VEC4F, true);
    ADD_VALUE_TYPE("Vec4h", VALUE_TYPE_VEC4H, true);
    ADD_VALUE_TYPE("Vec4i", VALUE_TYPE_VEC4I, true);

    ADD_VALUE_TYPE("Matrix2d", VALUE_TYPE_MATRIX2D, true);
    ADD_VALUE_TYPE("Matrix3d", VALUE_TYPE_MATRIX3D, true);
    ADD_VALUE_TYPE("Matrix4d", VALUE_TYPE_MATRIX4D, true);

    // Non-array types.
    ADD_VALUE_TYPE("Dictionary", VALUE_TYPE_DICTIONARY,
                   false);  // std::map<std::string, Value>

    ADD_VALUE_TYPE("TokenListOp", VALUE_TYPE_TOKEN_LIST_OP, false);
    ADD_VALUE_TYPE("StringListOp", VALUE_TYPE_STRING_LIST_OP, false);
    ADD_VALUE_TYPE("PathListOp", VALUE_TYPE_PATH_LIST_OP, false);
    ADD_VALUE_TYPE("ReferenceListOp", VALUE_TYPE_REFERENCE_LIST_OP, false);
    ADD_VALUE_TYPE("IntListOp", VALUE_TYPE_INT_LIST_OP, false);
    ADD_VALUE_TYPE("Int64ListOp", VALUE_TYPE_INT64_LIST_OP, false);
    ADD_VALUE_TYPE("UIntListOp", VALUE_TYPE_UINT_LIST_OP, false);
    ADD_VALUE_TYPE("UInt64ListOp", VALUE_TYPE_UINT64_LIST_OP, false);

    ADD_VALUE_TYPE("PathVector", VALUE_TYPE_PATH_VECTOR, false);
    ADD_VALUE_TYPE("TokenVector", VALUE_TYPE_TOKEN_VECTOR, false);

    ADD_VALUE_TYPE("Specifier", VALUE_TYPE_SPECIFIER, false);
    ADD_VALUE_TYPE("Permission", VALUE_TYPE_PERMISSION, false);
    ADD_VALUE_TYPE("Variability", VALUE_TYPE_VARIABILITY, false);

    ADD_VALUE_TYPE("VariantSelectionMap", VALUE_TYPE_VARIANT_SELECTION_MAP,
                   false);
    ADD_VALUE_TYPE("TimeSamples", VALUE_TYPE_TIME_SAMPLES, false);
    ADD_VALUE_TYPE("Payload", VALUE_TYPE_PAYLOAD, false);
    ADD_VALUE_TYPE("DoubleVector", VALUE_TYPE_DOUBLE_VECTOR, false);
    ADD_VALUE_TYPE("LayerOffsetVector", VALUE_TYPE_LAYER_OFFSET_VECTOR, false);
    ADD_VALUE_TYPE("StringVector", VALUE_TYPE_STRING_VECTOR, false);
    ADD_VALUE_TYPE("ValueBlock", VALUE_TYPE_VALUE_BLOCK, false);
    ADD_VALUE_TYPE("Value", VALUE_TYPE_VALUE, false);
    ADD_VALUE_TYPE("UnregisteredValue", VALUE_TYPE_UNREGISTERED_VALUE, false);
    ADD_VALUE_TYPE("UnregisteredValueListOp",
                   VALUE_TYPE_UNREGISTERED_VALUE_LIST_OP, false);
    ADD_VALUE_TYPE("PayloadListOp", VALUE_TYPE_PAYLOAD_LIST_OP, false);
    ADD_VALUE_TYPE("TimeCode", VALUE_TYPE_TIME_CODE, true);
  }
#undef ADD_VALUE_TYPE

  if (!table.count(type_id)) {
    // Invalid or unsupported.
    std::cerr << "Unknonw type id: " << type_id << "\n";
    return table.at(0);
  }

  return table.at(type_id);
}

std::string GetValueTypeRepr(int32_t type_id) {
  ValueType dty = GetValueType(type_id);

  std::stringstream ss;
  ss << "ValueType: " << dty.name << "(" << dty.id
     << "}, supports_array = " << dty.supports_array;
  return ss.str();
}

enum SpecType {
  SpecTypeUnknown = 0,
  SpecTypeAttribute,
  SpecTypeConnection,
  SpecTypeExpression,
  SpecTypeMapper,
  SpecTypeMapperArg,
  SpecTypePrim,
  SpecTypePseudoRoot,
  SpecTypeRelationship,
  SpecTypeRelationshipTarget,
  SpecTypeVariant,
  SpecTypeVariantSet,
  NumSpecTypes
};

// For PrimSpec
enum Specifier {
  SpecifierDef,  // 0
  SpecifierOver,
  SpecifierClass,
  NumSpecifiers
};

enum Permission {
  PermissionPublic,  // 0
  PermissionPrivate,
  NumPermissions
};

enum Variability {
  VariabilityVarying,  // 0
  VariabilityUniform,
  VariabilityConfig,
  NumVariabilities
};

std::string GetSpecTypeString(SpecType ty) {
  if (SpecTypeUnknown == ty) {
    return "SpecTypeUnknown";
  } else if (SpecTypeAttribute == ty) {
    return "SpecTypeAttribute";
  } else if (SpecTypeConnection == ty) {
    return "SpecTypeConection";
  } else if (SpecTypeExpression == ty) {
    return "SpecTypeExpression";
  } else if (SpecTypeMapper == ty) {
    return "SpecTypeMapper";
  } else if (SpecTypeMapperArg == ty) {
    return "SpecTypeMapperArg";
  } else if (SpecTypePrim == ty) {
    return "SpecTypePrim";
  } else if (SpecTypePseudoRoot == ty) {
    return "SpecTypePseudoRoot";
  } else if (SpecTypeRelationship == ty) {
    return "SpecTypeRelationship";
  } else if (SpecTypeRelationshipTarget == ty) {
    return "SpecTypeRelationshipTarget";
  } else if (SpecTypeVariant == ty) {
    return "SpecTypeVariant";
  } else if (SpecTypeVariantSet == ty) {
    return "SpecTypeVariantSet";
  }
  return "??? SpecType " + std::to_string(ty);
}

std::string GetSpecifierString(Specifier ty) {
  if (SpecifierDef == ty) {
    return "SpecifierDef";
  } else if (SpecifierOver == ty) {
    return "SpecifierOver";
  } else if (SpecifierClass == ty) {
    return "SpecifierClass";
  }
  return "??? Specifier " + std::to_string(ty);
}

std::string GetPermissionString(Permission ty) {
  if (PermissionPublic == ty) {
    return "PermissionPublic";
  } else if (PermissionPrivate == ty) {
    return "PermissionPrivate";
  }
  return "??? Permission " + std::to_string(ty);
}

std::string GetVariabilityString(Variability ty) {
  if (VariabilityVarying == ty) {
    return "VariabilityVarying";
  } else if (VariabilityUniform == ty) {
    return "VariabilityUniform";
  } else if (VariabilityConfig == ty) {
    return "VariabilityConfig";
  }
  return "??? Variability " + std::to_string(ty);
}

///
/// Node represents scene graph node.
///
class Node {
 public:
  Node(int64_t parent, Path &path) : _parent(parent), _path(path) {}

  const int64_t GetParent() const { return _parent; }

  const std::vector<int64_t> &GetChildren() const { return _children; }

  ///
  /// Get full path(e.g. `/muda/dora/bora` when the parent is `/muda/dora` and
  /// this node is `bora`)
  ///
  std::string GetFullPath() const;

 private:
  int64_t _parent;                 // -1 = this node is the root node.
  std::vector<int64_t> _children;  // index to child nodes.

  Path _path;  // local path
};

class Scene {
 public:
  std::vector<Node> _nodes;
};

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
  bool operator<(const Index &other) const { return value < other.value; }
  uint32_t value;
};

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

  constexpr ValueRep(int32_t t, bool isInlined, bool isArray, uint64_t payload)
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

  inline int32_t GetType() const {
    return static_cast<int32_t>((data >> 48) & 0xFF);
  }
  inline void SetType(int32_t t) {
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

  // friend inline size_t hash_value(ValueRep v) {
  //  return static_cast<size_t>(v.data);
  //}

  std::string GetStringRepr() const {
    std::stringstream ss;
    ss << "ty: " << static_cast<int>(GetType()) << ", isArray: " << IsArray()
       << ", isInlined: " << IsInlined() << ", isCompressed: " << IsCompressed()
       << ", payload: " << GetPayload();

    return ss.str();
  }

 private:
  static constexpr uint64_t _Combine(int32_t t, bool isInlined, bool isArray,
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
// Spec describes the relation of a path(i.e. node) and field(e.g. vertex data)
//
struct Spec {
  Index path_index;
  Index fieldset_index;
  SpecType spec_type;
};

struct Section {
  Section() { memset(this, 0, sizeof(*this)); }
  Section(char const *name, int64_t start, int64_t size);
  char name[kSectionNameMaxLength + 1];
  int64_t start, size;  // byte offset to section info and its data size
};

//
// TOC = list of sections.
//
struct TableOfContents {
  // Section const *GetSection(SectionName) const;
  // int64_t GetMinimumSectionStart() const;
  std::vector<Section> sections;
};

template <class Int>
static inline bool _ReadCompressedInts(const StreamReader *sr, Int *out,
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

  if (!sr->read(compSize, compSize,
                reinterpret_cast<uint8_t *>(compBuffer.data()))) {
    return false;
  }
  std::string err;
  bool ret = Compressor::DecompressFromBuffer(compBuffer.data(), compSize, out,
                                              size, &err);
  (void)err;

  return ret;
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

  std::string GetToken(Index token_index) {
    if ((token_index.value >= 0) || (token_index.value <= _tokens.size())) {
      return _tokens[token_index.value];
    } else {
      _err += "Token index out of range: " + std::to_string(token_index.value) +
              "\n";
      return std::string();
    }
  }

  // Get string from string index.
  std::string GetString(Index string_index) {
    if ((string_index.value >= 0) ||
        (string_index.value <= _string_indices.size())) {
      Index s_idx = _string_indices[string_index.value];
      return GetToken(s_idx);
    } else {
      _err +=
          "String index out of range: " + std::to_string(string_index.value) +
          "\n";
      return std::string();
    }
  }

  const bool HasField(const std::string &key) {
    // Simple linear search
    for (const auto &field : _fields) {
      std::string field_name = GetToken(field.token_index);
      if (field_name.compare(key) == 0) {
        return true;
      }
    }
    return false;
  }

  const bool GetField(Index index, Field &&field) const {
    if ((index.value >= 0) || (index.value <= _fields.size())) {
      field = _fields[index.value];
      return true;
    } else {
      return false;
    }
  }

  std::string GetFieldString(Index index) {
    if ((index.value >= 0) || (index.value <= _fields.size())) {
      // ok
    } else {
      return "#INVALID field index#";
    }

    const Field &f = _fields[index.value];

    std::string s = GetToken(f.token_index) + ":" + f.value_rep.GetStringRepr();

    return s;
  }

  std::string GetFieldSetString(Index index) {
    if ((index.value >= 0) || (index.value <= _fieldset_indices.size())) {
      // ok
    } else {
      return "#INVALID fieldset index#";
    }

    return std::to_string(_fieldset_indices[index.value].value);
  }

  Path GetPath(Index index) {
    if ((index.value >= 0) || (index.value <= _fields.size())) {
      // ok
    } else {
      // TODO(syoyo): Report error
      return Path();
    }

    const Path &p = _paths[index.value];

    return p;
  }

  std::string GetPathString(Index index) {
    if ((index.value >= 0) || (index.value <= _fields.size())) {
      // ok
    } else {
      return "#INVALID path index#";
    }

    const Path &p = _paths[index.value];

    return p.name();
  }

  std::string GetSpecString(Index index) {
    if ((index.value >= 0) || (index.value <= _fields.size())) {
      // ok
    } else {
      return "#INVALID spec index#";
    }

    const Spec &spec = _specs[index.value];

    std::string path_str = GetPathString(spec.path_index);
    std::string fieldset_str = GetFieldSetString(spec.fieldset_index);
    std::string specty_str = GetSpecTypeString(spec.spec_type);

    return "[Spec] path: " + path_str + ", fieldset: " + fieldset_str +
           ", spec_type: " + specty_str;
  }

  bool BuildLiveFieldSets();

  // TODO PrefetchStructuralSections

  std::string GetError() { return _err; }

 private:
  bool ReadCompressedPaths(const uint64_t ref_num_paths);

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
  std::vector<Path> _paths;

  bool _BuildDecompressedPathsImpl(
      std::vector<uint32_t> const &pathIndexes,
      std::vector<int32_t> const &elementTokenIndexes,
      std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath);

  bool _UnpackValueRep(const ValueRep &rep, Value *value);

  //
  // Reader util functions
  //
  bool _ReadIndex(Index *i);

  bool _ReadToken(std::string *s);
  bool _ReadString(std::string *s);

  bool _ReadValueRep(ValueRep *rep);

  bool _ReadPathArray(std::vector<Path> *d);

  // Dictionary
  bool _ReadDictionary(Value::Dictionary *d);

  // integral array
  template <typename T>
  bool _ReadIntArray(bool is_compressed, std::vector<T> *d);

  bool _ReadHalfArray(bool is_compressed, std::vector<uint16_t> *d);
  bool _ReadFloatArray(bool is_compressed, std::vector<float> *d);
  bool _ReadDoubleArray(bool is_compressed, std::vector<double> *d);

  // PathListOp
  bool _ReadPathListOp(ListOp<Path> *d);
};

bool Parser::_ReadIndex(Index *i) {
  // string is serialized as StringIndex
  uint32_t value;
  if (!_sr->read4(&value)) {
    _err += "Failed to read Index\n";
    return false;
  }
  (*i) = Index(value);
  return true;
}

bool Parser::_ReadToken(std::string *s) {
  Index token_index;
  if (!_ReadIndex(&token_index)) {
    _err += "Failed to read Index for token data.\n";
    return false;
  }

  (*s) = GetToken(token_index);

  return true;
}

bool Parser::_ReadString(std::string *s) {
  // string is serialized as StringIndex
  Index string_index;
  if (!_ReadIndex(&string_index)) {
    _err += "Failed to read Index for string data.\n";
    return false;
  }

  (*s) = GetString(string_index);

  return true;
}

bool Parser::_ReadValueRep(ValueRep *rep) {
  if (!_sr->read8(reinterpret_cast<uint64_t *>(rep))) {
    _err += "Failed to read ValueRep.\n";
    return false;
  }

  std::cout << "value = " << rep->GetData() << "\n";

  return true;
}

template <typename T>
bool Parser::_ReadIntArray(bool is_compressed, std::vector<T> *d) {
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
    if (!_sr->read(sizeof(T) * length, sizeof(T) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read integer array data.\n";
      return false;
    }
  }

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

  std::cout << "array.len = " << length << "\n";

  d->resize(length);

  if (length < kMinCompressedArraySize) {
    size_t sz = sizeof(T) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  return _ReadCompressedInts(_sr, d->data(), d->size());
}

bool Parser::_ReadHalfArray(bool is_compressed, std::vector<uint16_t> *d) {
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

  std::cout << "array.len = " << length << "\n";

  d->resize(length);

  if (length < kMinCompressedArraySize) {
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
    if (!_ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadHalfArray.\n";
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      float f = float(ints[i]);
      float16 h = float_to_half_full(f);
      (*d)[i] = h.u;
    }
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadHalfArray.\n";
      return false;
    }

    std::vector<uint16_t> lut(lutSize);
    if (!_sr->read(sizeof(uint16_t) * lutSize, sizeof(uint16_t) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadHalfArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!_ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
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

bool Parser::_ReadFloatArray(bool is_compressed, std::vector<float> *d) {
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

  std::cout << "array.len = " << length << "\n";

  d->resize(length);

  if (length < kMinCompressedArraySize) {
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
    if (!_ReadCompressedInts(_sr, ints.data(), ints.size())) {
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
    if (!_ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
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

bool Parser::_ReadDoubleArray(bool is_compressed, std::vector<double> *d) {
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

  std::cout << "array.len = " << length << "\n";

  d->resize(length);

  if (length < kMinCompressedArraySize) {
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
    if (!_ReadCompressedInts(_sr, ints.data(), ints.size())) {
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
    if (!_ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
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

bool Parser::_ReadPathListOp(ListOp<Path> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
    std::cout << "Explicit\n";
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    size_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<Index> ivalue(n);

    if (!_sr->read(n * sizeof(Index), n * sizeof(Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(n);
    for (size_t i = 0; i < n; i++) {
      result[i] = GetPath(ivalue[i]);
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

bool Parser::_ReadDictionary(Value::Dictionary *d) {
  Value::Dictionary dict;
  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for Dictionary data.\n";
    return false;
  }

  std::cout << "# of elements in dict " << sz << "\n";

  while (sz--) {
    // key(StringIndex)
    std::string key;
    std::cout << "key before tell = " << _sr->tell() << "\n";
    if (!_ReadString(&key)) {
      _err += "Failed to read key string for Dictionary element.\n";
      return false;
    }

    std::cout << "offt before tell = " << _sr->tell() << "\n";

    // 8byte for the offset for recursive value. See _RecursiveRead() in
    // crateFile.cpp for details.
    int64_t offset{0};
    if (!_sr->read8(&offset)) {
      _err += "Failed to read the offset for value in Dictionary.\n";
      return false;
    }

    std::cout << "value offset = " << offset << "\n";

    std::cout << "tell = " << _sr->tell() << "\n";

    // -8 to compensate sizeof(offset)
    if (!_sr->seek_from_currect(offset - 8)) {
      _err +=
          "Failed to seek. Invalid offset value: " + std::to_string(offset) +
          "\n";
      return false;
    }

    std::cout << "+offset tell = " << _sr->tell() << "\n";

    std::cout << "key = " << key << "\n";

    ValueRep rep{0};
    if (!_ReadValueRep(&rep)) {
      _err += "Failed to read value for Dictionary element.\n";
      return false;
    }

    std::cout << "vrep.ty = " << rep.GetType() << "\n";
    std::cout << "vrep = " << GetValueTypeRepr(rep.GetType()) << "\n";

    Value value;
    if (!_UnpackValueRep(rep, &value)) {
      _err += "Failed to unpack value of Dictionary element.\n";
      return false;
    }

    dict[key] = value;
  }

  (*d) = dict;
  return true;
}

bool Parser::_UnpackValueRep(const ValueRep &rep, Value *value) {
  ValueType ty = GetValueType(rep.GetType());
  std::cout << GetValueTypeRepr(rep.GetType()) << "\n";
  if (rep.IsInlined()) {
    uint32_t d = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));

    std::cout << "d = " << d << "\n";
    std::cout << "ty.id = " << ty.id << "\n";
    if (ty.id == VALUE_TYPE_BOOL) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      std::cout << "Bool: " << d << "\n";

      value->SetBool(d ? true : false);

      return true;

    } else if (ty.id == VALUE_TYPE_ASSET_PATH) {
      // AssetPath = std::string(storage format is TokenIndex).

      std::string str = GetToken(Index(d));

      value->SetAssetPath(str);

      return true;

    } else if (ty.id == VALUE_TYPE_SPECIFIER) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      std::cout << "Specifier: "
                << GetSpecifierString(static_cast<Specifier>(d)) << "\n";

      if (d >= NumSpecifiers) {
        _err += "Invalid value for Specifier\n";
        return false;
      }

      value->SetSpecifier(d);

      return true;
    } else if (ty.id == VALUE_TYPE_PERMISSION) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      std::cout << "Permission: "
                << GetPermissionString(static_cast<Permission>(d)) << "\n";

      if (d >= NumPermissions) {
        _err += "Invalid value for Permission\n";
        return false;
      }

      value->SetPermission(d);

      return true;
    } else if (ty.id == VALUE_TYPE_VARIABILITY) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      std::cout << "Variability: "
                << GetVariabilityString(static_cast<Variability>(d)) << "\n";

      if (d >= NumVariabilities) {
        _err += "Invalid value for Variability\n";
        return false;
      }

      value->SetVariability(d);

      return true;
    } else if (ty.id == VALUE_TYPE_TOKEN) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      std::string str = GetToken(Index(d));
      std::cout << "value.token = " << str << "\n";

      value->SetToken(str);

      return true;

    } else if (ty.id == VALUE_TYPE_STRING) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      std::string str = GetString(Index(d));
      std::cout << "value.string = " << str << "\n";

      value->SetString(str);

      return true;

    } else if (ty.id == VALUE_TYPE_FLOAT) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      float f;
      memcpy(&f, &d, sizeof(float));

      std::cout << "value.float = " << f << "\n";

      value->SetFloat(f);

      return true;

    } else if (ty.id == VALUE_TYPE_DOUBLE) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));
      // Value is saved as float
      float f;
      memcpy(&f, &d, sizeof(float));
      double v = double(f);

      std::cout << "value.double = " << v << "\n";

      value->SetDouble(v);

      return true;

    } else if (ty.id == VALUE_TYPE_VEC3I) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Vec3i v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);

      std::cout << "value.vec3i = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";

      value->SetVec3i(v);

      return true;

    } else if (ty.id == VALUE_TYPE_VEC3F) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Vec3f v;
      v[0] = static_cast<float>(data[0]);
      v[1] = static_cast<float>(data[1]);
      v[2] = static_cast<float>(data[2]);

      std::cout << "value.vec3f = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";

      value->SetVec3f(v);

      return true;

    } else if (ty.id == VALUE_TYPE_MATRIX2D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Matrix contains diagnonal components only, and values are represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      Matrix2d v;
      memset(v.m, 0, sizeof(Matrix2d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);

      std::cout << "value.matrix(diag) = " << data[0] << ", " << data[1] << "\n";

      value->SetMatrix2d(v);

      return true;

    } else if (ty.id == VALUE_TYPE_MATRIX3D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Matrix contains diagnonal components only, and values are represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      Matrix3d v;
      memset(v.m, 0, sizeof(Matrix3d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);

      std::cout << "value.matrix(diag) = " << data[0] << ", " << data[1] << ", " << data[2] << "\n";

      value->SetMatrix3d(v);

      return true;

    } else if (ty.id == VALUE_TYPE_MATRIX4D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      // Matrix contains diagnonal components only, and values are represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      Matrix4d v;
      memset(v.m, 0, sizeof(Matrix4d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);
      v.m[3][3] = static_cast<double>(data[3]);

      std::cout << "value.matrix(diag) = " << data[0] << ", " << data[1] << ", " << data[2] << ", " << data[3] << "\n";

      value->SetMatrix4d(v);

      return true;
    } else {
      // TODO(syoyo)
      std::cerr << "TODO: Inlined Value: " << GetValueTypeRepr(rep.GetType())
                << "\n";

      return false;
    }
  } else {
    // payload is the offset to data.
    int64_t offset = rep.GetPayload();
    if (!_sr->seek_set(offset)) {
      std::cerr << "Invalid offset\n";
      return false;
    }

    printf("rep = 0x%016lx\n", rep.GetData());

    if (ty.id == VALUE_TYPE_TOKEN) {
      // Guess array of Token
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      uint64_t n;
      if (!_sr->read8(&n)) {
        std::cerr << "Failed to read the number of array elements\n";
        return false;
      }

      std::vector<Index> v(n);
      if (!_sr->read(n * sizeof(Index), n * sizeof(Index),
                     reinterpret_cast<uint8_t *>(v.data()))) {
        std::cerr << "Failed to read TokenIndex array\n";
        return false;
      }


      std::vector<std::string> tokens(n);

      for (size_t i = 0; i < n; i++) {
        std::cout << "Token[" << i << "] = " << GetToken(v[i]) << " (" << v[i].value << ")\n";
        tokens[i] = GetToken(v[i]);
      }

      value->SetTokenArray(tokens);

      return true;

    } if (ty.id == VALUE_TYPE_INT) {
      assert(rep.IsArray());

      std::vector<int32_t> v;
      if (!_ReadIntArray(rep.IsCompressed(), &v)) {
        std::cerr << "Failed to read Int array\n";
        return false;
      }

      if (v.empty()) {
        std::cerr << "Empty Int array\n";
        return false;
      }

      for (size_t i = 0; i < v.size(); i++) {
        std::cout << "Int[" << i << "] = " << v[i] << "\n";
      }

      if (rep.IsArray()) {
        value->SetIntArray(v.data(), v.size());
      } else {
        value->SetInt(v[0]);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_VEC2F) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          std::cerr << "Failed to read the number of array elements\n";
          return false;
        }

        std::vector<Vec2f> v(n);
        if (!_sr->read(n * sizeof(Vec2f), n * sizeof(Vec2f),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          std::cerr << "Failed to read Vec2f array\n";
          return false;
        }

        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Vec2f[" << i << "] = " << v[i][0] << ", " << v[i][1] << "\n";
        }

        value->SetVec2fArray(v.data(), v.size());

      } else {
        Vec2f v;
        if (!_sr->read(sizeof(Vec2f), sizeof(Vec2f),
                       reinterpret_cast<uint8_t *>(&v))) {
          std::cerr << "Failed to read Vec2f\n";
          return false;
        }

        std::cout << "Vec2f = " << v[0] << ", " << v[1] << "\n";

        value->SetVec2f(v);
      }

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3F) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          std::cerr << "Failed to read the number of array elements\n";
          return false;
        }

        std::vector<Vec3f> v(n);
        if (!_sr->read(n * sizeof(Vec3f), n * sizeof(Vec3f),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          std::cerr << "Failed to read Vec3f array\n";
          return false;
        }

        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Vec3f[" << i << "] = " << v[i][0] << ", " << v[i][1] << ", " << v[i][2] << "\n";
        }
        value->SetVec3fArray(v.data(), v.size());

      } else {
        Vec3f v;
        if (!_sr->read(sizeof(Vec3f), sizeof(Vec3f),
                       reinterpret_cast<uint8_t *>(&v))) {
          std::cerr << "Failed to read Vec3f\n";
          return false;
        }

        std::cout << "Vec3f = " << v[0] << ", " << v[1] << ", " << v[2] << "\n";

        value->SetVec3f(v);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_VEC4F) {
      assert(!rep.IsCompressed());

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          std::cerr << "Failed to read the number of array elements\n";
          return false;
        }

        std::vector<Vec4f> v(n);
        if (!_sr->read(n * sizeof(Vec4f), n * sizeof(Vec4f),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          std::cerr << "Failed to read Vec4f array\n";
          return false;
        }

        value->SetVec4fArray(v.data(), v.size());

      } else {
        Vec4f v;
        if (!_sr->read(sizeof(Vec4f), sizeof(Vec4f),
                       reinterpret_cast<uint8_t *>(&v))) {
          std::cerr << "Failed to read Vec4f\n";
          return false;
        }

        std::cout << "Vec4f = " << v[0] << ", " << v[1] << ", " << v[2] << ", "
                  << v[3] << "\n";

        value->SetVec4f(v);
      }

      return true;

    } else if (ty.id == VALUE_TYPE_TOKEN_VECTOR) {
      assert(!rep.IsCompressed());
      // std::vector<Index>
      size_t n;
      if (!_sr->read8(&n)) {
        std::cerr << "Failed to read TokenVector value\n";
        return false;
      }
      std::cout << "n = " << n << "\n";

      std::vector<Index> indices(n);
      if (!_sr->read(n * sizeof(Index), n * sizeof(Index),
                     reinterpret_cast<uint8_t *>(indices.data()))) {
        std::cerr << "Failed to read TokenVector value\n";
        return false;
      }

      for (size_t i = 0; i < indices.size(); i++) {
        std::cout << "tokenIndex[" << i << "] = " << int(indices[i].value)
                  << "\n";
      }

      std::vector<std::string> tokens(indices.size());
      for (size_t i = 0; i < indices.size(); i++) {
        tokens[i] = GetToken(indices[i]);
        std::cout << "tokenVector[" << i << "] = " << tokens[i] << ", (" << int(indices[i].value)
                  << ")\n";
      }

      value->SetTokenArray(tokens);

      return true;
    } else if (ty.id == VALUE_TYPE_HALF) {
      if (rep.IsArray()) {
        std::vector<uint16_t> v;
        if (!_ReadHalfArray(rep.IsCompressed(), &v)) {
          _err += "Failed to read half array value\n";
          return false;
        }

        value->SetHalfArray(v.data(), v.size());

        return true;
      } else {
      
        assert(!rep.IsCompressed());

        // ???
        _err += "Non-inlined, non-array Half value is not supported.\n";
        return false;
      }
    } else if (ty.id == VALUE_TYPE_FLOAT) {
      if (rep.IsArray()) {
        std::vector<float> v;
        if (!_ReadFloatArray(rep.IsCompressed(), &v)) {
          _err += "Failed to read float array value\n";
          return false;
        }

        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Float[" << i << "] = " << v[i] << "\n";
        }

        value->SetFloatArray(v.data(), v.size());

        return true;
      } else {
      
        assert(!rep.IsCompressed());

        // ???
        _err += "Non-inlined, non-array Float value is not supported.\n";
        return false;
      }

    } else if (ty.id == VALUE_TYPE_DOUBLE) {
      if (rep.IsArray()) {
        std::vector<double> v;
        if (!_ReadDoubleArray(rep.IsCompressed(), &v)) {
          _err += "Failed to read Double value\n";
          return false;
        }

        for (size_t i = 0; i < v.size(); i++) {
          std::cout << "Double[" << i << "] = " << v[i] << "\n";
        }


        value->SetDoubleArray(v.data(), v.size());

        return true;
      } else {
      
        assert(!rep.IsCompressed());

        double v;
        if (!_sr->read_double(&v)) {
          _err += "Failed to read Double value\n";
          return false;
        }

        std::cout << "Double " << v << "\n";

        value->SetDouble(v);

        return true;
      }
    } else if (ty.id == VALUE_TYPE_VEC3I) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3i v;
      if (!_sr->read(sizeof(Vec3i), sizeof(Vec3i),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3i value\n";
        return false;
      }

      std::cout << "value.vec3i = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
      value->SetVec3i(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3F) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3f v;
      if (!_sr->read(sizeof(Vec3f), sizeof(Vec3f),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3f value\n";
        return false;
      }

      std::cout << "value.vec3f = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
      value->SetVec3f(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3D) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3d v;
      if (!_sr->read(sizeof(Vec3d), sizeof(Vec3d),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3d value\n";
        return false;
      }

      std::cout << "value.vec3d = " << v[0] << ", " << v[1] << ", " << v[2]
                << "\n";
      value->SetVec3d(v);

      return true;
    } else if (ty.id == VALUE_TYPE_VEC3H) {
      assert(!rep.IsCompressed());
      assert(rep.IsArray());

      Vec3h v;
      if (!_sr->read(sizeof(Vec3h), sizeof(Vec3h),
                     reinterpret_cast<uint8_t *>(&v))) {
        _err += "Failed to read Vec3h value\n";
        return false;
      }

      std::cout << "value.vec3d = " << to_float(v[0]) << ", " << to_float(v[1])
                << ", " << to_float(v[2]) << "\n";
      value->SetVec3h(v);

      return true;
    } else if (ty.id == VALUE_TYPE_MATRIX4D) {
      assert((!rep.IsCompressed()) && (!rep.IsArray()));

      static_assert(sizeof(Matrix4d) == (8*16), "");

      Matrix4d v;
      if (!_sr->read(sizeof(Matrix4d), sizeof(Matrix4d),
                     reinterpret_cast<uint8_t *>(v.m))) {
        _err += "Failed to read Matrix4d value\n";
        return false;
      }

      std::cout << "value.matrix4d = ";
      for (size_t i = 0; i < 4; i++) {
        for (size_t j = 0; j < 4; j++) {
          std::cout << v.m[i][j];
          if ((i == 3) && (j == 3)) {
          } else {
            std::cout << ", ";
          }
        }
      }
      std::cout << "\n";

      value->SetMatrix4d(v);

      return true;

    } else if (ty.id == VALUE_TYPE_DICTIONARY) {
      assert(!rep.IsCompressed());
      assert(!rep.IsArray());

      Value::Dictionary dict;

      if (!_ReadDictionary(&dict)) {
        _err += "Failed to read Dictionary value\n";
        return false;
      }

      std::cout << "Dict. nelems = " << dict.size() << "\n";

      value->SetDictionary(dict);

      return true;

    } else if (ty.id == VALUE_TYPE_PATH_LIST_OP) {
      // SdfListOp<class SdfPath>
      // => underliying storage is the array of ListOp[PathIndex]
      ListOp<Path> lst;

      if (!_ReadPathListOp(&lst)) {
        _err += "Failed to read PathListOp data\n";
        return false;
      }

      value->SetPathListOp(lst);

      return true;

    } else {
      // TODO(syoyo)
      std::cerr << "TODO: " << GetValueTypeRepr(rep.GetType()) << "\n";
      return false;
    }
  }

  (void)value;

  return false;
}

bool Parser::_BuildDecompressedPathsImpl(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath) {
  bool hasChild = false, hasSibling = false;
  do {
    auto thisIndex = curIndex++;
    if (parentPath.IsEmpty()) {
      parentPath = Path::AbsoluteRootPath();
      _paths[pathIndexes[thisIndex]] = parentPath;
    } else {
      int32_t tokenIndex = elementTokenIndexes[thisIndex];
      bool isPrimPropertyPath = tokenIndex < 0;
      tokenIndex = std::abs(tokenIndex);

      std::cout << "tokenIndex = " << tokenIndex << "\n";
      if (tokenIndex >= int32_t(_tokens.size())) {
        _err += "Invalid tokenIndex in _BuildDecompressedPathsImpl.\n";
        return false;
      }
      auto const &elemToken = _tokens[tokenIndex];
      std::cout << "elemToken = " << elemToken << "\n";
      _paths[pathIndexes[thisIndex]] =
          isPrimPropertyPath ? parentPath.AppendProperty(elemToken)
                             : parentPath.AppendElement(elemToken);
    }

    // If we have either a child or a sibling but not both, then just
    // continue to the neighbor.  If we have both then spawn a task for the
    // sibling and do the child ourself.  We think that our path trees tend
    // to be broader more often than deep.

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        // TODO(syoyo) parallel processing?
        auto siblingIndex = thisIndex + jumps[thisIndex];
        if (!_BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes,
                                         jumps, siblingIndex, parentPath)) {
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

bool Parser::ReadCompressedPaths(const uint64_t ref_num_paths) {
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

  std::cout << "numPaths : " << numPaths << "\n";

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

    std::cout << "comBuffer.size = " << compBuffer.size() << "\n";
    std::cout << "pathIndexesSize = " << pathIndexesSize << "\n";

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), pathIndexesSize, pathIndexes.data(), numPaths, &err,
        workingSpace.data());
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
      _err += "Failed to decode elementTokenIndexes\n" + err;
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
      _err += "Failed to decode jumps\n" + err;
      return false;
    }
  }

  _paths.resize(numPaths);

  // Now build the paths.
  if (!_BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps, 0,
                                   Path())) {
    return false;
  }

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

  std::cout << "num_fields = " << num_fields << "\n";
  for (size_t i = 0; i < num_fields; i++) {
    std::cout << "field[" << i
              << "] name = " << GetToken(_fields[i].token_index)
              << ", value = " << _fields[i].value_rep.GetStringRepr() << "\n";
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

bool Parser::BuildLiveFieldSets() {
  // In-memory storage for a single "spec" -- prim, property, etc.
  typedef std::pair<std::string, Value> FieldValuePair;
  typedef std::vector<FieldValuePair> FieldValuePairVector;

  // TODO(syoyo): Use unordered_map(need hash function)
  std::map<Index, FieldValuePairVector> live_fieldsets;

  for (auto fsBegin = _fieldset_indices.begin(),
            fsEnd = std::find(fsBegin, _fieldset_indices.end(), Index());
       fsBegin != _fieldset_indices.end(); fsBegin = fsEnd + 1,
            fsEnd = std::find(fsBegin, _fieldset_indices.end(), Index())) {
    auto &pairs = live_fieldsets[Index(fsBegin - _fieldset_indices.begin())];

    pairs.resize(fsEnd - fsBegin);
    std::cout << "range size = " << (fsEnd - fsBegin) << "\n";
    for (size_t i = 0; fsBegin != fsEnd; ++fsBegin, ++i) {
      assert((fsBegin->value >= 0) && (fsBegin->value < _fields.size()));
      std::cout << "fieldIndex = " << (fsBegin->value) << "\n";
      auto const &field = _fields[fsBegin->value];
      pairs[i].first = GetToken(field.token_index);
      // TODO(syoyo) Unpack
      if (!_UnpackValueRep(field.value_rep, &pairs[i].second)) {
        std::cerr << "Failed to unpack ValueRep : "
                  << field.value_rep.GetStringRepr() << "\n";
        return false;
      }
    }
  }

  size_t sum = 0;
  for (const auto &item : live_fieldsets) {
    std::cout << "livefieldsets[" << item.first.value
              << "].count = " << item.second.size() << "\n";
    sum += item.second.size();
  }
  std::cout << "Total fields used = " << sum << "\n";

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

    std::string err;
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), path_indexes_size, tmp.data(), num_specs, &err,
            working_space.data())) {
      _err += "Failed to decode pathIndexes at `SPECS` section.\n";
      _err += err;
      return false;
    }

    for (size_t i = 0; i < num_specs; ++i) {
      std::cout << "tmp = " << tmp[i] << "\n";
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

    std::string err;
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), fset_indexes_size, tmp.data(), num_specs, &err,
            working_space.data())) {
      _err += "Failed to decode fieldset indices at `SPECS` section.\n";
      _err += err;
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      std::cout << "fieldset = " << tmp[i] << "\n";
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

    std::string err;
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), spectype_size, tmp.data(), num_specs, &err,
            working_space.data())) {
      _err += "Failed to decode fieldset indices at `SPECS` section.\n";
      _err += err;
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      std::cout << "spectype = " << tmp[i] << "\n";
      _specs[i].spec_type = static_cast<SpecType>(tmp[i]);
    }
  }

  for (size_t i = 0; i != num_specs; ++i) {
    std::cout << "spec[" << i << "].pathIndex  = " << _specs[i].path_index.value
              << ", fieldset_index = " << _specs[i].fieldset_index.value
              << ", spec_type = " << _specs[i].spec_type << "\n";
    std::cout << "spec[" << i << "] string_repr = " << GetSpecString(Index(i))
              << "\n";
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

  uint64_t num_paths;
  if (!_sr->read8(&num_paths)) {
    _err += "Failed to read # of paths at `PATHS` section.\n";
    return false;
  }

  if (!ReadCompressedPaths(num_paths)) {
    _err += "Failed to read compressed paths.\n";
    return false;
  }

  std::cout << "# of paths " << _paths.size() << "\n";

  for (size_t i = 0; i < _paths.size(); i++) {
    std::cout << "path[" << i << "] = " << _paths[i].name() << "\n";
  }

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

bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options) {
  bool swap_endian = false;  // @FIXME

  StreamReader sr(addr, length, swap_endian);

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

  if (!parser.ReadPaths()) {
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

  ///
  /// Reconstruct C++ representation of USD scene graph.
  ///
  if (!parser.BuildLiveFieldSets()) {
    if (err) {
      (*err) = parser.GetError();
    }
  }

  return true;
}

bool LoadUSDCFromFile(const std::string &filename, std::string *warn,
                      std::string *err, const USDLoadOptions &options) {
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

  return LoadUSDCFromMemory(data.data(), data.size(), warn, err, options);
}

namespace {

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of(".") != std::string::npos)
    return filename.substr(filename.find_last_of(".") + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 // static_cast<int(*)(int)>(std::tolower)         // wrong
                 // [](int c){ return std::tolower(c); }           // wrong
                 // [](char c){ return std::tolower(c); }          // wrong
                 [](unsigned char c) { return std::tolower(c); }  // correct
  );
  return s;
}

}  // namespace

bool LoadUSDZFromFile(const std::string &filename, std::string *warn,
                      std::string *err, const USDLoadOptions &options) {
  // <filename, byte_begin, byte_end>
  std::vector<std::tuple<std::string, size_t, size_t>> assets;

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

    if (sz < (11 * 8) + 30) {  // 88 for USDC header, 30 for ZIP header
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a USDZ : \"" +
            filename + "\"\n";
      }
      return false;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  size_t offset = 0;
  while ((offset + 30) < data.size()) {
    //
    // PK zip format:
    // https://users.cs.jmu.edu/buchhofp/forensics/formats/pkzip.html
    //
    std::vector<char> local_header(30);
    memcpy(local_header.data(), data.data() + offset, 30);

    // if we've reached the global header, stop reading
    if (local_header[2] != 0x03 || local_header[3] != 0x04) break;

    offset += 30;

    // read in the variable name
    uint16_t name_len = *(uint16_t *)&local_header[26];
    if ((offset + name_len) > data.size()) {
      if (err) {
        (*err) += "Invalid ZIP data\n";
      }
      return false;
    }

    std::string varname(name_len, ' ');
    memcpy(&varname[0], data.data() + offset, name_len);

    offset += name_len;

    std::cout << "varname = " << varname << "\n";

    // read in the extra field
    uint16_t extra_field_len = *(uint16_t *)&local_header[28];
    if (extra_field_len > 0) {
      if (offset + extra_field_len > data.size()) {
        if (err) {
          (*err) += "Invalid extra field length in ZIP data\n";
        }
        return false;
      }
    }

    offset += extra_field_len;

    uint16_t compr_method = *reinterpret_cast<uint16_t *>(&local_header[0] + 8);
    // uint32_t compr_bytes = *reinterpret_cast<uint32_t*>(&local_header[0]+18);
    uint32_t uncompr_bytes =
        *reinterpret_cast<uint32_t *>(&local_header[0] + 22);

    // USDZ only supports uncompressed ZIP
    if (compr_method != 0) {
      if (err) {
        (*err) += "Compressed ZIP is not supported for USDZ\n";
      }
      return false;
    }

    // [offset, uncompr_bytes]
    assets.push_back({varname, offset, offset + uncompr_bytes});

    offset += uncompr_bytes;
  }

  for (size_t i = 0; i < assets.size(); i++) {
    std::cout << "[" << i << "] " << std::get<0>(assets[i]) << " : byte range ("
              << std::get<1>(assets[i]) << ", " << std::get<2>(assets[i])
              << ")\n";
  }

  int32_t usdc_index = -1;
  {
    bool warned = false;  // to report single warning message.
    for (size_t i = 0; i < assets.size(); i++) {
      std::string ext = str_tolower(GetFileExtension(std::get<0>(assets[i])));
      if (ext.compare("usdc") == 0) {
        if ((usdc_index > -1) && (!warned)) {
          if (warn) {
            (*warn) +=
                "Multiple USDC files were found in USDZ. Use the first found "
                "one: " +
                std::get<0>(assets[size_t(usdc_index)]) + "]\n";
          }
          warned = true;
        }

        if (usdc_index == -1) {
          usdc_index = int32_t(i);
        }
      }
    }
  }

  if (usdc_index == -1) {
    if (err) {
      (*err) += "USDC file not found in USDZ\n";
    }
    return false;
  }

  {
    const size_t start_addr = std::get<1>(assets[usdc_index]);
    const size_t end_addr = std::get<2>(assets[usdc_index]);
    const size_t usdc_size = end_addr - start_addr;
    const uint8_t *usdc_addr = &data[start_addr];
    bool ret = LoadUSDCFromMemory(usdc_addr, usdc_size, warn, err, options);

    if (!ret) {
      if (err) {
        (*err) += "Failed to load USDC.\n";
      }

      return false;
    }
  }

  return true;
}

static_assert(sizeof(Field) == 16, "");
static_assert(sizeof(Spec) == 12, "");
static_assert(sizeof(Index) == 4, "");
static_assert(sizeof(Vec4h) == 8, "");
static_assert(sizeof(Vec4f) == 16, "");
static_assert(sizeof(Vec4d) == 32, "");

}  // namespace tinyusdz
