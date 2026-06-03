// SPDX-License-Identifier: MIT
// Fuzzer for LZ4 APIs hardened against null stream/state pointers.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lz4/lz4.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 4096) return 0;

  char src[128] = {};
  char dst[512] = {};
  const size_t n = std::min(size, sizeof(src));
  if (n > 0) {
    std::memcpy(src, data, n);
  }

  const int acceleration = size > 0 ? int(data[0] % 64u) - 16 : 1;
  (void)LZ4_compress_fast_extState(nullptr, src, dst, int(n), int(sizeof(dst)),
                                   acceleration);

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  (void)LZ4_decompress_fast_continue(nullptr, src, dst,
                                      size > 1 ? int(data[1] % sizeof(dst)) : 0);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

  return 0;
}
