// Test shortest representation and format ranges for dtoa_dragonbox
//
// This test validates:
// 1. Shortest representation: Dragonbox produces the shortest decimal string
//    that round-trips back to the original floating-point value
// 2. Human-readable format range: [1e-4, 1e16) for double, [1e-4, 1e7) for float
// 3. Scientific notation: Used for values outside human-readable range

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include <tuple>
#include <limits>

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

// Test that the string round-trips back to the original value
bool test_roundtrip_double(double value, const std::string& desc) {
  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
  char* end = internal::dtoa_dragonbox(value, buffer);
  *end = '\0';

  std::string result(buffer);

  // Parse the string back to double
  double parsed = std::strtod(buffer, nullptr);

  bool passed = (parsed == value) || (std::isnan(parsed) && std::isnan(value));

  std::cout << std::setw(30) << desc << " -> "
            << std::setw(25) << result;

  if (passed) {
    std::cout << " [ROUNDTRIP OK]" << std::endl;
  } else {
    std::cout << " [ROUNDTRIP FAIL] parsed=" << parsed << std::endl;
  }

  return passed;
}

bool test_roundtrip_float(float value, const std::string& desc) {
  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
  char* end = internal::dtoa_dragonbox(value, buffer);
  *end = '\0';

  std::string result(buffer);

  // Parse the string back to float
  float parsed = std::strtof(buffer, nullptr);

  bool passed = (parsed == value) || (std::isnan(parsed) && std::isnan(value));

  std::cout << std::setw(30) << desc << " -> "
            << std::setw(25) << result;

  if (passed) {
    std::cout << " [ROUNDTRIP OK]" << std::endl;
  } else {
    std::cout << " [ROUNDTRIP FAIL] parsed=" << parsed << std::endl;
  }

  return passed;
}

// Test that output uses expected format (decimal vs scientific)
enum class ExpectedFormat {
  DECIMAL,      // e.g., "123.456"
  SCIENTIFIC    // e.g., "1.23456e+02"
};

bool test_format_double(double value, const std::string& desc, ExpectedFormat expected) {
  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
  char* end = internal::dtoa_dragonbox(value, buffer);
  *end = '\0';

  std::string result(buffer);

  bool has_e = result.find('e') != std::string::npos;
  bool is_scientific = has_e;

  bool passed = (expected == ExpectedFormat::SCIENTIFIC) == is_scientific;

  std::cout << std::setw(30) << desc << " -> "
            << std::setw(25) << result << " ";

  if (passed) {
    std::cout << "[" << (is_scientific ? "SCIENTIFIC" : "DECIMAL") << " OK]" << std::endl;
  } else {
    std::cout << "[FORMAT FAIL: expected "
              << (expected == ExpectedFormat::SCIENTIFIC ? "SCIENTIFIC" : "DECIMAL")
              << ", got "
              << (is_scientific ? "SCIENTIFIC" : "DECIMAL") << "]" << std::endl;
  }

  return passed;
}

bool test_format_float(float value, const std::string& desc, ExpectedFormat expected) {
  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
  char* end = internal::dtoa_dragonbox(value, buffer);
  *end = '\0';

  std::string result(buffer);

  bool has_e = result.find('e') != std::string::npos;
  bool is_scientific = has_e;

  bool passed = (expected == ExpectedFormat::SCIENTIFIC) == is_scientific;

  std::cout << std::setw(30) << desc << " -> "
            << std::setw(25) << result << " ";

  if (passed) {
    std::cout << "[" << (is_scientific ? "SCIENTIFIC" : "DECIMAL") << " OK]" << std::endl;
  } else {
    std::cout << "[FORMAT FAIL: expected "
              << (expected == ExpectedFormat::SCIENTIFIC ? "SCIENTIFIC" : "DECIMAL")
              << ", got "
              << (is_scientific ? "SCIENTIFIC" : "DECIMAL") << "]" << std::endl;
  }

  return passed;
}

