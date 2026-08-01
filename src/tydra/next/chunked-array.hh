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

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include "safe-arithmetic.hh"
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

namespace tinyusdz {
namespace tydra {
namespace next {

// Default chunk size: 64KB (good for WASM page alignment)
constexpr size_t kDefaultChunkSize = 64 * 1024;

// Pre-flight probe for a large TEMPORARY allocation. Built with
// -fno-exceptions, where an allocator failure aborts the whole module
// ("bad_alloc was thrown in -fno-exceptions mode"); probing with malloc and
// releasing immediately lets converters fail a single prim with an error
// instead. Meaningful on single-threaded wasm (the heap is grow-only, so a
// successful probe means the following allocation of the same size succeeds).
inline bool ProbeAlloc(size_t bytes) {
  if (bytes == 0) return true;
  void* p = std::malloc(bytes);
  if (!p) return false;
  std::free(p);
  return true;
}

// ChunkedArray: Fixed-size chunk allocation for large arrays
// - Each chunk is allocated separately (no realloc)
// - Elements accessed via index or iterator
// - Can get contiguous data via flatten() for GPU upload
template <typename T, size_t ChunkBytes = kDefaultChunkSize>
class ChunkedArray {
 public:
  static constexpr size_t kElementsPerChunk = ChunkBytes / sizeof(T);
  static_assert(kElementsPerChunk > 0, "Element size too large for chunk");
  static_assert(std::is_trivially_copyable<T>::value,
                "ChunkedArray requires a trivially copyable element type");

  ChunkedArray() = default;
  ~ChunkedArray() = default;

  // Move only (no copy to avoid accidental large copies)
  ChunkedArray(ChunkedArray&&) = default;
  ChunkedArray& operator=(ChunkedArray&&) = default;
  ChunkedArray(const ChunkedArray&) = delete;
  ChunkedArray& operator=(const ChunkedArray&) = delete;

  // Growth uses nothrow allocation: under -fno-exceptions (wasm) a throwing
  // operator new would abort the whole module, so a failed chunk allocation
  // instead latches alloc_failed(), leaves size()/contents unchanged, and the
  // grow call reports false. Callers converting untrusted scenes check either
  // the call result (preferred for resize+fill patterns) or alloc_failed()
  // after a conversion step and drop the prim with an error.

  // Reserve space for n elements (pre-allocates chunks)
  bool reserve(size_t n) { return ensure_capacity(n); }

  // Add element, returns index (SIZE_MAX on allocation failure)
  size_t push_back(const T& value) {
    size_t idx = size_;
    if (size_ == (std::numeric_limits<size_t>::max)()) {
      alloc_failed_ = true;
      return static_cast<size_t>(-1);
    }
    if (!ensure_capacity(size_ + 1)) return static_cast<size_t>(-1);
    (*this)[idx] = value;
    ++size_;
    return idx;
  }

  size_t push_back(T&& value) {
    size_t idx = size_;
    if (size_ == (std::numeric_limits<size_t>::max)()) {
      alloc_failed_ = true;
      return static_cast<size_t>(-1);
    }
    if (!ensure_capacity(size_ + 1)) return static_cast<size_t>(-1);
    (*this)[idx] = std::move(value);
    ++size_;
    return idx;
  }

  // Add multiple elements from contiguous array
  bool append(const T* data, size_t count) {
    if (count == 0) return true;
    if (!data || count > (std::numeric_limits<size_t>::max)() - size_) {
      alloc_failed_ = true;
      return false;
    }
    if (!ensure_capacity(size_ + count)) return false;

    size_t remaining = count;
    const T* src = data;

    while (remaining > 0) {
      size_t chunk_idx = size_ / kElementsPerChunk;
      size_t offset = size_ % kElementsPerChunk;
      size_t space_in_chunk = kElementsPerChunk - offset;
      size_t to_copy = (remaining < space_in_chunk) ? remaining : space_in_chunk;

      size_t copy_bytes;
      if (!safe::mul(to_copy, sizeof(T), &copy_bytes)) return false;
      std::memcpy(chunks_[chunk_idx].get() + offset, src, copy_bytes);

      src += to_copy;
      size_ += to_copy;
      remaining -= to_copy;
    }
    return true;
  }

  // Resize (may add uninitialized elements). On allocation failure the size
  // stays unchanged and false is returned — resize+fill callers must check.
  bool resize(size_t n) {
    if (!ensure_capacity(n)) return false;
    size_ = n;
    return true;
  }

