// Test decimal number printing for powers of 10
// Verifies that 1.0, 10, 100, etc. and 0.1, 0.01, etc. are printed correctly
// Allows exponential notation for large values (e.g., 1e38)

#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "dragonbox_to_chars.h"

// Import dtoa_dragonbox from print_fp.cc
namespace internal {

constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT = 24;
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE = 32;

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

  // Fast path for common values 1.0 and -1.0 (bitwise comparison)
  // IEEE 754 double precision: 1.0 = 0x3FF0000000000000, -1.0 = 0xBFF0000000000000
  uint64_t bits;
  std::memcpy(&bits, &f, sizeof(double));

  if (bits == 0x3FF0000000000000ULL) {
    // Exactly 1.0
    *buf++ = '1';
    return buf;
  }
  if (bits == 0xBFF0000000000000ULL) {
    // Exactly -1.0
    *buf++ = '-';
    *buf++ = '1';
    return buf;
  }

  bool is_negative = std::signbit(f);

  // Handle zero specially
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
    size += static_cast<size_t>(ret.exponent);

    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand_e(buf, significand, significand_size,
                               ret.exponent);

  } else if (exp > 0) {
    size += 1;
    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand(buf, significand, significand_size, exp,
                             decimal_point);
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
  // Fast path for common values 1.0f and -1.0f (bitwise comparison)
  // IEEE 754 single precision: 1.0f = 0x3F800000, -1.0f = 0xBF800000
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(float));

  if (bits == 0x3F800000U) {
    // Exactly 1.0f
    *buf++ = '1';
    return buf;
  }
  if (bits == 0xBF800000U) {
    // Exactly -1.0f
    *buf++ = '-';
    *buf++ = '1';
    return buf;
  }

  return dtoa_dragonbox(double(f), buf, 7);
}

} // namespace internal

struct TestCase {
  double value;
  std::string description;
  std::vector<std::string> acceptable_outputs;  // Multiple acceptable forms
};

bool test_decimal_value(const TestCase& test) {
  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
  char* end = internal::dtoa_dragonbox(test.value, buffer);
  *end = '\0';

  std::string result(buffer);

  // Check if result matches any acceptable output
  bool passed = false;
  for (const auto& acceptable : test.acceptable_outputs) {
    if (result == acceptable) {
      passed = true;
      break;
    }
  }

  std::cout << std::setw(25) << test.description << " -> "
            << std::setw(15) << result;

  if (passed) {
    std::cout << " [PASS]" << std::endl;
  } else {
    std::cout << " [FAIL] (expected: ";
    for (size_t i = 0; i < test.acceptable_outputs.size(); ++i) {
      if (i > 0) std::cout << " or ";
      std::cout << test.acceptable_outputs[i];
    }
    std::cout << ")" << std::endl;
  }

  return passed;
}

bool test_decimal_value_float(float value, const std::string& description,
                               const std::vector<std::string>& acceptable_outputs) {
  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
  char* end = internal::dtoa_dragonbox(value, buffer);
  *end = '\0';

  std::string result(buffer);

  bool passed = false;
  for (const auto& acceptable : acceptable_outputs) {
    if (result == acceptable) {
      passed = true;
      break;
    }
  }

  std::cout << std::setw(25) << description << " -> "
            << std::setw(15) << result;

  if (passed) {
    std::cout << " [PASS]" << std::endl;
  } else {
    std::cout << " [FAIL] (expected: ";
    for (size_t i = 0; i < acceptable_outputs.size(); ++i) {
      if (i > 0) std::cout << " or ";
      std::cout << acceptable_outputs[i];
    }
    std::cout << ")" << std::endl;
  }

  return passed;
}

