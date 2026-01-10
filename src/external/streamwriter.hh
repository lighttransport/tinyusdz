// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Syoyo Fujita and many contributors.
// All rights reserved.
//
// StreamWriter: Simple header-only stream writer with endian support
//
// Part of TinyEXR V2 API (EXPERIMENTAL)
//
// Supports two modes:
// 1. Dynamic mode: Writes to std::vector<uint8_t> that grows automatically
// 2. Bounded mode: Writes to fixed-size buffer with bounds checking

#ifndef TINYEXR_STREAMWRITER_HH_
#define TINYEXR_STREAMWRITER_HH_

#include <cstdint>
#include <cstring>
#include <vector>

namespace tinyexr {

// Forward declare Endian from streamreader.hh
enum class Endian;

// Writer modes
enum class WriterMode {
  Dynamic,    // Grows automatically (uses std::vector)
  Bounded     // Fixed-size buffer (no allocation)
};

class StreamWriter {
public:
  // Constructor for dynamic mode (grows automatically)
  StreamWriter(Endian endian = Endian::Little)
      : mode_(WriterMode::Dynamic),
        bounded_data_(nullptr),
        bounded_capacity_(0),
        pos_(0),
        endian_(endian) {
    // Detect native endian
    uint16_t test = 0x0001;
    native_is_little_ = (*reinterpret_cast<uint8_t*>(&test) == 0x01);

    // Determine if we need to swap bytes
    if (endian_ == Endian::Native) {
      needs_swap_ = false;
    } else {
      bool data_is_little = (endian_ == Endian::Little);
      needs_swap_ = (data_is_little != native_is_little_);
    }
  }

  // Constructor for bounded mode (fixed-size buffer)
  StreamWriter(uint8_t* data, size_t capacity, Endian endian = Endian::Little)
      : mode_(WriterMode::Bounded),
        bounded_data_(data),
        bounded_capacity_(capacity),
        pos_(0),
        endian_(endian) {
    // Detect native endian
    uint16_t test = 0x0001;
    native_is_little_ = (*reinterpret_cast<uint8_t*>(&test) == 0x01);

    // Determine if we need to swap bytes
    if (endian_ == Endian::Native) {
      needs_swap_ = false;
    } else {
      bool data_is_little = (endian_ == Endian::Little);
      needs_swap_ = (data_is_little != native_is_little_);
    }
  }

  // Write n bytes from source buffer
  // Returns false on error (out of bounds in bounded mode)
  bool write(size_t n, const uint8_t* src) {
    if (!src || n == 0) {
      return false;
    }

    if (mode_ == WriterMode::Dynamic) {
      // Ensure capacity
      if (pos_ + n > dynamic_data_.size()) {
        dynamic_data_.resize(pos_ + n);
      }
      std::memcpy(&dynamic_data_[pos_], src, n);
      pos_ += n;
      return true;
    } else {
      // Bounded mode - check capacity
      if (!bounded_data_ || pos_ + n > bounded_capacity_) {
        return false;  // Out of bounds
      }
      std::memcpy(bounded_data_ + pos_, src, n);
      pos_ += n;
      return true;
    }
  }

  // Write 1 byte (uint8_t)
  bool write1(uint8_t val) {
    return write(1, &val);
  }

  // Write 2 bytes (uint16_t) with endian swap if needed
  bool write2(uint16_t val) {
    uint8_t buf[2];

    if (needs_swap_) {
      buf[0] = static_cast<uint8_t>(val & 0xFF);
      buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    } else {
      std::memcpy(buf, &val, 2);
    }

    return write(2, buf);
  }

