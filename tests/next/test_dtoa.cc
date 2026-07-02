// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Unit test for the next USDA float formatter (src/next/writer/dtoa) after the
// zmij usdcat SIMD fast path was wired in. Validates, via the PUBLIC API only:
//   1. Notation contract  — curated value -> exact usdcat string (float+double).
//   2. Round-trip         — dtos(v) parses back to v (shortest, correct digits),
//                           over a large random sample spanning the SIMD fast
//                           path AND the dragonbox fallback range.
//   3. API agreement      — dtos(), dtos_to(), dtos_append() are byte-identical.
//   4. Buffer safety      — the SIMD overshoot never writes past kDtoaBufSize
//                           (canary bytes stay intact).
//
// Fast by default (a few million samples, ~1 s) for ctest; pass "exhaustive" to
// also sweep all 2^32 float bit patterns.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "next/writer/dtoa.hh"

using tinyusdz::next::dtos;
using tinyusdz::next::dtos_append;
using tinyusdz::next::dtos_to;
using tinyusdz::next::kDtoaBufSize;

namespace {

int g_failures = 0;

#define CHECK_EQ_STR(got, want)                                              \
  do {                                                                       \
    std::string g_ = (got), w_ = (want);                                     \
    if (g_ != w_) {                                                          \
      std::printf("  FAIL %s:%d  got='%s' want='%s'\n", __FILE__, __LINE__,  \
                  g_.c_str(), w_.c_str());                                   \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

// ---- 1. notation contract ----
void test_notation() {
  // doubles
  CHECK_EQ_STR(dtos(0.0), "0");
  CHECK_EQ_STR(dtos(-0.0), "-0");
  CHECK_EQ_STR(dtos(1.0), "1");
  CHECK_EQ_STR(dtos(-1.0), "-1");
  CHECK_EQ_STR(dtos(100.0), "100");
  CHECK_EQ_STR(dtos(0.3), "0.3");
  CHECK_EQ_STR(dtos(1.5), "1.5");
  CHECK_EQ_STR(dtos(13.944), "13.944");
  CHECK_EQ_STR(dtos(3.14159), "3.14159");
  CHECK_EQ_STR(dtos(1e-6), "0.000001");    // fixed (boundary)
  CHECK_EQ_STR(dtos(1e-7), "1e-7");        // scientific, unpadded, no '+'
  CHECK_EQ_STR(dtos(1e14), "100000000000000");
  CHECK_EQ_STR(dtos(1e15), "1e15");        // scientific (>= 15)
  CHECK_EQ_STR(dtos(1e30), "1e30");
  CHECK_EQ_STR(dtos(-1e-30), "-1e-30");
  CHECK_EQ_STR(dtos(std::nan("")), "nan");
  CHECK_EQ_STR(dtos(std::numeric_limits<double>::infinity()), "inf");
  CHECK_EQ_STR(dtos(-std::numeric_limits<double>::infinity()), "-inf");
  // floats
  CHECK_EQ_STR(dtos(0.0f), "0");
  CHECK_EQ_STR(dtos(-0.0f), "-0");
  CHECK_EQ_STR(dtos(1.0f), "1");
  CHECK_EQ_STR(dtos(100.0f), "100");
  CHECK_EQ_STR(dtos(0.3f), "0.3");
  CHECK_EQ_STR(dtos(1.5f), "1.5");
  CHECK_EQ_STR(dtos(1e-7f), "1e-7");
  CHECK_EQ_STR(dtos(1e15f), "1e15");
  CHECK_EQ_STR(dtos(std::nanf("")), "nan");
  CHECK_EQ_STR(dtos(-std::numeric_limits<float>::infinity()), "-inf");
}

// ---- 3. the three APIs must agree, byte for byte ----
template <typename T>
void check_api_agreement(T v) {
  char b1[kDtoaBufSize];
  std::size_t n = dtos_to(b1, v);
  std::string s_to(b1, n);
  std::string s_ret = dtos(v);
  std::string s_app;
  dtos_append(s_app, v);
  if (s_to != s_ret || s_to != s_app) {
    std::printf("  FAIL api-agree: to='%s' ret='%s' app='%s'\n", s_to.c_str(),
                s_ret.c_str(), s_app.c_str());
    ++g_failures;
  }
}

// ---- 4. buffer-overshoot safety (canary after a kDtoaBufSize buffer) ----
template <typename T>
void check_buffer_safe(T v) {
  struct {
    char buf[kDtoaBufSize];
    unsigned char canary[16];
  } g;
  std::memset(g.canary, 0xAB, sizeof(g.canary));
  (void)dtos_to(g.buf, v);
  for (unsigned char c : g.canary) {
    if (c != 0xAB) {
      std::printf("  FAIL buffer overshoot past kDtoaBufSize for a value\n");
      ++g_failures;
      break;
    }
  }
}

// ---- 2. round-trip: dtos(v) must parse back to exactly v ----
inline uint64_t splitmix64(uint64_t& s) {
  uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

void check_roundtrip_double(double v) {
  if (!std::isfinite(v)) return;
  std::string s = dtos(v);
  double back = std::strtod(s.c_str(), nullptr);
  uint64_t a, b;
  std::memcpy(&a, &v, 8);
  std::memcpy(&b, &back, 8);
  if (a != b) {
    std::printf("  FAIL roundtrip double '%s' -> %.17g (bits %llx != %llx)\n",
                s.c_str(), back, (unsigned long long)a, (unsigned long long)b);
    ++g_failures;
  }
  check_api_agreement(v);
  check_buffer_safe(v);
}

void check_roundtrip_float(float v) {
  if (!std::isfinite(v)) return;
  std::string s = dtos(v);
  float back = std::strtof(s.c_str(), nullptr);
  uint32_t a, b;
  std::memcpy(&a, &v, 4);
  std::memcpy(&b, &back, 4);
  if (a != b) {
    std::printf("  FAIL roundtrip float '%s' -> %.9g\n", s.c_str(), (double)back);
    ++g_failures;
  }
  check_api_agreement(v);
  check_buffer_safe(v);
}

void test_random_roundtrip(uint64_t n) {
  uint64_t s = 0xDEADBEEFCAFEULL;
  for (uint64_t i = 0; i < n; ++i) {
    uint64_t bits = splitmix64(s);
    double d;
    std::memcpy(&d, &bits, 8);
    check_roundtrip_double(d);
    float f;
    uint32_t fb = (uint32_t)(bits >> 11);
    std::memcpy(&f, &fb, 4);
    check_roundtrip_float(f);
    // Also realistic-range values (the SIMD fast path).
    double u = (double)(bits >> 11) * (1.0 / 9007199254740992.0);
    check_roundtrip_double(u * 2000.0 - 1000.0);
    check_roundtrip_float((float)(u * 2.0 - 1.0));
  }
}

void test_float_exhaustive() {
  for (uint64_t i = 0; i < (1ull << 32); ++i) {
    float f;
    uint32_t b = (uint32_t)i;
    std::memcpy(&f, &b, 4);
    check_roundtrip_float(f);
    if ((i & 0x3ffffff) == 0)
      std::printf("  ... %.1f%%\n", 100.0 * i / (double)(1ull << 32));
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("[test_dtoa] notation contract...\n");
  test_notation();

  const bool exhaustive = argc > 1 && std::string(argv[1]) == "exhaustive";
  const uint64_t n = exhaustive ? 2000000 : 2000000;
  std::printf("[test_dtoa] round-trip + API agreement + buffer safety "
              "(%llu iters)...\n",
              (unsigned long long)n);
  test_random_roundtrip(n);

  if (exhaustive) {
    std::printf("[test_dtoa] exhaustive all-2^32-float round-trip...\n");
    test_float_exhaustive();
  }

  if (g_failures == 0) {
    std::printf("[test_dtoa] PASS\n");
    return 0;
  }
  std::printf("[test_dtoa] FAIL (%d failures)\n", g_failures);
  return 1;
}