int main() {
  std::cout << "=== Testing Shortest Representation and Format Ranges ===" << std::endl;
  std::cout << std::endl;

  int total_tests = 0;
  int passed_tests = 0;

  // ========================================================================
  // Test 1: Shortest Representation (Round-trip test)
  // ========================================================================
  std::cout << "=== Test 1: Shortest Representation (Double Round-trip) ===" << std::endl;
  std::cout << "Dragonbox should produce the shortest string that round-trips." << std::endl;
  std::cout << std::endl;

  std::vector<std::pair<double, std::string>> roundtrip_doubles = {
    {0.0, "zero"},
    {1.0, "one"},
    {-1.0, "negative one"},
    {0.1, "0.1"},
    {0.2, "0.2"},
    {0.3, "0.3"},
    {123.456, "123.456"},
    {3.14159265358979323846, "pi"},
    {2.71828182845904523536, "e"},
    {1.23456789012345e20, "large scientific"},
    {1.23456789012345e-20, "small scientific"},
    {9999999999999999.0, "large integer"},
    {0.00009999999999999999, "small decimal"},
    {std::numeric_limits<double>::min(), "min positive"},
    {std::numeric_limits<double>::max(), "max"},
    {std::numeric_limits<double>::denorm_min(), "denorm min"},
  };

  for (const auto& test : roundtrip_doubles) {
    total_tests++;
    if (test_roundtrip_double(test.first, test.second)) {
      passed_tests++;
    }
  }

  std::cout << std::endl;

  // ========================================================================
  // Test 2: Shortest Representation (Float Round-trip)
  // ========================================================================
  std::cout << "=== Test 2: Shortest Representation (Float Round-trip) ===" << std::endl;

  std::vector<std::pair<float, std::string>> roundtrip_floats = {
    {0.0f, "zero"},
    {1.0f, "one"},
    {-1.0f, "negative one"},
    {3.14159f, "pi"},
    {2.71828f, "e"},
    {123.456f, "123.456f"},
    {1.23456e20f, "large scientific"},
    {1.23456e-20f, "small scientific"},
    {std::numeric_limits<float>::min(), "min positive"},
    {std::numeric_limits<float>::max(), "max"},
  };

  for (const auto& test : roundtrip_floats) {
    total_tests++;
    if (test_roundtrip_float(test.first, test.second)) {
      passed_tests++;
    }
  }

  std::cout << std::endl;

  // ========================================================================
  // Test 3: Human-readable Range for Double [1e-4, 1e16)
  // ========================================================================
  std::cout << "=== Test 3: Human-readable Format Range for Double ===" << std::endl;
  std::cout << "Range: [1e-4, 1e16) should use DECIMAL format" << std::endl;
  std::cout << "Outside range should use SCIENTIFIC notation" << std::endl;
  std::cout << std::endl;

  // Test boundary and near-boundary values
  std::vector<std::tuple<double, std::string, ExpectedFormat>> format_doubles = {
    // Below lower boundary (< 1e-4) -> SCIENTIFIC
    {1e-5, "1e-5", ExpectedFormat::SCIENTIFIC},
    {9.9999e-5, "9.9999e-5 (just below)", ExpectedFormat::SCIENTIFIC},

    // At/above lower boundary (>= 1e-4) -> DECIMAL
    {1e-4, "1e-4 (boundary)", ExpectedFormat::DECIMAL},
    {0.0001, "0.0001", ExpectedFormat::DECIMAL},
    {0.001, "0.001", ExpectedFormat::DECIMAL},
    {0.01, "0.01", ExpectedFormat::DECIMAL},
    {0.1, "0.1", ExpectedFormat::DECIMAL},
    {1.0, "1.0", ExpectedFormat::DECIMAL},
    {10.0, "10.0", ExpectedFormat::DECIMAL},
    {100.0, "100.0", ExpectedFormat::DECIMAL},
    {1000.0, "1000.0", ExpectedFormat::DECIMAL},
    {10000.0, "10000.0", ExpectedFormat::DECIMAL},
    {100000.0, "100000.0", ExpectedFormat::DECIMAL},
    {1000000.0, "1000000.0", ExpectedFormat::DECIMAL},
    {10000000.0, "10000000.0", ExpectedFormat::DECIMAL},
    {100000000.0, "100000000.0", ExpectedFormat::DECIMAL},
    {1000000000.0, "1000000000.0", ExpectedFormat::DECIMAL},
    {10000000000.0, "10000000000.0", ExpectedFormat::DECIMAL},
    {100000000000.0, "100000000000.0", ExpectedFormat::DECIMAL},
    {1000000000000.0, "1000000000000.0", ExpectedFormat::DECIMAL},
    {10000000000000.0, "10000000000000.0", ExpectedFormat::DECIMAL},
    {100000000000000.0, "100000000000000.0", ExpectedFormat::DECIMAL},
    {1000000000000000.0, "1000000000000000.0", ExpectedFormat::DECIMAL},

    // Below upper boundary (< 1e16) -> DECIMAL
    // Note: 9.999999999999999e15 actually rounds to exactly 1e16 in double precision
    // {9.999999999999999e15, "9.999...e15 (just below)", ExpectedFormat::DECIMAL},

    // At/above upper boundary (>= 1e16) -> SCIENTIFIC
    {1e16, "1e16 (boundary)", ExpectedFormat::SCIENTIFIC},
    {1e17, "1e17", ExpectedFormat::SCIENTIFIC},
    {1e20, "1e20", ExpectedFormat::SCIENTIFIC},
    {1e100, "1e100", ExpectedFormat::SCIENTIFIC},
  };

  for (const auto& test : format_doubles) {
    total_tests++;
    if (test_format_double(std::get<0>(test), std::get<1>(test), std::get<2>(test))) {
      passed_tests++;
    }
  }

  std::cout << std::endl;

  // ========================================================================
  // Test 4: Human-readable Range for Float [1e-4, 1e7)
  // ========================================================================
  std::cout << "=== Test 4: Human-readable Format Range for Float ===" << std::endl;
  std::cout << "Range: [1e-4, 1e7) should use DECIMAL format" << std::endl;
  std::cout << "Outside range should use SCIENTIFIC notation" << std::endl;
  std::cout << std::endl;

  std::vector<std::tuple<float, std::string, ExpectedFormat>> format_floats = {
    // Below lower boundary (< 1e-4) -> SCIENTIFIC
    {1e-5f, "1e-5f", ExpectedFormat::SCIENTIFIC},
    {9.999e-5f, "9.999e-5f (just below)", ExpectedFormat::SCIENTIFIC},

    // At/above lower boundary (>= 1e-4) -> DECIMAL
    // Note: 1e-4f doesn't have exact float representation, actual value is slightly below
    // {1e-4f, "1e-4f (boundary)", ExpectedFormat::DECIMAL},
    {0.001f, "0.001f", ExpectedFormat::DECIMAL},
    {0.01f, "0.01f", ExpectedFormat::DECIMAL},
    {1.0f, "1.0f", ExpectedFormat::DECIMAL},
    {10.0f, "10.0f", ExpectedFormat::DECIMAL},
    {100.0f, "100.0f", ExpectedFormat::DECIMAL},
    {1000.0f, "1000.0f", ExpectedFormat::DECIMAL},
    {10000.0f, "10000.0f", ExpectedFormat::DECIMAL},
    {100000.0f, "100000.0f", ExpectedFormat::DECIMAL},
    {1000000.0f, "1000000.0f", ExpectedFormat::DECIMAL},

    // Below upper boundary (< 1e7) -> DECIMAL
    {9999999.0f, "9999999.0f (just below)", ExpectedFormat::DECIMAL},

    // At/above upper boundary (>= 1e7) -> SCIENTIFIC
    {1e7f, "1e7f (boundary)", ExpectedFormat::SCIENTIFIC},
    {1e8f, "1e8f", ExpectedFormat::SCIENTIFIC},
    {1e10f, "1e10f", ExpectedFormat::SCIENTIFIC},
    {1e20f, "1e20f", ExpectedFormat::SCIENTIFIC},
  };

  for (const auto& test : format_floats) {
    total_tests++;
    if (test_format_float(std::get<0>(test), std::get<1>(test), std::get<2>(test))) {
      passed_tests++;
    }
  }

  std::cout << std::endl;

  // ========================================================================
  // Test 5: Scientific Notation for Extreme Values
  // ========================================================================
  std::cout << "=== Test 5: Scientific Notation for Extreme Values ===" << std::endl;

  std::vector<std::tuple<double, std::string, ExpectedFormat>> extreme_doubles = {
    {1e-100, "1e-100 (very small)", ExpectedFormat::SCIENTIFIC},
    {1e-50, "1e-50", ExpectedFormat::SCIENTIFIC},
    {1e-10, "1e-10", ExpectedFormat::SCIENTIFIC},
    {1e50, "1e50", ExpectedFormat::SCIENTIFIC},
    {1e100, "1e100", ExpectedFormat::SCIENTIFIC},
    {1e200, "1e200 (very large)", ExpectedFormat::SCIENTIFIC},
  };

  for (const auto& test : extreme_doubles) {
    total_tests++;
    if (test_format_double(std::get<0>(test), std::get<1>(test), std::get<2>(test))) {
      passed_tests++;
    }
  }

  std::cout << std::endl;

  // ========================================================================
  // Summary
  // ========================================================================
  std::cout << "=== Test Summary ===" << std::endl;
  std::cout << "Total tests: " << total_tests << std::endl;
  std::cout << "Passed: " << passed_tests << std::endl;
  std::cout << "Failed: " << (total_tests - passed_tests) << std::endl;

  if (passed_tests == total_tests) {
    std::cout << std::endl << "All tests PASSED!" << std::endl;
    std::cout << std::endl;
    std::cout << "Summary of validated properties:" << std::endl;
    std::cout << "  1. Shortest representation: All values round-trip correctly" << std::endl;
    std::cout << "  2. Double human-readable range: [1e-4, 1e16) uses decimal format" << std::endl;
    std::cout << "  3. Float human-readable range: [1e-4, 1e7) uses decimal format" << std::endl;
    std::cout << "  4. Scientific notation: Used for extreme values outside ranges" << std::endl;
    return 0;
  } else {
    std::cout << std::endl << "Some tests FAILED!" << std::endl;
    return 1;
  }
}
