// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fpnge.h"
#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__)
#define FORCE_INLINE_LAMBDA
#define FORCE_INLINE __forceinline
#define __SSE4_1__ 1
#define __PCLMUL__ 1
#ifdef __AVX2__
#define __BMI2__ 1
#endif
#else
#define FORCE_INLINE_LAMBDA __attribute__((always_inline))
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

#if defined(__x86_64__) || defined(__amd64__) || defined(__LP64) ||            \
    defined(_M_X64) || defined(_M_AMD64) ||                                    \
    (defined(_WIN64) && !defined(_M_ARM64))
#define PLATFORM_AMD64 1
#endif

#if !defined(FPNGE_USE_PEXT)
#if defined(__BMI2__) && defined(PLATFORM_AMD64) &&                            \
    !defined(__tune_znver1__) && !defined(__tune_znver2__) &&                  \
    !defined(__tune_bdver4__)
#define FPNGE_USE_PEXT 1
#else
#define FPNGE_USE_PEXT 0
#endif
#endif

#ifdef __AVX2__
#include <immintrin.h>
#define MM(f) _mm256_##f
#define MMSI(f) _mm256_##f##_si256
#define MIVEC __m256i
#define BCAST128 _mm256_broadcastsi128_si256
// workaround for compilers not supporting _mm256_zextsi128_si256
#if (defined(__clang__) && __clang_major__ >= 5 &&                             \
     (!defined(__APPLE__) || __clang_major__ >= 7)) ||                         \
    (defined(__GNUC__) && __GNUC__ >= 10) ||                                   \
    (defined(_MSC_VER) && _MSC_VER >= 1910)
#define INT2VEC(v) _mm256_zextsi128_si256(_mm_cvtsi32_si128(v))
#elif defined(__OPTIMIZE__)
// technically incorrect, but should work fine most of the time
#define INT2VEC(v) _mm256_castsi128_si256(_mm_cvtsi32_si128(v))
#else
// _mm256_insert_epi32 is unavailable on MSVC 19.0, so prefer the following
#define INT2VEC(v)                                                             \
  _mm256_inserti128_si256(_mm256_setzero_si256(), _mm_cvtsi32_si128(v), 0);
#endif
#define SIMD_WIDTH 32
#define SIMD_MASK 0xffffffffU
#elif defined(__SSE4_1__)
#include <nmmintrin.h>
#define MM(f) _mm_##f
#define MMSI(f) _mm_##f##_si128
#define MIVEC __m128i
#define BCAST128(v) (v)
#define INT2VEC _mm_cvtsi32_si128
#define SIMD_WIDTH 16
#define SIMD_MASK 0xffffU
#else
#error Requires SSE4.1 support minium
#endif

namespace {

alignas(16) constexpr uint8_t kBitReverseNibbleLookup[16] = {
    0b0000, 0b1000, 0b0100, 0b1100, 0b0010, 0b1010, 0b0110, 0b1110,
    0b0001, 0b1001, 0b0101, 0b1101, 0b0011, 0b1011, 0b0111, 0b1111,
};

static constexpr uint8_t kLZ77NBits[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                           1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                           4, 4, 4, 4, 5, 5, 5, 5, 0};

static constexpr uint16_t kLZ77Base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

static uint16_t BitReverse(size_t nbits, uint16_t bits) {
  uint16_t rev16 = (kBitReverseNibbleLookup[bits & 0xF] << 12) |
                   (kBitReverseNibbleLookup[(bits >> 4) & 0xF] << 8) |
                   (kBitReverseNibbleLookup[(bits >> 8) & 0xF] << 4) |
                   (kBitReverseNibbleLookup[bits >> 12]);
  return rev16 >> (16 - nbits);
}

struct HuffmanTable {
  uint8_t nbits[286];
  uint16_t end_bits;

  alignas(16) uint8_t approx_nbits[16];

  alignas(16) uint8_t first16_nbits[16];
  alignas(16) uint8_t first16_bits[16];

  alignas(16) uint8_t last16_nbits[16];
  alignas(16) uint8_t last16_bits[16];

  alignas(16) uint8_t mid_lowbits[16];
  uint8_t mid_nbits;

  uint32_t lz77_length_nbits[259] = {};
  uint32_t lz77_length_bits[259] = {};
  uint32_t lz77_length_sym[259] = {};

  uint32_t dist_nbits, dist_bits;

  // Computes nbits[i] for i <= n, subject to min_limit[i] <= nbits[i] <=
  // max_limit[i], so to minimize sum(nbits[i] * freqs[i]).
  static void ComputeCodeLengths(const uint64_t *freqs, size_t n,
                                 uint8_t *min_limit, uint8_t *max_limit,
                                 uint8_t *nbits) {
    size_t precision = 0;
    uint64_t freqsum = 0;
    for (size_t i = 0; i < n; i++) {
      assert(freqs[i] != 0);
      freqsum += freqs[i];
      if (min_limit[i] < 1)
        min_limit[i] = 1;
      assert(min_limit[i] <= max_limit[i]);
      precision = std::max<size_t>(max_limit[i], precision);
    }
    uint64_t infty = freqsum * precision;
    std::vector<uint64_t> dynp(((1U << precision) + 1) * (n + 1), infty);
    auto d = [&](size_t sym, size_t off) -> uint64_t & {
      return dynp[sym * ((1 << precision) + 1) + off];
    };
    d(0, 0) = 0;
    for (size_t sym = 0; sym < n; sym++) {
      for (size_t bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
        size_t off_delta = 1U << (precision - bits);
        for (size_t off = 0; off + off_delta <= (1U << precision); off++) {
          d(sym + 1, off + off_delta) = std::min(
              d(sym, off) + freqs[sym] * bits, d(sym + 1, off + off_delta));
        }
      }
    }

    size_t sym = n;
    size_t off = 1U << precision;

    while (sym-- > 0) {
      assert(off > 0);
      for (size_t bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
        size_t off_delta = 1U << (precision - bits);
        if (off_delta <= off &&
            d(sym + 1, off) == d(sym, off - off_delta) + freqs[sym] * bits) {
          off -= off_delta;
          nbits[sym] = bits;
          break;
        }
      }
    }
  }

  void ComputeNBits(const uint64_t *collected_data) {
    constexpr uint64_t kBaselineData[286] = {
        113, 54, 28, 18, 12, 9, 7, 6, 5, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 4, 5, 6, 7, 9,
        12,  18, 29, 54, 1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1,
    };

    uint64_t data[286];

    for (size_t i = 0; i < 286; i++) {
      data[i] = collected_data[i] + kBaselineData[i];
    }

    // Compute Huffman code length ensuring that all the "fake" symbols for [16,
    // 240) and [255, 285) have their maximum length.
    uint64_t collapsed_data[16 + 14 + 16 + 2] = {};
    uint8_t collapsed_min_limit[16 + 14 + 16 + 2] = {};
    uint8_t collapsed_max_limit[16 + 14 + 16 + 2];
    for (size_t i = 0; i < 48; i++) {
      collapsed_max_limit[i] = 8;
    }
    for (size_t i = 0; i < 16; i++) {
      collapsed_data[i] = data[i];
    }
    for (size_t i = 0; i < 14; i++) {
      collapsed_data[16 + i] = 1;
      collapsed_min_limit[16 + i] = 8;
    }
    for (size_t j = 0; j < 16; j++) {
      collapsed_data[16 + 14 + j] += data[240 + j];
    }
    collapsed_data[16 + 14 + 16] = 1;
    collapsed_min_limit[16 + 14 + 16] = 8;
    collapsed_data[16 + 14 + 16 + 1] = data[285];

    uint8_t collapsed_nbits[48] = {};
    ComputeCodeLengths(collapsed_data, 48, collapsed_min_limit,
                       collapsed_max_limit, collapsed_nbits);

    // Compute "extra" code lengths for symbols >= 256, except 285.
    uint8_t tail_nbits[29] = {};
    uint8_t tail_min_limit[29] = {};
    uint8_t tail_max_limit[29] = {};
    for (size_t i = 0; i < 29; i++) {
      tail_min_limit[i] = 4;
      tail_max_limit[i] = 7;
    }
    ComputeCodeLengths(data + 256, 29, tail_min_limit, tail_max_limit,
                       tail_nbits);

    for (size_t i = 0; i < 16; i++) {
      nbits[i] = collapsed_nbits[i];
    }
    for (size_t i = 0; i < 14; i++) {
      for (size_t j = 0; j < 16; j++) {
        nbits[(i + 1) * 16 + j] = collapsed_nbits[16 + i] + 4;
      }
    }
    for (size_t i = 0; i < 16; i++) {
      nbits[240 + i] = collapsed_nbits[30 + i];
    }
    for (size_t i = 0; i < 29; i++) {
      nbits[256 + i] = collapsed_nbits[46] + tail_nbits[i];
    }
    nbits[285] = collapsed_nbits[47];
  }

  void ComputeCanonicalCode(const uint8_t *nbits, uint16_t *bits) {
    uint8_t code_length_counts[16] = {};
    for (size_t i = 0; i < 286; i++) {
      code_length_counts[nbits[i]]++;
    }
    uint16_t next_code[16] = {};
    uint16_t code = 0;
    for (size_t i = 1; i < 16; i++) {
      code = (code + code_length_counts[i - 1]) << 1;
      next_code[i] = code;
    }
    for (size_t i = 0; i < 286; i++) {
      bits[i] = BitReverse(nbits[i], next_code[nbits[i]]++);
    }
  }

