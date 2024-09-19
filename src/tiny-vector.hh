// SPDX-License-Identifier: MIT
// Copyright 2024 - Present : Syoyo Fujita
//

// Simple short stack vector
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

#if 0
template <class T, std::size_t N, class Allocator = std::allocator<T>> class short_vector {
  private:
    std::array<T, N> _stack;
    // TODO: Implement our own vector class.
    std::vector<T, Allocator> _heap;
    std::size_t _size{0};

  public:
    typedef T value_type;
    typedef std::size_t size_type;
    typedef value_type &reference;
    typedef const value_type &const_reference;
    typedef Allocator allocator_type;
    typedef T *pointer;
    typedef const T *const_pointer;

    small_vector() = default;  
}
#endif

} // namespace tinyusdz



