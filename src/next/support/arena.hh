// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Monotonic bump arena
//
// A C-style region allocator: it hands out uninitialized, aligned memory from a
// chain of malloc'd blocks and frees everything at once. Individual objects are
// never freed and their destructors are NOT run, so the arena is only for
// trivially-destructible / POD transient data (parse scratch, name pooling,
// index buffers). This trades many small malloc/free calls for a handful of big
// ones, which is the "less C++ object creation" lever from the next-core plan.
//
// Usage:
//   Arena a;
//   auto* ids = a.alloc_uninitialized<uint32_t>(count);   // uninitialized
//   std::string_view name = a.copy_string(token.text);    // pooled, NUL-term
//   a.reset();   // reuse the blocks for the next parse (no free)
//
// Not thread-safe; give each worker its own Arena.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace tinyusdz {
namespace next {

class Arena {
 public:
  static constexpr size_t kDefaultBlockSize = 64 * 1024;

  Arena() = default;
  explicit Arena(size_t first_block_size)
      : next_block_size_(first_block_size ? first_block_size : kDefaultBlockSize) {}
  ~Arena() { release(); }

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  Arena(Arena&& o) noexcept { steal(o); }
  Arena& operator=(Arena&& o) noexcept {
    if (this != &o) {
      release();
      steal(o);
    }
    return *this;
  }

  /// Allocate `bytes` of uninitialized memory aligned to `align` (a power of
  /// two). Returns nullptr only if the underlying malloc fails.
  void* allocate(size_t bytes, size_t align = alignof(std::max_align_t)) {
    if (cur_) {
      const uintptr_t base = reinterpret_cast<uintptr_t>(block_data(cur_));
      const uintptr_t cur = base + cur_off_;
      const uintptr_t aligned = (cur + (align - 1)) & ~(uintptr_t(align) - 1);
      const size_t pad = size_t(aligned - cur);
      if (pad + bytes <= cur_->cap - cur_off_) {
        cur_off_ += pad + bytes;
        used_total_ += pad + bytes;
        return reinterpret_cast<void*>(aligned);
      }
    }
    return allocate_slow(bytes, align);
  }

  /// Allocate uninitialized storage for `n` T's, aligned for T. The caller is
  /// responsible for constructing (placement-new) and, if T is non-trivial,
  /// destroying the objects — the arena will not.
  template <class T>
  T* alloc_uninitialized(size_t n = 1) {
    return static_cast<T*>(allocate(n * sizeof(T), alignof(T)));
  }

  /// Copy `n` bytes into the arena; returns a pointer to the copy.
  void* copy_bytes(const void* src, size_t n,
                   size_t align = alignof(std::max_align_t)) {
    void* p = allocate(n, align);
    if (p && n) {
      std::memcpy(p, src, n);
    }
    return p;
  }

  /// Copy a string into the arena, NUL-terminated. Returns a view over the
  /// arena copy (valid until reset()/release()/destruction). Handy for pooling
  /// names without a std::string per name.
  std::string_view copy_string(std::string_view s) {
    char* p = static_cast<char*>(allocate(s.size() + 1, 1));
    if (!p) {
      return {};
    }
    if (!s.empty()) {
      std::memcpy(p, s.data(), s.size());
    }
    p[s.size()] = '\0';
    return std::string_view(p, s.size());
  }

  /// Reset all offsets, retaining the allocated blocks for reuse by subsequent
  /// allocations (no free). Cheap way to recycle the arena between parses.
  void reset() {
    // Move every owned block onto the spare list, cleared, so allocate_slow can
    // reuse them before mallocing anything new.
    spare_ = nullptr;
    for (Block* b = blocks_; b; b = b->owner_next) {
      b->free_next = spare_;
      spare_ = b;
    }
    cur_ = nullptr;
    cur_off_ = 0;
    used_total_ = 0;
  }

  /// Free every block back to the OS.
  void release() {
    Block* b = blocks_;
    while (b) {
      Block* next = b->owner_next;
      std::free(b);
      b = next;
    }
    blocks_ = nullptr;
    spare_ = nullptr;
    cur_ = nullptr;
    cur_off_ = 0;
    used_total_ = 0;
    reserved_total_ = 0;
    next_block_size_ = kDefaultBlockSize;
  }

  /// Live bytes handed out since the last reset()/release() (includes alignment
  /// padding).
  size_t bytes_used() const { return used_total_; }
  /// Total bytes malloc'd for blocks (retained across reset()).
  size_t bytes_reserved() const { return reserved_total_; }

 private:
  // Block header; usable storage follows immediately after (aligned to
  // max_align_t because malloc guarantees it and sizeof(Block) is a multiple).
  struct Block {
    Block* owner_next;  // intrusive list of ALL blocks (for release()).
    Block* free_next;   // intrusive list of reusable blocks (after reset()).
    size_t cap;         // usable bytes in the trailing storage.
  };

  static char* block_data(Block* b) {
    return reinterpret_cast<char*>(b) + sizeof(Block);
  }

  void* allocate_slow(size_t bytes, size_t align) {
    // Try to reuse a spare block big enough for the worst-case aligned request.
    const size_t need = bytes + align;
    Block* prev = nullptr;
    for (Block* b = spare_; b; prev = b, b = b->free_next) {
      if (b->cap >= need) {
        if (prev) {
          prev->free_next = b->free_next;
        } else {
          spare_ = b->free_next;
        }
        activate(b);
        return allocate(bytes, align);
      }
    }
    // Otherwise malloc a fresh block. Grow geometrically so a long parse pays
    // few mallocs, but always cover this request.
    size_t cap = next_block_size_;
    if (cap < need) {
      cap = need;
    }
    Block* b = static_cast<Block*>(std::malloc(sizeof(Block) + cap));
    if (!b) {
      return nullptr;
    }
    b->cap = cap;
    b->owner_next = blocks_;
    blocks_ = b;
    reserved_total_ += cap;
    if (next_block_size_ < (size_t(1) << 22)) {  // cap growth at 4 MB.
      next_block_size_ *= 2;
    }
    activate(b);
    return allocate(bytes, align);
  }

  void activate(Block* b) {
    cur_ = b;
    cur_off_ = 0;
  }

  void steal(Arena& o) {
    blocks_ = o.blocks_;
    spare_ = o.spare_;
    cur_ = o.cur_;
    cur_off_ = o.cur_off_;
    used_total_ = o.used_total_;
    reserved_total_ = o.reserved_total_;
    next_block_size_ = o.next_block_size_;
    o.blocks_ = nullptr;
    o.spare_ = nullptr;
    o.cur_ = nullptr;
    o.cur_off_ = 0;
    o.used_total_ = 0;
    o.reserved_total_ = 0;
    o.next_block_size_ = kDefaultBlockSize;
  }

  Block* blocks_ = nullptr;  // all owned blocks (newest first).
  Block* spare_ = nullptr;   // reusable blocks after reset().
  Block* cur_ = nullptr;     // block currently being filled.
  size_t cur_off_ = 0;       // bytes used in cur_.
  size_t used_total_ = 0;
  size_t reserved_total_ = 0;
  size_t next_block_size_ = kDefaultBlockSize;
};

}  // namespace next
}  // namespace tinyusdz
