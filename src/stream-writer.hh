/*
Copyright (c) 2022 - Present Syoyo Fujita.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Syoyo Fujita nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

//
// Simple stream writer for pretty printing. Can be used instead of std::stringstream for better control.
//

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <string>

namespace tinyusdz {

namespace {

static inline void swap2(unsigned short *val) {
  unsigned short tmp = *val;
  uint8_t *dst = reinterpret_cast<uint8_t *>(val);
  uint8_t *src = reinterpret_cast<uint8_t *>(&tmp);

  dst[0] = src[1];
  dst[1] = src[0];
}

static inline void swap4(uint32_t *val) {
  uint32_t tmp = *val;
  uint8_t *dst = reinterpret_cast<uint8_t *>(val);
  uint8_t *src = reinterpret_cast<uint8_t *>(&tmp);

  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
}

static inline void swap4(int *val) {
  int tmp = *val;
  uint8_t *dst = reinterpret_cast<uint8_t *>(val);
  uint8_t *src = reinterpret_cast<uint8_t *>(&tmp);

  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
}

static inline void swap8(uint64_t *val) {
  uint64_t tmp = (*val);
  uint8_t *dst = reinterpret_cast<uint8_t *>(val);
  uint8_t *src = reinterpret_cast<uint8_t *>(&tmp);

  dst[0] = src[7];
  dst[1] = src[6];
  dst[2] = src[5];
  dst[3] = src[4];
  dst[4] = src[3];
  dst[5] = src[2];
  dst[6] = src[1];
  dst[7] = src[0];
}

static inline void swap8(int64_t *val) {
  int64_t tmp = (*val);
  uint8_t *dst = reinterpret_cast<uint8_t *>(val);
  uint8_t *src = reinterpret_cast<uint8_t *>(&tmp);

  dst[0] = src[7];
  dst[1] = src[6];
  dst[2] = src[5];
  dst[3] = src[4];
  dst[4] = src[3];
  dst[5] = src[2];
  dst[6] = src[1];
  dst[7] = src[0];
}

} // namespace

///
/// Simple stream writer for pretty printing
///
class StreamWriter {
 public:
  explicit StreamWriter(const size_t max_length = 1024 * 1024 * 10)  // 10MB default
      : max_length_(max_length) {
    buffer_.reserve(1024);  // Initial reserve for performance
  }

  // Write string
  void write(const std::string& str) {
    if (buffer_.size() + str.size() > max_length_) {
      return;  // Silently ignore if exceeds max
    }
    buffer_ += str;
  }

  // Write C-string
  void write(const char* str) {
    if (!str) return;
    size_t len = std::strlen(str);
    if (buffer_.size() + len > max_length_) {
      return;
    }
    buffer_ += str;
  }

  // Write single char
  void write(char c) {
    if (buffer_.size() + 1 > max_length_) {
      return;
    }
    buffer_ += c;
  }

  // Write integer types
  void write(int value) {
    write(std::to_string(value));
  }

  void write(unsigned int value) {
    write(std::to_string(value));
  }

  void write(long value) {
    write(std::to_string(value));
  }

  void write(unsigned long value) {
    write(std::to_string(value));
  }

  void write(long long value) {
    write(std::to_string(value));
  }

  void write(unsigned long long value) {
    write(std::to_string(value));
  }

  // Write floating point
  void write(float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
    write(buf);
  }

  void write(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", value);
    write(buf);
  }

  // Write boolean
  void write(bool value) {
    write(value ? "true" : "false");
  }

  // Convenience operator<<
  template<typename T>
  StreamWriter& operator<<(const T& value) {
    write(value);
    return *this;
  }

  // Get the accumulated string
  const std::string& str() const { return buffer_; }

  // Get C-string
  const char* c_str() const { return buffer_.c_str(); }

  // Clear buffer
  void clear() { buffer_.clear(); }

  // Get current size
  size_t size() const { return buffer_.size(); }

  // Check if empty
  bool empty() const { return buffer_.empty(); }

  // Reserve capacity
  void reserve(size_t capacity) {
    if (capacity <= max_length_) {
      buffer_.reserve(capacity);
    }
  }

 private:
  std::string buffer_;
  const size_t max_length_;
};

} // namespace tinyusdz
