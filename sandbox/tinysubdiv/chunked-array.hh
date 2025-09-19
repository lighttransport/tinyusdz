#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace tinysubdiv {

template <typename T, size_t ChunkSize = 4096>
class ChunkedTypedArray {
 public:
  static constexpr size_t kChunkSize = ChunkSize;
  static constexpr size_t kElementsPerChunk = ChunkSize / sizeof(T);

  ChunkedTypedArray() = default;
  ~ChunkedTypedArray() = default;

  ChunkedTypedArray(const ChunkedTypedArray&) = delete;
  ChunkedTypedArray& operator=(const ChunkedTypedArray&) = delete;

  ChunkedTypedArray(ChunkedTypedArray&&) = default;
  ChunkedTypedArray& operator=(ChunkedTypedArray&&) = default;

  void reserve(size_t capacity) {
    if (capacity <= capacity_) return;

    const size_t num_chunks = (capacity + kElementsPerChunk - 1) / kElementsPerChunk;
    chunks_.reserve(num_chunks);

    while (chunks_.size() < num_chunks) {
      chunks_.emplace_back(std::make_unique<T[]>(kElementsPerChunk));
    }
    capacity_ = num_chunks * kElementsPerChunk;
  }

  void push_back(const T& value) {
    ensure_capacity(size_ + 1);
    (*this)[size_++] = value;
  }

  void push_back(T&& value) {
    ensure_capacity(size_ + 1);
    (*this)[size_++] = std::move(value);
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    ensure_capacity(size_ + 1);
    new (&(*this)[size_]) T(std::forward<Args>(args)...);
    ++size_;
  }

  void resize(size_t new_size) {
    if (new_size > capacity_) {
      reserve(new_size);
    }

    if (new_size > size_) {
      // Initialize new elements
      ensure_capacity(new_size);
      for (size_t i = size_; i < new_size; ++i) {
        const size_t chunk_idx = i / kElementsPerChunk;
        const size_t element_idx = i % kElementsPerChunk;
        new (&chunks_[chunk_idx][element_idx]) T();
      }
    } else if (new_size < size_) {
      // Destroy excess elements
      for (size_t i = new_size; i < size_; ++i) {
        const size_t chunk_idx = i / kElementsPerChunk;
        const size_t element_idx = i % kElementsPerChunk;
        chunks_[chunk_idx][element_idx].~T();
      }
    }
    size_ = new_size;
  }

  void clear() {
    resize(0);
  }

  T& operator[](size_t index) {
    assert(index < size_);
    const size_t chunk_idx = index / kElementsPerChunk;
    const size_t element_idx = index % kElementsPerChunk;
    return chunks_[chunk_idx][element_idx];
  }

  const T& operator[](size_t index) const {
    assert(index < size_);
    const size_t chunk_idx = index / kElementsPerChunk;
    const size_t element_idx = index % kElementsPerChunk;
    return chunks_[chunk_idx][element_idx];
  }

  T& at(size_t index) {
    if (index >= size_) {
      throw std::out_of_range("ChunkedTypedArray::at");
    }
    return (*this)[index];
  }

  const T& at(size_t index) const {
    if (index >= size_) {
      throw std::out_of_range("ChunkedTypedArray::at");
    }
    return (*this)[index];
  }

  T* get_chunk_ptr(size_t chunk_index) {
    if (chunk_index >= chunks_.size()) return nullptr;
    return chunks_[chunk_index].get();
  }

  const T* get_chunk_ptr(size_t chunk_index) const {
    if (chunk_index >= chunks_.size()) return nullptr;
    return chunks_[chunk_index].get();
  }

  size_t get_chunk_count() const {
    return chunks_.size();
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return size_ == 0; }

  void copy_to(T* dest) const {
    size_t copied = 0;
    for (size_t chunk_idx = 0; chunk_idx < chunks_.size() && copied < size_; ++chunk_idx) {
      const size_t elements_in_chunk = std::min(kElementsPerChunk, size_ - copied);
      std::memcpy(dest + copied, chunks_[chunk_idx].get(), elements_in_chunk * sizeof(T));
      copied += elements_in_chunk;
    }
  }

  void copy_from(const T* src, size_t count) {
    resize(count);
    size_t copied = 0;
    for (size_t chunk_idx = 0; chunk_idx < chunks_.size() && copied < count; ++chunk_idx) {
      const size_t elements_to_copy = std::min(kElementsPerChunk, count - copied);
      std::memcpy(chunks_[chunk_idx].get(), src + copied, elements_to_copy * sizeof(T));
      copied += elements_to_copy;
    }
  }

  template <typename Func>
  void for_each_chunk(Func&& func) {
    for (size_t chunk_idx = 0; chunk_idx < chunks_.size(); ++chunk_idx) {
      const size_t start_idx = chunk_idx * kElementsPerChunk;
      const size_t end_idx = std::min(start_idx + kElementsPerChunk, size_);
      if (start_idx < size_) {
        func(chunks_[chunk_idx].get(), start_idx, end_idx - start_idx);
      }
    }
  }

  template <typename Func>
  void for_each_chunk(Func&& func) const {
    for (size_t chunk_idx = 0; chunk_idx < chunks_.size(); ++chunk_idx) {
      const size_t start_idx = chunk_idx * kElementsPerChunk;
      const size_t end_idx = std::min(start_idx + kElementsPerChunk, size_);
      if (start_idx < size_) {
        func(chunks_[chunk_idx].get(), start_idx, end_idx - start_idx);
      }
    }
  }

 private:
  void ensure_capacity(size_t required_size) {
    if (required_size > capacity_) {
      size_t new_capacity = capacity_ == 0 ? kElementsPerChunk : capacity_ * 2;
      reserve(std::max(required_size, new_capacity));
    }
  }

  std::vector<std::unique_ptr<T[]>> chunks_;
  size_t size_ = 0;
  size_t capacity_ = 0;
};

template <typename T, size_t ChunkSize = 4096>
class AlignedChunkedArray : public ChunkedTypedArray<T, ChunkSize> {
 public:
  static constexpr size_t kAlignment = 64; // Cache line size

  void reserve(size_t capacity) {
    const size_t elements_per_chunk = this->kElementsPerChunk;
    const size_t num_chunks = (capacity + elements_per_chunk - 1) / elements_per_chunk;
    aligned_chunks_.reserve(num_chunks);

    while (aligned_chunks_.size() < num_chunks) {
      void* ptr = nullptr;
#ifdef _WIN32
      ptr = _aligned_malloc(elements_per_chunk * sizeof(T), kAlignment);
#else
      if (posix_memalign(&ptr, kAlignment, elements_per_chunk * sizeof(T)) != 0) {
        ptr = nullptr;
      }
#endif
      if (!ptr) {
        throw std::bad_alloc();
      }
      aligned_chunks_.emplace_back(static_cast<T*>(ptr));
    }
  }

  ~AlignedChunkedArray() {
    for (auto& chunk : aligned_chunks_) {
#ifdef _WIN32
      _aligned_free(chunk);
#else
      free(chunk);
#endif
    }
  }

 private:
  std::vector<T*> aligned_chunks_;
};

}  // namespace tinysubdiv