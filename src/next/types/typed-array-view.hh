// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - TypedArrayView
// Non-owning view to typed array data for zero-copy access

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace tinyusdz {
namespace next {

/// Non-owning view to a contiguous array of elements
/// Similar to std::span (C++20) but simpler and works with C++14
/// Used for zero-copy access to mmap'd USDC data
template <typename T>
class TypedArrayView {
public:
  using value_type = T;
  using size_type = size_t;
  using pointer = const T*;
  using const_pointer = const T*;
  using reference = const T&;
  using const_reference = const T&;
  using iterator = const T*;
  using const_iterator = const T*;

  /// Default constructor - empty view
  constexpr TypedArrayView() noexcept : data_(nullptr), size_(0) {}

  /// Construct from pointer and count
  constexpr TypedArrayView(const T* data, size_t count) noexcept
      : data_(data), size_(count) {}

  /// Construct from pointer range [begin, end)
  constexpr TypedArrayView(const T* begin, const T* end) noexcept
      : data_(begin), size_(end > begin ? static_cast<size_t>(end - begin) : 0) {}

  // ============================================================
  // Element access
  // ============================================================

  /// Get element at index (no bounds checking)
  constexpr const_reference operator[](size_type idx) const { return data_[idx]; }

  /// Get element at index with bounds checking
  const_reference at(size_type idx) const {
    if (idx >= size_) {
      // In production, this would throw or assert
      return data_[0];  // UB if empty, but matches behavior expectations
    }
    return data_[idx];
  }

  /// Get first element
  constexpr const_reference front() const { return data_[0]; }

  /// Get last element
  constexpr const_reference back() const { return data_[size_ - 1]; }

  /// Get raw pointer to data
  constexpr const_pointer data() const noexcept { return data_; }

  // ============================================================
  // Capacity
  // ============================================================

  /// Get number of elements
  constexpr size_type size() const noexcept { return size_; }

  /// Get size in bytes
  constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }

  /// Check if empty
  constexpr bool empty() const noexcept { return size_ == 0; }

  // ============================================================
  // Iterators
  // ============================================================

  constexpr const_iterator begin() const noexcept { return data_; }
  constexpr const_iterator end() const noexcept { return data_ + size_; }
  constexpr const_iterator cbegin() const noexcept { return data_; }
  constexpr const_iterator cend() const noexcept { return data_ + size_; }

  // ============================================================
  // Subviews
  // ============================================================

  /// Get first N elements
  constexpr TypedArrayView first(size_type count) const {
    return TypedArrayView(data_, count < size_ ? count : size_);
  }

  /// Get last N elements
  constexpr TypedArrayView last(size_type count) const {
    size_type n = count < size_ ? count : size_;
    return TypedArrayView(data_ + (size_ - n), n);
  }

  /// Get subview starting at offset with given count
  constexpr TypedArrayView subview(size_type offset, size_type count = SIZE_MAX) const {
    if (offset >= size_) return TypedArrayView();
    size_type remaining = size_ - offset;
    size_type n = count < remaining ? count : remaining;
    return TypedArrayView(data_ + offset, n);
  }

private:
  const T* data_;
  size_t size_;
};

// ============================================================
// Common type aliases
// ============================================================

using FloatArrayView = TypedArrayView<float>;
using DoubleArrayView = TypedArrayView<double>;
using Int32ArrayView = TypedArrayView<int32_t>;
using UInt32ArrayView = TypedArrayView<uint32_t>;
using Int64ArrayView = TypedArrayView<int64_t>;
using UInt64ArrayView = TypedArrayView<uint64_t>;
using Int16ArrayView = TypedArrayView<int16_t>;
using UInt16ArrayView = TypedArrayView<uint16_t>;
using UInt8ArrayView = TypedArrayView<uint8_t>;

// ============================================================
// ArrayData - union of owned vector or non-owning view
// ============================================================

/// Discriminator for ArrayData storage
enum class ArrayStorageKind : uint8_t {
  Empty = 0,     // No data
  Owned = 1,     // Data stored in std::vector (owns memory)
  View = 2,      // Data is a view to external memory (does not own)
};

/// Type-erased array data that can be either owned or a view
/// Used by Value class to support zero-copy USDC reading
class ArrayData {
public:
  ArrayData() : kind_(ArrayStorageKind::Empty), type_size_(0), count_(0),
                owned_data_(nullptr), view_data_(nullptr) {}

  ~ArrayData() { clear(); }

  // Non-copyable (would need to decide copy semantics for views)
  ArrayData(const ArrayData&) = delete;
  ArrayData& operator=(const ArrayData&) = delete;

  // Moveable
  ArrayData(ArrayData&& other) noexcept;
  ArrayData& operator=(ArrayData&& other) noexcept;

  /// Create owned storage from vector (takes ownership via move)
  template <typename T>
  static ArrayData MakeOwned(std::vector<T>&& vec) {
    ArrayData ad;
    ad.kind_ = ArrayStorageKind::Owned;
    ad.type_size_ = sizeof(T);
    ad.count_ = vec.size();
    // Move vector to heap
    auto* heap_vec = new std::vector<T>(std::move(vec));
    ad.owned_data_ = heap_vec->data();
    ad.owned_destructor_ = [](void* p) { delete static_cast<std::vector<T>*>(p); };
    ad.owned_vector_ = heap_vec;
    return ad;
  }

  /// Create view to external data (does not take ownership)
  template <typename T>
  static ArrayData MakeView(const T* data, size_t count) {
    ArrayData ad;
    ad.kind_ = ArrayStorageKind::View;
    ad.type_size_ = sizeof(T);
    ad.count_ = count;
    ad.view_data_ = data;
    return ad;
  }

  /// Get storage kind
  ArrayStorageKind kind() const { return kind_; }

  /// Check if this is a view (non-owning)
  bool is_view() const { return kind_ == ArrayStorageKind::View; }

  /// Check if this owns its data
  bool is_owned() const { return kind_ == ArrayStorageKind::Owned; }

  /// Check if empty
  bool empty() const { return kind_ == ArrayStorageKind::Empty || count_ == 0; }

  /// Get element count
  size_t count() const { return count_; }

  /// Get element size in bytes
  size_t type_size() const { return type_size_; }

  /// Get total size in bytes
  size_t size_bytes() const { return count_ * type_size_; }

  /// Get raw data pointer
  const void* data() const {
    switch (kind_) {
      case ArrayStorageKind::Owned: return owned_data_;
      case ArrayStorageKind::View: return view_data_;
      default: return nullptr;
    }
  }

  /// Get typed view (asserts type size matches)
  template <typename T>
  TypedArrayView<T> as_view() const {
    if (type_size_ != sizeof(T) || kind_ == ArrayStorageKind::Empty) {
      return TypedArrayView<T>();
    }
    return TypedArrayView<T>(static_cast<const T*>(data()), count_);
  }

  /// Get mutable pointer (only valid for owned data)
  void* mutable_data() {
    if (kind_ != ArrayStorageKind::Owned) return nullptr;
    return const_cast<void*>(static_cast<const void*>(owned_data_));
  }

  /// Clear and release resources
  void clear();

  /// Convert view to owned (makes a copy)
  /// Returns true if conversion happened, false if already owned or empty
  bool make_owned();

private:
  ArrayStorageKind kind_;
  uint8_t type_size_;    // Size of one element in bytes
  uint16_t reserved_ = 0;
  uint32_t count_;       // Number of elements

  // For owned storage
  const void* owned_data_ = nullptr;
  void* owned_vector_ = nullptr;  // Pointer to the actual vector
  void (*owned_destructor_)(void*) = nullptr;

  // For view storage
  const void* view_data_ = nullptr;
};

}  // namespace next
}  // namespace tinyusdz
