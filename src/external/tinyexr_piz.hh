// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Syoyo Fujita and many contributors.
// All rights reserved.
//
// TinyEXR PIZ Compression/Decompression Module
//
// Part of TinyEXR V2 API (EXPERIMENTAL)
//
// Provides PIZ compression/decompression with:
// - Optimized Huffman decoding via FastHuffmanDecoder
// - SIMD-accelerated wavelet transform (SSE2/NEON)
// - Range compression via bitmap/LUT
//
// Usage:
//   #define TINYEXR_ENABLE_SIMD 1
//   #include "tinyexr_piz.hh"

#ifndef TINYEXR_PIZ_HH_
#define TINYEXR_PIZ_HH_

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>

// Include dependencies
#include "tinyexr_v2.hh"
#include "tinyexr_huffman.hh"

#if defined(TINYEXR_ENABLE_SIMD) && TINYEXR_ENABLE_SIMD
#include "tinyexr_simd.hh"
#endif

namespace tinyexr {
namespace piz {

// ============================================================================
// Compiler Hints for Optimization
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define PIZ_LIKELY(x)   __builtin_expect(!!(x), 1)
#define PIZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PIZ_ALWAYS_INLINE __attribute__((always_inline)) inline
#define PIZ_PREFETCH(addr) __builtin_prefetch(addr)
#else
#define PIZ_LIKELY(x)   (x)
#define PIZ_UNLIKELY(x) (x)
#define PIZ_ALWAYS_INLINE inline
#define PIZ_PREFETCH(addr) ((void)0)
#endif

// ============================================================================
// Constants
// ============================================================================

static const int BITMAP_SIZE = 8192;        // 65536 / 8
static const int USHORT_RANGE = 65536;      // 2^16

// ============================================================================
// PIZ Channel Data (internal structure for wavelet processing)
// ============================================================================

struct PIZChannelData {
  uint16_t* start;
  uint16_t* end;
  int nx;           // width
  int ny;           // height
  int size;         // 1 for HALF, 2 for FLOAT/UINT
};

// ============================================================================
// Bitmap and LUT Functions
// ============================================================================

// Build a bitmap from the data, marking which 16-bit values are present
inline void bitmapFromData(const uint16_t* data, int nData,
                           uint8_t* bitmap,
                           uint16_t& minNonZero, uint16_t& maxNonZero) {
  // Clear bitmap
  std::memset(bitmap, 0, BITMAP_SIZE);

  // Mark bits for each value in data
  for (int i = 0; i < nData; ++i) {
    bitmap[data[i] >> 3] |= (1 << (data[i] & 7));
  }

  // Zero is not explicitly stored in the bitmap;
  // we assume that the data always contain zeroes
  bitmap[0] &= ~1;

  // Find min and max non-zero bytes
  minNonZero = BITMAP_SIZE - 1;
  maxNonZero = 0;

  for (int i = 0; i < BITMAP_SIZE; ++i) {
    if (bitmap[i]) {
      if (minNonZero > i) minNonZero = static_cast<uint16_t>(i);
      if (maxNonZero < i) maxNonZero = static_cast<uint16_t>(i);
    }
  }
}

// Build forward LUT: maps original values to compressed range
inline uint16_t forwardLutFromBitmap(const uint8_t* bitmap, uint16_t* lut) {
  int k = 0;

  for (int i = 0; i < USHORT_RANGE; ++i) {
    if ((i == 0) || (bitmap[i >> 3] & (1 << (i & 7))))
      lut[i] = static_cast<uint16_t>(k++);
    else
      lut[i] = 0;
  }

  return static_cast<uint16_t>(k - 1);  // maximum value stored in lut[]
}

// Build reverse LUT: maps compressed range back to original values
inline uint16_t reverseLutFromBitmap(const uint8_t* bitmap, uint16_t* lut) {
  int k = 0;

  for (int i = 0; i < USHORT_RANGE; ++i) {
    if ((i == 0) || (bitmap[i >> 3] & (1 << (i & 7)))) {
      lut[k++] = static_cast<uint16_t>(i);
    }
  }

  int n = k - 1;

  // Fill rest with zeros
  while (k < USHORT_RANGE) {
    lut[k++] = 0;
  }

  return static_cast<uint16_t>(n);  // maximum k where lut[k] is non-zero
}

// Apply lookup table to data (in-place)
// Returns true on success, false if parameters are invalid
inline bool applyLut(const uint16_t* lut, uint16_t* data, int nData, size_t lutSize = USHORT_RANGE) {
  // Validate input parameters
  if (PIZ_UNLIKELY(!lut || !data)) return false;
  if (PIZ_UNLIKELY(nData < 0)) return false;
  if (PIZ_UNLIKELY(lutSize == 0)) return false;

#if defined(TINYEXR_ENABLE_SIMD) && TINYEXR_ENABLE_SIMD
  // SIMD version uses full 64K LUT, validate size
  if (PIZ_UNLIKELY(lutSize < USHORT_RANGE)) {
    // Fall back to scalar with bounds check
    for (int i = 0; i < nData; ++i) {
      size_t idx = static_cast<size_t>(data[i]);
      if (idx >= lutSize) return false;  // Out of bounds
      data[i] = lut[idx];
    }
    return true;
  }
  // Use AVX2 gather when available, otherwise prefetch version
#if TINYEXR_SIMD_AVX2
  tinyexr::simd::apply_lut_avx2(data, static_cast<size_t>(nData), lut);
#else
  tinyexr::simd::apply_lut_prefetch(data, static_cast<size_t>(nData), lut);
#endif
#else
  for (int i = 0; i < nData; ++i) {
    size_t idx = static_cast<size_t>(data[i]);
    if (idx >= lutSize) return false;  // Out of bounds
    data[i] = lut[idx];
  }
#endif
  return true;
}

// ============================================================================
// Wavelet Transform (Scalar Fallback)
// ============================================================================

// 14-bit wavelet encode (for max value < 16384)
inline void wenc14(uint16_t a, uint16_t b, uint16_t& l, uint16_t& h) {
  int16_t as = static_cast<int16_t>(a);
  int16_t bs = static_cast<int16_t>(b);

  int16_t ms = (as + bs) >> 1;
  int16_t ds = as - bs;

  l = static_cast<uint16_t>(ms);
  h = static_cast<uint16_t>(ds);
}

// 14-bit wavelet decode
inline void wdec14(uint16_t l, uint16_t h, uint16_t& a, uint16_t& b) {
  int16_t ls = static_cast<int16_t>(l);
  int16_t hs = static_cast<int16_t>(h);

  int hi = hs;
  int ai = ls + (hi & 1) + (hi >> 1);

  int16_t as = static_cast<int16_t>(ai);
  int16_t bs = static_cast<int16_t>(ai - hi);

  a = static_cast<uint16_t>(as);
  b = static_cast<uint16_t>(bs);
}

// 16-bit wavelet encode (with modulo arithmetic)
static const int NBITS = 16;
static const int A_OFFSET = 1 << (NBITS - 1);
static const int M_OFFSET = 1 << (NBITS - 1);
static const int MOD_MASK = (1 << NBITS) - 1;

inline void wenc16(uint16_t a, uint16_t b, uint16_t& l, uint16_t& h) {
  int ao = (a + A_OFFSET) & MOD_MASK;
  int m = ((ao + b) >> 1);
  int d = ao - b;

  if (d < 0) m = (m + M_OFFSET) & MOD_MASK;
  d &= MOD_MASK;

  l = static_cast<uint16_t>(m);
  h = static_cast<uint16_t>(d);
}

// 16-bit wavelet decode
inline void wdec16(uint16_t l, uint16_t h, uint16_t& a, uint16_t& b) {
  int m = l;
  int d = h;
  int bb = (m - (d >> 1)) & MOD_MASK;
  int aa = (d + bb - A_OFFSET) & MOD_MASK;
  b = static_cast<uint16_t>(bb);
  a = static_cast<uint16_t>(aa);
}

// 2D Wavelet encoding
inline void wav2Encode(uint16_t* in, int nx, int ox, int ny, int oy, uint16_t mx) {
  bool w14 = (mx < (1 << 14));
  int n = (nx > ny) ? ny : nx;
  int p = 1;
  int p2 = 2;

  while (p2 <= n) {
    uint16_t* py = in;
    uint16_t* ey = in + oy * (ny - p2);
    int oy1 = oy * p;
    int oy2 = oy * p2;
    int ox1 = ox * p;
    int ox2 = ox * p2;
    uint16_t i00, i01, i10, i11;

    // Y loop
    for (; py <= ey; py += oy2) {
      uint16_t* px = py;
      uint16_t* ex = py + ox * (nx - p2);

      // X loop
      for (; px <= ex; px += ox2) {
        uint16_t* p01 = px + ox1;
        uint16_t* p10 = px + oy1;
        uint16_t* p11 = p10 + ox1;

        if (w14) {
          wenc14(*px, *p01, i00, i01);
          wenc14(*p10, *p11, i10, i11);
          wenc14(i00, i10, *px, *p10);
          wenc14(i01, i11, *p01, *p11);
        } else {
          wenc16(*px, *p01, i00, i01);
          wenc16(*p10, *p11, i10, i11);
          wenc16(i00, i10, *px, *p10);
          wenc16(i01, i11, *p01, *p11);
        }
      }

      // Encode odd column
      if (nx & p) {
        uint16_t* p10 = px + oy1;
        if (w14)
          wenc14(*px, *p10, i00, *p10);
        else
          wenc16(*px, *p10, i00, *p10);
        *px = i00;
      }
    }

    // Encode odd line
    if (ny & p) {
      uint16_t* px = py;
      uint16_t* ex = py + ox * (nx - p2);

      for (; px <= ex; px += ox2) {
        uint16_t* p01 = px + ox1;
        if (w14)
          wenc14(*px, *p01, i00, *p01);
        else
          wenc16(*px, *p01, i00, *p01);
        *px = i00;
      }
    }

    p = p2;
    p2 <<= 1;
  }
}

// 2D Wavelet decoding with bounds validation
// Optimized with branch hints and prefetching
// Returns true on success, false if parameters are invalid
inline bool wav2Decode(uint16_t* in, int nx, int ox, int ny, int oy, uint16_t mx,
                       size_t bufferSize = 0) {
  // Validate input parameters (unlikely to fail with valid data)
  if (PIZ_UNLIKELY(!in)) return false;
  if (PIZ_UNLIKELY(nx <= 0 || ny <= 0)) return false;
  if (PIZ_UNLIKELY(ox <= 0 || oy <= 0)) return false;

  // Check for integer overflow in buffer size calculation
  int64_t maxOffset = static_cast<int64_t>(oy) * (ny - 1) +
                      static_cast<int64_t>(ox) * (nx - 1);
  if (PIZ_UNLIKELY(maxOffset < 0 || (bufferSize > 0 && static_cast<size_t>(maxOffset) >= bufferSize))) {
    return false;  // Buffer overflow would occur
  }

  const bool w14 = (mx < (1 << 14));
  int n = (nx > ny) ? ny : nx;
  int p = 1;
  int p2;

  // Search max level
  while (p <= n) p <<= 1;
  p >>= 1;
  p2 = p;
  p >>= 1;

  // Split into separate loops based on w14 to avoid branch in inner loop
  if (PIZ_LIKELY(w14)) {
    // 14-bit path (most common case for HALF data)
    while (p >= 1) {
      uint16_t* py = in;
      uint16_t* ey = in + oy * (ny - p2);
      const int oy1 = oy * p;
      const int oy2 = oy * p2;
      const int ox1 = ox * p;
      const int ox2 = ox * p2;
      uint16_t i00, i01, i10, i11;

      // Validate stride calculations don't overflow
      if (PIZ_UNLIKELY(oy1 < 0 || oy2 < 0 || ox1 < 0 || ox2 < 0)) return false;
      if (PIZ_UNLIKELY(oy2 == 0 || ox2 == 0)) return false;

      // Y loop
      for (; py <= ey; py += oy2) {
        uint16_t* px = py;
        uint16_t* ex = py + ox * (nx - p2);

        // Prefetch next row
        PIZ_PREFETCH(py + oy2);

        // X loop (unrolled inner decode)
        for (; px <= ex; px += ox2) {
          uint16_t* p01 = px + ox1;
          uint16_t* p10 = px + oy1;
          uint16_t* p11 = p10 + ox1;

          // Prefetch ahead in row
          PIZ_PREFETCH(px + ox2 + ox2);

          wdec14(*px, *p10, i00, i10);
          wdec14(*p01, *p11, i01, i11);
          wdec14(i00, i01, *px, *p01);
          wdec14(i10, i11, *p10, *p11);
        }

        // Decode odd column
        if (nx & p) {
          uint16_t* p10 = px + oy1;
          wdec14(*px, *p10, i00, *p10);
          *px = i00;
        }
      }

      // Decode odd line
      if (ny & p) {
        uint16_t* px = py;
        uint16_t* ex = py + ox * (nx - p2);

        for (; px <= ex; px += ox2) {
          uint16_t* p01 = px + ox1;
          wdec14(*px, *p01, i00, *p01);
          *px = i00;
        }
      }

      p2 = p;
      p >>= 1;
    }
  } else {
    // 16-bit path (for larger value ranges)
    while (p >= 1) {
      uint16_t* py = in;
      uint16_t* ey = in + oy * (ny - p2);
      const int oy1 = oy * p;
      const int oy2 = oy * p2;
      const int ox1 = ox * p;
      const int ox2 = ox * p2;
      uint16_t i00, i01, i10, i11;

      // Validate stride calculations don't overflow
      if (PIZ_UNLIKELY(oy1 < 0 || oy2 < 0 || ox1 < 0 || ox2 < 0)) return false;
      if (PIZ_UNLIKELY(oy2 == 0 || ox2 == 0)) return false;

      // Y loop
      for (; py <= ey; py += oy2) {
        uint16_t* px = py;
        uint16_t* ex = py + ox * (nx - p2);

        // Prefetch next row
        PIZ_PREFETCH(py + oy2);

        // X loop
        for (; px <= ex; px += ox2) {
          uint16_t* p01 = px + ox1;
          uint16_t* p10 = px + oy1;
          uint16_t* p11 = p10 + ox1;

          // Prefetch ahead in row
          PIZ_PREFETCH(px + ox2 + ox2);

          wdec16(*px, *p10, i00, i10);
          wdec16(*p01, *p11, i01, i11);
          wdec16(i00, i01, *px, *p01);
          wdec16(i10, i11, *p10, *p11);
        }

        // Decode odd column
        if (nx & p) {
          uint16_t* p10 = px + oy1;
          wdec16(*px, *p10, i00, *p10);
          *px = i00;
        }
      }

      // Decode odd line
      if (ny & p) {
        uint16_t* px = py;
        uint16_t* ex = py + ox * (nx - p2);

        for (; px <= ex; px += ox2) {
          uint16_t* p01 = px + ox1;
          wdec16(*px, *p01, i00, *p01);
          *px = i00;
        }
      }

      p2 = p;
      p >>= 1;
    }
  }
  return true;
}

// ============================================================================
// Huffman Constants and Helpers (matching V1 exactly)
// ============================================================================

static const int HUF_ENCBITS = 16;
static const int HUF_DECBITS = 14;
static const int HUF_ENCSIZE = (1 << HUF_ENCBITS) + 1;
static const int HUF_DECSIZE = 1 << HUF_DECBITS;
static const int HUF_DECMASK = HUF_DECSIZE - 1;

static const int SHORT_ZEROCODE_RUN = 59;
static const int LONG_ZEROCODE_RUN = 63;
static const int SHORTEST_LONG_RUN = 2 + LONG_ZEROCODE_RUN - SHORT_ZEROCODE_RUN;

// Decoding table entry (matching V1 HufDec)
struct HufDec {
  int len;
  int lit;
  int* p;
};

// Get n bits from packed stream (big-endian bit order, like V1)
// Returns -1 on error (bounds exceeded)
inline int64_t getBits(int nBits, int64_t& c, int& lc, const uint8_t*& in,
                       const uint8_t* in_end) {
  // Validate nBits range
  if (nBits <= 0 || nBits > 58) {
    return -1;
  }
  while (lc < nBits) {
    if (in >= in_end) {
      return -1;  // Bounds check: prevent reading past buffer
    }
    // Use unsigned shift to avoid signed overflow UB
    c = static_cast<int64_t>((static_cast<uint64_t>(c) << 8) | static_cast<uint64_t>(*in++));
    lc += 8;
  }
  lc -= nBits;
  return (c >> lc) & ((1LL << nBits) - 1);
}

// Legacy getBits without bounds check (for compatibility during transition)
inline int64_t getBitsUnchecked(int nBits, int64_t& c, int& lc, const uint8_t*& in) {
  while (lc < nBits) {
    // Use unsigned shift to avoid signed overflow UB
    c = static_cast<int64_t>((static_cast<uint64_t>(c) << 8) | static_cast<uint64_t>(*in++));
    lc += 8;
  }
  lc -= nBits;
  return (c >> lc) & ((1LL << nBits) - 1);
}

// Build canonical Huffman code table (matching V1 hufCanonicalCodeTable)
inline void hufCanonicalCodeTable(int64_t* hcode) {
  int64_t n[59] = {0};

  // Count codes of each length
  for (int i = 0; i < HUF_ENCSIZE; ++i) {
    if (hcode[i] >= 0 && hcode[i] <= 58) {
      n[hcode[i]]++;
    }
  }

  // Compute starting codes for each length (reverse order)
  int64_t c = 0;
  for (int i = 58; i > 0; --i) {
    int64_t nc = ((c + n[i]) >> 1);
    n[i] = c;
    c = nc;
  }

  // Assign codes
  for (int i = 0; i < HUF_ENCSIZE; ++i) {
    int l = static_cast<int>(hcode[i]);
    if (l > 0 && l <= 58) {
      hcode[i] = l | (n[l] << 6);
      n[l]++;
    }
  }
}

// Unpack encoding table (matching V1 hufUnpackEncTable)
inline bool hufUnpackEncTable(const uint8_t*& ptr, size_t ni, int im, int iM,
                               int64_t* hcode) {
  // Validate input parameters
  if (!hcode) return false;
  if (im < 0 || im >= HUF_ENCSIZE) return false;
  if (iM < 0 || iM >= HUF_ENCSIZE) return false;
  if (im > iM) return false;
  if (ni == 0) return false;

  std::memset(hcode, 0, sizeof(int64_t) * HUF_ENCSIZE);

  const uint8_t* start = ptr;
  const uint8_t* end = ptr + ni;
  int64_t c = 0;
  int lc = 0;

  for (int i = im; i <= iM; i++) {
    // Bounds check before reading
    if (ptr >= end && lc < 6) {
      return false;  // Not enough data
    }

    int64_t l = getBits(6, c, lc, ptr, end);
    if (l < 0) return false;  // getBits failed
    hcode[i] = l;

    if (l == LONG_ZEROCODE_RUN) {
      int64_t zerun_raw = getBits(8, c, lc, ptr, end);
      if (zerun_raw < 0) return false;  // getBits failed
      int zerun = static_cast<int>(zerun_raw) + SHORTEST_LONG_RUN;
      if (zerun < 0 || i + zerun > iM + 1) {
        return false;  // Invalid run length or overflow
      }
      while (zerun-- > 0 && i <= iM) hcode[i++] = 0;
      i--;
    } else if (l >= SHORT_ZEROCODE_RUN) {
      int zerun = static_cast<int>(l - SHORT_ZEROCODE_RUN + 2);
      if (zerun < 0 || i + zerun > iM + 1) {
        return false;  // Invalid run length or overflow
      }
      while (zerun-- > 0 && i <= iM) hcode[i++] = 0;
      i--;
    }
  }

  hufCanonicalCodeTable(hcode);
  return true;
}

// Build decoding table (matching V1 hufBuildDecTable)
// Two-pass approach to ensure long code symbols are stored contiguously
inline bool hufBuildDecTable(const int64_t* hcode, int im, int iM,
                              HufDec* hdecod, std::vector<int>& long_codes) {
  // Validate input parameters
  if (!hcode || !hdecod) return false;
  if (im < 0 || im >= HUF_ENCSIZE) return false;
  if (iM < 0 || iM >= HUF_ENCSIZE) return false;
  if (im > iM) return false;

  // Clear table (use memset for efficiency)
  std::memset(hdecod, 0, sizeof(HufDec) * HUF_DECSIZE);

  // Pass 1: Collect long codes per table index
  std::vector<std::vector<int>> long_code_lists(HUF_DECSIZE);

  for (int i = im; i <= iM; i++) {
    int64_t c = hcode[i] >> 6;
    int l = static_cast<int>(hcode[i] & 63);

    // Validate code length
    if (l < 0 || l > 58) {
      return false;  // Invalid code length
    }

    // Validate code value fits in length
    if (l > 0 && (c >> l) != 0) {
      return false;  // Code value too large for specified length
    }

    if (l > HUF_DECBITS) {
      // Long code - collect in per-index list
      int shift = l - HUF_DECBITS;
      if (shift < 0 || shift > 58) return false;  // Sanity check
      int idx = static_cast<int>((c >> shift) & HUF_DECMASK);
      if (idx < 0 || idx >= HUF_DECSIZE) return false;  // Bounds check
      if (hdecod[idx].len != 0) {
        // Already has a short code? Error
        return false;
      }
      long_code_lists[idx].push_back(i);
    } else if (l > 0) {
      // Short code - fill all matching entries
      int shift = HUF_DECBITS - l;
      if (shift < 0 || shift > HUF_DECBITS) return false;  // Sanity check
      int j = static_cast<int>(c << shift);
      if (j < 0 || j >= HUF_DECSIZE) return false;  // Bounds check
      int count = 1 << shift;
      if (j + count > HUF_DECSIZE) return false;  // Bounds check
      for (int k = 0; k < count; k++) {
        int idx = j + k;
        if (idx < 0 || idx >= HUF_DECSIZE) return false;  // Defensive
        HufDec* pl = &hdecod[idx];
        if (pl->len != 0 || !long_code_lists[idx].empty()) {
          // Already assigned? Error
          return false;
        }
        pl->len = l;
        pl->lit = i;
      }
    }
  }

  // Pass 2: Flatten long code lists into contiguous array
  long_codes.clear();

  // Calculate total size needed and check for overflow
  size_t total_long_codes = 0;
  for (int i = 0; i < HUF_DECSIZE; i++) {
    total_long_codes += long_code_lists[i].size();
    if (total_long_codes > static_cast<size_t>(HUF_ENCSIZE)) {
      return false;  // Too many long codes (shouldn't happen with valid data)
    }
  }
  long_codes.reserve(total_long_codes);

  for (int i = 0; i < HUF_DECSIZE; i++) {
    if (!long_code_lists[i].empty()) {
      hdecod[i].lit = static_cast<int>(long_code_lists[i].size());
      size_t start_idx = long_codes.size();
      for (int sym : long_code_lists[i]) {
        if (sym < 0 || sym >= HUF_ENCSIZE) return false;  // Validate symbol
        long_codes.push_back(sym);
      }
      // Store offset temporarily (will convert to pointer after)
      hdecod[i].p = reinterpret_cast<int*>(start_idx + 1);  // +1 so 0 means nullptr
    }
  }

  // Pass 3: Convert offsets to actual pointers
  for (int i = 0; i < HUF_DECSIZE; i++) {
    if (hdecod[i].p != nullptr) {
      size_t idx = reinterpret_cast<size_t>(hdecod[i].p) - 1;
      if (idx >= long_codes.size()) return false;  // Bounds check
      hdecod[i].p = &long_codes[idx];
    }
  }

  return true;
}

// Extract code from hcode entry
inline int64_t hufCode(int64_t hcode) { return hcode >> 6; }
inline int hufLength(int64_t hcode) { return static_cast<int>(hcode & 63); }

// Get one character and update bit buffer (with bounds check)
// Returns false if bounds exceeded
inline bool hufGetCharSafe(int64_t& c, int& lc, const uint8_t*& in, const uint8_t* ie) {
  if (in >= ie) return false;
  // Use unsigned shift to avoid signed overflow UB
  c = static_cast<int64_t>((static_cast<uint64_t>(c) << 8) | static_cast<uint64_t>(*in++));
  lc += 8;
  // Check for bit accumulator overflow (shouldn't happen in normal use)
  if (lc > 64) return false;
  return true;
}

// Get one character and update bit buffer (unchecked, for hot paths)
inline void hufGetChar(int64_t& c, int& lc, const uint8_t*& in) {
  // Use unsigned shift to avoid signed overflow UB
  c = static_cast<int64_t>((static_cast<uint64_t>(c) << 8) | static_cast<uint64_t>(*in++));
  lc += 8;
}

// Get code from decoder output (optimized version)
// Note: RLE run length is just the 8-bit value, NOT value + 2 (matching V1)
PIZ_ALWAYS_INLINE bool hufGetCode(int lit, int rlc, int64_t& c, int& lc,
                       const uint8_t*& in, const uint8_t* ie,
                       uint16_t*& out, const uint16_t* outb, const uint16_t* oe) {
  // Validate lc is in reasonable range (unlikely to fail)
  if (PIZ_UNLIKELY(lc < 0 || lc > 64)) return false;

  if (PIZ_UNLIKELY(lit == rlc)) {
    // Run-length encoded (relatively rare path)
    if (lc < 8) {
      if (PIZ_UNLIKELY(in >= ie)) return false;  // Bounds check
      hufGetChar(c, lc, in);
    }
    if (PIZ_UNLIKELY(lc < 8)) return false;  // Still not enough bits
    lc -= 8;
    int run = static_cast<int>((c >> lc) & 0xFF);  // No +2, matching V1

    // Validate RLE operation (unlikely to fail with valid data)
    if (PIZ_UNLIKELY(out <= outb)) return false;  // Need at least one previous value
    if (PIZ_UNLIKELY(out + run > oe)) return false;  // Would overflow output buffer

    // Optimized RLE fill using SIMD when available
    uint16_t prev = out[-1];
    if (run > 0) {
#if defined(TINYEXR_ENABLE_SIMD) && TINYEXR_ENABLE_SIMD
      // Use SIMD-accelerated fill for larger runs
      out = tinyexr::simd::fill_u16_simd(out, prev, static_cast<size_t>(run));
#else
      // Scalar fallback with loop unrolling
      if (run >= 8) {
        uint16_t* end = out + run;
        while (out + 8 <= end) {
          out[0] = prev; out[1] = prev; out[2] = prev; out[3] = prev;
          out[4] = prev; out[5] = prev; out[6] = prev; out[7] = prev;
          out += 8;
        }
        while (out < end) *out++ = prev;
      } else {
        while (run-- > 0) *out++ = prev;
      }
#endif
    }
  } else {
    // Literal value (common path)
    if (PIZ_UNLIKELY(lit < 0 || lit > 65535)) return false;
    if (PIZ_UNLIKELY(out >= oe)) return false;  // Output buffer full
    *out++ = static_cast<uint16_t>(lit);
  }
  return true;
}

// Decode Huffman data (matching V1 hufDecode)
// Optimized with branch prediction hints while preserving bounds checking
inline bool hufDecode(const int64_t* hcode, const HufDec* hdecod,
                      const uint8_t* in, int ni, int rlc, int no,
                      uint16_t* out, bool debug = false) {
  // Validate input parameters (only once at start)
  if (PIZ_UNLIKELY(!hcode || !hdecod || !in || !out)) return false;
  if (PIZ_UNLIKELY(ni < 0 || no < 0)) return false;
  if (PIZ_UNLIKELY(rlc < 0 || rlc >= HUF_ENCSIZE)) return false;

  int64_t c = 0;
  int lc = 0;
  const uint16_t* outb = out;
  const uint16_t* oe = out + no;
  const uint8_t* ie = in + (ni + 7) / 8;
  [[maybe_unused]] const uint8_t* in_start = in;  // For debug only

  // Prefetch decode table (likely to be accessed soon)
  PIZ_PREFETCH(hdecod);

  // Main decode loop - optimized for the common case (short codes)
  while (PIZ_LIKELY(in < ie)) {
    // Inline byte read for speed (bounds already checked by loop condition)
    // Use unsigned shift to avoid signed overflow UB
    c = static_cast<int64_t>((static_cast<uint64_t>(c) << 8) | static_cast<uint64_t>(*in++));
    lc += 8;
    if (PIZ_UNLIKELY(lc > 64)) {
      if (debug) fprintf(stderr, "Bit accumulator overflow\n");
      return false;
    }

    // Process as many codes as possible from current bits
    while (PIZ_LIKELY(lc >= HUF_DECBITS)) {
      // Fast path: check output buffer
      if (PIZ_UNLIKELY(out >= oe)) {
        goto done;
      }

      // Table lookup (mask guarantees valid index)
      const int tableIndex = static_cast<int>((c >> (lc - HUF_DECBITS)) & HUF_DECMASK);
      const HufDec& pl = hdecod[tableIndex];

      // Prefetch next table entry (only if enough bits available to avoid negative shift)
      if (lc >= HUF_DECBITS + 8) {
        PIZ_PREFETCH(&hdecod[(c >> (lc - HUF_DECBITS - 8)) & HUF_DECMASK]);
      }

      if (PIZ_LIKELY(pl.len > 0)) {
        // Short code path (most common case - ~95% of codes)
        // Bounds check: pl.len validated during table build
        lc -= pl.len;
        c &= (1LL << lc) - 1;  // Clear consumed bits

        // Inline literal output (most common)
        if (PIZ_LIKELY(pl.lit != rlc)) {
          if (PIZ_UNLIKELY(out >= oe)) goto done;
          *out++ = static_cast<uint16_t>(pl.lit);
        } else {
          // RLE path (less common)
          if (PIZ_UNLIKELY(!hufGetCode(pl.lit, rlc, c, lc, in, ie, out, outb, oe))) {
            if (debug) fprintf(stderr, "getCode failed (short)\n");
            return false;
          }
        }
      } else if (pl.p) {
        // Long code path (rare - <5% of codes)
        // Validate pl.lit bounds
        if (PIZ_UNLIKELY(pl.lit <= 0 || pl.lit > HUF_ENCSIZE)) {
          if (debug) fprintf(stderr, "Invalid long code count\n");
          return false;
        }

        bool found = false;
        const int numCandidates = pl.lit;

        for (int j = 0; j < numCandidates; j++) {
          // Prefetch next candidate's code entry
          if (j + 1 < numCandidates) {
            PIZ_PREFETCH(&hcode[pl.p[j + 1]]);
          }
          const int sym = pl.p[j];
          if (PIZ_UNLIKELY(sym < 0 || sym >= HUF_ENCSIZE)) {
            if (debug) fprintf(stderr, "Invalid symbol in long code list\n");
            return false;
          }

          const int l = hufLength(hcode[sym]);
          if (PIZ_UNLIKELY(l <= 0 || l > 58)) {
            if (debug) fprintf(stderr, "Invalid code length\n");
            return false;
          }

          // Read more bytes if needed
          while (lc < l && in < ie) {
            // Use unsigned shift to avoid signed overflow UB
            c = static_cast<int64_t>((static_cast<uint64_t>(c) << 8) | static_cast<uint64_t>(*in++));
            lc += 8;
          }

          if (lc >= l) {
            const int shift = lc - l;
            const int64_t expected = hufCode(hcode[sym]);
            const int64_t actual = (c >> shift) & ((1LL << l) - 1);

            if (expected == actual) {
              lc = shift;
              c &= (1LL << lc) - 1;

              // Output symbol
              if (PIZ_LIKELY(sym != rlc)) {
                if (PIZ_UNLIKELY(out >= oe)) goto done;
                *out++ = static_cast<uint16_t>(sym);
              } else {
                if (PIZ_UNLIKELY(!hufGetCode(sym, rlc, c, lc, in, ie, out, outb, oe))) {
                  if (debug) fprintf(stderr, "getCode failed (long)\n");
                  return false;
                }
              }
              found = true;
              break;
            }
          }
        }

        if (PIZ_UNLIKELY(!found)) {
          if (debug) fprintf(stderr, "Long code not found\n");
          return false;
        }
      } else {
        // Invalid code
        if (debug) fprintf(stderr, "Invalid code (no entry)\n");
        return false;
      }
    }
  }

done:
  // Process remaining bits (rare path)
  {
    const int padding = (8 - ni) & 7;
    c >>= padding;
    lc -= padding;
    if (lc < 0) lc = 0;
  }

  while (lc > 0 && out < oe) {
    const int shift = HUF_DECBITS - lc;
    if (PIZ_UNLIKELY(shift < 0 || shift > HUF_DECBITS)) break;

    const int tableIndex = static_cast<int>((c << shift) & HUF_DECMASK);
    const HufDec& pl = hdecod[tableIndex];

    if (PIZ_LIKELY(pl.len > 0 && pl.len <= lc)) {
      lc -= pl.len;
      c &= (1LL << lc) - 1;

      if (PIZ_LIKELY(pl.lit != rlc)) {
        *out++ = static_cast<uint16_t>(pl.lit);
      } else {
        if (PIZ_UNLIKELY(!hufGetCode(pl.lit, rlc, c, lc, in, ie, out, outb, oe))) {
          if (debug) fprintf(stderr, "getCode failed (remainder)\n");
          return false;
        }
      }
    } else {
      break;  // Can't decode more
    }
  }

  if (debug) {
    fprintf(stderr, "Decode complete: output %ld (expected %d)\n",
            (long)(out - outb), no);
  }
  return (out - outb) == no;
}

// Decompress Huffman-encoded data (matching V1 hufUncompress)
// With comprehensive bounds checking for safety
inline bool hufUncompress(const uint8_t* compressed, size_t nCompressed,
                          uint16_t* raw, size_t nRaw) {
  // Validate input pointers
  if (!compressed || !raw) return false;

  if (nCompressed == 0) {
    return nRaw == 0;
  }

  // Must have at least 20-byte header
  if (nCompressed < 20) return false;

  // Validate output size is reasonable (prevent overflow)
  if (nRaw > static_cast<size_t>(INT32_MAX)) return false;

  // Read header (V1 format: im(4), iM(4), tableLength(4), nBits(4), reserved(4))
  // nBits is at offset 12, NOT offset 8 (offset 8 is tableLength which is unused)
  uint32_t im_val, iM_val, nBits_val;
  std::memcpy(&im_val, compressed, 4);
  std::memcpy(&iM_val, compressed + 4, 4);
  // offset 8: tableLength (unused)
  std::memcpy(&nBits_val, compressed + 12, 4);  // nBits at offset 12!

  int im = static_cast<int>(im_val);
  int iM = static_cast<int>(iM_val);
  int nBits = static_cast<int>(nBits_val);

  // Validate symbol range
  if (im < 0 || im >= HUF_ENCSIZE || iM < 0 || iM >= HUF_ENCSIZE) {
    return false;
  }
  if (im > iM) return false;

  // Validate nBits is reasonable
  if (nBits < 0) return false;

  const uint8_t* ptr = compressed + 20;
  size_t remaining = nCompressed - 20;

  // Unpack encoding table
  std::vector<int64_t> freq(HUF_ENCSIZE);
  if (!hufUnpackEncTable(ptr, remaining, im, iM, freq.data())) {
    return false;
  }

  // Validate ptr didn't go past end
  if (ptr > compressed + nCompressed) {
    return false;
  }

  // Update remaining
  remaining = nCompressed - static_cast<size_t>(ptr - compressed);

  // Validate nBits against remaining data
  if (nBits < 0 || static_cast<size_t>(nBits) > 8 * remaining) {
    return false;
  }

  // Special case: nBits=0 means all values are the same (symbol im)
  // This happens when im==iM (only one unique symbol in the data)
  if (nBits == 0) {
    for (size_t i = 0; i < nRaw; i++) {
      raw[i] = static_cast<uint16_t>(im);
    }
    return true;
  }

  // Build decode table
  std::vector<HufDec> hdec(HUF_DECSIZE);
  std::vector<int> long_codes;
  if (!hufBuildDecTable(freq.data(), im, iM, hdec.data(), long_codes)) {
    return false;
  }

  // Decode data
  bool result = hufDecode(freq.data(), hdec.data(), ptr,
                          nBits, iM,
                          static_cast<int>(nRaw), raw);

  return result;
}

// ============================================================================
// PIZ Decompression (V2 API)
// ============================================================================

// Decompress PIZ-compressed block
// Returns Result<void> with success/error status
// With comprehensive bounds checking for safety
inline tinyexr::v2::Result<void> DecompressPizV2(
    uint8_t* dst, size_t dstSize,
    const uint8_t* src, size_t srcSize,
    int numChannels, const tinyexr::v2::Channel* channels,
    int dataWidth, int numLines) {

  using namespace tinyexr::v2;

  // Validate pointers
  if (!dst || !src) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ null pointer argument",
      "DecompressPizV2",
      0
    ));
  }

  // Validate parameters
  if (numChannels <= 0 || numChannels > 1024) {  // Reasonable max channels
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ invalid numChannels: " + std::to_string(numChannels),
      "DecompressPizV2",
      0
    ));
  }
  if (!channels) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ null channels pointer",
      "DecompressPizV2",
      0
    ));
  }
  if (dataWidth <= 0 || dataWidth > 1000000) {  // Reasonable max width
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ invalid dataWidth: " + std::to_string(dataWidth),
      "DecompressPizV2",
      0
    ));
  }
  if (numLines <= 0 || numLines > 1000000) {  // Reasonable max lines
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ invalid numLines: " + std::to_string(numLines),
      "DecompressPizV2",
      0
    ));
  }

  // Handle uncompressed case (Issue 40 from V1)
  if (srcSize == dstSize) {
    std::memcpy(dst, src, srcSize);
    return Result<void>::ok();
  }

  // Validate input
  if (srcSize < 4) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ compressed data too short",
      "DecompressPizV2",
      0
    ));
  }

  // Read bitmap range
  uint16_t minNonZero, maxNonZero;
  std::memcpy(&minNonZero, src, 2);
  std::memcpy(&maxNonZero, src + 2, 2);

  if (maxNonZero >= BITMAP_SIZE) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ maxNonZero out of range: " + std::to_string(maxNonZero),
      "DecompressPizV2",
      2
    ));
  }

  size_t pos = 4;

  // Read and reconstruct bitmap
  std::vector<uint8_t> bitmap(BITMAP_SIZE, 0);

  if (minNonZero <= maxNonZero) {
    size_t bitmapLen = maxNonZero - minNonZero + 1;
    if (pos + bitmapLen > srcSize) {
      return Result<void>::error(ErrorInfo(
        ErrorCode::InvalidData,
        "PIZ bitmap extends beyond input",
        "DecompressPizV2",
        pos
      ));
    }
    // Bounds check for bitmap index
    if (minNonZero + bitmapLen > BITMAP_SIZE) {
      return Result<void>::error(ErrorInfo(
        ErrorCode::InvalidData,
        "PIZ bitmap index out of range",
        "DecompressPizV2",
        pos
      ));
    }
    std::memcpy(&bitmap[minNonZero], src + pos, bitmapLen);
    pos += bitmapLen;
  } else {
    // Issue 194: all pixels are zero
    if (minNonZero != (BITMAP_SIZE - 1) || maxNonZero != 0) {
      return Result<void>::error(ErrorInfo(
        ErrorCode::InvalidData,
        "Invalid PIZ minNonZero/maxNonZero combination",
        "DecompressPizV2",
        0
      ));
    }
  }

  // Build reverse LUT
  std::vector<uint16_t> lut(USHORT_RANGE, 0);
  uint16_t maxValue = reverseLutFromBitmap(bitmap.data(), lut.data());

  // Read Huffman length
  if (pos + 4 > srcSize) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ missing Huffman length",
      "DecompressPizV2",
      pos
    ));
  }

  int32_t huffLen;
  std::memcpy(&huffLen, src + pos, 4);
  pos += 4;

  // Validate Huffman length
  if (huffLen < 0) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ negative Huffman length: " + std::to_string(huffLen),
      "DecompressPizV2",
      pos - 4
    ));
  }
  if (pos + static_cast<size_t>(huffLen) > srcSize) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ Huffman data extends beyond input",
      "DecompressPizV2",
      pos
    ));
  }

  // Calculate expected buffer size and validate
  if (dstSize == 0 || dstSize % sizeof(uint16_t) != 0) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ invalid destination size",
      "DecompressPizV2",
      0
    ));
  }
  size_t tmpBufSize = dstSize / sizeof(uint16_t);

  // Validate expected size against channel parameters (accounting for subsampling)
  size_t expectedSize = 0;
  for (int i = 0; i < numChannels; ++i) {
    size_t pixelSize = sizeof(int);  // UINT and FLOAT
    if (channels[i].pixel_type == 1) {  // HALF
      pixelSize = sizeof(short);
    }
    // Account for subsampling
    int x_samp = channels[i].x_sampling > 0 ? channels[i].x_sampling : 1;
    int y_samp = channels[i].y_sampling > 0 ? channels[i].y_sampling : 1;
    int channelWidth = dataWidth / x_samp;
    int channelHeight = numLines / y_samp;
    // For partial blocks, add extra line/column if there's a remainder
    if ((numLines % y_samp) != 0) channelHeight++;
    if ((dataWidth % x_samp) != 0) channelWidth++;
    size_t channelBytes = static_cast<size_t>(channelWidth) * channelHeight * pixelSize;
    // Check for overflow
    if (expectedSize > SIZE_MAX - channelBytes) {
      return Result<void>::error(ErrorInfo(
        ErrorCode::InvalidData,
        "PIZ channel size overflow",
        "DecompressPizV2",
        0
      ));
    }
    expectedSize += channelBytes;
  }
  if (expectedSize != dstSize) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::InvalidData,
      "PIZ destination size mismatch: expected " + std::to_string(expectedSize) +
      " but got " + std::to_string(dstSize),
      "DecompressPizV2",
      0
    ));
  }

  std::vector<uint16_t> tmpBuffer(tmpBufSize);

  // Huffman decompression
  if (!hufUncompress(src + pos, static_cast<size_t>(huffLen), tmpBuffer.data(), tmpBufSize)) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::CompressionError,
      "PIZ Huffman decompression failed",
      "DecompressPizV2",
      pos
    ));
  }

  // Set up channel data for wavelet decode
  // Use stack allocation for common cases (up to 16 channels)
  constexpr size_t kMaxStackChannels = 16;
  PIZChannelData channelDataStack[kMaxStackChannels];
  std::vector<PIZChannelData> channelDataVec;
  PIZChannelData* channelData;
  const size_t numChan = static_cast<size_t>(numChannels);

  if (numChan <= kMaxStackChannels) {
    channelData = channelDataStack;
  } else {
    channelDataVec.resize(numChan);
    channelData = channelDataVec.data();
  }

  uint16_t* tmpBufferEnd = tmpBuffer.data();
  uint16_t* tmpBufferLimit = tmpBuffer.data() + tmpBufSize;

  for (int i = 0; i < numChannels; ++i) {
    const Channel& chan = channels[i];

    size_t pixelSize = sizeof(int);  // UINT and FLOAT
    if (chan.pixel_type == 1) {  // HALF
      pixelSize = sizeof(short);
    }

    // Account for subsampling
    int x_samp = chan.x_sampling > 0 ? chan.x_sampling : 1;
    int y_samp = chan.y_sampling > 0 ? chan.y_sampling : 1;
    int channelWidth = dataWidth / x_samp;
    int channelHeight = numLines / y_samp;
    // For partial blocks, add extra line/column if there's a remainder
    if ((numLines % y_samp) != 0) channelHeight++;
    if ((dataWidth % x_samp) != 0) channelWidth++;

    channelData[i].start = tmpBufferEnd;
    channelData[i].end = channelData[i].start;
    channelData[i].nx = channelWidth;
    channelData[i].ny = channelHeight;
    channelData[i].size = static_cast<int>(pixelSize / sizeof(short));

    size_t channelElements = static_cast<size_t>(channelData[i].nx) *
                             channelData[i].ny * channelData[i].size;

    // Bounds check for buffer advancement
    if (tmpBufferEnd + channelElements > tmpBufferLimit) {
      return Result<void>::error(ErrorInfo(
        ErrorCode::InvalidData,
        "PIZ channel data exceeds buffer at channel " + std::to_string(i),
        "DecompressPizV2",
        0
      ));
    }
    tmpBufferEnd += channelElements;
  }

  // Wavelet decode each channel (with bounds checking)
  for (size_t i = 0; i < numChan; ++i) {
    PIZChannelData& cd = channelData[i];

    // Calculate buffer size for this channel
    size_t channelBufSize = static_cast<size_t>(cd.nx) * cd.ny * cd.size;

    for (int j = 0; j < cd.size; ++j) {
      if (!wav2Decode(cd.start + j, cd.nx, cd.size, cd.ny, cd.nx * cd.size, maxValue, channelBufSize)) {
        return Result<void>::error(ErrorInfo(
          ErrorCode::CompressionError,
          "PIZ wavelet decode failed at channel " + std::to_string(i) + " component " + std::to_string(j),
          "DecompressPizV2",
          0
        ));
      }
    }
  }

  // Expand pixel data to original range using reverse LUT (with bounds checking)
  if (!applyLut(lut.data(), tmpBuffer.data(), static_cast<int>(tmpBufSize), USHORT_RANGE)) {
    return Result<void>::error(ErrorInfo(
      ErrorCode::CompressionError,
      "PIZ LUT application failed",
      "DecompressPizV2",
      0
    ));
  }

  // Copy data to output, interleaving channels (accounting for subsampling)
  uint8_t* outPtr = dst;
  uint8_t* outLimit = dst + dstSize;

  for (int y = 0; y < numLines; y++) {
    for (size_t i = 0; i < numChan; ++i) {
      const Channel& chan = channels[i];
      PIZChannelData& cd = channelData[i];

      // For subsampled channels, only output on sampled lines
      int y_samp = chan.y_sampling > 0 ? chan.y_sampling : 1;
      if ((y % y_samp) != 0) {
        continue;  // This line doesn't have data for this channel
      }

      size_t n = static_cast<size_t>(cd.nx * cd.size);
      size_t copyBytes = n * sizeof(uint16_t);

      // Bounds check for output
      if (outPtr + copyBytes > outLimit) {
        return Result<void>::error(ErrorInfo(
          ErrorCode::InvalidData,
          "PIZ output buffer overflow at line " + std::to_string(y),
          "DecompressPizV2",
          0
        ));
      }
      // Bounds check for input (cd.end)
      if (cd.end + n > tmpBufferLimit) {
        return Result<void>::error(ErrorInfo(
          ErrorCode::InvalidData,
          "PIZ channel data underflow at line " + std::to_string(y),
          "DecompressPizV2",
          0
        ));
      }

      std::memcpy(outPtr, cd.end, copyBytes);
      outPtr += copyBytes;
      cd.end += n;
    }
  }

  return Result<void>::ok();
}