  void FillNBits() {
    for (size_t i = 0; i < 16; i++) {
      first16_nbits[i] = nbits[i];
      last16_nbits[i] = nbits[240 + i];
    }
    mid_nbits = nbits[16];
    for (size_t i = 16; i < 240; i++) {
      assert(nbits[i] == mid_nbits);
    }
    // Construct lz77 lookup tables.
    for (size_t i = 0; i < 29; i++) {
      for (size_t j = 0; j < (1U << kLZ77NBits[i]); j++) {
        lz77_length_nbits[kLZ77Base[i] + j] = nbits[257 + i] + kLZ77NBits[i];
        lz77_length_sym[kLZ77Base[i] + j] = 257 + i;
      }
    }

    dist_nbits = 1;

    approx_nbits[0] =
        nbits[0] - 1; // subtract 1 as a fudge for catering for RLE
    for (size_t i = 1; i < 15; i++) {
      approx_nbits[i] = (nbits[i] + nbits[256 - i] + 1) / 2;
    }
    approx_nbits[15] = mid_nbits;
  }

  void FillBits() {
    uint16_t bits[286];
    ComputeCanonicalCode(nbits, bits);
    for (size_t i = 0; i < 16; i++) {
      first16_bits[i] = bits[i];
      last16_bits[i] = bits[240 + i];
    }
    mid_lowbits[0] = mid_lowbits[15] = 0;
    for (size_t i = 16; i < 240; i += 16) {
      mid_lowbits[i / 16] = bits[i] & ((1U << (mid_nbits - 4)) - 1);
    }
    for (size_t i = 16; i < 240; i++) {
      assert((uint32_t(mid_lowbits[i / 16]) |
              (kBitReverseNibbleLookup[i % 16] << (mid_nbits - 4))) == bits[i]);
    }
    end_bits = bits[256];
    // Construct lz77 lookup tables.
    for (size_t i = 0; i < 29; i++) {
      for (size_t j = 0; j < (1U << kLZ77NBits[i]); j++) {
        lz77_length_bits[kLZ77Base[i] + j] =
            bits[257 + i] | (j << nbits[257 + i]);
      }
    }

    dist_bits = 0;
  }

  HuffmanTable(const uint64_t *collected_data) {
    ComputeNBits(collected_data);
    FillNBits();
    FillBits();
  }

  // estimate for CollectSymbolCounts
  // only fills nbits; skips computing actual codes
  HuffmanTable() {
    // the following is similar to ComputeNBits(0, 0, 0 ...), but much faster
    constexpr uint8_t collapsed_nbits[] = {
        2,  3,  4,  5,  5,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,
        8,  8,  8,  8,  8,  7,  7,  7,  7,  6,  6,  6,  5,  5,  4,  3,

        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 8};
    for (size_t i = 0; i < 16; i++) {
      nbits[i] = collapsed_nbits[i];
      nbits[240 + i] = collapsed_nbits[16 + i];
    }
    for (size_t i = 16; i < 240; i++) {
      nbits[i] = 12;
    }
    for (size_t i = 0; i < 30; i++) {
      nbits[256 + i] = collapsed_nbits[32 + i];
    }

    FillNBits();
  }
};

struct BitWriter {
  void Write(uint32_t count, uint64_t bits) {
    buffer |= bits << bits_in_buffer;
    bits_in_buffer += count;
    memcpy(data + bytes_written, &buffer, 8);
    size_t bytes_in_buffer = bits_in_buffer / 8;
    bits_in_buffer &= 7;
    buffer >>= bytes_in_buffer * 8;
    bytes_written += bytes_in_buffer;
  }

  void ZeroPadToByte() {
    if (bits_in_buffer != 0) {
      Write(8 - bits_in_buffer, 0);
    }
  }

  void WriteBytes(const char *data, size_t count) {
    memcpy(this->data + bytes_written, data, count);
    bytes_written += count;
  }

  unsigned char *data;
  size_t bytes_written = 0;
  size_t bits_in_buffer = 0;
  uint64_t buffer = 0;
};

static void WriteHuffmanCode(const HuffmanTable &table,
                             BitWriter *__restrict writer) {
  constexpr uint8_t kCodeLengthNbits[] = {
      4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0,
  };
  constexpr uint8_t kCodeLengthOrder[] = {
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
  };
  writer->Write(5, 29); // all lit/len codes
  writer->Write(5, 0);  // distance code up to dist, included
  writer->Write(4, 15); // all code length codes
  for (size_t i = 0; i < 19; i++) {
    writer->Write(3, kCodeLengthNbits[kCodeLengthOrder[i]]);
  }

  for (size_t i = 0; i < 286; i++) {
    writer->Write(4, kBitReverseNibbleLookup[table.nbits[i]]);
  }
  writer->Write(4, 0b1000);
}

#ifdef __PCLMUL__
} // namespace
#include <wmmintrin.h>
namespace {
alignas(32) static const uint8_t pshufb_shf_table[] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
    0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

class Crc32 {
  __m128i x0, x1, x2, x3;

  static inline __m128i double_xor(__m128i a, __m128i b, __m128i c) {
#ifdef __AVX512VL__
    return _mm_ternarylogic_epi32(a, b, c, 0x96);
#else
    return _mm_xor_si128(_mm_xor_si128(a, b), c);
#endif
  }
  static inline __m128i do_one_fold(__m128i src, __m128i data) {
    const auto k1k2 = _mm_set_epi32(1, 0x54442bd4, 1, 0xc6e41596);
    return double_xor(_mm_clmulepi64_si128(src, k1k2, 0x01),
                      _mm_clmulepi64_si128(src, k1k2, 0x10), data);
  }

public:
  Crc32() {
    x0 = _mm_cvtsi32_si128(0x9db42487);
    x1 = _mm_setzero_si128();
    x2 = _mm_setzero_si128();
    x3 = _mm_setzero_si128();
  }
  size_t update(const unsigned char *__restrict data, size_t len) {
    auto amount = len & ~63;
    for (size_t i = 0; i < amount; i += 64) {
      x0 = do_one_fold(x0, _mm_loadu_si128((__m128i *)(data + i)));
      x1 = do_one_fold(x1, _mm_loadu_si128((__m128i *)(data + i + 0x10)));
      x2 = do_one_fold(x2, _mm_loadu_si128((__m128i *)(data + i + 0x20)));
      x3 = do_one_fold(x3, _mm_loadu_si128((__m128i *)(data + i + 0x30)));
    }
    return amount;
  }
  uint32_t update_final(const unsigned char *__restrict data, size_t len) {
    if (len >= 64) {
      update(data, len);
      data += len & ~63;
      len &= 63;
    }

    if (len >= 48) {
      auto t3 = x3;
      x3 = do_one_fold(x2, _mm_loadu_si128((__m128i *)data + 2));
      x2 = do_one_fold(x1, _mm_loadu_si128((__m128i *)data + 1));
      x1 = do_one_fold(x0, _mm_loadu_si128((__m128i *)data));
      x0 = t3;
    } else if (len >= 32) {
      auto t2 = x2;
      auto t3 = x3;
      x3 = do_one_fold(x1, _mm_loadu_si128((__m128i *)data + 1));
      x2 = do_one_fold(x0, _mm_loadu_si128((__m128i *)data));
      x1 = t3;
      x0 = t2;
    } else if (len >= 16) {
      auto t3 = x3;
      x3 = do_one_fold(x0, _mm_loadu_si128((__m128i *)data));
      x0 = x1;
      x1 = x2;
      x2 = t3;
    }
    data += len & 48;
    len &= 15;

    if (len > 0) {
      auto xmm_shl = _mm_loadu_si128((__m128i *)(pshufb_shf_table + len));
      auto xmm_shr = _mm_xor_si128(xmm_shl, _mm_set1_epi8(-128));

      auto t0 = _mm_loadu_si128((__m128i *)data);
      auto t1 = _mm_shuffle_epi8(x0, xmm_shl);

      x0 = _mm_or_si128(_mm_shuffle_epi8(x0, xmm_shr),
                        _mm_shuffle_epi8(x1, xmm_shl));
      x1 = _mm_or_si128(_mm_shuffle_epi8(x1, xmm_shr),
                        _mm_shuffle_epi8(x2, xmm_shl));
      x2 = _mm_or_si128(_mm_shuffle_epi8(x2, xmm_shr),
                        _mm_shuffle_epi8(x3, xmm_shl));
      x3 = _mm_or_si128(_mm_shuffle_epi8(x3, xmm_shr),
                        _mm_shuffle_epi8(t0, xmm_shl));

      x3 = do_one_fold(t1, x3);
    }

    const auto k3k4 = _mm_set_epi32(1, 0x751997d0, 0, 0xccaa009e);
    const auto k5k4 = _mm_set_epi32(1, 0x63cd6124, 0, 0xccaa009e);
    const auto poly = _mm_set_epi32(1, 0xdb710640, 0, 0xf7011641);

    x0 = double_xor(x1, _mm_clmulepi64_si128(x0, k3k4, 0x10),
                    _mm_clmulepi64_si128(x0, k3k4, 0x01));
    x0 = double_xor(x2, _mm_clmulepi64_si128(x0, k3k4, 0x10),
                    _mm_clmulepi64_si128(x0, k3k4, 0x01));
    x0 = double_xor(x3, _mm_clmulepi64_si128(x0, k3k4, 0x10),
                    _mm_clmulepi64_si128(x0, k3k4, 0x01));

    x1 =
        _mm_xor_si128(_mm_clmulepi64_si128(x0, k5k4, 0), _mm_srli_si128(x0, 8));

    x0 = _mm_slli_si128(x1, 4);
    x0 = _mm_clmulepi64_si128(x0, k5k4, 0x10);
#ifdef __AVX512VL__
    x0 = _mm_ternarylogic_epi32(x0, x1, _mm_set_epi32(0, -1, -1, 0), 0x28);
#else
    x1 = _mm_and_si128(x1, _mm_set_epi32(0, -1, -1, 0));
    x0 = _mm_xor_si128(x0, x1);
#endif

    x1 = _mm_clmulepi64_si128(x0, poly, 0);
    x1 = _mm_clmulepi64_si128(x1, poly, 0x10);
#ifdef __AVX512VL__
    x1 = _mm_ternarylogic_epi32(x1, x0, x0, 0xC3); // NOT(XOR(x1, x0))
#else
    x0 = _mm_xor_si128(x0, _mm_set_epi32(0, -1, -1, 0));
    x1 = _mm_xor_si128(x1, x0);
#endif
    return _mm_extract_epi32(x1, 2);
  }
};
#else
} // namespace
#include <array>
#include <cstddef>
#include <utility>
namespace {
// from https://joelfilho.com/blog/2020/compile_time_lookup_tables_in_cpp/
template <std::size_t Length, typename Generator, std::size_t... Indexes>
constexpr auto lut_impl(Generator &&f, std::index_sequence<Indexes...>) {
  using content_type = decltype(f(std::size_t{0}));
  return std::array<content_type, Length>{{f(Indexes)...}};
}
template <std::size_t Length, typename Generator>
constexpr auto lut(Generator &&f) {
  return lut_impl<Length>(std::forward<Generator>(f),
                          std::make_index_sequence<Length>{});
}

constexpr uint32_t crc32_slice8_gen(unsigned n) {
  uint32_t crc = n & 0xff;
  for (int i = n >> 8; i >= 0; i--) {
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^
            ((crc & 1) * 0xEDB88320); // 0xEDB88320 = CRC32 polynomial
    }
  }
  return crc;
}
static constexpr auto kCrcSlice8LUT = lut<256 * 8>(crc32_slice8_gen);

