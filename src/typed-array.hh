// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Syoyo Fujita.
// Copyright 2025 - Present, Light Transport Entertainment Inc.

///
/// @file typed-array.hh
/// @brief TypedArray - A type-safe wrapper around Buffer with std::vector-like interface
///
/// TypedArray provides a type-safe container that uses Buffer for storage.
/// - Copyable and moveable
/// - No deduplication
/// - Provides common std::vector methods
///

#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "buffer-util.hh"

namespace tinyusdz {

///
/// TypedArray - A type-safe container backed by Buffer storage
///
/// @tparam T The element type to store
///
template <typename T>
class TypedArray {
 public:
  // Type aliases (similar to std::vector)
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  ///
  /// Default constructor - creates empty array
  ///
  TypedArray() = default;

  ///
  /// Destructor
  ///
  ~TypedArray() = default;

  ///
  /// Constructor with size - creates array with count default-constructed elements
  ///
  explicit TypedArray(size_type count) {
    resize(count);
  }

  ///
  /// Constructor with size and default value
  ///
  TypedArray(size_type count, const T& value) {
    resize(count, value);
  }

  ///
  /// Constructor from initializer list
  ///
  TypedArray(std::initializer_list<T> init) {
    reserve(init.size());
    for (const auto& item : init) {
      push_back(item);
    }
  }

  ///
  /// Constructor from raw data pointer (copies data)
  ///
  TypedArray(const T* data_ptr, size_type count) {
    if (data_ptr && count > 0) {
      _buffer.resize(count * sizeof(T));
      std::memcpy(_buffer.data(), data_ptr, count * sizeof(T));
    }
  }

  ///
  /// Copy constructor
  ///
  TypedArray(const TypedArray& other) : _buffer(other._buffer) {}

  ///
  /// Copy assignment operator
  ///
  TypedArray& operator=(const TypedArray& other) {
    if (this != &other) {
      _buffer = other._buffer;
    }
    return *this;
  }

  ///
  /// Move constructor
  ///
  TypedArray(TypedArray&& other) noexcept
      : _buffer(std::move(other._buffer)) {
  }

  ///
  /// Move assignment operator
  ///
  TypedArray& operator=(TypedArray&& other) noexcept {
    if (this != &other) {
      _buffer = std::move(other._buffer);
    }
    return *this;
  }

  //
  // Storage information
  //

  ///
  /// Check if storage is contiguous (always true for Buffer)
  ///
  static constexpr bool is_contiguous() noexcept {
    return true;
  }

  //
  // Capacity methods
  //

  ///
  /// Check if array is empty
  ///
  bool empty() const noexcept {
    return _buffer.empty();
  }

  ///
  /// Get number of elements
  ///
  size_type size() const noexcept {
    return _buffer.size() / sizeof(T);
  }

  ///
  /// Get number of elements that can be held in currently allocated storage
  ///
  size_type capacity() const noexcept {
    return _buffer.capacity() / sizeof(T);
  }

  ///
  /// Reserve storage for at least new_cap elements
  ///
  void reserve(size_type new_cap) {
    _buffer.reserve(new_cap * sizeof(T));
  }

  ///
  /// Reduce capacity to fit size
  ///
  void shrink_to_fit() {
    _buffer.shrink_to_fit();
  }

  //
  // Modifiers
  //

  ///
  /// Clear contents (size becomes 0, capacity unchanged)
  ///
  void clear() noexcept {
    _buffer.clear();
  }

  ///
  /// Resize to contain count elements
  /// If current size is less than count, additional default-constructed elements are appended
  /// If current size is greater than count, the array is reduced to first count elements
  ///
  void resize(size_type count) {
    size_type old_size = size();
    _buffer.resize(count * sizeof(T));

    // Default-construct new elements if expanding
    if (count > old_size) {
      T* ptr = data();
      for (size_type i = old_size; i < count; ++i) {
        new (ptr + i) T();
      }
    }
  }

  ///
  /// Resize to contain count elements with specified value
  ///
  void resize(size_type count, const T& value) {
    size_type old_size = size();
    _buffer.resize(count * sizeof(T));

    // Copy-construct new elements if expanding
    if (count > old_size) {
      T* ptr = data();
      for (size_type i = old_size; i < count; ++i) {
        new (ptr + i) T(value);
      }
    }
  }

  ///
  /// Add element to the end
  ///
  void push_back(const T& value) {
    size_type old_size = size();
    _buffer.resize((old_size + 1) * sizeof(T));
    new (data() + old_size) T(value);
  }

  ///
  /// Add element to the end (move version)
  ///
  void push_back(T&& value) {
    size_type old_size = size();
    _buffer.resize((old_size + 1) * sizeof(T));
    new (data() + old_size) T(std::move(value));
  }

