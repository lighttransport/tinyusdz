// Test integration of dtos in str-util
#include "src/str-util.hh"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace tinyusdz;

bool test_basic() {
  std::cout << "Testing basic values..." << std::endl;

  // Test 1.0 and -1.0 (fast path)
  assert(dtos(1.0) == "1");
  assert(dtos(-1.0) == "-1");
  assert(dtos(1.0f) == "1");
  assert(dtos(-1.0f) == "-1");

  // Test 0.0
  assert(dtos(0.0) == "0");
  assert(dtos(0.0f) == "0");

  // Test powers of 10
  assert(dtos(10.0) == "10");
  assert(dtos(100.0) == "100");
  assert(dtos(1000.0) == "1000");

  // Test fractional
  assert(dtos(0.1) == "0.1");
  assert(dtos(0.01) == "0.01");

  std::cout << "  All basic tests passed!" << std::endl;
  return true;
}

bool test_scientific() {
  std::cout << "Testing scientific notation..." << std::endl;

  // Large values use scientific notation
  std::string result = dtos(1e20);
  assert(result.find('e') != std::string::npos);
  std::cout << "  1e20 -> " << result << std::endl;

  // Small values use scientific notation
  result = dtos(1e-10);
  assert(result.find('e') != std::string::npos);
  std::cout << "  1e-10 -> " << result << std::endl;

  std::cout << "  Scientific notation tests passed!" << std::endl;
  return true;
}

bool test_roundtrip() {
  std::cout << "Testing round-trip conversion..." << std::endl;

  double values[] = {0.3, 123.456, 3.14159265358979, 2.71828182845904};

  for (double v : values) {
    std::string s = dtos(v);
    double parsed = std::strtod(s.c_str(), nullptr);
    assert(parsed == v);
    std::cout << "  " << v << " -> \"" << s << "\" -> " << parsed << " [OK]" << std::endl;
  }

  std::cout << "  Round-trip tests passed!" << std::endl;
  return true;
}

int main() {
  std::cout << "=== Testing dtos integration in str-util ===" << std::endl;
  std::cout << std::endl;

  bool all_pass = true;
  all_pass &= test_basic();
  all_pass &= test_scientific();
  all_pass &= test_roundtrip();

  std::cout << std::endl;
  if (all_pass) {
    std::cout << "All tests PASSED!" << std::endl;
    return 0;
  } else {
    std::cout << "Some tests FAILED!" << std::endl;
    return 1;
  }
}
