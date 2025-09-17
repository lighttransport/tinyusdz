// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Container value types (Dictionary, TimeSamples) for TinyUSDZ
// Part of the value-types.hh modularization effort

#pragma once

#include <map>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

#include "value-core-types.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {

// Forward declarations
class Path;
class Reference;
class Payload;

namespace value {

// Forward declaration
class Value;

// Dictionary type (key-value pairs)
class dict {
 public:
  using map_type = std::map<std::string, std::shared_ptr<Value>>;
  using iterator = map_type::iterator;
  using const_iterator = map_type::const_iterator;
  
  dict() = default;
  dict(const dict &rhs);
  dict(dict &&rhs) = default;
  dict& operator=(const dict &rhs);
  dict& operator=(dict &&rhs) = default;
  
  // Element access
  Value* at(const std::string &key);
  const Value* at(const std::string &key) const;
  
  Value* find(const std::string &key);
  const Value* find(const std::string &key) const;
  
  bool contains(const std::string &key) const {
    return data_.find(key) != data_.end();
  }
  
  // Modifiers
  void insert(const std::string &key, const Value &val);
  void insert(const std::string &key, Value &&val);
  void erase(const std::string &key);
  void clear() { data_.clear(); }
  
  // Capacity
  size_t size() const { return data_.size(); }
  bool empty() const { return data_.empty(); }
  
  // Iterators
  iterator begin() { return data_.begin(); }
  iterator end() { return data_.end(); }
  const_iterator begin() const { return data_.begin(); }
  const_iterator end() const { return data_.end(); }
  const_iterator cbegin() const { return data_.cbegin(); }
  const_iterator cend() const { return data_.cend(); }
  
  // Get all keys
  std::vector<std::string> keys() const;
  
  bool operator==(const dict &rhs) const;
  bool operator!=(const dict &rhs) const {
    return !(*this == rhs);
  }

 private:
  map_type data_;
};

// TimeSamples - animated/time-varying values
class TimeSamples {
 public:
  using time_type = double;
  using value_ptr = std::shared_ptr<Value>;
  using sample_type = std::pair<time_type, value_ptr>;
  using container_type = std::vector<sample_type>;
  
  TimeSamples() = default;
  TimeSamples(const TimeSamples &rhs);
  TimeSamples(TimeSamples &&rhs) = default;
  TimeSamples& operator=(const TimeSamples &rhs);
  TimeSamples& operator=(TimeSamples &&rhs) = default;
  
  // Add a sample at a specific time
  void AddSample(time_type time, const Value &val);
  void AddSample(time_type time, Value &&val);
  
  // Get value at specific time (with interpolation if needed)
  Value* GetValue(time_type time);
  const Value* GetValue(time_type time) const;
  
  // Get exact sample (no interpolation)
  Value* GetSample(time_type time);
  const Value* GetSample(time_type time) const;
  
  // Get bracket samples for interpolation
  bool GetBracketSamples(time_type time,
                         time_type *lower_time, Value **lower_value,
                         time_type *upper_time, Value **upper_value);
  bool GetBracketSamples(time_type time,
                         time_type *lower_time, const Value **lower_value,
                         time_type *upper_time, const Value **upper_value) const;
  
  // Access samples directly
  const container_type& samples() const { return samples_; }
  container_type& samples() { return samples_; }
  
  size_t size() const { return samples_.size(); }
  bool empty() const { return samples_.empty(); }
  
  // Get time range
  bool GetTimeRange(time_type *min_time, time_type *max_time) const;
  
  // Clear all samples
  void clear() { samples_.clear(); }
  
  // Sort samples by time (called automatically when needed)
  void Sort();
  
  bool operator==(const TimeSamples &rhs) const;
  bool operator!=(const TimeSamples &rhs) const {
    return !(*this == rhs);
  }

 private:
  container_type samples_;
  mutable bool sorted_{false};
  
  void EnsureSorted() const;
};

// ListOp - list editing operations
template <typename T>
class ListOp {
 public:
  enum Mode {
    Explicit,      // Replace the list
    Prepend,       // Add items to the beginning
    Append,        // Add items to the end
    Delete,        // Remove items
    Add,           // Add items (unordered)
    Reorder,       // Reorder items
    Reset          // Clear and replace
  };
  
