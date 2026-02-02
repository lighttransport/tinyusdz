// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - ChunkedArray for WASM-efficient memory allocation
//
// Design goals:
// - Fixed-size chunks that fit well in WASM linear memory
// - No reallocation/copy on growth (just add new chunk)
// - Direct pointer access for GPU upload
// - Memory-efficient for large arrays
// - Optional MemoryPool support for unified memory management

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>

// Include MemoryPool for pool allocation support
#include "next/memory/memory-pool.hh"

// Forward declaration
namespace tinyusdz {
namespace next {
class MemoryContext;
}
}

namespace tinyusdz {
namespace tydra {
namespace next {

// Default chunk size: 64KB (good for WASM page alignment)
constexpr size_t kDefaultChunkSize = 64 * 1024;

// ChunkedArray: Fixed-size chunk allocation for large arrays
// - Each chunk is allocated separately (no realloc)
// - Elements accessed via index or iterator
// - Can get contiguous data via flatten() for GPU upload
// - Supports both heap allocation and MemoryPool allocation
template <typename T, size_t ChunkBytes = kDefaultChunkSize>
class ChunkedArray {
 public:
  static constexpr size_t kElementsPerChunk = ChunkBytes / sizeof(T);
  static_assert(kElementsPerChunk > 0, "Element size too large for chunk");

  ChunkedArray() = default;

  /// Construct with memory pool for allocation
  /// Note: Pool chunks are not owned by this array - they're freed when pool is cleared
  explicit ChunkedArray(tinyusdz::next::MemoryPool* pool) {
    if (pool) {
      set_allocator(PoolAllocator, static_cast<void*>(pool));
    }
  }

  ~ChunkedArray() {
    // Chunks with is_heap_owned=true are deleted by Chunk destructor
    // Pool-allocated chunks (is_heap_owned=false) are freed when pool is cleared
    chunks_.clear();
  }

  // Move only (no copy to avoid accidental large copies)
  ChunkedArray(ChunkedArray&&) = default;
  ChunkedArray& operator=(ChunkedArray&&) = default;
  ChunkedArray(const ChunkedArray&) = delete;
  ChunkedArray& operator=(const ChunkedArray&) = delete;

  // Reserve space for n elements (pre-allocates chunks)
  void reserve(size_t n) {
    ensure_capacity(n);
  }

  // Add element, returns index
  size_t push_back(const T& value) {
    size_t idx = size_;
    ensure_capacity(size_ + 1);
    (*this)[idx] = value;
    ++size_;
    return idx;
  }

  size_t push_back(T&& value) {
    size_t idx = size_;
    ensure_capacity(size_ + 1);
    (*this)[idx] = std::move(value);
    ++size_;
    return idx;
  }

  // Add multiple elements from contiguous array
  void append(const T* data, size_t count) {
    if (count == 0) return;
    ensure_capacity(size_ + count);

    size_t remaining = count;
    const T* src = data;

    while (remaining > 0) {
      size_t chunk_idx = size_ / kElementsPerChunk;
      size_t offset = size_ % kElementsPerChunk;
      size_t space_in_chunk = kElementsPerChunk - offset;
      size_t to_copy = (remaining < space_in_chunk) ? remaining : space_in_chunk;

      std::memcpy(chunks_[chunk_idx].data + offset, src, to_copy * sizeof(T));

      src += to_copy;
      size_ += to_copy;
      remaining -= to_copy;
    }
  }

  // Resize (may add uninitialized elements)
  void resize(size_t n) {
    ensure_capacity(n);
    size_ = n;
  }

  void resize(size_t n, const T& value) {
    size_t old_size = size_;
    ensure_capacity(n);
    size_ = n;
    for (size_t i = old_size; i < n; ++i) {
      (*this)[i] = value;
    }
  }

  void clear() {
    size_ = 0;
    // Keep chunks allocated for reuse
  }

  void shrink_to_fit() {
    size_t needed_chunks = (size_ + kElementsPerChunk - 1) / kElementsPerChunk;
    if (needed_chunks == 0) needed_chunks = 0;
    while (chunks_.size() > needed_chunks) {
      chunks_.pop_back();
    }
  }

