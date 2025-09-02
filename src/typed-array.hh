// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Syoyo Fujita.
// Copyright 2024 - Present, Light Transport Entertainment Inc.

///
/// TypedArray: A type-safe wrapper around std::vector<uint8_t> with nonstd::span views
/// Provides in-place type transformation capabilities for memory-efficient operations
///

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>
#include <functional>
#include <stdexcept>

#include "nonstd/span.hpp"

namespace tinyusdz {

template<typename T>
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
        _is_view = false;
        resize(count);
    }

    // Constructor with size and default value
    TypedArray(size_type count, const T& value) {
        _is_view = false;
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

    // View constructor - creates a non-owning view over external data
    // When is_view=true, no allocation occurs, just stores pointer and size
    TypedArray(T* data, size_type count, bool is_view) {
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
    TypedArray(const TypedArray& other) {
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
    TypedArray(TypedArray&& other) noexcept {
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
    }

    // Copy assignment
    TypedArray& operator=(const TypedArray& other) {
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
    TypedArray& operator=(TypedArray&& other) noexcept {
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
        return *this;
    }

    // Destructor
    ~TypedArray() = default;

    // Check if this is a view (non-owning)
    bool is_view() const noexcept {
        return _is_view;
    }

    // Create a view from this array
    TypedArray create_view() const {
        return TypedArray(const_cast<T*>(data()), size(), true);
    }

    // Static helper to create a view
    static TypedArray make_view(T* data, size_type count) {
        return TypedArray(data, count, true);
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
        return _is_view ? const_cast<const_pointer>(_view_ptr) : reinterpret_cast<const_pointer>(_storage.data());
    }

    // Element access
    reference operator[](size_type index) {
        return data()[index];
    }

    const_reference operator[](size_type index) const {
        return data()[index];
    }

    reference at(size_type index) {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        // Exceptions disabled - just return element (bounds checking in debug mode only)
        assert(index < size() && "TypedArray::at: index out of range");
#else
        if (index >= size()) {
            throw std::out_of_range("TypedArray::at: index out of range");
        }
#endif
        return data()[index];
    }

    const_reference at(size_type index) const {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        // Exceptions disabled - just return element (bounds checking in debug mode only)
        assert(index < size() && "TypedArray::at: index out of range");
#else
        if (index >= size()) {
            throw std::out_of_range("TypedArray::at: index out of range");
        }
#endif
        return data()[index];
    }

    reference front() {
        return data()[0];
    }

    const_reference front() const {
        return data()[0];
    }

    reference back() {
        return data()[size() - 1];
    }

    const_reference back() const {
        return data()[size() - 1];
    }

    // Iterators
    iterator begin() noexcept {
        return data();
    }

    const_iterator begin() const noexcept {
        return data();
    }

    const_iterator cbegin() const noexcept {
        return data();
    }

    iterator end() noexcept {
        return data() + size();
    }

    const_iterator end() const noexcept {
        return data() + size();
    }

    const_iterator cend() const noexcept {
        return data() + size();
    }

    // Span views
    nonstd::span<T> span() noexcept {
        return nonstd::span<T>(data(), size());
    }

    nonstd::span<const T> span() const noexcept {
        return nonstd::span<const T>(data(), size());
    }

    nonstd::span<T> subspan(size_type offset, size_type count = static_cast<size_type>(-1)) {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        assert(offset <= size() && "TypedArray::subspan: offset out of range");
#else
        if (offset > size()) {
            throw std::out_of_range("TypedArray::subspan: offset out of range");
        }
#endif
        size_type actual_count = (count == static_cast<size_type>(-1)) ? (size() - offset) : count;
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        assert(offset + actual_count <= size() && "TypedArray::subspan: count exceeds array bounds");
#else
        if (offset + actual_count > size()) {
            throw std::out_of_range("TypedArray::subspan: count exceeds array bounds");
        }
#endif
        return nonstd::span<T>(data() + offset, actual_count);
    }

    nonstd::span<const T> subspan(size_type offset, size_type count = static_cast<size_type>(-1)) const {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        assert(offset <= size() && "TypedArray::subspan: offset out of range");
#else
        if (offset > size()) {
            throw std::out_of_range("TypedArray::subspan: offset out of range");
        }
#endif
        size_type actual_count = (count == static_cast<size_type>(-1)) ? (size() - offset) : count;
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        assert(offset + actual_count <= size() && "TypedArray::subspan: count exceeds array bounds");
#else
        if (offset + actual_count > size()) {
            throw std::out_of_range("TypedArray::subspan: count exceeds array bounds");
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

    void resize(size_type count) {
        if (_is_view) {
            // Cannot resize a view - this would require allocation
            // Could throw an exception or assert, but for now just return
            assert(!_is_view && "Cannot resize a TypedArray view");
            return;
        }
        _storage.resize(count * sizeof(T));
    }

    void resize(size_type count, const T& value) {
        if (_is_view) {
            assert(!_is_view && "Cannot resize a TypedArray view");
            return;
        }
        size_type old_size = size();
        _storage.resize(count * sizeof(T));
        
        // Initialize new elements with the given value
        for (size_type i = old_size; i < count; ++i) {
            new (data() + i) T(value);
        }
    }

    void reserve(size_type new_capacity) {
        if (_is_view) {
            assert(!_is_view && "Cannot reserve capacity for a TypedArray view");
            return;
        }
        _storage.reserve(new_capacity * sizeof(T));
    }

    void shrink_to_fit() {
        if (!_is_view) {
            _storage.shrink_to_fit();
        }
    }

    void push_back(const T& value) {
        if (_is_view) {
            assert(!_is_view && "Cannot push_back to a TypedArray view");
            return;
        }
        size_type old_size = size();
        resize(old_size + 1);
        data()[old_size] = value;
    }

    void push_back(T&& value) {
        if (_is_view) {
            assert(!_is_view && "Cannot push_back to a TypedArray view");
            return;
        }
        size_type old_size = size();
        resize(old_size + 1);
        data()[old_size] = std::move(value);
    }

    void pop_back() {
        if (_is_view) {
            assert(!_is_view && "Cannot pop_back from a TypedArray view");
            return;
        }
        if (!empty()) {
            resize(size() - 1);
        }
    }

    // In-place transform function
    // Transforms from current type T to new type N
    // Requirement: sizeof(T) >= sizeof(N) for in-place operation
    template<typename N, typename Func>
    TypedArray<N> transform(Func func) const {
        static_assert(sizeof(T) >= sizeof(N), 
                     "transform: source type size must be >= destination type size for in-place operation");
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
            for (size_type j = 0; j < n_elements_per_t && (i * n_elements_per_t + j) < dst_count; ++j) {
                size_type dst_idx = i * n_elements_per_t + j;
                func(data()[i], result.data()[dst_idx]);
            }
        }
        
        return result;
    }

    // Specialized transform for 1:1 type conversion (e.g., int to float)
    template<typename N, typename Func>
    TypedArray<N> transform_1to1(Func func) const {
        static_assert(sizeof(T) >= sizeof(N), 
                     "transform_1to1: source type size must be >= destination type size");
        static_assert(std::is_trivially_copyable<N>::value, 
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

    // Enhanced transform supporting sizeof(srcTy) <= sizeof(dstTy) with controlled buffer growth
    template<typename N, typename Func>
    TypedArray<N> transform_expand(Func func) const {
        static_assert(std::is_trivially_copyable<N>::value, 
                     "transform_expand: destination type must be trivially copyable");

        TypedArray<N> result;
        if (empty()) {
            return result;
        }

        size_type src_count = size();
        
        // Use template meta-programming instead of if constexpr for C++14 compatibility
        return transform_expand_impl<N>(func, src_count, result, 
                                       std::integral_constant<bool, (sizeof(T) >= sizeof(N))>{});
    }

    // In-place transform with expansion (modifies current array)
    // Only works when sizeof(T) <= sizeof(N) and we have sufficient capacity
    template<typename N, typename Func>
    TypedArray<N> transform_inplace_expand(Func func) {
        static_assert(std::is_trivially_copyable<N>::value, 
                     "transform_inplace_expand: destination type must be trivially copyable");

        if (empty()) {
            TypedArray<N> result;
            return result;
        }

        size_type src_count = size();
        size_type required_bytes = src_count * sizeof(N);
        //size_type current_bytes = _storage.size();

        // Check if we can expand in-place or need reallocation
        if (required_bytes <= _storage.capacity()) {
            // Can expand in-place - transform from end to beginning to avoid overwriting
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
            _storage.clear(); // Current array is now empty
            return result;
        } else {
            // Need reallocation - use regular transform_expand
            return transform_expand<N>(func);
        }
    }

    // Transform with controlled growth - specify maximum buffer growth factor
    template<typename N, typename Func>
    TypedArray<N> transform_with_limit(Func func, double max_growth_factor = 2.0) const {
        static_assert(std::is_trivially_copyable<N>::value, 
                     "transform_with_limit: destination type must be trivially copyable");

        TypedArray<N> result;
        if (empty()) {
            return result;
        }

        size_type src_count = size();
        size_type required_bytes = src_count * sizeof(N);
        size_type current_bytes = _storage.size();
        
        // Check if expansion exceeds the growth limit
        double growth_ratio = static_cast<double>(required_bytes) / static_cast<double>(current_bytes);
        if (growth_ratio > max_growth_factor) {
            //throw std::runtime_error("transform_with_limit: required buffer growth exceeds limit");
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
    std::vector<uint8_t>& storage() noexcept {
        assert(!_is_view && "Cannot get storage from a TypedArray view");
        return _storage;
    }

    const std::vector<uint8_t>& storage() const noexcept {
        assert(!_is_view && "Cannot get storage from a TypedArray view");
        return _storage;
    }

    // Swap
    void swap(TypedArray& other) noexcept {
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
    bool operator==(const TypedArray& other) const {
        if (size() != other.size()) return false;
        if (size() == 0) return true;
        return std::memcmp(data(), other.data(), size() * sizeof(T)) == 0;
    }

    bool operator!=(const TypedArray& other) const {
        return !(*this == other);
    }

private:
    std::vector<uint8_t> _storage;
    T* _view_ptr = nullptr;        // Pointer to external data when in view mode
    size_type _view_size = 0;       // Size of view in elements
    bool _is_view = false;          // Flag indicating view mode
    
    // Helper method implementations for C++14 compatibility (instead of if constexpr)
    template<typename N, typename Func>
    TypedArray<N> transform_expand_impl(Func func, size_type src_count, TypedArray<N>& result, 
                                       std::true_type /* sizeof(T) >= sizeof(N) */) const {
        // Shrinking case - can use in-place approach
        size_type dst_count = (src_count * sizeof(T)) / sizeof(N);
        result.resize(dst_count);
        
        for (size_type i = 0; i < src_count; ++i) {
            size_type n_elements_per_t = sizeof(T) / sizeof(N);
            for (size_type j = 0; j < n_elements_per_t && (i * n_elements_per_t + j) < dst_count; ++j) {
                size_type dst_idx = i * n_elements_per_t + j;
                func(data()[i], result.data()[dst_idx]);
            }
        }
        return result;
    }
    
    template<typename N, typename Func>
    TypedArray<N> transform_expand_impl(Func func, size_type src_count, TypedArray<N>& result,
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
template<typename T>
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
    TypedArrayView(pointer data, size_type count) noexcept 
        : _span(data, count) {}

    // Constructor from raw pointer range
    TypedArrayView(pointer first, pointer last) noexcept 
        : _span(first, last) {}

    // Constructor from C-style array
    template<std::size_t N>
    TypedArrayView(T (&arr)[N]) noexcept 
        : _span(arr, N) {}

    // Constructor from std::vector with type size validation
    template<typename U>
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
    template<typename U>
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
    template<typename U>
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
            _span = nonstd::span<T>(reinterpret_cast<const T*>(typed_array.data()), count);
        }
    }

    // Constructor from mutable TypedArray with type size validation
    template<typename U>
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
    size_type size() const noexcept {
        return _span.size();
    }

    size_type size_bytes() const noexcept {
        return _span.size_bytes();
    }

    bool empty() const noexcept {
        return _span.empty();
    }

    // Data access
    pointer data() noexcept {
        return _span.data();
    }

    const_pointer data() const noexcept {
        return _span.data();
    }

    // Element access
    reference operator[](size_type index) const {
        return _span[index];
    }

    reference at(size_type index) const {
#if !defined(TINYUSDZ_CXX_EXCEPTIONS) || (TINYUSDZ_CXX_EXCEPTIONS == 0)
        assert(index < size() && "TypedArrayView::at: index out of range");
#else
        if (index >= size()) {
            throw std::out_of_range("TypedArrayView::at: index out of range");
        }
#endif
        return _span[index];
    }

    reference front() const {
        return _span.front();
    }

    reference back() const {
        return _span.back();
    }

    // Iterators
    iterator begin() const noexcept {
        return _span.begin();
    }

    iterator end() const noexcept {
        return _span.end();
    }

    const_iterator cbegin() const noexcept {
        return _span.cbegin();
    }

    const_iterator cend() const noexcept {
        return _span.cend();
    }

    // Subviews
    TypedArrayView first(size_type count) const {
        return TypedArrayView(_span.first(count));
    }

    TypedArrayView last(size_type count) const {
        return TypedArrayView(_span.last(count));
    }

    TypedArrayView subspan(size_type offset, size_type count = static_cast<size_type>(-1)) const {
        size_type actual_count = (count == static_cast<size_type>(-1)) ? (size() - offset) : count;
        return TypedArrayView(_span.subspan(offset, actual_count));
    }

    // Get underlying span
    nonstd::span<T> span() const noexcept {
        return _span;
    }

    // Type reinterpret view (with validation)
    template<typename N>
    TypedArrayView<N> reinterpret_as() const noexcept {
        static_assert(std::is_trivially_copyable<N>::value, 
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
    template<typename N>
    TypedArrayView<N> reinterpret_as_mutable() const noexcept {
        static_assert(std::is_trivially_copyable<N>::value, 
                     "reinterpret_as_mutable: destination type must be trivially copyable");
        static_assert(!std::is_const<T>::value, 
                     "reinterpret_as_mutable: source type must not be const");
        
        if (sizeof(N) > sizeof(T) || size() * sizeof(T) < sizeof(N)) {
            // Cannot safely reinterpret as N - return empty view
            return TypedArrayView<N>();
        }
        
        // Safe to reinterpret as N
        size_type new_count = (size() * sizeof(T)) / sizeof(N);
        return TypedArrayView<N>(reinterpret_cast<N*>(const_cast<typename std::remove_const<T>::type*>(data())), new_count);
    }

    // Check if this view is valid (non-empty and properly aligned)
    bool is_valid() const noexcept {
        return !empty() && data() != nullptr && 
               (reinterpret_cast<std::uintptr_t>(data()) % alignof(T)) == 0;
    }

    // Comparison operators
    bool operator==(const TypedArrayView& other) const noexcept {
        if (size() != other.size()) return false;
        if (data() == other.data()) return true; // Same memory location
        return std::memcmp(data(), other.data(), size() * sizeof(T)) == 0;
    }

    bool operator!=(const TypedArrayView& other) const noexcept {
        return !(*this == other);
    }

private:
    nonstd::span<T> _span;
};

// Non-member swap
template<typename T>
void swap(TypedArray<T>& lhs, TypedArray<T>& rhs) noexcept {
    lhs.swap(rhs);
}

// Convenience functions for creating TypedArrayView

template<typename T>
TypedArrayView<T> make_typed_array_view(T* data, std::size_t count) {
    return TypedArrayView<T>(data, count);
}

template<typename T>
TypedArrayView<const T> make_typed_array_view(const T* data, std::size_t count) {
    return TypedArrayView<const T>(data, count);
}

template<typename T, std::size_t N>
TypedArrayView<T> make_typed_array_view(T (&arr)[N]) {
    return TypedArrayView<T>(arr);
}

template<typename T>
TypedArrayView<T> make_typed_array_view(std::vector<T>& vec) {
    return TypedArrayView<T>(vec);
}

template<typename T>
TypedArrayView<const T> make_typed_array_view(const std::vector<T>& vec) {
    return TypedArrayView<const T>(vec);
}

template<typename T>
TypedArrayView<T> make_typed_array_view(TypedArray<T>& arr) {
    return TypedArrayView<T>(arr);
}

template<typename T>
TypedArrayView<const T> make_typed_array_view(const TypedArray<T>& arr) {
    return TypedArrayView<const T>(arr);
}

template<typename T>
TypedArrayView<T> make_typed_array_view(nonstd::span<T> sp) {
    return TypedArrayView<T>(sp);
}

// Type reinterpret convenience functions
template<typename T, typename U>
TypedArrayView<T> reinterpret_typed_array_view(const TypedArrayView<U>& view) {
    return view.template reinterpret_as<T>();
}

template<typename T, typename U>
TypedArrayView<T> reinterpret_typed_array_view_mutable(const TypedArrayView<U>& view) {
    return view.template reinterpret_as_mutable<T>();
}

// Convenience function to create TypedArray from span
template<typename T>
TypedArray<T> make_typed_array(nonstd::span<const T> sp) {
    return TypedArray<T>(sp.data(), sp.size());
}

} // namespace tinyusdz
