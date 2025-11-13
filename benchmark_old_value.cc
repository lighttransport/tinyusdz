// Benchmark for original linb::any-based Value implementation
// Compare against Value32 results

#include <iostream>
#include <chrono>
#include <string>
#include <iomanip>

// Use original Value (linb::any-based)
#include "src/value-types.hh"

using namespace tinyusdz::value;

class Timer {
public:
  Timer() : start_(std::chrono::high_resolution_clock::now()) {}

  double elapsed_ms() const {
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start_).count();
  }

private:
  std::chrono::high_resolution_clock::time_point start_;
};

constexpr size_t ITERATIONS = 1000000;
constexpr size_t ACCESS_ITERATIONS = 10000000;

void print_header() {
  std::cout << "=== Original Value (linb::any) Performance Benchmark ===\n\n";
  std::cout << "sizeof(Value) = " << sizeof(Value) << " bytes\n";
  std::cout << "Stack storage in linb::any: 2 * sizeof(void*) = " << (2 * sizeof(void*)) << " bytes\n";
  std::cout << "Iterations: " << ITERATIONS << " (10M for access test)\n\n";
  std::cout << std::left << std::setw(40) << "Operation"
            << std::right << std::setw(15) << "Time (ms)"
            << std::setw(20) << "ns/op"
            << std::setw(15) << "Mop/s\n";
  std::cout << std::string(90, '-') << "\n";
}

void print_result(const char* name, double ms, size_t iterations) {
  double ns_per_op = (ms * 1000000.0) / iterations;
  double mops = iterations / (ms * 1000.0);

  std::cout << std::left << std::setw(40) << name
            << std::right << std::setw(15) << std::fixed << std::setprecision(3) << ms
            << std::setw(20) << std::setprecision(2) << ns_per_op
            << std::setw(15) << std::setprecision(2) << mops << "\n";
}

int main() {
  print_header();

  volatile int sink_i = 0;
  volatile double sink_d = 0;
  volatile size_t sink_s = 0;

  // 1. Construct inline (int32_t) - 4 bytes, fits in 16-byte stack
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS; ++i) {
      Value v(int32_t(42));
      const int32_t* ptr = v.as<int32_t>();
      if (ptr) sink_i += *ptr;
    }
    print_result("Construct inline (int32_t)", timer.elapsed_ms(), ITERATIONS);
  }

  // 2. Construct inline (double) - 8 bytes, fits in 16-byte stack
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS; ++i) {
      Value v(3.14159);
      const double* ptr = v.as<double>();
      if (ptr) sink_d += *ptr;
    }
    print_result("Construct inline (double)", timer.elapsed_ms(), ITERATIONS);
  }

  // 3. Construct heap (std::string) - typically 32 bytes, uses heap
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS / 10; ++i) {
      std::string str = "Hello, World!";
      Value v(str);
      const std::string* ptr = v.as<std::string>();
      if (ptr) sink_s += ptr->size();
    }
    print_result("Construct heap (std::string)", timer.elapsed_ms(), ITERATIONS / 10);
  }

  // 4. Copy (inline) - int32_t
  // Note: Direct Value copy triggers template recursion, so we test construct+destruct cost
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS; ++i) {
      Value v1(int32_t(42));
      Value v2(int32_t(42));  // Construct again (copy semantics)
      const int32_t* ptr = v2.as<int32_t>();
      if (ptr) sink_i += *ptr;
    }
    print_result("Copy (inline int32_t) [construct×2]", timer.elapsed_ms(), ITERATIONS);
  }

  // 5. Copy (heap) - std::string
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS / 10; ++i) {
      std::string str = "test";
      Value v1(str);
      Value v2(str);  // Construct again (copy semantics)
      const std::string* ptr = v2.as<std::string>();
      if (ptr) sink_s += ptr->size();
    }
    print_result("Copy (heap std::string) [construct×2]", timer.elapsed_ms(), ITERATIONS / 10);
  }

  // 6. Move (inline) - int32_t
  // Note: linb::any move is same as copy for inline types, measure construct cost
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS; ++i) {
      Value v1(int32_t(42));
      const int32_t* ptr = v1.as<int32_t>();
      if (ptr) sink_i += *ptr;
    }
    print_result("Move (inline int32_t) [construct only]", timer.elapsed_ms(), ITERATIONS);
  }

  // 7. Move (heap) - std::string
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS / 10; ++i) {
      std::string str = "test";
      Value v1(str);
      const std::string* ptr = v1.as<std::string>();
      if (ptr) sink_s += ptr->size();
    }
    print_result("Move (heap std::string) [construct only]", timer.elapsed_ms(), ITERATIONS / 10);
  }

  // 8. Access (inline) - int32_t
  {
    Value v(int32_t(42));
    Timer timer;
    for (size_t i = 0; i < ACCESS_ITERATIONS; ++i) {
      const int32_t* ptr = v.as<int32_t>();
      if (ptr) sink_i += *ptr;
    }
    print_result("Access via as<T>() (inline)", timer.elapsed_ms(), ACCESS_ITERATIONS);
  }

  // 9. Access (heap) - std::string
  {
    std::string str = "test";
    Value v(str);
    Timer timer;
    for (size_t i = 0; i < ACCESS_ITERATIONS; ++i) {
      const std::string* ptr = v.as<std::string>();
      if (ptr) sink_s += ptr->size();
    }
    print_result("Access via as<T>() (heap)", timer.elapsed_ms(), ACCESS_ITERATIONS);
  }

  // 10. Type queries
  {
    Value v(int32_t(42));
    Timer timer;
    for (size_t i = 0; i < ACCESS_ITERATIONS; ++i) {
      sink_i += v.type_id();
    }
    print_result("type_id() query", timer.elapsed_ms(), ACCESS_ITERATIONS);
  }

  // 11. Mixed workload
  {
    Timer timer;
    for (size_t i = 0; i < ITERATIONS / 10; ++i) {
      Value v_int(int32_t(i % 1000));
      const int32_t* p1 = v_int.as<int32_t>();
      if (p1) sink_i += *p1;

      Value v_double(3.14159 * i);
      const double* p2 = v_double.as<double>();
      if (p2) sink_d += *p2;

      Value v_copy(int32_t(i % 1000));  // Construct instead of copy
      const int32_t* p3 = v_copy.as<int32_t>();
      if (p3) sink_i += *p3;

      if (i % 10 == 0) {
        std::string str = "test";
        Value v_str(str);
        const std::string* p4 = v_str.as<std::string>();
        if (p4) sink_s += p4->size();
      }
    }
    print_result("Mixed workload (realistic)", timer.elapsed_ms(), ITERATIONS / 10);
  }

  std::cout << "\n";
  std::cout << "=== Analysis ===\n\n";
  std::cout << "This is the BASELINE - original linb::any-based Value.\n";
  std::cout << "Compare these results with Value32 benchmark results.\n\n";

  std::cout << "linb::any characteristics:\n";
  std::cout << "  - Stack storage: " << (2 * sizeof(void*)) << " bytes (2 * sizeof(void*))\n";
  std::cout << "  - Vtable pointer: " << sizeof(void*) << " bytes\n";
  std::cout << "  - Total size: " << sizeof(Value) << " bytes\n";
  std::cout << "  - Types ≤16 bytes: inline (int32_t, double, int64_t, pointers)\n";
  std::cout << "  - Types >16 bytes: heap (std::string, float3, float4, etc.)\n\n";

  return 0;
}