  // Access
  T& operator[](size_t idx) {
    size_t chunk_idx = idx / kElementsPerChunk;
    size_t offset = idx % kElementsPerChunk;
    return chunks_[chunk_idx].data[offset];
  }

  const T& operator[](size_t idx) const {
    size_t chunk_idx = idx / kElementsPerChunk;
    size_t offset = idx % kElementsPerChunk;
    return chunks_[chunk_idx].data[offset];
  }

  T& at(size_t idx) {
    if (idx >= size_) throw std::out_of_range("ChunkedArray::at");
    return (*this)[idx];
  }

  const T& at(size_t idx) const {
    if (idx >= size_) throw std::out_of_range("ChunkedArray::at");
    return (*this)[idx];
  }

  T& front() { return (*this)[0]; }
  const T& front() const { return (*this)[0]; }
  T& back() { return (*this)[size_ - 1]; }
  const T& back() const { return (*this)[size_ - 1]; }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  size_t chunk_count() const { return chunks_.size(); }
  size_t capacity() const { return chunks_.size() * kElementsPerChunk; }

  // Memory usage in bytes
  size_t memory_usage() const {
    return chunks_.size() * ChunkBytes + sizeof(*this);
  }

  // Get pointer to chunk data (for direct GPU upload)
  T* chunk_data(size_t chunk_idx) {
    return chunks_[chunk_idx].data;
  }

  const T* chunk_data(size_t chunk_idx) const {
    return chunks_[chunk_idx].data;
  }

  // Get size of elements in a specific chunk
  size_t chunk_size(size_t chunk_idx) const {
    if (chunk_idx >= chunks_.size()) return 0;
    if (chunk_idx < chunks_.size() - 1) return kElementsPerChunk;
    // Last chunk may be partial
    size_t last_chunk_size = size_ % kElementsPerChunk;
    return (last_chunk_size == 0 && size_ > 0) ? kElementsPerChunk : last_chunk_size;
  }

  // Flatten to contiguous vector (for compatibility/GPU upload)
  std::vector<T> flatten() const {
    std::vector<T> result;
    result.reserve(size_);
    for (size_t i = 0; i < chunks_.size(); ++i) {
      size_t count = chunk_size(i);
      result.insert(result.end(), chunks_[i].data, chunks_[i].data + count);
    }
    return result;
  }

  // Copy to pre-allocated buffer (avoids allocation)
  void copy_to(T* dest) const {
    size_t copied = 0;
    for (size_t i = 0; i < chunks_.size() && copied < size_; ++i) {
      size_t count = chunk_size(i);
      std::memcpy(dest + copied, chunks_[i].data, count * sizeof(T));
      copied += count;
    }
  }

  // Iterator support
  class iterator {
   public:
    using value_type = T;
    using pointer = T*;
    using reference = T&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    iterator(ChunkedArray* arr, size_t idx) : arr_(arr), idx_(idx) {}

    reference operator*() { return (*arr_)[idx_]; }
    pointer operator->() { return &(*arr_)[idx_]; }

    iterator& operator++() { ++idx_; return *this; }
    iterator operator++(int) { iterator tmp = *this; ++idx_; return tmp; }
    iterator& operator--() { --idx_; return *this; }
    iterator operator--(int) { iterator tmp = *this; --idx_; return tmp; }

    iterator& operator+=(difference_type n) { idx_ += n; return *this; }
    iterator& operator-=(difference_type n) { idx_ -= n; return *this; }
    iterator operator+(difference_type n) const { return iterator(arr_, idx_ + n); }
    iterator operator-(difference_type n) const { return iterator(arr_, idx_ - n); }
    difference_type operator-(const iterator& other) const {
      return static_cast<difference_type>(idx_) - static_cast<difference_type>(other.idx_);
    }

    bool operator==(const iterator& other) const { return idx_ == other.idx_; }
    bool operator!=(const iterator& other) const { return idx_ != other.idx_; }
    bool operator<(const iterator& other) const { return idx_ < other.idx_; }
    bool operator>(const iterator& other) const { return idx_ > other.idx_; }
    bool operator<=(const iterator& other) const { return idx_ <= other.idx_; }
    bool operator>=(const iterator& other) const { return idx_ >= other.idx_; }

   private:
    ChunkedArray* arr_;
    size_t idx_;
  };

