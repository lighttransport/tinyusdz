// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - SIMD scanning helpers (implementation)

#include "simd-scan.hh"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <emmintrin.h>  // SSE2 (baseline on x86-64)
#define TINYUSDZ_SIMDSCAN_SSE2 1
#endif

namespace tinyusdz {
namespace next {
namespace simdscan {

namespace {

// Is `c` one of the array-structural bytes the capture loop must stop on?
inline bool IsStructural(char c) {
  return c == '[' || c == ']' || c == '"' || c == '\'' || c == '@' || c == '#';
}

// popcount of a <=16-bit mask, inlined SWAR. __builtin_popcount emits a libgcc
// call (__popcountdi2) under the SSE2 baseline (no POPCNT), which showed up hot;
// this stays a few register ops.
inline unsigned PopCount16(unsigned x) {
  x = x - ((x >> 1) & 0x5555u);
  x = (x & 0x3333u) + ((x >> 2) & 0x3333u);
  x = (x + (x >> 4)) & 0x0F0Fu;
  return (x + (x >> 8)) & 0x1Fu;
}

// Scalar reference (also the tail handler and the non-x86 path).
const char* ScanScalar(const char* p, const char* end, size_t* newlines) {
  size_t nl = 0;
  for (; p < end; ++p) {
    const char c = *p;
    if (IsStructural(c)) {
      *newlines += nl;
      return p;
    }
    if (c == '\n') ++nl;
  }
  *newlines += nl;
  return end;
}

}  // namespace

#if defined(TINYUSDZ_SIMDSCAN_SSE2)

const char* ScanArrayStructural(const char* p, const char* end,
                                size_t* newlines) {
  const __m128i lb = _mm_set1_epi8('[');
  const __m128i rb = _mm_set1_epi8(']');
  const __m128i dq = _mm_set1_epi8('"');
  const __m128i sq = _mm_set1_epi8('\'');
  const __m128i at = _mm_set1_epi8('@');
  const __m128i hs = _mm_set1_epi8('#');
  const __m128i nlv = _mm_set1_epi8('\n');

  size_t nl = 0;
  while (end - p >= 16) {
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    __m128i m = _mm_or_si128(_mm_cmpeq_epi8(v, lb), _mm_cmpeq_epi8(v, rb));
    m = _mm_or_si128(m, _mm_cmpeq_epi8(v, dq));
    m = _mm_or_si128(m, _mm_cmpeq_epi8(v, sq));
    m = _mm_or_si128(m, _mm_cmpeq_epi8(v, at));
    m = _mm_or_si128(m, _mm_cmpeq_epi8(v, hs));
    const unsigned mask =
        static_cast<unsigned>(_mm_movemask_epi8(m)) & 0xFFFFu;
    const unsigned nlmask =
        static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, nlv))) &
        0xFFFFu;
    if (mask) {
      const int idx = __builtin_ctz(mask);
      // newlines strictly before the structural byte
      nl += PopCount16(nlmask & ((1u << idx) - 1u));
      *newlines += nl;
      return p + idx;
    }
    if (nlmask) nl += PopCount16(nlmask);
    p += 16;
  }
  *newlines += nl;
  return ScanScalar(p, end, newlines);  // <16-byte tail
}

const char* Backend() { return "sse2"; }

#else  // non-x86: scalar (WASM/ARM rely on the compiler; correctness identical)

const char* ScanArrayStructural(const char* p, const char* end,
                                size_t* newlines) {
  return ScanScalar(p, end, newlines);
}

const char* Backend() { return "scalar"; }

#endif

}  // namespace simdscan
}  // namespace next
}  // namespace tinyusdz