class Crc32 {
  uint32_t state;

  // this is based off Fast CRC32 slice-by-8:
  // https://create.stephan-brumme.com/crc32/
  static inline uint32_t crc_process_iter(uint32_t crc,
                                          const uint32_t *current) {
    uint32_t one = *current++ ^ crc;
    uint32_t two = *current;
    return kCrcSlice8LUT[(two >> 24) & 0xFF] ^
           kCrcSlice8LUT[0x100 + ((two >> 16) & 0xFF)] ^
           kCrcSlice8LUT[0x200 + ((two >> 8) & 0xFF)] ^
           kCrcSlice8LUT[0x300 + (two & 0xFF)] ^
           kCrcSlice8LUT[0x400 + ((one >> 24) & 0xFF)] ^
           kCrcSlice8LUT[0x500 + ((one >> 16) & 0xFF)] ^
           kCrcSlice8LUT[0x600 + ((one >> 8) & 0xFF)] ^
           kCrcSlice8LUT[0x700 + (one & 0xFF)];
  }

public:
  Crc32() : state(0xffffffff) {}
  size_t update(const unsigned char *__restrict data, size_t len) {
    auto amount = len & ~7;
    for (size_t i = 0; i < amount; i += 8) {
      state = crc_process_iter(state, (uint32_t *)(data + i));
    }
    return amount;
  }
  uint32_t update_final(const unsigned char *__restrict data, size_t len) {
    auto i = update(data, len);
    for (; i < len; i++) {
      state = (state >> 8) ^ kCrcSlice8LUT[(state & 0xFF) ^ data[i]];
    }
    return ~state;
  }
};

#endif

constexpr unsigned kAdler32Mod = 65521;

static void UpdateAdler32(uint32_t &s1, uint32_t &s2, uint8_t byte) {
  s1 += byte;
  s2 += s1;
  s1 %= kAdler32Mod;
  s2 %= kAdler32Mod;
}

static uint32_t hadd(MIVEC v) {
  auto sum =
#ifdef __AVX2__
      _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
#else
      v;
#endif
  sum = _mm_hadd_epi32(sum, sum);
  sum = _mm_hadd_epi32(sum, sum);
  return _mm_cvtsi128_si32(sum);
}

template <size_t predictor>
static FORCE_INLINE MIVEC PredictVec(const unsigned char *current_buf,
                                     const unsigned char *top_buf,
                                     const unsigned char *left_buf,
                                     const unsigned char *topleft_buf) {
  auto data = MMSI(load)((MIVEC *)(current_buf));
  if (predictor == 0) {
    return data;
  } else if (predictor == 1) {
    auto pred = MMSI(loadu)((MIVEC *)(left_buf));
    return MM(sub_epi8)(data, pred);
  } else if (predictor == 2) {
    auto pred = MMSI(load)((MIVEC *)(top_buf));
    return MM(sub_epi8)(data, pred);
  } else if (predictor == 3) {
    auto left = MMSI(loadu)((MIVEC *)(left_buf));
    auto top = MMSI(load)((MIVEC *)(top_buf));
    auto pred = MM(sub_epi8)(MM(add_epi8)(top, left), MM(avg_epu8)(top, left));
    return MM(sub_epi8)(data, pred);
  } else {
    auto left = MMSI(loadu)((MIVEC *)(left_buf));
    auto top = MMSI(load)((MIVEC *)(top_buf));
    auto c = MMSI(loadu)((MIVEC *)(topleft_buf));

    auto a = MM(min_epu8)(left, top);
    auto b = MM(max_epu8)(left, top);

    auto pa = MM(subs_epu8)(b, c);
    auto pb = MM(subs_epu8)(c, a);

    auto min_pab = MM(min_epu8)(pa, pb);
    auto pc = MM(sub_epi8)(MM(max_epu8)(pa, pb), min_pab);

    auto min_pabc = MM(min_epu8)(min_pab, pc);
    auto use_a = MM(cmpeq_epi8)(min_pabc, pa);
    auto use_b = MM(cmpeq_epi8)(min_pabc, pb);

    auto pred = MM(blendv_epi8)(MM(blendv_epi8)(c, b, use_b), a, use_a);
    return MM(sub_epi8)(data, pred);
  }
}

alignas(SIMD_WIDTH) constexpr int32_t _kMaskVec[] = {0,  0,  0,  0,
#if SIMD_WIDTH == 32
                                                     0,  0,  0,  0,
                                                     -1, -1, -1, -1,
#endif
                                                     -1, -1, -1, -1};
static const uint8_t *kMaskVec =
    reinterpret_cast<const uint8_t *>(_kMaskVec) + SIMD_WIDTH;

template <size_t predictor, typename CB, typename CB_ADL, typename CB_RLE>
static void
ProcessRow(size_t bytes_per_line, const unsigned char *current_row_buf,
           const unsigned char *top_buf, const unsigned char *left_buf,
           const unsigned char *topleft_buf, CB &&cb, CB_ADL &&cb_adl,
           CB_RLE &&cb_rle) {
  size_t run = 0;
  size_t i = 0;
  for (; i + SIMD_WIDTH <= bytes_per_line; i += SIMD_WIDTH) {
    auto pdata = PredictVec<predictor>(current_row_buf + i, top_buf + i,
                                       left_buf + i, topleft_buf + i);
    unsigned pdatais0 =
        MM(movemask_epi8)(MM(cmpeq_epi8)(pdata, MMSI(setzero)()));
    if (pdatais0 == SIMD_MASK) {
      run += SIMD_WIDTH;
    } else {
      if (run != 0) {
        cb_rle(run);
      }
      run = 0;
      cb(pdata, SIMD_WIDTH);
    }
    cb_adl(pdata, SIMD_WIDTH, i);
  }
  size_t bytes_remaining =
      bytes_per_line ^ i; // equivalent to `bytes_per_line - i`
  if (bytes_remaining) {
    auto pdata = PredictVec<predictor>(current_row_buf + i, top_buf + i,
                                       left_buf + i, topleft_buf + i);
    unsigned pdatais0 =
        MM(movemask_epi8)(MM(cmpeq_epi8)(pdata, MMSI(setzero)()));
    auto mask = (1UL << bytes_remaining) - 1;

    if ((pdatais0 & mask) == mask && run + bytes_remaining >= 16) {
      run += bytes_remaining;
    } else {
      if (run != 0) {
        cb_rle(run);
      }
      run = 0;
      cb(pdata, bytes_remaining);
    }
    cb_adl(pdata, bytes_remaining, i);
  }
  if (run != 0) {
    cb_rle(run);
  }
}

