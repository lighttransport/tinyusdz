// SPDX-License-Identifier: MIT
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace tinyusdz {

///
/// Buffer class to replace std::vector<uint8_t> with manual memory management
/// and configurable alignment.
///
/// @tparam Alignment Memory alignment in bytes (default: 16)
///
template <size_t Alignment = 16>
class Buffer {
 public:
  Buffer() : data_(nullptr), size_(0), capacity_(0) {}

  ~Buffer() { free_memory(); }

  // Copy constructor
  Buffer(const Buffer& other) : data_(nullptr), size_(0), capacity_(0) {
    if (other.size_ > 0) {
      resize(other.size_);
      std::memcpy(data_, other.data_, other.size_);
    }
  }

  // Move constructor
  Buffer(Buffer&& other) noexcept
      : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  // Copy assignment
  Buffer& operator=(const Buffer& other) {
    if (this != &other) {
      if (other.size_ > 0) {
        resize(other.size_);
        std::memcpy(data_, other.data_, other.size_);
      } else {
        clear();
      }
    }
    return *this;
  }

  // Move assignment
  Buffer& operator=(Buffer&& other) noexcept {
    if (this != &other) {
      free_memory();
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      other.data_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
    }
    return *this;
  }

  ///
  /// Check if buffer is empty
  ///
  bool empty() const { return size_ == 0; }

  ///
  /// Get current size in bytes
  ///
  size_t size() const { return size_; }

  ///
  /// Get current capacity in bytes
  ///
  size_t capacity() const { return capacity_; }

  ///
  /// Resize buffer to new_size bytes
  /// If new_size > capacity, reallocates memory
  /// If new_size < size, shrinks size but keeps capacity
  ///
  void resize(size_t new_size) {
    if (new_size > capacity_) {
      reallocate(new_size);
    }
    size_ = new_size;
  }

  ///
  /// Shrink capacity to match current size
  ///
  void shrink_to_fit() {
    if (size_ < capacity_) {
      if (size_ == 0) {
        free_memory();
      } else {
        reallocate(size_);
      }
    }
  }

  ///
  /// Clear buffer (sets size to 0, keeps capacity)
  ///
  void clear() { size_ = 0; }

  ///
  /// Get raw pointer to data
  ///
  uint8_t* data() { return data_; }
  const uint8_t* data() const { return data_; }

  ///
  /// Reserve capacity without changing size
  ///
  void reserve(size_t new_capacity) {
    if (new_capacity > capacity_) {
      reallocate(new_capacity);
    }
  }

  ///
  /// Array access operator
  ///
  uint8_t& operator[](size_t index) { return data_[index]; }
  const uint8_t& operator[](size_t index) const { return data_[index]; }

  ///
  /// Push back a single byte
  ///
  void push_back(uint8_t value) {
    size_t old_size = size_;
    resize(size_ + 1);
    data_[old_size] = value;
  }

 private:
  void free_memory() {
    if (data_) {
#ifdef _WIN32
      _aligned_free(data_);
#else
      free(data_);
#endif
      data_ = nullptr;
      capacity_ = 0;
      size_ = 0;
    }
  }

  void reallocate(size_t new_capacity) {
    uint8_t* new_data = nullptr;

#ifdef _WIN32
    new_data = static_cast<uint8_t*>(_aligned_malloc(new_capacity, Alignment));
#else
    if (posix_memalign(reinterpret_cast<void**>(&new_data), Alignment,
                       new_capacity) != 0) {
      new_data = nullptr;
    }
#endif

    if (!new_data) {
      // Allocation failed - could throw or handle error
      // For now, keep existing buffer
      return;
    }

    if (data_ && size_ > 0) {
      std::memcpy(new_data, data_, size_);
    }

    free_memory();
    data_ = new_data;
    capacity_ = new_capacity;
  }

  uint8_t* data_;
  size_t size_;
  size_t capacity_;
};

}  // namespace tinyusdz