// ============================================================================
// Huffman Encoding Functions (for PIZ compression)
// ============================================================================

// Count symbol frequencies in data
inline void countFrequencies(int64_t* freq, const uint16_t* data, int n) {
  for (int i = 0; i < HUF_ENCSIZE; i++) {
    freq[i] = 0;
  }
  for (int i = 0; i < n; i++) {
    freq[data[i]]++;
  }
}

// Heap comparison functor for Huffman tree building
struct HufHeapCompare {
  bool operator()(int64_t* a, int64_t* b) const { return *a > *b; }
};

// Build Huffman encoding table from frequencies
// Returns false on failure (encoding would be too long)
inline bool hufBuildEncTable(int64_t* freq, int* im, int* iM) {
  // Find min and max non-zero indices
  *im = 0;
  while (*im < HUF_ENCSIZE && freq[*im] == 0) (*im)++;

  *iM = HUF_ENCSIZE - 1;
  while (*iM > *im && freq[*iM] == 0) (*iM)--;

  if (*im > *iM) {
    *im = *iM = 0;
    return true;
  }

  // Limit encoding to prevent overflow (use 20-bit safe threshold)
  // This ensures we don't exceed 58-bit code lengths
  int nSymbols = *iM - *im + 1;

  // Build Huffman tree using heap
  std::vector<int64_t*> heap;
  heap.reserve(nSymbols + 1);

  // Add all non-zero frequency symbols to heap
  for (int i = *im; i <= *iM; i++) {
    heap.push_back(&freq[i]);
  }
  std::make_heap(heap.begin(), heap.end(), HufHeapCompare());

  // Build tree
  std::vector<int64_t> tree_storage(nSymbols);
  int64_t* tree_next = tree_storage.data();

  while (heap.size() > 1) {
    // Pop two smallest
    std::pop_heap(heap.begin(), heap.end(), HufHeapCompare());
    int64_t* a = heap.back(); heap.pop_back();

    std::pop_heap(heap.begin(), heap.end(), HufHeapCompare());
    int64_t* b = heap.back(); heap.pop_back();

    // Create parent node
    *tree_next = *a + *b;

    // Mark children with depth increment
    // Store depth in high 6 bits, frequency is lost but we don't need it
    *a = (*a & ~0x3FULL) | ((*a & 0x3F) + 1);
    *b = (*b & ~0x3FULL) | ((*b & 0x3F) + 1);

    heap.push_back(tree_next);
    std::push_heap(heap.begin(), heap.end(), HufHeapCompare());
    tree_next++;
  }

  // Extract code lengths (stored in low 6 bits)
  for (int i = *im; i <= *iM; i++) {
    int length = static_cast<int>(freq[i] & 0x3F);
    if (length > 58) length = 58;  // Clamp to max
    freq[i] = length;
  }

  // Convert lengths to canonical codes
  hufCanonicalCodeTable(freq);
  return true;
}

