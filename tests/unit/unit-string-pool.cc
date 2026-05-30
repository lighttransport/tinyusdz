// SPDX-License-Identifier: Apache 2.0
// StringPool unit tests

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-string-pool.h"

#include <string>
#include <thread>
#include <vector>

#include "string-pool.hh"

using namespace tinyusdz;

void string_pool_intern_test(void) {
  StringPool &pool = StringPool::instance();

  // Intern a string and verify same pointer returned
  const char *s1 = pool.intern("hello");
  TEST_CHECK(s1 != nullptr);
  TEST_CHECK(std::string(s1) == "hello");

  const char *s2 = pool.intern("hello");
  TEST_CHECK(s1 == s2);  // Same pointer = deduplicated
  TEST_MSG("Interning same string should return same pointer");
}

void string_pool_intern_different_test(void) {
  StringPool &pool = StringPool::instance();

  const char *s1 = pool.intern("alpha");
  const char *s2 = pool.intern("beta");
  TEST_CHECK(s1 != nullptr);
  TEST_CHECK(s2 != nullptr);
  TEST_CHECK(s1 != s2);
  TEST_CHECK(std::string(s1) == "alpha");
  TEST_CHECK(std::string(s2) == "beta");
  TEST_MSG("Different strings should have different pointers");
}

void string_pool_lookup_test(void) {
  StringPool &pool = StringPool::instance();

  // Lookup existing string
  pool.intern("findme");
  const char *found = pool.lookup("findme");
  TEST_CHECK(found != nullptr);
  TEST_CHECK(std::string(found) == "findme");

  // Lookup non-existent string
  const char *not_found = pool.lookup("nothere");
  TEST_CHECK(not_found == nullptr);
  TEST_MSG("Lookup should return nullptr for unknown strings");
}

void string_pool_pre_intern_test(void) {
  StringPool &pool = StringPool::instance();

  pool.pre_intern({"one", "two", "three"});

  TEST_CHECK(pool.lookup("one") != nullptr);
  TEST_CHECK(pool.lookup("two") != nullptr);
  TEST_CHECK(pool.lookup("three") != nullptr);
  TEST_CHECK(pool.lookup("four") == nullptr);
  TEST_MSG("pre_intern should batch-intern strings");
}

void string_pool_size_test(void) {
  // Use the singleton; just verify size increases with unique strings
  StringPool &pool = StringPool::instance();
  size_t before = pool.size();
  pool.intern("size_test_unique_abcxyz");
  pool.intern("size_test_unique_abcxyz");  // duplicate
  pool.intern("size_test_unique_123456");
  TEST_CHECK(pool.size() >= before + 2);
  TEST_MSG("size() should count unique strings");
}

void string_pool_charptr_test(void) {
  StringPool &pool = StringPool::instance();

  // Verify both overloads work and return valid pointers
  const char *s1 = pool.intern("charptr_test_a");
  const char *s2 = pool.intern(std::string("charptr_test_b"));
  TEST_CHECK(s1 != nullptr);
  TEST_CHECK(s2 != nullptr);
  TEST_CHECK(s1 != s2);
  TEST_CHECK(std::string(s1) == "charptr_test_a");
  TEST_CHECK(std::string(s2) == "charptr_test_b");
  TEST_MSG("const char* and std::string overloads should work");
}

void string_pool_pre_intern_common_test(void) {
  // Test that PreInternCommonStrings doesn't crash
  PreInternCommonStrings();

  // Verify some common strings are interned
  StringPool &pool = StringPool::instance();
  TEST_CHECK(pool.lookup("Xform") != nullptr);
  TEST_CHECK(pool.lookup("Mesh") != nullptr);
  TEST_CHECK(pool.lookup("Material") != nullptr);
  TEST_CHECK(pool.lookup("material:binding") != nullptr);
  TEST_CHECK(pool.lookup("points") != nullptr);
  TEST_CHECK(pool.lookup("UsdPreviewSurface") != nullptr);
  TEST_MSG("PreInternCommonStrings should intern common USD strings");
}
