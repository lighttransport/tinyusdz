// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - little-endian byte stream writer.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

class StreamWriter {
 public:
  explicit StreamWriter(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

  size_t position() const { return buffer_.size(); }

  void write_bytes(const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), ptr, ptr + size);
  }

  void write_u8(uint8_t v) { buffer_.push_back(v); }
  void write_i8(int8_t v) { buffer_.push_back(static_cast<uint8_t>(v)); }

  void write_u16(uint16_t v) {
    buffer_.push_back(static_cast<uint8_t>(v));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
  }

  void write_u32(uint32_t v) {
    buffer_.push_back(static_cast<uint8_t>(v));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
    buffer_.push_back(static_cast<uint8_t>(v >> 16));
    buffer_.push_back(static_cast<uint8_t>(v >> 24));
  }

  void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }

  void write_u64(uint64_t v) {
    write_u32(static_cast<uint32_t>(v));
    write_u32(static_cast<uint32_t>(v >> 32));
  }

  void write_i64(int64_t v) { write_u64(static_cast<uint64_t>(v)); }

  void write_float(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(float));
    write_u32(bits);
  }

  void write_double(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(double));
    write_u64(bits);
  }

  void write_string(const std::string& s) {
    write_u64(s.size());
    write_bytes(s.data(), s.size());
  }

  size_t align(size_t alignment) {
    size_t pos = buffer_.size();
    size_t aligned = (pos + alignment - 1) & ~(alignment - 1);
    while (buffer_.size() < aligned) {
      buffer_.push_back(0);
    }
    return aligned;
  }

  void patch_u64(size_t offset, uint64_t v) {
    buffer_[offset + 0] = static_cast<uint8_t>(v);
    buffer_[offset + 1] = static_cast<uint8_t>(v >> 8);
    buffer_[offset + 2] = static_cast<uint8_t>(v >> 16);
    buffer_[offset + 3] = static_cast<uint8_t>(v >> 24);
    buffer_[offset + 4] = static_cast<uint8_t>(v >> 32);
    buffer_[offset + 5] = static_cast<uint8_t>(v >> 40);
    buffer_[offset + 6] = static_cast<uint8_t>(v >> 48);
    buffer_[offset + 7] = static_cast<uint8_t>(v >> 56);
  }

 private:
  std::vector<uint8_t>& buffer_;
};

}  // namespace next
}  // namespace tinyusdz
