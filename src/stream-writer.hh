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
// Simple byte stream writer. Consider endianness when writing 2, 4, 8 bytes data.
//

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>

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

#if 0 // TODO

///
/// Simple stream writeer
///
class StreamWriter {
 public:
  // max_length: Max byte lengths.
  explicit StreamWriter(const size_t max_length,
                        const bool swap_endian)
      : max_length_(max_length), swap_endian_(swap_endian), idx_(0) {
    (void)pad_;
  }

  bool seek_set(const uint64_t offset) const {
    if (offset >= max_length_) {
      return false;
    }

    idx_ = offset;
    return true;
  }

  bool seek_from_current(const int64_t offset) const {
    if ((int64_t(idx_) + offset) < 0) {
      return false;
    }

    if (size_t((int64_t(idx_) + offset)) > length_) {
      return false;
    }

    idx_ = size_t(int64_t(idx_) + offset);
    return true;
  }

  size_t writeN(const size_t n, const uint64_t dst_len, uint8_t *dst) const {
    size_t len = n;
    if ((idx_ + len) > length_) {
      len = length_ - size_t(idx_);
    }

    if (len > 0) {
      if (dst_len < len) {
        // dst does not have enough space. return 0 for a while.
        return 0;
      }

      memcpy(dst, &binary_[idx_], len);
      idx_ += len;
      return len;

    } else {
      return 0;
    }
  }

  bool write1(uint8_t *ret) const {
    if ((idx_ + 1) > length_) {
      return false;
    }

    const uint8_t val = binary_[idx_];

    (*ret) = val;
    idx_ += 1;

    return true;
  }

  bool write_bool(bool *ret) const {
    if ((idx_ + 1) > length_) {
      return false;
    }

    const char val = static_cast<const char>(binary_[idx_]);

    (*ret) = bool(val);
    idx_ += 1;

    return true;
  }

  bool write1(char *ret) const {
    if ((idx_ + 1) > length_) {
      return false;
    }

    const char val = static_cast<const char>(binary_[idx_]);

    (*ret) = val;
    idx_ += 1;

    return true;
  }

  bool write2(unsigned short *ret) const {
    if ((idx_ + 2) > length_) {
      return false;
    }

    unsigned short val =
        *(reinterpret_cast<const unsigned short *>(&binary_[idx_]));

    if (swap_endian_) {
      swap2(&val);
    }

    (*ret) = val;
    idx_ += 2;

    return true;
  }

  bool write4(uint32_t *ret) const {
    if ((idx_ + 4) > length_) {
      return false;
    }

    uint32_t val = *(reinterpret_cast<const uint32_t *>(&binary_[idx_]));

    if (swap_endian_) {
      swap4(&val);
    }

    (*ret) = val;
    idx_ += 4;

    return true;
  }

  bool write4(int *ret) const {
    if ((idx_ + 4) > length_) {
      return false;
    }

    int val = *(reinterpret_cast<const int *>(&binary_[idx_]));

    if (swap_endian_) {
      swap4(&val);
    }

    (*ret) = val;
    idx_ += 4;

    return true;
  }

  bool write8(uint64_t *ret) const {
    if ((idx_ + 8) > length_) {
      return false;
    }

    uint64_t val = *(reinterpret_cast<const uint64_t *>(&binary_[idx_]));

    if (swap_endian_) {
      swap8(&val);
    }

    (*ret) = val;
    idx_ += 8;

    return true;
  }

  bool write8(int64_t *ret) const {
    if ((idx_ + 8) > length_) {
      return false;
    }

    int64_t val = *(reinterpret_cast<const int64_t *>(&binary_[idx_]));

    if (swap_endian_) {
      swap8(&val);
    }

    (*ret) = val;
    idx_ += 8;

    return true;
  }

  bool write_float(const float value) const {
    if (!write4(reinterpret_cast<const int *>(&value))) {
      return false;
    }

    return true;
  }

  bool write_double(const double value) const {
    if (!write8(reinterpret_cast<const uint64_t *>(&value))) {
      return false;
    }

    return true;
  }

  size_t tell() const { return size_t(idx_); }
  //bool eof() const { return idx_ >= length_; }

  bool swap_endian() const { return swap_endian_; }

  size_t size() const { return length_; }

 private:

  bool Reserve_(size_t additional_bytes) {
    size_t req_bytes = binary_.size() + additional_bytes;

    if (req_bytes > max_length_) {
      return false;
    }

    // grow +20%

    //
    binary_.resize

  }

  const std::vector<uint8_t> binary_;
  const size_t max_length_;
  bool swap_endian_;
  char pad_[7];
  mutable uint64_t idx_;
};
#endif

///
/// ChunkedTypedArray: A chunked storage container for efficient growth
///
template<typename T>
class ChunkedTypedArray {
 public:
  using value_type = T;
  using size_type = size_t;

  static constexpr size_t kDefaultChunkSize = 4096; // bytes per chunk

  explicit ChunkedTypedArray(size_t chunk_size_bytes = kDefaultChunkSize)
      : chunk_size_(chunk_size_bytes / sizeof(T)), total_size_(0) {
    if (chunk_size_ == 0) {
      chunk_size_ = 1;
    }
  }

  ~ChunkedTypedArray() = default;

  void push_back(const T& value) {
    if (chunks_.empty() || chunks_.back().size() >= chunk_size_) {
      chunks_.emplace_back();
      chunks_.back().reserve(chunk_size_);
    }
    chunks_.back().push_back(value);
    ++total_size_;
  }

