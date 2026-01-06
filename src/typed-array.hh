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
class TypedArrayImpl {
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
  TypedArrayImpl() = default;

  // Constructor with size
  explicit TypedArrayImpl(size_type count) {
    _is_view = false;
    resize(count);
  }

  // Constructor with size and default value
  TypedArrayImpl(size_type count, const T& value) {
    _is_view = false;
    resize(count, value);
  }

  // Constructor from initializer list
  TypedArrayImpl(std::initializer_list<T> init) {
    reserve(init.size());
    for (const auto& item : init) {
      push_back(item);
    }
  }

  // Constructor from existing data (copies data)
  TypedArrayImpl(const T* data, size_type count) {
    if (data && count > 0) {
      _storage.resize(count * sizeof(T));
      std::memcpy(_storage.data(), data, count * sizeof(T));
    }
  }

  // View constructor - creates a non-owning view over external data
  // When is_view=true, no allocation occurs, just stores pointer and size
  TypedArrayImpl(T* data, size_type count, bool is_view) {
    if (is_view && data && count > 0) {
      _view_ptr = data;
      _view_size = count;
      _is_view = true;
    } else if (!is_view && data && count > 0) {
      // Non-view mode: copy data
      _storage.resize(count * sizeof(T));
      std::memcpy(_storage.data(), data, count * sizeof(T));
    }
  }

  // Copy constructor
  TypedArrayImpl(const TypedArrayImpl& other) {
    if (other._is_view) {
      // Copy view properties
      _view_ptr = other._view_ptr;
      _view_size = other._view_size;
      _is_view = true;
    } else {
      // Copy storage
      _storage = other._storage;
      _is_view = false;
    }
  }

  // Move constructor
  TypedArrayImpl(TypedArrayImpl&& other) noexcept {
    //DCOUT("TypedArrayImpl move ctor: this=" << std::hex << this << " from other=" << &other
    //            << " other._is_view=" << other._is_view << " other.size()=" << std::dec << other.size());
    if (other._is_view) {
      _view_ptr = other._view_ptr;
      _view_size = other._view_size;
      _is_view = true;
      other._view_ptr = nullptr;
      other._view_size = 0;
    } else {
      _storage = std::move(other._storage);
      _is_view = false;
    }
    DCOUT("TypedArrayImpl move ctor done: this.size()=" << size() << " other.size()=" << other.size());
  }

  // Copy assignment
  TypedArrayImpl& operator=(const TypedArrayImpl& other) {
    if (this != &other) {
      if (other._is_view) {
        _storage.clear();
        _view_ptr = other._view_ptr;
        _view_size = other._view_size;
        _is_view = true;
      } else {
        _view_ptr = nullptr;
        _view_size = 0;
        _storage = other._storage;
        _is_view = false;
      }
    }
    return *this;
  }

  // Move assignment
  TypedArrayImpl& operator=(TypedArrayImpl&& other) noexcept {
    //DCOUT("TypedArrayImpl move assign: this=" << std::hex << this << " from other=" << &other
    //            << " this.size()=" << std::dec << size() << " other.size()=" << other.size());
    if (this != &other) {
      if (other._is_view) {
        _storage.clear();
        _view_ptr = other._view_ptr;
        _view_size = other._view_size;
        _is_view = true;
        other._view_ptr = nullptr;
        other._view_size = 0;
      } else {
        _view_ptr = nullptr;
        _view_size = 0;
        _storage = std::move(other._storage);
        _is_view = false;
      }
    }
    DCOUT("TypedArrayImpl move assign done: this.size()=" << size() << " other.size()=" << other.size());
    return *this;
  }

  // Destructor
  ~TypedArrayImpl() {
    //DCOUT("TypedArrayImpl dtor: this=" << std::hex << this << " _is_view=" << _is_view << " size()=" << std::dec << size());
    if (_is_view) {
      // no free
    } else {
       _storage.clear();
    }
  }

  // Check if this is a view (non-owning)
  bool is_view() const noexcept { return _is_view; }

  // Create a view from this array
  TypedArrayImpl create_view() const {
    return TypedArrayImpl(const_cast<T*>(data()), size(), true);
  }

  // Static helper to create a view
  static TypedArrayImpl make_view(T* data, size_type count) {
    return TypedArrayImpl(data, count, true);
  }

  // Size operations
  size_type size() const noexcept {
    return _is_view ? _view_size : (_storage.size() / sizeof(T));
  }

