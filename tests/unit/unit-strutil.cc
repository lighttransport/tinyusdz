#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-strutil.h"
#include "str-util.hh"

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
