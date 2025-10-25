#include "parse_fp16.hh"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cassert>

using namespace fp16;

// Test helper to print fp16 in binary format
void print_fp16_bits(uint16_t value) {
  fp16_bits bits;
  bits.u = value;
  std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0') << value
            << " [sign=" << bits.parts.sign
            << ", exp=" << std::dec << bits.parts.exponent
            << ", mantissa=" << bits.parts.mantissa << "]";
}

// Test structure for organizing test cases
struct TestCase {
  std::string input;
  std::string description;
  bool should_succeed;
  uint16_t expected_value;  // Only checked if should_succeed is true

  TestCase(const std::string& in, const std::string& desc, bool success = true, uint16_t expected = 0)
    : input(in), description(desc), should_succeed(success), expected_value(expected) {}
};

bool test_basic_parsing() {
  std::cout << "\n=== Test: Basic Parsing ===" << std::endl;

  std::vector<TestCase> tests = {
    {"0", "zero", true, 0x0000},
    {"-0", "negative zero", true, 0x8000},
    {"1", "one", true, fp32_to_fp16(1.0f)},
    {"-1", "negative one", true, fp32_to_fp16(-1.0f)},
    {"2", "two", true, fp32_to_fp16(2.0f)},
    {"0.5", "one half", true, fp32_to_fp16(0.5f)},
    {"0.25", "one quarter", true, fp32_to_fp16(0.25f)},
    {"3.14159", "pi approximation", true, fp32_to_fp16(3.14159f)},
    {"-2.71828", "negative e approximation", true, fp32_to_fp16(-2.71828f)},
    {"100", "hundred", true, fp32_to_fp16(100.0f)},
    {"1000", "thousand", true, fp32_to_fp16(1000.0f)},
    {"0.001", "one thousandth", true, fp32_to_fp16(0.001f)},
    {"0.0001", "ten thousandth", true, fp32_to_fp16(0.0001f)},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    parse_result result = parse_fp16(test.input.c_str());

    if (result.success != test.should_succeed) {
      std::cout << "FAIL: " << test.description << " - Expected "
                << (test.should_succeed ? "success" : "failure")
                << " but got " << (result.success ? "success" : "failure") << std::endl;
      failed++;
      continue;
    }

    if (test.should_succeed && result.value != test.expected_value) {
      std::cout << "FAIL: " << test.description << " - Value mismatch" << std::endl;
      std::cout << "  Input: " << test.input << std::endl;
      std::cout << "  Expected: ";
      print_fp16_bits(test.expected_value);
      std::cout << " = " << fp16_to_fp32(test.expected_value) << std::endl;
      std::cout << "  Got:      ";
      print_fp16_bits(result.value);
      std::cout << " = " << fp16_to_fp32(result.value) << std::endl;
      failed++;
      continue;
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_special_values() {
  std::cout << "\n=== Test: Special Values (Infinity, NaN) ===" << std::endl;

  std::vector<TestCase> tests = {
    {"inf", "positive infinity", true, 0x7C00},
    {"-inf", "negative infinity", true, 0xFC00},
    {"infinity", "positive infinity (long)", true, 0x7C00},
    {"-infinity", "negative infinity (long)", true, 0xFC00},
    {"INF", "positive infinity (uppercase)", true, 0x7C00},
    {"-INF", "negative infinity (uppercase)", true, 0xFC00},
    {"nan", "NaN", true},
    {"-nan", "negative NaN", true},
    {"NaN", "NaN (mixed case)", true},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    parse_result result = parse_fp16(test.input.c_str());

    if (result.success != test.should_succeed) {
      std::cout << "FAIL: " << test.description << " - Expected "
                << (test.should_succeed ? "success" : "failure")
                << " but got " << (result.success ? "success" : "failure") << std::endl;
      failed++;
      continue;
    }

    if (test.should_succeed) {
      fp16_bits bits;
      bits.u = result.value;

      // Check infinity
      if (test.input.find("inf") != std::string::npos ||
          test.input.find("INF") != std::string::npos) {
        if (bits.parts.exponent != 0x1F || bits.parts.mantissa != 0) {
          std::cout << "FAIL: " << test.description << " - Not infinity" << std::endl;
          std::cout << "  Got: ";
          print_fp16_bits(result.value);
          std::cout << std::endl;
          failed++;
          continue;
        }

        bool should_be_negative = (test.input[0] == '-');
        if (bits.parts.sign != (should_be_negative ? 1 : 0)) {
          std::cout << "FAIL: " << test.description << " - Wrong sign" << std::endl;
          failed++;
          continue;
        }
      }

      // Check NaN
      if (test.input.find("nan") != std::string::npos ||
          test.input.find("NaN") != std::string::npos) {
        if (bits.parts.exponent != 0x1F || bits.parts.mantissa == 0) {
          std::cout << "FAIL: " << test.description << " - Not NaN" << std::endl;
          std::cout << "  Got: ";
          print_fp16_bits(result.value);
          std::cout << std::endl;
          failed++;
          continue;
        }
      }
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_exponent_notation() {
  std::cout << "\n=== Test: Exponent Notation ===" << std::endl;

  std::vector<TestCase> tests = {
    {"1e0", "1 * 10^0", true, fp32_to_fp16(1.0f)},
    {"1e1", "1 * 10^1", true, fp32_to_fp16(10.0f)},
    {"1e2", "1 * 10^2", true, fp32_to_fp16(100.0f)},
    {"1e3", "1 * 10^3", true, fp32_to_fp16(1000.0f)},
    {"1e-1", "1 * 10^-1", true, fp32_to_fp16(0.1f)},
    {"1e-2", "1 * 10^-2", true, fp32_to_fp16(0.01f)},
    {"1e-3", "1 * 10^-3", true, fp32_to_fp16(0.001f)},
    {"2.5e2", "2.5 * 10^2", true, fp32_to_fp16(250.0f)},
    {"3.14e1", "3.14 * 10^1", true, fp32_to_fp16(31.4f)},
    {"-1.5e-1", "-1.5 * 10^-1", true, fp32_to_fp16(-0.15f)},
    {"1E2", "uppercase E", true, fp32_to_fp16(100.0f)},
    {"1e+2", "explicit positive exponent", true, fp32_to_fp16(100.0f)},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    parse_result result = parse_fp16(test.input.c_str());

    if (result.success != test.should_succeed) {
      std::cout << "FAIL: " << test.description << " - Expected "
                << (test.should_succeed ? "success" : "failure")
                << " but got " << (result.success ? "success" : "failure") << std::endl;
      failed++;
      continue;
    }

    if (test.should_succeed && result.value != test.expected_value) {
      float expected_f32 = fp16_to_fp32(test.expected_value);
      float got_f32 = fp16_to_fp32(result.value);

      // Allow small relative error due to fp16 precision
      float rel_error = std::abs(expected_f32 - got_f32) / std::max(std::abs(expected_f32), 1e-7f);

      if (rel_error > 0.01f) { // 1% tolerance
        std::cout << "FAIL: " << test.description << " - Value mismatch" << std::endl;
        std::cout << "  Input: " << test.input << std::endl;
        std::cout << "  Expected: " << expected_f32 << " (";
        print_fp16_bits(test.expected_value);
        std::cout << ")" << std::endl;
        std::cout << "  Got:      " << got_f32 << " (";
        print_fp16_bits(result.value);
        std::cout << ")" << std::endl;
        std::cout << "  Relative error: " << rel_error << std::endl;
        failed++;
        continue;
      }
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_overflow_underflow() {
  std::cout << "\n=== Test: Overflow and Underflow ===" << std::endl;

  std::vector<TestCase> tests = {
    {"65504", "max fp16 normal", true, fp32_to_fp16(65504.0f)},
    {"65520", "overflow to infinity", true, 0x7C00},
    {"100000", "large overflow", true, 0x7C00},
    {"-100000", "large negative overflow", true, 0xFC00},
    {"0.00006103515625", "min positive normal fp16", true, fp32_to_fp16(0.00006103515625f)},
    {"0.000000059604645", "min positive subnormal fp16", true, fp32_to_fp16(0.000000059604645f)},
    {"0.00000001", "underflow to zero", true, fp32_to_fp16(0.00000001f)},
    {"1e-10", "very small underflow", true, fp32_to_fp16(1e-10f)},
    {"1e10", "large overflow with exponent", true, 0x7C00},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    parse_result result = parse_fp16(test.input.c_str());

    if (result.success != test.should_succeed) {
      std::cout << "FAIL: " << test.description << " - Expected "
                << (test.should_succeed ? "success" : "failure")
                << " but got " << (result.success ? "success" : "failure") << std::endl;
      failed++;
      continue;
    }

    if (test.should_succeed) {
      // For overflow/underflow tests, check the result matches expected
      if (result.value != test.expected_value) {
        std::cout << "FAIL: " << test.description << " - Value mismatch" << std::endl;
        std::cout << "  Input: " << test.input << std::endl;
        std::cout << "  Expected: ";
        print_fp16_bits(test.expected_value);
        std::cout << " = " << fp16_to_fp32(test.expected_value) << std::endl;
        std::cout << "  Got:      ";
        print_fp16_bits(result.value);
        std::cout << " = " << fp16_to_fp32(result.value) << std::endl;
        failed++;
        continue;
      }
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_invalid_input() {
  std::cout << "\n=== Test: Invalid Input ===" << std::endl;

  std::vector<TestCase> tests = {
    {"", "empty string", false},
    {"   ", "whitespace only", false},
    {"abc", "non-numeric", false},
    {"1.2.3", "multiple dots", false},
    {"-", "just minus sign", false},
    {"+", "just plus sign", false},
    {"e10", "missing mantissa", false},
    {"1e", "incomplete exponent", false},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    parse_result result = parse_fp16(test.input.c_str());

    if (result.success != test.should_succeed) {
      std::cout << "FAIL: " << test.description << " - Expected "
                << (test.should_succeed ? "success" : "failure")
                << " but got " << (result.success ? "success" : "failure") << std::endl;
      std::cout << "  Input: \"" << test.input << "\"" << std::endl;
      failed++;
      continue;
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_whitespace_handling() {
  std::cout << "\n=== Test: Whitespace Handling ===" << std::endl;

  std::vector<TestCase> tests = {
    {"  1", "leading spaces", true, fp32_to_fp16(1.0f)},
    {"\t1", "leading tab", true, fp32_to_fp16(1.0f)},
    {"\n1", "leading newline", true, fp32_to_fp16(1.0f)},
    {"  -1.5", "leading spaces with sign", true, fp32_to_fp16(-1.5f)},
    {"  1e2", "leading spaces with exponent", true, fp32_to_fp16(100.0f)},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    parse_result result = parse_fp16(test.input.c_str());

    if (result.success != test.should_succeed) {
      std::cout << "FAIL: " << test.description << " - Expected "
                << (test.should_succeed ? "success" : "failure")
                << " but got " << (result.success ? "success" : "failure") << std::endl;
      failed++;
      continue;
    }

    if (test.should_succeed && result.value != test.expected_value) {
      std::cout << "FAIL: " << test.description << " - Value mismatch" << std::endl;
      std::cout << "  Input: \"" << test.input << "\"" << std::endl;
      std::cout << "  Expected: " << fp16_to_fp32(test.expected_value) << std::endl;
      std::cout << "  Got:      " << fp16_to_fp32(result.value) << std::endl;
      failed++;
      continue;
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_boundary_values() {
  std::cout << "\n=== Test: Boundary Values ===" << std::endl;

  // Test specific fp16 boundary values
  struct BoundaryTest {
    std::string name;
    float value;
  };

  std::vector<BoundaryTest> boundaries = {
    {"Positive zero", 0.0f},
    {"Negative zero", -0.0f},
    {"Smallest positive normal", 6.103515625e-5f},
    {"Largest subnormal", 6.097555e-5f},
    {"Smallest positive subnormal", 5.96046448e-8f},
    {"One", 1.0f},
    {"Largest normal", 65504.0f},
    {"Powers of 2", 2.0f},
    {"Powers of 2", 4.0f},
    {"Powers of 2", 8.0f},
    {"Powers of 2", 16.0f},
    {"Powers of 2", 32.0f},
    {"Powers of 2", 64.0f},
    {"Powers of 2", 128.0f},
    {"Powers of 2", 256.0f},
    {"Powers of 2", 512.0f},
    {"Powers of 2", 1024.0f},
    {"Negative powers of 2", 0.5f},
    {"Negative powers of 2", 0.25f},
    {"Negative powers of 2", 0.125f},
    {"Negative powers of 2", 0.0625f},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& boundary : boundaries) {
    // Convert to string
    char str[64];
    snprintf(str, sizeof(str), "%.15g", boundary.value);

    // Parse it
    parse_result result = parse_fp16(str);

    if (!result.success) {
      std::cout << "FAIL: " << boundary.name << " - Parse failed" << std::endl;
      std::cout << "  Input string: " << str << std::endl;
      failed++;
      continue;
    }

    // Convert expected value to fp16
    uint16_t expected_fp16 = fp32_to_fp16(boundary.value);
    float expected_fp32 = fp16_to_fp32(expected_fp16);

    // Convert back to fp32 and check
    float parsed_fp32 = fp16_to_fp32(result.value);

    // Check if the fp16 values match exactly
    if (result.value != expected_fp16) {
      std::cout << "FAIL: " << boundary.name << " - Value mismatch" << std::endl;
      std::cout << "  Original:  " << boundary.value << std::endl;
      std::cout << "  String:    " << str << std::endl;
      std::cout << "  Expected fp16: ";
      print_fp16_bits(expected_fp16);
      std::cout << " = " << expected_fp32 << std::endl;
      std::cout << "  Got fp16:      ";
      print_fp16_bits(result.value);
      std::cout << " = " << parsed_fp32 << std::endl;
      failed++;
      continue;
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

bool test_conversion_roundtrip() {
  std::cout << "\n=== Test: Conversion Roundtrip ===" << std::endl;

  int passed = 0;
  int failed = 0;
  int num_tests = 1000;

  // Test random values
  for (int i = 0; i < num_tests; i++) {
    // Generate random fp32 value
    float value = (static_cast<float>(rand()) / RAND_MAX) * 100.0f - 50.0f;

    // Convert to fp16
    uint16_t fp16_value = fp32_to_fp16(value);

    // Convert to string
    std::string str = fp16_to_string(fp16_value);

    // Parse back
    parse_result result = parse_fp16(str.c_str());

    if (!result.success) {
      std::cout << "FAIL: Roundtrip " << i << " - Parse failed" << std::endl;
      std::cout << "  Original fp32: " << value << std::endl;
      std::cout << "  String: " << str << std::endl;
      failed++;
      continue;
    }

    // Check if we get back the same fp16 value
    if (result.value != fp16_value) {
      float original = fp16_to_fp32(fp16_value);
      float parsed = fp16_to_fp32(result.value);

      // Allow for some rounding differences
      float rel_error = std::abs(original - parsed) / std::max(std::abs(original), 1e-7f);

      if (rel_error > 0.01f) { // 1% tolerance
        std::cout << "FAIL: Roundtrip " << i << " - Value changed" << std::endl;
        std::cout << "  Original fp32:  " << value << std::endl;
        std::cout << "  Original fp16:  " << original << " (";
        print_fp16_bits(fp16_value);
        std::cout << ")" << std::endl;
        std::cout << "  String:         " << str << std::endl;
        std::cout << "  Parsed fp16:    " << parsed << " (";
        print_fp16_bits(result.value);
        std::cout << ")" << std::endl;
        std::cout << "  Relative error: " << rel_error << std::endl;
        failed++;
        continue;
      }
    }

    passed++;
  }

  std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
  return failed == 0;
}

int main() {
  std::cout << "======================================" << std::endl;
  std::cout << "FP16 String Parser Comprehensive Tests" << std::endl;
  std::cout << "======================================" << std::endl;

  int total_passed = 0;
  int total_failed = 0;

  // Run all test suites
  bool result;

  result = test_basic_parsing();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_special_values();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_exponent_notation();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_overflow_underflow();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_invalid_input();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_whitespace_handling();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_boundary_values();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  result = test_conversion_roundtrip();
  total_passed += result ? 1 : 0;
  total_failed += result ? 0 : 1;

  // Summary
  std::cout << "\n======================================" << std::endl;
  std::cout << "Test Summary" << std::endl;
  std::cout << "======================================" << std::endl;
  std::cout << "Test suites passed: " << total_passed << "/" << (total_passed + total_failed) << std::endl;

  if (total_failed == 0) {
    std::cout << "\nAll tests PASSED!" << std::endl;
    return 0;
  } else {
    std::cout << "\nSome tests FAILED!" << std::endl;
    return 1;
  }
}