  size_type capacity() const noexcept {
    return _is_view ? _view_size : (_storage.capacity() / sizeof(T));
  }

  bool empty() const noexcept {
    return _is_view ? (_view_size == 0) : _storage.empty();
  }

  size_type max_size() const noexcept {
    return _is_view ? _view_size : (_storage.max_size() / sizeof(T));
  }

  // Data access
  pointer data() noexcept {
    return _is_view ? _view_ptr : reinterpret_cast<pointer>(_storage.data());
  }

  const_pointer data() const noexcept {
    return _is_view ? const_cast<const_pointer>(_view_ptr)
                    : reinterpret_cast<const_pointer>(_storage.data());
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
    if (_is_view) {
      // For view mode, just reset the view
      _view_ptr = nullptr;
      _view_size = 0;
    } else {
      _storage.clear();
    }
  }

  bool resize(size_type count) {
    if (_is_view) {
      // Cannot resize a view - this would require allocation
      // Could throw an exception or assert, but for now just return
      // assert(!_is_view && "Cannot resize a TypedArray view");
      return false;
    }
    _storage.resize(count * sizeof(T));
    return true;
  }

  bool resize(size_type count, const T& value) {
    if (_is_view) {
      // assert(!_is_view && "Cannot resize a TypedArray view");
      return false;
    }
    size_type old_size = size();
    _storage.resize(count * sizeof(T));

    // Initialize new elements with the given value
    for (size_type i = old_size; i < count; ++i) {
      new (data() + i) T(value);
    }
    return true;
  }

  bool reserve(size_type new_capacity) {
    if (_is_view) {
      // assert(!_is_view && "Cannot reserve capacity for a TypedArray view");
      return false;
    }
    _storage.reserve(new_capacity * sizeof(T));
    return true;
  }

  void shrink_to_fit() {
    if (!_is_view) {
      _storage.shrink_to_fit();
    }
  }

  bool push_back(const T& value) {
    if (_is_view) {
      // assert(!_is_view && "Cannot push_back to a TypedArray view");
      return false;
    }
    size_type old_size = size();
    resize(old_size + 1);
    data()[old_size] = value;

    return true;
  }

  bool push_back(T&& value) {
    if (_is_view) {
      // assert(!_is_view && "Cannot push_back to a TypedArray view");
      return false;
    }
    size_type old_size = size();
    resize(old_size + 1);
    data()[old_size] = std::move(value);

    return true;
  }

  bool pop_back() {
    if (_is_view) {
      return false;
    }
    if (!empty()) {
      resize(size() - 1);
    }

    return true;
  }

