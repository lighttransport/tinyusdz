// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Type traits and type system for TinyUSDZ Value types
// Part of the value-types.hh modularization effort

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <typeinfo>

#include "value-core-types.hh"
#include "value-math-types.hh"
#include "value-array-types.hh"
#include "value-container-types.hh"

namespace tinyusdz {
namespace value {

//
// Type ID for TypeTraits<T>::type_id.
//
// These type IDs are internally used and can be changed arbitrary.
// These ID assignment won't affect Crate binary serialization.
// (See `crate-format.hh` for Type ID used in Crate binary)
//
constexpr uint32_t TYPE_ID_1D_ARRAY_BIT = 1 << 20;  // 1024
constexpr uint32_t TYPE_ID_2D_ARRAY_BIT = 1 << 21;  // 2048
constexpr uint32_t TYPE_ID_TERMINATOR_BIT = 1 << 24;

enum TypeId {
  TYPE_ID_INVALID = 0,
  TYPE_ID_NULL,
  TYPE_ID_VOID,
  TYPE_ID_MONOSTATE,
  TYPE_ID_VALUEBLOCK,  // Value block. `None` in ascii.

  // -- begin value type
  TYPE_ID_VALUE_BEGIN,
  
  TYPE_ID_TOKEN,
  TYPE_ID_STRING,
  TYPE_ID_STRING_DATA,
  
  TYPE_ID_BOOL,
  
  TYPE_ID_CHAR,
  TYPE_ID_CHAR2,
  TYPE_ID_CHAR3,
  TYPE_ID_CHAR4,
  
  TYPE_ID_UCHAR,
  TYPE_ID_UCHAR2,
  TYPE_ID_UCHAR3,
  TYPE_ID_UCHAR4,
  
  TYPE_ID_SHORT,
  TYPE_ID_SHORT2,
  TYPE_ID_SHORT3,
  TYPE_ID_SHORT4,
  
  TYPE_ID_USHORT,
  TYPE_ID_USHORT2,
  TYPE_ID_USHORT3,
  TYPE_ID_USHORT4,
  
  TYPE_ID_INT,
  TYPE_ID_INT2,
  TYPE_ID_INT3,
  TYPE_ID_INT4,
  
  TYPE_ID_UINT,
  TYPE_ID_UINT2,
  TYPE_ID_UINT3,
  TYPE_ID_UINT4,
  
  TYPE_ID_INT64,
  TYPE_ID_UINT64,
  
  TYPE_ID_HALF,
  TYPE_ID_HALF2,
  TYPE_ID_HALF3,
  TYPE_ID_HALF4,
  
  TYPE_ID_FLOAT,
  TYPE_ID_FLOAT2,
  TYPE_ID_FLOAT3,
  TYPE_ID_FLOAT4,
  
  TYPE_ID_DOUBLE,
  TYPE_ID_DOUBLE2,
  TYPE_ID_DOUBLE3,
  TYPE_ID_DOUBLE4,
  
  TYPE_ID_QUATH,
  TYPE_ID_QUATF,
  TYPE_ID_QUATD,
  
  TYPE_ID_MATRIX2D,
  TYPE_ID_MATRIX3D,
  TYPE_ID_MATRIX4D,
  
  TYPE_ID_MATRIX2F,
  TYPE_ID_MATRIX3F,
  TYPE_ID_MATRIX4F,
  
  TYPE_ID_NORMAL3H,
  TYPE_ID_NORMAL3F,
  TYPE_ID_NORMAL3D,
  
  TYPE_ID_POINT3H,
  TYPE_ID_POINT3F,
  TYPE_ID_POINT3D,
  
  TYPE_ID_VECTOR3H,
  TYPE_ID_VECTOR3F,
  TYPE_ID_VECTOR3D,
  
  TYPE_ID_COLOR3H,
  TYPE_ID_COLOR3F,
  TYPE_ID_COLOR3D,
  
  TYPE_ID_COLOR4H,
  TYPE_ID_COLOR4F,
  TYPE_ID_COLOR4D,
  
