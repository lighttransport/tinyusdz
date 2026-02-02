// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory Context
// Unified memory management combining MemoryPool and MemoryBudget

#pragma once

#include "memory-pool.hh"
#include "../memory-budget.hh"
#include <memory>

namespace tinyusdz {
namespace next {

/// Memory context for unified memory management
/// Combines MemoryPool for allocation and MemoryBudget for tracking/limiting
/// Thread-safety: NOT thread-safe. Use one context per thread/parser.
class MemoryContext {
public:
  /// Default constructor - creates owned pool and unlimited budget
  MemoryContext()
      : owned_pool_(std::make_unique<MemoryPool>()),
        pool_(owned_pool_.get()),
        owned_budget_(std::make_unique<MemoryBudget>()),
        budget_(owned_budget_.get()) {}

  /// Constructor with budget limit in megabytes
  explicit MemoryContext(uint32_t max_mb)
      : owned_pool_(std::make_unique<MemoryPool>()),
        pool_(owned_pool_.get()),
        owned_budget_(std::make_unique<MemoryBudget>(
            static_cast<uint64_t>(max_mb) * 1024 * 1024)),
        budget_(owned_budget_.get()) {}

  /// Constructor with custom tile size and budget
  MemoryContext(size_t tile_size, uint32_t max_mb)
      : owned_pool_(std::make_unique<MemoryPool>(tile_size)),
        pool_(owned_pool_.get()),
        owned_budget_(std::make_unique<MemoryBudget>(
            static_cast<uint64_t>(max_mb) * 1024 * 1024)),
        budget_(owned_budget_.get()) {}

  /// Constructor with external pool and budget (non-owning)
  MemoryContext(MemoryPool* pool, MemoryBudget* budget)
      : pool_(pool), budget_(budget) {}

  /// No copy
  MemoryContext(const MemoryContext&) = delete;
  MemoryContext& operator=(const MemoryContext&) = delete;

  /// Move
  MemoryContext(MemoryContext&& other) noexcept
      : owned_pool_(std::move(other.owned_pool_)),
        pool_(other.pool_),
        owned_budget_(std::move(other.owned_budget_)),
        budget_(other.budget_) {
    other.pool_ = nullptr;
    other.budget_ = nullptr;
  }

  MemoryContext& operator=(MemoryContext&& other) noexcept {
    if (this != &other) {
      owned_pool_ = std::move(other.owned_pool_);
      pool_ = other.pool_;
      owned_budget_ = std::move(other.owned_budget_);
      budget_ = other.budget_;
      other.pool_ = nullptr;
      other.budget_ = nullptr;
    }
    return *this;
  }

  // ============================================================
  // Accessors
  // ============================================================

  /// Get the memory pool
  MemoryPool* pool() { return pool_; }
  const MemoryPool* pool() const { return pool_; }

  /// Get the memory budget
  MemoryBudget* budget() { return budget_; }
  const MemoryBudget* budget() const { return budget_; }

  /// Check if context is valid
  bool IsValid() const { return pool_ != nullptr && budget_ != nullptr; }

  // ============================================================
  // Budget-tracked allocation
  // ============================================================

  /// Try to allocate with budget tracking
  /// Returns nullptr if budget exceeded
  void* Allocate(size_t size) {
    if (!budget_->TryReserve(size)) {
      return nullptr;
    }
    void* ptr = pool_->Allocate(size);
    if (!ptr) {
      budget_->Release(size);
    }
    return ptr;
  }

  /// Try to allocate aligned with budget tracking
  void* AllocateAligned(size_t size, size_t alignment) {
    if (!budget_->TryReserve(size)) {
      return nullptr;
    }
    void* ptr = pool_->AllocateAligned(size, alignment);
    if (!ptr) {
      budget_->Release(size);
    }
    return ptr;
  }

  /// Allocate array with budget tracking
  template <typename T>
  T* AllocateArray(size_t count) {
    size_t size = count * sizeof(T);
    if (!budget_->TryReserve(size)) {
      return nullptr;
    }
    T* ptr = pool_->AllocateArray<T>(count);
    if (!ptr) {
      budget_->Release(size);
    }
    return ptr;
  }