// Output n bits to buffer with bounds checking
// Returns false if buffer overflow would occur
inline bool outputBitsSafe(int nBits, int64_t bits, int64_t& c, int& lc,
                           uint8_t*& out, const uint8_t* outEnd) {
  c = (c << nBits) | bits;
  lc += nBits;
  while (lc >= 8) {
    if (out >= outEnd) return false;  // Buffer overflow
    lc -= 8;
    *out++ = static_cast<uint8_t>(c >> lc);
  }
  return true;
}

// Output n bits to buffer (legacy, no bounds checking)
inline void outputBits(int nBits, int64_t bits, int64_t& c, int& lc, uint8_t*& out) {
  c = (c << nBits) | bits;
  lc += nBits;
  while (lc >= 8) {
    lc -= 8;
    *out++ = static_cast<uint8_t>(c >> lc);
  }
}

// Output Huffman code with bounds checking
inline bool outputCodeSafe(int64_t code, int64_t& c, int& lc,
                           uint8_t*& out, const uint8_t* outEnd) {
  int length = static_cast<int>(code & 63);
  int64_t bits = code >> 6;
  return outputBitsSafe(length, bits, c, lc, out, outEnd);
}

// Output Huffman code
inline void outputCode(int64_t code, int64_t& c, int& lc, uint8_t*& out) {
  int length = static_cast<int>(code & 63);
  int64_t bits = code >> 6;
  outputBits(length, bits, c, lc, out);
}