  ///
  /// Construct element in-place at the end
  ///
  template <typename... Args>
  void emplace_back(Args&&... args) {
    size_type old_size = size();
    _buffer.resize((old_size + 1) * sizeof(T));
    new (data() + old_size) T(std::forward<Args>(args)...);
  }

  ///
  /// Remove the last element
  ///
  void pop_back() {
    if (!empty()) {
      size_type new_size = size() - 1;
      // Call destructor on the last element
      data()[new_size].~T();
      _buffer.resize(new_size * sizeof(T));
    }
  }

  //
  // Element access
  //

  ///
  /// Access element at index (no bounds checking)
  ///
  reference operator[](size_type index) noexcept {
    return data()[index];
  }

  const_reference operator[](size_type index) const noexcept {
    return data()[index];
  }

  ///
  /// Access element at index with bounds checking
  ///
  reference at(size_type index) {
    // Note: bounds checking disabled when exceptions are not available
    // Use operator[] if bounds checking is not needed
    return data()[index];
  }

  const_reference at(size_type index) const {
    // Note: bounds checking disabled when exceptions are not available
    // Use operator[] if bounds checking is not needed
    return data()[index];
  }

  ///
  /// Access first element
  ///
  reference front() noexcept {
    return data()[0];
  }

  const_reference front() const noexcept {
    return data()[0];
  }

  ///
  /// Access last element
  ///
  reference back() noexcept {
    return data()[size() - 1];
  }

  const_reference back() const noexcept {
    return data()[size() - 1];
  }

  ///
  /// Get pointer to underlying data
  ///
  pointer data() noexcept {
    return reinterpret_cast<T*>(_buffer.data());
  }

  const_pointer data() const noexcept {
    return reinterpret_cast<const T*>(_buffer.data());
  }

  //
  // Iterators
  //

  ///
  /// Get iterator to beginning
  ///
  iterator begin() noexcept {
    return data();
  }

  const_iterator begin() const noexcept {
    return data();
  }

  const_iterator cbegin() const noexcept {
    return data();
  }

  ///
  /// Get iterator to end
  ///
  iterator end() noexcept {
    return data() + size();
  }

  const_iterator end() const noexcept {
    return data() + size();
  }

  const_iterator cend() const noexcept {
    return data() + size();
  }

  //
  // Additional useful methods
  //

  ///
  /// Assign count copies of value
  ///
  void assign(size_type count, const T& value) {
    clear();
    resize(count, value);
  }

  ///
  /// Swap contents with another TypedArray
  ///
  void swap(TypedArray& other) noexcept {
    Buffer<16> temp = std::move(_buffer);
    _buffer = std::move(other._buffer);
    other._buffer = std::move(temp);
  }

  ///
  /// Get reference to underlying buffer (for advanced use)
  ///
  Buffer<16>& get_buffer() noexcept {
    return _buffer;
  }

  const Buffer<16>& get_buffer() const noexcept {
    return _buffer;
  }

  ///
  /// Get packed value for POD storage (returns pointer to this as uint64_t)
  /// Used by PODTimeSamples for storing array references
  ///
  uint64_t get_packed_value() const noexcept {
    return reinterpret_cast<uint64_t>(this);
  }

 private:
  Buffer<16> _buffer;
};

///
/// TypedArrayView - A lightweight non-owning view over typed array data
///
/// Similar to std::span (C++20) but compatible with C++14.
/// Provides read-only or mutable access to contiguous array data.
///
/// @tparam T The element type (can be const T for read-only view)
///
template <typename T>
class TypedArrayView {
 public:
  // Type aliases
  using value_type = typename std::remove_const<T>::type;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  ///
  /// Default constructor - creates empty view
  ///
  TypedArrayView() noexcept : _data(nullptr), _size(0) {}

  ///
  /// Constructor from pointer and size
  ///
  TypedArrayView(T* data_ptr, size_type count) noexcept : _data(data_ptr), _size(count) {}

  ///
  /// Constructor from TypedArray (mutable version)
  ///
  TypedArrayView(TypedArray<value_type>& array) noexcept
      : _data(array.data()), _size(array.size()) {}

  ///
  /// Constructor from TypedArray (const version)
  ///
  TypedArrayView(const TypedArray<value_type>& array) noexcept
      : _data(const_cast<value_type*>(array.data())), _size(array.size()) {}

  ///
  /// Constructor from std::vector (mutable version)
  ///
  TypedArrayView(std::vector<value_type>& vec) noexcept
      : _data(vec.data()), _size(vec.size()) {}

  ///
  /// Constructor from std::vector (const version)
  ///
  TypedArrayView(const std::vector<value_type>& vec) noexcept
      : _data(const_cast<value_type*>(vec.data())), _size(vec.size()) {}