template <typename CB, typename CB_ADL, typename CB_RLE>
static void
ProcessRow(uint8_t predictor, size_t bytes_per_line,
           const unsigned char *current_row_buf, const unsigned char *top_buf,
           const unsigned char *left_buf, const unsigned char *topleft_buf,
           CB &&cb, CB_ADL &&cb_adl, CB_RLE &&cb_rle) {
  if (predictor == 1) {
    ProcessRow<1>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else if (predictor == 2) {
    ProcessRow<2>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else if (predictor == 3) {
    ProcessRow<3>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else if (predictor == 4) {
    ProcessRow<4>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else {
    assert(predictor == 0);
    ProcessRow<0>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  }
}

template <typename CB> static void ForAllRLESymbols(size_t length, CB &&cb) {
  assert(length >= 4);
  length -= 1;

  if (length < 258) {
    // fast path if long sequences are rare in the image
    cb(length, 1);
  } else {
    auto runs = length / 258;
    auto remain = length % 258;
    if (remain == 1 || remain == 2) {
      remain += 258 - 3;
      runs--;
      cb(3, 1);
    }
    if (runs) {
      cb(258, runs);
    }
    if (remain) {
      cb(remain, 1);
    }
  }
}

template <size_t pred, bool store_pred>
static void
TryPredictor(size_t bytes_per_line, const unsigned char *current_row_buf,
             const unsigned char *top_buf, const unsigned char *left_buf,
             const unsigned char *topleft_buf, unsigned char *predicted_data,
             const HuffmanTable &table, size_t &best_cost, uint8_t &predictor) {
  size_t cost_rle = 0;
  MIVEC cost_direct = MMSI(setzero)();
  auto cost_chunk_cb = [&](const MIVEC bytes,
                           const size_t bytes_in_vec) FORCE_INLINE_LAMBDA {
    auto data_for_lut = MMSI(and)(MM(set1_epi8)(0xF), bytes);
    // get a mask of `bytes` that are between -16 and 15 inclusive
    // (`-16 <= bytes <= 15` is equivalent to `bytes + 112 > 95`)
    auto use_lowhi = MM(cmpgt_epi8)(MM(add_epi8)(bytes, MM(set1_epi8)(112)),
                                    MM(set1_epi8)(95));

    auto nbits_low16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.first16_nbits)), data_for_lut);
    auto nbits_hi16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.last16_nbits)), data_for_lut);

    auto nbits = MM(blendv_epi8)(nbits_low16, nbits_hi16, bytes);
    nbits = MM(blendv_epi8)(MM(set1_epi8)(table.mid_nbits), nbits, use_lowhi);

    auto nbits_discard =
        MMSI(and)(nbits, MMSI(loadu)((MIVEC *)(kMaskVec - bytes_in_vec)));

    cost_direct =
        MM(add_epi32)(cost_direct, MM(sad_epu8)(nbits, nbits_discard));
  };
  auto rle_cost_cb = [&](size_t run) {
    cost_rle += table.first16_nbits[0];
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      cost_rle += (table.dist_nbits + table.lz77_length_nbits[len]) * count;
    });
  };
  if (store_pred) {
    ProcessRow<pred>(
        bytes_per_line, current_row_buf, top_buf, left_buf, topleft_buf,
        cost_chunk_cb,
        [=](const MIVEC pdata, size_t, size_t i) {
          MMSI(store)((MIVEC *)(predicted_data + i), pdata);
        },
        rle_cost_cb);
  } else {
    ProcessRow<pred>(
        bytes_per_line, current_row_buf, top_buf, left_buf, topleft_buf,
        cost_chunk_cb, [](const MIVEC, size_t, size_t) {}, rle_cost_cb);
  }
  size_t cost = cost_rle + hadd(cost_direct);
  if (cost < best_cost) {
    best_cost = cost;
    predictor = pred;
  }
}