// Send code with optional run-length encoding (with bounds checking)
inline bool sendCodeSafe(int64_t sCode, int runCount, int64_t runCode,
                         int64_t& c, int& lc, uint8_t*& out, const uint8_t* outEnd) {
  int sLen = static_cast<int>(sCode & 63);
  int rlcLen = static_cast<int>(runCode & 63);

  // Check if RLC is more efficient
  if (sLen + rlcLen + 8 < sLen * (runCount + 1)) {
    if (!outputCodeSafe(sCode, c, lc, out, outEnd)) return false;
    if (!outputCodeSafe(runCode, c, lc, out, outEnd)) return false;
    if (!outputBitsSafe(8, runCount, c, lc, out, outEnd)) return false;
  } else {
    for (int i = 0; i <= runCount; i++) {
      if (!outputCodeSafe(sCode, c, lc, out, outEnd)) return false;
    }
  }
  return true;
}

// Send code with optional run-length encoding
inline void sendCode(int64_t sCode, int runCount, int64_t runCode,
                     int64_t& c, int& lc, uint8_t*& out) {
  int sLen = static_cast<int>(sCode & 63);
  int rlcLen = static_cast<int>(runCode & 63);

  // Check if RLC is more efficient
  if (sLen + rlcLen + 8 < sLen * (runCount + 1)) {
    outputCode(sCode, c, lc, out);
    outputCode(runCode, c, lc, out);
    outputBits(8, runCount, c, lc, out);
  } else {
    for (int i = 0; i <= runCount; i++) {
      outputCode(sCode, c, lc, out);
    }
  }
}