  ///
  /// Copy constructor (default is fine - cheap to copy)
  ///
  TypedArrayView(const TypedArrayView&) noexcept = default;

  ///
  /// Copy assignment (default is fine - cheap to copy)
  ///
  TypedArrayView& operator=(const TypedArrayView&) noexcept = default;

  ///
  /// Move constructor
  ///
  TypedArrayView(TypedArrayView&&) noexcept = default;

  ///
  /// Move assignment
  ///
  TypedArrayView& operator=(TypedArrayView&&) noexcept = default;

  //
  // Capacity methods
  //

  ///
  /// Check if view is empty
  ///
  bool empty() const noexcept {
    return _size == 0;
  }

  ///
  /// Get number of elements
  ///
  size_type size() const noexcept {
    return _size;
  }

  ///
  /// Get size in bytes
  ///
  size_type size_bytes() const noexcept {
    return _size * sizeof(T);
  }

  //
  // Element access
  //

  ///
  /// Access element at index (no bounds checking)
  ///
  reference operator[](size_type index) const noexcept {
    return _data[index];
  }

  ///
  /// Access element at index with bounds checking
  ///
  reference at(size_type index) const {
    // Note: bounds checking disabled when exceptions are not available
    // Use operator[] if bounds checking is not needed
    return _data[index];
  }

  ///
  /// Access first element
  ///
  reference front() const noexcept {
    return _data[0];
  }

  ///
  /// Access last element
  ///
  reference back() const noexcept {
    return _data[_size - 1];
  }

  ///
  /// Get pointer to underlying data
  ///
  pointer data() const noexcept {
    return _data;
  }

  //
  // Iterators
  //

  ///
  /// Get iterator to beginning
  ///
  iterator begin() const noexcept {
    return _data;
  }

  const_iterator cbegin() const noexcept {
    return _data;
  }

  ///
  /// Get iterator to end
  ///
  iterator end() const noexcept {
    return _data + _size;
  }

  const_iterator cend() const noexcept {
    return _data + _size;
  }

  //
  // View operations
  //

  ///
  /// Create a subview starting at offset with count elements
  ///
  TypedArrayView subview(size_type offset, size_type count) const {
    if (offset >= _size) {
      return TypedArrayView();
    }
    size_type actual_count = (offset + count > _size) ? (_size - offset) : count;
    return TypedArrayView(_data + offset, actual_count);
  }

  ///
  /// Create a subview from offset to end
  ///
  TypedArrayView subview(size_type offset) const {
    if (offset >= _size) {
      return TypedArrayView();
    }
    return TypedArrayView(_data + offset, _size - offset);
  }

  ///
  /// Get first count elements
  ///
  TypedArrayView first(size_type count) const {
    return subview(0, count);
  }

  ///
  /// Get last count elements
  ///
  TypedArrayView last(size_type count) const {
    if (count >= _size) {
      return *this;
    }
    return TypedArrayView(_data + (_size - count), count);
  }

 private:
  T* _data;
  size_type _size;
};

// Backward compatibility aliases
template <typename T>
using TypedArrayImpl = TypedArray<T>;

template <typename T>
using ChunkedTypedArray = TypedArray<T>;

// Explicit template instantiations for commonly used types
extern template class TypedArray<uint8_t>;
extern template class TypedArray<uint16_t>;
extern template class TypedArray<uint32_t>;
extern template class TypedArray<uint64_t>;
extern template class TypedArray<int8_t>;
extern template class TypedArray<int16_t>;
extern template class TypedArray<int32_t>;
extern template class TypedArray<int64_t>;
extern template class TypedArray<float>;
extern template class TypedArray<double>;

// TypedArrayView instantiations (both const and non-const)
extern template class TypedArrayView<uint8_t>;
extern template class TypedArrayView<const uint8_t>;
extern template class TypedArrayView<uint16_t>;
extern template class TypedArrayView<const uint16_t>;
extern template class TypedArrayView<uint32_t>;
extern template class TypedArrayView<const uint32_t>;
extern template class TypedArrayView<uint64_t>;
extern template class TypedArrayView<const uint64_t>;
extern template class TypedArrayView<int8_t>;
extern template class TypedArrayView<const int8_t>;
extern template class TypedArrayView<int16_t>;
extern template class TypedArrayView<const int16_t>;
extern template class TypedArrayView<int32_t>;
extern template class TypedArrayView<const int32_t>;
extern template class TypedArrayView<int64_t>;
extern template class TypedArrayView<const int64_t>;
extern template class TypedArrayView<float>;
extern template class TypedArrayView<const float>;
extern template class TypedArrayView<double>;
extern template class TypedArrayView<const double>;

}  // namespace tinyusdz
