// SPDX-License-Identifier: MIT
// Copyright 2025 - Present, Light Transport Entertainment Inc.

///
/// TypedArray: A type-safe wrapper around std::vector<uint8_t> with
/// nonstd::span views Provides in-place type transformation capabilities for
/// memory-efficient operations
///

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "nonstd/span.hpp"
#include "logger.hh"

namespace tinyusdz {

template <typename T>
class TypedArray {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  // Default constructor
  TypedArray() = default;

  // Constructor with size
  explicit TypedArray(size_type count) {
    resize(count);
  }

  // Constructor with size and default value
  TypedArray(size_type count, const T& value) {
    resize(count, value);
  }

  // Constructor from initializer list
  TypedArray(std::initializer_list<T> init) {
    reserve(init.size());
    for (const auto& item : init) {
      push_back(item);
    }
  }

  // Constructor from existing data (copies data)
  TypedArray(const T* data, size_type count) {
    if (data && count > 0) {
      _storage.resize(count * sizeof(T));
      std::memcpy(_storage.data(), data, count * sizeof(T));
    }
  }

  // Copy constructor
  TypedArray(const TypedArray& other)
      : _storage(other._storage) {
  }

  // Move constructor
  TypedArray(TypedArray&& other) noexcept
      : _storage(std::move(other._storage)) {
  }

  // Copy assignment
  TypedArray& operator=(const TypedArray& other) {
    if (this != &other) {
      _storage = other._storage;
    }
    return *this;
  }

  // Move assignment
  TypedArray& operator=(TypedArray&& other) noexcept {
    if (this != &other) {
      _storage = std::move(other._storage);
    }
    return *this;
  }

  // Destructor
  ~TypedArray() = default;

  // Size operations
  size_type size() const noexcept {
    return _storage.size() / sizeof(T);
  }

  size_type capacity() const noexcept {
    return _storage.capacity() / sizeof(T);
  }

  bool empty() const noexcept {
    return _storage.empty();
  }

  size_type max_size() const noexcept {
    return _storage.max_size() / sizeof(T);
  }

  size_type memory_usage() const noexcept {
    return _storage.capacity();
  }

  // Data access
  pointer data() noexcept {
    return reinterpret_cast<pointer>(_storage.data());
  }

  const_pointer data() const noexcept {
    return reinterpret_cast<const_pointer>(_storage.data());
  }

  // Element access
  reference operator[](size_type index) { return data()[index]; }

  const_reference operator[](size_type index) const { return data()[index]; }

  reference at(size_type index) {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - just return element
#else
    if (index >= size()) {
      throw std::out_of_range("TypedArray::at: index out of range");
    }
#endif
    return data()[index];
  }

  const_reference at(size_type index) const {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - just return element
#else
    if (index >= size()) {
      throw std::out_of_range("TypedArray::at: index out of range");
    }
#endif
    return data()[index];
  }

  reference front() { return data()[0]; }

  const_reference front() const { return data()[0]; }

  reference back() { return data()[size() - 1]; }

  const_reference back() const { return data()[size() - 1]; }

  // Iterators
  iterator begin() noexcept { return data(); }

  const_iterator begin() const noexcept { return data(); }

  const_iterator cbegin() const noexcept { return data(); }

  iterator end() noexcept { return data() + size(); }

  const_iterator end() const noexcept { return data() + size(); }

  const_iterator cend() const noexcept { return data() + size(); }

  // Span views
  nonstd::span<T> span() noexcept { return nonstd::span<T>(data(), size()); }

  nonstd::span<const T> span() const noexcept {
    return nonstd::span<const T>(data(), size());
  }

  nonstd::span<T> subspan(size_type offset,
                          size_type count = static_cast<size_type>(-1)) {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - no bounds checking
#else
    if (offset > size()) {
      throw std::out_of_range("TypedArray::subspan: offset out of range");
    }
#endif
    size_type actual_count =
        (count == static_cast<size_type>(-1)) ? (size() - offset) : count;
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - no bounds checking
#else
    if (offset + actual_count > size()) {
      throw std::out_of_range(
          "TypedArray::subspan: count exceeds array bounds");
    }
#endif
    return nonstd::span<T>(data() + offset, actual_count);
  }

  nonstd::span<const T> subspan(
      size_type offset, size_type count = static_cast<size_type>(-1)) const {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - no bounds checking
#else
    if (offset > size()) {
      throw std::out_of_range("TypedArray::subspan: offset out of range");
    }
#endif
    size_type actual_count =
        (count == static_cast<size_type>(-1)) ? (size() - offset) : count;
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - no bounds checking
#else
    if (offset + actual_count > size()) {
      throw std::out_of_range(
          "TypedArray::subspan: count exceeds array bounds");
    }
#endif
    return nonstd::span<const T>(data() + offset, actual_count);
  }

  // Modifiers
  void clear() noexcept {
    _storage.clear();
  }

  void resize(size_type count) {
    _storage.resize(count * sizeof(T));
  }

  void resize(size_type count, const T& value) {
    size_type old_size = size();
    _storage.resize(count * sizeof(T));

    // Initialize new elements with the given value
    for (size_type i = old_size; i < count; ++i) {
      new (data() + i) T(value);
    }
  }

  void reserve(size_type new_capacity) {
    _storage.reserve(new_capacity * sizeof(T));
  }

  void shrink_to_fit() {
    _storage.shrink_to_fit();
  }

  void push_back(const T& value) {
    size_type old_size = size();
    resize(old_size + 1);
    data()[old_size] = value;
  }

  void push_back(T&& value) {
    size_type old_size = size();
    resize(old_size + 1);
    data()[old_size] = std::move(value);
  }

  void pop_back() {
    if (!empty()) {
      resize(size() - 1);
    }
  }

  // In-place transform function
  // Transforms from current type T to new type N
  // Requirement: sizeof(T) >= sizeof(N) for in-place operation
  template <typename N, typename Func>
  TypedArray<N> transform(Func func) const {
    static_assert(sizeof(T) >= sizeof(N),
                  "transform: source type size must be >= destination type "
                  "size for in-place operation");
    static_assert(std::is_trivially_copyable<N>::value,
                  "transform: destination type must be trivially copyable");

    TypedArray<N> result;
    if (empty()) {
      return result;
    }

    // Calculate how many elements of type N we can fit
    size_type src_count = size();
    size_type dst_count = (src_count * sizeof(T)) / sizeof(N);

    result.resize(dst_count);

    // Apply transformation
    for (size_type i = 0; i < src_count; ++i) {
      // Calculate how many N elements this T element can produce
      size_type n_elements_per_t = sizeof(T) / sizeof(N);
      for (size_type j = 0;
           j < n_elements_per_t && (i * n_elements_per_t + j) < dst_count;
           ++j) {
        size_type dst_idx = i * n_elements_per_t + j;
        func(data()[i], result.data()[dst_idx]);
      }
    }

    return result;
  }

  // Specialized transform for 1:1 type conversion (e.g., int to float)
  template <typename N, typename Func>
  TypedArray<N> transform_1to1(Func func) const {
    static_assert(
        sizeof(T) >= sizeof(N),
        "transform_1to1: source type size must be >= destination type size");
    static_assert(
        std::is_trivially_copyable<N>::value,
        "transform_1to1: destination type must be trivially copyable");

    TypedArray<N> result;
    if (empty()) {
      return result;
    }

    result.resize(size());

    for (size_type i = 0; i < size(); ++i) {
      func(data()[i], result.data()[i]);
    }

    return result;
  }

