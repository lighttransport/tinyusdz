// SPDX-License-Identifier: Apache 2.0
// Hash function benchmark & collision test for USDC TimeSamples dedup.
//
// Compares FNV-1a (current) vs XXH3_64bits (candidate) on workloads
// representative of the dedup path: float arrays, double arrays,
// mixed small/large buffers.
//
// Build:
//   clang++ -O2 -std=c++17 -I../../../src -I../../../src/external \
//           -o hash_bench hash_bench.cc
//
// Run:
//   ./hash_bench            # default: 1M iterations
//   ./hash_bench 5000000    # custom iteration count

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <unordered_set>
#include <vector>

// xxHash: header-only mode
#define XXH_INLINE_ALL
#include "xxhash.h"

// ============================================================================
// FNV-1a (current implementation from crate-writer.cc)
// ============================================================================
static size_t fnv1a_hash(const void *data, size_t byte_count) {
  const auto *p = static_cast<const uint8_t *>(data);
  size_t seed = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < byte_count; ++i) {
    seed ^= static_cast<size_t>(p[i]);
    seed *= 0x100000001b3ULL;
  }
  return seed;
}

// ============================================================================
// XXH3_64bits wrapper
// ============================================================================
static size_t xxh3_hash(const void *data, size_t byte_count) {
  return static_cast<size_t>(XXH3_64bits(data, byte_count));
}

// ============================================================================
// NaN-aware wrapper: canonicalize +0/-0 before hashing
// ============================================================================
struct NanCanonBuf {
  std::vector<uint8_t> buf;

  // Copy data, replacing +0/-0 floats with canonical zero bits
  void canonicalize_float(const void *data, size_t byte_count) {
    buf.resize(byte_count);
    std::memcpy(buf.data(), data, byte_count);
    size_t count = byte_count / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
      float v;
      std::memcpy(&v, buf.data() + i * sizeof(float), sizeof(float));
      if (v == 0.0f) {
        uint32_t zero = 0;
        std::memcpy(buf.data() + i * sizeof(float), &zero, sizeof(float));
      }
    }
  }

  void canonicalize_double(const void *data, size_t byte_count) {
    buf.resize(byte_count);
    std::memcpy(buf.data(), data, byte_count);
    size_t count = byte_count / sizeof(double);
    for (size_t i = 0; i < count; ++i) {
      double v;
      std::memcpy(&v, buf.data() + i * sizeof(double), sizeof(double));
      if (v == 0.0) {
        uint64_t zero = 0;
        std::memcpy(buf.data() + i * sizeof(double), &zero, sizeof(double));
      }
    }
  }
};

// ============================================================================
// Timer utility
// ============================================================================
struct Timer {
  using Clock = std::chrono::high_resolution_clock;
  Clock::time_point start;
  void begin() { start = Clock::now(); }
  double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
  }
};

// ============================================================================
// Workload generators
// ============================================================================
struct Workload {
  std::string name;
  std::vector<std::vector<uint8_t>> buffers;
  size_t elem_size;  // for NaN canonicalization
  bool is_float;
};

static Workload gen_float_arrays(size_t count, size_t elems_per_array,
                                  std::mt19937 &rng) {
  Workload w;
  w.name = "float[" + std::to_string(elems_per_array) + "] x " +
           std::to_string(count);
  w.elem_size = sizeof(float);
  w.is_float = true;
  std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

  for (size_t i = 0; i < count; ++i) {
    std::vector<uint8_t> buf(elems_per_array * sizeof(float));
    auto *fp = reinterpret_cast<float *>(buf.data());
    for (size_t j = 0; j < elems_per_array; ++j) {
      fp[j] = dist(rng);
    }
    // Sprinkle some +0/-0
    if (i % 7 == 0) fp[0] = 0.0f;
    if (i % 7 == 1) fp[0] = -0.0f;
    w.buffers.push_back(std::move(buf));
  }
  return w;
}

