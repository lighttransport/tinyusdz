// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - Crate Format Implementation

#include "crate-format.hh"
#include "../c/lightusd-core-c.h"  // lu_crate_decode_*
#include "safe-arithmetic.hh"

// Include LZ4 from existing LightUSD
#include "../../lz4/lz4.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>

namespace lightusd {
namespace next {

const char* CrateTypeIdName(CrateTypeId id) {
  return lu_crate_type_name(static_cast<uint8_t>(id));
}

size_t CrateTypeIdSize(CrateTypeId id) {
  return lu_crate_type_size(static_cast<uint8_t>(id));
}

DecompressResult DecompressLZ4(const uint8_t* src, size_t src_size,
                                size_t uncompressed_size) {
  DecompressResult result;
  result.data.resize(uncompressed_size);
  const char* error = nullptr;
  size_t out_size = 0;
  if (lu_lz4_decompress(src, src_size, result.data.data(), uncompressed_size,
                        &out_size, &error)) {
    result.data.resize(out_size);
    result.success = true;
  } else {
    result.data.clear();
    result.error = error ? error : "LZ4 decompression failed";
  }
  return result;
}

CompressResult CompressLZ4(const uint8_t* src, size_t src_size) {
  CompressResult result;
  if (!src || src_size == 0) {
    result.success = true;
    return result;
  }
  size_t bound = 0;
  if (!lu_lz4_compress_bound(src_size, &bound)) {
    result.error = "LZ4_compressBound failed";
    return result;
  }
  result.data.resize(bound);
  const char* error = nullptr;
  size_t out_size = 0;
  if (lu_lz4_compress(src, src_size, result.data.data(), bound, &out_size,
                      &error)) {
    result.data.resize(out_size);
    result.success = true;
  } else {
    result.data.clear();
    result.error = error ? error : "LZ4 compression failed";
  }
  return result;
}

// ============================================================
// TfFastCompression wrappers (pxrUSD crate LZ4 format)
// ============================================================
//
// pxrUSD wraps raw LZ4 blocks with a n_chunks prefix byte:
//   n_chunks == 0: single LZ4 block follows
//   n_chunks  > 0: n_chunks blocks, each preceded by i32 chunk_size
//
// For data < 2GB, n_chunks is always 0 (single block).

/// Maximum input size for a single LZ4 chunk (~2GB - 32MB safety margin)
static constexpr size_t kMaxLZ4ChunkSize = 0x7E000000;

CompressResult CompressCrateBlob(const uint8_t* src, size_t src_size) {
  CompressResult result;
  size_t bound = 0;
  if (!lu_crate_blob_compress_bound(src_size, &bound)) {
    result.error = "LZ4_compressBound failed";
    return result;
  }
  result.data.resize(bound);
  const char* error = nullptr;
  size_t out_size = 0;
  if (lu_crate_blob_compress(src, src_size, result.data.data(), bound, &out_size,
                             &error)) {
    result.data.resize(out_size);
    result.success = true;
  } else {
    result.data.clear();
    result.error = error ? error : "LZ4 compression failed";
  }
  return result;
}

DecompressResult DecompressCrateBlob(const uint8_t* src, size_t src_size,
                                     size_t uncompressed_size) {
  DecompressResult result;
  result.data.resize(uncompressed_size);
  const char* error = nullptr;
  size_t out_size = 0;
  if (lu_crate_blob_decompress(src, src_size, result.data.data(),
                               uncompressed_size, &out_size, &error)) {
    result.data.resize(out_size);
    result.success = true;
  } else {
    result.data.clear();
    if (error) result.error = error;
  }
  return result;
}

DecompressResult DecompressCrateBlobWithCapacityHint(
    const uint8_t* src, size_t src_size, size_t uncompressed_size_limit,
    size_t initial_capacity) {
  DecompressResult result;
  if (!src || src_size == 0) {
    result.success = (uncompressed_size_limit == 0);
    return result;
  }

  if (src_size < 1) {
    result.error = "Crate blob too small (missing n_chunks)";
    return result;
  }

  if (uncompressed_size_limit == 0) {
    result.success = true;
    return result;
  }

  if (uncompressed_size_limit > 1024 * 1024 * 1024) {
    result.error = "Uncompressed size too large";
    return result;
  }

  const uint8_t n_chunks = src[0];
  if (n_chunks != 0) {
    return DecompressCrateBlob(src, src_size, uncompressed_size_limit);
  }

  const uint8_t* data_start = src + 1;
  const size_t data_size = src_size - 1;
  if (data_size > kMaxLZ4ChunkSize) {  // see DecompressCrateBlob
    result.error = "Compressed crate blob too large";
    return result;
  }

  // Bound the doubling retry below by what LZ4 can physically produce: its
  // maximum expansion ratio is 255:1. Without this a deliberately-corrupt blob
  // (which never decodes successfully) drives the loop all the way to the 1 GiB
  // limit, costing ~2 GiB of resize+decode work PER compressed array.
  const size_t kMaxLZ4Ratio = 255;
  size_t ratio_limit = (data_size > (std::numeric_limits<size_t>::max)() /
                                        kMaxLZ4Ratio)
                           ? (std::numeric_limits<size_t>::max)()
                           : data_size * kMaxLZ4Ratio;
  if (ratio_limit < uncompressed_size_limit) {
    uncompressed_size_limit = ratio_limit;
  }
  if (uncompressed_size_limit == 0) {
    result.success = true;
    return result;
  }

  size_t capacity =
      std::min(uncompressed_size_limit,
               (std::max)(size_t(1), initial_capacity));

  for (;;) {
    result.data.resize(capacity);
    const char* decode_error = nullptr;
    size_t decoded = 0;
    if (lu_lz4_decompress_upto(data_start, data_size, result.data.data(),
                               capacity, &decoded, &decode_error)) {
      result.data.resize(decoded);
      result.success = true;
      return result;
    }

    if (capacity >= uncompressed_size_limit) {
      result.data.clear();
      result.error = "LZ4 decompression failed";
      return result;
    }
    const size_t next_capacity =
        (capacity > uncompressed_size_limit / 2)
            ? uncompressed_size_limit
            : capacity * 2;
    capacity = (std::max)(next_capacity, capacity + size_t(1));
  }
}

// USD Integer compression encoder based on pxrUSD's Usd_IntegerCompression
std::vector<uint8_t> EncodeIntegers(const uint32_t* values, size_t count) {
  std::vector<uint8_t> result;
  if (count == 0) return result;
  size_t bound = 0;
  if (!lu_crate_encode_integers_bound(count, &bound)) return result;
  result.resize(bound);
  size_t out_size = 0;
  const char* error = nullptr;
  if (lu_crate_encode_integers(values, count, result.data(), bound, &out_size,
                               &error)) {
    result.resize(out_size);
  } else {
    result.clear();
  }
  return result;
}

// USD Integer compression decoder
// Based on pxrUSD's Usd_IntegerCompression
namespace {

// Decode integers encoded with USD's integer compression
bool DecodeIntegers(const uint8_t* src, size_t src_size,
                    uint32_t* dst, size_t count) {
  return lu_crate_decode_integers(src, src_size, dst, count) != 0;
}

bool DecodeIntegers64(const uint8_t* src, size_t src_size,
                      uint64_t* dst, size_t count) {
  return lu_crate_decode_integers64(src, src_size, dst, count) != 0;
}

}  // anonymous namespace

DecompressResult DecompressIntegers(const uint8_t* src, size_t src_size,
                                     size_t num_integers, bool is_64bit) {
  DecompressResult result;

  if (num_integers == 0) {
    result.success = true;
    return result;
  }

  if (!src || src_size == 0) {
    result.error = "Empty input";
    return result;
  }

  if (is_64bit) {
    size_t nbytes;
    if (!safe::mul(num_integers, sizeof(uint64_t), &nbytes)) {
      result.error = "Integer overflow in decompressed buffer size (64-bit)";
      return result;
    }
    result.data.resize(nbytes);
    if (!DecodeIntegers64(src, src_size,
                          reinterpret_cast<uint64_t*>(result.data.data()),
                          num_integers)) {
      result.error = "Integer decompression failed";
      result.data.clear();
      return result;
    }
  } else {
    size_t nbytes;
    if (!safe::mul(num_integers, sizeof(uint32_t), &nbytes)) {
      result.error = "Integer overflow in decompressed buffer size (32-bit)";
      return result;
    }
    result.data.resize(nbytes);
    if (!DecodeIntegers(src, src_size,
                        reinterpret_cast<uint32_t*>(result.data.data()),
                        num_integers)) {
      result.error = "Integer decompression failed";
      result.data.clear();
      return result;
    }
  }

  result.success = true;
  return result;
}

// ============================================================
// pxrUSD delta-coded integer compression
// ============================================================

namespace {

bool ComputeDeltaCodeBytes(size_t count, size_t* out) {
  return lu_crate_delta_code_bytes(count, out) != 0;
}

}  // namespace

namespace {

// Most frequent delta, ties broken toward the smallest value -- exactly what a
// std::map<int32_t, count> scan in ascending key order picks (the former
// implementation, which cost O(n log k) with a node allocation per distinct
// delta: ~5 s for the ~30M-entry FIELDSETS table of a large
// crate). Short inputs sort a copy; long ones tally deltas in
// [-32768, 32767] (the overwhelmingly common case) in a flat table, scanning
// only the touched range, and the rest in a hash map.
class DeltaHistogram {
 public:
  explicit DeltaHistogram(size_t expected) : sorted_mode_(expected < 2048) {
    if (sorted_mode_) {
      few_.reserve(expected);
    } else {
      small_.assign(kSmallSpan, 0);
    }
  }
  void add(int32_t d) {
    if (sorted_mode_) {
      few_.push_back(d);
    } else if (d >= kSmallMin &&
               d < kSmallMin + static_cast<int32_t>(kSmallSpan)) {
      const size_t slot = static_cast<size_t>(d - kSmallMin);
      ++small_[slot];
      lo_ = std::min(lo_, slot);
      hi_ = std::max(hi_, slot);
    } else {
      add_large(d);
    }
  }
  int32_t most_common() {
    int32_t best = 0;
    size_t best_count = 0;
    bool have = false;
    auto consider = [&](int32_t d, size_t c) {
      if (c == 0) return;
      if (!have || c > best_count || (c == best_count && d < best)) {
        best = d;
        best_count = c;
        have = true;
      }
    };
    if (sorted_mode_) {
      std::sort(few_.begin(), few_.end());
      for (size_t i = 0; i < few_.size();) {
        size_t j = i + 1;
        while (j < few_.size() && few_[j] == few_[i]) ++j;
        consider(few_[i], j - i);
        i = j;
      }
      return best;
    }
    for (size_t i = lo_; i <= hi_ && i < kSmallSpan; ++i) {
      consider(static_cast<int32_t>(i) + kSmallMin, small_[i]);
    }
    for (const LargeSlot& e : large_) {
      if (e.key != kEmpty) consider(static_cast<int32_t>(e.key), e.count);
    }
    return best;
  }

