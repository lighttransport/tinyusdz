// Performance test for fast path optimization (1.0 and -1.0)
// Demonstrates the speedup from bitwise comparison for common values

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>

#include "dragonbox_to_chars.h"

namespace internal {

constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE = 32;

// Helper functions (minimal set needed for testing)
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

// WITH fast path (optimized version)
char* dtoa_dragonbox_fast(const double f, char* buf) {
  // Fast path for common values 1.0 and -1.0 (bitwise comparison)
  uint64_t bits;
  std::memcpy(&bits, &f, sizeof(double));

  if (bits == 0x3FF0000000000000ULL) {
    *buf++ = '1';
    return buf;
  }
  if (bits == 0xBFF0000000000000ULL) {
    *buf++ = '-';
    *buf++ = '1';
    return buf;
  }

  // Fallback to dragonbox (simplified - just handle these specific cases)
  if (f == 0.0) {
    *buf++ = '0';
    return buf;
  }

  auto ret = jkj::dragonbox::to_decimal(f);
  auto significand = ret.significand;
  int significand_size = count_digits(significand);

  // Simple formatting for demo
  char temp[32];
  sprintf(temp, "%llu", (unsigned long long)significand);
  memcpy(buf, temp, significand_size);
  return buf + significand_size;
}

// WITHOUT fast path (original version)
char* dtoa_dragonbox_slow(const double f, char* buf) {
  // No fast path - always use dragonbox

  if (f == 0.0) {
    *buf++ = '0';
    return buf;
  }

  auto ret = jkj::dragonbox::to_decimal(f);
  auto significand = ret.significand;
  int significand_size = count_digits(significand);

  // Handle 1.0 and -1.0 through normal path
  bool is_negative = std::signbit(f);

  if (is_negative) {
    *buf++ = '-';
  }

  // Simple formatting for demo
  char temp[32];
  sprintf(temp, "%llu", (unsigned long long)significand);
  memcpy(buf, temp, significand_size);
  return buf + significand_size;
}

} // namespace internal

void benchmark_fast_path() {
  const size_t iterations = 10000000;  // 10 million iterations

  std::vector<double> test_values = {1.0, -1.0, 1.0, -1.0};  // Mostly 1.0 and -1.0

  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];

  std::cout << "Benchmarking with " << iterations << " iterations...\n" << std::endl;

  // Benchmark WITH fast path
  auto start_fast = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < iterations; i++) {
    double val = test_values[i % test_values.size()];
    char* end = internal::dtoa_dragonbox_fast(val, buffer);
    *end = '\0';
    // Prevent optimization
    if (buffer[0] == 'X') std::cout << "";
  }
  auto end_fast = std::chrono::high_resolution_clock::now();

  // Benchmark WITHOUT fast path
  auto start_slow = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < iterations; i++) {
    double val = test_values[i % test_values.size()];
    char* end = internal::dtoa_dragonbox_slow(val, buffer);
    *end = '\0';
    // Prevent optimization
    if (buffer[0] == 'X') std::cout << "";
  }
  auto end_slow = std::chrono::high_resolution_clock::now();

  auto duration_fast = std::chrono::duration_cast<std::chrono::milliseconds>(end_fast - start_fast).count();
  auto duration_slow = std::chrono::duration_cast<std::chrono::milliseconds>(end_slow - start_slow).count();

  std::cout << "Results:" << std::endl;
  std::cout << "--------" << std::endl;
  std::cout << "WITH fast path:    " << duration_fast << " ms" << std::endl;
  std::cout << "WITHOUT fast path: " << duration_slow << " ms" << std::endl;
  std::cout << std::endl;

  if (duration_fast > 0) {
    double speedup = static_cast<double>(duration_slow) / static_cast<double>(duration_fast);
    std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x faster" << std::endl;
  }

  std::cout << std::endl;
  std::cout << "Note: Fast path uses bitwise comparison (O(1)) for exact 1.0 and -1.0" << std::endl;
  std::cout << "      instead of full dragonbox algorithm (more complex)." << std::endl;
}

void test_correctness() {
  std::cout << "=== Testing Fast Path Correctness ===" << std::endl;
  std::cout << std::endl;

  char buffer_fast[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
  char buffer_slow[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];

  std::vector<std::pair<double, std::string>> test_cases = {
    {1.0, "1.0"},
    {-1.0, "-1.0"},
    {0.0, "0.0"},
  };

  bool all_passed = true;

  for (const auto& test : test_cases) {
    char* end_fast = internal::dtoa_dragonbox_fast(test.first, buffer_fast);
    *end_fast = '\0';

    char* end_slow = internal::dtoa_dragonbox_slow(test.first, buffer_slow);
    *end_slow = '\0';

    std::string result_fast(buffer_fast);
    std::string result_slow(buffer_slow);

    std::cout << std::setw(10) << test.second << " -> ";
    std::cout << "Fast: " << std::setw(6) << result_fast << ", ";
    std::cout << "Slow: " << std::setw(6) << result_slow;

    if (result_fast == result_slow) {
      std::cout << " [OK]" << std::endl;
    } else {
      std::cout << " [MISMATCH]" << std::endl;
      all_passed = false;
    }
  }

  std::cout << std::endl;
  if (all_passed) {
    std::cout << "All correctness tests PASSED!" << std::endl;
  } else {
    std::cout << "Some correctness tests FAILED!" << std::endl;
  }
  std::cout << std::endl;
}

int main(int argc, char** argv) {
  std::cout << "=== Fast Path Performance Test ===" << std::endl;
  std::cout << std::endl;

  // First verify correctness
  test_correctness();

  std::cout << std::endl;
  std::cout << "=== Performance Benchmark ===" << std::endl;
  std::cout << std::endl;

  // Then benchmark performance
  benchmark_fast_path();

  return 0;
}