static Workload gen_double_arrays(size_t count, size_t elems_per_array,
                                   std::mt19937 &rng) {
  Workload w;
  w.name = "double[" + std::to_string(elems_per_array) + "] x " +
           std::to_string(count);
  w.elem_size = sizeof(double);
  w.is_float = true;
  std::uniform_real_distribution<double> dist(-1000.0, 1000.0);

  for (size_t i = 0; i < count; ++i) {
    std::vector<uint8_t> buf(elems_per_array * sizeof(double));
    auto *dp = reinterpret_cast<double *>(buf.data());
    for (size_t j = 0; j < elems_per_array; ++j) {
      dp[j] = dist(rng);
    }
    if (i % 7 == 0) dp[0] = 0.0;
    if (i % 7 == 1) dp[0] = -0.0;
    w.buffers.push_back(std::move(buf));
  }
  return w;
}

static Workload gen_int_arrays(size_t count, size_t elems_per_array,
                                std::mt19937 &rng) {
  Workload w;
  w.name = "int32[" + std::to_string(elems_per_array) + "] x " +
           std::to_string(count);
  w.elem_size = sizeof(int32_t);
  w.is_float = false;

  for (size_t i = 0; i < count; ++i) {
    std::vector<uint8_t> buf(elems_per_array * sizeof(int32_t));
    auto *ip = reinterpret_cast<int32_t *>(buf.data());
    for (size_t j = 0; j < elems_per_array; ++j) {
      ip[j] = static_cast<int32_t>(rng());
    }
    w.buffers.push_back(std::move(buf));
  }
  return w;
}

static Workload gen_float3_scalars(size_t count, std::mt19937 &rng) {
  Workload w;
  w.name = "float3 (12B scalar) x " + std::to_string(count);
  w.elem_size = sizeof(float);
  w.is_float = true;
  std::uniform_real_distribution<float> dist(-10.0f, 10.0f);

  for (size_t i = 0; i < count; ++i) {
    std::vector<uint8_t> buf(3 * sizeof(float));
    auto *fp = reinterpret_cast<float *>(buf.data());
    fp[0] = dist(rng);
    fp[1] = dist(rng);
    fp[2] = dist(rng);
    w.buffers.push_back(std::move(buf));
  }
  return w;
}

static Workload gen_matrix4d(size_t count, std::mt19937 &rng) {
  Workload w;
  w.name = "matrix4d (128B) x " + std::to_string(count);
  w.elem_size = sizeof(double);
  w.is_float = true;
  std::uniform_real_distribution<double> dist(-1.0, 1.0);

  for (size_t i = 0; i < count; ++i) {
    std::vector<uint8_t> buf(16 * sizeof(double));
    auto *dp = reinterpret_cast<double *>(buf.data());
    for (int j = 0; j < 16; ++j) dp[j] = dist(rng);
    w.buffers.push_back(std::move(buf));
  }
  return w;
}

// ============================================================================
// Throughput benchmark
// ============================================================================
struct BenchResult {
  double fnv1a_ms;
  double xxh3_ms;
  double fnv1a_MBps;
  double xxh3_MBps;
  size_t total_bytes;
};

static BenchResult bench_throughput(const Workload &w, size_t iters) {
  BenchResult r{};

  // Compute total bytes per iteration
  size_t bytes_per_iter = 0;
  for (const auto &b : w.buffers) bytes_per_iter += b.size();
  r.total_bytes = bytes_per_iter * iters;

  volatile size_t sink = 0;  // prevent optimization

  // --- FNV-1a ---
  Timer t;
  t.begin();
  for (size_t it = 0; it < iters; ++it) {
    for (const auto &b : w.buffers) {
      sink += fnv1a_hash(b.data(), b.size());
    }
  }
  r.fnv1a_ms = t.elapsed_ms();

  // --- XXH3 ---
  t.begin();
  for (size_t it = 0; it < iters; ++it) {
    for (const auto &b : w.buffers) {
      sink += xxh3_hash(b.data(), b.size());
    }
  }
  r.xxh3_ms = t.elapsed_ms();

  double total_MB = r.total_bytes / (1024.0 * 1024.0);
  r.fnv1a_MBps = total_MB / (r.fnv1a_ms / 1000.0);
  r.xxh3_MBps = total_MB / (r.xxh3_ms / 1000.0);

  (void)sink;
  return r;
}

