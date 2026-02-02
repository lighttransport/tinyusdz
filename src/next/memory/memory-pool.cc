// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory Pool Implementation

#include "memory-pool.hh"
#include <cstdlib>
#include <algorithm>

namespace tinyusdz {
namespace next {

MemoryPool::MemoryPool(size_t tile_size)
    : tile_size_(tile_size > 0 ? tile_size : kDefaultTileSize) {
  stats_.tile_size = tile_size_;
}

MemoryPool::~MemoryPool() {
  Clear();
}

MemoryPool::MemoryPool(MemoryPool&& other) noexcept
    : tile_size_(other.tile_size_),
      tiles_(std::move(other.tiles_)),
      large_allocs_(std::move(other.large_allocs_)),
      current_tile_(other.current_tile_),
      stats_(other.stats_) {
  other.current_tile_ = 0;
  other.stats_ = Stats{};
  other.stats_.tile_size = other.tile_size_;
}

MemoryPool& MemoryPool::operator=(MemoryPool&& other) noexcept {
  if (this != &other) {
    Clear();
    tile_size_ = other.tile_size_;
    tiles_ = std::move(other.tiles_);
    large_allocs_ = std::move(other.large_allocs_);
    current_tile_ = other.current_tile_;
    stats_ = other.stats_;
    other.current_tile_ = 0;
    other.stats_ = Stats{};
    other.stats_.tile_size = other.tile_size_;
  }
  return *this;
}

void* MemoryPool::Allocate(size_t size) {
  return AllocateAligned(size, 8);
}

void* MemoryPool::AllocateAligned(size_t size, size_t alignment) {
  if (size == 0) return nullptr;

  // Large allocations go directly to heap
  if (size > tile_size_ / 2) {
    return AllocateLarge(size);
  }

  return AllocateFromTile(size, alignment);
}

void* MemoryPool::AllocateZeroed(size_t size) {
  void* ptr = Allocate(size);
  if (ptr) {
    std::memset(ptr, 0, size);
  }
  return ptr;
}

void* MemoryPool::AllocateCopy(const void* src, size_t size) {
  void* ptr = Allocate(size);
  if (ptr && src) {
    std::memcpy(ptr, src, size);
  }
  return ptr;
}

char* MemoryPool::AllocateString(const char* str) {
  if (!str) return nullptr;
  return AllocateString(str, std::strlen(str));
}

char* MemoryPool::AllocateString(const char* str, size_t len) {
  char* ptr = static_cast<char*>(Allocate(len + 1));
  if (ptr) {
    if (str) {
      std::memcpy(ptr, str, len);
    }
    ptr[len] = '\0';
  }
  return ptr;
}

void* MemoryPool::AllocateFromTile(size_t size, size_t alignment) {
  // Ensure we have at least one tile
  if (tiles_.empty()) {
    AddTile();
  }

  // Try to allocate from current tile
  while (current_tile_ < tiles_.size()) {
    Tile& tile = tiles_[current_tile_];

    // Calculate aligned offset
    size_t aligned_offset = (tile.used + alignment - 1) & ~(alignment - 1);

    if (aligned_offset + size <= tile.size) {
      void* ptr = tile.data + aligned_offset;
      tile.used = aligned_offset + size;
      stats_.total_used += size;
      stats_.allocation_count++;
      if (stats_.total_used > stats_.peak_used) {
        stats_.peak_used = stats_.total_used;
      }
      return ptr;
    }

    // Move to next tile
    current_tile_++;
  }

  // Need a new tile
  AddTile();
  return AllocateFromTile(size, alignment);
}

void* MemoryPool::AllocateLarge(size_t size) {
  // Allocate aligned memory
  void* ptr = nullptr;
#ifdef _WIN32
  ptr = _aligned_malloc(size, 16);
#else
  if (posix_memalign(&ptr, 16, size) != 0) {
    ptr = nullptr;
  }
#endif

  if (ptr) {
    large_allocs_.push_back(ptr);
    stats_.total_allocated += size;
    stats_.total_used += size;
    stats_.allocation_count++;
    stats_.large_alloc_count++;
    if (stats_.total_used > stats_.peak_used) {
      stats_.peak_used = stats_.total_used;
    }
  }
  return ptr;
}

void MemoryPool::AddTile() {
  Tile tile;
  tile.size = tile_size_;
  tile.used = 0;

#ifdef _WIN32
  tile.data = static_cast<uint8_t*>(_aligned_malloc(tile_size_, 16));
#else
  void* ptr = nullptr;
  if (posix_memalign(&ptr, 16, tile_size_) == 0) {
    tile.data = static_cast<uint8_t*>(ptr);
  } else {
    tile.data = nullptr;
  }
#endif

  if (tile.data) {
    tiles_.push_back(tile);
    stats_.tile_count++;
    stats_.total_allocated += tile_size_;
  }
}

void MemoryPool::Reset() {
  // Reset all tiles to unused
  for (auto& tile : tiles_) {
    tile.used = 0;
  }
  current_tile_ = 0;

  // Free large allocations
  for (void* ptr : large_allocs_) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }
  large_allocs_.clear();

  // Update stats
  stats_.total_used = 0;
  stats_.allocation_count = 0;
  stats_.large_alloc_count = 0;
}

void MemoryPool::Clear() {
  // Free all tiles
  for (auto& tile : tiles_) {
#ifdef _WIN32
    _aligned_free(tile.data);
#else
    free(tile.data);
#endif
  }
  tiles_.clear();

  // Free large allocations
  for (void* ptr : large_allocs_) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }
  large_allocs_.clear();

  current_tile_ = 0;
  stats_ = Stats{};
  stats_.tile_size = tile_size_;
}

void MemoryPool::Reserve(size_t expected_bytes) {
  size_t tiles_needed = (expected_bytes + tile_size_ - 1) / tile_size_;
  while (tiles_.size() < tiles_needed) {
    AddTile();
  }
}

MemoryPool::Stats MemoryPool::GetStats() const {
  return stats_;
}

}  // namespace next
}  // namespace tinyusdz
