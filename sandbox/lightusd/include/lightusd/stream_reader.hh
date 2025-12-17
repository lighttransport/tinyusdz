// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// StreamReader - Safe binary stream reader with bounds checking

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace lightusd {
namespace v1 {

// Byte swapping utilities for endianness handling
namespace detail {

inline void swap2(uint16_t* val) {
    uint16_t tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[1];
    dst[1] = src[0];
}

inline void swap4(uint32_t* val) {
    uint32_t tmp = *val;
    uint8_t* dst = reinterpret_cast<uint8_t*>(val);
    uint8_t* src = reinterpret_cast<uint8_t*>(&tmp);
    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];
}

inline void swap8(uint64_t* val) {
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

} // namespace detail

/// Safe binary stream reader with bounds checking
class StreamReader {
public:
    StreamReader(const uint8_t* data, size_t size, bool swap_endian = false)
        : data_(data), size_(size), pos_(0), swap_endian_(swap_endian) {}

    // Position control
    bool seek(size_t pos) {
        if (pos > size_) return false;
        pos_ = pos;
        return true;
    }

    bool seek_relative(int64_t offset) {
        int64_t new_pos = static_cast<int64_t>(pos_) + offset;
        if (new_pos < 0 || static_cast<size_t>(new_pos) > size_) return false;
        pos_ = static_cast<size_t>(new_pos);
        return true;
    }

    size_t tell() const { return pos_; }
    size_t size() const { return size_; }
    size_t remaining() const { return size_ - pos_; }
    bool eof() const { return pos_ >= size_; }

    // Raw data access
    const uint8_t* data() const { return data_; }
    const uint8_t* current() const { return data_ + pos_; }

    // Read raw bytes
    bool read(void* dst, size_t count) {
        if (pos_ + count > size_) return false;
        std::memcpy(dst, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    // Skip bytes
    bool skip(size_t count) {
        if (pos_ + count > size_) return false;
        pos_ += count;
        return true;
    }

    // Read single byte
    bool read_u8(uint8_t* out) {
        if (pos_ + 1 > size_) return false;
        *out = data_[pos_++];
        return true;
    }

    bool read_i8(int8_t* out) {
        return read_u8(reinterpret_cast<uint8_t*>(out));
    }

    // Read 16-bit values
    bool read_u16(uint16_t* out) {
        if (pos_ + 2 > size_) return false;
        std::memcpy(out, data_ + pos_, 2);
        if (swap_endian_) detail::swap2(out);
        pos_ += 2;
        return true;
    }

    bool read_i16(int16_t* out) {
        return read_u16(reinterpret_cast<uint16_t*>(out));
    }

    // Read 32-bit values
    bool read_u32(uint32_t* out) {
        if (pos_ + 4 > size_) return false;
        std::memcpy(out, data_ + pos_, 4);
        if (swap_endian_) detail::swap4(out);
        pos_ += 4;
        return true;
    }

    bool read_i32(int32_t* out) {
        return read_u32(reinterpret_cast<uint32_t*>(out));
    }

    bool read_f32(float* out) {
        return read_u32(reinterpret_cast<uint32_t*>(out));
    }

    // Read 64-bit values
    bool read_u64(uint64_t* out) {
        if (pos_ + 8 > size_) return false;
        std::memcpy(out, data_ + pos_, 8);
        if (swap_endian_) detail::swap8(out);
        pos_ += 8;
        return true;
    }

    bool read_i64(int64_t* out) {
        return read_u64(reinterpret_cast<uint64_t*>(out));
    }

    bool read_f64(double* out) {
        return read_u64(reinterpret_cast<uint64_t*>(out));
    }

    // Read string (null-terminated or fixed length)
    bool read_string(std::string* out, size_t max_len = 0) {
        out->clear();
        if (max_len > 0) {
            // Fixed length string
            if (pos_ + max_len > size_) return false;
            out->assign(reinterpret_cast<const char*>(data_ + pos_), max_len);
            pos_ += max_len;
            // Trim null characters
            size_t null_pos = out->find('\0');
            if (null_pos != std::string::npos) {
                out->resize(null_pos);
            }
        } else {
            // Null-terminated string
            size_t start = pos_;
            while (pos_ < size_ && data_[pos_] != 0) {
                ++pos_;
            }
            out->assign(reinterpret_cast<const char*>(data_ + start), pos_ - start);
            if (pos_ < size_) ++pos_;  // Skip null terminator
        }
        return true;
    }

    // Read length-prefixed string (32-bit length prefix)
    bool read_length_string(std::string* out) {
        uint32_t len;
        if (!read_u32(&len)) return false;
        if (pos_ + len > size_) return false;
        out->assign(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return true;
    }

    // Peek without advancing position
    bool peek_u8(uint8_t* out) const {
        if (pos_ + 1 > size_) return false;
        *out = data_[pos_];
        return true;
    }

    bool peek_u32(uint32_t* out) const {
        if (pos_ + 4 > size_) return false;
        std::memcpy(out, data_ + pos_, 4);
        if (swap_endian_) detail::swap4(out);
        return true;
    }

    bool peek_u64(uint64_t* out) const {
        if (pos_ + 8 > size_) return false;
        std::memcpy(out, data_ + pos_, 8);
        if (swap_endian_) detail::swap8(out);
        return true;
    }

    // Check if we can read n bytes
    bool can_read(size_t n) const {
        return pos_ + n <= size_;
    }

    // Alignment
    bool align(size_t alignment) {
        size_t rem = pos_ % alignment;
        if (rem == 0) return true;
        return skip(alignment - rem);
    }

    bool swap_endian() const { return swap_endian_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    bool swap_endian_;
};

} // namespace v1
} // namespace lightusd
