// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Array and compound value types for TinyUSDZ
// Part of the value-types.hh modularization effort

#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <type_traits>

#include "typed-array.hh"
#include "value-core-types.hh"
#include "value-math-types.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {
namespace value {

// Forward declarations
class Value;

// Array type wrapper for USD array values
template <typename T>
class ArrayValue {
 public:
  using value_type = T;
  using container_type = std::vector<T>;
  
  ArrayValue() = default;
  explicit ArrayValue(const std::vector<T> &v) : values_(v) {}
  explicit ArrayValue(std::vector<T> &&v) : values_(std::move(v)) {}
  ArrayValue(size_t n, const T &val) : values_(n, val) {}
  
  // Access
  const std::vector<T>& get() const { return values_; }
  std::vector<T>& get() { return values_; }
  
  size_t size() const { return values_.size(); }
  bool empty() const { return values_.empty(); }
  
  T& operator[](size_t i) { return values_[i]; }
  const T& operator[](size_t i) const { return values_[i]; }
  
  T& at(size_t i) { return values_.at(i); }
  const T& at(size_t i) const { return values_.at(i); }
  
  // Modifiers
  void push_back(const T &v) { values_.push_back(v); }
  void push_back(T &&v) { values_.push_back(std::move(v)); }
  void clear() { values_.clear(); }
  void resize(size_t n) { values_.resize(n); }
  void resize(size_t n, const T &val) { values_.resize(n, val); }
  void reserve(size_t n) { values_.reserve(n); }
  
  // Iterators
  auto begin() { return values_.begin(); }
  auto end() { return values_.end(); }
  auto begin() const { return values_.begin(); }
  auto end() const { return values_.end(); }
  
  bool operator==(const ArrayValue<T> &rhs) const {
    return values_ == rhs.values_;
  }
  
  bool operator!=(const ArrayValue<T> &rhs) const {
    return !(*this == rhs);
  }

 private:
  std::vector<T> values_;
};

// Common array types
using BoolArray = ArrayValue<bool>;
using CharArray = ArrayValue<char>;
using UCharArray = ArrayValue<unsigned char>;
using IntArray = ArrayValue<int32_t>;
using UIntArray = ArrayValue<uint32_t>;
using Int64Array = ArrayValue<int64_t>;
using UInt64Array = ArrayValue<uint64_t>;
using HalfArray = ArrayValue<half>;
using FloatArray = ArrayValue<float>;
using DoubleArray = ArrayValue<double>;
using StringArray = ArrayValue<std::string>;
using TokenArray = ArrayValue<token>;
using AssetPathArray = ArrayValue<AssetPath>;

// Vector array types
using Float2Array = ArrayValue<float2>;
using Float3Array = ArrayValue<float3>;
using Float4Array = ArrayValue<float4>;
using Double2Array = ArrayValue<double2>;
using Double3Array = ArrayValue<double3>;
using Double4Array = ArrayValue<double4>;

using Int2Array = ArrayValue<int2>;
using Int3Array = ArrayValue<int3>;
using Int4Array = ArrayValue<int4>;
using UInt2Array = ArrayValue<uint2>;
using UInt3Array = ArrayValue<uint3>;
using UInt4Array = ArrayValue<uint4>;

// Matrix array types
using Matrix2dArray = ArrayValue<matrix2d>;
using Matrix3dArray = ArrayValue<matrix3d>;
using Matrix4dArray = ArrayValue<matrix4d>;

// Quaternion array types
using QuathArray = ArrayValue<quath>;
using QuatfArray = ArrayValue<quatf>;
using QuatdArray = ArrayValue<quatd>;

// Normal/Point/Vector array types
using Normal3hArray = ArrayValue<normal3h>;
using Normal3fArray = ArrayValue<normal3f>;
using Normal3dArray = ArrayValue<normal3d>;
using Point3hArray = ArrayValue<point3h>;
using Point3fArray = ArrayValue<point3f>;
using Point3dArray = ArrayValue<point3d>;
using Vector3hArray = ArrayValue<vector3h>;
using Vector3fArray = ArrayValue<vector3f>;
using Vector3dArray = ArrayValue<vector3d>;

// Color array types
using Color3hArray = ArrayValue<color3h>;
using Color3fArray = ArrayValue<color3f>;
using Color3dArray = ArrayValue<color3d>;
using Color4hArray = ArrayValue<color4h>;
using Color4fArray = ArrayValue<color4f>;
using Color4dArray = ArrayValue<color4d>;

// Texcoord array types
using TexCoord2hArray = ArrayValue<texcoord2h>;
using TexCoord2fArray = ArrayValue<texcoord2f>;
using TexCoord2dArray = ArrayValue<texcoord2d>;
using TexCoord3hArray = ArrayValue<texcoord3h>;
using TexCoord3fArray = ArrayValue<texcoord3f>;
using TexCoord3dArray = ArrayValue<texcoord3d>;

// Optional array support (for sparse arrays)
template <typename T>
using OptionalArray = std::vector<nonstd::optional<T>>;

using OptionalBoolArray = OptionalArray<bool>;
using OptionalIntArray = OptionalArray<int32_t>;
using OptionalFloatArray = OptionalArray<float>;
using OptionalDoubleArray = OptionalArray<double>;
using OptionalFloat3Array = OptionalArray<float3>;
using OptionalDouble3Array = OptionalArray<double3>;

// Type-erased array holder (can hold any array type)
class AnyArray {
 public:
  AnyArray() = default;
  
