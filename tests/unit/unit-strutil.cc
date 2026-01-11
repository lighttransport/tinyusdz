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
