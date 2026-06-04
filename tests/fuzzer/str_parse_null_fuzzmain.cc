// SPDX-License-Identifier: MIT
// Fuzzer for hardened numeric string parse helpers.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "str-util.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 4096) return 0;

  const size_t n = std::min<size_t>(size, 256);
  const std::string s(reinterpret_cast<const char *>(data), n);

  (void)tinyusdz::atoi(s);
  (void)tinyusdz::atoll(s);
  (void)tinyusdz::atof_float(s);
  (void)tinyusdz::atod(s);

  const char *cstr = s.c_str();
  (void)tinyusdz::atoi(cstr);
  (void)tinyusdz::atoll(cstr);
  (void)tinyusdz::atof_float(cstr);
  (void)tinyusdz::atod(cstr);

  (void)tinyusdz::atoi(static_cast<const char *>(nullptr));
  (void)tinyusdz::atoll(static_cast<const char *>(nullptr));
  (void)tinyusdz::atof_float(static_cast<const char *>(nullptr));
  (void)tinyusdz::atod(static_cast<const char *>(nullptr));

  return 0;
}