  template <typename T>
  explicit AnyArray(const ArrayValue<T> &arr) 
      : data_(std::make_shared<ArrayValue<T>>(arr)) {}
  
  template <typename T>
  explicit AnyArray(ArrayValue<T> &&arr)
      : data_(std::make_shared<ArrayValue<T>>(std::move(arr))) {}
  
  bool empty() const { return !data_; }
  
  template <typename T>
  const ArrayValue<T>* as() const {
    return dynamic_cast<const ArrayValue<T>*>(data_.get());
  }
  
  template <typename T>
  ArrayValue<T>* as() {
    return dynamic_cast<ArrayValue<T>*>(data_.get());
  }

 private:
  std::shared_ptr<void> data_;
};

// 2D Array support
template <typename T>
class Array2D {
 public:
  Array2D() : rows_(0), cols_(0) {}
  Array2D(size_t rows, size_t cols) 
      : rows_(rows), cols_(cols), data_(rows * cols) {}
  Array2D(size_t rows, size_t cols, const T &val)
      : rows_(rows), cols_(cols), data_(rows * cols, val) {}
  
  T& operator()(size_t r, size_t c) {
    return data_[r * cols_ + c];
  }
  
  const T& operator()(size_t r, size_t c) const {
    return data_[r * cols_ + c];
  }
  
  size_t rows() const { return rows_; }
  size_t cols() const { return cols_; }
  size_t size() const { return data_.size(); }
  bool empty() const { return data_.empty(); }
  
  void resize(size_t rows, size_t cols) {
    rows_ = rows;
    cols_ = cols;
    data_.resize(rows * cols);
  }
  
  void resize(size_t rows, size_t cols, const T &val) {
    rows_ = rows;
    cols_ = cols;
    data_.resize(rows * cols, val);
  }
  
  const std::vector<T>& data() const { return data_; }
  std::vector<T>& data() { return data_; }
  
  bool operator==(const Array2D<T> &rhs) const {
    return (rows_ == rhs.rows_) && 
           (cols_ == rhs.cols_) && 
           (data_ == rhs.data_);
  }
  
  bool operator!=(const Array2D<T> &rhs) const {
    return !(*this == rhs);
  }

 private:
  size_t rows_;
  size_t cols_;
  std::vector<T> data_;
};

// Common 2D array types
using FloatArray2D = Array2D<float>;
using DoubleArray2D = Array2D<double>;
using IntArray2D = Array2D<int32_t>;

// Chunked array for very large arrays
template <typename T>
using ChunkedArray = ChunkedTypedArray<T>;

// Array dimension helper
constexpr uint32_t ARRAY_DIM_1D = 1;
constexpr uint32_t ARRAY_DIM_2D = 2;
constexpr uint32_t ARRAY_DIM_3D = 3;
constexpr uint32_t ARRAY_DIM_4D = 4;

// Helper to check if type is an array type
template <typename T>
struct is_array_type : std::false_type {};

template <typename T>
struct is_array_type<ArrayValue<T>> : std::true_type {};

template <typename T>
struct is_array_type<Array2D<T>> : std::true_type {};

template <typename T>
struct is_array_type<std::vector<T>> : std::true_type {};

// Get element type from array type
template <typename T>
struct array_element_type {
  using type = T;
};

template <typename T>
struct array_element_type<ArrayValue<T>> {
  using type = T;
};

template <typename T>
struct array_element_type<Array2D<T>> {
  using type = T;
};

template <typename T>
struct array_element_type<std::vector<T>> {
  using type = T;
};

} // namespace value
} // namespace tinyusdz