static FORCE_INLINE void WriteBitsLong(MIVEC nbits, MIVEC bits_lo,
                                       MIVEC bits_hi, size_t mid_lo_nbits,
                                       BitWriter *__restrict writer) {

  // Merge bits_lo and bits_hi in 16-bit "bits".
#if FPNGE_USE_PEXT
  auto bits0 = MM(unpacklo_epi8)(bits_lo, bits_hi);
  auto bits1 = MM(unpackhi_epi8)(bits_lo, bits_hi);

  // convert nbits into a mask
  auto nbits_hi = MM(sub_epi8)(nbits, MM(set1_epi8)(mid_lo_nbits));
  auto nbits0 = MM(unpacklo_epi8)(nbits, nbits_hi);
  auto nbits1 = MM(unpackhi_epi8)(nbits, nbits_hi);
  const auto nbits_to_mask =
      BCAST128(_mm_set_epi32(0xffffffff, 0xffffffff, 0x7f3f1f0f, 0x07030100));
  auto bitmask0 = MM(shuffle_epi8)(nbits_to_mask, nbits0);
  auto bitmask1 = MM(shuffle_epi8)(nbits_to_mask, nbits1);

  // aggregate nbits
  alignas(16) uint16_t nbits_a[SIMD_WIDTH / 4];
  auto bit_count = MM(maddubs_epi16)(nbits, MM(set1_epi8)(1));
#ifdef __AVX2__
  auto bit_count2 = _mm_hadd_epi16(_mm256_castsi256_si128(bit_count),
                                   _mm256_extracti128_si256(bit_count, 1));
  _mm_store_si128((__m128i *)nbits_a, bit_count2);
#else
  bit_count = _mm_hadd_epi16(bit_count, bit_count);
  _mm_storel_epi64((__m128i *)nbits_a, bit_count);
#endif

  alignas(SIMD_WIDTH) uint64_t bitmask_a[SIMD_WIDTH / 4];
  MMSI(store)((MIVEC *)bitmask_a, bitmask0);
  MMSI(store)((MIVEC *)bitmask_a + 1, bitmask1);

#else

  auto nbits0 = MM(unpacklo_epi8)(nbits, MMSI(setzero)());
  auto nbits1 = MM(unpackhi_epi8)(nbits, MMSI(setzero)());
  MIVEC bits0, bits1;
  if (mid_lo_nbits == 8) {
    bits0 = MM(unpacklo_epi8)(bits_lo, bits_hi);
    bits1 = MM(unpackhi_epi8)(bits_lo, bits_hi);
  } else {
    auto nbits_shift = _mm_cvtsi32_si128(8 - mid_lo_nbits);
    auto bits_lo_shifted = MM(sll_epi16)(bits_lo, nbits_shift);
    bits0 = MM(unpacklo_epi8)(bits_lo_shifted, bits_hi);
    bits1 = MM(unpackhi_epi8)(bits_lo_shifted, bits_hi);

    bits0 = MM(srl_epi16)(bits0, nbits_shift);
    bits1 = MM(srl_epi16)(bits1, nbits_shift);
  }

  // 16 -> 32
  auto nbits0_32_lo = MMSI(and)(nbits0, MM(set1_epi32)(0xFFFF));
  auto nbits1_32_lo = MMSI(and)(nbits1, MM(set1_epi32)(0xFFFF));

  auto bits0_32_lo = MMSI(and)(bits0, MM(set1_epi32)(0xFFFF));
  auto bits1_32_lo = MMSI(and)(bits1, MM(set1_epi32)(0xFFFF));
#ifdef __AVX2__
  auto bits0_32_hi = MM(sllv_epi32)(MM(srli_epi32)(bits0, 16), nbits0_32_lo);
  auto bits1_32_hi = MM(sllv_epi32)(MM(srli_epi32)(bits1, 16), nbits1_32_lo);
#else
  // emulate variable shift by abusing float exponents
  // this works because Huffman symbols are not allowed to exceed 15 bits, so
  // will fit within a float's mantissa and (number << 15) won't overflow when
  // converted back to a signed int
  auto bits0_32_hi =
      _mm_castps_si128(MM(cvtepi32_ps)(MM(srli_epi32)(bits0, 16)));
  auto bits1_32_hi =
      _mm_castps_si128(MM(cvtepi32_ps)(MM(srli_epi32)(bits1, 16)));

  // add shift amount to the exponent
  bits0_32_hi = MM(add_epi32)(bits0_32_hi, MM(slli_epi32)(nbits0_32_lo, 23));
  bits1_32_hi = MM(add_epi32)(bits1_32_hi, MM(slli_epi32)(nbits1_32_lo, 23));

  bits0_32_hi = MM(cvtps_epi32)(_mm_castsi128_ps(bits0_32_hi));
  bits1_32_hi = MM(cvtps_epi32)(_mm_castsi128_ps(bits1_32_hi));
#endif

  nbits0 = MM(madd_epi16)(nbits0, MM(set1_epi16)(1));
  nbits1 = MM(madd_epi16)(nbits1, MM(set1_epi16)(1));
  auto bits0_32 = MMSI(or)(bits0_32_lo, bits0_32_hi);
  auto bits1_32 = MMSI(or)(bits1_32_lo, bits1_32_hi);

  // 32 -> 64
#ifdef __AVX2__
  auto nbits_inv0_64_lo = MM(subs_epu8)(MM(set1_epi64x)(32), nbits0);
  auto nbits_inv1_64_lo = MM(subs_epu8)(MM(set1_epi64x)(32), nbits1);
  bits0 = MM(sllv_epi32)(bits0_32, nbits_inv0_64_lo);
  bits1 = MM(sllv_epi32)(bits1_32, nbits_inv1_64_lo);
  bits0 = MM(srlv_epi64)(bits0, nbits_inv0_64_lo);
  bits1 = MM(srlv_epi64)(bits1, nbits_inv1_64_lo);
#else
  auto nbits0_64_lo = MMSI(and)(nbits0, MM(set1_epi64x)(0xFFFFFFFF));
  auto nbits1_64_lo = MMSI(and)(nbits1, MM(set1_epi64x)(0xFFFFFFFF));
  // just do two shifts for SSE variant
  auto bits0_64_lo = MMSI(and)(bits0_32, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits1_64_lo = MMSI(and)(bits1_32, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits0_64_hi = MM(srli_epi64)(bits0_32, 32);
  auto bits1_64_hi = MM(srli_epi64)(bits1_32, 32);

  bits0_64_hi = _mm_blend_epi16(
      _mm_sll_epi64(bits0_64_hi, nbits0_64_lo),
      _mm_sll_epi64(bits0_64_hi,
                    _mm_unpackhi_epi64(nbits0_64_lo, nbits0_64_lo)),
      0xf0);
  bits1_64_hi = _mm_blend_epi16(
      _mm_sll_epi64(bits1_64_hi, nbits1_64_lo),
      _mm_sll_epi64(bits1_64_hi,
                    _mm_unpackhi_epi64(nbits1_64_lo, nbits1_64_lo)),
      0xf0);

  bits0 = MMSI(or)(bits0_64_lo, bits0_64_hi);
  bits1 = MMSI(or)(bits1_64_lo, bits1_64_hi);
#endif

  auto nbits01 = MM(hadd_epi32)(nbits0, nbits1);

  // nbits_a <= 40 as we have at most 10 bits per symbol, so the call to the
  // writer is safe.
  alignas(SIMD_WIDTH) uint32_t nbits_a[SIMD_WIDTH / 4];
  MMSI(store)((MIVEC *)nbits_a, nbits01);

#endif

  alignas(SIMD_WIDTH) uint64_t bits_a[SIMD_WIDTH / 4];
  MMSI(store)((MIVEC *)bits_a, bits0);
  MMSI(store)((MIVEC *)bits_a + 1, bits1);

#ifdef __AVX2__
  constexpr uint8_t kPerm[] = {0, 1, 4, 5, 2, 3, 6, 7};
#else
  constexpr uint8_t kPerm[] = {0, 1, 2, 3};
#endif

  for (size_t ii = 0; ii < SIMD_WIDTH / 4; ii++) {
    uint64_t bits = bits_a[kPerm[ii]];
#if FPNGE_USE_PEXT
    bits = _pext_u64(bits, bitmask_a[kPerm[ii]]);
#endif
    auto count = nbits_a[ii];
    writer->Write(count, bits);
  }
}

// as above, but where nbits <= 8, so we can ignore bits_hi
static FORCE_INLINE void WriteBitsShort(MIVEC nbits, MIVEC bits,
                                        BitWriter *__restrict writer) {

#if FPNGE_USE_PEXT
  // convert nbits into a mask
  auto bitmask = MM(shuffle_epi8)(
      BCAST128(_mm_set_epi32(0xffffffff, 0xffffffff, 0x7f3f1f0f, 0x07030100)),
      nbits);
  auto bit_count = MM(sad_epu8)(nbits, MMSI(setzero)());

  alignas(SIMD_WIDTH) uint64_t nbits_a[SIMD_WIDTH / 8];
  MMSI(store)((MIVEC *)nbits_a, bit_count);
  alignas(SIMD_WIDTH) uint64_t bits_a[SIMD_WIDTH / 8];
  MMSI(store)((MIVEC *)bits_a, bits);
  alignas(SIMD_WIDTH) uint64_t bitmask_a[SIMD_WIDTH / 8];
  MMSI(store)((MIVEC *)bitmask_a, bitmask);
#else
  // 8 -> 16
  auto prod = MM(slli_epi16)(
      MM(shuffle_epi8)(BCAST128(_mm_set_epi32(
                           //  since we can't handle 8 bits, we'll under-shift
                           //  it and do an extra shift later on
                           -1, 0xffffff80, 0x40201008, 0x040201ff)),
                       nbits),
      8);
  auto bits_hi =
      MM(mulhi_epu16)(MMSI(andnot)(MM(set1_epi16)(0xff), bits), prod);
  bits_hi = MM(add_epi16)(bits_hi, bits_hi); // fix under-shifting
  bits = MMSI(or)(MMSI(and)(bits, MM(set1_epi16)(0xff)), bits_hi);
  nbits = MM(maddubs_epi16)(nbits, MM(set1_epi8)(1));

  // 16 -> 32
  auto nbits_32_lo = MMSI(and)(nbits, MM(set1_epi32)(0xFFFF));
  auto bits_32_lo = MMSI(and)(bits, MM(set1_epi32)(0xFFFF));
  auto bits_32_hi = MM(srli_epi32)(bits, 16);
#ifdef __AVX2__
  bits_32_hi = MM(sllv_epi32)(bits_32_hi, nbits_32_lo);
#else
  // need to avoid overflow when converting float -> int, because it converts to
  // a signed int; do this by offsetting the shift by 1
  nbits_32_lo = MM(add_epi16)(nbits_32_lo, MM(set1_epi32)(0xFFFF));
  bits_32_hi = _mm_castps_si128(MM(cvtepi32_ps)(bits_32_hi));
  bits_32_hi = MM(add_epi32)(bits_32_hi, MM(slli_epi32)(nbits_32_lo, 23));
  bits_32_hi = MM(cvtps_epi32)(_mm_castsi128_ps(bits_32_hi));
  bits_32_hi = MM(add_epi32)(bits_32_hi, bits_32_hi); // fix under-shifting
#endif
  nbits = MM(madd_epi16)(nbits, MM(set1_epi16)(1));
  bits = MMSI(or)(bits_32_lo, bits_32_hi);

  // 32 -> 64
#ifdef __AVX2__
  auto nbits_inv_64_lo = MM(subs_epu8)(MM(set1_epi64x)(32), nbits);
  bits = MM(sllv_epi32)(bits, nbits_inv_64_lo);
  bits = MM(srlv_epi64)(bits, nbits_inv_64_lo);
#else
  auto nbits_64_lo = MMSI(and)(nbits, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits_64_lo = MMSI(and)(bits, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits_64_hi = MM(srli_epi64)(bits, 32);
  bits_64_hi = _mm_blend_epi16(
      _mm_sll_epi64(bits_64_hi, nbits_64_lo),
      _mm_sll_epi64(bits_64_hi, _mm_unpackhi_epi64(nbits_64_lo, nbits_64_lo)),
      0xf0);
  bits = MMSI(or)(bits_64_lo, bits_64_hi);
#endif

  auto nbits2 = _mm_hadd_epi32(
#ifdef __AVX2__
      _mm256_castsi256_si128(nbits), _mm256_extracti128_si256(nbits, 1)
#else
      nbits, nbits
#endif
  );

  alignas(16) uint32_t nbits_a[4];
  alignas(SIMD_WIDTH) uint64_t bits_a[SIMD_WIDTH / 8];

  _mm_store_si128((__m128i *)nbits_a, nbits2);
  MMSI(store)((MIVEC *)bits_a, bits);

#endif

  for (size_t ii = 0; ii < SIMD_WIDTH / 8; ii++) {
    uint64_t bits64 = bits_a[ii];
#if FPNGE_USE_PEXT
    bits64 = _pext_u64(bits64, bitmask_a[ii]);
#endif
    if (nbits_a[ii] + writer->bits_in_buffer > 63) {
      // hope this case rarely occurs
      writer->Write(16, bits64 & 0xffff);
      bits64 >>= 16;
      nbits_a[ii] -= 16;
    }
    writer->Write(nbits_a[ii], bits64);
  }
}

static FORCE_INLINE void AddApproxCost(MIVEC &total, MIVEC pdata,
                                       MIVEC bit_costs) {
  auto approx_sym = MM(min_epu8)(MM(abs_epi8)(pdata), MM(set1_epi8)(15));
  auto cost = MM(shuffle_epi8)(bit_costs, approx_sym);
  total = MM(add_epi64)(total, MM(sad_epu8)(cost, MMSI(setzero)()));
}
static FORCE_INLINE void AddApproxCost(MIVEC &total, MIVEC pdata,
                                       MIVEC bit_costs, MIVEC maskv) {
  auto approx_sym = MM(min_epu8)(MM(abs_epi8)(pdata), MM(set1_epi8)(15));
  auto cost = MM(shuffle_epi8)(bit_costs, approx_sym);
  auto cost_mask = MMSI(and)(maskv, cost);
  total = MM(add_epi64)(total, MM(sad_epu8)(cost, cost_mask));
}

static uint8_t
SelectPredictor(size_t bytes_per_line, const unsigned char *current_row_buf,
                const unsigned char *top_buf, const unsigned char *left_buf,
                const unsigned char *topleft_buf, unsigned char *paeth_data,
                const HuffmanTable &table, const struct FPNGEOptions *options) {
  if (options->predictor <= 4) {
    return options->predictor;
  }
  if (options->predictor == FPNGE_PREDICTOR_APPROX) {
    auto bit_costs = BCAST128(_mm_load_si128((__m128i *)(table.approx_nbits)));
    size_t i = 0;
    auto cost1 = MMSI(setzero)();
    auto cost2 = MMSI(setzero)();
    auto cost3 = MMSI(setzero)();
    auto cost4 = MMSI(setzero)();
    MIVEC pdata;

    for (; i + SIMD_WIDTH <= bytes_per_line; i += SIMD_WIDTH) {
      pdata = PredictVec<1>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost1, pdata, bit_costs);

      pdata = PredictVec<2>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost2, pdata, bit_costs);

      pdata = PredictVec<3>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost3, pdata, bit_costs);

      pdata = PredictVec<4>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost4, pdata, bit_costs);
      MMSI(store)((MIVEC *)(paeth_data + i), pdata);
    }

    size_t bytes_remaining =
        bytes_per_line ^ i; // equivalent to `bytes_per_line - i`
    if (bytes_remaining) {
      auto maskv = MMSI(loadu)((MIVEC *)(kMaskVec - bytes_remaining));

      pdata = PredictVec<1>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost1, pdata, bit_costs, maskv);

      pdata = PredictVec<2>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost2, pdata, bit_costs, maskv);

      pdata = PredictVec<3>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost3, pdata, bit_costs, maskv);

      pdata = PredictVec<4>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost4, pdata, bit_costs, maskv);
      MMSI(store)((MIVEC *)(paeth_data + i), pdata);
    }

    uint8_t predictor = 1;
    size_t best_cost = hadd(cost1);
    auto test_cost = [&](MIVEC costv, uint8_t pred) {
      size_t cost = hadd(costv);
      if (cost < best_cost) {
        best_cost = cost;
        predictor = pred;
      }
    };
    test_cost(cost2, 2);
    test_cost(cost3, 3);
    test_cost(cost4, 4);
    return predictor;
  }

  assert(options->predictor == FPNGE_PREDICTOR_BEST);
  uint8_t predictor;
  size_t best_cost = ~0U;
  TryPredictor<1, /*store_pred=*/false>(bytes_per_line, current_row_buf,
                                        top_buf, left_buf, topleft_buf, nullptr,
                                        table, best_cost, predictor);
  TryPredictor<2, /*store_pred=*/false>(bytes_per_line, current_row_buf,
                                        top_buf, left_buf, topleft_buf, nullptr,
                                        table, best_cost, predictor);
  TryPredictor<3, /*store_pred=*/false>(bytes_per_line, current_row_buf,
                                        top_buf, left_buf, topleft_buf, nullptr,
                                        table, best_cost, predictor);
  TryPredictor<4, /*store_pred=*/true>(bytes_per_line, current_row_buf, top_buf,
                                       left_buf, topleft_buf, paeth_data, table,
                                       best_cost, predictor);
  return predictor;
}