  ListOp() = default;
  explicit ListOp(Mode mode) : mode_(mode) {}
  ListOp(Mode mode, const std::vector<T> &items) 
      : mode_(mode), items_(items) {}
  ListOp(Mode mode, std::vector<T> &&items)
      : mode_(mode), items_(std::move(items)) {}
  
  Mode GetMode() const { return mode_; }
  void SetMode(Mode mode) { mode_ = mode; }
  
  const std::vector<T>& GetItems() const { return items_; }
  std::vector<T>& GetItems() { return items_; }
  
  void SetItems(const std::vector<T> &items) { items_ = items; }
  void SetItems(std::vector<T> &&items) { items_ = std::move(items); }
  
  bool IsExplicit() const { return mode_ == Explicit; }
  bool HasKeys() const { return !items_.empty(); }
  
  // Apply this ListOp to a vector
  void ApplyTo(std::vector<T> *result) const;
  
  // Compose two ListOps
  ListOp<T> Compose(const ListOp<T> &other) const;
  
  void clear() {
    mode_ = Explicit;
    items_.clear();
  }
  
  bool empty() const {
    return (mode_ == Explicit) && items_.empty();
  }
  
  bool operator==(const ListOp<T> &rhs) const {
    return (mode_ == rhs.mode_) && (items_ == rhs.items_);
  }
  
  bool operator!=(const ListOp<T> &rhs) const {
    return !(*this == rhs);
  }

 private:
  Mode mode_{Explicit};
  std::vector<T> items_;
  
  // Optional: store deleted and added items separately
  std::vector<T> deleted_items_;
  std::vector<T> added_items_;
  std::vector<T> prepended_items_;
  std::vector<T> appended_items_;
};

// Common ListOp types
using ListOpToken = ListOp<token>;
using ListOpString = ListOp<std::string>;
using ListOpPath = ListOp<Path>;
using ListOpReference = ListOp<Reference>;
using ListOpPayload = ListOp<Payload>;
using ListOpInt = ListOp<int32_t>;
using ListOpInt64 = ListOp<int64_t>;
using ListOpUInt = ListOp<uint32_t>;
using ListOpUInt64 = ListOp<uint64_t>;

// VariantSelectionMap - for variant sets
using VariantSelectionMap = std::map<std::string, std::string>;

// LayerOffset - time offset and scale for layer composition
struct LayerOffset {
  double offset{0.0};
  double scale{1.0};
  
  LayerOffset() = default;
  LayerOffset(double o, double s) : offset(o), scale(s) {}
  
  bool IsIdentity() const {
    return (offset == 0.0) && (scale == 1.0);
  }
  
  double operator()(double t) const {
    return t * scale + offset;
  }
  
  LayerOffset GetInverse() const {
    if (scale == 0.0) {
      return LayerOffset(0, 0);
    }
    return LayerOffset(-offset / scale, 1.0 / scale);
  }
  
  bool operator==(const LayerOffset &rhs) const {
    return (offset == rhs.offset) && (scale == rhs.scale);
  }
  
  bool operator!=(const LayerOffset &rhs) const {
    return !(*this == rhs);
  }
};

// Range types
template <typename T>
struct range {
  T min;
  T max;
  
  range() : min(T()), max(T()) {}
  range(const T &mn, const T &mx) : min(mn), max(mx) {}
  
  bool contains(const T &v) const {
    return (v >= min) && (v <= max);
  }
  
  T size() const { return max - min; }
  
  bool empty() const { return min == max; }
  
  bool operator==(const range<T> &rhs) const {
    return (min == rhs.min) && (max == rhs.max);
  }
  
  bool operator!=(const range<T> &rhs) const {
    return !(*this == rhs);
  }
};

using range1i = range<int32_t>;
using range1f = range<float>;
using range1d = range<double>;

using range2i = range<int2>;
using range2f = range<float2>;
using range2d = range<double2>;

using range3i = range<int3>;
using range3f = range<float3>;
using range3d = range<double3>;

// Interval type (for time ranges)
using interval = range<double>;

// Type names for container types
constexpr auto kDict = "dictionary";
constexpr auto kTimeSamples = "timeSamples";
constexpr auto kVariantSelectionMap = "variantSelectionMap";
constexpr auto kLayerOffset = "layerOffset";

} // namespace value
} // namespace tinyusdz