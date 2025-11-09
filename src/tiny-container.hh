// SPDX-License-Identifier: MIT
// Copyright 2024 - Present : Syoyo Fujita
//

// Simple stack container class for custom vector/string.
// Inspired from
// - https://github.com/p-ranav/small_vector
// - https://chromium.googlesource.com/chromium/chromium/+/master/base/stack_container.h

#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace tinyusdz {

template<typename T, std::size_t Size>
class StackAllocator : public std::allocator<T> {

  typedef typename std::allocator<T>::pointer pointer;
  typedef typename std::allocator<T>::size_type size_type;

  struct StackBuf
  {
    T *data() { return reinterpret_cast<T *>(_buf); }
    const T *data() const {
      return reinterpret_cast<const T *>(_buf);
    }

    char _buf[sizeof(T[Size])];
    bool use_stack{false};
  };

  pointer allocate(size_type n, void *hint = nullptr) {
    if (_buf && !_buf->use_stack && (n <= Size)) {
      _buf->use_stack = true;
      return _buf->data(); 
    } else {
      std::allocator<T>::allocate(n, hint);
    }
  }

  void deallocate(pointer p, size_type sz) {
    if (_buf && (p == _buf->data())) {
      _buf->use_stack = false;
    } else {
      std::allocator<T>::deallocate(p, sz);
    }
  }
  
 private:
    StackBuf *_buf{nullptr};
};

// T : container type
// N : capacity
template<typename T, size_t Size>
class StackContainer {
 public:

  using Allocator = StackAllocator<T, Size>;

  StackContainer() : _allocator(&_stack), _container(_allocator) {
    _container.reserve(Size);
  }

  T &get() { return _container; }
  const T &get() const { return _container; }

  T *operator->() { return &_container; }
  const T *operator->() const { return &_container; }

 protected:
  typename Allocator::StackBuf _stack;
  
  Allocator _allocator;
  T _container;

  // disallow copy and assign.
  StackContainer(const StackContainer &) = delete;
  void operator=(const StackContainer &) = delete;


};

template <typename T, size_t Size>
class StackVector : public StackContainer<std::vector<T, StackAllocator<T, Size>>, Size> {
 public:
  StackVector() : StackContainer<std::vector<T, StackAllocator<T, Size>>, Size>() {}


  StackVector(const StackVector<T, Size> &rhs) : StackContainer<std::vector<T, StackAllocator<T, Size>>, Size>() {
    this->get().assign(rhs->begin(), rhs->end());
  }

  StackVector<T, Size> &operator=(const StackVector<T, Size> &rhs) {
    this->get().assign(rhs->begin(), rhs->end());
    return *this;
  }

  T &operator[](size_t i) { return this->get().operator[](i); }
   const T &operator[](size_t i) const {
     return this->get().operator[](i);
  }

  // TODO: lvalue ctor
};


} // namespace tinyusdz