static void
EncodeOneRow(size_t bytes_per_line, const unsigned char *current_row_buf,
             const unsigned char *top_buf, const unsigned char *left_buf,
             const unsigned char *topleft_buf, unsigned char *paeth_data,
             const HuffmanTable &table, uint32_t &s1, uint32_t &s2,
             BitWriter *__restrict writer, const struct FPNGEOptions *options) {
  uint8_t predictor =
      SelectPredictor(bytes_per_line, current_row_buf, top_buf, left_buf,
                      topleft_buf, paeth_data, table, options);

  writer->Write(table.first16_nbits[predictor], table.first16_bits[predictor]);
  UpdateAdler32(s1, s2, predictor);

  auto adler_accum_s1 = INT2VEC(s1);
  auto adler_accum_s2 = INT2VEC(s2);
  auto adler_s1_sum = MMSI(setzero)();

  uint16_t bytes_since_flush = 1;

  auto flush_adler = [&]() {
    adler_accum_s2 = MM(add_epi32)(
        adler_accum_s2, MM(slli_epi32)(adler_s1_sum, SIMD_WIDTH == 32 ? 5 : 4));
    adler_s1_sum = MMSI(setzero)();

    uint32_t ls1 = hadd(adler_accum_s1);
    uint32_t ls2 = hadd(adler_accum_s2);
    ls1 %= kAdler32Mod;
    ls2 %= kAdler32Mod;
    s1 = ls1;
    s2 = ls2;
    adler_accum_s1 = INT2VEC(s1);
    adler_accum_s2 = INT2VEC(s2);
    bytes_since_flush = 0;
  };

  auto encode_chunk_cb = [&](const MIVEC bytes, const size_t bytes_in_vec) {
    auto maskv = MMSI(loadu)((MIVEC *)(kMaskVec - bytes_in_vec));

    auto data_for_lut = MMSI(and)(MM(set1_epi8)(0xF), bytes);
    data_for_lut = MMSI(or)(data_for_lut, maskv);
    // get a mask of `bytes` that are between -16 and 15 inclusive
    // (`-16 <= bytes <= 15` is equivalent to `bytes + 112 > 95`)
    auto use_lowhi = MM(cmpgt_epi8)(MM(add_epi8)(bytes, MM(set1_epi8)(112)),
                                    MM(set1_epi8)(95));

    auto nbits_low16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.first16_nbits)), data_for_lut);
    auto nbits_hi16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.last16_nbits)), data_for_lut);
    auto nbits = MM(blendv_epi8)(nbits_low16, nbits_hi16, bytes);

    auto bits_low16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.first16_bits)), data_for_lut);
    auto bits_hi16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.last16_bits)), data_for_lut);
    auto bits_lo = MM(blendv_epi8)(bits_low16, bits_hi16, bytes);

    if (MM(movemask_epi8)(use_lowhi) ^ SIMD_MASK) {
      auto data_for_midlut =
          MMSI(and)(MM(set1_epi8)(0xF), MM(srai_epi16)(bytes, 4));

      auto bits_mid_lo = MM(shuffle_epi8)(
          BCAST128(_mm_load_si128((__m128i *)table.mid_lowbits)),
          data_for_midlut);

      auto bits_hi = MM(shuffle_epi8)(
          BCAST128(_mm_load_si128((__m128i *)kBitReverseNibbleLookup)),
          data_for_lut);

      use_lowhi = MMSI(or)(use_lowhi, maskv);
      nbits = MM(blendv_epi8)(MM(set1_epi8)(table.mid_nbits), nbits, use_lowhi);
      bits_lo = MM(blendv_epi8)(bits_mid_lo, bits_lo, use_lowhi);

#if !FPNGE_USE_PEXT
      bits_hi = MMSI(andnot)(use_lowhi, bits_hi);
#endif

      WriteBitsLong(nbits, bits_lo, bits_hi, table.mid_nbits - 4, writer);
    } else {
      // since mid (symbols 16-239) is not present, we can take some shortcuts
      // this is expected to occur frequently if compression is effective
      WriteBitsShort(nbits, bits_lo, writer);
    }
  };

  auto adler_chunk_cb = [&](const MIVEC pdata, size_t bytes_in_vec, size_t) {
    bytes_since_flush += bytes_in_vec;
    auto bytes = pdata;

    auto muls = MM(set_epi8)(
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
#if SIMD_WIDTH == 32
        ,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
#endif
    );

    if (bytes_in_vec < SIMD_WIDTH) {
      adler_accum_s2 = MM(add_epi32)(
          MM(mul_epu32)(MM(set1_epi32)(bytes_in_vec), adler_accum_s1),
          adler_accum_s2);
      bytes =
          MMSI(andnot)(MMSI(loadu)((MIVEC *)(kMaskVec - bytes_in_vec)), bytes);
      muls = MM(add_epi8)(muls, MM(set1_epi8)(bytes_in_vec - SIMD_WIDTH));
    } else {
      adler_s1_sum = MM(add_epi32)(adler_s1_sum, adler_accum_s1);
    }

    adler_accum_s1 =
        MM(add_epi32)(adler_accum_s1, MM(sad_epu8)(bytes, MMSI(setzero)()));

    auto bytesmuls = MM(maddubs_epi16)(bytes, muls);
    adler_accum_s2 = MM(add_epi32)(
        adler_accum_s2, MM(madd_epi16)(bytesmuls, MM(set1_epi16)(1)));

    if (bytes_since_flush >= 5500) {
      flush_adler();
    }
  };

  auto encode_rle_cb = [&](size_t run) {
    writer->Write(table.first16_nbits[0], table.first16_bits[0]);
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      uint32_t bits = (table.dist_bits << table.lz77_length_nbits[len]) |
                      table.lz77_length_bits[len];
      auto nbits = table.lz77_length_nbits[len] + table.dist_nbits;
      while (count--) {
        writer->Write(nbits, bits);
      }
    });
  };

  if (options->predictor > 4 && predictor == 4) {
    // re-use Paeth data
    ProcessRow<0>(bytes_per_line, paeth_data, nullptr, nullptr, nullptr,
                  encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  } else {
    ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
               topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  }

  flush_adler();
}