  /// Allocate string with budget tracking
  char* AllocateString(const char* str) {
    if (!str) return nullptr;
    size_t len = std::strlen(str);
    if (!budget_->TryReserve(len + 1)) {
      return nullptr;
    }
    char* ptr = pool_->AllocateString(str);
    if (!ptr) {
      budget_->Release(len + 1);
    }
    return ptr;
  }

  /// Check if allocation would exceed budget
  bool CanAllocate(size_t size) const {
    return budget_->GetRemainingBudget() >= size;
  }

  // ============================================================
  // Pool management
  // ============================================================

  /// Reset pool (reuses memory, releases budget tracking)
  void Reset() {
    if (pool_) {
      pool_->Reset();
    }
    if (budget_) {
      // Keep mmap tracking, reset heap usage
      uint64_t mmap = budget_->GetMmapUsage();
      budget_->Reset();
      if (mmap > 0) {
        budget_->TrackMmapMemory(mmap);
      }
    }
  }

  /// Clear pool (frees all memory)
  void Clear() {
    if (pool_) {
      pool_->Clear();
    }
    if (budget_) {
      budget_->Reset();
    }
  }

  /// Reserve capacity for expected allocations
  void Reserve(size_t expected_bytes) {
    if (pool_ && budget_->TryReserve(expected_bytes)) {
      pool_->Reserve(expected_bytes);
    }
  }

  // ============================================================
  // Statistics
  // ============================================================

  struct Stats {
    // Pool stats
    size_t tile_count = 0;
    size_t tile_size = 0;
    size_t pool_allocated = 0;
    size_t pool_used = 0;
    size_t pool_peak = 0;
    size_t pool_allocation_count = 0;
    size_t large_alloc_count = 0;

    // Budget stats
    uint64_t budget_max = 0;
    uint64_t budget_used = 0;
    uint64_t budget_remaining = 0;
    uint64_t mmap_bytes = 0;

    // Combined
    uint64_t total_memory() const { return budget_used + mmap_bytes; }
  };

  Stats GetStats() const {
    Stats s;
    if (pool_) {
      auto ps = pool_->GetStats();
      s.tile_count = ps.tile_count;
      s.tile_size = ps.tile_size;
      s.pool_allocated = ps.total_allocated;
      s.pool_used = ps.total_used;
      s.pool_peak = ps.peak_used;
      s.pool_allocation_count = ps.allocation_count;
      s.large_alloc_count = ps.large_alloc_count;
    }
    if (budget_) {
      s.budget_max = budget_->GetMaxBudget();
      s.budget_used = budget_->GetCurrentUsage();
      s.budget_remaining = budget_->GetRemainingBudget();
      s.mmap_bytes = budget_->GetMmapUsage();
    }
    return s;
  }

private:
  // NOTE: Member order matches initializer list order to avoid -Wreorder warnings
  // owned_pool_ -> pool_ -> owned_budget_ -> budget_
  std::unique_ptr<MemoryPool> owned_pool_;
  MemoryPool* pool_ = nullptr;
  std::unique_ptr<MemoryBudget> owned_budget_;
  MemoryBudget* budget_ = nullptr;
};

/// Shared memory context reference
/// Allows multiple objects to share a memory context
class MemoryContextRef {
public:
  MemoryContextRef() = default;
  explicit MemoryContextRef(std::shared_ptr<MemoryContext> ctx)
      : ctx_(std::move(ctx)) {}

  /// Create a new context with default settings
  static MemoryContextRef Create() {
    return MemoryContextRef(std::make_shared<MemoryContext>());
  }

  /// Create a new context with budget limit in MB
  static MemoryContextRef Create(uint32_t max_mb) {
    return MemoryContextRef(std::make_shared<MemoryContext>(max_mb));
  }

  /// Create a new context with custom tile size and budget
  static MemoryContextRef Create(size_t tile_size, uint32_t max_mb) {
    return MemoryContextRef(std::make_shared<MemoryContext>(tile_size, max_mb));
  }

  /// Check if valid
  bool IsValid() const { return ctx_ != nullptr && ctx_->IsValid(); }

  /// Get the context
  MemoryContext* get() const { return ctx_.get(); }
  MemoryContext* operator->() const { return ctx_.get(); }
  MemoryContext& operator*() const { return *ctx_; }

  /// Get reference count
  long use_count() const { return ctx_.use_count(); }

private:
  std::shared_ptr<MemoryContext> ctx_;
};

}  // namespace next
}  // namespace tinyusdz
