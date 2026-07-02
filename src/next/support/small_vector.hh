// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - small_vector<T, N>
//
// A vector with N elements of inline storage; it spills to the heap only when it
// grows past N. For the common case of short sequences (a prim's child indices,
// a parse stack, an xformOpOrder, a handful of connection paths) this avoids a
// heap allocation entirely — the "less std::vector churn" lever from the
// next-core plan. Interface is a std::vector-compatible subset.

#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

namespace tinyusdz {
namespace next {

template <class T, size_t N>
class small_vector {
  static_assert(N > 0, "small_vector inline capacity must be > 0");

 public:
  using value_type = T;
  using size_type = size_t;
  using iterator = T*;
  using const_iterator = const T*;

  small_vector() noexcept : data_(inline_ptr()), size_(0), cap_(N) {}

  small_vector(const small_vector& o) : data_(inline_ptr()), size_(0), cap_(N) {
    reserve(o.size_);
    for (size_t i = 0; i < o.size_; ++i) {
      new (data_ + i) T(o.data_[i]);
    }
    size_ = o.size_;
  }

  small_vector(small_vector&& o) noexcept : data_(inline_ptr()), size_(0), cap_(N) {
    take(std::move(o));
  }

  small_vector& operator=(const small_vector& o) {
    if (this != &o) {
      clear();
      reserve(o.size_);
      for (size_t i = 0; i < o.size_; ++i) {
        new (data_ + i) T(o.data_[i]);
      }
      size_ = o.size_;
    }
    return *this;
  }

  small_vector& operator=(small_vector&& o) noexcept {
    if (this != &o) {
      reset_to_inline();
      take(std::move(o));
    }
    return *this;
  }

  ~small_vector() {
    destroy_all();
    if (!is_inline()) {
      std::free(data_);
    }
  }

  // -- element access --
  T& operator[](size_t i) { return data_[i]; }
  const T& operator[](size_t i) const { return data_[i]; }
  T& front() { return data_[0]; }
  const T& front() const { return data_[0]; }
  T& back() { return data_[size_ - 1]; }
  const T& back() const { return data_[size_ - 1]; }
  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }

  // -- iterators --
  iterator begin() noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator end() const noexcept { return data_ + size_; }

  // -- capacity --
  bool empty() const noexcept { return size_ == 0; }
  size_t size() const noexcept { return size_; }
  size_t capacity() const noexcept { return cap_; }
  /// True while the elements still live in the inline buffer (no heap spill).
  bool inline_storage() const noexcept { return is_inline(); }

  void reserve(size_t n) {
    if (n > cap_) {
      grow(n);
    }
  }

  // -- modifiers --
  void push_back(const T& v) {
    if (size_ == cap_) {
      grow(cap_ * 2);
    }
    new (data_ + size_) T(v);
    ++size_;
  }

  void push_back(T&& v) {
    if (size_ == cap_) {
      grow(cap_ * 2);
    }
    new (data_ + size_) T(std::move(v));
    ++size_;
  }

  template <class... Args>
  T& emplace_back(Args&&... args) {
    if (size_ == cap_) {
      grow(cap_ * 2);
    }
    T* p = new (data_ + size_) T(std::forward<Args>(args)...);
    ++size_;
    return *p;
  }

  void pop_back() {
    --size_;
    data_[size_].~T();
  }

  void clear() noexcept {
    destroy_all();
    size_ = 0;
  }

  void resize(size_t n) {
    if (n < size_) {
      for (size_t i = n; i < size_; ++i) {
        data_[i].~T();
      }
    } else if (n > size_) {
      reserve(n);
      for (size_t i = size_; i < n; ++i) {
        new (data_ + i) T();
      }
    }
    size_ = n;
  }

 private:
  T* inline_ptr() noexcept { return reinterpret_cast<T*>(inline_storage_); }
  const T* inline_ptr() const noexcept {
    return reinterpret_cast<const T*>(inline_storage_);
  }
  bool is_inline() const noexcept { return data_ == inline_ptr(); }

  void destroy_all() noexcept {
    for (size_t i = 0; i < size_; ++i) {
      data_[i].~T();
    }
  }

  void reset_to_inline() noexcept {
    destroy_all();
    if (!is_inline()) {
      std::free(data_);
    }
    data_ = inline_ptr();
    size_ = 0;
    cap_ = N;
  }

  void grow(size_t new_cap) {
    if (new_cap < N + 1) {
      new_cap = N + 1;  // first spill is always to the heap, past inline N.
    }
    if (new_cap <= cap_) {
      return;
    }
    T* nd = static_cast<T*>(std::malloc(new_cap * sizeof(T)));
    for (size_t i = 0; i < size_; ++i) {
      new (nd + i) T(std::move(data_[i]));
      data_[i].~T();
    }
    if (!is_inline()) {
      std::free(data_);
    }
    data_ = nd;
    cap_ = new_cap;
  }

  // Adopt o's contents; `*this` must already be empty inline storage.
  void take(small_vector&& o) noexcept {
    if (o.is_inline()) {
      for (size_t i = 0; i < o.size_; ++i) {
        new (data_ + i) T(std::move(o.data_[i]));
        o.data_[i].~T();
      }
      size_ = o.size_;
      o.size_ = 0;
    } else {
      data_ = o.data_;
      size_ = o.size_;
      cap_ = o.cap_;
      o.data_ = o.inline_ptr();
      o.size_ = 0;
      o.cap_ = N;
    }
  }

  alignas(T) unsigned char inline_storage_[N * sizeof(T)];
  T* data_;
  size_t size_;
  size_t cap_;
};

}  // namespace next
}  // namespace tinyusdz
