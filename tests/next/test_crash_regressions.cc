// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Replays minimized fuzzer-found crash inputs through the next USDC reader.
// Each must load-or-reject cleanly (no crash/UB). Run under the ASAN/UBSAN
// build (build/next-asan or -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined) to
// catch the original out-of-bounds / null-memcpy / shift-UB regressions; in a
// plain build the heap-overflow case still aborts on its own.
//
// Inputs live in tests/next/crash_regressions/usdc_*.bin and are stored WITHOUT
// the 8-byte "PXR-USDC" magic (which the fuzz harness prepends); we re-prepend
// it here.

#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

#include "next/reader/usdc-reader.hh"

#ifndef CRASH_REGRESSION_DIR
#define CRASH_REGRESSION_DIR "."
#endif

using namespace tinyusdz::next;

static bool read_file(const std::string &path, std::string &out) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  out.resize(static_cast<size_t>(f.tellg()));
  f.seekg(0);
  return static_cast<bool>(f.read(&out[0], static_cast<std::streamsize>(out.size())));
}

int main() {
  const std::string dir = CRASH_REGRESSION_DIR;
  DIR *d = opendir(dir.c_str());
  if (!d) {
    std::printf("crash_regressions dir not found: %s (skipping)\n", dir.c_str());
    return 0;
  }

  int replayed = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != nullptr) {
    std::string name = ent->d_name;
    if (name.size() < 5 || name.substr(name.size() - 4) != ".bin") continue;

    std::string body;
    if (!read_file(dir + "/" + name, body)) continue;

    std::string buf("PXR-USDC");
    buf += body;

    USDCLoadOptions opts;
    opts.crate_options.max_array_elements = 1u << 20;
    opts.crate_options.max_tokens = 1u << 18;
    opts.crate_options.max_strings = 1u << 18;
    opts.crate_options.max_fields = 1u << 18;
    opts.crate_options.max_specs = 1u << 18;
    opts.crate_options.max_paths = 1u << 18;

    // Must return (success or graceful failure), never crash / trip a sanitizer.
    USDCLoadResult r = LoadUSDCFromMemory(
        reinterpret_cast<const uint8_t *>(buf.data()), buf.size(), opts);
    (void)r;
    std::printf("  replayed %s\n", name.c_str());
    ++replayed;
  }
  closedir(d);

  std::printf("crash regressions: %d input(s) replayed, no crash.\n", replayed);
  return 0;
}
