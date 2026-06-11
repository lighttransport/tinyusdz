// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// libFuzzer harness: src/next USDA (ASCII) parser.
//
// Build (clang): cmake -S src/next -B build-fuzz -DTINYUSDZ_NEXT_BUILD_FUZZERS=ON
// Seed from tests/usda/*.usda.

#include <cstdint>
#include <cstddef>

#include "next/reader/usda-reader.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  tinyusdz::next::LoadOptions opts;
  opts.parse_options.max_depth = 128;
  opts.parse_options.max_file_size = 16u << 20;  // 16 MB

  tinyusdz::next::LoadResult result = tinyusdz::next::LoadUSDAFromString(
      reinterpret_cast<const char *>(data), size, opts);
  (void)result;
  return 0;
}