static void
CollectSymbolCounts(size_t bytes_per_line, const unsigned char *current_row_buf,
                    const unsigned char *top_buf, const unsigned char *left_buf,
                    const unsigned char *topleft_buf, unsigned char *paeth_data,
                    uint64_t *__restrict symbol_counts,
                    const struct FPNGEOptions *options) {

  auto encode_chunk_cb = [&](const MIVEC pdata, const size_t bytes_in_vec) {
    alignas(SIMD_WIDTH) uint8_t predicted_data[SIMD_WIDTH];
    MMSI(store)((MIVEC *)predicted_data, pdata);
    for (size_t i = 0; i < bytes_in_vec; i++) {
      symbol_counts[predicted_data[i]] += 1;
    }
  };

  auto adler_chunk_cb = [&](const MIVEC, size_t, size_t) {};

  auto encode_rle_cb = [&](size_t run) {
    symbol_counts[0] += 1;
    constexpr size_t kLZ77Sym[] = {
        0,   0,   0,   257, 258, 259, 260, 261, 262, 263, 264, 265, 265, 266,
        266, 267, 267, 268, 268, 269, 269, 269, 269, 270, 270, 270, 270, 271,
        271, 271, 271, 272, 272, 272, 272, 273, 273, 273, 273, 273, 273, 273,
        273, 274, 274, 274, 274, 274, 274, 274, 274, 275, 275, 275, 275, 275,
        275, 275, 275, 276, 276, 276, 276, 276, 276, 276, 276, 277, 277, 277,
        277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 278,
        278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278,
        278, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279,
        279, 279, 279, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280,
        280, 280, 280, 280, 280, 281, 281, 281, 281, 281, 281, 281, 281, 281,
        281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281,
        281, 281, 281, 281, 281, 281, 281, 281, 281, 282, 282, 282, 282, 282,
        282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282,
        282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 283,
        283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283,
        283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283,
        283, 283, 283, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284,
        284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284,
        284, 284, 284, 284, 284, 284, 285,
    };
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      symbol_counts[kLZ77Sym[len]] += count;
    });
  };

  if (options->predictor == FPNGE_PREDICTOR_APPROX) {
    // filter selection here seems to be slightly more effective when using the
    // approximate selector; more investigation is probably warranted
    HuffmanTable dummy_table;
    uint8_t predictor =
        SelectPredictor(bytes_per_line, current_row_buf, top_buf, left_buf,
                        topleft_buf, paeth_data, dummy_table, options);
    if (predictor == 4) {
      ProcessRow<0>(bytes_per_line, paeth_data, nullptr, nullptr, nullptr,
                    encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
    } else {
      ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
                 topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
    }
  } else {
    uint8_t predictor = options->predictor > 4 ? 4 : options->predictor;
    ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
               topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  }
}

static void AppendBE32(size_t value, BitWriter *__restrict writer) {
  writer->Write(8, value >> 24);
  writer->Write(8, (value >> 16) & 0xFF);
  writer->Write(8, (value >> 8) & 0xFF);
  writer->Write(8, value & 0xFF);
}

static void WriteHeader(size_t width, size_t height, size_t bytes_per_channel,
                        size_t num_channels, char cicp_colorspace,
                        const FPNGEAdditionalChunk *additional_chunks,
                        int num_additional_chunks,
                        BitWriter *__restrict writer) {
  constexpr uint8_t kPNGHeader[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  for (size_t i = 0; i < 8; i++) {
    writer->Write(8, kPNGHeader[i]);
  }
  // Length
  writer->Write(32, 0x0d000000);
  assert(writer->bits_in_buffer == 0);
  size_t crc_start = writer->bytes_written;
  // IHDR
  writer->Write(32, 0x52444849);
  AppendBE32(width, writer);
  AppendBE32(height, writer);
  // Bit depth
  writer->Write(8, bytes_per_channel * 8);
  // Colour type
  constexpr uint8_t numc_to_colour_type[] = {0, 0, 4, 2, 6};
  writer->Write(8, numc_to_colour_type[num_channels]);
  // Compression, filter and interlace methods.
  writer->Write(24, 0);
  assert(writer->bits_in_buffer == 0);
  size_t crc_end = writer->bytes_written;
  uint32_t crc =
      Crc32().update_final(writer->data + crc_start, crc_end - crc_start);
  AppendBE32(crc, writer);

  if (cicp_colorspace == FPNGE_CICP_PQ) {
    writer->Write(32, 0x04000000);
    writer->Write(32, 0x50434963); // cICP
    writer->Write(32, 0x01001009); // PQ, Rec2020
    writer->Write(32, 0xfe23234d); // CRC
  }
  for (int i = 0; i < num_additional_chunks; i++) {
    AppendBE32(additional_chunks[i].data_size, writer);
    size_t crc_start = writer->bytes_written;
    uint32_t name = 0;
    memcpy(&name, additional_chunks[i].name, 4);
    writer->Write(32, name);
    writer->WriteBytes(additional_chunks[i].data,
                       additional_chunks[i].data_size);
    size_t crc_end = writer->bytes_written;
    uint32_t crc =
        Crc32().update_final(writer->data + crc_start, crc_end - crc_start);
    AppendBE32(crc, writer);
  }
}

void CopyRow(unsigned char *dst, const unsigned char *src, size_t nb_channels,
             size_t bytes_per_channel, FPNGEColorChannelOrder order,
             size_t width) {
  if (order == FPNGE_ORDER_RGB || nb_channels <= 2) {
    memcpy(dst, src, nb_channels * bytes_per_channel * width);
    return;
  }
  size_t x = 0;
  if (nb_channels == 4 && bytes_per_channel == 1) {
    for (; x + SIMD_WIDTH / 4 <= width; x += SIMD_WIDTH / 4) {
      auto vec = MMSI(loadu)((MIVEC *)(src + x * 4));
      auto shuf = MM(shuffle_epi8)(
          vec, BCAST128(_mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14,
                                      13, 12, 15)));
      MMSI(storeu)((MIVEC *)(dst + x * 4), shuf);
    }
  } else if (nb_channels == 4 && bytes_per_channel == 2) {
    for (; x + SIMD_WIDTH / 8 <= width; x += SIMD_WIDTH / 8) {
      auto vec = MMSI(loadu)((MIVEC *)(src + x * 8));
      auto shuf = MM(shuffle_epi8)(
          vec, BCAST128(_mm_setr_epi8(4, 5, 2, 3, 0, 1, 6, 7, 12, 13, 10, 11, 8,
                                      9, 14, 15)));
      MMSI(storeu)((MIVEC *)(dst + x * 8), shuf);
    }
  } else if (nb_channels == 3 && bytes_per_channel == 1) {
    for (; x + 16 <= width; x += 16) {
      auto vec1 = _mm_loadu_si128((__m128i *)(src + x * 3));
      auto vec2 = _mm_loadu_si128((__m128i *)(src + x * 3 + 16));
      auto vec3 = _mm_loadu_si128((__m128i *)(src + x * 3 + 32));
      auto s1a =
          _mm_setr_epi8(2, 1, 0, 5, 4, 3, 8, 7, 6, 11, 10, 9, 14, 13, 12, -1);
      auto s1b = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, 1);
      auto shuf1 = _mm_or_si128(_mm_shuffle_epi8(vec1, s1a),
                                _mm_shuffle_epi8(vec2, s1b));
      auto s2a = _mm_setr_epi8(-1, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s2b =
          _mm_setr_epi8(0, -1, 4, 3, 2, 7, 6, 5, 10, 9, 8, 13, 12, 11, -1, 15);
      auto s2c = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, 0, -1);
      auto shuf2 = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(vec1, s2a),
                                             _mm_shuffle_epi8(vec2, s2b)),
                                _mm_shuffle_epi8(vec3, s2c));
      auto s3b = _mm_setr_epi8(14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s3c =
          _mm_setr_epi8(-1, 3, 2, 1, 6, 5, 4, 9, 8, 7, 12, 11, 10, 15, 14, 13);
      auto shuf3 = _mm_or_si128(_mm_shuffle_epi8(vec2, s3b),
                                _mm_shuffle_epi8(vec3, s3c));

      _mm_storeu_si128((__m128i *)(dst + x * 3), shuf1);
      _mm_storeu_si128((__m128i *)(dst + x * 3 + 16), shuf2);
      _mm_storeu_si128((__m128i *)(dst + x * 3 + 32), shuf3);
    }
  } else if (nb_channels == 3 && bytes_per_channel == 2) {
    for (; x + 8 <= width; x += 8) {
      auto vec1 = _mm_loadu_si128((__m128i *)(src + x * 6));
      auto vec2 = _mm_loadu_si128((__m128i *)(src + x * 6 + 16));
      auto vec3 = _mm_loadu_si128((__m128i *)(src + x * 6 + 32));
      auto s1a =
          _mm_setr_epi8(4, 5, 2, 3, 0, 1, 10, 11, 8, 9, 6, 7, -1, -1, 14, 15);
      auto s1b = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               0, 1, -1, -1);
      auto shuf1 = _mm_or_si128(_mm_shuffle_epi8(vec1, s1a),
                                _mm_shuffle_epi8(vec2, s1b));
      auto s2a = _mm_setr_epi8(12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s2b =
          _mm_setr_epi8(-1, -1, 6, 7, 4, 5, 2, 3, 12, 13, 10, 11, 8, 9, -1, -1);
      auto s2c = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, 2, 3);
      auto shuf2 = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(vec1, s2a),
                                             _mm_shuffle_epi8(vec2, s2b)),
                                _mm_shuffle_epi8(vec3, s2c));
      auto s3b = _mm_setr_epi8(-1, -1, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s3c =
          _mm_setr_epi8(0, 1, -1, -1, 8, 9, 6, 7, 4, 5, 14, 15, 12, 13, 10, 11);
      auto shuf3 = _mm_or_si128(_mm_shuffle_epi8(vec2, s3b),
                                _mm_shuffle_epi8(vec3, s3c));

      _mm_storeu_si128((__m128i *)(dst + x * 6), shuf1);
      _mm_storeu_si128((__m128i *)(dst + x * 6 + 16), shuf2);
      _mm_storeu_si128((__m128i *)(dst + x * 6 + 32), shuf3);
    }
  }
  for (; x < width; x++) {
    if (nb_channels == 3 && bytes_per_channel == 1) {
      dst[x * 3] = src[x * 3 + 2];
      dst[x * 3 + 1] = src[x * 3 + 1];
      dst[x * 3 + 2] = src[x * 3];
    }
    if (nb_channels == 3 && bytes_per_channel == 2) {
      dst[x * 6] = src[x * 6 + 4];
      dst[x * 6 + 1] = src[x * 6 + 3];
      dst[x * 6 + 2] = src[x * 6 + 2];
      dst[x * 6 + 3] = src[x * 6 + 3];
      dst[x * 6 + 4] = src[x * 6];
      dst[x * 6 + 5] = src[x * 6 + 1];
    }
    if (nb_channels == 4 && bytes_per_channel == 1) {
      dst[x * 4] = src[x * 4 + 2];
      dst[x * 4 + 1] = src[x * 4 + 1];
      dst[x * 4 + 2] = src[x * 4];
      dst[x * 4 + 3] = src[x * 4 + 3];
    }
    if (nb_channels == 4 && bytes_per_channel == 2) {
      dst[x * 8] = src[x * 8 + 4];
      dst[x * 8 + 1] = src[x * 8 + 3];
      dst[x * 8 + 2] = src[x * 8 + 2];
      dst[x * 8 + 3] = src[x * 8 + 3];
      dst[x * 8 + 4] = src[x * 8];
      dst[x * 8 + 5] = src[x * 8 + 1];
      dst[x * 8 + 6] = src[x * 8 + 6];
      dst[x * 8 + 7] = src[x * 8 + 7];
    }
  }
}

} // namespace