  TYPE_ID_TEXCOORD2H,
  TYPE_ID_TEXCOORD2F,
  TYPE_ID_TEXCOORD2D,
  
  TYPE_ID_TEXCOORD3H,
  TYPE_ID_TEXCOORD3F,
  TYPE_ID_TEXCOORD3D,
  
  TYPE_ID_FRAME4D,
  
  TYPE_ID_RECT2I,
  TYPE_ID_RECT2F,
  TYPE_ID_RECT2D,
  
  TYPE_ID_RANGE1I,
  TYPE_ID_RANGE1F,
  TYPE_ID_RANGE1D,
  
  TYPE_ID_RANGE2I,
  TYPE_ID_RANGE2F,
  TYPE_ID_RANGE2D,
  
  TYPE_ID_RANGE3I,
  TYPE_ID_RANGE3F,
  TYPE_ID_RANGE3D,
  
  TYPE_ID_INTERVAL,
  
  TYPE_ID_ASSET_PATH,
  TYPE_ID_TIME_CODE,
  TYPE_ID_DICT,
  TYPE_ID_TIMESAMPLES,
  TYPE_ID_VARIANT_SELECION_MAP,
  TYPE_ID_LAYER_OFFSET,
  
  // -- end value type
  TYPE_ID_VALUE_END,
  
  // ListOp types
  TYPE_ID_LIST_OP_TOKEN,
  TYPE_ID_LIST_OP_STRING,
  TYPE_ID_LIST_OP_PATH,
  TYPE_ID_LIST_OP_REFERENCE,
  TYPE_ID_LIST_OP_INT,
  TYPE_ID_LIST_OP_INT64,
  TYPE_ID_LIST_OP_UINT,
  TYPE_ID_LIST_OP_UINT64,
  TYPE_ID_LIST_OP_PAYLOAD,
  
  // Path and reference types
  TYPE_ID_PATH,
  TYPE_ID_PATH_VECTOR,
  TYPE_ID_TOKEN_VECTOR,
  TYPE_ID_REFERENCE,
  TYPE_ID_PAYLOAD,
  TYPE_ID_RELATIONSHIP,
  
  // Prim types
  TYPE_ID_SPECIFIER,
  TYPE_ID_PERMISSION,
  TYPE_ID_VARIABILITY,
  
  // Model and geometry types (defined in prim-types)
  TYPE_ID_MODEL_BEGIN = (1 << 10),
  // ... (defined elsewhere)
  TYPE_ID_MODEL_END = (1 << 14),
  
  // User-defined types
  TYPE_ID_USER_BEGIN = 1 << 16,
  
