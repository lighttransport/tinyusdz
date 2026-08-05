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
#include <iterator>
#include <limits>
#include <new>
#include <vector>
#include "safe-arithmetic.hh"
#include <memory>
#include <type_traits>

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

// Optional accounting hooks for chunk allocations.
//
// A budget wants to (a) see the bulk geometry it is supposed to be bounding
// and (b) refuse an allocation that would bust the cap, at the point of
// allocation rather than only at the next phase boundary. MemBudget provides
// exactly that through non-throwing TryAdd/Sub -- but this header cannot
// include mem-budget.hh (it drags in <fstream>, <mutex> and windows.h into
// every translation unit that touches a mesh), and PoolAlloc is unusable here
// anyway because it reports exhaustion by throwing and the wasm converter is
// built -fno-exceptions.
//
// So the dependency is inverted: this header declares the hooks, and whoever
// owns the budget installs them (MemBudget::InstallChunkedArrayTracking()).
// Unset by default -- one predictable null check per 64KB chunk, which is
// nothing next to the allocation itself.
//
// Single-threaded install, like the rest of the conversion pipeline.
using ChunkAllocHook = bool (*)(size_t bytes);  // false => refuse
using ChunkFreeHook = void (*)(size_t bytes);

inline ChunkAllocHook& ChunkAllocHookRef() {
  static ChunkAllocHook hook = nullptr;
  return hook;
}
inline ChunkFreeHook& ChunkFreeHookRef() {
  static ChunkFreeHook hook = nullptr;
  return hook;
}
inline void SetChunkAllocHooks(ChunkAllocHook alloc, ChunkFreeHook free_hook) {
  ChunkAllocHookRef() = alloc;
  ChunkFreeHookRef() = free_hook;
}

/// Allocate `n` elements, charged to the budget if one is installed. Returns
/// nullptr when the allocation fails OR the budget refuses it -- callers
/// already treat that as an allocation failure (latching alloc_failed()), so a
/// bust becomes a clean skip at the point of allocation instead of an OOM
/// between phase checks.
template <typename T>
inline T* AllocChunkElems(size_t n) {
  const size_t bytes = n * sizeof(T);
  ChunkAllocHook hook = ChunkAllocHookRef();
  if (hook && !hook(bytes)) return nullptr;
  T* p = new (std::nothrow) T[n];
  if (!p && hook) ChunkFreeHookRef()(bytes);  // hand the reservation back
  return p;
}

