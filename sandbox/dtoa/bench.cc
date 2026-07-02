// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// sandbox/dtoa — throughput benchmark: dragonbox baseline vs ryu vs zmij, all
// rendering usdcat notation via the shared render_usd (+ zmij_native raw ceiling
// and the repo `oracle` for reference). Hand-rolled chrono timing (no deps).
//
// Distributions:
//   realistic : USD-like values (coords in [-1000,1000], normals [-1,1],
//               uvs [0,1]) — mostly short "nice" decimals, what dominates real
//               .usda files and usually decides the practical winner.
//   bits      : random finite bit patterns — the adversarial long-digit mix
//               (like dtoa-benchmark); over-represents hard cases.
// Workloads: float scalar, double scalar, and float3[] with ", " join (the
// array/vector hot path in the real value printer).
//
// Usage: ./bench [N]   (N = values per dataset, default 4,000,000)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

#include "dtoa_candidates.hh"

using namespace usddtoa;
using Clock = std::chrono::steady_clock;

namespace {

inline uint64_t splitmix64(uint64_t& s) {
  uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}
// Uniform double in [0,1) from a 53-bit draw.
inline double u01(uint64_t& s) {
  return (splitmix64(s) >> 11) * (1.0 / 9007199254740992.0);
}

std::vector<float> gen_realistic_floats(size_t n) {
  std::vector<float> v(n);
  uint64_t s = 0x1234567;
  for (size_t i = 0; i < n; ++i) {
    uint64_t k = splitmix64(s) % 3;
    double x;
    if (k == 0) x = (u01(s) * 2000.0 - 1000.0);        // coordinate
    else if (k == 1) x = (u01(s) * 2.0 - 1.0);          // normal component
    else x = u01(s);                                    // uv
    v[i] = static_cast<float>(x);
  }
  return v;
}
std::vector<double> gen_realistic_doubles(size_t n) {
  std::vector<double> v(n);
  uint64_t s = 0x7654321;
  for (size_t i = 0; i < n; ++i) {
    uint64_t k = splitmix64(s) % 3;
    if (k == 0) v[i] = u01(s) * 2000.0 - 1000.0;
    else if (k == 1) v[i] = u01(s) * 2.0 - 1.0;
    else v[i] = u01(s);
  }
  return v;
}
std::vector<float> gen_bits_floats(size_t n) {
  std::vector<float> v(n);
  uint64_t s = 0xABCDEF;
  for (size_t i = 0; i < n;) {
    uint32_t b = (uint32_t)splitmix64(s);
    float f; std::memcpy(&f, &b, 4);
    if (f == f && !(f > 3.4e38f || f < -3.4e38f)) { v[i++] = f; }  // finite
  }
  return v;
}
std::vector<double> gen_bits_doubles(size_t n) {
  std::vector<double> v(n);
  uint64_t s = 0xFEDCBA;
  for (size_t i = 0; i < n;) {
    uint64_t b = splitmix64(s);
    double d; std::memcpy(&d, &b, 8);
    if (d == d && d < 1e308 && d > -1e308) { v[i++] = d; }  // finite
  }
  return v;
}

volatile uint64_t g_sink = 0;

// Time `fn` over the whole dataset, best-of-reps, return ns per value.
template <typename T, typename Fn>
double time_scalar(const std::vector<T>& data, Fn fn, int reps) {
  double best = 1e300;
  char buf[48];
  for (int r = 0; r < reps; ++r) {
    uint64_t sink = 0;
    auto t0 = Clock::now();
    for (T v : data) {
      size_t n = fn(buf, v);
      sink += n + (uint64_t)(unsigned char)buf[0];
    }
    auto t1 = Clock::now();
    g_sink += sink;
    double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                    .count();
    double per = ns / data.size();
    if (per < best) best = per;
  }
  return best;
}

// float3[] with ", " join — mirrors ChunkedStream's per-element array path.
template <typename Fn>
double time_vec3(const std::vector<float>& data, Fn fn, int reps) {
  double best = 1e300;
  char line[128];
  size_t triples = data.size() / 3;
  for (int r = 0; r < reps; ++r) {
    uint64_t sink = 0;
    auto t0 = Clock::now();
    for (size_t i = 0; i < triples; ++i) {
      char* p = line;
      for (int c = 0; c < 3; ++c) {
        p += fn(p, data[i * 3 + c]);
        if (c != 2) { *p++ = ','; *p++ = ' '; }
      }
      sink += (uint64_t)(p - line) + (unsigned char)line[0];
    }
    auto t1 = Clock::now();
    g_sink += sink;
    double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                    .count();
    double per = ns / triples;
    if (per < best) best = per;
  }
  return best;
}

struct FCand { const char* name; std::size_t (*f)(char*, float); };
struct DCand { const char* name; std::size_t (*f)(char*, double); };

const FCand kF[] = {
    {"dragonbox", usd_dtos_dragonbox},
    {"zmij", usd_dtos_zmij},
    {"zmij_simd", usd_dtos_zmij_simd},
    {"zmij_fused", usd_dtos_zmij_fused},
    {"ryu", usd_dtos_ryu},
    {"zmij_native*", usd_dtos_zmij_native},
};
const DCand kD[] = {
    {"dragonbox", usd_dtos_dragonbox},
    {"zmij", usd_dtos_zmij},
    {"zmij_simd", usd_dtos_zmij_simd},
    {"zmij_fused", usd_dtos_zmij_fused},
    {"ryu", usd_dtos_ryu},
    {"zmij_native*", usd_dtos_zmij_native},
};

void print_header(const char* title) {
  std::printf("\n=== %s ===\n  %-14s %10s %12s  %s\n", title, "candidate",
              "ns/val", "Mvals/s", "rel");
}
void print_row(const char* name, double ns, double base_ns) {
  std::printf("  %-14s %10.2f %12.1f  %.2fx\n", name, ns, 1000.0 / ns,
              base_ns / ns);
}

}  // namespace

