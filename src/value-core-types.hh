// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Core value types for TinyUSDZ
// Part of the value-types.hh modularization effort

#pragma once

#include <string>
#include <cstdint>
#include <limits>
#include <cmath>

#include "token-type.hh"

namespace tinyusdz {
namespace value {

// Type identifiers for core types
constexpr auto kToken = "token";
constexpr auto kString = "string";
constexpr auto kPath = "Path";
constexpr auto kAssetPath = "asset";
constexpr auto kDictionary = "dictionary";
constexpr auto kTimeCode = "timecode";
constexpr auto kBool = "bool";

// SdfAssetPath
class AssetPath {
 public:
  AssetPath() = default;
  AssetPath(const std::string &a) : asset_path_(a) {}
  AssetPath(const std::string &a, const std::string &r)
      : asset_path_(a), resolved_path_(r) {}

  bool Resolve() {
    // TODO: Implement resolution
    return false;
  }

  const std::string &GetAssetPath() const { return asset_path_; }
  const std::string GetResolvedPath() const { return resolved_path_; }

  void SetAssetPath(const std::string &path) { asset_path_ = path; }
  void SetResolvedPath(const std::string &path) { resolved_path_ = path; }

  bool operator==(const AssetPath &rhs) const {
    return (asset_path_ == rhs.asset_path_) && 
           (resolved_path_ == rhs.resolved_path_);
  }

  bool operator!=(const AssetPath &rhs) const {
    return !(*this == rhs);
  }

 private:
  std::string asset_path_;
  std::string resolved_path_;
};

// TimeCode represents a time value in USD
class TimeCode {
 public:
  TimeCode() = default;
  explicit TimeCode(const double d) : time_(d) {}

  static constexpr double Default() {
    // Return qNaN. same in pxrUSD
    return std::numeric_limits<double>::quiet_NaN();
  }

  double Get(bool *is_default_timecode = nullptr) const {
    if (is_default_timecode) {
      (*is_default_timecode) = is_default();
    }
    return time_;
  }

  double get(bool *is_default_timecode = nullptr) const {
    return Get(is_default_timecode);
  }

  bool is_default() const {
    // TODO: Bitwise comparison
    return std::isnan(time_);
  }

  void set(double t) { time_ = t; }

  bool operator==(const TimeCode &rhs) const {
    if (is_default() && rhs.is_default()) {
      return true;
    }
    return time_ == rhs.time_;
  }

  bool operator!=(const TimeCode &rhs) const {
    return !(*this == rhs);
  }

  bool operator<(const TimeCode &rhs) const {
    // NaN is always greater than any value for sorting
    if (is_default()) return false;
    if (rhs.is_default()) return true;
    return time_ < rhs.time_;
  }

 private:
  double time_{std::numeric_limits<double>::quiet_NaN()};
};

static_assert(sizeof(TimeCode) == 8, "Size of TimeCode must be 8.");

// Simple timecode struct for serialization
struct timecode {
  double value;
  
  timecode() : value(TimeCode::Default()) {}
  explicit timecode(double v) : value(v) {}
  
  bool operator==(const timecode &rhs) const {
    return value == rhs.value;
  }
};

// String data for primvar and metadata (supports multi-line)
class StringData {
 public:
  StringData() = default;
  explicit StringData(const std::string &s) : value_(s) {}
  explicit StringData(std::string &&s) : value_(std::move(s)) {}
  
  const std::string &get() const { return value_; }
  void set(const std::string &s) { value_ = s; }
  void set(std::string &&s) { value_ = std::move(s); }
  
  bool is_multiline() const {
    return value_.find('\n') != std::string::npos;
  }
  
  bool empty() const { return value_.empty(); }
  size_t size() const { return value_.size(); }
  
  operator const std::string&() const { return value_; }
  
  bool operator==(const StringData &rhs) const {
    return value_ == rhs.value_;
  }
  
  bool operator!=(const StringData &rhs) const {
    return !(*this == rhs);
  }

 private:
  std::string value_;
  
  // Optional metadata
  int line_row{0};
  int line_col{0};
};

// Character types
using char2 = std::array<char, 2>;
using char3 = std::array<char, 3>;
using char4 = std::array<char, 4>;

using uchar = unsigned char;
using uchar2 = std::array<unsigned char, 2>;
using uchar3 = std::array<unsigned char, 3>;
using uchar4 = std::array<unsigned char, 4>;

// Integer vector types  
using int2 = std::array<int32_t, 2>;
using int3 = std::array<int32_t, 3>;
using int4 = std::array<int32_t, 4>;

using uint2 = std::array<uint32_t, 2>;
using uint3 = std::array<uint32_t, 3>;
using uint4 = std::array<uint32_t, 4>;

using short2 = std::array<int16_t, 2>;
using short3 = std::array<int16_t, 3>;
using short4 = std::array<int16_t, 4>;

using ushort2 = std::array<uint16_t, 2>;
using ushort3 = std::array<uint16_t, 3>;
using ushort4 = std::array<uint16_t, 4>;

// Type names for integer types
constexpr auto kChar = "char";
constexpr auto kChar2 = "char2";
constexpr auto kChar3 = "char3";
constexpr auto kChar4 = "char4";
constexpr auto kUChar = "uchar";
constexpr auto kUChar2 = "uchar2";
constexpr auto kUChar3 = "uchar3";
constexpr auto kUChar4 = "uchar4";

constexpr auto kInt = "int";
constexpr auto kInt2 = "int2";
constexpr auto kInt3 = "int3";
constexpr auto kInt4 = "int4";
constexpr auto kUInt = "uint";
constexpr auto kUInt2 = "uint2";
constexpr auto kUInt3 = "uint3";
constexpr auto kUInt4 = "uint4";

constexpr auto kInt64 = "int64";
constexpr auto kUInt64 = "uint64";

constexpr auto kShort = "short";
constexpr auto kShort2 = "short2";
constexpr auto kShort3 = "short3";
constexpr auto kShort4 = "short4";
constexpr auto kUShort = "ushort";
constexpr auto kUShort2 = "ushort2";
constexpr auto kUShort3 = "ushort3";
constexpr auto kUShort4 = "ushort4";

} // namespace value
} // namespace tinyusdz