extern "C" size_t FPNGEEncode(size_t bytes_per_channel, size_t num_channels,
                              const void *data, size_t width, size_t row_stride,
                              size_t height, void *output,
                              const struct FPNGEOptions *options) {
  assert(bytes_per_channel == 1 || bytes_per_channel == 2);
  assert(num_channels != 0 && num_channels <= 4);
  size_t bytes_per_line = bytes_per_channel * num_channels * width;
  assert(row_stride >= bytes_per_line);

  // allows for padding, and for extra initial space for the "left" pixel for
  // predictors.
  size_t bytes_per_line_buf =
      (bytes_per_line + 4 * bytes_per_channel + SIMD_WIDTH - 1) / SIMD_WIDTH *
      SIMD_WIDTH;

  // Extra space for alignment purposes.
  std::vector<unsigned char> buf(bytes_per_line_buf * 2 + SIMD_WIDTH - 1 +
                                 4 * bytes_per_channel);
  unsigned char *aligned_buf_ptr = buf.data() + 4 * bytes_per_channel;
  aligned_buf_ptr += (intptr_t)aligned_buf_ptr % SIMD_WIDTH
                         ? (SIMD_WIDTH - (intptr_t)aligned_buf_ptr % SIMD_WIDTH)
                         : 0;

  std::vector<unsigned char> pdata_buf(bytes_per_line_buf + SIMD_WIDTH - 1);
  unsigned char *aligned_pdata_ptr = pdata_buf.data();
  aligned_pdata_ptr +=
      (intptr_t)aligned_pdata_ptr % SIMD_WIDTH
          ? (SIMD_WIDTH - (intptr_t)aligned_pdata_ptr % SIMD_WIDTH)
          : 0;

  struct FPNGEOptions default_options;
  if (options == nullptr) {
    FPNGEFillOptions(&default_options, FPNGE_COMPRESS_LEVEL_DEFAULT,
                     FPNGE_CICP_NONE);
    options = &default_options;
  }

  // options sanity check
  assert(options->predictor >= 0 && options->predictor <= FPNGE_PREDICTOR_BEST);
  assert(options->huffman_sample >= 0 && options->huffman_sample <= 127);

  BitWriter writer;
  writer.data = static_cast<unsigned char *>(output);

  WriteHeader(width, height, bytes_per_channel, num_channels,
              options->cicp_colorspace, options->additional_chunks,
              options->num_additional_chunks, &writer);

  assert(writer.bits_in_buffer == 0);
  size_t chunk_length_pos = writer.bytes_written;
  writer.bytes_written += 4; // Skip space for length.
  size_t crc_pos = writer.bytes_written;
  writer.Write(32, 0x54414449); // IDAT
  // Deflate header
  writer.Write(8, 8);  // deflate with smallest window
  writer.Write(8, 29); // cfm+flg check value

  uint64_t symbol_counts[286] = {};

  // Sample rows in the center of the image.
  size_t y0 = height * (127 - options->huffman_sample) / 256;
  size_t y1 = height * (129 + options->huffman_sample) / 256;
  if (y1 == 0 && height > 0) { // for 1 pixel high images
    y1 = 1;
  }

  for (size_t y = y0; y < y1; y++) {
    const unsigned char *current_row_in =
        static_cast<const unsigned char *>(data) + row_stride * y;
    unsigned char *current_row_buf =
        aligned_buf_ptr + (y % 2 ? bytes_per_line_buf : 0);
    const unsigned char *top_buf =
        aligned_buf_ptr + ((y + 1) % 2 ? bytes_per_line_buf : 0);
    const unsigned char *left_buf =
        current_row_buf - bytes_per_channel * num_channels;
    const unsigned char *topleft_buf =
        top_buf - bytes_per_channel * num_channels;

    CopyRow(current_row_buf, current_row_in, num_channels, bytes_per_channel,
            (FPNGEColorChannelOrder)options->channel_order, width);
    if (y == y0 && y != 0) {
      continue;
    }

    CollectSymbolCounts(bytes_per_line, current_row_buf, top_buf, left_buf,
                        topleft_buf, aligned_pdata_ptr, symbol_counts, options);
  }

  memset(buf.data(), 0, buf.size());

  HuffmanTable huffman_table(symbol_counts);

  // Single block, dynamic huffman
  writer.Write(3, 0b101);
  WriteHuffmanCode(huffman_table, &writer);

  Crc32 crc;
  uint32_t s1 = 1;
  uint32_t s2 = 0;
  for (size_t y = 0; y < height; y++) {
    const unsigned char *current_row_in =
        static_cast<const unsigned char *>(data) + row_stride * y;
    unsigned char *current_row_buf =
        aligned_buf_ptr + (y % 2 ? bytes_per_line_buf : 0);
    const unsigned char *top_buf =
        aligned_buf_ptr + ((y + 1) % 2 ? bytes_per_line_buf : 0);
    const unsigned char *left_buf =
        current_row_buf - bytes_per_channel * num_channels;
    const unsigned char *topleft_buf =
        top_buf - bytes_per_channel * num_channels;

    CopyRow(current_row_buf, current_row_in, num_channels, bytes_per_channel,
            (FPNGEColorChannelOrder)options->channel_order, width);

    EncodeOneRow(bytes_per_line, current_row_buf, top_buf, left_buf,
                 topleft_buf, aligned_pdata_ptr, huffman_table, s1, s2, &writer,
                 options);

    crc_pos +=
        crc.update(writer.data + crc_pos, writer.bytes_written - crc_pos);
  }

  // EOB
  writer.Write(huffman_table.nbits[256], huffman_table.end_bits);

  writer.ZeroPadToByte();
  assert(writer.bits_in_buffer == 0);
  s1 %= kAdler32Mod;
  s2 %= kAdler32Mod;
  uint32_t adler32 = (s2 << 16) | s1;
  AppendBE32(adler32, &writer);

  size_t data_len = writer.bytes_written - chunk_length_pos - 8;
  writer.data[chunk_length_pos + 0] = data_len >> 24;
  writer.data[chunk_length_pos + 1] = (data_len >> 16) & 0xFF;
  writer.data[chunk_length_pos + 2] = (data_len >> 8) & 0xFF;
  writer.data[chunk_length_pos + 3] = data_len & 0xFF;

  auto final_crc =
      crc.update_final(writer.data + crc_pos, writer.bytes_written - crc_pos);
  AppendBE32(final_crc, &writer);

  // IEND
  writer.Write(32, 0);
  writer.Write(32, 0x444e4549);
  writer.Write(32, 0x826042ae);

  return writer.bytes_written;
}