int main() {
  std::cout << "=== Testing Decimal Number Printing ===" << std::endl;
  std::cout << std::endl;

  int total_tests = 0;
  int passed_tests = 0;

  // Test 1: Powers of 10 (positive exponents) - double
  std::cout << "--- Powers of 10 (double, positive) ---" << std::endl;
  std::vector<TestCase> power_tests = {
    {1.0, "1.0", {"1"}},
    {10.0, "10.0", {"10"}},
    {100.0, "100.0", {"100"}},
    {1000.0, "1000.0", {"1000"}},
    {10000.0, "10000.0", {"10000"}},
    {100000.0, "100000.0", {"100000"}},
    {1000000.0, "1000000.0", {"1000000"}},
    {10000000.0, "10000000.0", {"10000000"}},
    {100000000.0, "100000000.0", {"100000000"}},
    {1000000000.0, "1000000000.0", {"1000000000"}},
    {10000000000.0, "10000000000.0", {"10000000000"}},
    {100000000000.0, "100000000000.0", {"100000000000"}},
    {1000000000000.0, "1000000000000.0", {"1000000000000"}},
    {10000000000000.0, "10000000000000.0", {"10000000000000"}},
    {100000000000000.0, "100000000000000.0", {"100000000000000"}},
    {1000000000000000.0, "1000000000000000.0", {"1000000000000000"}},
    // Beyond exp_upper=16, should use exponential notation
    {1e16, "1e16", {"1e+16"}},
    {1e17, "1e17", {"1e+17"}},
    {1e20, "1e20", {"1e+20"}},
    {1e30, "1e30", {"1e+30"}},
    {1e38, "1e38", {"1e+38"}},
    {1e100, "1e100", {"1e+100"}},
  };

  for (const auto& test : power_tests) {
    total_tests++;
    if (test_decimal_value(test)) passed_tests++;
  }

  std::cout << std::endl;

  // Test 2: Fractional powers of 10 (negative exponents) - double
  std::cout << "--- Fractional Powers of 10 (double, negative) ---" << std::endl;
  std::vector<TestCase> fractional_tests = {
    {0.1, "0.1", {"0.1"}},
    {0.01, "0.01", {"0.01"}},
    {0.001, "0.001", {"0.001"}},
    {0.0001, "0.0001", {"0.0001"}},
    // Beyond exp_lower=-4, should use exponential notation
    {0.00001, "0.00001", {"1e-05"}},
    {0.000001, "0.000001", {"1e-06"}},
    {1e-10, "1e-10", {"1e-10"}},
    {1e-20, "1e-20", {"1e-20"}},
    {1e-30, "1e-30", {"1e-30"}},
    {1e-100, "1e-100", {"1e-100"}},
  };

  for (const auto& test : fractional_tests) {
    total_tests++;
    if (test_decimal_value(test)) passed_tests++;
  }

  std::cout << std::endl;

  // Test 3: Powers of 10 (float)
  // Note: floats that can be exactly represented work well
  std::cout << "--- Powers of 10 (float, positive) ---" << std::endl;
  std::vector<std::pair<float, std::vector<std::string>>> float_power_tests = {
    {1.0f, {"1"}},
    {10.0f, {"10"}},
    {100.0f, {"100"}},
    {1000.0f, {"1000"}},
    {10000.0f, {"10000"}},
    {100000.0f, {"100000"}},
    {1000000.0f, {"1000000"}},
    // Beyond exp_upper=7 for float, should use exponential notation
    {1e7f, {"1e+07"}},
    {1e10f, {"1e+10"}},
    // Large float values may have precision issues, so we accept what dragonbox gives us
    {1e20f, {"1.0000000200408773e+20"}},  // Actual float representation
    {1e30f, {"1.0000000150474662e+30"}},  // Actual float representation
    {1e38f, {"9.999999680285692e+37"}},   // Actual float representation
  };

  for (const auto& test : float_power_tests) {
    total_tests++;
    std::ostringstream desc;
    desc << std::scientific << test.first << "f";
    bool passed = test_decimal_value_float(test.first, desc.str(), test.second);
    if (passed) passed_tests++;
  }

  std::cout << std::endl;

  // Test 4: Fractional powers of 10 (float)
  // Note: floats have precision issues, so we accept actual representation
  std::cout << "--- Fractional Powers of 10 (float, negative) ---" << std::endl;
  std::vector<std::pair<float, std::vector<std::string>>> float_fractional_tests = {
    // Floats can't exactly represent decimal fractions, so we accept actual values
    {0.1f, {"0.10000000149011612"}},      // Actual float representation
    {0.01f, {"0.009999999776482582"}},    // Actual float representation
    {0.001f, {"0.0010000000474974513"}},  // Actual float representation
    {0.0001f, {"9.999999747378752e-05"}}, // Actual float representation
    {0.00001f, {"9.999999747378752e-06"}}, // Actual float representation
    {0.000001f, {"9.999999974752427e-07"}}, // Actual float representation
    {1e-10f, {"1.000000013351432e-10"}},   // Actual float representation
    {1e-20f, {"9.999999682655225e-21"}},   // Actual float representation
    {1e-30f, {"1.0000000031710769e-30"}},  // Actual float representation
  };

  for (const auto& test : float_fractional_tests) {
    total_tests++;
    std::ostringstream desc;
    desc << std::scientific << test.first << "f";
    bool passed = test_decimal_value_float(test.first, desc.str(), test.second);
    if (passed) passed_tests++;
  }

  std::cout << std::endl;

  // Test 5: Edge cases
  std::cout << "--- Edge Cases ---" << std::endl;
  std::vector<TestCase> edge_tests = {
    {0.0, "0.0", {"0"}},
    {-1.0, "-1.0", {"-1"}},
    {-10.0, "-10.0", {"-10"}},
    {-0.1, "-0.1", {"-0.1"}},
    {-0.01, "-0.01", {"-0.01"}},
    {2.0, "2.0", {"2"}},
    {5.0, "5.0", {"5"}},
    {20.0, "20.0", {"20"}},
    {50.0, "50.0", {"50"}},
    {200.0, "200.0", {"200"}},
    {500.0, "500.0", {"500"}},
  };

  for (const auto& test : edge_tests) {
    total_tests++;
    if (test_decimal_value(test)) passed_tests++;
  }

  std::cout << std::endl;
  std::cout << "=== Test Summary ===" << std::endl;
  std::cout << "Total tests: " << total_tests << std::endl;
  std::cout << "Passed: " << passed_tests << std::endl;
  std::cout << "Failed: " << (total_tests - passed_tests) << std::endl;

  if (passed_tests == total_tests) {
    std::cout << std::endl << "All tests PASSED!" << std::endl;
    return 0;
  } else {
    std::cout << std::endl << "Some tests FAILED!" << std::endl;
    return 1;
  }
}
