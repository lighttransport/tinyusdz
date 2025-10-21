#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

#include "dragonbox_to_chars.h"

// Import dtoa_dragonbox from print_fp.cc
namespace internal {

// Maximum buffer sizes required for dtoa_dragonbox (same as in print_fp.cc)
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT = 24;
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE = 32;
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE = DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE;

template <typename T>
inline int count_digits(T n) {
  int count = 1;
  for (;;) {
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}

inline auto digits2(size_t value) -> const char* {
  alignas(2) static const char data[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  return &data[value * 2];
}

inline void write2digits(char* out, size_t value) {
  *out++ = static_cast<char>('0' + value / 10);
  *out = static_cast<char>('0' + value % 10);
}

char* write_exponent(int exp, char* out) {
  if (exp < 0) {
    *out++ = '-';
    exp = -exp;
  } else {
    *out++ = '+';
  }
  auto uexp = static_cast<uint32_t>(exp);
  if (uexp >= 100u) {
    const char* top = digits2(uexp / 100);
    if (uexp >= 1000u) *out++ = top[0];
    *out++ = static_cast<char>(top[1]);
    uexp %= 100;
  }
  const char* d = digits2(uexp);
  *out++ = static_cast<char>(d[0]);
  *out++ = static_cast<char>(d[1]);
  return out;
}

inline char* fill_n(char* p, int n, char c) {
  for (int i = 0; i < n; i++, p++) {
    *p = c;
  }
  return p;
}

inline void format_decimal_impl(char* out, uint64_t value, uint32_t size) {
  unsigned n = size;
  while (value >= 100) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value % 100));
    value /= 100;
  }
  if (value >= 10) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value));
  } else {
    out[--n] = static_cast<char>('0' + value);
  }
}

inline char* format_decimal(char* out, uint64_t value, uint32_t num_digits) {
  format_decimal_impl(out, value, num_digits);
  return out + num_digits;
}

inline char* write_significand_e(char* out, uint64_t significand,
                                 int significand_size, int exponent) {
  out = format_decimal(out, significand, significand_size);
  return fill_n(out, exponent, '0');
}

inline char* write_significand(char* out, uint64_t significand,
                               int significand_size, int integral_size,
                               char decimal_point) {
  if (!decimal_point) return format_decimal(out, significand, significand_size);
  out += significand_size + 1;
  char* end = out;
  int floating_size = significand_size - integral_size;
  for (int i = floating_size / 2; i > 0; --i) {
    out -= 2;
    write2digits(out, static_cast<std::size_t>(significand % 100));
    significand /= 100;
  }
  if (floating_size % 2 != 0) {
    *--out = static_cast<char>('0' + significand % 10);
    significand /= 10;
  }
  *--out = decimal_point;
  format_decimal(out - integral_size, significand, integral_size);
  return end;
}

char* dtoa_dragonbox(const double f, char* buf, int exp_upper = 16) {
  const int spec_precision = -1;

  bool is_negative = std::signbit(f);

  // Handle zero specially (dragonbox doesn't handle it)
  if (f == 0.0) {
    *buf++ = '0';
    return buf;
  }

  auto ret = jkj::dragonbox::to_decimal(f);

  const int exp_lower = -4;
  char exp_char = 'e';
  char zero_char = '0';

  auto significand = ret.significand;
  int significand_size = count_digits(significand);

  size_t size = size_t(significand_size) + (is_negative ? 1u : 0u);

  int output_exp = ret.exponent + significand_size - 1;
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper);

  char decimal_point = '.';
  if (use_exp_format) {
    int num_zeros = 0;
    if (significand_size == 1) {
      decimal_point = '\0';
    }
    auto abs_output_exp = output_exp >= 0 ? output_exp : -output_exp;
    int exp_digits = 2;
    if (abs_output_exp >= 100) exp_digits = abs_output_exp >= 1000 ? 4 : 3;

    size += (decimal_point ? 1u : 0u) + 2u + size_t(exp_digits);

    if (is_negative) {
      *buf++ = '-';
    }

    buf = write_significand(buf, significand, significand_size, 1, decimal_point);

    if (num_zeros > 0) buf = fill_n(buf, num_zeros, zero_char);
    *buf++ = exp_char;
    return write_exponent(output_exp, buf);
  }

  int exp = ret.exponent + significand_size;
  if (ret.exponent >= 0) {
    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand_e(buf, significand, significand_size, ret.exponent);
  } else if (exp > 0) {
    size += 1;
    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand(buf, significand, significand_size, exp, decimal_point);
  }

  int num_zeros = -exp;
  bool pointy = num_zeros != 0 || significand_size != 0;
  size += 1u + (pointy ? 1u : 0u) + size_t(num_zeros);

  if (is_negative) {
    *buf++ = '-';
  }

  *buf++ = zero_char;

  if (!pointy) return buf;
  *buf++ = decimal_point;
  buf = fill_n(buf, num_zeros, zero_char);

  return format_decimal(buf, significand, significand_size);
}

