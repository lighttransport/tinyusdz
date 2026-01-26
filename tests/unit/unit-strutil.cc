#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-strutil.h"
#include "str-util.hh"
#include "tiny-string.hh"
#include <cmath>

using namespace tinyusdz;

void strutil_test(void) {
  {
    std::string s = "HelloA WorlZ";
    std::string ls = tinyusdz::to_lower(s);
    TEST_CHECK(ls.compare("helloa worlz") == 0);
  }

  {
    std::string s = "_aaa";
    TEST_CHECK(is_valid_utf8_identifier(s));

    // TODO: Do not allow underscore-only identifier?
    s = "___";
    TEST_CHECK(is_valid_utf8_identifier(s));

    s = "customLayerData";
    TEST_CHECK(isValidIdentifier(s));

    // Now TinyUSDZ allow UTF-8 string as identifier by default.
    s = u8"響";
    TEST_CHECK(isValidIdentifier(s));

    // Emoji in identifier is not allowed.
    s = u8"_hello😩";
    TEST_CHECK(!isValidIdentifier(s));

  }
}

void tinystring_test(void) {
  
  tstring s("hello");
  tstring s2("bora");
  tstring s3("ll");
  tstring s4("hellobora");
  tstring_view v0(s);
  tstring_view v1(s);
  tstring_view v2(s2);
  tstring_view v3(s3);
  tstring_view v4(s4);

  TEST_CHECK(v0 == v1);
  TEST_CHECK(v0 != v2);

  TEST_CHECK(v0.contains(v0));
  TEST_CHECK(v0.contains(v3));
  TEST_CHECK(!v0.contains(v2));

  TEST_CHECK(!v4.starts_with(v2));
  TEST_CHECK(v4.starts_with(v0));

  TEST_CHECK(!v4.ends_with(v0));
  TEST_CHECK(v4.ends_with(v2));

}