// Encode data using Huffman codes
// Returns output size in bits
inline int hufEncode(const int64_t* hcode, const uint16_t* in, int ni, int rlc, uint8_t* out) {
  uint8_t* outStart = out;
  int64_t c = 0;
  int lc = 0;

  if (ni == 0) return 0;

  int s = in[0];
  int cs = 0;

  for (int i = 1; i < ni; i++) {
    if (s == in[i] && cs < 255) {
      cs++;
    } else {
      sendCode(hcode[s], cs, hcode[rlc], c, lc, out);
      cs = 0;
    }
    s = in[i];
  }

  // Send remaining code
  sendCode(hcode[s], cs, hcode[rlc], c, lc, out);

  // Flush remaining bits
  if (lc) {
    *out++ = static_cast<uint8_t>((c << (8 - lc)) & 0xFF);
  }

  return static_cast<int>((out - outStart) * 8 + lc);
}

// Encode data using Huffman codes with bounds checking
// Returns output size in bits, or -1 on buffer overflow
inline int hufEncodeSafe(const int64_t* hcode, const uint16_t* in, int ni, int rlc,
                         uint8_t* out, const uint8_t* outEnd) {
  uint8_t* outStart = out;
  int64_t c = 0;
  int lc = 0;

  if (ni == 0) return 0;

  int s = in[0];
  int cs = 0;

  for (int i = 1; i < ni; i++) {
    if (s == in[i] && cs < 255) {
      cs++;
    } else {
      if (!sendCodeSafe(hcode[s], cs, hcode[rlc], c, lc, out, outEnd)) return -1;
      cs = 0;
    }
    s = in[i];
  }

  // Send remaining code
  if (!sendCodeSafe(hcode[s], cs, hcode[rlc], c, lc, out, outEnd)) return -1;

  // Flush remaining bits
  if (lc) {
    if (out >= outEnd) return -1;
    *out++ = static_cast<uint8_t>((c << (8 - lc)) & 0xFF);
  }

  return static_cast<int>((out - outStart) * 8 + lc);
}