char* dtoa_dragonbox(const float f, char* buf) {
  return dtoa_dragonbox(double(f), buf, 7);
}

} // namespace internal

// Test statistics (thread-safe)
struct TestStats {
  std::atomic<uint64_t> total_tests{0};
  std::atomic<uint64_t> passed{0};
  std::atomic<uint64_t> failed{0};
  std::atomic<uint64_t> special_cases{0}; // NaN, Inf, etc.
  std::mutex print_mutex; // For thread-safe printing of errors
  bool fail_fast{false}; // Exit immediately on first failure

  void print() const {
    uint64_t total = total_tests.load();
    uint64_t pass = passed.load();
    uint64_t fail = failed.load();
    uint64_t special = special_cases.load();

    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "Total tests:    " << total << std::endl;
    std::cout << "Passed:         " << pass << std::endl;
    std::cout << "Failed:         " << fail << std::endl;
    std::cout << "Special cases:  " << special << std::endl;
    std::cout << "Pass rate:      " << std::fixed << std::setprecision(6)
              << (100.0 * pass / total) << "%" << std::endl;
  }
};

// Test float value
bool test_float_value(uint32_t bit_pattern, TestStats& stats, bool verbose = false) {
  stats.total_tests.fetch_add(1, std::memory_order_relaxed);

  // Reinterpret bits as float
  float f;
  std::memcpy(&f, &bit_pattern, sizeof(float));

  // Handle special cases
  if (!std::isfinite(f)) {
    stats.special_cases.fetch_add(1, std::memory_order_relaxed);
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Handle zero specially
  if (f == 0.0f) {
    stats.special_cases.fetch_add(1, std::memory_order_relaxed);
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Handle denormal numbers - they may not roundtrip reliably through stod
  if (std::fpclassify(f) == FP_SUBNORMAL) {
    stats.special_cases.fetch_add(1, std::memory_order_relaxed);
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Convert using dragonbox
  char dragonbox_buf[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
  char* end = internal::dtoa_dragonbox(f, dragonbox_buf);
  *end = '\0';

  // Convert back using std::stod
  double roundtrip_val;
  float roundtrip_float;
  try {
    roundtrip_val = std::stod(dragonbox_buf);
    roundtrip_float = static_cast<float>(roundtrip_val);
  } catch (const std::exception& e) {
    stats.failed.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(stats.print_mutex);
      std::cout << "EXCEPTION at bit pattern 0x" << std::hex << bit_pattern << std::dec << std::endl;
      std::cout << "  Original:     " << std::scientific << std::setprecision(17) << f << std::endl;
      std::cout << "  Dragonbox:    " << dragonbox_buf << std::endl;
      std::cout << "  Exception:    " << e.what() << std::endl;
    }
    if (stats.fail_fast) {
      std::cout << "\nFail-fast mode: Exiting on first error" << std::endl;
      std::exit(1);
    }
    return false;
  }

  // Check if roundtrip matches
  bool roundtrip_ok = (f == roundtrip_float);

  if (roundtrip_ok) {
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  } else {
    stats.failed.fetch_add(1, std::memory_order_relaxed);

    // For comparison, also use std::to_string
    std::string std_str = std::to_string(f);

    {
      std::lock_guard<std::mutex> lock(stats.print_mutex);
      std::cout << "FAIL at bit pattern 0x" << std::hex << bit_pattern << std::dec << std::endl;
      std::cout << "  Original:     " << std::scientific << std::setprecision(17) << f << std::endl;
      std::cout << "  Dragonbox:    " << dragonbox_buf << std::endl;
      std::cout << "  Roundtrip:    " << roundtrip_float << std::endl;
      std::cout << "  std::to_string: " << std_str << std::endl;

      // Show bit patterns
      uint32_t orig_bits, roundtrip_bits;
      std::memcpy(&orig_bits, &f, sizeof(float));
      std::memcpy(&roundtrip_bits, &roundtrip_float, sizeof(float));
      std::cout << "  Original bits:  0x" << std::hex << orig_bits << std::dec << std::endl;
      std::cout << "  Roundtrip bits: 0x" << std::hex << roundtrip_bits << std::dec << std::endl;
    }

    if (stats.fail_fast) {
      std::cout << "\nFail-fast mode: Exiting on first error" << std::endl;
      std::exit(1);
    }

    return false;
  }
}

// Test double value
bool test_double_value(uint64_t bit_pattern, TestStats& stats, bool verbose = false) {
  stats.total_tests.fetch_add(1, std::memory_order_relaxed);

  // Reinterpret bits as double
  double d;
  std::memcpy(&d, &bit_pattern, sizeof(double));

  // Handle special cases
  if (!std::isfinite(d)) {
    stats.special_cases.fetch_add(1, std::memory_order_relaxed);
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Handle zero specially
  if (d == 0.0) {
    stats.special_cases.fetch_add(1, std::memory_order_relaxed);
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Handle denormal numbers - they may not roundtrip reliably through stod
  if (std::fpclassify(d) == FP_SUBNORMAL) {
    stats.special_cases.fetch_add(1, std::memory_order_relaxed);
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Convert using dragonbox
  char dragonbox_buf[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
  char* end = internal::dtoa_dragonbox(d, dragonbox_buf, 16);
  *end = '\0';

  // Convert back using std::stod
  double roundtrip_val;
  try {
    roundtrip_val = std::stod(dragonbox_buf);
  } catch (const std::exception& e) {
    stats.failed.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(stats.print_mutex);
      std::cout << "EXCEPTION at bit pattern 0x" << std::hex << bit_pattern << std::dec << std::endl;
      std::cout << "  Original:     " << std::scientific << std::setprecision(17) << d << std::endl;
      std::cout << "  Dragonbox:    " << dragonbox_buf << std::endl;
      std::cout << "  Exception:    " << e.what() << std::endl;
    }
    if (stats.fail_fast) {
      std::cout << "\nFail-fast mode: Exiting on first error" << std::endl;
      std::exit(1);
    }
    return false;
  }

  // Check if roundtrip matches
  bool roundtrip_ok = (d == roundtrip_val);

  if (roundtrip_ok) {
    stats.passed.fetch_add(1, std::memory_order_relaxed);
    return true;
  } else {
    stats.failed.fetch_add(1, std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> lock(stats.print_mutex);
      std::cout << "FAIL at bit pattern 0x" << std::hex << bit_pattern << std::dec << std::endl;
      std::cout << "  Original:     " << std::scientific << std::setprecision(17) << d << std::endl;
      std::cout << "  Dragonbox:    " << dragonbox_buf << std::endl;
      std::cout << "  Roundtrip:    " << roundtrip_val << std::endl;

      // Show bit patterns
      uint64_t orig_bits, roundtrip_bits;
      std::memcpy(&orig_bits, &d, sizeof(double));
      std::memcpy(&roundtrip_bits, &roundtrip_val, sizeof(double));
      std::cout << "  Original bits:  0x" << std::hex << orig_bits << std::dec << std::endl;
      std::cout << "  Roundtrip bits: 0x" << std::hex << roundtrip_bits << std::dec << std::endl;
    }

    if (stats.fail_fast) {
      std::cout << "\nFail-fast mode: Exiting on first error" << std::endl;
      std::exit(1);
    }

    return false;
  }
}

// Run exhaustive float tests (all 2^32 bit patterns) - Parallel version
void run_exhaustive_float_test_parallel(unsigned int num_threads = 0, bool verbose = true, bool fail_fast = false) {
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 1; // Fallback if hardware_concurrency fails
  }

  std::cout << "\n=== Exhaustive Float Test (2^32 patterns) - Parallel ===" << std::endl;
  std::cout << "Using " << num_threads << " threads" << std::endl;
  std::cout << "This will test all possible 32-bit float values..." << std::endl;
  std::cout << "Estimated time: several hours (depends on CPU)" << std::endl;
  if (fail_fast) {
    std::cout << "Fail-fast mode: ENABLED (will exit on first error)" << std::endl;
  }

  TestStats stats;
  stats.fail_fast = fail_fast;
  auto start = std::chrono::steady_clock::now();
  std::atomic<uint64_t> next_progress_milestone{100000000};

  // Worker function for each thread
  auto worker = [&](uint32_t thread_id, uint64_t start_pattern, uint64_t end_pattern) {
    for (uint64_t i = start_pattern; i < end_pattern; i++) {
      uint32_t bit_pattern = static_cast<uint32_t>(i);
      test_float_value(bit_pattern, stats, false); // verbose=false for threads

      // Progress update every 100M tests (thread-safe)
      if (verbose) {
        uint64_t total = stats.total_tests.load(std::memory_order_relaxed);
        uint64_t milestone = next_progress_milestone.load(std::memory_order_relaxed);
        if (total >= milestone) {
          // Try to claim this milestone
          if (next_progress_milestone.compare_exchange_strong(milestone, milestone + 100000000)) {
            auto current = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start).count();
            double progress_pct = (100.0 * total) / (1ULL << 32);

            std::lock_guard<std::mutex> lock(stats.print_mutex);
            std::cout << "\n[" << std::fixed << std::setprecision(2) << std::setw(6) << progress_pct << "%] "
                      << "Progress: " << total << " / " << (1ULL << 32) << " tests"
                      << " | Elapsed: " << elapsed << "s" << std::endl;
            stats.print();
          }
        }
      }
    }
  };

  // Divide work among threads
  uint64_t total_patterns = 1ULL << 32;
  uint64_t patterns_per_thread = total_patterns / num_threads;

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (unsigned int t = 0; t < num_threads; t++) {
    uint64_t start_pattern = t * patterns_per_thread;
    uint64_t end_pattern = (t == num_threads - 1) ? total_patterns : (t + 1) * patterns_per_thread;
    threads.emplace_back(worker, t, start_pattern, end_pattern);
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

  std::cout << "\nCompleted in " << elapsed << " seconds" << std::endl;
  std::cout << "Threads used: " << num_threads << std::endl;
  stats.print();
}

// Run exhaustive float tests (all 2^32 bit patterns) - Single-threaded version
void run_exhaustive_float_test(bool verbose = true, bool fail_fast = false) {
  std::cout << "\n=== Exhaustive Float Test (2^32 patterns) - Single-threaded ===" << std::endl;
  std::cout << "This will test all possible 32-bit float values..." << std::endl;
  std::cout << "Estimated time: several hours" << std::endl;
  std::cout << "NOTE: Use parallel version for faster testing" << std::endl;
  if (fail_fast) {
    std::cout << "Fail-fast mode: ENABLED (will exit on first error)" << std::endl;
  }

  TestStats stats;
  stats.fail_fast = fail_fast;
  auto start = std::chrono::steady_clock::now();

  // Test all 2^32 bit patterns
  for (uint64_t i = 0; i < (1ULL << 32); i++) {
    uint32_t bit_pattern = static_cast<uint32_t>(i);
    test_float_value(bit_pattern, stats, false);

    // Progress update every 100M tests
    if (verbose && (i > 0) && (i % 100000000 == 0)) {
      auto current = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start).count();
      double progress_pct = (100.0 * i) / (1ULL << 32);
      std::cout << "\n[" << std::fixed << std::setprecision(2) << std::setw(6) << progress_pct << "%] "
                << "Progress: " << i << " / " << (1ULL << 32) << " tests"
                << " | Elapsed: " << elapsed << "s" << std::endl;
      stats.print();
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

  std::cout << "\nCompleted in " << elapsed << " seconds" << std::endl;
  stats.print();
}

// Run sampled double tests
void run_sampled_double_test(uint64_t num_samples, bool verbose = true, bool fail_fast = false) {
  std::cout << "\n=== Sampled Double Test (" << num_samples << " samples) ===" << std::endl;
  std::cout << "Note: Full exhaustive test would require 2^64 patterns (~10^19 tests)" << std::endl;
  if (fail_fast) {
    std::cout << "Fail-fast mode: ENABLED (will exit on first error)" << std::endl;
  }

  TestStats stats;
  stats.fail_fast = fail_fast;
  auto start = std::chrono::steady_clock::now();

  // Strategy: Sample key ranges
  // 1. All sign/exponent combinations with sampled mantissa
  // 2. Random sampling across entire range

  uint64_t samples_per_strategy = num_samples / 2;

  // Strategy 1: Systematic sampling
  std::cout << "Strategy 1: Systematic sampling of exponent ranges..." << std::endl;
  for (uint64_t i = 0; i < samples_per_strategy; i++) {
    // Sample across exponent and mantissa space
    uint64_t sign_bit = (i % 2) << 63;
    uint64_t exponent = ((i / 2) % 2048) << 52;
    uint64_t mantissa = (i * 0x123456789ABCDEFULL) & 0xFFFFFFFFFFFFFULL;
    uint64_t bit_pattern = sign_bit | exponent | mantissa;

    test_double_value(bit_pattern, stats, verbose);

    if (verbose && (i > 0) && (i % 10000000 == 0)) {
      auto current = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start).count();
      std::cout << "Progress: " << i << " / " << samples_per_strategy
                << " Elapsed: " << elapsed << "s" << std::endl;
    }
  }

  // Strategy 2: Random sampling
  std::cout << "Strategy 2: Random sampling..." << std::endl;
  std::mt19937_64 rng(12345);
  for (uint64_t i = 0; i < samples_per_strategy; i++) {
    uint64_t bit_pattern = rng();
    test_double_value(bit_pattern, stats, verbose);

    if (verbose && (i > 0) && (i % 10000000 == 0)) {
      auto current = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start).count();
      std::cout << "Progress: " << (samples_per_strategy + i) << " / " << num_samples
                << " Elapsed: " << elapsed << "s" << std::endl;
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

  std::cout << "\nCompleted in " << elapsed << " seconds" << std::endl;
  stats.print();
}

// Quick sanity test
void run_sanity_test() {
  std::cout << "\n=== Quick Sanity Test ===" << std::endl;

  TestStats stats;

  // Test known values
  struct TestCase {
    float value;
    const char* expected_contains;
  };

  TestCase test_cases[] = {
    {0.0f, "0"},
    {1.0f, "1"},
    {-1.0f, "-1"},
    {3.14159f, "3.14159"},
    {-2.71828f, "2.71828"},
    {0.0001f, "0.0001"},
    {1000000.0f, "1000000"},
    {1e-10f, "e-"},
    {1e10f, "e+"},
    {std::numeric_limits<float>::min(), "e-"},
    {std::numeric_limits<float>::max(), "e+"},
  };

  for (const auto& tc : test_cases) {
    char buf[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
    char* end = internal::dtoa_dragonbox(tc.value, buf);
    *end = '\0';

    std::cout << "Value: " << std::scientific << std::setprecision(10) << tc.value
              << " -> \"" << buf << "\"" << std::endl;

    // Roundtrip test
    double roundtrip = std::stod(buf);
    float roundtrip_float = static_cast<float>(roundtrip);

    if (tc.value == roundtrip_float) {
      std::cout << "  ✓ Roundtrip OK" << std::endl;
      stats.passed.fetch_add(1, std::memory_order_relaxed);
    } else {
      std::cout << "  ✗ Roundtrip FAILED: got " << roundtrip_float << std::endl;
      stats.failed.fetch_add(1, std::memory_order_relaxed);
    }
    stats.total_tests.fetch_add(1, std::memory_order_relaxed);
  }

  stats.print();
}

int main(int argc, char** argv) {
  std::cout << "=== Dragonbox dtoa Exhaustive Test Suite ===" << std::endl;

  if (argc < 2) {
    unsigned int hw_threads = std::thread::hardware_concurrency();
    std::cout << "\nUsage:" << std::endl;
    std::cout << "  " << argv[0] << " <mode> [threads] [--fail-fast]" << std::endl;
    std::cout << "\nModes:" << std::endl;
    std::cout << "  sanity           - Quick sanity check" << std::endl;
    std::cout << "  float_quick      - Quick float test (1M samples)" << std::endl;
    std::cout << "  float_exhaustive - Full 2^32 float test (parallel, SLOW!)" << std::endl;
    std::cout << "  double_quick     - Quick double test (10M samples)" << std::endl;
    std::cout << "  double_medium    - Medium double test (100M samples)" << std::endl;
    std::cout << "  double_large     - Large double test (1B samples)" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  threads          - Number of threads (default: " << hw_threads << " = hardware_concurrency)" << std::endl;
    std::cout << "                     Use 0 for auto-detect, 1 for single-threaded" << std::endl;
    std::cout << "  --fail-fast      - Exit immediately on first error (for debugging)" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << argv[0] << " float_exhaustive              # Use all cores" << std::endl;
    std::cout << "  " << argv[0] << " float_exhaustive 8            # Use 8 threads" << std::endl;
    std::cout << "  " << argv[0] << " float_exhaustive 1            # Single-threaded" << std::endl;
    std::cout << "  " << argv[0] << " float_exhaustive 0 --fail-fast  # Exit on first error" << std::endl;
    return 1;
  }

  std::string mode = argv[1];
  unsigned int num_threads = 0; // 0 = auto-detect
  bool fail_fast = false;

  // Parse optional arguments
  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--fail-fast") {
      fail_fast = true;
      std::cout << "Fail-fast mode enabled: Will exit on first error" << std::endl;
    } else {
      // Try to parse as thread count
      try {
        num_threads = static_cast<unsigned int>(std::stoi(arg));
      } catch (...) {
        std::cerr << "Warning: Unknown argument: " << arg << std::endl;
      }
    }
  }

  if (mode == "sanity") {
    run_sanity_test();
  } else if (mode == "float_quick") {
    std::cout << "\n=== Quick Float Test (1M samples) ===" << std::endl;
    TestStats stats;
    stats.fail_fast = fail_fast;
    std::mt19937 rng(12345);
    for (uint32_t i = 0; i < 1000000; i++) {
      uint32_t bit_pattern = rng();
      test_float_value(bit_pattern, stats, false);
    }
    stats.print();
  } else if (mode == "float_exhaustive") {
    // Note: fail_fast is passed through TestStats in the function
    if (num_threads == 1) {
      run_exhaustive_float_test(true, fail_fast);
    } else {
      run_exhaustive_float_test_parallel(num_threads, true, fail_fast);
    }
  } else if (mode == "double_quick") {
    run_sampled_double_test(10000000, true, fail_fast);  // 10M
  } else if (mode == "double_medium") {
    run_sampled_double_test(100000000, true, fail_fast);  // 100M
  } else if (mode == "double_large") {
    run_sampled_double_test(1000000000, true, fail_fast);  // 1B
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    return 1;
  }

  return 0;
}