  TYPE_ID_ALL = (TYPE_ID_TERMINATOR_BIT - 1)
};

// Type traits template
template <typename T>
struct TypeTraits {
  static constexpr uint32_t type_id = TYPE_ID_INVALID;
  static constexpr const char* type_name = "invalid";
  static constexpr bool is_value_type = false;
  static constexpr bool is_array_type = false;
  static constexpr bool is_container_type = false;
  static constexpr bool is_numeric_type = false;
  static constexpr bool is_vector_type = false;
  static constexpr bool is_matrix_type = false;
};

// Macro to define type traits for simple types
#define DEFINE_TYPE_TRAIT(T, ID, NAME) \
  template <> \
  struct TypeTraits<T> { \
    static constexpr uint32_t type_id = ID; \
    static constexpr const char* type_name = NAME; \
    static constexpr bool is_value_type = true; \
    static constexpr bool is_array_type = false; \
    static constexpr bool is_container_type = false; \
    static constexpr bool is_numeric_type = false; \
    static constexpr bool is_vector_type = false; \
    static constexpr bool is_matrix_type = false; \
  }

// Define traits for core types
DEFINE_TYPE_TRAIT(bool, TYPE_ID_BOOL, kBool);
DEFINE_TYPE_TRAIT(char, TYPE_ID_CHAR, kChar);
DEFINE_TYPE_TRAIT(unsigned char, TYPE_ID_UCHAR, kUChar);
DEFINE_TYPE_TRAIT(int16_t, TYPE_ID_SHORT, kShort);
DEFINE_TYPE_TRAIT(uint16_t, TYPE_ID_USHORT, kUShort);
DEFINE_TYPE_TRAIT(int32_t, TYPE_ID_INT, kInt);
DEFINE_TYPE_TRAIT(uint32_t, TYPE_ID_UINT, kUInt);
DEFINE_TYPE_TRAIT(int64_t, TYPE_ID_INT64, kInt64);
DEFINE_TYPE_TRAIT(uint64_t, TYPE_ID_UINT64, kUInt64);
DEFINE_TYPE_TRAIT(half, TYPE_ID_HALF, kHalf);
DEFINE_TYPE_TRAIT(float, TYPE_ID_FLOAT, kFloat);
DEFINE_TYPE_TRAIT(double, TYPE_ID_DOUBLE, kDouble);
DEFINE_TYPE_TRAIT(std::string, TYPE_ID_STRING, kString);
DEFINE_TYPE_TRAIT(token, TYPE_ID_TOKEN, kToken);
DEFINE_TYPE_TRAIT(AssetPath, TYPE_ID_ASSET_PATH, kAssetPath);
DEFINE_TYPE_TRAIT(TimeCode, TYPE_ID_TIME_CODE, kTimeCode);
DEFINE_TYPE_TRAIT(StringData, TYPE_ID_STRING_DATA, "stringData");

// Define traits for vector types
DEFINE_TYPE_TRAIT(float2, TYPE_ID_FLOAT2, kFloat2);
DEFINE_TYPE_TRAIT(float3, TYPE_ID_FLOAT3, kFloat3);
DEFINE_TYPE_TRAIT(float4, TYPE_ID_FLOAT4, kFloat4);
DEFINE_TYPE_TRAIT(double2, TYPE_ID_DOUBLE2, kDouble2);
DEFINE_TYPE_TRAIT(double3, TYPE_ID_DOUBLE3, kDouble3);
DEFINE_TYPE_TRAIT(double4, TYPE_ID_DOUBLE4, kDouble4);

// Define traits for matrix types
DEFINE_TYPE_TRAIT(matrix2d, TYPE_ID_MATRIX2D, kMatrix2d);
DEFINE_TYPE_TRAIT(matrix3d, TYPE_ID_MATRIX3D, kMatrix3d);
DEFINE_TYPE_TRAIT(matrix4d, TYPE_ID_MATRIX4D, kMatrix4d);

// Define traits for quaternion types
DEFINE_TYPE_TRAIT(quath, TYPE_ID_QUATH, kQuath);
DEFINE_TYPE_TRAIT(quatf, TYPE_ID_QUATF, kQuatf);
DEFINE_TYPE_TRAIT(quatd, TYPE_ID_QUATD, kQuatd);

// Define traits for container types
DEFINE_TYPE_TRAIT(dict, TYPE_ID_DICT, kDict);
DEFINE_TYPE_TRAIT(TimeSamples, TYPE_ID_TIMESAMPLES, kTimeSamples);
DEFINE_TYPE_TRAIT(VariantSelectionMap, TYPE_ID_VARIANT_SELECION_MAP, kVariantSelectionMap);
DEFINE_TYPE_TRAIT(LayerOffset, TYPE_ID_LAYER_OFFSET, kLayerOffset);

#undef DEFINE_TYPE_TRAIT

// Array type traits
template <typename T>
struct TypeTraits<ArrayValue<T>> {
  static constexpr uint32_t type_id = TypeTraits<T>::type_id | TYPE_ID_1D_ARRAY_BIT;
  static constexpr const char* type_name = nullptr; // Dynamic based on T
  static constexpr bool is_value_type = true;
  static constexpr bool is_array_type = true;
  static constexpr bool is_container_type = false;
  static constexpr bool is_numeric_type = false;
  static constexpr bool is_vector_type = false;
  static constexpr bool is_matrix_type = false;
  