int main(int argc, char** argv) {
  size_t N = argc > 1 ? (size_t)std::strtoull(argv[1], nullptr, 10) : 4000000;
  int reps = 5;
  std::printf("sandbox/dtoa benchmark — N=%zu values/dataset, best-of-%d\n", N,
              reps);
  std::printf("(* zmij_native = raw zmij::write, NOT usdcat notation; speed "
              "ceiling only)\n");

  auto rf = gen_realistic_floats(N);
  auto rd = gen_realistic_doubles(N);
  auto bf = gen_bits_floats(N);
  auto bd = gen_bits_doubles(N);

  // --- float scalar ---
  print_header("float scalar — realistic USD values");
  double base = time_scalar(rf, kF[0].f, reps);  // dragonbox is the baseline
  for (auto& c : kF) print_row(c.name, time_scalar(rf, c.f, reps), base);

  print_header("float scalar — random finite bit patterns");
  base = time_scalar(bf, kF[0].f, reps);
  for (auto& c : kF) print_row(c.name, time_scalar(bf, c.f, reps), base);

  // --- double scalar ---
  print_header("double scalar — realistic USD values");
  base = time_scalar(rd, kD[0].f, reps);
  for (auto& c : kD) print_row(c.name, time_scalar(rd, c.f, reps), base);

  print_header("double scalar — random finite bit patterns");
  base = time_scalar(bd, kD[0].f, reps);
  for (auto& c : kD) print_row(c.name, time_scalar(bd, c.f, reps), base);

  // --- float3[] vector (array hot path) ---
  print_header("float3[] with \", \" join — realistic (per-triple)");
  base = time_vec3(rf, kF[0].f, reps);
  for (auto& c : kF) print_row(c.name, time_vec3(rf, c.f, reps), base);

  if (g_sink == 0x123456789ULL) std::fputc(' ', stderr);  // keep sink live
  return 0;
}