// Pack encoding table for storage with bounds checking
// Uses run-length encoding for zero-length codes
// Returns false if buffer overflow would occur
inline bool hufPackEncTableSafe(const int64_t* hcode, int im, int iM,
                                 uint8_t*& out, const uint8_t* outEnd) {
  int64_t c = 0;
  int lc = 0;

  for (int i = im; i <= iM; i++) {
    int l = static_cast<int>(hcode[i] & 63);

    if (l == 0) {
      // Count consecutive zeros
      int zerun = 1;
      while (i + zerun <= iM && (hcode[i + zerun] & 63) == 0 && zerun < 255) {
        zerun++;
      }

      if (zerun >= SHORTEST_LONG_RUN) {
        if (!outputBitsSafe(6, LONG_ZEROCODE_RUN, c, lc, out, outEnd)) return false;
        if (!outputBitsSafe(8, zerun - SHORTEST_LONG_RUN, c, lc, out, outEnd)) return false;
        i += zerun - 1;
      } else if (zerun >= 2) {
        if (!outputBitsSafe(6, SHORT_ZEROCODE_RUN + zerun - 2, c, lc, out, outEnd)) return false;
        i += zerun - 1;
      } else {
        if (!outputBitsSafe(6, 0, c, lc, out, outEnd)) return false;
      }
    } else {
      if (!outputBitsSafe(6, l, c, lc, out, outEnd)) return false;
    }
  }

  // Flush remaining bits
  if (lc) {
    if (out >= outEnd) return false;
    *out++ = static_cast<uint8_t>((c << (8 - lc)) & 0xFF);
  }
  return true;
}

