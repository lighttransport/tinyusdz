// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// USDC(CrateFile) format
#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <memory>
#include <cstdlib>

#include "prim-types.hh"
#include "value-type.hh"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace crate {

constexpr size_t kMinCompressedArraySize = 16;
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
  Index() : value(~0u) {}
  explicit Index(uint32_t v) : value(v) {}
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

  explicit constexpr ValueRep(uint64_t d) : data(d) {}

  constexpr ValueRep(int32_t t, bool isInlined, bool isArray, uint64_t payload)
      : data(Combine(t, isInlined, isArray, payload)) {}

  static const uint64_t IsArrayBit_ = 1ull << 63;
  static const uint64_t IsInlinedBit_ = 1ull << 62;
  static const uint64_t IsCompressedBit_ = 1ull << 61;

  static const uint64_t PayloadMask_ = ((1ull << 48) - 1);

  inline bool IsArray() const { return data & IsArrayBit_; }
  inline void SetIsArray() { data |= IsArrayBit_; }

  inline bool IsInlined() const { return data & IsInlinedBit_; }
  inline void SetIsInlined() { data |= IsInlinedBit_; }

  inline bool IsCompressed() const { return data & IsCompressedBit_; }
  inline void SetIsCompressed() { data |= IsCompressedBit_; }

  inline int32_t GetType() const {
    return static_cast<int32_t>((data >> 48) & 0xFF);
  }
  inline void SetType(int32_t t) {
    data &= ~(0xFFull << 48);                  // clear type byte in data.
    data |= (static_cast<uint64_t>(t) << 48);  // set it.
  }

  inline uint64_t GetPayload() const { return data & PayloadMask_; }

  inline void SetPayload(uint64_t payload) {
    data &= ~PayloadMask_;  // clear existing payload.
    data |= payload & PayloadMask_;
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
  static constexpr uint64_t Combine(int32_t t, bool isInlined, bool isArray,
                                     uint64_t payload) {
    return (isArray ? IsArrayBit_ : 0) | (isInlined ? IsInlinedBit_ : 0) |
           (static_cast<uint64_t>(t) << 48) | (payload & PayloadMask_);
  }

  uint64_t data;
};

struct TokenIndex : Index { using Index::Index; };
struct StringIndex : Index { using Index::Index; };
struct FieldIndex : Index { using Index::Index; };
struct FieldSetIndex : Index { using Index::Index; };
struct PathIndex : Index { using Index::Index; };

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

class CrateValue {
 public:
  typedef std::map<std::string, CrateValue> Dictionary;

  std::string GetTypeName() const;
  uint32_t GetTypeId() const;

#define SET_TYPE_SCALAR(__ty) void Set(const __ty& v) { value_ = v; }
#define SET_TYPE_1D(__ty) void Set(const std::vector<__ty> &v) { value_ = v; }

#define SET_TYPE_LIST(__FUNC) \
  __FUNC(value::half) \
  __FUNC(value::half2) \
  __FUNC(value::half3) \
  __FUNC(value::half4) \
  __FUNC(int) \
  __FUNC(value::int2) \
  __FUNC(value::int3) \
  __FUNC(value::int4) \
  __FUNC(uint32_t) \
  __FUNC(value::uint2) \
  __FUNC(value::uint3) \
  __FUNC(value::uint4) \
  __FUNC(float) \
  __FUNC(value::float2) \
  __FUNC(value::float3) \
  __FUNC(value::float4) \
  __FUNC(double) \
  __FUNC(value::double2) \
  __FUNC(value::double3) \
  __FUNC(value::double4) \
  __FUNC(value::quath) \
  __FUNC(value::quatf) \
  __FUNC(value::quatd) \
  __FUNC(value::matrix2d) \
  __FUNC(value::matrix3d) \
  __FUNC(value::matrix4d) \
  __FUNC(value::asset) \
  __FUNC(value::token) \
  __FUNC(std::string)


  SET_TYPE_SCALAR(bool)
  SET_TYPE_SCALAR(Specifier)
  SET_TYPE_SCALAR(Permission)
  SET_TYPE_SCALAR(Variability)
  SET_TYPE_SCALAR(value::dict)

  SET_TYPE_SCALAR(ListOp<value::token>)
  SET_TYPE_SCALAR(ListOp<Path>)
  SET_TYPE_SCALAR(std::vector<Path>)
  SET_TYPE_SCALAR(value::TimeSamples)
  SET_TYPE_SCALAR(Dictionary)

  SET_TYPE_LIST(SET_TYPE_SCALAR)
  SET_TYPE_LIST(SET_TYPE_1D)

  // Useful function to retrieve concrete value with type T.
  // Undefined behavior(usually will triger segmentation fault) when
  // type-mismatch. (We don't throw exception)
  template <class T>
  const T &value() const {
    return (*reinterpret_cast<const T *>(value_.value()));
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_value() const {
    if (value::TypeTrait<T>::type_id == value_.type_id()) {
      return std::move(value<T>());
    } else if (value::TypeTrait<T>::underlying_type_id == value_.underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      return std::move(value<T>());
    }
    return nonstd::nullopt;
  }

 private:
  value::any_value value_;
};



nonstd::expected<uint32_t, std::string> GetValueType(int32_t type_id);
std::string GetValueTypeString(int32_t type_id);

struct StdHashWrapper {
    template <class T>
    inline size_t operator()(const T &val) const {
        return std::hash<T>()(val);
    }
};


} // namespace crate

namespace value {

// Same macro in value-type.hh
#define DEFINE_TYPE_TRAIT(__dty, __name, __tyid, __nc)           \
  template <>                                                    \
  struct TypeTrait<__dty> {                                      \
    using value_type = __dty;                                    \
    using value_underlying_type = __dty;                         \
    static constexpr uint32_t ndim = 0; /* array dim */          \
    static constexpr uint32_t ncomp =                            \
        __nc; /* the number of components(e.g. float3 => 3) */   \
    static constexpr uint32_t type_id = __tyid;                  \
    static constexpr uint32_t underlying_type_id = __tyid;       \
    static std::string type_name() { return __name; }            \
    static std::string underlying_type_name() { return __name; } \
  }

// synonym to `value::dict` 
DEFINE_TYPE_TRAIT(crate::CrateValue::Dictionary, "dict", TYPE_ID_DICT, 1);

} // namespace value
  
} // namespace tinyusdz


