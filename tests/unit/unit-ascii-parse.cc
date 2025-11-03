#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-ascii-parse.h"
#include "tinyusdz.hh"

using namespace tinyusdz;

// Helper function to parse a USD string
static bool parseUSDString(const std::string &usd_content, std::string *err) {
  Stage stage;
  std::string warn;

  bool ret = LoadUSDFromMemory(
      reinterpret_cast<const uint8_t *>(usd_content.data()),
      usd_content.size(),
      "memory.usda",
      &stage,
      &warn,
      err);

  return ret;
}

//
// int64_t digit length guard tests
// These tests verify that excessively long integer literals are rejected
// to prevent denial-of-service attacks via parser resource exhaustion
//

void ascii_parse_int64_valid_test(void) {
  std::string err;

  // Test value under the digit limit (should succeed)
  {
    std::string usd = R"(#usda 1.0
def Xform "Test" {
    int64 testValue = 9223372036854775807
}
)";
    bool ret = parseUSDString(usd, &err);
    TEST_CHECK(ret == true);
  }
}

void ascii_parse_int64_excessive_digits_test(void) {
  std::string err;

  // Test 22 digits (over the 21 digit limit) - should fail
  {
    std::string usd = R"(#usda 1.0
def Xform "Test" {
    int64 testExcessive = 1234567890123456789012
}
)";
    bool ret = parseUSDString(usd, &err);
    TEST_CHECK(ret == false);
  }

  // Test 30 digits - should fail
  {
    std::string usd = R"(#usda 1.0
def Xform "Test" {
    int64 testHuge = 123456789012345678901234567890
}
)";
    bool ret = parseUSDString(usd, &err);
    TEST_CHECK(ret == false);
  }
}

//
// uint64_t digit length guard tests
//

void ascii_parse_uint64_valid_test(void) {
  std::string err;

  // Test value under the digit limit (should succeed)
  {
    std::string usd = R"(#usda 1.0
def Xform "Test" {
    uint64 testValue = 18446744073709551615
}
)";
    bool ret = parseUSDString(usd, &err);
    TEST_CHECK(ret == true);
  }
}

void ascii_parse_uint64_excessive_digits_test(void) {
  std::string err;

  // Test 23 digits (over the 22 digit limit) - should fail
  {
    std::string usd = R"(#usda 1.0
def Xform "Test" {
    uint64 testExcessive = 12345678901234567890123
}
)";
    bool ret = parseUSDString(usd, &err);
    TEST_CHECK(ret == false);
  }

  // Test 30 digits - should fail
  {
    std::string usd = R"(#usda 1.0
def Xform "Test" {
    uint64 testHuge = 123456789012345678901234567890
}
)";
    bool ret = parseUSDString(usd, &err);
    TEST_CHECK(ret == false);
  }
}
