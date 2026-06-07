// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-present Light Transport Entertainment, Inc.
//
// SIMD-accelerated per-row image kernels with runtime ISA dispatch.
//
// Kernels are written as plain, auto-vectorizable loops. On x86 GCC/Clang the
// kernel is compiled into SSE2/SSE4.1/AVX/AVX2 variants via function
// multi-versioning (target_clones + ifunc), and the best is selected at load
// time — one portable binary, optimal everywhere. Under wasm the same loop is
// auto-vectorized by `-msimd128`. Everywhere else a scalar build is used.
// Floating-point contraction is disabled in the implementation so the
// vectorized result is bit-identical to the scalar reference.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tinyusdz {
namespace imageproc {

enum class SimdLevel { Scalar, SSE2, SSE41, AVX, AVX2, Wasm };

// The ISA path that dispatched kernels will actually run on (best supported, or
// Scalar when multi-versioning is disabled / unsupported). For logging/tests.
SimdLevel ActiveSimdLevel();
const char *ToString(SimdLevel level);

// out[i] = M * in[i] for `n_pixels` interleaved RGB float triplets.
// `m` is a row-major 3x3 matrix. `in` and `out` may be the same buffer.
// Used by linear colorspace conversions (DisplayP3/Rec2020/ACEScg <-> sRGB).
void Mat3MulRGBf(const float *in, float *out, size_t n_pixels, const float m[9]);

// One output channel's source for PackChannels8: each output pixel's byte is
// `in[x*in_stride + channel]`, or `constant` when `in` is null.
struct PackSource {
  const uint8_t *in = nullptr;  // null => emit `constant`
  int in_stride = 1;            // input bytes per pixel
  int channel = 0;              // byte offset within the input pixel
  uint8_t constant = 0;
};

// Pack `out_channels` (1..4) sources into the interleaved 8-bit row `out`:
// out[x*out_channels + c] = sources[c] sampled at pixel x, for `n_pixels`.
// Used by the channel repack/unpack paths (combine grayscale maps into ORM/RGBA
// etc.). Bytewise, so the vectorized result equals the scalar result exactly.
void PackChannels8(uint8_t *out, size_t n_pixels, int out_channels,
                   const PackSource *sources);

}  // namespace imageproc
}  // namespace tinyusdz
