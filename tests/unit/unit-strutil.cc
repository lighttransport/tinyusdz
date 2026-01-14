#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-strutil.h"
#include "str-util.hh"
#include "tiny-string.hh"

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