  class const_iterator {
   public:
    using value_type = T;
    using pointer = const T*;
    using reference = const T&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    const_iterator(const ChunkedArray* arr, size_t idx) : arr_(arr), idx_(idx) {}

    reference operator*() const { return (*arr_)[idx_]; }
    pointer operator->() const { return &(*arr_)[idx_]; }

    const_iterator& operator++() { ++idx_; return *this; }
    const_iterator operator++(int) { const_iterator tmp = *this; ++idx_; return tmp; }

    bool operator==(const const_iterator& other) const { return idx_ == other.idx_; }
    bool operator!=(const const_iterator& other) const { return idx_ != other.idx_; }

   private:
    const ChunkedArray* arr_;
    size_t idx_;
  };

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size_); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, size_); }
  const_iterator cbegin() const { return const_iterator(this, 0); }
  const_iterator cend() const { return const_iterator(this, size_); }

  /// Set external allocator function for pool-based allocation
  /// The allocator should return a pointer to kElementsPerChunk elements
  using AllocatorFn = T* (*)(void* user_data, size_t count);
  void set_allocator(AllocatorFn fn, void* user_data) {
    allocator_fn_ = fn;
    allocator_data_ = user_data;
  }

  /// Check if using custom allocator
  bool has_allocator() const { return allocator_fn_ != nullptr; }

 private:
  /// Allocator function that uses MemoryPool
  static T* PoolAllocator(void* user_data, size_t count) {
    auto* pool = static_cast<tinyusdz::next::MemoryPool*>(user_data);
    if (!pool) return nullptr;
    return static_cast<T*>(pool->Allocate(count * sizeof(T)));
  }

  void ensure_capacity(size_t n) {
    while (capacity() < n) {
      T* chunk = nullptr;
      bool is_heap = true;

      if (allocator_fn_) {
        chunk = allocator_fn_(allocator_data_, kElementsPerChunk);
        is_heap = false;
      }

      if (!chunk) {
        // Fallback to heap allocation
        chunk = new T[kElementsPerChunk];
        is_heap = true;
      }

      chunks_.push_back({chunk, is_heap});
    }
  }

  // Chunk storage with ownership flag
  struct Chunk {
    T* data = nullptr;
    bool is_heap_owned = true;

    ~Chunk() {
      if (is_heap_owned && data) {
        delete[] data;
      }
    }

    // Move only
    Chunk() = default;
    Chunk(T* d, bool heap) : data(d), is_heap_owned(heap) {}
    Chunk(Chunk&& o) noexcept : data(o.data), is_heap_owned(o.is_heap_owned) {
      o.data = nullptr;
      o.is_heap_owned = false;
    }
    Chunk& operator=(Chunk&& o) noexcept {
      if (this != &o) {
        if (is_heap_owned && data) delete[] data;
        data = o.data;
        is_heap_owned = o.is_heap_owned;
        o.data = nullptr;
        o.is_heap_owned = false;
      }
      return *this;
    }
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    T& operator[](size_t idx) { return data[idx]; }
    const T& operator[](size_t idx) const { return data[idx]; }
    T* get() { return data; }
    const T* get() const { return data; }
  };

  std::vector<Chunk> chunks_;
  size_t size_ = 0;
  AllocatorFn allocator_fn_ = nullptr;
  void* allocator_data_ = nullptr;
};

// Type aliases for common vertex data
using FloatChunked = ChunkedArray<float>;
using DoubleChunked = ChunkedArray<double>;
using Int32Chunked = ChunkedArray<int32_t>;
using UInt32Chunked = ChunkedArray<uint32_t>;
using UInt16Chunked = ChunkedArray<uint16_t>;
using UInt8Chunked = ChunkedArray<uint8_t>;

// Vec3 chunk (for positions, normals)
struct alignas(16) Vec3f {
  float x, y, z;
  float _pad;  // Padding for alignment
};
using Vec3fChunked = ChunkedArray<Vec3f>;

// Vec2 chunk (for UVs)
struct Vec2f {
  float u, v;
};
using Vec2fChunked = ChunkedArray<Vec2f>;

// Vec4 chunk (for colors, tangents)
struct alignas(16) Vec4f {
  float x, y, z, w;
};
using Vec4fChunked = ChunkedArray<Vec4f>;

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
