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
