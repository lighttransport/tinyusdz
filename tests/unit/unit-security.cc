#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-security.h"
#include "tinyusdz.hh"

#include <string>
#include <vector>

using namespace tinyusdz;

void security_empty_input_test(void) {
  Stage stage;
  std::string warn, err;

  // nullptr with 0 length
  {
    bool ok = LoadUSDAFromMemory(nullptr, 0, "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Loading nullptr/0 should fail gracefully");
  }

  // Empty string
  {
    std::string empty_str;
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(empty_str.data()), empty_str.size(),
        "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Loading empty string should fail gracefully");
  }

  // Single whitespace
  {
    std::string ws = " ";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(ws.data()), ws.size(),
        "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Loading whitespace-only should fail gracefully");
  }
}

void security_truncated_header_test(void) {
  Stage stage;
  std::string warn, err;

  // Just "#usd" (truncated)
  {
    std::string truncated = "#usd";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(truncated.data()), truncated.size(),
        "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Truncated header '#usd' should fail gracefully");
  }

  // "#usda" without version
  {
    std::string partial = "#usda";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(partial.data()), partial.size(),
        "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Partial header '#usda' should fail gracefully");
  }

  // "#usda 1" (incomplete version)
  {
    std::string incomplete = "#usda 1";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(incomplete.data()),
        incomplete.size(), "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Incomplete version should fail gracefully");
  }

  // Just a newline
  {
    std::string nl = "\n";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(nl.data()), nl.size(),
        "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("Newline-only input should fail gracefully");
  }
}

void security_null_bytes_test(void) {
  Stage stage;
  std::string warn, err;

  // Valid header followed by null bytes
  {
    std::string data = "#usda 1.0\n";
    data += '\0';
    data += '\0';
    data += "def Xform \"test\" {}\n";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(data.data()), data.size(),
        "test.usda", &stage, &warn, &err);
    // Either succeeds or fails, but should not crash
    (void)ok;
    TEST_CHECK(true);
    TEST_MSG("Null bytes in data should not crash");
  }

  // All null bytes
  {
    std::vector<uint8_t> nulls(64, 0);
    bool ok = LoadUSDAFromMemory(nulls.data(), nulls.size(),
                                 "test.usda", &stage, &warn, &err);
    TEST_CHECK(!ok);
    TEST_MSG("All-null input should fail gracefully");
  }

  // Null bytes in prim name
  {
    std::string data = "#usda 1.0\ndef Xform \"te";
    data += '\0';
    data += "st\" {}\n";
    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(data.data()), data.size(),
        "test.usda", &stage, &warn, &err);
    // Should not crash regardless of result
    (void)ok;
    TEST_CHECK(true);
    TEST_MSG("Null byte in prim name should not crash");
  }
}

void security_deeply_nested_test(void) {
  Stage stage;
  std::string warn, err;

  // Generate USDA with 500 levels of nesting
  std::string usda = "#usda 1.0\n";
  const int depth = 500;

  for (int i = 0; i < depth; i++) {
    std::string indent(i * 4, ' ');
    usda += indent + "def Xform \"level" + std::to_string(i) + "\"\n";
    usda += indent + "{\n";
  }
  for (int i = depth - 1; i >= 0; i--) {
    std::string indent(i * 4, ' ');
    usda += indent + "}\n";
  }

  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
      "test.usda", &stage, &warn, &err);
  // Should either succeed or fail gracefully (no stack overflow)
  (void)ok;
  TEST_CHECK(true);
  TEST_MSG("Deeply nested USDA should not cause stack overflow");
}

void security_huge_array_test(void) {
  Stage stage;
  std::string warn, err;

  // Declare a large array without actually providing that many elements
  std::string usda =
      "#usda 1.0\n"
      "def Mesh \"HugeMesh\"\n"
      "{\n"
      "    int[] faceVertexCounts = [999999999]\n"
      "    int[] faceVertexIndices = [0]\n"
      "}\n";

  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
      "test.usda", &stage, &warn, &err);
  // Should handle gracefully - may succeed (it's valid syntax even if
  // semantically wrong) or fail, but should not crash
  (void)ok;
  TEST_CHECK(true);
  TEST_MSG("Huge array declaration should not crash");

  // Another test: very large claimed array count in bracket notation
  {
    std::string usda2 =
        "#usda 1.0\n"
        "def Mesh \"HugeMesh2\"\n"
        "{\n"
        "    float[] points = [";
    // Add a modest number of actual values
    for (int i = 0; i < 100; i++) {
      if (i > 0) usda2 += ", ";
      usda2 += "0.0";
    }
    usda2 += "]\n}\n";

    Stage stage2;
    bool ok2 = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(usda2.data()), usda2.size(),
        "test.usda", &stage2, &warn, &err);
    (void)ok2;
    TEST_CHECK(true);
    TEST_MSG("Large float array should not crash");
  }
}

void security_malformed_utf8_test(void) {
  Stage stage;
  std::string warn, err;

  // Invalid UTF-8 continuation byte
  {
    std::string usda = "#usda 1.0\ndef Xform \"test\" {\n";
    usda += "    string myStr = \"hello \x80\x81\x82 world\"\n";
    usda += "}\n";

    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
        "test.usda", &stage, &warn, &err);
    (void)ok;
    TEST_CHECK(true);
    TEST_MSG("Invalid UTF-8 continuation bytes should not crash");
  }

  // Overlong encoding
  {
    std::string usda = "#usda 1.0\ndef Xform \"test2\" {\n";
    usda += "    string myStr = \"test \xC0\xAF end\"\n";
    usda += "}\n";

    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
        "test.usda", &stage, &warn, &err);
    (void)ok;
    TEST_CHECK(true);
    TEST_MSG("Overlong UTF-8 encoding should not crash");
  }

  // Truncated multi-byte sequence
  {
    std::string usda = "#usda 1.0\ndef Xform \"test3\" {\n";
    usda += "    string myStr = \"test \xE0\x80\"\n";
    usda += "}\n";

    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
        "test.usda", &stage, &warn, &err);
    (void)ok;
    TEST_CHECK(true);
    TEST_MSG("Truncated multi-byte UTF-8 should not crash");
  }

  // Invalid start byte (0xFE, 0xFF)
  {
    std::string usda = "#usda 1.0\ndef Xform \"test4\" {\n";
    usda += "    string myStr = \"test \xFE\xFF end\"\n";
    usda += "}\n";

    bool ok = LoadUSDAFromMemory(
        reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
        "test.usda", &stage, &warn, &err);
    (void)ok;
    TEST_CHECK(true);
    TEST_MSG("Invalid UTF-8 start bytes 0xFE/0xFF should not crash");
  }
}

void security_recursive_reference_test(void) {
  Stage stage;
  std::string warn, err;

  // USDA that references itself
  std::string usda =
      "#usda 1.0\n"
      "def Xform \"Root\" (\n"
      "    prepend references = @test.usda@\n"
      ")\n"
      "{\n"
      "}\n";

  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
      "test.usda", &stage, &warn, &err);
  // Should fail or succeed, but must not hang or infinite-loop
  (void)ok;
  TEST_CHECK(true);
  TEST_MSG("Self-referencing USDA should not cause infinite loop");
}