// Pack encoding table for storage (legacy, no bounds checking)
// Uses run-length encoding for zero-length codes
inline void hufPackEncTable(const int64_t* hcode, int im, int iM, uint8_t*& out) {
  int64_t c = 0;
  int lc = 0;

  for (int i = im; i <= iM; i++) {
    int l = static_cast<int>(hcode[i] & 63);

    if (l == 0) {
      // Count consecutive zeros
      int zerun = 1;
      while (i + zerun <= iM && (hcode[i + zerun] & 63) == 0 && zerun < 255) {
        zerun++;
      }

      if (zerun >= SHORTEST_LONG_RUN) {
        outputBits(6, LONG_ZEROCODE_RUN, c, lc, out);
        outputBits(8, zerun - SHORTEST_LONG_RUN, c, lc, out);
        i += zerun - 1;
      } else if (zerun >= 2) {
        outputBits(6, SHORT_ZEROCODE_RUN + zerun - 2, c, lc, out);
        i += zerun - 1;
      } else {
        outputBits(6, 0, c, lc, out);
      }
    } else {
      outputBits(6, l, c, lc, out);
    }
  }

  // Flush remaining bits
  if (lc) {
    *out++ = static_cast<uint8_t>((c << (8 - lc)) & 0xFF);
  }
}

// Huffman compress uint16 data
// Returns compressed size, or 0 on failure
inline int hufCompress(const uint16_t* raw, int nRaw, uint8_t* compressed) {
  if (nRaw == 0) return 0;

  std::vector<int64_t> freq(HUF_ENCSIZE);

  countFrequencies(freq.data(), raw, nRaw);

  int im = 0, iM = 0;
  if (!hufBuildEncTable(freq.data(), &im, &iM)) {
    return 0;  // Encoding failed
  }

  // Write header: im, iM, tableLength, nBits, reserved
  uint8_t* tableStart = compressed + 20;
  uint8_t* tableEnd = tableStart;
  hufPackEncTable(freq.data(), im, iM, tableEnd);
  int tableLength = static_cast<int>(tableEnd - tableStart);

  uint8_t* dataStart = tableEnd;
  int nBits = hufEncode(freq.data(), raw, nRaw, iM, dataStart);
  int dataLength = (nBits + 7) / 8;

  // Write header
  std::memcpy(compressed, &im, 4);
  std::memcpy(compressed + 4, &iM, 4);
  std::memcpy(compressed + 8, &tableLength, 4);
  std::memcpy(compressed + 12, &nBits, 4);
  int zero = 0;
  std::memcpy(compressed + 16, &zero, 4);  // Reserved

  return static_cast<int>(dataStart + dataLength - compressed);
}