  // In-place transform function
  // Transforms from current type T to new type N
  // Requirement: sizeof(T) >= sizeof(N) for in-place operation
  template <typename N, typename Func>
  TypedArrayImpl<N> transform(Func func) const {
    static_assert(sizeof(T) >= sizeof(N),
                  "transform: source type size must be >= destination type "
                  "size for in-place operation");
    static_assert(std::is_trivially_copyable<N>::value,
                  "transform: destination type must be trivially copyable");

    TypedArrayImpl<N> result;
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
  TypedArrayImpl<N> transform_1to1(Func func) const {
    static_assert(
        sizeof(T) >= sizeof(N),
        "transform_1to1: source type size must be >= destination type size");
    static_assert(
        std::is_trivially_copyable<N>::value,
        "transform_1to1: destination type must be trivially copyable");

    TypedArrayImpl<N> result;
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
  TypedArrayImpl<N> transform_expand(Func func) const {
    static_assert(
        std::is_trivially_copyable<N>::value,
        "transform_expand: destination type must be trivially copyable");

    TypedArrayImpl<N> result;
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
  TypedArrayImpl<N> transform_inplace_expand(Func func) {
    static_assert(std::is_trivially_copyable<N>::value,
                  "transform_inplace_expand: destination type must be "
                  "trivially copyable");

    if (empty()) {
      TypedArrayImpl<N> result;
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

      // Return TypedArrayImpl<N> that shares the same storage
      TypedArrayImpl<N> result;
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
  TypedArrayImpl<N> transform_with_limit(Func func,
                                         double max_growth_factor = 2.0) const {
    static_assert(
        std::is_trivially_copyable<N>::value,
        "transform_with_limit: destination type must be trivially copyable");

    TypedArrayImpl<N> result;
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
  void swap(TypedArrayImpl& other) noexcept {
    if (_is_view || other._is_view) {
      // Swap all members including view state
      std::swap(_storage, other._storage);
      std::swap(_view_ptr, other._view_ptr);
      std::swap(_view_size, other._view_size);
      std::swap(_is_view, other._is_view);
    } else {
      _storage.swap(other._storage);
    }
  }

  // Comparison operators
  bool operator==(const TypedArrayImpl& other) const {
    if (size() != other.size()) return false;
    if (size() == 0) return true;
    return std::memcmp(data(), other.data(), size() * sizeof(T)) == 0;
  }

  bool operator!=(const TypedArrayImpl& other) const {
    return !(*this == other);
  }

  // Hash function for use with unordered_map
  // Hashes ALL elements for correctness
  struct Hash {
    size_t operator()(const TypedArrayImpl<T>& arr) const {
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
  T* _view_ptr = nullptr;    // Pointer to external data when in view mode
  size_type _view_size = 0;  // Size of view in elements
  bool _is_view = false;     // Flag indicating view mode

  // Helper method implementations for C++14 compatibility (instead of if
  // constexpr)
  template <typename N, typename Func>
  TypedArrayImpl<N> transform_expand_impl(
      Func func, size_type src_count, TypedArrayImpl<N>& result,
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
  TypedArrayImpl<N> transform_expand_impl(
      Func func, size_type src_count, TypedArrayImpl<N>& result,
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
/// TypedArray: Optimized 64-bit storage for TypedArrayImpl<T> pointers
///
/// Memory layout:
///   Bit 63 (MSB):    dedup/mmap flag (1 = shared/mmap, don't delete; 0 =
///   owned, delete on destruction) Bits 0-47:       48-bit pointer to
///   TypedArrayImpl<T> object (sufficient for x86-64 canonical addresses) Bits
///   48-62:      Reserved/unused (15 bits available for future use)
///
/// The 48-bit pointer is sufficient because:
/// - x86-64 CPUs use canonical addresses with only 48 bits of virtual address
/// space
/// - ARM64 typically uses 48-52 bits (VA_BITS), with 48 being most common
///
/// Usage:
///   - When dedup flag is set (1): The pointer is shared/memory-mapped and will
///   NOT be deleted
///   - When dedup flag is clear (0): The pointer is owned and WILL be deleted
///   on destruction
///
template <typename T>
class TypedArray {
 public:
  // Default constructor - creates owned array with size 0
  TypedArray() noexcept : _packed_data(0) {
    reset(new TypedArrayImpl<T>(0), false);
  }

  // Constructor from pointer with optional dedup flag
  // ptr: pointer to TypedArrayImpl<T> to manage
  // dedup_flag: if true, marks as shared/mmap (won't delete); if false, takes
  // ownership (will delete)
  explicit TypedArray(TypedArrayImpl<T>* ptr, bool dedup_flag = false) noexcept
      : _packed_data(0) {
    reset(ptr, dedup_flag);
  }

  // Reconstruct TypedArray from packed_data.
  // No validity check of pointer address, so be careful to use this constructor.
  TypedArray(const uint64_t packed_data) : _packed_data(packed_data) {
  }

  // Destructor - conditionally deletes based on dedup flag
  ~TypedArray() {
    if (!is_dedup() && get() != nullptr) {
      delete get();
    }
  }

  // Copy constructor - performs deep copy to avoid ownership issues
  TypedArray(const TypedArray& other) : _packed_data(0) {
    if (other.is_null()) {
      // Source is null - nothing to copy
      _packed_data = 0;
    } else if (other.is_dedup()) {
      // Source is shared/mmap - can safely share the pointer
      _packed_data = other._packed_data;
    } else {
      // Source is owned - perform deep copy to avoid ownership conflicts
      TypedArrayImpl<T>* src_ptr = other.get();
      if (src_ptr) {
        // Create new TypedArrayImpl with deep copy of data
        TypedArrayImpl<T>* new_ptr = new TypedArrayImpl<T>(*src_ptr);
        reset(new_ptr, false);  // This copy owns the new data
      }
    }
  }

  // Move constructor - transfers ownership
  TypedArray(TypedArray&& other) noexcept : _packed_data(other._packed_data) {
    other._packed_data = 0;  // Reset source to null
  }

  // Copy assignment - performs deep copy to avoid ownership issues
  TypedArray& operator=(const TypedArray& other) {
    if (this != &other) {
      // Delete current resource if owned
      if (!is_dedup() && get() != nullptr) {
        delete get();
      }

      // Copy from other
      if (other.is_null()) {
        // Source is null
        _packed_data = 0;
      } else if (other.is_dedup()) {
        // Source is shared/mmap - can safely share the pointer
        _packed_data = other._packed_data;
      } else {
        // Source is owned - perform deep copy
        TypedArrayImpl<T>* src_ptr = other.get();
        if (src_ptr) {
          TypedArrayImpl<T>* new_ptr = new TypedArrayImpl<T>(*src_ptr);
          reset(new_ptr, false);  // This copy owns the new data
        } else {
          _packed_data = 0;
        }
      }
    }
    return *this;
  }

  // Move assignment - transfers ownership
  TypedArray& operator=(TypedArray&& other) noexcept {
    if (this != &other) {
      // Delete current resource if owned
      if (!is_dedup() && get() != nullptr) {
        delete get();
      }

      // Move from other
      _packed_data = other._packed_data;
      other._packed_data = 0;
    }
    return *this;
  }

  // Check if this is a dedup/mmap pointer (won't be deleted)
  bool is_dedup() const noexcept {
    return (_packed_data & DEDUP_FLAG_BIT) != 0;
  }

  // Set the dedup flag
  void set_dedup(bool dedup) noexcept {
    if (dedup) {
      _packed_data |= DEDUP_FLAG_BIT;
    } else {
      _packed_data &= ~DEDUP_FLAG_BIT;
    }
  }

  // Transfer ownership: set dedup flag to prevent deletion
  // Use this when transferring ownership of the impl to another owner (e.g. shared_ptr)
  void reset_ownership() noexcept {
    set_dedup(true);
  }

  // Get the raw pointer
  TypedArrayImpl<T>* get() const noexcept {
    uint64_t ptr_bits = _packed_data & PTR_MASK;

    // Sign-extend from 48 bits to 64 bits for canonical address
    // If bit 47 is set, we need to set bits 48-63 to maintain canonical form
    if (ptr_bits & (1ULL << 47)) {
      ptr_bits |= 0xFFFF000000000000ULL;
    }

    return reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);
  }

  // Pointer dereference operators
  TypedArrayImpl<T>* operator->() const noexcept { return get(); }

  TypedArrayImpl<T>& operator*() const noexcept { return *get(); }

  // Convenience methods that forward to TypedArrayImpl
  using size_type = typename TypedArrayImpl<T>::size_type;
  using reference = typename TypedArrayImpl<T>::reference;
  using const_reference = typename TypedArrayImpl<T>::const_reference;

  size_type size() const noexcept { return get() ? get()->size() : 0; }

  bool empty() const noexcept { return get() ? get()->empty() : true; }

  reference operator[](size_type index) const { return (*get())[index]; }

  reference at(size_type index) const { return get()->at(index); }

  typename TypedArrayImpl<T>::pointer data() const noexcept {
    return get() ? get()->data() : nullptr;
  }

  bool resize(size_type count) {
    if (get()) {
      return get()->resize(count);
    }
    return false;
  }

  void clear() noexcept {
    if (get()) get()->clear();
  }

  // Check if pointer is null
  bool is_null() const noexcept { return ((_packed_data & PTR_MASK) == 0); }

  // Explicit bool conversion
  explicit operator bool() const noexcept { return !is_null(); }

  // Reset to new pointer with optional dedup flag
  // Deletes current pointer if owned
  void reset(TypedArrayImpl<T>* ptr = nullptr,
             bool dedup_flag = false) noexcept {
    TypedArrayImpl<T>* old_ptr = get();
    bool old_is_dedup = is_dedup();

    //DCOUT("TypedArray::reset: old_ptr=" << std::hex << old_ptr << " old_is_dedup=" << old_is_dedup
    //            << " new_ptr=" << ptr << " new_dedup=" << dedup_flag << std::dec);

    // Delete current resource if owned
    if (!old_is_dedup && old_ptr != nullptr) {
      //DCOUT("TypedArray::reset: deleting old_ptr=" << std::hex << old_ptr << std::dec);
      delete old_ptr;
    }

    // Pack new pointer and flag
    if (ptr == nullptr) {
      _packed_data = 0;
    } else {
      uint64_t ptr_value = reinterpret_cast<uint64_t>(ptr);

      // Ensure pointer fits in 48 bits (canonical address check)
      // Valid x86-64 canonical addresses have either:
      // - Bits 63-47 all 0 (user space: 0x0000'0000'0000'0000 -
      // 0x0000'7FFF'FFFF'FFFF)
      // - Bits 63-47 all 1 (kernel space: 0xFFFF'8000'0000'0000 -
      // 0xFFFF'FFFF'FFFF'FFFF)

      // Store only the lower 48 bits
      _packed_data = ptr_value & PTR_MASK;

      // Set dedup flag if requested
      if (dedup_flag) {
        _packed_data |= DEDUP_FLAG_BIT;
      }
    }

    if (ptr != nullptr) {
      //DCOUT("TypedArray::reset done: new size=" << ptr->size());
    }
  }

  // Release ownership without deleting
  // Returns the pointer and clears this instance
  TypedArrayImpl<T>* release() noexcept {
    TypedArrayImpl<T>* ptr = get();
    _packed_data = 0;
    return ptr;
  }

  // Get the raw packed 64-bit value (for debugging/serialization)
  uint64_t get_packed_value() const noexcept { return _packed_data; }

  // Comparison operators
  bool operator==(const TypedArray& other) const noexcept {
    return get() == other.get();
  }

  bool operator!=(const TypedArray& other) const noexcept {
    return get() != other.get();
  }

  bool operator==(std::nullptr_t) const noexcept { return is_null(); }

  bool operator!=(std::nullptr_t) const noexcept { return !is_null(); }

 private:
  uint64_t _packed_data;  // Packed pointer (48 bits) + dedup flag (1 bit) +
                          // reserved (15 bits)

  // Bit layout constants
  static constexpr uint64_t DEDUP_FLAG_BIT = 1ULL
                                             << 63;  // MSB: dedup/mmap flag
  static constexpr uint64_t PTR_MASK =
      0x0000FFFFFFFFFFFFULL;  // Lower 48 bits: pointer
  static constexpr uint64_t RESERVED_MASK =
      0x7FFF000000000000ULL;  // Bits 48-62: reserved
};

// Helper function to create owned TypedArray
template <typename T>
TypedArray<T> make_typed_array_ptr(TypedArrayImpl<T>* ptr) {
  return TypedArray<T>(ptr, false);
}

// Helper function to create dedup/mmap TypedArray
template <typename T>
TypedArray<T> make_typed_array_ptr_dedup(TypedArrayImpl<T>* ptr) {
  return TypedArray<T>(ptr, true);
}

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

  // Constructor from TypedArrayImpl with type size validation
  template <typename U>
  TypedArrayView(const TypedArrayImpl<U>& typed_array) noexcept {
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

  // Constructor from mutable TypedArrayImpl with type size validation
  template <typename U>
  TypedArrayView(TypedArrayImpl<U>& typed_array) noexcept {
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
void swap(TypedArrayImpl<T>& lhs, TypedArrayImpl<T>& rhs) noexcept {
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
TypedArrayView<T> make_typed_array_view(TypedArrayImpl<T>& arr) {
  return TypedArrayView<T>(arr);
}

template <typename T>
TypedArrayView<const T> make_typed_array_view(const TypedArrayImpl<T>& arr) {
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

// Convenience function to create TypedArrayImpl from span
template <typename T>
TypedArrayImpl<T> make_typed_array(nonstd::span<const T> sp) {
  return TypedArrayImpl<T>(sp.data(), sp.size());
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

#if 0
    // Explicit copy method - creates a deep copy of the chunked array
    ChunkedTypedArray copy() const {
        ChunkedTypedArray result;  // Use default constructor
        result._chunk_size_bytes = _chunk_size_bytes;
        result._elements_per_chunk = _elements_per_chunk;
        result._total_size = _total_size;
        result._front_offset = _front_offset;
        result._use_fixed_chunk_size = _use_fixed_chunk_size;

        // Deep copy each chunk
        result._chunks.reserve(_chunks.size());
        for (const auto& chunk : _chunks) {
            result._chunks.push_back(chunk);
        }

        return result;
    }
#endif

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
  const TypedArrayImpl<uint8_t>* get_chunk(size_type chunk_index) const {
    if (_is_mmap_mode || chunk_index >= _chunks.size()) return nullptr;
    return &_chunks[chunk_index];
  }

  TypedArrayImpl<uint8_t>* get_chunk(size_type chunk_index) {
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
  std::vector<TypedArrayImpl<uint8_t>>
      _chunks;  // Storage chunks using TypedArrayImpl (for copy mode)
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
// TypedArray Factory Functions
// ============================================================================
// These factory functions provide clearer, more intuitive interfaces for
// creating TypedArray instances for common use cases: ownership, deduplication,
// memory-mapping, and views.
//
// Benefits:
// - Self-documenting: Function names clearly indicate intent
// - Type-safe: No confusing boolean flag parameters
// - Zero overhead: All inline, same performance as direct constructors
// - Backward compatible: Existing code continues to work
// ============================================================================

// ----------------------------------------------------------------------------
// TypedArray Factory Functions (Smart Pointer Wrapper)
// ----------------------------------------------------------------------------

///
/// Create TypedArray for owned array (will be deleted by TypedArray)
/// Use this when TypedArray should manage the lifetime of the implementation.
///
/// Example:
///   auto* impl = new TypedArrayImpl<float>(100);
///   TypedArray<float> arr = MakeOwnedTypedArray(impl);
///   // arr will delete impl when destroyed
///
template <typename T>
inline TypedArray<T> MakeOwnedTypedArray(TypedArrayImpl<T>* ptr) {
  return TypedArray<T>(ptr, false);  // dedup_flag = false: will delete
}

///
/// Create TypedArray for deduplicated array (shared, won't be deleted)
/// Use this when the array is shared/cached and managed elsewhere.
///
/// Example:
///   // Array is stored in dedup cache
///   auto it = _dedup_float_array.find(value_rep);
///   TypedArray<float> arr = MakeDedupTypedArray(it->second.get());
///   // arr won't delete the cached array
///
template <typename T>
inline TypedArray<T> MakeDedupTypedArray(TypedArrayImpl<T>* ptr) {
  return TypedArray<T>(ptr, true);  // dedup_flag = true: won't delete
}

///
/// Create TypedArray for shared array (alias for MakeDedupTypedArray)
/// Use this when the array is shared among multiple owners.
///
template <typename T>
inline TypedArray<T> MakeSharedTypedArray(TypedArrayImpl<T>* ptr) {
  return TypedArray<T>(ptr, true);  // dedup_flag = true: won't delete
}

///
/// Create TypedArray for memory-mapped array (non-owning, won't be deleted)
/// Use this for arrays backed by mmap'd files or external memory.
///
/// Example:
///   float* mmap_data = static_cast<float*>(mmap_ptr);
///   auto* impl = new TypedArrayImpl<float>(mmap_data, count, true);
///   TypedArray<float> arr = MakeMmapTypedArray(impl);
///
template <typename T>
inline TypedArray<T> MakeMmapTypedArray(TypedArrayImpl<T>* ptr) {
  return TypedArray<T>(ptr, true);  // dedup_flag = true: won't delete
}

// ----------------------------------------------------------------------------
// TypedArrayImpl Factory Functions (Array Implementation)
// ----------------------------------------------------------------------------

///
/// Create TypedArrayImpl with owned copy of data
/// Copies the data into internal storage.
///
/// Example:
///   float data[] = {1.0f, 2.0f, 3.0f};
///   auto arr = MakeTypedArrayCopy(data, 3);
///
template <typename T>
inline TypedArrayImpl<T> MakeTypedArrayCopy(const T* data, size_t count) {
  return TypedArrayImpl<T>(data, count);  // Copies data
}

///
/// Create non-owning view over external memory
/// Does not copy data, just references it. Caller must ensure memory lifetime.
///
/// Example:
///   float external_buffer[1000];
///   auto view = MakeTypedArrayView(external_buffer, 1000);
///   // view doesn't own the data
///
template <typename T>
inline TypedArrayImpl<T> MakeTypedArrayView(T* data, size_t count) {
  return TypedArrayImpl<T>(data, count, true);  // is_view = true
}

///
/// Create non-owning view for memory-mapped data
/// Alias for MakeTypedArrayView with clearer intent for mmap use cases.
///
/// Example:
///   float* mmap_ptr = static_cast<float*>(mmap(fd, ...));
///   auto arr = MakeTypedArrayMmap(mmap_ptr, element_count);
///
template <typename T>
inline TypedArrayImpl<T> MakeTypedArrayMmap(T* data, size_t count) {
  return TypedArrayImpl<T>(data, count, true);  // is_view = true
}

///
/// Create empty TypedArrayImpl with specified capacity
/// Reserves memory without initializing elements.
///
/// Example:
///   auto arr = MakeTypedArrayReserved<double>(1000);
///   for (int i = 0; i < 500; ++i) {
///       arr.push_back(i * 1.5);
///   }
///
template <typename T>
inline TypedArrayImpl<T> MakeTypedArrayReserved(size_t capacity) {
  TypedArrayImpl<T> arr;
  arr.reserve(capacity);
  return arr;
}

// ----------------------------------------------------------------------------
// Combined Convenience Functions
// ----------------------------------------------------------------------------

///
/// Create owned TypedArray from data copy
/// Combines allocation, copy, and wrapping in one call.
///
/// Example:
///   float data[] = {1.0f, 2.0f, 3.0f};
///   TypedArray<float> arr = CreateOwnedTypedArray(data, 3);
///
template <typename T>
inline TypedArray<T> CreateOwnedTypedArray(const T* data, size_t count) {
  auto* impl = new TypedArrayImpl<T>(data, count);
  return MakeOwnedTypedArray(impl);
}

///
/// Create owned TypedArray with specified size (uninitialized)
/// Allocates array with given size, elements are uninitialized.
///
/// Example:
///   TypedArray<int> arr = CreateOwnedTypedArray<int>(100);
///   for (size_t i = 0; i < arr.size(); ++i) {
///       arr[i] = static_cast<int>(i);
///   }
///
template <typename T>
inline TypedArray<T> CreateOwnedTypedArray(size_t count) {
  auto* impl = new TypedArrayImpl<T>(count);
  return MakeOwnedTypedArray(impl);
}

///
/// Create owned TypedArray with specified size and default value
/// Allocates and initializes all elements with the given value.
///
/// Example:
///   TypedArray<float> arr = CreateOwnedTypedArray<float>(100, 1.0f);
///
template <typename T>
inline TypedArray<T> CreateOwnedTypedArray(size_t count, const T& value) {
  auto* impl = new TypedArrayImpl<T>(count, value);
  return MakeOwnedTypedArray(impl);
}

///
/// Create deduplicated TypedArray from existing implementation pointer
/// Use this when storing in deduplication cache.
///
/// Example:
///   TypedArrayImpl<int32_t>& cached = _dedup_int32_array[value_rep];
///   TypedArray<int32_t> arr = CreateDedupTypedArray(&cached);
///
template <typename T>
inline TypedArray<T> CreateDedupTypedArray(TypedArrayImpl<T>* ptr) {
  return MakeDedupTypedArray(ptr);
}

///
/// Create mmap TypedArray over external memory
/// Combines view creation and wrapping for mmap use cases.
///
/// Example:
///   float* mmap_data = static_cast<float*>(mmap_ptr);
///   TypedArray<float> arr = CreateMmapTypedArray(mmap_data, count);
///
template <typename T>
inline TypedArray<T> CreateMmapTypedArray(T* data, size_t count) {
  auto* impl = new TypedArrayImpl<T>(data, count, true);  // View mode
  return MakeMmapTypedArray(impl);
}

///
/// Deep copy an existing TypedArray
/// Creates a new independent copy with its own storage.
///
/// Example:
///   TypedArray<double> original = ...;
///   TypedArray<double> copy = DuplicateTypedArray(original);
///   // copy is completely independent
///
template <typename T>
inline TypedArray<T> DuplicateTypedArray(const TypedArray<T>& source) {
  if (!source || source.empty()) {
    return TypedArray<T>();
  }
  auto* impl = new TypedArrayImpl<T>(source.data(), source.size());
  return MakeOwnedTypedArray(impl);
}

///
/// Deep copy a TypedArrayImpl
/// Creates a new implementation with copied data.
///
/// Example:
///   TypedArrayImpl<float> original = ...;
///   TypedArrayImpl<float> copy = DuplicateTypedArrayImpl(original);
///
template <typename T>
inline TypedArrayImpl<T> DuplicateTypedArrayImpl(
    const TypedArrayImpl<T>& source) {
  if (source.empty()) {
    return TypedArrayImpl<T>();
  }
  return TypedArrayImpl<T>(source.data(), source.size());
}

}  // namespace tinyusdz
