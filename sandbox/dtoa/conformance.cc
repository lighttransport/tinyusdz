// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// sandbox/dtoa — byte-identity conformance gate.
//
// Every shared-render candidate (dragonbox / ryu / zmij) MUST reproduce the
// repo's production dtos_to (the oracle, which matches usdcat) byte-for-byte, or
// it is disqualified as a drop-in. zmij_native is intentionally NOT checked (it
// emits its own Python-style notation, not usdcat's).
//
// Modes (argv[1]):
//   (default) / all   : curated + all 2^32 floats + 1e9 sampled doubles
//   quick             : curated + 10M random floats + 10M random doubles
//   curated           : just the hand-picked edge cases
//   float_exhaustive  : all 2^32 float bit patterns (argv[2] = thread count)
//   double_sampled    : argv[2] = count (default 1e9), argv[3] = threads
// Exit code is nonzero if ANY candidate mismatches anywhere.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "dtoa_candidates.hh"

using namespace usddtoa;

namespace {

constexpr int kNumCandidates = 6;
const char* kNames[kNumCandidates] = {"dragonbox",  "ryu",
                                      "zmij",       "zmij_fused",
                                      "dragonbox_fused", "zmij_simd"};

// One candidate's usdcat function for float & double.
std::size_t call_candidate(int idx, char* dst, float v) {
  switch (idx) {
    case 0: return usd_dtos_dragonbox(dst, v);
    case 1: return usd_dtos_ryu(dst, v);
    case 2: return usd_dtos_zmij(dst, v);
    case 3: return usd_dtos_zmij_fused(dst, v);
    case 4: return usd_dtos_dragonbox_fused(dst, v);
    default: return usd_dtos_zmij_simd(dst, v);
  }
}
std::size_t call_candidate(int idx, char* dst, double v) {
  switch (idx) {
    case 0: return usd_dtos_dragonbox(dst, v);
    case 1: return usd_dtos_ryu(dst, v);
    case 2: return usd_dtos_zmij(dst, v);
    case 3: return usd_dtos_zmij_fused(dst, v);
    case 4: return usd_dtos_dragonbox_fused(dst, v);
    default: return usd_dtos_zmij_simd(dst, v);
  }
}

struct Report {
  std::atomic<uint64_t> mismatches[kNumCandidates];
  std::string first_example[kNumCandidates];  // guarded by first_seen
  std::atomic<bool> first_seen[kNumCandidates];
  Report() {
    for (int i = 0; i < kNumCandidates; ++i) {
      mismatches[i].store(0);
      first_seen[i].store(false);
    }
  }
};

template <typename T>
inline void check_value(T v, Report& rep) {
  char oracle[48];
  std::size_t no = usd_dtos_oracle(oracle, v);
  for (int c = 0; c < kNumCandidates; ++c) {
    char got[48];
    std::size_t ng = call_candidate(c, got, v);
    if (ng != no || std::memcmp(oracle, got, no) != 0) {
      if (rep.mismatches[c].fetch_add(1) < 8 &&
          !rep.first_seen[c].exchange(true)) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(T));
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "  [%s] value_bits=0x%llx  oracle='%.*s'  got='%.*s'",
                      kNames[c], (unsigned long long)bits, (int)no, oracle,
                      (int)ng, got);
        rep.first_example[c] = buf;
      }
    }
  }
}

int finish(const Report& rep, const char* label, uint64_t checked) {
  bool ok = true;
  std::printf("[%s] checked=%llu\n", label, (unsigned long long)checked);
  for (int c = 0; c < kNumCandidates; ++c) {
    uint64_t m = rep.mismatches[c].load();
    if (m == 0) {
      std::printf("  %-10s OK (0 mismatches)\n", kNames[c]);
    } else {
      ok = false;
      std::printf("  %-10s FAIL: %llu mismatches\n%s\n", kNames[c],
                  (unsigned long long)m, rep.first_example[c].c_str());
    }
  }
  return ok ? 0 : 1;
}