  static std::string GetTypeName() {
    return std::string(TypeTraits<T>::type_name) + "[]";
  }
};

// Helper to check numeric types
template <typename T>
struct is_numeric : std::false_type {};

template <> struct is_numeric<bool> : std::true_type {};
template <> struct is_numeric<char> : std::true_type {};
template <> struct is_numeric<unsigned char> : std::true_type {};
template <> struct is_numeric<int16_t> : std::true_type {};
template <> struct is_numeric<uint16_t> : std::true_type {};
template <> struct is_numeric<int32_t> : std::true_type {};
template <> struct is_numeric<uint32_t> : std::true_type {};
template <> struct is_numeric<int64_t> : std::true_type {};
template <> struct is_numeric<uint64_t> : std::true_type {};
template <> struct is_numeric<half> : std::true_type {};
template <> struct is_numeric<float> : std::true_type {};
template <> struct is_numeric<double> : std::true_type {};

// Helper to check vector types
template <typename T>
struct is_vector : std::false_type {};

template <> struct is_vector<float2> : std::true_type {};
template <> struct is_vector<float3> : std::true_type {};
template <> struct is_vector<float4> : std::true_type {};
template <> struct is_vector<double2> : std::true_type {};
template <> struct is_vector<double3> : std::true_type {};
template <> struct is_vector<double4> : std::true_type {};
template <> struct is_vector<half2> : std::true_type {};
template <> struct is_vector<half3> : std::true_type {};
template <> struct is_vector<half4> : std::true_type {};

// Helper to check matrix types
template <typename T>
struct is_matrix : std::false_type {};

template <typename T, size_t N>
struct is_matrix<matrix<T, N>> : std::true_type {};

// Get type ID from type name string
uint32_t GetTypeIdFromTypeName(const std::string &type_name);

// Get type name from type ID
std::string GetTypeNameFromTypeId(uint32_t type_id);

// Check if type ID represents an array type
inline bool IsArrayType(uint32_t type_id) {
  return (type_id & TYPE_ID_1D_ARRAY_BIT) != 0;
}

// Get base type ID from array type ID
inline uint32_t GetBaseTypeId(uint32_t array_type_id) {
  return array_type_id & ~(TYPE_ID_1D_ARRAY_BIT | TYPE_ID_2D_ARRAY_BIT);
}

// Check if type is a valid USD value type
inline bool IsValidValueType(uint32_t type_id) {
  return (type_id > TYPE_ID_VALUE_BEGIN) && (type_id < TYPE_ID_VALUE_END);
}

// Type visitor pattern support
template <typename Visitor, typename T>
auto visit_type(Visitor &&visitor, T &&value) 
    -> decltype(visitor(std::forward<T>(value))) {
  return visitor(std::forward<T>(value));
}

// Type erasure support
class TypeErasedValue {
 public:
  virtual ~TypeErasedValue() = default;
  virtual uint32_t GetTypeId() const = 0;
  virtual std::string GetTypeName() const = 0;
  virtual TypeErasedValue* Clone() const = 0;
  virtual bool Equals(const TypeErasedValue* other) const = 0;
};

template <typename T>
class TypedValue : public TypeErasedValue {
 public:
  explicit TypedValue(const T &val) : value_(val) {}
  explicit TypedValue(T &&val) : value_(std::move(val)) {}
  
  uint32_t GetTypeId() const override {
    return TypeTraits<T>::type_id;
  }
  
  std::string GetTypeName() const override {
    if constexpr (std::is_same_v<T, ArrayValue<typename T::value_type>>) {
      return TypeTraits<T>::GetTypeName();
    } else {
      return TypeTraits<T>::type_name;
    }
  }
  
  TypeErasedValue* Clone() const override {
    return new TypedValue<T>(value_);
  }
  
  bool Equals(const TypeErasedValue* other) const override {
    if (other->GetTypeId() != GetTypeId()) {
      return false;
    }
    auto* typed_other = static_cast<const TypedValue<T>*>(other);
    return value_ == typed_other->value_;
  }
  
  const T& GetValue() const { return value_; }
  T& GetValue() { return value_; }

 private:
  T value_;
};

} // namespace value
} // namespace tinyusdz