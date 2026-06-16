// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - SIMD scanning helpers (implementation)
//
// Two build paths, selected at compile time:
//   * SSE2  - the x86-64 baseline (and 32-bit x86 only when the compiler is
//             actually targeting SSE2). All x86 toolchains (GCC/Clang/MSVC).
//   * scalar - everything else: ARM/AArch64, WebAssembly (no-SIMD), other ISAs,
//             and 32-bit x86 without SSE2. Byte-for-byte identical results, just
//             without the bulk speedup.
// The scalar routine is the SSE2 path's <16-byte tail handler too, so it is
// always compiled.

#include "simd-scan.hh"

// ---------------------------------------------------------------------------
// SSE2 availability. SSE2 is mandatory in the x86-64 ABI, so it is always safe
// there; on 32-bit x86 only enable it when the compiler says it is targeting
// SSE2 (otherwise the emitted SSE2 instructions could fault on an older CPU).
// WebAssembly and non-x86 ISAs deliberately fall through to the scalar path.
// Define TINYUSDZ_SIMDSCAN_FORCE_SCALAR to force the scalar path everywhere
// (portability escape hatch / for testing the fallback on an x86 host).
// ---------------------------------------------------------------------------
#if !defined(TINYUSDZ_SIMDSCAN_FORCE_SCALAR)
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define TINYUSDZ_SIMDSCAN_SSE2 1
#elif defined(__i386__) && defined(__SSE2__)
#define TINYUSDZ_SIMDSCAN_SSE2 1
#elif defined(_M_IX86) && defined(_M_IX86_FP) && (_M_IX86_FP >= 2)
#define TINYUSDZ_SIMDSCAN_SSE2 1
#endif
#endif  // !TINYUSDZ_SIMDSCAN_FORCE_SCALAR

#if defined(TINYUSDZ_SIMDSCAN_SSE2)
#include <emmintrin.h>  // SSE2
#if defined(_MSC_VER)
#include <intrin.h>  // _BitScanForward (MSVC has no __builtin_ctz)
#endif
#endif

namespace tinyusdz {
namespace next {
namespace simdscan {

namespace {

// Is `c` one of the array-structural bytes the capture loop must stop on?
inline bool IsStructural(char c) {
  return c == '[' || c == ']' || c == '"' || c == '\'' || c == '@' || c == '#';
}

// Scalar reference (also the SSE2 path's <16-byte tail handler, so it is always
// compiled — not just on the scalar-only build).
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

namespace {

// popcount of a <=16-bit mask, inlined SWAR. __builtin_popcount emits a libgcc
// call (__popcountdi2) under the SSE2 baseline (no POPCNT), which showed up hot;
// this stays a few register ops and is compiler-portable.
inline unsigned PopCount16(unsigned x) {
  x = x - ((x >> 1) & 0x5555u);
  x = (x & 0x3333u) + ((x >> 2) & 0x3333u);
  x = (x + (x >> 4)) & 0x0F0Fu;
  return (x + (x >> 8)) & 0x1Fu;
}

// Index of the lowest set bit. `x` is always non-zero at the call sites.
inline unsigned LowestSetBit(unsigned x) {
#if defined(_MSC_VER)
  unsigned long idx;
  _BitScanForward(&idx, x);
  return static_cast<unsigned>(idx);
#else
  return static_cast<unsigned>(__builtin_ctz(x));
#endif
}

}  // namespace

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
      const unsigned idx = LowestSetBit(mask);
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

#else  // scalar: non-x86 (ARM/AArch64/...), WebAssembly (no-SIMD), x86 w/o SSE2

const char* ScanArrayStructural(const char* p, const char* end,
                                size_t* newlines) {
  return ScanScalar(p, end, newlines);
}

const char* Backend() { return "scalar"; }

#endif  // TINYUSDZ_SIMDSCAN_SSE2

}  // namespace simdscan
}  // namespace next
}  // namespace tinyusdz