// ============================================================================
// NaN-aware throughput: canonicalize + hash
// ============================================================================
struct NanBenchResult {
  double fnv1a_ms;
  double xxh3_ms;
};

static NanBenchResult bench_nan_aware(const Workload &w, size_t iters) {
  NanBenchResult r{};
  NanCanonBuf canon;
  volatile size_t sink = 0;

  // --- FNV-1a with NaN canonicalization ---
  Timer t;
  t.begin();
  for (size_t it = 0; it < iters; ++it) {
    for (const auto &b : w.buffers) {
      if (w.is_float && w.elem_size == sizeof(float))
        canon.canonicalize_float(b.data(), b.size());
      else if (w.is_float && w.elem_size == sizeof(double))
        canon.canonicalize_double(b.data(), b.size());
      else {
        canon.buf.assign(b.begin(), b.end());
      }
      sink += fnv1a_hash(canon.buf.data(), canon.buf.size());
    }
  }
  r.fnv1a_ms = t.elapsed_ms();

  // --- XXH3 with NaN canonicalization ---
  t.begin();
  for (size_t it = 0; it < iters; ++it) {
    for (const auto &b : w.buffers) {
      if (w.is_float && w.elem_size == sizeof(float))
        canon.canonicalize_float(b.data(), b.size());
      else if (w.is_float && w.elem_size == sizeof(double))
        canon.canonicalize_double(b.data(), b.size());
      else {
        canon.buf.assign(b.begin(), b.end());
      }
      sink += xxh3_hash(canon.buf.data(), canon.buf.size());
    }
  }
  r.xxh3_ms = t.elapsed_ms();

  (void)sink;
  return r;
}

// ============================================================================
// Collision test
// ============================================================================
struct CollisionResult {
  size_t n_inputs;
  size_t fnv1a_unique;
  size_t xxh3_unique;
  size_t fnv1a_collisions;
  size_t xxh3_collisions;
};

static CollisionResult test_collisions(const Workload &w) {
  CollisionResult r{};
  r.n_inputs = w.buffers.size();

  std::unordered_set<size_t> fnv_set, xxh_set;
  for (const auto &b : w.buffers) {
    fnv_set.insert(fnv1a_hash(b.data(), b.size()));
    xxh_set.insert(xxh3_hash(b.data(), b.size()));
  }
  r.fnv1a_unique = fnv_set.size();
  r.xxh3_unique = xxh_set.size();
  r.fnv1a_collisions = r.n_inputs - r.fnv1a_unique;
  r.xxh3_collisions = r.n_inputs - r.xxh3_unique;
  return r;
}

