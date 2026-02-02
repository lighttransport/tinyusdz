// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory Pool
// Arena-style allocator with 64KB tiles (WASM page aligned)

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <atomic>

namespace tinyusdz {
namespace next {

/// Default tile size: 64KB (aligned with WASM page size)
constexpr size_t kDefaultTileSize = 64 * 1024;

/// Memory pool with arena-style allocation
/// Fast bump allocation within tiles, no individual deallocation
/// Thread-safety: NOT thread-safe. Use one pool per thread/parser.
class MemoryPool {
public:
  /// Statistics for memory usage tracking
  struct Stats {
    size_t tile_count = 0;        // Number of allocated tiles
    size_t tile_size = 0;         // Size of each tile
    size_t total_allocated = 0;   // Total bytes allocated from pool
    size_t total_used = 0;        // Bytes actually used by allocations
    size_t peak_used = 0;         // Peak usage
    size_t allocation_count = 0;  // Number of allocations made
    size_t large_alloc_count = 0; // Allocations larger than tile size
  };

  /// Construct with specified tile size
  explicit MemoryPool(size_t tile_size = kDefaultTileSize);

  /// Destructor - frees all tiles
  ~MemoryPool();

  /// Non-copyable
  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

  /// Moveable
  MemoryPool(MemoryPool&& other) noexcept;
  MemoryPool& operator=(MemoryPool&& other) noexcept;

  // ============================================================
  // Allocation
  // ============================================================

  /// Allocate bytes with default alignment (8 bytes)
  void* Allocate(size_t size);

  /// Allocate bytes with specified alignment
  void* AllocateAligned(size_t size, size_t alignment);

  /// Allocate and zero-initialize
  void* AllocateZeroed(size_t size);

  /// Allocate array of T
  template <typename T>
  T* AllocateArray(size_t count) {
    void* ptr = AllocateAligned(count * sizeof(T), alignof(T));
    return static_cast<T*>(ptr);
  }

  /// Allocate array of T and zero-initialize
  template <typename T>
  T* AllocateArrayZeroed(size_t count) {
    T* ptr = AllocateArray<T>(count);
    if (ptr) {
      std::memset(ptr, 0, count * sizeof(T));
    }
    return ptr;
  }

  /// Allocate and copy data
  void* AllocateCopy(const void* src, size_t size);

  /// Allocate string (null-terminated copy)
  char* AllocateString(const char* str);
  char* AllocateString(const char* str, size_t len);

  // ============================================================
  // Pool management
  // ============================================================

  /// Reset pool - invalidates all allocations, reuses tiles
  void Reset();

  /// Clear pool - frees all tiles
  void Clear();

  /// Reserve capacity for expected allocations
  void Reserve(size_t expected_bytes);

  /// Get statistics
  Stats GetStats() const;

  /// Get tile size
  size_t tile_size() const { return tile_size_; }

  /// Get total allocated memory
  size_t total_allocated() const { return stats_.total_allocated; }

  /// Get total used memory
  size_t total_used() const { return stats_.total_used; }

private:
  struct Tile {
    uint8_t* data;
    size_t size;
    size_t used;
  };

  size_t tile_size_;
  std::vector<Tile> tiles_;
  std::vector<void*> large_allocs_;  // Allocations larger than tile
  size_t current_tile_ = 0;
  Stats stats_;

  void* AllocateFromTile(size_t size, size_t alignment);
  void* AllocateLarge(size_t size);
  void AddTile();
};

/// Shared memory pool reference
/// Allows multiple objects to share a pool with reference counting
class MemoryPoolRef {
public:
  MemoryPoolRef() = default;
  explicit MemoryPoolRef(std::shared_ptr<MemoryPool> pool) : pool_(std::move(pool)) {}

  /// Create a new pool with default tile size
  static MemoryPoolRef Create(size_t tile_size = kDefaultTileSize) {
    return MemoryPoolRef(std::make_shared<MemoryPool>(tile_size));
  }

  /// Check if valid
  bool IsValid() const { return pool_ != nullptr; }

  /// Get the pool
  MemoryPool* get() const { return pool_.get(); }
  MemoryPool* operator->() const { return pool_.get(); }
  MemoryPool& operator*() const { return *pool_; }

  /// Get reference count
  long use_count() const { return pool_.use_count(); }

private:
  std::shared_ptr<MemoryPool> pool_;
};

// ============================================================
// TypedArray - Pool-allocated array
// ============================================================

/// Storage type for TypedArray
enum class ArrayStorage : uint8_t {
  Empty = 0,       // No data
  PoolOwned = 1,   // Allocated from memory pool
  HeapOwned = 2,   // Allocated from heap (std::vector-style)
  View = 3,        // Non-owning view to external data
};

/// Pool-allocated typed array
/// Similar interface to std::vector but uses memory pool for allocation
/// Supports three modes: pool-owned, heap-owned, and view (non-owning)
template <typename T>
class TypedArray {
public:
  using value_type = T;
  using size_type = size_t;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = T*;
  using const_iterator = const T*;

  /// Default constructor - empty array
  TypedArray() = default;

  /// Destructor
  ~TypedArray() { Clear(); }

  /// Construct with size (pool-allocated if pool provided, else heap)
  TypedArray(size_t count, MemoryPool* pool = nullptr) {
    Resize(count, pool);
  }

  /// Construct from data (copies into pool or heap)
  TypedArray(const T* data, size_t count, MemoryPool* pool = nullptr) {
    Assign(data, count, pool);
  }

