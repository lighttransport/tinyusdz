// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Binary Stream Reader
// Simple binary file reading interface

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

/// Binary stream reader for USDC files
/// Wraps a memory buffer with position tracking and endian-aware reading
class StreamReader {
public:
  /// Construct from memory buffer (does not take ownership)
  StreamReader(const uint8_t* data, size_t size)
      : data_(data), size_(size), pos_(0) {}

  /// Construct from vector (does not take ownership)
  explicit StreamReader(const std::vector<uint8_t>& buffer)
      : data_(buffer.data()), size_(buffer.size()), pos_(0) {}

  // ============================================================
  // Position control
  // ============================================================

  /// Get current position
  size_t position() const { return pos_; }

  /// Get total size
  size_t size() const { return size_; }

  /// Get remaining bytes
  size_t remaining() const { return (pos_ < size_) ? (size_ - pos_) : 0; }

  /// Check if at end
  bool at_end() const { return pos_ >= size_; }

  /// Seek to absolute position
  bool seek(size_t pos) {
    if (pos > size_) return false;
    pos_ = pos;
    return true;
  }

  /// Skip bytes forward
  bool skip(size_t count) {
    if (pos_ + count > size_) return false;
    pos_ += count;
    return true;
  }

  /// Align position to boundary
  bool align(size_t alignment) {
    size_t aligned = (pos_ + alignment - 1) & ~(alignment - 1);
    return seek(aligned);
  }

  // ============================================================
  // Raw reading
  // ============================================================

  /// Read raw bytes into buffer
  bool read(void* dst, size_t count) {
    if (pos_ + count > size_) return false;
    std::memcpy(dst, data_ + pos_, count);
    pos_ += count;
    return true;
  }

  /// Read raw bytes into vector
  bool read(std::vector<uint8_t>& dst, size_t count) {
    if (pos_ + count > size_) return false;
    dst.resize(count);
    std::memcpy(dst.data(), data_ + pos_, count);
    pos_ += count;
    return true;
  }

  /// Get pointer to current position (for zero-copy access)
  const uint8_t* current() const {
    return (pos_ < size_) ? (data_ + pos_) : nullptr;
  }

  /// Get pointer at absolute position
  const uint8_t* at(size_t pos) const {
    return (pos < size_) ? (data_ + pos) : nullptr;
  }

  // ============================================================
  // Typed reading (little-endian)
  // ============================================================

  bool read_u8(uint8_t& v) { return read(&v, 1); }
  bool read_i8(int8_t& v) { return read(&v, 1); }

  bool read_u16(uint16_t& v) {
    if (!read(&v, 2)) return false;
    // Assume little-endian (USDC is LE)
    return true;
  }

  bool read_i16(int16_t& v) {
    uint16_t u;
    if (!read_u16(u)) return false;
    v = static_cast<int16_t>(u);
    return true;
  }

  bool read_u32(uint32_t& v) {
    if (!read(&v, 4)) return false;
    return true;
  }

  bool read_i32(int32_t& v) {
    uint32_t u;
    if (!read_u32(u)) return false;
    v = static_cast<int32_t>(u);
    return true;
  }

  bool read_u64(uint64_t& v) {
    if (!read(&v, 8)) return false;
    return true;
  }

  bool read_i64(int64_t& v) {
    uint64_t u;
    if (!read_u64(u)) return false;
    v = static_cast<int64_t>(u);
    return true;
  }

  bool read_f32(float& v) {
    uint32_t u;
    if (!read_u32(u)) return false;
    std::memcpy(&v, &u, 4);
    return true;
  }

  bool read_f64(double& v) {
    uint64_t u;
    if (!read_u64(u)) return false;
    std::memcpy(&v, &u, 8);
    return true;
  }

  // ============================================================
  // String reading
  // ============================================================

  /// Read null-terminated string
  bool read_cstring(std::string& s) {
    s.clear();
    while (pos_ < size_) {
      char c = static_cast<char>(data_[pos_++]);
      if (c == '\0') return true;
      s += c;
    }
    return false;  // No null terminator found
  }

  /// Read fixed-length string (may not be null-terminated)
  bool read_fixed_string(std::string& s, size_t len) {
    if (pos_ + len > size_) return false;
    s.assign(reinterpret_cast<const char*>(data_ + pos_), len);
    pos_ += len;
    // Remove trailing nulls
    while (!s.empty() && s.back() == '\0') {
      s.pop_back();
    }
    return true;
  }

  // ============================================================
  // Array reading
  // ============================================================

  /// Read array of uint32_t
  bool read_u32_array(std::vector<uint32_t>& arr, size_t count) {
    arr.resize(count);
    return read(arr.data(), count * sizeof(uint32_t));
  }

  /// Read array of int32_t
  bool read_i32_array(std::vector<int32_t>& arr, size_t count) {
    arr.resize(count);
    return read(arr.data(), count * sizeof(int32_t));
  }

  /// Read array of float
  bool read_f32_array(std::vector<float>& arr, size_t count) {
    arr.resize(count);
    return read(arr.data(), count * sizeof(float));
  }

  /// Read array of double
  bool read_f64_array(std::vector<double>& arr, size_t count) {
    arr.resize(count);
    return read(arr.data(), count * sizeof(double));
  }

private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_;
};

}  // namespace next
}  // namespace tinyusdz