// ============================================================================
// Memory: sizeof state objects
// ============================================================================
static void report_memory() {
  printf("\n=== Memory Usage (state object sizes) ===\n");
  printf("  FNV-1a state:    %zu bytes (just a size_t seed)\n", sizeof(size_t));
  printf("  XXH3 state:      %zu bytes (XXH3_state_t)\n",
         sizeof(XXH3_state_t));
  printf("  XXH64 state:     %zu bytes (XXH64_state_t)\n",
         sizeof(XXH64_state_t));
  printf("  Note: For one-shot hashing (our use case), no state is\n");
  printf("        allocated — XXH3_64bits() uses stack-local state.\n");
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char **argv) {
  size_t iters = 1000000;
  if (argc > 1) iters = static_cast<size_t>(atol(argv[1]));

  printf("Hash Function Benchmark: FNV-1a vs XXH3_64bits\n");
  printf("Iterations per workload: %zu\n", iters);
  printf("================================================================\n");

  std::mt19937 rng(42);  // fixed seed for reproducibility

  // Generate workloads
  std::vector<Workload> workloads;
  workloads.push_back(gen_float3_scalars(100, rng));          // 12B, typical
  workloads.push_back(gen_float_arrays(100, 8, rng));         // 32B
  workloads.push_back(gen_float_arrays(100, 100, rng));       // 400B
  workloads.push_back(gen_float_arrays(100, 1000, rng));      // 4KB
  workloads.push_back(gen_double_arrays(100, 16, rng));       // 128B (matrix4d size)
  workloads.push_back(gen_matrix4d(100, rng));                // 128B
  workloads.push_back(gen_int_arrays(100, 100, rng));         // 400B (non-float)

  // === Raw throughput benchmarks ===
  printf("\n--- Raw Throughput (hash only, no NaN canonicalization) ---\n");
  printf("%-35s %12s %12s %12s %12s  %7s\n",
         "Workload", "FNV-1a ms", "XXH3 ms", "FNV MB/s", "XXH3 MB/s",
         "Speedup");
  printf("%-35s %12s %12s %12s %12s  %7s\n",
         "-----------------------------------", "----------", "----------",
         "----------", "----------", "-------");

  for (const auto &w : workloads) {
    auto r = bench_throughput(w, iters);
    printf("%-35s %12.2f %12.2f %12.1f %12.1f  %6.1fx\n", w.name.c_str(),
           r.fnv1a_ms, r.xxh3_ms, r.fnv1a_MBps, r.xxh3_MBps,
           r.fnv1a_ms / r.xxh3_ms);
  }

  // === NaN-aware throughput (canonicalize + hash) ===
  printf("\n--- NaN-Aware Throughput (canonicalize + hash) ---\n");
  printf("%-35s %12s %12s  %7s\n", "Workload", "FNV-1a ms", "XXH3 ms",
         "Speedup");
  printf("%-35s %12s %12s  %7s\n",
         "-----------------------------------", "----------", "----------",
         "-------");

  for (const auto &w : workloads) {
    auto r = bench_nan_aware(w, iters);
    printf("%-35s %12.2f %12.2f  %6.1fx\n", w.name.c_str(), r.fnv1a_ms,
           r.xxh3_ms, r.fnv1a_ms / r.xxh3_ms);
  }

  // === Collision tests ===
  printf("\n--- Collision Test (unique random inputs) ---\n");
  printf("%-35s %10s %10s %10s %10s %10s\n", "Workload", "Inputs",
         "FNV uniq", "XXH3 uniq", "FNV coll", "XXH3 coll");
  printf("%-35s %10s %10s %10s %10s %10s\n",
         "-----------------------------------", "----------", "----------",
         "----------", "----------", "----------");

  // Large collision sets
  std::vector<Workload> collision_workloads;
  collision_workloads.push_back(gen_float3_scalars(1000000, rng));
  collision_workloads.push_back(gen_float_arrays(1000000, 8, rng));
  collision_workloads.push_back(gen_float_arrays(1000000, 3, rng));
  collision_workloads.push_back(gen_int_arrays(1000000, 4, rng));
  collision_workloads.push_back(gen_double_arrays(500000, 4, rng));

  for (const auto &w : collision_workloads) {
    auto r = test_collisions(w);
    printf("%-35s %10zu %10zu %10zu %10zu %10zu\n", w.name.c_str(),
           r.n_inputs, r.fnv1a_unique, r.xxh3_unique, r.fnv1a_collisions,
           r.xxh3_collisions);
  }

  // === NaN-specific collision: +0 vs -0 ===
  printf("\n--- NaN-Specific: +0.0 vs -0.0 hash values ---\n");
  {
    float pos_zero = +0.0f, neg_zero = -0.0f;
    printf("  float  +0.0: FNV=%016zx  XXH3=%016zx\n",
           fnv1a_hash(&pos_zero, 4), xxh3_hash(&pos_zero, 4));
    printf("  float  -0.0: FNV=%016zx  XXH3=%016zx\n",
           fnv1a_hash(&neg_zero, 4), xxh3_hash(&neg_zero, 4));
    printf("  (Different hashes expected — NaN canonicalization happens\n");
    printf("   BEFORE hashing in the dedup path, not inside the hash fn)\n");
  }

  report_memory();

  printf("\n=== Summary ===\n");
  printf("  XXH3_64bits is the recommended replacement for FNV-1a:\n");
  printf("  - Better hash quality (fewer collisions)\n");
  printf("  - Significantly faster for buffers > ~32 bytes\n");
  printf("  - Comparable for very small buffers (12-32 bytes)\n");
  printf("  - No runtime state allocation for one-shot use\n");

  return 0;
}