 private:
  static constexpr int32_t kSmallMin = -32768;
  static constexpr size_t kSmallSpan = 65536;
  bool sorted_mode_;
  std::vector<int32_t> few_;
  std::vector<uint32_t> small_;
  size_t lo_ = kSmallSpan;
  size_t hi_ = 0;

  // Open-addressing (linear probe) tally for deltas outside the flat range:
  // FIELDSETS/PATHS tables have millions of distinct large jumps, where a
  // node-based map is dominated by allocation.
  static constexpr int64_t kEmpty = INT64_MIN;
  struct LargeSlot {
    int64_t key;
    size_t count;
  };
  std::vector<LargeSlot> large_;
  size_t large_used_ = 0;
  static size_t HashDelta(int32_t d) {
    uint64_t x = static_cast<uint32_t>(d);
    x *= 0x9E3779B97F4A7C15ull;
    return static_cast<size_t>(x ^ (x >> 29));
  }
  void add_large(int32_t d) {
    if ((large_used_ + 1) * 2 > large_.size()) {
      std::vector<LargeSlot> old;
      old.swap(large_);
      large_.assign(old.empty() ? 1024 : old.size() * 2, LargeSlot{kEmpty, 0});
      large_used_ = 0;
      for (const LargeSlot& e : old) {
        if (e.key != kEmpty) insert_large(static_cast<int32_t>(e.key), e.count);
      }
    }
    insert_large(d, 1);
  }
  void insert_large(int32_t d, size_t n) {
    const size_t mask = large_.size() - 1;
    for (size_t i = HashDelta(d) & mask;; i = (i + 1) & mask) {
      LargeSlot& e = large_[i];
      if (e.key == d) {
        e.count += n;
        return;
      }
      if (e.key == kEmpty) {
        e.key = d;
        e.count = n;
        ++large_used_;
        return;
      }
    }
  }
};

}  // namespace

std::vector<uint8_t> EncodeDeltaU32(const uint32_t* values, size_t count) {
  std::vector<uint8_t> result;
  if (count == 0 || !values) return result;
  size_t bound = 0;
  if (!lu_crate_encode_delta_u32_bound(count, &bound)) return result;
  result.resize(bound);
  size_t out_size = 0;
  const char* error = nullptr;
  if (lu_crate_encode_delta_u32(values, count, result.data(), bound, &out_size,
                                &error)) {
    result.resize(out_size);
  } else {
    result.clear();
  }
  return result;
}

bool DecodeDeltaU32(const uint8_t* buffer, size_t buffer_size,
                    uint32_t* dst, size_t count) {
  return lu_crate_decode_delta_u32(buffer, buffer_size, dst, count) != 0;
}

std::vector<uint8_t> EncodeDeltaS32(const int32_t* values, size_t count) {
  std::vector<uint8_t> result;
  if (count == 0 || !values) return result;
  size_t bound = 0;
  if (!lu_crate_encode_delta_s32_bound(count, &bound)) return result;
  result.resize(bound);
  size_t out_size = 0;
  const char* error = nullptr;
  if (lu_crate_encode_delta_s32(values, count, result.data(), bound, &out_size,
                                &error)) {
    result.resize(out_size);
  } else {
    result.clear();
  }
  return result;
}

bool DecodeDeltaS32(const uint8_t* buffer, size_t buffer_size,
                    int32_t* dst, size_t count) {
  return lu_crate_decode_delta_s32(buffer, buffer_size, dst, count) != 0;
}

// ============================================================
// pxrUSD compressed integer section I/O
// ============================================================

CompressResult WriteCompressedU32(const uint32_t* values, size_t count) {
  CompressResult result;

  // Delta-encode
  std::vector<uint8_t> delta_encoded = EncodeDeltaU32(values, count);

  // LZ4 compress with n_chunks prefix
  CompressResult cr = CompressCrateBlob(delta_encoded.data(), delta_encoded.size());
  if (!cr.success) {
    result.error = cr.error;
    return result;
  }

  result.data = std::move(cr.data);
  result.success = true;
  return result;
}

DecompressResult DecompressCompressedU32(const uint8_t* data, size_t data_size,
                                          uint32_t* dst, size_t count) {
  DecompressResult result;
  if (!data || data_size == 0 || count == 0) {
    result.success = true;
    return result;
  }

  if (data_size < 8) {
    result.error = "Compressed integer buffer too small for size prefix";
    return result;
  }

  // Read u64 compressed_size
  uint64_t comp_size;
  std::memcpy(&comp_size, data, 8);

  if (comp_size == 0 || comp_size > data_size - 8) {
    result.error = "Invalid compressed size in integer data";
    return result;
  }

  // Estimate max delta-encoded size.
  size_t code_bits = 0;
  size_t code_bytes = 0;
  size_t value_bytes = 0;
  size_t hint_value_bytes = 0;
  size_t initial_delta_size = 0;
  size_t max_delta_size = 0;
  if (!safe::mul(count, size_t(2), &code_bits) ||
      !safe::add(code_bits, size_t(7), &code_bits) ||
      !safe::mul(count, sizeof(int32_t), &value_bytes) ||
      !safe::mul(count, size_t(2), &hint_value_bytes) ||
      !safe::add(sizeof(int32_t), code_bits / 8, &code_bytes) ||
      !safe::add(code_bytes, hint_value_bytes, &initial_delta_size) ||
      !safe::add(code_bytes, value_bytes, &max_delta_size)) {
    result.error = "Compressed integer decoded size overflow";
    return result;
  }
  initial_delta_size = std::min(initial_delta_size, max_delta_size);

  // Decompress the n_chunks LZ4 blob
  DecompressResult dr = DecompressCrateBlobWithCapacityHint(
      data + 8, static_cast<size_t>(comp_size), max_delta_size,
      initial_delta_size);
  if (!dr.success) {
    result.error = "Failed to decompress compressed integers: " + dr.error;
    return result;
  }

  // Delta-decode
  if (!DecodeDeltaU32(dr.data.data(), dr.data.size(), dst, count)) {
    result.error = "Failed to delta-decode integers";
    return result;
  }

  result.success = true;
  return result;
}

bool DecodeDeltaU64(const uint8_t* buffer, size_t buffer_size, uint64_t* dst,
                    size_t count) {
  return lu_crate_decode_delta_u64(buffer, buffer_size, dst, count) != 0;
}

DecompressResult DecompressCompressedU64(const uint8_t* data, size_t data_size,
                                         uint64_t* dst, size_t count) {
  DecompressResult result;
  if (!data || data_size == 0 || count == 0) {
    result.success = true;
    return result;
  }

  if (data_size < 8) {
    result.error = "Compressed integer buffer too small for size prefix";
    return result;
  }

  uint64_t comp_size;
  std::memcpy(&comp_size, data, 8);

  if (comp_size == 0 || comp_size > data_size - 8) {
    result.error = "Invalid compressed size in integer data";
    return result;
  }

  // Worst-case delta-coded size: int64 common value + 2-bit codes + up to one
  // full int64 per element.
  size_t code_bits = 0;
  size_t code_bytes = 0;
  size_t value_bytes = 0;
  size_t max_delta_size = 0;
  if (!safe::mul(count, size_t(2), &code_bits) ||
      !safe::add(code_bits, size_t(7), &code_bits) ||
      !safe::mul(count, sizeof(int64_t), &value_bytes) ||
      !safe::add(sizeof(int64_t), code_bits / 8, &code_bytes) ||
      !safe::add(code_bytes, value_bytes, &max_delta_size)) {
    result.error = "Compressed 64-bit integer decoded size overflow";
    return result;
  }

  DecompressResult dr = DecompressCrateBlob(
      data + 8, static_cast<size_t>(comp_size), max_delta_size);
  if (!dr.success) {
    result.error = "Failed to decompress compressed integers: " + dr.error;
    return result;
  }

  if (!DecodeDeltaU64(dr.data.data(), dr.data.size(), dst, count)) {
    result.error = "Failed to delta-decode 64-bit integers";
    return result;
  }

  result.success = true;
  return result;
}

// ---------------------------------------------------------------------------
// Inline method bodies moved out of the header so units that include it do
// not re-parse them.
// ---------------------------------------------------------------------------
std::string CrateSection::name_str() const {
    return std::string(name, strnlen(name, 15));
  }
const CrateSection* CrateTOC::find(const char* name) const {
    for (const auto& s : sections) {
      if (strncmp(s.name, name, 15) == 0) {
        return &s;
      }
    }
    return nullptr;
  }
bool CrateVersion::is_valid() const {
    // Check minimum version.
    // Compare as int: major is uint8_t, so `major < 0` is always false and
    // trips -Wtautological-unsigned-zero-compare under -Weverything. The cast
    // keeps the check meaningful if kCrateMinVersionMajor ever rises above 0.
    if (static_cast<int>(major) < kCrateMinVersionMajor) return false;
    if (major == kCrateMinVersionMajor) {
      if (minor < kCrateMinVersionMinor) return false;
      if (minor == kCrateMinVersionMinor &&
          static_cast<int>(patch) < kCrateMinVersionPatch)
        return false;
    }
    // Check maximum version
    if (major > kCrateMaxVersionMajor) return false;
    if (major == kCrateMaxVersionMajor) {
      if (minor > kCrateMaxVersionMinor) return false;
    }
    return true;
  }
std::string CrateVersion::to_string() const {
    return UIntToStr(major) + "." + UIntToStr(minor) + "." + UIntToStr(patch);
  }
bool CrateVersion::is_pre_070() const { return major == 0 && minor < 7; }
bool TokenIndex::is_valid() const { return value != 0xFFFFFFFF; }
bool StringIndex::is_valid() const { return value != 0xFFFFFFFF; }
bool PathIndex::is_valid() const { return value != 0xFFFFFFFF; }
bool FieldsetIndex::is_valid() const { return value != 0xFFFFFFFF; }

}  // namespace next
}  // namespace lightusd