// splitmix64 for reproducible sampling (Math.random/Date banned; fixed seed).
inline uint64_t splitmix64(uint64_t& s) {
  uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

int run_curated() {
  Report rep;
  const double dv[] = {0.0,   -0.0,     1.0,    -1.0,   0.3,    0.1,
                       1.5,   3.14159,  13.944, 1e-6,   1e-7,   1e15,
                       1e14,  1e16,     1e30,   1e300,  1e-30,  1e-300,
                       123456789012345.0, 2.2250738585072014e-308,  // min normal
                       4.9406564584124654e-324,                     // min subnormal
                       1.7976931348623157e308,                      // max
                       9.999999999999999e22, 5.0507837461e-27};
  uint64_t n = 0;
  for (double v : dv) { check_value(v, rep); ++n; }
  const float fv[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.3f, 0.1f, 1.5f, 100.0f,
                      1e-7f, 1e-6f, 1e15f, 3.14159f, 1.4e-45f /*min subnormal*/,
                      3.4028235e38f /*max*/, 1.1754944e-38f /*min normal*/,
                      0.009999999776482582f};
  for (float v : fv) { check_value(v, rep); ++n; }
  // Special-value float bit patterns (nan/inf variants).
  for (uint32_t b : {0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7f800001u,
                     0xffc00000u}) {
    float f; std::memcpy(&f, &b, 4); check_value(f, rep); ++n;
  }
  return finish(rep, "curated", n);
}

int run_float_exhaustive(int threads) {
  if (threads < 1) threads = (int)std::thread::hardware_concurrency();
  if (threads < 1) threads = 1;
  Report rep;
  const uint64_t total = 1ull << 32;
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&, t]() {
      const uint64_t begin = total * t / threads;
      const uint64_t end = total * (t + 1) / threads;
      for (uint64_t i = begin; i < end; ++i) {
        float f;
        uint32_t b = (uint32_t)i;
        std::memcpy(&f, &b, 4);
        check_value(f, rep);
      }
    });
  }
  for (auto& th : pool) th.join();
  return finish(rep, "float_exhaustive (all 2^32)", total);
}

int run_double_sampled(uint64_t count, int threads) {
  if (threads < 1) threads = (int)std::thread::hardware_concurrency();
  if (threads < 1) threads = 1;
  Report rep;
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&, t]() {
      const uint64_t begin = count * t / threads;
      const uint64_t end = count * (t + 1) / threads;
      uint64_t seed = 0xD1B54A32D192ED03ULL + t * 0x9e3779b97f4a7c15ULL;
      for (uint64_t i = begin; i < end; ++i) {
        uint64_t bits = splitmix64(seed);
        double d;
        std::memcpy(&d, &bits, 8);
        check_value(d, rep);
      }
    });
  }
  for (auto& th : pool) th.join();
  return finish(rep, "double_sampled (random bit patterns)", count);
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "all";
  int rc = 0;
  if (mode == "curated") {
    rc = run_curated();
  } else if (mode == "float_exhaustive") {
    rc |= run_curated();
    rc |= run_float_exhaustive(argc > 2 ? std::atoi(argv[2]) : 0);
  } else if (mode == "double_sampled") {
    uint64_t cnt = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1000000000ull;
    rc |= run_curated();
    rc |= run_double_sampled(cnt, argc > 3 ? std::atoi(argv[3]) : 0);
  } else if (mode == "quick") {
    rc |= run_curated();
    rc |= run_double_sampled(10000000ull, 0);
  } else {  // "all" — the default exhaustive gate
    rc |= run_curated();
    rc |= run_float_exhaustive(0);
    rc |= run_double_sampled(1000000000ull, 0);
  }
  std::printf(rc == 0 ? "\nCONFORMANCE: PASS\n" : "\nCONFORMANCE: FAIL\n");
  return rc;
}