  // Write 4 bytes (uint32_t) with endian swap if needed
  bool write4(uint32_t val) {
    uint8_t buf[4];

    if (needs_swap_) {
      buf[0] = static_cast<uint8_t>(val & 0xFF);
      buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
      buf[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
      buf[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    } else {
      std::memcpy(buf, &val, 4);
    }

    return write(4, buf);
  }

  // Write 8 bytes (uint64_t) with endian swap if needed
  bool write8(uint64_t val) {
    uint8_t buf[8];

    if (needs_swap_) {
      buf[0] = static_cast<uint8_t>(val & 0xFF);
      buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
      buf[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
      buf[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
      buf[4] = static_cast<uint8_t>((val >> 32) & 0xFF);
      buf[5] = static_cast<uint8_t>((val >> 40) & 0xFF);
      buf[6] = static_cast<uint8_t>((val >> 48) & 0xFF);
      buf[7] = static_cast<uint8_t>((val >> 56) & 0xFF);
    } else {
      std::memcpy(buf, &val, 8);
    }

    return write(8, buf);
  }

  // Write float (4 bytes) with endian swap if needed
  bool write_float(float val) {
    uint32_t bits;
    std::memcpy(&bits, &val, 4);
    return write4(bits);
  }

  // Write double (8 bytes) with endian swap if needed
  bool write_double(double val) {
    uint64_t bits;
    std::memcpy(&bits, &val, 8);
    return write8(bits);
  }

  // Write null-terminated string (includes null terminator)
  bool write_string(const char* str) {
    if (!str) {
      return false;
    }
    size_t len = 0;
    while (str[len] != '\0') {
      len++;
    }
    // Write string including null terminator
    return write(len + 1, reinterpret_cast<const uint8_t*>(str));
  }

  // Write fixed-length string (no null terminator)
  bool write_fixed_string(const char* str, size_t len) {
    if (!str) {
      return false;
    }
    return write(len, reinterpret_cast<const uint8_t*>(str));
  }

  // Seek to absolute position
  // In dynamic mode, extends buffer if needed
  // In bounded mode, fails if position exceeds capacity
  bool seek(size_t pos) {
    if (mode_ == WriterMode::Dynamic) {
      if (pos > dynamic_data_.size()) {
        dynamic_data_.resize(pos);
      }
      pos_ = pos;
      return true;
    } else {
      if (pos > bounded_capacity_) {
        return false;
      }
      pos_ = pos;
      return true;
    }
  }

  // Seek relative to current position
  bool seek_relative(int64_t offset) {
    int64_t new_pos = static_cast<int64_t>(pos_) + offset;
    if (new_pos < 0) {
      return false;
    }
    return seek(static_cast<size_t>(new_pos));
  }

  // Rewind to beginning
  void rewind() {
    pos_ = 0;
  }

  // Get current position
  size_t tell() const {
    return pos_;
  }

  // Get current size (bytes written)
  size_t size() const {
    if (mode_ == WriterMode::Dynamic) {
      return dynamic_data_.size();
    } else {
      return pos_;  // In bounded mode, size is the current position
    }
  }

  // Get capacity (for bounded mode)
  size_t capacity() const {
    if (mode_ == WriterMode::Dynamic) {
      return dynamic_data_.capacity();
    } else {
      return bounded_capacity_;
    }
  }

  // Check if we're at or past the end (bounded mode only)
  bool at_capacity() const {
    if (mode_ == WriterMode::Dynamic) {
      return false;  // Dynamic mode never at capacity
    } else {
      return pos_ >= bounded_capacity_;
    }
  }

  // Get remaining capacity (bounded mode)
  size_t remaining() const {
    if (mode_ == WriterMode::Dynamic) {
      return static_cast<size_t>(-1);  // Unlimited
    } else {
      return (pos_ < bounded_capacity_) ? (bounded_capacity_ - pos_) : 0;
    }
  }

  // Get mode
  WriterMode mode() const {
    return mode_;
  }

  // Get reference to internal buffer (dynamic mode only)
  const std::vector<uint8_t>& data() const {
    return dynamic_data_;
  }

  // Get mutable reference to internal buffer (dynamic mode only)
  std::vector<uint8_t>& data() {
    return dynamic_data_;
  }

  // Get pointer to data (works for both modes)
  const uint8_t* data_ptr() const {
    if (mode_ == WriterMode::Dynamic) {
      return dynamic_data_.empty() ? nullptr : &dynamic_data_[0];
    } else {
      return bounded_data_;
    }
  }

  // Reserve capacity (dynamic mode only)
  void reserve(size_t capacity) {
    if (mode_ == WriterMode::Dynamic) {
      dynamic_data_.reserve(capacity);
    }
  }

  // Clear and reset (dynamic mode only)
  void clear() {
    if (mode_ == WriterMode::Dynamic) {
      dynamic_data_.clear();
    }
    pos_ = 0;
  }

private:
  WriterMode mode_;

  // Dynamic mode storage
  std::vector<uint8_t> dynamic_data_;

  // Bounded mode storage
  uint8_t* bounded_data_;
  size_t bounded_capacity_;

  size_t pos_;
  Endian endian_;
  bool native_is_little_;
  bool needs_swap_;
};

}  // namespace tinyexr

#endif  // TINYEXR_STREAMWRITER_HH_