  /// Construct view (non-owning)
  static TypedArray MakeView(const T* data, size_t count) {
    TypedArray arr;
    arr.data_ = const_cast<T*>(data);
    arr.size_ = count;
    arr.capacity_ = count;
    arr.storage_ = ArrayStorage::View;
    return arr;
  }

  /// Move constructor
  TypedArray(TypedArray&& other) noexcept
      : data_(other.data_), size_(other.size_), capacity_(other.capacity_),
        storage_(other.storage_), pool_(other.pool_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.storage_ = ArrayStorage::Empty;
    other.pool_ = nullptr;
  }

  /// Move assignment
  TypedArray& operator=(TypedArray&& other) noexcept {
    if (this != &other) {
      Clear();
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      storage_ = other.storage_;
      pool_ = other.pool_;
      other.data_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
      other.storage_ = ArrayStorage::Empty;
      other.pool_ = nullptr;
    }
    return *this;
  }

  /// Copy constructor (always copies to heap)
  TypedArray(const TypedArray& other) {
    if (!other.empty()) {
      Assign(other.data_, other.size_, nullptr);
    }
  }

  /// Copy assignment (always copies to heap)
  TypedArray& operator=(const TypedArray& other) {
    if (this != &other) {
      Clear();
      if (!other.empty()) {
        Assign(other.data_, other.size_, nullptr);
      }
    }
    return *this;
  }

  // ============================================================
  // Element access
  // ============================================================

  T& operator[](size_t idx) { return data_[idx]; }
  const T& operator[](size_t idx) const { return data_[idx]; }

  T& front() { return data_[0]; }
  const T& front() const { return data_[0]; }

  T& back() { return data_[size_ - 1]; }
  const T& back() const { return data_[size_ - 1]; }

  T* data() { return data_; }
  const T* data() const { return data_; }

  // ============================================================
  // Capacity
  // ============================================================

  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  size_t size_bytes() const { return size_ * sizeof(T); }

  // ============================================================
  // Iterators
  // ============================================================

  iterator begin() { return data_; }
  iterator end() { return data_ + size_; }
  const_iterator begin() const { return data_; }
  const_iterator end() const { return data_ + size_; }
  const_iterator cbegin() const { return data_; }
  const_iterator cend() const { return data_ + size_; }

  // ============================================================
  // Modifiers
  // ============================================================

  /// Clear contents
  void Clear() {
    if (storage_ == ArrayStorage::HeapOwned && data_) {
      delete[] data_;
    }
    // Pool-owned memory is freed when pool is reset/destroyed
    // View doesn't own memory
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
    storage_ = ArrayStorage::Empty;
    pool_ = nullptr;
  }

  /// Resize array
  void Resize(size_t count, MemoryPool* pool = nullptr) {
    if (count == 0) {
      Clear();
      return;
    }

    if (count <= capacity_ && storage_ != ArrayStorage::View) {
      size_ = count;
      return;
    }

    T* new_data = nullptr;
    ArrayStorage new_storage;

    if (pool) {
      new_data = pool->AllocateArray<T>(count);
      new_storage = ArrayStorage::PoolOwned;
    } else {
      new_data = new T[count];
      new_storage = ArrayStorage::HeapOwned;
    }

    // Copy existing data
    if (data_ && size_ > 0) {
      size_t copy_count = (count < size_) ? count : size_;
      std::memcpy(new_data, data_, copy_count * sizeof(T));
    }

    // Free old data if heap-owned
    if (storage_ == ArrayStorage::HeapOwned && data_) {
      delete[] data_;
    }

    data_ = new_data;
    size_ = count;
    capacity_ = count;
    storage_ = new_storage;
    pool_ = pool;
  }

  /// Assign from pointer
  void Assign(const T* src, size_t count, MemoryPool* pool = nullptr) {
    Resize(count, pool);
    if (src && count > 0) {
      std::memcpy(data_, src, count * sizeof(T));
    }
  }

  /// Assign from vector
  void Assign(const std::vector<T>& vec, MemoryPool* pool = nullptr) {
    Assign(vec.data(), vec.size(), pool);
  }

  // ============================================================
  // Storage info
  // ============================================================

  /// Get storage type
  ArrayStorage storage() const { return storage_; }

  /// Check if this is a view
  bool is_view() const { return storage_ == ArrayStorage::View; }

  /// Check if pool-owned
  bool is_pool_owned() const { return storage_ == ArrayStorage::PoolOwned; }

  /// Check if heap-owned
  bool is_heap_owned() const { return storage_ == ArrayStorage::HeapOwned; }

  /// Convert view to owned (makes a copy)
  bool MakeOwned(MemoryPool* pool = nullptr) {
    if (storage_ != ArrayStorage::View || !data_ || size_ == 0) {
      return false;
    }

    const T* old_data = data_;
    size_t old_size = size_;

    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
    storage_ = ArrayStorage::Empty;

    Assign(old_data, old_size, pool);
    return true;
  }

private:
  T* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
  ArrayStorage storage_ = ArrayStorage::Empty;
  MemoryPool* pool_ = nullptr;  // Non-owning reference
};

// Common instantiations
using FloatArray = TypedArray<float>;
using DoubleArray = TypedArray<double>;
using Int32Array = TypedArray<int32_t>;
using UInt32Array = TypedArray<uint32_t>;
using Int64Array = TypedArray<int64_t>;
using UInt64Array = TypedArray<uint64_t>;
using UInt8Array = TypedArray<uint8_t>;

}  // namespace next
}  // namespace tinyusdz
