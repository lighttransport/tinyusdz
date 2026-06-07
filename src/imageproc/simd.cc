// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-present Light Transport Entertainment, Inc.
//
// NOTE: this TU is compiled with -ffp-contract=off (set per-source in
// CMakeLists.txt) so a*b+c is never fused into an FMA. That keeps every
// auto-vectorized per-ISA variant bit-identical to the scalar reference, which
// the colorspace refactor relies on for unchanged render output.

#include "imageproc/simd.hh"

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

// Native runtime multi-versioning via GCC/Clang target_clones + ELF ifunc
// (Linux). The build defines TINYUSDZ_IMAGEPROC_MULTIVERSION when this is wanted
// and the toolchain/target support it.
#if defined(TINYUSDZ_IMAGEPROC_MULTIVERSION) && !defined(__wasm__) &&         \
    (defined(__i386__) || defined(__x86_64__)) &&                            \
    (defined(__GNUC__) || defined(__clang__)) && defined(__ELF__) &&          \
    __has_attribute(target_clones)
#define IMAGEPROC_MV \
  __attribute__((target_clones("avx2", "avx", "sse4.1", "sse2", "default")))
#define IMAGEPROC_MV_ON 1
#else
#define IMAGEPROC_MV
#endif

namespace tinyusdz {
namespace imageproc {

SimdLevel ActiveSimdLevel() {
#if defined(__wasm_simd128__)
  return SimdLevel::Wasm;
#elif defined(IMAGEPROC_MV_ON)
  __builtin_cpu_init();
  if (__builtin_cpu_supports("avx2")) return SimdLevel::AVX2;
  if (__builtin_cpu_supports("avx")) return SimdLevel::AVX;
  if (__builtin_cpu_supports("sse4.1")) return SimdLevel::SSE41;
  if (__builtin_cpu_supports("sse2")) return SimdLevel::SSE2;
  return SimdLevel::Scalar;
#else
  return SimdLevel::Scalar;
#endif
}

const char *ToString(SimdLevel level) {
  switch (level) {
    case SimdLevel::Scalar: return "scalar";
    case SimdLevel::SSE2: return "sse2";
    case SimdLevel::SSE41: return "sse4.1";
    case SimdLevel::AVX: return "avx";
    case SimdLevel::AVX2: return "avx2";
    case SimdLevel::Wasm: return "wasm-simd128";
  }
  return "unknown";
}

IMAGEPROC_MV
void Mat3MulRGBf(const float *in, float *out, size_t n_pixels,
                 const float m[9]) {
  const float m0 = m[0], m1 = m[1], m2 = m[2];
  const float m3 = m[3], m4 = m[4], m5 = m[5];
  const float m6 = m[6], m7 = m[7], m8 = m[8];
  for (size_t i = 0; i < n_pixels; ++i) {
    const float r = in[3 * i + 0];
    const float g = in[3 * i + 1];
    const float b = in[3 * i + 2];
    out[3 * i + 0] = m0 * r + m1 * g + m2 * b;
    out[3 * i + 1] = m3 * r + m4 * g + m5 * b;
    out[3 * i + 2] = m6 * r + m7 * g + m8 * b;
  }
}

IMAGEPROC_MV
void PackChannels8(uint8_t *out, size_t n_pixels, int out_channels,
                   const PackSource *sources) {
  // Channel-outer so each inner pixel loop has fixed (compile-visible) strides —
  // a strided gather/scatter the vectorizer handles far better than a per-pixel
  // loop over a runtime channel count. Output is identical either way.
  const int oc = out_channels;
  for (int c = 0; c < oc; ++c) {
    const PackSource &s = sources[c];
    if (s.in) {
      const uint8_t *in = s.in + s.channel;
      const int istr = s.in_stride;
      for (size_t x = 0; x < n_pixels; ++x) out[x * oc + c] = in[x * istr];
    } else {
      const uint8_t v = s.constant;
      for (size_t x = 0; x < n_pixels; ++x) out[x * oc + c] = v;
    }
  }
}

}  // namespace imageproc
}  // namespace tinyusdz
