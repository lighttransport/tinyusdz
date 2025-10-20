// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// RAII Memory Budget Manager for TinyUSDZ
//
#pragma once

#include <cstdint>
#include <string>
#include <limits>
#include <sstream>
#include <iomanip>

#include "nonstd/expected.hpp"
#include "logger.hh"

namespace tinyusdz {

class MemoryBudgetManager {
 public:
  explicit MemoryBudgetManager(uint64_t max_budget = std::numeric_limits<uint32_t>::max())
      : max_budget_(max_budget), current_usage_(0), enable_logging_(false) {}

  bool CheckAndReserve(uint64_t requested_bytes, const std::string& tag = "") {
    if (current_usage_ + requested_bytes > max_budget_) {
      if (enable_logging_) {
        LogMemoryOperation("REJECT", requested_bytes, tag, false);
      }
      return false;
    }
    current_usage_ += requested_bytes;
    if (enable_logging_) {
      LogMemoryOperation("RESERVE", requested_bytes, tag, true);
    }
    return true;
  }

  void Release(uint64_t bytes_to_release, const std::string& tag = "") {
    uint64_t actual_release = bytes_to_release;
    if (current_usage_ >= bytes_to_release) {
      current_usage_ -= bytes_to_release;
    } else {
      actual_release = current_usage_;
      current_usage_ = 0;
    }
    if (enable_logging_) {
      LogMemoryOperation("RELEASE", actual_release, tag, true);
    }
  }

  uint64_t GetCurrentUsage() const { return current_usage_; }
  uint64_t GetMaxBudget() const { return max_budget_; }
  uint64_t GetRemainingBudget() const { return max_budget_ - current_usage_; }
  
  size_t GetUsageInMB() const { return size_t(current_usage_ / (1024 * 1024)); }

  void Reset() { 
    uint64_t prev_usage = current_usage_;
    current_usage_ = 0; 
    if (enable_logging_ && prev_usage > 0) {
      LogMemoryOperation("RESET", prev_usage, "", true);
    }
  }
  
  void SetLoggingEnabled(bool enabled) { enable_logging_ = enabled; }
  bool IsLoggingEnabled() const { return enable_logging_; }

  class ScopedReservation {
   public:
    ScopedReservation(MemoryBudgetManager& manager, uint64_t bytes, const std::string& tag = "")
        : manager_(manager), bytes_(bytes), tag_(tag), reserved_(false) {
      reserved_ = manager_.CheckAndReserve(bytes, tag);
    }

    ~ScopedReservation() {
      if (reserved_) {
        manager_.Release(bytes_, tag_);
      }
    }

    bool IsReserved() const { return reserved_; }

    ScopedReservation(const ScopedReservation&) = delete;
    ScopedReservation& operator=(const ScopedReservation&) = delete;

    ScopedReservation(ScopedReservation&& other) noexcept
        : manager_(other.manager_), bytes_(other.bytes_), tag_(other.tag_), reserved_(other.reserved_) {
      other.reserved_ = false;
    }

    ScopedReservation& operator=(ScopedReservation&& other) noexcept {
      if (this != &other) {
        if (reserved_) {
          manager_.Release(bytes_);
        }
        bytes_ = other.bytes_;
        reserved_ = other.reserved_;
        other.reserved_ = false;
      }
      return *this;
    }

   private:
    MemoryBudgetManager& manager_;
    uint64_t bytes_;
    std::string tag_;
    bool reserved_;
  };

  ScopedReservation ReserveScoped(uint64_t bytes, const std::string& tag = "") {
    return ScopedReservation(*this, bytes, tag);
  }

 private:
  void LogMemoryOperation(const std::string& op_type, uint64_t bytes, 
                          const std::string& tag, bool success) const {
    if (!logging::Logger::getInstance().shouldLog(logging::LogLevel::Info)) {
      return;
    }
    
    std::stringstream ss;
    ss << "[MEM_" << op_type << "]";
    if (!tag.empty()) {
      ss << " [" << tag << "]";
    }
    ss << " Requested: " << FormatBytes(bytes)
       << " | Current: " << FormatBytes(current_usage_)
       << " | Limit: " << FormatBytes(max_budget_)
       << " | Available: " << FormatBytes(GetRemainingBudget());
    if (!success && op_type == "REJECT") {
      ss << " | REJECTED (would exceed limit by " 
         << FormatBytes((current_usage_ + bytes) - max_budget_) << ")";
    }
    
    TUSDZ_LOG_I(ss.str());
  }
  
  static std::string FormatBytes(uint64_t bytes) {
    std::stringstream ss;
    if (bytes >= 1024ull * 1024ull * 1024ull) {
      ss << std::fixed << std::setprecision(2) 
         << (double(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024ull * 1024ull) {
      ss << std::fixed << std::setprecision(2) 
         << (double(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024ull) {
      ss << std::fixed << std::setprecision(2) 
         << (double(bytes) / 1024.0) << " KB";
    } else {
      ss << bytes << " B";
    }
    return ss.str();
  }

  uint64_t max_budget_;
  uint64_t current_usage_;
  bool enable_logging_;
};

template <typename ReturnType>
class MemoryBudgetGuard {
 public:
  MemoryBudgetGuard(MemoryBudgetManager& manager, uint64_t bytes, 
                    const std::string& error_tag = "")
      : reservation_(manager.ReserveScoped(bytes, error_tag)), error_tag_(error_tag) {}

  template <typename T>
  nonstd::expected<T, std::string> CheckAndReturn(T&& value) {
    if (!reservation_.IsReserved()) {
      return nonstd::make_unexpected(error_tag_.empty() 
        ? "Reached maximum memory budget" 
        : error_tag_ + ": Reached maximum memory budget");
    }
    return std::forward<T>(value);
  }

  bool IsValid() const { return reservation_.IsReserved(); }

 private:
  MemoryBudgetManager::ScopedReservation reservation_;
  std::string error_tag_;
};

#define MEMORY_BUDGET_CHECK(manager, bytes, error_tag) \
  do { \
    auto memory_guard = MemoryBudgetGuard<bool>(manager, bytes, error_tag); \
    if (!memory_guard.IsValid()) { \
      PUSH_ERROR_AND_RETURN_TAG(error_tag, "Reached maximum memory budget"); \
    } \
  } while(0)

// Macro with logging support
#define MEMORY_BUDGET_CHECK_WITH_LOG(manager, bytes, varname) \
  do { \
    if (!(manager).CheckAndReserve(bytes, varname)) { \
      PUSH_ERROR_AND_RETURN_TAG(varname, "Reached maximum memory budget"); \
    } \
  } while(0)

#define MEMORY_BUDGET_CHECK_AND_RETURN(manager, bytes, error_tag, return_type) \
  MemoryBudgetGuard<return_type>(manager, bytes, error_tag)

}  // namespace tinyusdz
