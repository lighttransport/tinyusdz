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

  (void)lightusd::atoi(s);
  (void)lightusd::atoll(s);
  (void)lightusd::atof_float(s);
  (void)lightusd::atod(s);

  const char *cstr = s.c_str();
  (void)lightusd::atoi(cstr);
  (void)lightusd::atoll(cstr);
  (void)lightusd::atof_float(cstr);
  (void)lightusd::atod(cstr);

  (void)lightusd::atoi(static_cast<const char *>(nullptr));
  (void)lightusd::atoll(static_cast<const char *>(nullptr));
  (void)lightusd::atof_float(static_cast<const char *>(nullptr));
  (void)lightusd::atod(static_cast<const char *>(nullptr));

  return 0;
}