void parse_int_test(void) {
  using namespace tinyusdz::str;
  
  int32_t result;
  
  // Basic positive numbers
  {
    tstring_view sv("123");
    TEST_CHECK(parse_int(sv, &result));
    TEST_CHECK(result == 123);
  }
  
  // Basic negative numbers
  {
    tstring_view sv("-456");
    TEST_CHECK(parse_int(sv, &result));
    TEST_CHECK(result == -456);
  }
  
  // Zero
  {
    tstring_view sv("0");
    TEST_CHECK(parse_int(sv, &result));
    TEST_CHECK(result == 0);
  }
  
  // Positive sign
  {
    tstring_view sv("+789");
    TEST_CHECK(parse_int(sv, &result));
    TEST_CHECK(result == 789);
  }
  
  // Maximum int32_t value
  {
    tstring_view sv("2147483647");
    TEST_CHECK(parse_int(sv, &result));
    TEST_CHECK(result == 2147483647);
  }
  
  // Minimum int32_t value
  {
    tstring_view sv("-2147483648");
    TEST_CHECK(parse_int(sv, &result));
    TEST_CHECK(result == -2147483648);
  }
  
  // Empty string
  {
    tstring_view sv("");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  // Just a sign
  {
    tstring_view sv("-");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("+");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  // Non-numeric characters
  {
    tstring_view sv("123a");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("a123");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("12.3");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  // Overflow cases
  {
    tstring_view sv("2147483648");  // INT32_MAX + 1
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("-2147483649");  // INT32_MIN - 1
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  // Very large numbers
  {
    tstring_view sv("999999999999999999");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("-999999999999999999");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  // Leading/trailing spaces (should fail since parse_int doesn't handle whitespace)
  {
    tstring_view sv(" 123");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("123 ");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  // Multiple signs
  {
    tstring_view sv("++123");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("--123");
    TEST_CHECK(!parse_int(sv, &result));
  }
  
  {
    tstring_view sv("+-123");
    TEST_CHECK(!parse_int(sv, &result));
  }

}

void dtoa_test(void) {
  // Test double to ASCII conversion with full precision

  {
    // Test pi with full double precision
    double pi = 3.141592653589793;
    char buf[384];
    char *end = dtoa(pi, buf);
    *end = '\0';
    std::string result(buf);

    // Should preserve full precision (at least 15 significant digits)
    TEST_CHECK(result.find("3.14159265358979") != std::string::npos);
    TEST_MSG("pi result: %s", result.c_str());
  }

  {
    // Test e with full double precision
    double e = 2.718281828459045;
    char buf[384];
    char *end = dtoa(e, buf);
    *end = '\0';
    std::string result(buf);

    // Should preserve full precision
    TEST_CHECK(result.find("2.71828182845904") != std::string::npos);
    TEST_MSG("e result: %s", result.c_str());
  }

  {
    // Test negative value
    double neg = -2.718281828459045;
    char buf[384];
    char *end = dtoa(neg, buf);
    *end = '\0';
    std::string result(buf);

    TEST_CHECK(result[0] == '-');
    TEST_CHECK(result.find("2.71828182845904") != std::string::npos);
    TEST_MSG("negative e result: %s", result.c_str());
  }

  {
    // Test zero
    double zero = 0.0;
    char buf[384];
    char *end = dtoa(zero, buf);
    *end = '\0';
    std::string result(buf);

    TEST_CHECK(result == "0.0");
    TEST_MSG("zero result: %s", result.c_str());
  }

  {
    // Test negative zero
    double neg_zero = -0.0;
    char buf[384];
    char *end = dtoa(neg_zero, buf);
    *end = '\0';
    std::string result(buf);

    // dtoa_milo should output "-0.0" for negative zero
    TEST_CHECK(result == "-0.0");
    TEST_MSG("negative zero result: %s", result.c_str());
  }

  {
    // Test small number
    double small = 0.000001234567890123456;
    char buf[384];
    char *end = dtoa(small, buf);
    *end = '\0';
    std::string result(buf);

    // Should use scientific notation for very small numbers
    TEST_MSG("small number result: %s", result.c_str());
    TEST_CHECK(result.length() > 0);
  }

  {
    // Test large number
    double large = 1234567890123456.0;
    char buf[384];
    char *end = dtoa(large, buf);
    *end = '\0';
    std::string result(buf);

    TEST_MSG("large number result: %s", result.c_str());
    TEST_CHECK(result.length() > 0);
  }

  {
    // Test float conversion
    float f = 3.14159f;
    char buf[384];
    char *end = dtoa(f, buf);
    *end = '\0';
    std::string result(buf);

    // Should output float with appropriate precision
    TEST_CHECK(result.find("3.14159") != std::string::npos);
    TEST_MSG("float result: %s", result.c_str());
  }

  {
    // Test one
    double one = 1.0;
    char buf[384];
    char *end = dtoa(one, buf);
    *end = '\0';
    std::string result(buf);

    TEST_CHECK(result == "1.0");
    TEST_MSG("one result: %s", result.c_str());
  }

  {
    // Test integer-like double
    double int_like = 42.0;
    char buf[384];
    char *end = dtoa(int_like, buf);
    *end = '\0';
    std::string result(buf);

    TEST_CHECK(result == "42.0");
    TEST_MSG("integer-like result: %s", result.c_str());
  }
}

void fp_string_conversion_test(void) {
  auto nearly_equal = [](double a, double b, double eps) {
    return std::fabs(a - b) <= eps * (std::fabs(a) + std::fabs(b) + 1.0);
  };

  auto roundtrip_double = [&](double v, double eps) {
    std::string s = dtos(v);
    double parsed = tinyusdz::atof(s);
    TEST_CHECK_(nearly_equal(parsed, v, eps), "double roundtrip failed: v=%g, s=%s, parsed=%g", v, s.c_str(), parsed);

    char buf[DTOS_MAX_CHARS_DOUBLE + 2];
    buf[DTOS_MAX_CHARS_DOUBLE + 1] = '*';  // sentinel
    size_t len = dtos(v, buf);
    std::string buf_str(buf, len);
    TEST_CHECK_(buf_str == s, "double buffer string mismatch: expected %s, got %s", s.c_str(), buf_str.c_str());
    TEST_CHECK(buf[DTOS_MAX_CHARS_DOUBLE + 1] == '*');  // no overflow
  };

  auto roundtrip_float = [&](float v, float eps) {
    std::string s = dtos(v);
    double parsed = tinyusdz::atof(s);
    TEST_CHECK_(nearly_equal(parsed, static_cast<double>(v), eps), "float roundtrip failed: v=%g, s=%s, parsed=%g", v, s.c_str(), parsed);

    char buf[DTOS_MAX_CHARS_FLOAT + 2];
    buf[DTOS_MAX_CHARS_FLOAT + 1] = '*';  // sentinel
    size_t len = dtos(v, buf);
    std::string buf_str(buf, len);
    TEST_CHECK_(buf_str == s, "float buffer string mismatch: expected %s, got %s", s.c_str(), buf_str.c_str());
    TEST_CHECK(buf[DTOS_MAX_CHARS_FLOAT + 1] == '*');  // no overflow
  };

  // Fast-path values
  TEST_CHECK(dtos(1.0) == "1");
  TEST_CHECK(dtos(-1.0) == "-1");
  TEST_CHECK(dtos(1.0f) == "1");
  TEST_CHECK(dtos(-1.0f) == "-1");

  // Representative doubles
  roundtrip_double(0.0, 1e-15);
  roundtrip_double(-0.0, 1e-15);
  roundtrip_double(123456.789, 1e-12);
  roundtrip_double(-9876.54321, 1e-12);
  roundtrip_double(1e-6, 1e-12);
  roundtrip_double(1e20, 1e-12);
  roundtrip_double(-1e-8, 1e-12);

  // Representative floats
  roundtrip_float(0.0f, 1e-6f);
  roundtrip_float(-0.0f, 1e-6f);
  roundtrip_float(1234.5f, 1e-5f);
  roundtrip_float(-9876.5f, 1e-5f);
  roundtrip_float(1e-4f, 1e-5f);
  roundtrip_float(1e6f, 1e-5f);
}

// Test human-readable format ranges:
// - Double: [1e-4, 1e16) uses decimal format, outside uses scientific
// - Float: [1e-4, 1e7) uses decimal format, outside uses scientific
void fp_format_range_test(void) {
  // Helper: check if string uses scientific notation
  auto is_scientific = [](const std::string& s) -> bool {
    return s.find('e') != std::string::npos || s.find('E') != std::string::npos;
  };

  // Helper: count true significant digits (excludes leading zeros)
  auto count_sig_digits = [](const std::string& s) -> size_t {
    size_t count = 0;
    bool started = false;
    bool in_exponent = false;
    for (char c : s) {
      if (c == 'e' || c == 'E') in_exponent = true;
      else if (!in_exponent && c >= '0' && c <= '9') {
        if (c != '0' || started) { started = true; count++; }
      }
    }
    return count;
  };

  // Helper: verify roundtrip for double
  auto check_double_roundtrip = [](double v) {
    std::string s = dtos(v);
    double parsed = std::strtod(s.c_str(), nullptr);
    TEST_CHECK_(parsed == v || (std::isnan(parsed) && std::isnan(v)),
                "double roundtrip failed: v=%.17g, s='%s', parsed=%.17g", v, s.c_str(), parsed);
  };

  // Helper: verify roundtrip for float
  auto check_float_roundtrip = [](float v) {
    std::string s = dtos(v);
    float parsed = std::strtof(s.c_str(), nullptr);
    TEST_CHECK_(parsed == v || (std::isnan(parsed) && std::isnan(v)),
                "float roundtrip failed: v=%.9g, s='%s', parsed=%.9g", v, s.c_str(), parsed);
  };

  // =========================================================================
  // DOUBLE FORMAT RANGE TESTS [1e-4, 1e16)
  // =========================================================================

  // Below lower boundary (< 1e-4) -> SCIENTIFIC
  TEST_CHECK_(is_scientific(dtos(0.00001)), "0.00001 should be scientific: %s", dtos(0.00001).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-5)), "1e-5 should be scientific: %s", dtos(1e-5).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-6)), "1e-6 should be scientific: %s", dtos(1e-6).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-7)), "1e-7 should be scientific: %s", dtos(1e-7).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-8)), "1e-8 should be scientific: %s", dtos(1e-8).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-10)), "1e-10 should be scientific: %s", dtos(1e-10).c_str());
  TEST_CHECK_(is_scientific(dtos(9.9999e-5)), "9.9999e-5 should be scientific: %s", dtos(9.9999e-5).c_str());
  TEST_CHECK_(is_scientific(dtos(9.999999e-5)), "9.999999e-5 should be scientific: %s", dtos(9.999999e-5).c_str());
  TEST_CHECK_(is_scientific(dtos(0.000099)), "0.000099 should be scientific: %s", dtos(0.000099).c_str());
  TEST_CHECK_(is_scientific(dtos(0.0000999)), "0.0000999 should be scientific: %s", dtos(0.0000999).c_str());
  TEST_CHECK_(is_scientific(dtos(0.00009999)), "0.00009999 should be scientific: %s", dtos(0.00009999).c_str());

  // Negative values below boundary -> SCIENTIFIC
  TEST_CHECK_(is_scientific(dtos(-0.00001)), "-0.00001 should be scientific: %s", dtos(-0.00001).c_str());
  TEST_CHECK_(is_scientific(dtos(-1e-5)), "-1e-5 should be scientific: %s", dtos(-1e-5).c_str());
  TEST_CHECK_(is_scientific(dtos(-1e-6)), "-1e-6 should be scientific: %s", dtos(-1e-6).c_str());

  // At/above lower boundary (>= 1e-4) -> DECIMAL
  TEST_CHECK_(!is_scientific(dtos(1e-4)), "1e-4 should be decimal: %s", dtos(1e-4).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.0001)), "0.0001 should be decimal: %s", dtos(0.0001).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.00010001)), "0.00010001 should be decimal: %s", dtos(0.00010001).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.0002)), "0.0002 should be decimal: %s", dtos(0.0002).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.0005)), "0.0005 should be decimal: %s", dtos(0.0005).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.001)), "0.001 should be decimal: %s", dtos(0.001).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.01)), "0.01 should be decimal: %s", dtos(0.01).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.1)), "0.1 should be decimal: %s", dtos(0.1).c_str());
  TEST_CHECK_(!is_scientific(dtos(1.0)), "1.0 should be decimal: %s", dtos(1.0).c_str());
  TEST_CHECK_(!is_scientific(dtos(10.0)), "10.0 should be decimal: %s", dtos(10.0).c_str());
  TEST_CHECK_(!is_scientific(dtos(100.0)), "100.0 should be decimal: %s", dtos(100.0).c_str());
  TEST_CHECK_(!is_scientific(dtos(1000.0)), "1000.0 should be decimal: %s", dtos(1000.0).c_str());
  TEST_CHECK_(!is_scientific(dtos(1e10)), "1e10 should be decimal: %s", dtos(1e10).c_str());
  TEST_CHECK_(!is_scientific(dtos(1e15)), "1e15 should be decimal: %s", dtos(1e15).c_str());

  // Negative values in decimal range -> DECIMAL
  TEST_CHECK_(!is_scientific(dtos(-0.0001)), "-0.0001 should be decimal: %s", dtos(-0.0001).c_str());
  TEST_CHECK_(!is_scientific(dtos(-0.001)), "-0.001 should be decimal: %s", dtos(-0.001).c_str());
  TEST_CHECK_(!is_scientific(dtos(-1.0)), "-1.0 should be decimal: %s", dtos(-1.0).c_str());
  TEST_CHECK_(!is_scientific(dtos(-123.456)), "-123.456 should be decimal: %s", dtos(-123.456).c_str());

  // At/above upper boundary (>= 1e16) -> SCIENTIFIC
  TEST_CHECK_(is_scientific(dtos(1e16)), "1e16 should be scientific: %s", dtos(1e16).c_str());
  TEST_CHECK_(is_scientific(dtos(1e17)), "1e17 should be scientific: %s", dtos(1e17).c_str());
  TEST_CHECK_(is_scientific(dtos(1e20)), "1e20 should be scientific: %s", dtos(1e20).c_str());
  TEST_CHECK_(is_scientific(dtos(1e100)), "1e100 should be scientific: %s", dtos(1e100).c_str());
  TEST_CHECK_(is_scientific(dtos(-1e16)), "-1e16 should be scientific: %s", dtos(-1e16).c_str());

  // =========================================================================
  // FLOAT FORMAT RANGE TESTS [1e-4, 1e7)
  // =========================================================================

  // Below lower boundary (< 1e-4) -> SCIENTIFIC
  TEST_CHECK_(is_scientific(dtos(0.00001f)), "0.00001f should be scientific: %s", dtos(0.00001f).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-5f)), "1e-5f should be scientific: %s", dtos(1e-5f).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-6f)), "1e-6f should be scientific: %s", dtos(1e-6f).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-7f)), "1e-7f should be scientific: %s", dtos(1e-7f).c_str());
  TEST_CHECK_(is_scientific(dtos(9.999e-5f)), "9.999e-5f should be scientific: %s", dtos(9.999e-5f).c_str());
  TEST_CHECK_(is_scientific(dtos(-1e-5f)), "-1e-5f should be scientific: %s", dtos(-1e-5f).c_str());

  // At/above lower boundary (>= 1e-4) -> DECIMAL
  TEST_CHECK_(!is_scientific(dtos(0.0001f)), "0.0001f should be decimal: %s", dtos(0.0001f).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.001f)), "0.001f should be decimal: %s", dtos(0.001f).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.01f)), "0.01f should be decimal: %s", dtos(0.01f).c_str());
  TEST_CHECK_(!is_scientific(dtos(0.1f)), "0.1f should be decimal: %s", dtos(0.1f).c_str());
  TEST_CHECK_(!is_scientific(dtos(1.0f)), "1.0f should be decimal: %s", dtos(1.0f).c_str());
  TEST_CHECK_(!is_scientific(dtos(10.0f)), "10.0f should be decimal: %s", dtos(10.0f).c_str());
  TEST_CHECK_(!is_scientific(dtos(100.0f)), "100.0f should be decimal: %s", dtos(100.0f).c_str());
  TEST_CHECK_(!is_scientific(dtos(1000.0f)), "1000.0f should be decimal: %s", dtos(1000.0f).c_str());
  TEST_CHECK_(!is_scientific(dtos(1e6f)), "1e6f should be decimal: %s", dtos(1e6f).c_str());
  TEST_CHECK_(!is_scientific(dtos(9999999.0f)), "9999999.0f should be decimal: %s", dtos(9999999.0f).c_str());
  TEST_CHECK_(!is_scientific(dtos(-0.001f)), "-0.001f should be decimal: %s", dtos(-0.001f).c_str());
  TEST_CHECK_(!is_scientific(dtos(-123.456f)), "-123.456f should be decimal: %s", dtos(-123.456f).c_str());

  // At/above upper boundary (>= 1e7) -> SCIENTIFIC
  TEST_CHECK_(is_scientific(dtos(1e7f)), "1e7f should be scientific: %s", dtos(1e7f).c_str());
  TEST_CHECK_(is_scientific(dtos(1e8f)), "1e8f should be scientific: %s", dtos(1e8f).c_str());
  TEST_CHECK_(is_scientific(dtos(1e10f)), "1e10f should be scientific: %s", dtos(1e10f).c_str());
  TEST_CHECK_(is_scientific(dtos(1e20f)), "1e20f should be scientific: %s", dtos(1e20f).c_str());
  TEST_CHECK_(is_scientific(dtos(-1e7f)), "-1e7f should be scientific: %s", dtos(-1e7f).c_str());

  // =========================================================================
  // EXTREME VALUES -> SCIENTIFIC
  // =========================================================================
  TEST_CHECK_(is_scientific(dtos(1e-100)), "1e-100 should be scientific: %s", dtos(1e-100).c_str());
  TEST_CHECK_(is_scientific(dtos(1e-50)), "1e-50 should be scientific: %s", dtos(1e-50).c_str());
  TEST_CHECK_(is_scientific(dtos(1e50)), "1e50 should be scientific: %s", dtos(1e50).c_str());
  TEST_CHECK_(is_scientific(dtos(1e100)), "1e100 should be scientific: %s", dtos(1e100).c_str());
  TEST_CHECK_(is_scientific(dtos(1e200)), "1e200 should be scientific: %s", dtos(1e200).c_str());
  TEST_CHECK_(is_scientific(dtos(std::numeric_limits<double>::min())), "DBL_MIN should be scientific: %s", dtos(std::numeric_limits<double>::min()).c_str());
  TEST_CHECK_(is_scientific(dtos(std::numeric_limits<double>::max())), "DBL_MAX should be scientific: %s", dtos(std::numeric_limits<double>::max()).c_str());
  TEST_CHECK_(is_scientific(dtos(std::numeric_limits<float>::min())), "FLT_MIN should be scientific: %s", dtos(std::numeric_limits<float>::min()).c_str());
  TEST_CHECK_(is_scientific(dtos(std::numeric_limits<float>::max())), "FLT_MAX should be scientific: %s", dtos(std::numeric_limits<float>::max()).c_str());

  // =========================================================================
  // SHORTEST REPRESENTATION TESTS
  // =========================================================================

  // Verify common values produce short output
  {
    std::string s = dtos(0.1);
    TEST_CHECK_(s == "0.1", "0.1 should produce '0.1', got '%s'", s.c_str());
  }
  {
    std::string s = dtos(0.5);
    TEST_CHECK_(s == "0.5", "0.5 should produce '0.5', got '%s'", s.c_str());
  }
  {
    std::string s = dtos(0.25);
    TEST_CHECK_(s == "0.25", "0.25 should produce '0.25', got '%s'", s.c_str());
  }
  {
    std::string s = dtos(0.125);
    TEST_CHECK_(s == "0.125", "0.125 should produce '0.125', got '%s'", s.c_str());
  }

  // Float shortest representation
  {
    std::string s = dtos(0.707f);
    size_t digits = count_sig_digits(s);
    TEST_CHECK_(digits <= 9, "0.707f should have <= 9 sig digits, got %zu in '%s'", digits, s.c_str());
    check_float_roundtrip(0.707f);
  }
  {
    std::string s = dtos(0.1f);
    TEST_CHECK_(s == "0.1", "0.1f should produce '0.1', got '%s'", s.c_str());
  }

  // =========================================================================
  // ROUNDTRIP VERIFICATION FOR KEY VALUES
  // =========================================================================

  // Doubles at boundaries
  check_double_roundtrip(0.00001);
  check_double_roundtrip(0.0001);
  check_double_roundtrip(0.001);
  check_double_roundtrip(1e-5);
  check_double_roundtrip(1e-4);
  check_double_roundtrip(1e15);
  check_double_roundtrip(1e16);
  check_double_roundtrip(9.999999999999999e15);

  // Floats at boundaries
  check_float_roundtrip(0.00001f);
  check_float_roundtrip(0.0001f);
  check_float_roundtrip(1e-5f);
  check_float_roundtrip(1e-4f);
  check_float_roundtrip(1e6f);
  check_float_roundtrip(1e7f);
  check_float_roundtrip(9999999.0f);

  // Common problematic values
  check_double_roundtrip(0.1);
  check_double_roundtrip(0.2);
  check_double_roundtrip(0.3);
  check_double_roundtrip(1.0/3.0);
  check_double_roundtrip(2.0/3.0);
  check_float_roundtrip(0.1f);
  check_float_roundtrip(0.2f);
  check_float_roundtrip(0.3f);
  check_float_roundtrip(0.707f);
  check_float_roundtrip(3.14159f);
}