  bool resize(size_t n, const T& value) {
    size_t old_size = size_;
    if (!ensure_capacity(n)) return false;
    size_ = n;
    for (size_t i = old_size; i < n; ++i) {
      (*this)[i] = value;
    }
    return true;
  }

  // Latched when a chunk allocation failed; the array contents remain valid
  // at their pre-failure size.
  bool alloc_failed() const { return alloc_failed_; }

  void clear() {
    size_ = 0;
    // Keep chunks allocated for reuse
  }

  // Release excess memory: drop whole chunks past the logical size and
  // reallocate the final partial chunk to its exact element count. Scenes with
  // thousands of SMALL meshes would otherwise pay the full 64KB minimum chunk
  // for every non-empty array (~450KB per RenderMesh), which alone OOMs the
  // 2GB wasm32 heap on large prop scenes. Appending after shrink re-expands
  // the tail chunk transparently.
  void shrink_to_fit() {
    const size_t needed_chunks =
        size_ / kElementsPerChunk + (size_ % kElementsPerChunk != 0);
    while (chunks_.size() > needed_chunks) {
      chunks_.pop_back();
    }
    if (chunks_.empty()) {
      tail_capacity_ = 0;
      return;
    }
    tail_capacity_ = kElementsPerChunk;  // dropped chunks restore full tails
    const size_t used_in_tail = size_ - (chunks_.size() - 1) * kElementsPerChunk;
    if (used_in_tail < kElementsPerChunk) {
      T* exact = new (std::nothrow) T[used_in_tail];
      if (!exact) return;  // keep the full-size chunk; compaction is optional
      size_t tail_bytes;
      if (!safe::mul(used_in_tail, sizeof(T), &tail_bytes)) return;
      std::memcpy(exact, chunks_.back().get(), tail_bytes);
      chunks_.back().reset(exact);
      tail_capacity_ = used_in_tail;
    }
  }

  // Access
  T& operator[](size_t idx) {
    size_t chunk_idx = idx / kElementsPerChunk;
    size_t offset = idx % kElementsPerChunk;
    return chunks_[chunk_idx][offset];
  }

  const T& operator[](size_t idx) const {
    size_t chunk_idx = idx / kElementsPerChunk;
    size_t offset = idx % kElementsPerChunk;
    return chunks_[chunk_idx][offset];
  }

  // Bounds-checked access. Built with -fno-exceptions, so an out-of-range index
  // is a fatal programming error: report and abort immediately.
  T& at(size_t idx) {
    if (idx >= size_) {
      std::fprintf(stderr, "ChunkedArray::at: index %zu out of range (size %zu)\n",
                   idx, size_);
      std::abort();
    }
    return (*this)[idx];
  }

  const T& at(size_t idx) const {
    if (idx >= size_) {
      std::fprintf(stderr, "ChunkedArray::at: index %zu out of range (size %zu)\n",
                   idx, size_);
      std::abort();
    }
    return (*this)[idx];
  }

  T& front() { return (*this)[0]; }
  const T& front() const { return (*this)[0]; }
  T& back() { return (*this)[size_ - 1]; }
  const T& back() const { return (*this)[size_ - 1]; }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  size_t chunk_count() const { return chunks_.size(); }
  size_t capacity() const {
    if (chunks_.empty()) return 0;
    if ((chunks_.size() - 1) >
        ((std::numeric_limits<size_t>::max)() - tail_capacity_) /
            kElementsPerChunk) {
      return (std::numeric_limits<size_t>::max)();
    }
    return (chunks_.size() - 1) * kElementsPerChunk + tail_capacity_;
  }

  // True when the data already lives in one contiguous block (fits in the
  // first chunk), i.e. chunk_data(0) can be handed out directly without
  // flattening. Uses the LOGICAL size: extra reserved chunks don't matter.
  bool is_contiguous() const { return size_ <= kElementsPerChunk; }

  // Memory usage in bytes
  size_t memory_usage() const {
    if (chunks_.empty()) return sizeof(*this);
    const size_t max_size = (std::numeric_limits<size_t>::max)();
    const size_t tail_bytes = tail_capacity_ * sizeof(T);
    if (tail_bytes > max_size - sizeof(*this)) return max_size;
    if ((chunks_.size() - 1) >
        (max_size - tail_bytes - sizeof(*this)) / ChunkBytes) {
      return max_size;
    }
    return (chunks_.size() - 1) * ChunkBytes + tail_bytes + sizeof(*this);
  }