/// Release a charge. The chunks themselves are owned by unique_ptr, so the
/// raw delete[] stays there; only the accounting is unwound here.
inline void UnchargeChunkBytes(size_t bytes) {
  if (bytes == 0) return;
  if (ChunkFreeHook hook = ChunkFreeHookRef()) hook(bytes);
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

  // Move only (no implicit copy, to avoid accidental large copies).
  ChunkedArray(ChunkedArray&&) = default;
  ChunkedArray& operator=(ChunkedArray&&) = default;
  ChunkedArray(const ChunkedArray&) = delete;
  ChunkedArray& operator=(const ChunkedArray&) = delete;

  // O(1) copy-on-write share. The two arrays then reference the SAME chunks;
  // the first write through EITHER of them transparently takes a private copy
  // first (see detach_if_shared), so this is safe even if a consumer later
  // mutates one of them. Used to clone a mesh's immutable topology and vertex
  // attributes per point instance without duplicating the bytes: N instances
  // of a prototype cost N x (points+normals+tangents) + 1 x everything else,
  // instead of N x the whole mesh.
  void share_from(const ChunkedArray& other) {
    if (this == &other) return;
    chunks_storage_ = other.chunks_storage_;
    size_ = other.size_;
    tail_capacity_ = other.tail_capacity_;
    alloc_failed_ = other.alloc_failed_;
    if (chunks_storage_) {
      maybe_shared_ = true;
      other.maybe_shared_ = true;
    } else {
      maybe_shared_ = false;
    }
  }

  // True while the chunks are (possibly) shared with another array. Diagnostic
  // / accounting aid: memory_usage() reports the full byte count on BOTH
  // holders, so a budget must not add shared arrays twice.
  bool is_shared() const {
    return maybe_shared_ && chunks_storage_ && chunks_storage_.use_count() > 1;
  }

  // Growth uses nothrow allocation: under -fno-exceptions (wasm) a throwing
  // operator new would abort the whole module, so a failed chunk allocation
  // instead latches alloc_failed(), leaves size()/contents unchanged, and the
  // grow call reports false. Callers converting untrusted scenes check either
  // the call result (preferred for resize+fill patterns) or alloc_failed()
  // after a conversion step and drop the prim with an error.

  // Reserve space for n elements (pre-allocates chunks)
  bool reserve(size_t n) {
    detach_if_shared();
    return ensure_capacity(n);
  }

  // Add element, returns index (SIZE_MAX on allocation failure)
  size_t push_back(const T& value) {
    detach_if_shared();
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
    detach_if_shared();
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
    detach_if_shared();
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

      std::memcpy(chunks_storage_->v[chunk_idx].get() + offset, src,
                  to_copy * sizeof(T));

      src += to_copy;
      size_ += to_copy;
      remaining -= to_copy;
    }
    return true;
  }

  // Resize (may add uninitialized elements). On allocation failure the size
  // stays unchanged and false is returned — resize+fill callers must check.
  bool resize(size_t n) {
    detach_if_shared();
    if (!ensure_capacity(n)) return false;
    size_ = n;
    return true;
  }

  bool resize(size_t n, const T& value) {
    detach_if_shared();
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
    if (!chunks_storage_) return;
    detach_if_shared();
    const size_t needed_chunks =
        size_ / kElementsPerChunk + (size_ % kElementsPerChunk != 0);
    while (chunks_mut().size() > needed_chunks) {
      // Only the LAST chunk can be a shrunken (exact) tail; read its size
      // before dropping it. Whatever becomes the new last is full.
      const size_t bytes =
          (tail_capacity_ ? tail_capacity_ : kElementsPerChunk) * sizeof(T);
      chunks_mut().pop_back();
      chunks_storage_->charged_bytes -=
          (bytes < chunks_storage_->charged_bytes)
              ? bytes
              : chunks_storage_->charged_bytes;
      UnchargeChunkBytes(bytes);
      tail_capacity_ = kElementsPerChunk;
    }
    if (chunks_mut().empty()) {
      tail_capacity_ = 0;
      return;
    }
    tail_capacity_ = kElementsPerChunk;  // dropped chunks restore full tails
    const size_t used_in_tail = size_ - (chunks_mut().size() - 1) * kElementsPerChunk;
    if (used_in_tail < kElementsPerChunk) {
      T* exact = AllocChunkElems<T>(used_in_tail);
      if (!exact) return;  // keep the full-size chunk; compaction is optional
      std::memcpy(exact, chunks_mut().back().get(), used_in_tail * sizeof(T));
      chunks_mut().back().reset(exact);  // raw delete[] of the full tail
      // AllocChunkElems charged the NEW (exact) tail; release the FULL chunk
      // it replaced -- not the difference, or the new tail is counted twice.
      const size_t added = used_in_tail * sizeof(T);
      const size_t released = kElementsPerChunk * sizeof(T);
      chunks_storage_->charged_bytes += added;
      chunks_storage_->charged_bytes -=
          (released < chunks_storage_->charged_bytes)
              ? released
              : chunks_storage_->charged_bytes;
      UnchargeChunkBytes(released);
      tail_capacity_ = used_in_tail;
    }
  }

  // Access
  T& operator[](size_t idx) {
    detach_if_shared();
    size_t chunk_idx = idx / kElementsPerChunk;
    size_t offset = idx % kElementsPerChunk;
    return chunks_storage_->v[chunk_idx][offset];
  }

  const T& operator[](size_t idx) const {
    size_t chunk_idx = idx / kElementsPerChunk;
    size_t offset = idx % kElementsPerChunk;
    return chunks_storage_->v[chunk_idx][offset];
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

  // Copy out N consecutive elements starting at `base`.
  //
  // NEVER take `&arr[i]` and then index off it: a chunk is a separate
  // allocation, so a run of elements straddles the chunk boundary whenever
  // `base % kElementsPerChunk + N > kElementsPerChunk`. For the interleaved
  // vertex arrays that is routine (FloatChunked holds 16384 elements/chunk and
  // 16384 % 3 == 1, so every ~5461st xyz triple straddles), and the tail chunk
  // is exact-sized so reading past it is out of bounds even for N == 2.
  // Callers must have bounds-checked `base + N <= size()`.
  template <size_t N>
  void read_n(size_t base, T* out) const {
    const size_t chunk_idx = base / kElementsPerChunk;
    const size_t offset = base % kElementsPerChunk;
    if (offset + N <= kElementsPerChunk) {
      const T* p = chunks()[chunk_idx].get() + offset;
      for (size_t i = 0; i < N; ++i) out[i] = p[i];
      return;
    }
    for (size_t i = 0; i < N; ++i) out[i] = (*this)[base + i];
  }

  void read2(size_t base, T* out) const { read_n<2>(base, out); }
  void read3(size_t base, T* out) const { read_n<3>(base, out); }

  T& front() { return (*this)[0]; }
  const T& front() const { return (*this)[0]; }
  T& back() { return (*this)[size_ - 1]; }
  const T& back() const { return (*this)[size_ - 1]; }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  size_t chunk_count() const { return chunks().size(); }
  size_t capacity() const {
    if (chunks().empty()) return 0;
    if ((chunks().size() - 1) >
        ((std::numeric_limits<size_t>::max)() - tail_capacity_) /
            kElementsPerChunk) {
      return (std::numeric_limits<size_t>::max)();
    }
    return (chunks().size() - 1) * kElementsPerChunk + tail_capacity_;
  }

  // True when the data already lives in one contiguous block (fits in the
  // first chunk), i.e. chunk_data(0) can be handed out directly without
  // flattening. Uses the LOGICAL size: extra reserved chunks don't matter.
  bool is_contiguous() const { return size_ <= kElementsPerChunk; }

  // Memory usage in bytes
  size_t memory_usage() const {
    if (chunks().empty()) return sizeof(*this);
    const size_t max_size = (std::numeric_limits<size_t>::max)();
    const size_t tail_bytes = tail_capacity_ * sizeof(T);
    if (tail_bytes > max_size - sizeof(*this)) return max_size;
    if ((chunks().size() - 1) >
        (max_size - tail_bytes - sizeof(*this)) / ChunkBytes) {
      return max_size;
    }
    return (chunks().size() - 1) * ChunkBytes + tail_bytes + sizeof(*this);
  }

  // Get pointer to chunk data (for direct GPU upload)
  T* chunk_data(size_t chunk_idx) {
    detach_if_shared();
    return chunks_storage_->v[chunk_idx].get();
  }

  const T* chunk_data(size_t chunk_idx) const {
    return chunks()[chunk_idx].get();
  }

  // Number of DATA elements in a chunk, derived from the logical size —
  // NOT from chunks().size(): reserve() (or clear() + smaller refill) leaves
  // more chunks allocated than the logical size covers, and sizing the "last"
  // chunk off the allocation count made copy_to()/flatten() read (and memcpy
  // into exact-sized destination buffers!) whole 64KB chunks past the end.
  size_t chunk_size(size_t chunk_idx) const {
    if (chunk_idx >= chunks().size() ||
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
    for (size_t i = 0; i < chunks().size(); ++i) {
      size_t count = chunk_size(i);
      if (count == 0) break;
      result.insert(result.end(), chunks()[i].get(), chunks()[i].get() + count);
    }
    return result;
  }

  // Copy to pre-allocated buffer of exactly size() elements.
  bool copy_to(T* dest) const {
    if (size_ != 0 && !dest) return false;
    size_t copied = 0;
    for (size_t i = 0; i < chunks().size() && copied < size_; ++i) {
      size_t count = chunk_size(i);
      if (count == 0) break;
      std::memcpy(dest + copied, chunks()[i].get(), count * sizeof(T));
      copied += count;
    }
    return copied == size_;
  }

  bool copy_to(T* dest, size_t dest_count) const {
    if (dest_count < size_) return false;
    return copy_to(dest);
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
  // Empty-safe read view of the chunk vector.
  static const std::vector<std::unique_ptr<T[]>>& empty_chunks() {
    static const std::vector<std::unique_ptr<T[]>> kEmpty;
    return kEmpty;
  }
  const std::vector<std::unique_ptr<T[]>>& chunks() const {
    return chunks_storage_ ? chunks_storage_->v : empty_chunks();
  }
  // Mutable access: MUST be preceded by detach_if_shared().
  std::vector<std::unique_ptr<T[]>>& chunks_mut() {
    if (!chunks_storage_) chunks_storage_ = std::make_shared<Chunks>();
    return chunks_storage_->v;
  }

  // Take a private copy of the chunks if they are shared. Every mutating entry
  // point calls this FIRST -- including ones that only overwrite existing
  // elements (append after clear(), operator[] writes), not just ones that
  // grow, since those write through the shared buffers just the same.
  void detach_if_shared() {
    if (!maybe_shared_) return;
    maybe_shared_ = false;
    if (!chunks_storage_ || chunks_storage_.use_count() <= 1) return;
    auto copy = std::make_shared<Chunks>();
    const std::vector<std::unique_ptr<T[]>>& src = chunks_storage_->v;
    copy->v.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
      // Every chunk is kElementsPerChunk long except the last, which may have
      // been shrunk to tail_capacity_.
      const size_t n =
          (i + 1 == src.size()) ? tail_capacity_ : kElementsPerChunk;
      T* c = AllocChunkElems<T>(n);
      if (!c) {
        alloc_failed_ = true;
        // Keep sharing rather than hand back a truncated array; the caller's
        // write is dropped along with the latched failure flag. `copy` is
        // discarded here and its ~Chunks unwinds whatever it charged.
        return;
      }
      copy->charged_bytes += n * sizeof(T);
      if (src[i]) std::memcpy(c, src[i].get(), n * sizeof(T));
      copy->v.push_back(std::unique_ptr<T[]>(c));
    }
    chunks_storage_ = std::move(copy);
  }

  bool ensure_capacity(size_t n) {
    if (capacity() >= n) return true;
    detach_if_shared();  // defensive: every caller already detached
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
        n / kElementsPerChunk + (n % kElementsPerChunk != 0);
    if (needed_chunks >
        (std::numeric_limits<size_t>::max)() / sizeof(std::unique_ptr<T[]>) / 2) {
      alloc_failed_ = true;
      return false;
    }
    if (needed_chunks > chunks_mut().capacity() &&
        !ProbeAlloc(needed_chunks * sizeof(std::unique_ptr<T[]>) * 2)) {
      alloc_failed_ = true;
      return false;
    }
    // A shrunken (exact-size) tail chunk must grow back to full capacity
    // before more chunks are appended, so only the LAST chunk is ever short.
    if (!chunks_mut().empty() && tail_capacity_ < kElementsPerChunk) {
      T* full = AllocChunkElems<T>(kElementsPerChunk);
      if (!full) {
        alloc_failed_ = true;
        return false;
      }
      const size_t old_tail_bytes = tail_capacity_ * sizeof(T);
      std::memcpy(full, chunks_mut().back().get(), tail_capacity_ * sizeof(T));
      chunks_mut().back().reset(full);  // raw delete[] of the short tail
      // AllocChunkElems already charged the FULL chunk; give back the short
      // tail it replaced.
      chunks_storage_->charged_bytes +=
          (kElementsPerChunk * sizeof(T)) - old_tail_bytes;
      UnchargeChunkBytes(old_tail_bytes);
      tail_capacity_ = kElementsPerChunk;
    }
    while (capacity() < n) {
      T* chunk = AllocChunkElems<T>(kElementsPerChunk);
      if (!chunk) {
        alloc_failed_ = true;
        return false;
      }
      chunks_mut().push_back(std::unique_ptr<T[]>(chunk));
      chunks_storage_->charged_bytes += kElementsPerChunk * sizeof(T);
      tail_capacity_ = kElementsPerChunk;
    }
    return true;
  }

  // Chunk storage, shared copy-on-write (see share_from). Only the CHUNKS are
  // shared: size_/tail_capacity_/alloc_failed_ stay per-instance so clear()
  // and the size queries never have to detach.
  struct Chunks {
    std::vector<std::unique_ptr<T[]>> v;
    // Bytes charged to the budget for the chunks in `v`. Owned by the shared
    // storage, not by the ChunkedArray: with copy-on-write sharing the holders
    // come and go, but the charge must be released exactly once, when the last
    // one drops the storage.
    size_t charged_bytes = 0;
    ~Chunks() { UnchargeChunkBytes(charged_bytes); }
  };
  std::shared_ptr<Chunks> chunks_storage_;
  // Set whenever this instance has shared its storage with, or taken it from,
  // another. Lets the mutating fast path test one predictable bool instead of
  // an atomic use_count() load on every element write. Cleared once we know
  // the storage is exclusively ours. NOT thread-safe -- RenderMesh conversion
  // is single-threaded by construction.
  mutable bool maybe_shared_ = false;

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