  // Enhanced transform supporting sizeof(srcTy) <= sizeof(dstTy) with
  // controlled buffer growth
  template <typename N, typename Func>
  TypedArray<N> transform_expand(Func func) const {
    static_assert(
        std::is_trivially_copyable<N>::value,
        "transform_expand: destination type must be trivially copyable");

    TypedArray<N> result;
    if (empty()) {
      return result;
    }

    size_type src_count = size();

    // Use template meta-programming instead of if constexpr for C++14
    // compatibility
    return transform_expand_impl<N>(
        func, src_count, result,
        std::integral_constant<bool, (sizeof(T) >= sizeof(N))>{});
  }

  // In-place transform with expansion (modifies current array)
  // Only works when sizeof(T) <= sizeof(N) and we have sufficient capacity
  template <typename N, typename Func>
  TypedArray<N> transform_inplace_expand(Func func) {
    static_assert(std::is_trivially_copyable<N>::value,
                  "transform_inplace_expand: destination type must be "
                  "trivially copyable");

    if (empty()) {
      TypedArray<N> result;
      return result;
    }

    size_type src_count = size();
    size_type required_bytes = src_count * sizeof(N);
    // size_type current_bytes = _storage.size();

    // Check if we can expand in-place or need reallocation
    if (required_bytes <= _storage.capacity()) {
      // Can expand in-place - transform from end to beginning to avoid
      // overwriting
      _storage.resize(required_bytes);

      // Cast storage to both source and destination types
      T* src_data = reinterpret_cast<T*>(_storage.data());
      N* dst_data = reinterpret_cast<N*>(_storage.data());

      // Transform backwards to avoid overlap issues
      for (size_type i = src_count; i > 0; --i) {
        size_type src_idx = i - 1;
        size_type dst_idx = src_idx;
        func(src_data[src_idx], dst_data[dst_idx]);
      }

      // Return TypedArray<N> that shares the same storage
      TypedArray<N> result;
      result._storage = std::move(_storage);
      _storage.clear();  // Current array is now empty
      return result;
    } else {
      // Need reallocation - use regular transform_expand
      return transform_expand<N>(func);
    }
  }

  // Transform with controlled growth - specify maximum buffer growth factor
  template <typename N, typename Func>
  TypedArray<N> transform_with_limit(Func func,
                                         double max_growth_factor = 2.0) const {
    static_assert(
        std::is_trivially_copyable<N>::value,
        "transform_with_limit: destination type must be trivially copyable");

    TypedArray<N> result;
    if (empty()) {
      return result;
    }

    size_type src_count = size();
    size_type required_bytes = src_count * sizeof(N);
    size_type current_bytes = _storage.size();

    // Check if expansion exceeds the growth limit
    double growth_ratio = static_cast<double>(required_bytes) /
                          static_cast<double>(current_bytes);
    if (growth_ratio > max_growth_factor) {
      // throw std::runtime_error("transform_with_limit: required buffer growth
      // exceeds limit");
      return result;
    }

    // Proceed with transformation
    result.resize(src_count);
    for (size_type i = 0; i < src_count; ++i) {
      func(data()[i], result.data()[i]);
    }

    return result;
  }

  // Get raw storage access (advanced usage)
  std::vector<uint8_t>& storage() noexcept { return _storage; }

  const std::vector<uint8_t>& storage() const noexcept { return _storage; }

  // Swap
  void swap(TypedArray& other) noexcept {
    _storage.swap(other._storage);
  }

  // Comparison operators
  bool operator==(const TypedArray& other) const {
    if (size() != other.size()) return false;
    if (size() == 0) return true;
    return std::memcmp(data(), other.data(), size() * sizeof(T)) == 0;
  }

  bool operator!=(const TypedArray& other) const {
    return !(*this == other);
  }