  // Get pointer to chunk data (for direct GPU upload)
  T* chunk_data(size_t chunk_idx) {
    return chunks_[chunk_idx].get();
  }

  const T* chunk_data(size_t chunk_idx) const {
    return chunks_[chunk_idx].get();
  }

  // Number of DATA elements in a chunk, derived from the logical size —
  // NOT from chunks_.size(): reserve() (or clear() + smaller refill) leaves
  // more chunks allocated than the logical size covers, and sizing the "last"
  // chunk off the allocation count made copy_to()/flatten() read (and memcpy
  // into exact-sized destination buffers!) whole 64KB chunks past the end.
  size_t chunk_size(size_t chunk_idx) const {
    if (chunk_idx >= chunks_.size() ||
        chunk_idx > (std::numeric_limits<size_t>::max)() /
                        kElementsPerChunk) {
      return 0;
    }
    const size_t begin = chunk_idx * kElementsPerChunk;
    if (begin >= size_) return 0;
    const size_t remaining = size_ - begin;
    return remaining < kElementsPerChunk ? remaining : kElementsPerChunk;
  }

  // Flatten to contiguous vector (for compatibility/GPU upload)
  std::vector<T> flatten() const {
    std::vector<T> result;
    result.reserve(size_);
    for (size_t i = 0; i < chunks_.size(); ++i) {
      size_t count = chunk_size(i);
      if (count == 0) break;
      result.insert(result.end(), chunks_[i].get(), chunks_[i].get() + count);
    }
    return result;
  }

  // Copy to pre-allocated buffer of exactly size() elements.
  void copy_to(T* dest) const {
    size_t copied = 0;
    for (size_t i = 0; i < chunks_.size() && copied < size_; ++i) {
      size_t count = chunk_size(i);
      if (count == 0) break;
      size_t chunk_bytes;
      if (!safe::mul(count, sizeof(T), &chunk_bytes)) return;
      std::memcpy(dest + copied, chunks_[i].get(), chunk_bytes);
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

 private:
  bool ensure_capacity(size_t n) {
    if (capacity() >= n) return true;
    // No single C++ object may exceed PTRDIFF_MAX bytes. Reject before the
    // allocator probe: ASan and several wasm allocators abort (even for
    // nothrow new/malloc) when asked to probe an address-space-sized block.
    const size_t max_elements =
        static_cast<size_t>((std::numeric_limits<std::ptrdiff_t>::max)()) /
        sizeof(T);
    if (n >= max_elements) {
      alloc_failed_ = true;
      return false;
    }
    // Guard the chunk-vector growth too: reserve with a nothrow probe so the
    // push_back below cannot throw-abort under -fno-exceptions.
    const size_t needed_chunks =
        (n + kElementsPerChunk - 1) / kElementsPerChunk;
    size_t probe_bytes;
    if (!safe::mul3(needed_chunks, sizeof(chunks_[0]), size_t(2),
                    &probe_bytes)) {
      alloc_failed_ = true;
      return false;
    }
    if (needed_chunks > chunks_.capacity() && !ProbeAlloc(probe_bytes)) {
      alloc_failed_ = true;
      return false;
    }
    // A shrunken (exact-size) tail chunk must grow back to full capacity
    // before more chunks are appended, so only the LAST chunk is ever short.
    if (!chunks_.empty() && tail_capacity_ < kElementsPerChunk) {
      T* full = new (std::nothrow) T[kElementsPerChunk];
      if (!full) {
        alloc_failed_ = true;
        return false;
      }
      size_t tail_bytes;
      if (!safe::mul(tail_capacity_, sizeof(T), &tail_bytes)) {
        alloc_failed_ = true;
        return false;
      }
      std::memcpy(full, chunks_.back().get(), tail_bytes);
      chunks_.back().reset(full);
      tail_capacity_ = kElementsPerChunk;
    }
    while (capacity() < n) {
      T* chunk = new (std::nothrow) T[kElementsPerChunk];
      if (!chunk) {
        alloc_failed_ = true;
        return false;
      }
      chunks_.push_back(std::unique_ptr<T[]>(chunk));
      tail_capacity_ = kElementsPerChunk;
    }
    return true;
  }

  std::vector<std::unique_ptr<T[]>> chunks_;
  size_t size_ = 0;
  // Element capacity of the LAST chunk (all others are kElementsPerChunk).
  // 0 when no chunks are allocated.
  size_t tail_capacity_ = 0;
  bool alloc_failed_ = false;
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