  void append(const T* data, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      push_back(data[i]);
    }
  }

  T& operator[](size_t index) {
    size_t chunk_idx = index / chunk_size_;
    size_t elem_idx = index % chunk_size_;
    return chunks_[chunk_idx][elem_idx];
  }

  const T& operator[](size_t index) const {
    size_t chunk_idx = index / chunk_size_;
    size_t elem_idx = index % chunk_size_;
    return chunks_[chunk_idx][elem_idx];
  }

  size_t size() const noexcept { return total_size_; }
  bool empty() const noexcept { return total_size_ == 0; }

  void clear() {
    chunks_.clear();
    total_size_ = 0;
  }

  void reserve(size_t capacity) {
    size_t needed_chunks = (capacity + chunk_size_ - 1) / chunk_size_;
    if (needed_chunks > chunks_.size()) {
      chunks_.reserve(needed_chunks);
    }
  }

  std::vector<T> flatten() const {
    std::vector<T> result;
    result.reserve(total_size_);
    for (const auto& chunk : chunks_) {
      result.insert(result.end(), chunk.begin(), chunk.end());
    }
    return result;
  }

  template<typename OutputIt>
  void copy_to(OutputIt out) const {
    for (const auto& chunk : chunks_) {
      std::copy(chunk.begin(), chunk.end(), out);
    }
  }

  const T* data() const {
    if (chunks_.size() == 1) {
      return chunks_[0].data();
    }
    return nullptr; // Cannot provide contiguous data for multiple chunks
  }

  T* data() {
    if (chunks_.size() == 1) {
      return chunks_[0].data();
    }
    return nullptr; // Cannot provide contiguous data for multiple chunks
  }

 private:
  std::vector<std::vector<T>> chunks_;
  size_t chunk_size_; // elements per chunk
  size_t total_size_;
};

///
/// ChunkedStreamWriter: Simple StreamWriter with ChunkedTypedArray<uint8_t> backing storage
///
class ChunkedStreamWriter {
 public:
  explicit ChunkedStreamWriter(bool swap_endian = false, size_t chunk_size_bytes = 4096)
      : swap_endian_(swap_endian), data_(chunk_size_bytes), write_pos_(0) {
  }

  bool write1(uint8_t value) {
    data_.push_back(value);
    ++write_pos_;
    return true;
  }

  bool write1(char value) {
    data_.push_back(static_cast<uint8_t>(value));
    ++write_pos_;
    return true;
  }

  bool write2(uint16_t value) {
    if (swap_endian_) {
      swap2(&value);
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data_.append(bytes, 2);
    write_pos_ += 2;
    return true;
  }

  bool write4(uint32_t value) {
    if (swap_endian_) {
      swap4(&value);
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data_.append(bytes, 4);
    write_pos_ += 4;
    return true;
  }

  bool write4(int value) {
    if (swap_endian_) {
      swap4(&value);
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data_.append(bytes, 4);
    write_pos_ += 4;
    return true;
  }

  bool write8(uint64_t value) {
    if (swap_endian_) {
      swap8(&value);
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data_.append(bytes, 8);
    write_pos_ += 8;
    return true;
  }

  bool write8(int64_t value) {
    if (swap_endian_) {
      swap8(&value);
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data_.append(bytes, 8);
    write_pos_ += 8;
    return true;
  }

  bool write_float(float value) {
    return write4(*reinterpret_cast<const uint32_t*>(&value));
  }

  bool write_double(double value) {
    return write8(*reinterpret_cast<const uint64_t*>(&value));
  }

  bool write_bool(bool value) {
    return write1(static_cast<uint8_t>(value ? 1 : 0));
  }

  size_t writeN(const uint8_t* src, size_t n) {
    data_.append(src, n);
    write_pos_ += n;
    return n;
  }

  size_t tell() const { return write_pos_; }

  bool seek_set(size_t offset) {
    if (offset <= data_.size()) {
      write_pos_ = offset;
      return true;
    }
    return false;
  }

  bool seek_from_current(int64_t offset) {
    int64_t new_pos = static_cast<int64_t>(write_pos_) + offset;
    if (new_pos >= 0 && static_cast<size_t>(new_pos) <= data_.size()) {
      write_pos_ = static_cast<size_t>(new_pos);
      return true;
    }
    return false;
  }

  bool swap_endian() const { return swap_endian_; }

  size_t size() const { return data_.size(); }

  void clear() {
    data_.clear();
    write_pos_ = 0;
  }

  std::vector<uint8_t> flatten() const {
    return data_.flatten();
  }

  template<typename OutputIt>
  void copy_to(OutputIt out) const {
    data_.copy_to(out);
  }

  const ChunkedTypedArray<uint8_t>& data() const { return data_; }

 private:
  bool swap_endian_;
  ChunkedTypedArray<uint8_t> data_;
  size_t write_pos_;
  char pad_[7];

  static inline void swap2(uint16_t* val) {
    uint16_t tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[1];
    dst[1] = src[0];
  }

  static inline void swap4(uint32_t* val) {
    uint32_t tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];
  }

  static inline void swap4(int* val) {
    int tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];
  }

  static inline void swap8(uint64_t* val) {
    uint64_t tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[7];
    dst[1] = src[6];
    dst[2] = src[5];
    dst[3] = src[4];
    dst[4] = src[3];
    dst[5] = src[2];
    dst[6] = src[1];
    dst[7] = src[0];
  }

  static inline void swap8(int64_t* val) {
    int64_t tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[7];
    dst[1] = src[6];
    dst[2] = src[5];
    dst[3] = src[4];
    dst[4] = src[3];
    dst[5] = src[2];
    dst[6] = src[1];
    dst[7] = src[0];
  }
};

} // namespace tinyusdz