  // Hash function for use with unordered_map
  // Hashes ALL elements for correctness
  struct Hash {
    size_t operator()(const TypedArray<T>& arr) const {
      size_t hash = std::hash<size_t>{}(arr.size());

      // Hash all elements for complete hash coverage
      for (size_t i = 0; i < arr.size(); ++i) {
        // Combine hashes using boost hash_combine technique
        // with golden ratio constant for good mixing
        hash ^= std::hash<T>{}(arr[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
      }
      return hash;
    }
  };

  // Quick hash - samples only first 32 elements for performance
  // Useful for approximate hashing or when full precision isn't needed
  size_t quick_hash() const {
    size_t hash = std::hash<size_t>{}(size());
    const size_t max_sample = 32;
    const size_t num_to_hash = std::min(size(), max_sample);

    for (size_t i = 0; i < num_to_hash; ++i) {
      hash ^=
          std::hash<T>{}((*this)[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
  }

 private:
  std::vector<uint8_t> _storage;

  // Helper method implementations for C++14 compatibility (instead of if
  // constexpr)
  template <typename N, typename Func>
  TypedArray<N> transform_expand_impl(
      Func func, size_type src_count, TypedArray<N>& result,
      std::true_type /* sizeof(T) >= sizeof(N) */) const {
    // Shrinking case - can use in-place approach
    size_type dst_count = (src_count * sizeof(T)) / sizeof(N);
    result.resize(dst_count);

    for (size_type i = 0; i < src_count; ++i) {
      size_type n_elements_per_t = sizeof(T) / sizeof(N);
      for (size_type j = 0;
           j < n_elements_per_t && (i * n_elements_per_t + j) < dst_count;
           ++j) {
        size_type dst_idx = i * n_elements_per_t + j;
        func(data()[i], result.data()[dst_idx]);
      }
    }
    return result;
  }

  template <typename N, typename Func>
  TypedArray<N> transform_expand_impl(
      Func func, size_type src_count, TypedArray<N>& result,
      std::false_type /* sizeof(T) < sizeof(N) */) const {
    // Expanding case - requires buffer growth
    // Buffer grows exactly to needed size: num_items * sizeof(dstTy)
    result.resize(src_count);

    for (size_type i = 0; i < src_count; ++i) {
      func(data()[i], result.data()[i]);
    }
    return result;
  }
};


///
/// TypedArrayView: A lightweight view over typed data using nonstd::span
/// Provides zero-copy access to data stored in various containers
///
template <typename T>
class TypedArrayView {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = pointer;
  using const_iterator = const_pointer;

  // Default constructor - creates empty view
  TypedArrayView() noexcept : _span() {}

  // Constructor from raw pointer and size
  TypedArrayView(pointer data, size_type count) noexcept : _span(data, count) {}

  // Constructor from raw pointer range
  TypedArrayView(pointer first, pointer last) noexcept : _span(first, last) {}

  // Constructor from C-style array
  template <std::size_t N>
  TypedArrayView(T (&arr)[N]) noexcept : _span(arr, N) {}

  // Constructor from std::vector with type size validation
  template <typename U>
  TypedArrayView(const std::vector<U>& vec) noexcept {
    static_assert(std::is_trivially_copyable<T>::value,
                  "TypedArrayView: T must be trivially copyable");
    static_assert(std::is_trivially_copyable<U>::value,
                  "TypedArrayView: source type must be trivially copyable");

    if (sizeof(T) > sizeof(U) || vec.size() * sizeof(U) < sizeof(T)) {
      // Cannot safely view as T - create empty view
      _span = nonstd::span<T>();
    } else {
      // Safe to view as T
      size_type count = (vec.size() * sizeof(U)) / sizeof(T);
      _span = nonstd::span<T>(reinterpret_cast<const T*>(vec.data()), count);
    }
  }

  // Constructor from mutable std::vector with type size validation
  template <typename U>
  TypedArrayView(std::vector<U>& vec) noexcept {
    static_assert(std::is_trivially_copyable<T>::value,
                  "TypedArrayView: T must be trivially copyable");
    static_assert(std::is_trivially_copyable<U>::value,
                  "TypedArrayView: source type must be trivially copyable");

    if (sizeof(T) > sizeof(U) || vec.size() * sizeof(U) < sizeof(T)) {
      // Cannot safely view as T - create empty view
      _span = nonstd::span<T>();
    } else {
      // Safe to view as T
      size_type count = (vec.size() * sizeof(U)) / sizeof(T);
      _span = nonstd::span<T>(reinterpret_cast<T*>(vec.data()), count);
    }
  }

  // Constructor from TypedArray with type size validation
  template <typename U>
  TypedArrayView(const TypedArray<U>& typed_array) noexcept {
    static_assert(std::is_trivially_copyable<T>::value,
                  "TypedArrayView: T must be trivially copyable");
    static_assert(std::is_trivially_copyable<U>::value,
                  "TypedArrayView: source type must be trivially copyable");

    if (sizeof(T) > sizeof(U) || typed_array.size() * sizeof(U) < sizeof(T)) {
      // Cannot safely view as T - create empty view
      _span = nonstd::span<T>();
    } else {
      // Safe to view as T
      size_type count = (typed_array.size() * sizeof(U)) / sizeof(T);
      _span = nonstd::span<T>(reinterpret_cast<const T*>(typed_array.data()),
                              count);
    }
  }

  // Constructor from mutable TypedArray with type size validation
  template <typename U>
  TypedArrayView(TypedArray<U>& typed_array) noexcept {
    static_assert(std::is_trivially_copyable<T>::value,
                  "TypedArrayView: T must be trivially copyable");
    static_assert(std::is_trivially_copyable<U>::value,
                  "TypedArrayView: source type must be trivially copyable");

    if (sizeof(T) > sizeof(U) || typed_array.size() * sizeof(U) < sizeof(T)) {
      // Cannot safely view as T - create empty view
      _span = nonstd::span<T>();
    } else {
      // Safe to view as T
      size_type count = (typed_array.size() * sizeof(U)) / sizeof(T);
      _span = nonstd::span<T>(reinterpret_cast<T*>(typed_array.data()), count);
    }
  }

  // Constructor from nonstd::span
  explicit TypedArrayView(nonstd::span<T> sp) noexcept : _span(sp) {}

  // Copy constructor
  TypedArrayView(const TypedArrayView& other) noexcept = default;

  // Assignment operator
  TypedArrayView& operator=(const TypedArrayView& other) noexcept = default;

  // Size and capacity
  size_type size() const noexcept { return _span.size(); }

  size_type size_bytes() const noexcept { return _span.size_bytes(); }

  bool empty() const noexcept { return _span.empty(); }

  // Data access
  pointer data() noexcept { return _span.data(); }

  const_pointer data() const noexcept { return _span.data(); }

  // Element access
  reference operator[](size_type index) const { return _span[index]; }

  reference at(size_type index) const {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
    // Exceptions disabled - just return element
#else
    if (index >= size()) {
      throw std::out_of_range("TypedArrayView::at: index out of range");
    }
#endif
    return _span[index];
  }

  reference front() const { return _span.front(); }

  reference back() const { return _span.back(); }

  // Iterators
  iterator begin() const noexcept { return _span.begin(); }

  iterator end() const noexcept { return _span.end(); }

  const_iterator cbegin() const noexcept { return _span.cbegin(); }

  const_iterator cend() const noexcept { return _span.cend(); }

  // Subviews
  TypedArrayView first(size_type count) const {
    return TypedArrayView(_span.first(count));
  }

  TypedArrayView last(size_type count) const {
    return TypedArrayView(_span.last(count));
  }

  TypedArrayView subspan(size_type offset,
                         size_type count = static_cast<size_type>(-1)) const {
    size_type actual_count =
        (count == static_cast<size_type>(-1)) ? (size() - offset) : count;
    return TypedArrayView(_span.subspan(offset, actual_count));
  }

  // Get underlying span
  nonstd::span<T> span() const noexcept { return _span; }

  // Type reinterpret view (with validation)
  template <typename N>
  TypedArrayView<N> reinterpret_as() const noexcept {
    static_assert(
        std::is_trivially_copyable<N>::value,
        "reinterpret_as: destination type must be trivially copyable");

    if (sizeof(N) > sizeof(T) || size() * sizeof(T) < sizeof(N)) {
      // Cannot safely reinterpret as N - return empty view
      return TypedArrayView<N>();
    }

    // Safe to reinterpret as N
    size_type new_count = (size() * sizeof(T)) / sizeof(N);
    return TypedArrayView<N>(reinterpret_cast<const N*>(data()), new_count);
  }

  // Mutable type reinterpret view (with validation)
  template <typename N>
  TypedArrayView<N> reinterpret_as_mutable() const noexcept {
    static_assert(
        std::is_trivially_copyable<N>::value,
        "reinterpret_as_mutable: destination type must be trivially copyable");
    static_assert(!std::is_const<T>::value,
                  "reinterpret_as_mutable: source type must not be const");

    if (sizeof(N) > sizeof(T) || size() * sizeof(T) < sizeof(N)) {
      // Cannot safely reinterpret as N - return empty view
      return TypedArrayView<N>();
    }

    // Safe to reinterpret as N
    size_type new_count = (size() * sizeof(T)) / sizeof(N);
    return TypedArrayView<N>(
        reinterpret_cast<N*>(
            const_cast<typename std::remove_const<T>::type*>(data())),
        new_count);
  }

  // Check if this view is valid (non-empty and properly aligned)
  bool is_valid() const noexcept {
    return !empty() && data() != nullptr &&
           (reinterpret_cast<std::uintptr_t>(data()) % alignof(T)) == 0;
  }

  // Comparison operators
  bool operator==(const TypedArrayView& other) const noexcept {
    if (size() != other.size()) return false;
    if (data() == other.data()) return true;  // Same memory location
    return std::memcmp(data(), other.data(), size() * sizeof(T)) == 0;
  }

  bool operator!=(const TypedArrayView& other) const noexcept {
    return !(*this == other);
  }

 private:
  nonstd::span<T> _span;
};

// Non-member swap
template <typename T>
void swap(TypedArray<T>& lhs, TypedArray<T>& rhs) noexcept {
  lhs.swap(rhs);
}

// Convenience functions for creating TypedArrayView

template <typename T>
TypedArrayView<T> make_typed_array_view(T* data, std::size_t count) {
  return TypedArrayView<T>(data, count);
}

template <typename T>
TypedArrayView<const T> make_typed_array_view(const T* data,
                                              std::size_t count) {
  return TypedArrayView<const T>(data, count);
}

template <typename T, std::size_t N>
TypedArrayView<T> make_typed_array_view(T (&arr)[N]) {
  return TypedArrayView<T>(arr);
}

template <typename T>
TypedArrayView<T> make_typed_array_view(std::vector<T>& vec) {
  return TypedArrayView<T>(vec);
}

template <typename T>
TypedArrayView<const T> make_typed_array_view(const std::vector<T>& vec) {
  return TypedArrayView<const T>(vec);
}

template <typename T>
TypedArrayView<T> make_typed_array_view(TypedArray<T>& arr) {
  return TypedArrayView<T>(arr);
}

template <typename T>
TypedArrayView<const T> make_typed_array_view(const TypedArray<T>& arr) {
  return TypedArrayView<const T>(arr);
}

template <typename T>
TypedArrayView<T> make_typed_array_view(nonstd::span<T> sp) {
  return TypedArrayView<T>(sp);
}

// Type reinterpret convenience functions
template <typename T, typename U>
TypedArrayView<T> reinterpret_typed_array_view(const TypedArrayView<U>& view) {
  return view.template reinterpret_as<T>();
}

template <typename T, typename U>
TypedArrayView<T> reinterpret_typed_array_view_mutable(
    const TypedArrayView<U>& view) {
  return view.template reinterpret_as_mutable<T>();
}

// Convenience function to create TypedArray from span
template <typename T>
TypedArray<T> make_typed_array(nonstd::span<const T> sp) {
  return TypedArray<T>(sp.data(), sp.size());
}

///
/// ChunkedTypedArray: A typed array backed by chunked storage buffers
/// Provides memory-efficient storage for very large arrays by dividing storage
/// into chunks Does not provide direct data() access, only element access via
/// at() method
///
/// Supports two modes:
/// 1. Copy mode (default): Copies data into internal chunks
/// 2. MMAP mode: Stores spans to external memory without copying
///
template <typename T>
class ChunkedTypedArray {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;

  // Forward declarations for iterator classes
  class iterator;
  class const_iterator;

  // Structure to hold span information for mmap mode
  struct ChunkSpan {
    pointer data;
    size_type size;  // Size in elements (not bytes)

    ChunkSpan() : data(nullptr), size(0) {}
    ChunkSpan(pointer p, size_type s) : data(p), size(s) {}

    // Tiered chunk sizes
    static constexpr size_type CHUNK_SIZE_64KB =
        64 * 1024;  // 64KB for allocations up to 64MB
    static constexpr size_type CHUNK_SIZE_256KB =
        256 * 1024;  // 256KB for 64MB ~ 256MB
    static constexpr size_type CHUNK_SIZE_1MB =
        1024 * 1024;  // 1MB for 256MB ~ 1GB
    static constexpr size_type CHUNK_SIZE_4MB =
        4 * 1024 * 1024;  // 4MB for 1GB+

    // Allocation thresholds
    static constexpr size_type THRESHOLD_64MB = 64 * 1024 * 1024;
    static constexpr size_type THRESHOLD_256MB = 256 * 1024 * 1024;
    static constexpr size_type THRESHOLD_1GB = 1024 * 1024 * 1024;
  };

  // Iterator class for ChunkedTypedArray
  class iterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    iterator() : _array(nullptr), _index(0) {}

    iterator(ChunkedTypedArray* array, size_type index)
        : _array(array), _index(index) {}

    // Dereference
    reference operator*() const { return _array->at(_index); }

    pointer operator->() const { return &(_array->at(_index)); }

    // Array subscript
    reference operator[](difference_type n) const {
      return _array->at(_index + n);
    }

    // Increment/Decrement
    iterator& operator++() {
      ++_index;
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      ++_index;
      return tmp;
    }

    iterator& operator--() {
      --_index;
      return *this;
    }

    iterator operator--(int) {
      iterator tmp = *this;
      --_index;
      return tmp;
    }

    // Arithmetic
    iterator& operator+=(difference_type n) {
      _index += n;
      return *this;
    }

    iterator& operator-=(difference_type n) {
      _index -= n;
      return *this;
    }

    iterator operator+(difference_type n) const {
      return iterator(_array, _index + n);
    }

    iterator operator-(difference_type n) const {
      return iterator(_array, _index - n);
    }

    difference_type operator-(const iterator& other) const {
      return static_cast<difference_type>(_index) -
             static_cast<difference_type>(other._index);
    }

    // Comparison
    bool operator==(const iterator& other) const {
      return _array == other._array && _index == other._index;
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

    bool operator<(const iterator& other) const {
      return _index < other._index;
    }

    bool operator<=(const iterator& other) const {
      return _index <= other._index;
    }

    bool operator>(const iterator& other) const {
      return _index > other._index;
    }

    bool operator>=(const iterator& other) const {
      return _index >= other._index;
    }

   private:
    ChunkedTypedArray* _array;
    size_type _index;

    friend class ChunkedTypedArray::const_iterator;
  };

  // Const iterator class for ChunkedTypedArray
  class const_iterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    const_iterator() : _array(nullptr), _index(0) {}

    const_iterator(const ChunkedTypedArray* array, size_type index)
        : _array(array), _index(index) {}

    // Allow conversion from non-const iterator
    const_iterator(const iterator& it) : _array(it._array), _index(it._index) {}

    // Dereference
    reference operator*() const { return _array->at(_index); }

    pointer operator->() const { return &(_array->at(_index)); }

    // Array subscript
    reference operator[](difference_type n) const {
      return _array->at(_index + n);
    }

    // Increment/Decrement
    const_iterator& operator++() {
      ++_index;
      return *this;
    }

    const_iterator operator++(int) {
      const_iterator tmp = *this;
      ++_index;
      return tmp;
    }

    const_iterator& operator--() {
      --_index;
      return *this;
    }

    const_iterator operator--(int) {
      const_iterator tmp = *this;
      --_index;
      return tmp;
    }

    // Arithmetic
    const_iterator& operator+=(difference_type n) {
      _index += n;
      return *this;
    }

    const_iterator& operator-=(difference_type n) {
      _index -= n;
      return *this;
    }

    const_iterator operator+(difference_type n) const {
      return const_iterator(_array, _index + n);
    }

    const_iterator operator-(difference_type n) const {
      return const_iterator(_array, _index - n);
    }

    difference_type operator-(const const_iterator& other) const {
      return static_cast<difference_type>(_index) -
             static_cast<difference_type>(other._index);
    }

    // Comparison
    bool operator==(const const_iterator& other) const {
      return _array == other._array && _index == other._index;
    }

    bool operator!=(const const_iterator& other) const {
      return !(*this == other);
    }

    bool operator<(const const_iterator& other) const {
      return _index < other._index;
    }

    bool operator<=(const const_iterator& other) const {
      return _index <= other._index;
    }

    bool operator>(const const_iterator& other) const {
      return _index > other._index;
    }

    bool operator>=(const const_iterator& other) const {
      return _index >= other._index;
    }

   private:
    const ChunkedTypedArray* _array;
    size_type _index;
  };

  // Default constructor
  ChunkedTypedArray() : _total_size(0), _is_mmap_mode(false) {
    // No allocation yet, will determine chunk size when data is added
  }

  // Constructor with size (with optional custom chunk size)
  explicit ChunkedTypedArray(size_type count, size_type chunk_size_bytes = 0)
      : _total_size(count), _is_mmap_mode(false) {
    if (chunk_size_bytes != 0) {
      // User specified custom chunk size
      _chunk_size_bytes = chunk_size_bytes;
      _use_fixed_chunk_size = true;
    } else {
      // Auto-determine chunk size based on total allocation
      size_type total_bytes = count * sizeof(T);
      _chunk_size_bytes = calculate_chunk_size(total_bytes);
      _use_fixed_chunk_size = false;
    }
    _elements_per_chunk = _chunk_size_bytes / sizeof(T);
    if (_elements_per_chunk == 0) {
      _elements_per_chunk = 1;  // At least one element per chunk
    }
    allocate_chunks_for_size(count);
  }

  // MMAP mode constructor: Create from external pointer and size
  // Does not copy data, just creates spans over the memory
  ChunkedTypedArray(pointer data, size_type count,
                    size_type chunk_size_bytes = 0)
      : _total_size(count), _is_mmap_mode(true) {
    if (!data || count == 0) {
      _total_size = 0;
      return;
    }

    if (chunk_size_bytes != 0) {
      // User specified chunk size
      _chunk_size_bytes = chunk_size_bytes;
      _use_fixed_chunk_size = true;
    } else {
      // Auto-determine chunk size
      size_type total_bytes = count * sizeof(T);
      _chunk_size_bytes = calculate_chunk_size(total_bytes);
      _use_fixed_chunk_size = false;
    }
    _elements_per_chunk = _chunk_size_bytes / sizeof(T);
    if (_elements_per_chunk == 0) {
      _elements_per_chunk = 1;
    }

    // Create spans over the external data
    create_spans_from_pointer(data, count);
  }

  // MMAP mode constructor: Create from list of spans
  // Each span can have different size
  ChunkedTypedArray(const std::vector<ChunkSpan>& spans)
      : _is_mmap_mode(true), _use_fixed_chunk_size(false) {
    if (spans.empty()) {
      _total_size = 0;
      _chunk_size_bytes = 64 * 1024;  // 64KB default
      _elements_per_chunk = _chunk_size_bytes / sizeof(T);
      return;
    }

    // Store the spans and calculate total size
    _mmap_spans = spans;
    _total_size = 0;
    size_type max_chunk_elements = 0;
    for (const auto& span : spans) {
      _total_size += span.size;
      max_chunk_elements = std::max(max_chunk_elements, span.size);
    }

    // Set chunk metrics based on largest span
    _elements_per_chunk = max_chunk_elements;
    _chunk_size_bytes = _elements_per_chunk * sizeof(T);
  }

  // MMAP mode constructor: Create from initializer list of spans
  ChunkedTypedArray(std::initializer_list<ChunkSpan> spans)
      : ChunkedTypedArray(std::vector<ChunkSpan>(spans)) {}

  // Constructor with size and default value
  ChunkedTypedArray(size_type count, const T& value,
                    size_type chunk_size_bytes = 0)
      : _total_size(count), _is_mmap_mode(false) {
    if (chunk_size_bytes != 0) {
      // User specified custom chunk size
      _chunk_size_bytes = chunk_size_bytes;
      _use_fixed_chunk_size = true;
    } else {
      // Auto-determine chunk size based on total allocation
      size_type total_bytes = count * sizeof(T);
      _chunk_size_bytes = calculate_chunk_size(total_bytes);
      _use_fixed_chunk_size = false;
    }
    _elements_per_chunk = _chunk_size_bytes / sizeof(T);
    if (_elements_per_chunk == 0) {
      _elements_per_chunk = 1;  // At least one element per chunk
    }
    allocate_chunks_for_size(count);

    // Initialize all elements with the given value
    for (size_type i = 0; i < count; ++i) {
      at(i) = value;
    }
  }

  // Constructor from initializer list
  ChunkedTypedArray(std::initializer_list<T> init,
                    size_type chunk_size_bytes = 0)
      : _total_size(init.size()), _is_mmap_mode(false) {
    if (chunk_size_bytes != 0) {
      // User specified custom chunk size
      _chunk_size_bytes = chunk_size_bytes;
      _use_fixed_chunk_size = true;
    } else {
      // Auto-determine chunk size based on total allocation
      size_type total_bytes = init.size() * sizeof(T);
      _chunk_size_bytes = calculate_chunk_size(total_bytes);
      _use_fixed_chunk_size = false;
    }
    _elements_per_chunk = _chunk_size_bytes / sizeof(T);
    if (_elements_per_chunk == 0) {
      _elements_per_chunk = 1;  // At least one element per chunk
    }
    allocate_chunks_for_size(init.size());

    size_type idx = 0;
    for (const auto& item : init) {
      at(idx++) = item;
    }
  }

  // Copy constructor
  ChunkedTypedArray(const ChunkedTypedArray& other)
      : _chunk_size_bytes(other._chunk_size_bytes),
        _elements_per_chunk(other._elements_per_chunk),
        _total_size(other._total_size),
        _front_offset(other._front_offset),
        _use_fixed_chunk_size(other._use_fixed_chunk_size),
        _is_mmap_mode(other._is_mmap_mode) {
    if (_is_mmap_mode) {
      // Copy mmap spans (shallow copy - just pointers)
      _mmap_spans = other._mmap_spans;
    } else {
      // Deep copy each chunk
      _chunks.reserve(other._chunks.size());
      for (const auto& chunk : other._chunks) {
        _chunks.push_back(chunk);  // TypedArray has proper copy semantics
      }
    }
  }

  // Move constructor
  ChunkedTypedArray(ChunkedTypedArray&& other) noexcept
      : _chunks(std::move(other._chunks)),
        _mmap_spans(std::move(other._mmap_spans)),
        _chunk_size_bytes(other._chunk_size_bytes),
        _elements_per_chunk(other._elements_per_chunk),
        _total_size(other._total_size),
        _front_offset(other._front_offset),
        _use_fixed_chunk_size(other._use_fixed_chunk_size),
        _is_mmap_mode(other._is_mmap_mode) {
    other._total_size = 0;
    other._front_offset = 0;
  }

  // Copy assignment
  ChunkedTypedArray& operator=(const ChunkedTypedArray& other) {
    if (this != &other) {
      // Copy all members
      _chunk_size_bytes = other._chunk_size_bytes;
      _elements_per_chunk = other._elements_per_chunk;
      _total_size = other._total_size;
      _front_offset = other._front_offset;
      _use_fixed_chunk_size = other._use_fixed_chunk_size;
      _is_mmap_mode = other._is_mmap_mode;

      if (_is_mmap_mode) {
        // Copy mmap spans
        _chunks.clear();
        _mmap_spans = other._mmap_spans;
      } else {
        // Deep copy each chunk
        _mmap_spans.clear();
        _chunks.clear();
        _chunks.reserve(other._chunks.size());
        for (const auto& chunk : other._chunks) {
          _chunks.push_back(chunk);  // TypedArray has proper copy semantics
        }
      }
    }
    return *this;
  }

  // Move assignment
  ChunkedTypedArray& operator=(ChunkedTypedArray&& other) noexcept {
    if (this != &other) {
      _chunks = std::move(other._chunks);
      _mmap_spans = std::move(other._mmap_spans);
      _chunk_size_bytes = other._chunk_size_bytes;
      _elements_per_chunk = other._elements_per_chunk;
      _total_size = other._total_size;
      _front_offset = other._front_offset;
      _use_fixed_chunk_size = other._use_fixed_chunk_size;
      _is_mmap_mode = other._is_mmap_mode;
      other._total_size = 0;
      other._front_offset = 0;
    }
    return *this;
  }

  // Destructor
  ~ChunkedTypedArray() = default;


  // Size operations
  size_type size() const noexcept { return _total_size; }

  bool empty() const noexcept { return _total_size == 0; }

  size_type chunk_count() const noexcept {
    return _is_mmap_mode ? _mmap_spans.size() : _chunks.size();
  }

  // Check if in mmap mode (using external memory spans)
  bool is_mmap_mode() const noexcept { return _is_mmap_mode; }

  // Check if the array data is stored contiguously in memory
  // Returns true if:
  // - Array is empty
  // - In copy mode: only one chunk exists
  // - In MMAP mode: only one span OR spans are consecutive in memory
  bool is_contiguous() const noexcept {
    if (empty()) {
      return true;
    }

    if (_is_mmap_mode) {
      if (_mmap_spans.size() <= 1) {
        return true;
      }

      // Check if spans are consecutive in memory
      for (size_type i = 1; i < _mmap_spans.size(); ++i) {
        const auto& prev_span = _mmap_spans[i - 1];
        const auto& curr_span = _mmap_spans[i];

        // Check if current span starts exactly where previous one ends
        if (prev_span.data + prev_span.size != curr_span.data) {
          return false;
        }
      }
      return true;
    } else {
      // In copy mode, contiguous only if we have at most one chunk
      return _chunks.size() <= 1;
    }
  }

  // Get pointer to contiguous data (only valid if is_contiguous() returns true)
  // Returns nullptr if not contiguous or empty
  pointer data() {
    if (!is_contiguous() || empty()) {
      return nullptr;
    }

    if (_is_mmap_mode) {
      return _mmap_spans.empty() ? nullptr : _mmap_spans[0].data;
    } else {
      return _chunks.empty() ? nullptr
                             : reinterpret_cast<pointer>(_chunks[0].data());
    }
  }

  const_pointer data() const {
    if (!is_contiguous() || empty()) {
      return nullptr;
    }

    if (_is_mmap_mode) {
      return _mmap_spans.empty() ? nullptr : _mmap_spans[0].data;
    } else {
      return _chunks.empty()
                 ? nullptr
                 : reinterpret_cast<const_pointer>(_chunks[0].data());
    }
  }

  size_type chunk_size_bytes() const noexcept { return _chunk_size_bytes; }

  size_type elements_per_chunk() const noexcept { return _elements_per_chunk; }

  // Element access (main interface - no data() method provided)
  // No error checking for performance
  reference at(size_type index) {
    if (_is_mmap_mode) {
      // In mmap mode, find the span containing this index
      size_type physical_index = index - _front_offset;
      size_type current_offset = 0;
      for (auto& span : _mmap_spans) {
        if (physical_index < current_offset + span.size) {
          return span.data[physical_index - current_offset];
        }
        current_offset += span.size;
      }
      // Should not reach here if index is valid
      return _mmap_spans[0].data[0];  // Fallback to avoid undefined behavior
    } else {
      // Convert logical index to physical index
      size_type physical_index = index - _front_offset;
      size_type chunk_idx = physical_index / _elements_per_chunk;
      size_type element_idx = physical_index % _elements_per_chunk;
      return reinterpret_cast<T*>(_chunks[chunk_idx].data())[element_idx];
    }
  }

  const_reference at(size_type index) const {
    if (_is_mmap_mode) {
      // In mmap mode, find the span containing this index
      size_type physical_index = index - _front_offset;
      size_type current_offset = 0;
      for (const auto& span : _mmap_spans) {
        if (physical_index < current_offset + span.size) {
          return span.data[physical_index - current_offset];
        }
        current_offset += span.size;
      }
      // Should not reach here if index is valid
      return _mmap_spans[0].data[0];  // Fallback to avoid undefined behavior
    } else {
      // Convert logical index to physical index
      size_type physical_index = index - _front_offset;
      size_type chunk_idx = physical_index / _elements_per_chunk;
      size_type element_idx = physical_index % _elements_per_chunk;
      return reinterpret_cast<const T*>(_chunks[chunk_idx].data())[element_idx];
    }
  }

  // Operator[] for convenience - no bounds checking for performance
  reference operator[](size_type index) { return at(index); }

  const_reference operator[](size_type index) const { return at(index); }

  // Front and back access - returns pointer (nullptr if empty)
  pointer front_ptr() {
    if (empty()) return nullptr;
    return &at(_front_offset);  // First valid logical index
  }

  const_pointer front_ptr() const {
    if (empty()) return nullptr;
    return &at(_front_offset);  // First valid logical index
  }

  pointer back_ptr() {
    if (empty()) return nullptr;
    return &at(_front_offset + _total_size - 1);  // Last valid logical index
  }

  const_pointer back_ptr() const {
    if (empty()) return nullptr;
    return &at(_front_offset + _total_size - 1);  // Last valid logical index
  }

  // Front and back access - no error checking for performance
  reference front() {
    return at(_front_offset);  // First valid logical index
  }

  const_reference front() const {
    return at(_front_offset);  // First valid logical index
  }

  reference back() {
    return at(_front_offset + _total_size - 1);  // Last valid logical index
  }

  const_reference back() const {
    return at(_front_offset + _total_size - 1);  // Last valid logical index
  }

  // Iterator support
  iterator begin() { return iterator(this, _front_offset); }

  const_iterator begin() const { return const_iterator(this, _front_offset); }

  const_iterator cbegin() const { return const_iterator(this, _front_offset); }

  iterator end() { return iterator(this, _front_offset + _total_size); }

  const_iterator end() const {
    return const_iterator(this, _front_offset + _total_size);
  }

  const_iterator cend() const {
    return const_iterator(this, _front_offset + _total_size);
  }

  // Reverse iterator support
  std::reverse_iterator<iterator> rbegin() {
    return std::reverse_iterator<iterator>(end());
  }

  std::reverse_iterator<const_iterator> rbegin() const {
    return std::reverse_iterator<const_iterator>(end());
  }

  std::reverse_iterator<const_iterator> crbegin() const {
    return std::reverse_iterator<const_iterator>(cend());
  }

  std::reverse_iterator<iterator> rend() {
    return std::reverse_iterator<iterator>(begin());
  }

  std::reverse_iterator<const_iterator> rend() const {
    return std::reverse_iterator<const_iterator>(begin());
  }

  std::reverse_iterator<const_iterator> crend() const {
    return std::reverse_iterator<const_iterator>(cbegin());
  }

  // Modifiers
  void clear() noexcept {
    _chunks.clear();
    _mmap_spans.clear();
    _total_size = 0;
    _front_offset = 0;
    // Note: clear() switches mmap mode arrays back to normal mode
    _is_mmap_mode = false;
  }

  void resize(size_type count) {
    if (_is_mmap_mode) {
      // Cannot resize in mmap mode - would need to allocate new memory
      return;
    }

    if (count == _total_size) {
      return;
    }

    if (count < _total_size) {
      // Shrinking - remove unnecessary chunks
      size_type needed_chunks =
          (count + _elements_per_chunk - 1) / _elements_per_chunk;
      if (needed_chunks < _chunks.size()) {
        _chunks.resize(needed_chunks);
      }
      // Resize the last chunk if necessary
      if (needed_chunks > 0 && count > 0) {
        size_type last_chunk_elements =
            count - (needed_chunks - 1) * _elements_per_chunk;
        size_type last_chunk_bytes = last_chunk_elements * sizeof(T);
        _chunks.back().resize(last_chunk_bytes);
      }
    } else {
      // Growing - may need to recalculate chunk size for tiered allocation
      if (!_use_fixed_chunk_size && _chunks.empty()) {
        // First allocation or after clear() - recalculate chunk size
        size_type total_bytes = count * sizeof(T);
        _chunk_size_bytes = calculate_chunk_size(total_bytes);
        _elements_per_chunk = _chunk_size_bytes / sizeof(T);
        if (_elements_per_chunk == 0) {
          _elements_per_chunk = 1;
        }
      }
      // Allocate new chunks
      allocate_chunks_for_size(count);
    }
    _total_size = count;
  }

  void resize(size_type count, const T& value) {
    if (_is_mmap_mode) {
      return;
    }

    size_type old_size = _total_size;
    resize(count);

    // Initialize new elements with the given value
    for (size_type i = old_size; i < count; ++i) {
      at(i) = value;
    }
  }

  void push_back(const T& value) {
    if (_is_mmap_mode) {
      return;
    }
    resize(_total_size + 1);
    back() = value;
  }

  void push_back(T&& value) {
    if (_is_mmap_mode) {
      return;
    }
    resize(_total_size + 1);
    back() = std::move(value);
  }

  bool pop_back() {
    if (_is_mmap_mode) {
      return false;
    }
    if (empty()) {
      return false;
    }
    resize(_total_size - 1);
    return true;
  }

  // Reserve capacity (pre-allocate chunks)
  void reserve(size_type new_capacity) {
    if (_is_mmap_mode) {
      // Cannot reserve in mmap mode
      return;
    }

    if (new_capacity <= capacity()) {
      return;
    }

    size_type needed_chunks =
        (new_capacity + _elements_per_chunk - 1) / _elements_per_chunk;
    _chunks.reserve(needed_chunks);
  }

  // Get current capacity (in elements)
  size_type capacity() const noexcept {
    return _chunks.size() * _elements_per_chunk;
  }

  // Shrink chunks to fit actual size
  void shrink_to_fit() {
    if (_is_mmap_mode) {
      // Cannot shrink in mmap mode
      return;
    }

    if (empty()) {
      _chunks.clear();
      return;
    }

    // Remove excess chunks
    size_type needed_chunks =
        (_total_size + _elements_per_chunk - 1) / _elements_per_chunk;
    if (needed_chunks < _chunks.size()) {
      _chunks.resize(needed_chunks);
    }

    // Shrink the last chunk
    if (!_chunks.empty()) {
      size_type last_chunk_elements =
          _total_size - (needed_chunks - 1) * _elements_per_chunk;
      size_type last_chunk_bytes = last_chunk_elements * sizeof(T);
      _chunks.back().resize(last_chunk_bytes);
      _chunks.back().shrink_to_fit();
    }

    // Shrink chunk vector itself
    _chunks.shrink_to_fit();
  }

  // Copy data to a contiguous buffer (useful for exporting)
  // Copies the actual data in physical order (ignoring the logical offset)
  // Returns true on success, false if dest is null or array is empty
  bool copy_to(T* dest) const {
    if (empty() || !dest) {
      return false;
    }

    if (_is_mmap_mode) {
      // Copy from spans
      size_type copied = 0;
      for (const auto& span : _mmap_spans) {
        std::memcpy(dest + copied, span.data, span.size * sizeof(T));
        copied += span.size;
      }
    } else {
      // Copy from chunks
      size_type copied = 0;
      for (size_type chunk_idx = 0; chunk_idx < _chunks.size(); ++chunk_idx) {
        size_type elements_in_chunk =
            std::min(_elements_per_chunk, _total_size - copied);
        size_type bytes_to_copy = elements_in_chunk * sizeof(T);
        std::memcpy(dest + copied, _chunks[chunk_idx].data(), bytes_to_copy);
        copied += elements_in_chunk;
      }
    }
    return true;
  }

  // Copy data from a contiguous buffer
  // Replaces all current data and resets the front offset
  // Returns true on success, false if src is null
  bool copy_from(const T* src, size_type count) {
    if (!src) {
      return false;
    }

    if (count == 0) {
      clear();
      return true;
    }

    // Clear switches to copy mode if was in mmap mode
    clear();  // This resets _front_offset to 0 and _is_mmap_mode to false
    resize(count);
    size_type copied = 0;
    for (size_type chunk_idx = 0; chunk_idx < _chunks.size(); ++chunk_idx) {
      size_type elements_to_copy =
          std::min(_elements_per_chunk, count - copied);
      size_type bytes_to_copy = elements_to_copy * sizeof(T);
      std::memcpy(_chunks[chunk_idx].data(), src + copied, bytes_to_copy);
      copied += elements_to_copy;
    }
    return true;
  }

  // Get chunk at specific index (for advanced usage)
  // Returns nullptr if chunk_index is out of range or in mmap mode
  const TypedArray<uint8_t>* get_chunk(size_type chunk_index) const {
    if (_is_mmap_mode || chunk_index >= _chunks.size()) return nullptr;
    return &_chunks[chunk_index];
  }

  TypedArray<uint8_t>* get_chunk(size_type chunk_index) {
    if (_is_mmap_mode || chunk_index >= _chunks.size()) return nullptr;
    return &_chunks[chunk_index];
  }

  // Get span at specific index (for mmap mode)
  // Returns nullptr if not in mmap mode or index is out of range
  const ChunkSpan* get_span(size_type span_index) const {
    if (!_is_mmap_mode || span_index >= _mmap_spans.size()) return nullptr;
    return &_mmap_spans[span_index];
  }

  ChunkSpan* get_span(size_type span_index) {
    if (!_is_mmap_mode || span_index >= _mmap_spans.size()) return nullptr;
    return &_mmap_spans[span_index];
  }

  // Swap
  void swap(ChunkedTypedArray& other) noexcept {
    _chunks.swap(other._chunks);
    _mmap_spans.swap(other._mmap_spans);
    std::swap(_chunk_size_bytes, other._chunk_size_bytes);
    std::swap(_elements_per_chunk, other._elements_per_chunk);
    std::swap(_total_size, other._total_size);
    std::swap(_front_offset, other._front_offset);
    std::swap(_use_fixed_chunk_size, other._use_fixed_chunk_size);
    std::swap(_is_mmap_mode, other._is_mmap_mode);
  }

  // Comparison operators
  bool operator==(const ChunkedTypedArray& other) const {
    if (_total_size != other._total_size) {
      return false;
    }
    // Compare elements using physical indices (0 to _total_size-1)
    for (size_type i = 0; i < _total_size; ++i) {
      // Use physical indices for comparison (add offset to get logical index)
      if (at(_front_offset + i) != other.at(other._front_offset + i)) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const ChunkedTypedArray& other) const {
    return !(*this == other);
  }

  // Memory usage statistics
  size_type memory_usage() const noexcept {
    if (_is_mmap_mode) {
      // In mmap mode, we don't own the memory
      return 0;
    }

    size_type total = 0;
    for (const auto& chunk : _chunks) {
      total += chunk.capacity();
    }
    return total;
  }

  // Free chunk from front - removes the first chunk and adjusts indices
  // After this operation, element [0] corresponds to what was
  // [elements_per_chunk] before Returns true if a chunk was freed, false if
  // empty
  bool free_chunk_front() {
    if (_is_mmap_mode) {
      if (_mmap_spans.empty()) {
        return false;
      }

      // Remove the first span
      size_type elements_to_remove = _mmap_spans.front().size;
      _mmap_spans.erase(_mmap_spans.begin());

      // Adjust total size and offset
      _total_size -= elements_to_remove;
      _front_offset += elements_to_remove;
      return true;
    } else {
      if (_chunks.empty()) {
        return false;
      }

      // Calculate how many elements we're removing
      size_type elements_to_remove = std::min(_elements_per_chunk, _total_size);

      // Remove the first chunk
      _chunks.erase(_chunks.begin());

      // Adjust total size and offset
      _total_size -= elements_to_remove;
      _front_offset += elements_to_remove;
      return true;
    }
  }

  // Free chunk from back - removes the last chunk
  // Returns true if a chunk was freed, false if empty
  bool free_chunk_back() {
    if (_is_mmap_mode) {
      if (_mmap_spans.empty()) {
        return false;
      }

      // Remove the last span
      size_type last_chunk_elements = _mmap_spans.back().size;
      _mmap_spans.pop_back();

      // Adjust total size
      _total_size -= last_chunk_elements;
      return true;
    } else {
      if (_chunks.empty()) {
        return false;
      }

      // Calculate how many elements are in the last chunk
      size_type last_chunk_elements;
      if (_chunks.size() == 1) {
        // Only one chunk - it contains all remaining elements
        last_chunk_elements = _total_size;
      } else {
        // Multiple chunks - calculate elements in last chunk
        size_type elements_in_full_chunks =
            (_chunks.size() - 1) * _elements_per_chunk;
        last_chunk_elements = _total_size - elements_in_full_chunks;
      }

      // Remove the last chunk
      _chunks.pop_back();

      // Adjust total size
      _total_size -= last_chunk_elements;
      return true;
    }
  }

  // Get the logical index offset (for supporting free_chunk_front)
  size_type index_offset() const noexcept { return _front_offset; }

  // Get the first valid logical index
  size_type begin_index() const noexcept { return _front_offset; }

  // Get one past the last valid logical index
  size_type end_index() const noexcept { return _front_offset + _total_size; }

  // Check if a logical index is valid
  bool is_valid_index(size_type index) const noexcept {
    return index >= _front_offset && index < (_front_offset + _total_size);
  }

  // Reset the front offset (useful after multiple free_chunk_front operations)
  // NOTE: This doesn't reorganize data, just resets the logical indexing
  void reset_front_offset() noexcept { _front_offset = 0; }

 private:
  // Helper function to create spans from a contiguous memory block
  void create_spans_from_pointer(pointer data, size_type count) {
    _mmap_spans.clear();

    size_type remaining = count;
    pointer current_ptr = data;

    while (remaining > 0) {
      size_type chunk_elements = std::min(remaining, _elements_per_chunk);
      _mmap_spans.push_back(ChunkSpan(current_ptr, chunk_elements));
      current_ptr += chunk_elements;
      remaining -= chunk_elements;
    }
  }

  // Calculate appropriate chunk size based on total allocation size
  size_type calculate_chunk_size(size_type total_bytes) const {
    if (total_bytes <= 64 * 1024 * 1024) {           // <= 64MB
      return 64 * 1024;                              // 64KB chunks
    } else if (total_bytes <= 256 * 1024 * 1024) {   // <= 256MB
      return 256 * 1024;                             // 256KB chunks
    } else if (total_bytes <= 1024 * 1024 * 1024) {  // <= 1GB
      return 1024 * 1024;                            // 1MB chunks
    } else {
      return 4 * 1024 * 1024;  // 4MB chunks
    }
  }

  // Allocate chunks to accommodate the given size
  void allocate_chunks_for_size(size_type count) {
    if (count == 0) {
      _chunks.clear();
      return;
    }

    size_type needed_chunks =
        (count + _elements_per_chunk - 1) / _elements_per_chunk;

    // Allocate new chunks if needed
    while (_chunks.size() < needed_chunks) {
      _chunks.emplace_back();
      if (_chunks.size() < needed_chunks) {
        // Full chunk
        _chunks.back().resize(_elements_per_chunk * sizeof(T));
      } else {
        // Last chunk - may be partial
        size_type remaining_elements =
            count - (_chunks.size() - 1) * _elements_per_chunk;
        _chunks.back().resize(remaining_elements * sizeof(T));
      }
    }

    // Resize the last chunk if it exists and needs adjustment
    if (!_chunks.empty()) {
      size_type last_chunk_elements =
          count - (needed_chunks - 1) * _elements_per_chunk;
      size_type last_chunk_bytes = last_chunk_elements * sizeof(T);
      if (_chunks.back().size() != last_chunk_bytes) {
        _chunks.back().resize(last_chunk_bytes);
      }
    }
  }

 private:
  std::vector<TypedArray<uint8_t>>
      _chunks;  // Storage chunks using TypedArray (for copy mode)
  std::vector<ChunkSpan>
      _mmap_spans;  // Spans to external memory (for mmap mode)
  size_type _chunk_size_bytes =
      64 * 1024;  // Size of each chunk in bytes (default to 64KB)
  size_type _elements_per_chunk = 0;  // Number of T elements per chunk
  size_type _total_size;              // Total number of elements
  size_type _front_offset =
      0;  // Logical offset for indexing after free_chunk_front
  bool _use_fixed_chunk_size =
      false;                   // Whether to use fixed or tiered chunk size
  bool _is_mmap_mode = false;  // Whether using mmap mode (spans) or copy mode
};

// Convenience function to create mmap-mode ChunkedTypedArray from pointer
template <typename T>
ChunkedTypedArray<T> make_chunked_array_mmap(T* data, std::size_t count,
                                             std::size_t chunk_size_bytes = 0) {
  return ChunkedTypedArray<T>(data, count, chunk_size_bytes);
}

// Convenience function to create mmap-mode ChunkedTypedArray from spans
template <typename T>
ChunkedTypedArray<T> make_chunked_array_from_spans(
    const std::vector<typename ChunkedTypedArray<T>::ChunkSpan>& spans) {
  return ChunkedTypedArray<T>(spans);
};

// Non-member swap
template <typename T>
void swap(ChunkedTypedArray<T>& lhs, ChunkedTypedArray<T>& rhs) noexcept {
  lhs.swap(rhs);
}

// ============================================================================
// TypedArray Convenience Functions
// ============================================================================

///
/// Create TypedArray with owned copy of data
/// Copies the data into internal storage.
///
/// Example:
///   float data[] = {1.0f, 2.0f, 3.0f};
///   auto arr = MakeTypedArrayCopy(data, 3);
///
template <typename T>
inline TypedArray<T> MakeTypedArrayCopy(const T* data, size_t count) {
  return TypedArray<T>(data, count);  // Copies data
}

///
/// Create empty TypedArray with specified capacity
/// Reserves memory without initializing elements.
///
/// Example:
///   auto arr = MakeTypedArrayReserved<double>(1000);
///   for (int i = 0; i < 500; ++i) {
///       arr.push_back(i * 1.5);
///   }
///
template <typename T>
inline TypedArray<T> MakeTypedArrayReserved(size_t capacity) {
  TypedArray<T> arr;
  arr.reserve(capacity);
  return arr;
}

///
/// Deep copy an existing TypedArray
/// Creates a new independent copy with its own storage.
///
/// Example:
///   TypedArray<float> original = ...;
///   TypedArray<float> copy = DuplicateTypedArray(original);
///
template <typename T>
inline TypedArray<T> DuplicateTypedArray(const TypedArray<T>& source) {
  if (source.empty()) {
    return TypedArray<T>();
  }
  return TypedArray<T>(source.data(), source.size());
}

}  // namespace tinyusdz
