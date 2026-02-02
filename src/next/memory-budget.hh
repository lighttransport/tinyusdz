// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory Budget Manager
// RAII memory tracking for security-conscious parsing

#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

namespace tinyusdz {
namespace next {

/// Memory budget manager for tracking and limiting memory allocations
/// Thread-safety: NOT thread-safe. Each parser should have its own instance.
class MemoryBudget {
public:
  /// Default constructor with unlimited budget
  MemoryBudget()
      : max_budget_(std::numeric_limits<uint64_t>::max()), current_usage_(0) {}

  /// Constructor with specified budget in bytes
  explicit MemoryBudget(uint64_t max_bytes)
      : max_budget_(max_bytes), current_usage_(0) {}

  /// Constructor with budget in megabytes (convenience)
  static MemoryBudget FromMB(uint32_t mb) {
    return MemoryBudget(static_cast<uint64_t>(mb) * 1024 * 1024);
  }

  /// Constructor with budget in gigabytes (convenience)
  static MemoryBudget FromGB(uint32_t gb) {
    return MemoryBudget(static_cast<uint64_t>(gb) * 1024 * 1024 * 1024);
  }

  // ============================================================
  // Core allocation tracking
  // ============================================================

  /// Try to reserve memory. Returns true if allowed, false if would exceed budget.
  bool TryReserve(uint64_t bytes) {
    if (current_usage_ + bytes > max_budget_) {
      return false;
    }
    current_usage_ += bytes;
    return true;
  }

  /// Reserve memory, returning false if it would exceed budget (alias for TryReserve)
  bool CheckAndReserve(uint64_t bytes) { return TryReserve(bytes); }

  /// Release previously reserved memory
  void Release(uint64_t bytes) {
    if (current_usage_ >= bytes) {
      current_usage_ -= bytes;
    } else {
      current_usage_ = 0;
    }
  }

  /// Reset usage to zero
  void Reset() { current_usage_ = 0; }

  // ============================================================
  // Query
  // ============================================================

  /// Get current memory usage in bytes
  uint64_t GetCurrentUsage() const { return current_usage_; }

  /// Get current memory usage in megabytes
  size_t GetUsageInMB() const { return static_cast<size_t>(current_usage_ / (1024 * 1024)); }

  /// Get maximum budget in bytes
  uint64_t GetMaxBudget() const { return max_budget_; }

  /// Get remaining budget in bytes
  uint64_t GetRemainingBudget() const {
    return max_budget_ > current_usage_ ? max_budget_ - current_usage_ : 0;
  }

  /// Check if budget is unlimited
  bool IsUnlimited() const { return max_budget_ == std::numeric_limits<uint64_t>::max(); }

  // ============================================================
  // Configuration
  // ============================================================

  /// Set maximum budget in bytes
  void SetMaxBudget(uint64_t bytes) { max_budget_ = bytes; }

  /// Set maximum budget in megabytes
  void SetMaxBudgetMB(uint32_t mb) { max_budget_ = static_cast<uint64_t>(mb) * 1024 * 1024; }

  // ============================================================
  // RAII Scoped Reservation
  // ============================================================

  /// RAII helper that automatically releases memory on destruction
  class ScopedReservation {
  public:
    ScopedReservation(MemoryBudget& budget, uint64_t bytes)
        : budget_(budget), bytes_(bytes), reserved_(budget.TryReserve(bytes)) {}

    ~ScopedReservation() {
      if (reserved_) {
        budget_.Release(bytes_);
      }
    }

    /// Check if reservation succeeded
    bool IsReserved() const { return reserved_; }

    /// Explicitly release (prevents double-release in destructor)
    void Release() {
      if (reserved_) {
        budget_.Release(bytes_);
        reserved_ = false;
      }
    }

    /// Transfer ownership (mark as no longer reserved, but don't actually release)
    void Commit() { reserved_ = false; }

    // Non-copyable
    ScopedReservation(const ScopedReservation&) = delete;
    ScopedReservation& operator=(const ScopedReservation&) = delete;

    // Moveable
    ScopedReservation(ScopedReservation&& other) noexcept
        : budget_(other.budget_), bytes_(other.bytes_), reserved_(other.reserved_) {
      other.reserved_ = false;
    }

    ScopedReservation& operator=(ScopedReservation&& other) noexcept {
      if (this != &other) {
        if (reserved_) {
          budget_.Release(bytes_);
        }
        bytes_ = other.bytes_;
        reserved_ = other.reserved_;
        other.reserved_ = false;
      }
      return *this;
    }

  private:
    MemoryBudget& budget_;
    uint64_t bytes_;
    bool reserved_;
  };

  /// Create a scoped reservation
  ScopedReservation ReserveScoped(uint64_t bytes) {
    return ScopedReservation(*this, bytes);
  }

  // ============================================================
  // Convenience methods for common data types
  // ============================================================

  /// Reserve memory for a string
  bool ReserveString(size_t length) {
    // Account for string overhead (SSO threshold + null terminator)
    return TryReserve(length > 16 ? length + 1 : 0);
  }

  /// Reserve memory for a vector of elements
  template <typename T>
  bool ReserveVector(size_t count) {
    return TryReserve(count * sizeof(T));
  }

  /// Reserve memory for vector growth (from old_capacity to new_capacity)
  template <typename T>
  bool ReserveVectorGrowth(size_t old_capacity, size_t new_capacity) {
    if (new_capacity > old_capacity) {
      return TryReserve((new_capacity - old_capacity) * sizeof(T));
    }
    return true;
  }

  // ============================================================
  // Mmap / zero-copy support
  // ============================================================

  /// Track mmap'd memory (does not count against budget, just for reporting)
  /// Mmap'd memory is managed by the OS and doesn't use heap
  void TrackMmapMemory(uint64_t bytes) { mmap_bytes_ += bytes; }

  /// Release mmap'd memory tracking
  void ReleaseMmapMemory(uint64_t bytes) {
    if (mmap_bytes_ >= bytes) {
      mmap_bytes_ -= bytes;
    } else {
      mmap_bytes_ = 0;
    }
  }

  /// Get tracked mmap'd memory in bytes
  uint64_t GetMmapUsage() const { return mmap_bytes_; }

  /// Get total memory (heap + mmap) for reporting
  uint64_t GetTotalMemory() const { return current_usage_ + mmap_bytes_; }

  /// Check if a zero-copy view should be used instead of copying
  /// Returns true if the data size is large enough to benefit from zero-copy
  /// and the budget has room
  bool ShouldUseZeroCopy(uint64_t data_size, uint64_t threshold = 4096) const {
    // Use zero-copy for data larger than threshold (default 4KB)
    // and when heap budget would be exceeded
    if (data_size < threshold) return false;
    return current_usage_ + data_size > max_budget_ || data_size > threshold * 4;
  }

private:
  uint64_t mmap_bytes_ = 0;  // Tracked mmap'd memory (informational only)
  uint64_t max_budget_;
  uint64_t current_usage_;
};

}  // namespace next
}  // namespace tinyusdz