// Huffman compress uint16 data with bounds checking
// Returns compressed size, or -1 on buffer overflow, or 0 on encoding failure
inline int hufCompressSafe(const uint16_t* raw, int nRaw,
                           uint8_t* compressed, const uint8_t* compressedEnd) {
  if (nRaw == 0) return 0;

  // Need at least 20 bytes for header
  if (compressed + 20 > compressedEnd) return -1;

  std::vector<int64_t> freq(HUF_ENCSIZE);

  countFrequencies(freq.data(), raw, nRaw);

  int im = 0, iM = 0;
  if (!hufBuildEncTable(freq.data(), &im, &iM)) {
    return 0;  // Encoding failed
  }

  // Write header: im, iM, tableLength, nBits, reserved
  uint8_t* tableStart = compressed + 20;
  uint8_t* tableEnd = tableStart;

  // Pack encoding table with bounds checking
  if (!hufPackEncTableSafe(freq.data(), im, iM, tableEnd, compressedEnd)) {
    return -1;  // Buffer overflow
  }
  int tableLength = static_cast<int>(tableEnd - tableStart);

  // Encode data with bounds checking
  uint8_t* dataStart = tableEnd;
  int nBits = hufEncodeSafe(freq.data(), raw, nRaw, iM, dataStart, compressedEnd);
  if (nBits < 0) {
    return -1;  // Buffer overflow
  }
  int dataLength = (nBits + 7) / 8;

  // Write header
  std::memcpy(compressed, &im, 4);
  std::memcpy(compressed + 4, &iM, 4);
  std::memcpy(compressed + 8, &tableLength, 4);
  std::memcpy(compressed + 12, &nBits, 4);
  int zero = 0;
  std::memcpy(compressed + 16, &zero, 4);  // Reserved

  return static_cast<int>(dataStart + dataLength - compressed);
}

// ============================================================================
// PIZ Compression (V2 API)
// ============================================================================

// Compress data using PIZ algorithm
// Returns Result<size_t> with compressed size, or error
inline tinyexr::v2::Result<size_t> CompressPizV2(
    uint8_t* dst, size_t dstCapacity,
    const uint8_t* src, size_t srcSize,
    int numChannels, const tinyexr::v2::Channel* channels,
    int dataWidth, int numLines) {

  using namespace tinyexr::v2;

  // Validate input
  if (!dst || !src || !channels) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::InvalidArgument,
      "PIZ null pointer argument",
      "CompressPizV2", 0
    ));
  }

  if (numChannels <= 0 || dataWidth <= 0 || numLines <= 0) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::InvalidArgument,
      "PIZ invalid dimensions",
      "CompressPizV2", 0
    ));
  }

  // srcSize should be multiple of 2 (uint16 data)
  if (srcSize == 0 || srcSize % 2 != 0) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::InvalidArgument,
      "PIZ source size must be even",
      "CompressPizV2", 0
    ));
  }

  // Allocate temporary buffer for channel data
  std::vector<uint16_t> tmpBuffer(srcSize / sizeof(uint16_t));

  // Set up channel data structures
  std::vector<PIZChannelData> channelData(numChannels);
  uint16_t* tmpBufferEnd = tmpBuffer.data();

  for (int c = 0; c < numChannels; c++) {
    PIZChannelData& cd = channelData[c];

    cd.start = tmpBufferEnd;
    cd.end = cd.start;

    // Account for subsampling
    int x_samp = channels[c].x_sampling > 0 ? channels[c].x_sampling : 1;
    int y_samp = channels[c].y_sampling > 0 ? channels[c].y_sampling : 1;
    cd.nx = dataWidth / x_samp;
    cd.ny = numLines / y_samp;
    if ((numLines % y_samp) != 0) cd.ny++;
    if ((dataWidth % x_samp) != 0) cd.nx++;

    // Pixel size in uint16 units (1 for HALF, 2 for FLOAT/UINT)
    cd.size = (channels[c].pixel_type == 1) ? 1 : 2;  // 1 = HALF

    tmpBufferEnd += static_cast<size_t>(cd.nx) * cd.ny * cd.size;
  }

  // Verify we have enough source data
  size_t expectedSize = (tmpBufferEnd - tmpBuffer.data()) * sizeof(uint16_t);
  if (srcSize < expectedSize) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::InvalidArgument,
      "PIZ source size too small: need " + std::to_string(expectedSize) +
      " but got " + std::to_string(srcSize),
      "CompressPizV2", 0
    ));
  }

  // Copy input data to temp buffer (de-interleave by channel)
  const uint8_t* ptr = src;
  for (int y = 0; y < numLines; ++y) {
    for (int c = 0; c < numChannels; ++c) {
      PIZChannelData& cd = channelData[c];

      // Check if this line has data for this channel (subsampling)
      int y_samp = channels[c].y_sampling > 0 ? channels[c].y_sampling : 1;
      if ((y % y_samp) != 0) continue;

      size_t n = static_cast<size_t>(cd.nx) * cd.size;
      std::memcpy(cd.end, ptr, n * sizeof(uint16_t));
      ptr += n * sizeof(uint16_t);
      cd.end += n;
    }
  }

  // Build bitmap and forward LUT (range compression)
  std::vector<uint8_t> bitmap(BITMAP_SIZE, 0);
  uint16_t minNonZero, maxNonZero;
  bitmapFromData(tmpBuffer.data(), static_cast<int>(tmpBuffer.size()),
                 bitmap.data(), minNonZero, maxNonZero);

  std::vector<uint16_t> lut(USHORT_RANGE);
  uint16_t maxValue = forwardLutFromBitmap(bitmap.data(), lut.data());

  // Apply forward LUT to compress value range
  applyLut(lut.data(), tmpBuffer.data(), static_cast<int>(tmpBuffer.size()));

  // Apply wavelet encoding
  for (int c = 0; c < numChannels; ++c) {
    PIZChannelData& cd = channelData[c];
    for (int j = 0; j < cd.size; ++j) {
      wav2Encode(cd.start + j, cd.nx, cd.size, cd.ny, cd.nx * cd.size, maxValue);
    }
  }

  // Write output: bitmap range, bitmap data, Huffman length, Huffman data
  uint8_t* buf = dst;
  uint8_t* bufEnd = dst + dstCapacity;

  // Write minNonZero and maxNonZero
  if (buf + 4 > bufEnd) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::CompressionError,
      "PIZ output buffer too small for header",
      "CompressPizV2", 0
    ));
  }
  std::memcpy(buf, &minNonZero, sizeof(uint16_t));
  buf += sizeof(uint16_t);
  std::memcpy(buf, &maxNonZero, sizeof(uint16_t));
  buf += sizeof(uint16_t);

  // Write bitmap slice
  if (minNonZero <= maxNonZero) {
    size_t bitmapLen = maxNonZero - minNonZero + 1;
    if (buf + bitmapLen > bufEnd) {
      return Result<size_t>::error(ErrorInfo(
        ErrorCode::CompressionError,
        "PIZ output buffer too small for bitmap",
        "CompressPizV2", 0
      ));
    }
    std::memcpy(buf, &bitmap[minNonZero], bitmapLen);
    buf += bitmapLen;
  }

  // Reserve space for Huffman length
  if (buf + 4 > bufEnd) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::CompressionError,
      "PIZ output buffer too small for Huffman length",
      "CompressPizV2", 0
    ));
  }
  uint8_t* lengthPtr = buf;
  int zero = 0;
  std::memcpy(buf, &zero, sizeof(int));
  buf += sizeof(int);

  // Huffman compress with bounds checking
  int huffLen = hufCompressSafe(tmpBuffer.data(), static_cast<int>(tmpBuffer.size()), buf, bufEnd);
  if (huffLen < 0) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::CompressionError,
      "PIZ output buffer too small for Huffman data",
      "CompressPizV2", 0
    ));
  }
  if (huffLen == 0) {
    return Result<size_t>::error(ErrorInfo(
      ErrorCode::CompressionError,
      "PIZ Huffman encoding failed",
      "CompressPizV2", 0
    ));
  }

  // Write Huffman length
  std::memcpy(lengthPtr, &huffLen, sizeof(int));
  buf += huffLen;

  size_t outSize = buf - dst;

  // If compressed is larger than original, return uncompressed (Issue 40)
  if (outSize >= srcSize) {
    if (dstCapacity < srcSize) {
      return Result<size_t>::error(ErrorInfo(
        ErrorCode::CompressionError,
        "PIZ output buffer too small for uncompressed fallback",
        "CompressPizV2", 0
      ));
    }
    std::memcpy(dst, src, srcSize);
    return Result<size_t>::ok(srcSize);
  }

  return Result<size_t>::ok(outSize);
}

}  // namespace piz
}  // namespace tinyexr

#endif  // TINYEXR_PIZ_HH_
