// Test fp16 shortest representation and buffer size properties
#include <iostream>
#include <cstring>
#include <cmath>
#include <limits>
#include <iomanip>
#include <cstdint>

// Copy the half implementation
namespace internal {

struct half {
  uint16_t value;
  half() : value(0) {}
  explicit half(uint16_t v) : value(v) {}
};

inline float half_to_float(half h) {
  union {
    uint32_t u;
    float f;
  } fp32;

  uint16_t h16 = h.value;
  fp32.u = (h16 & 0x7fff) << 13;
  uint32_t exp_shifted = 0x7c00 << 13;
  uint32_t exp = exp_shifted & fp32.u;
  fp32.u += (127 - 15) << 23;

  if (exp == exp_shifted) {
    fp32.u += (128 - 16) << 23;
  } else if (exp == 0) {
    const uint32_t magic = 113 << 23;
    fp32.u += 1 << 23;
    fp32.f -= *reinterpret_cast<const float*>(&magic);
  }

  fp32.u |= (h16 & 0x8000) << 16;
  return fp32.f;
}

}

// Include dragonbox
#include "dragonbox_to_chars.h"

int main() {
  std::cout << "=== FP16 Shortest Representation and Buffer Size Tests ===\n\n";

  size_t max_length = 0;
  uint16_t max_length_pattern = 0;

  // Test critical values
  struct TestCase {
    uint16_t bits;
    const char* description;
  };

  TestCase test_cases[] = {
    {0x0000, "Zero"},
    {0x8000, "Negative zero"},
    {0x3c00, "1.0"},
    {0xbc00, "-1.0"},
    {0x7bff, "Max normal (65504)"},
    {0xfbff, "Min normal (-65504)"},
    {0x0400, "Min positive normal"},
    {0x0001, "Min positive subnormal"},
    {0x7c00, "+Infinity"},
    {0xfc00, "-Infinity"},
    {0x7e00, "NaN"},
    {0x4200, "3.0"},
    {0x5640, "100.0"},
    {0x0c00, "0.000061035156"}, // small value
  };

  std::cout << "Testing critical fp16 values:\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << std::left << std::setw(25) << "Description"
            << std::setw(10) << "Bits (hex)"
            << std::setw(20) << "Float value"
            << std::setw(15) << "Dragonbox output" << "\n";
  std::cout << std::string(70, '-') << "\n";

  for (const auto& tc : test_cases) {
    internal::half h(tc.bits);
    float f = internal::half_to_float(h);

    char buf[64];
    char* end = jkj::dragonbox::to_chars(double(f), buf);
    *end = '\0';

    size_t len = end - buf;
    if (len > max_length) {
      max_length = len;
      max_length_pattern = tc.bits;
    }

    std::cout << std::left << std::setw(25) << tc.description
              << "0x" << std::hex << std::setw(8) << tc.bits << std::dec
              << std::setw(20) << f
              << "\"" << buf << "\" (" << len << " chars)\n";
  }

  std::cout << "\n";
  std::cout << "Maximum output length: " << max_length << " characters\n";
  std::cout << "Occurred at bit pattern: 0x" << std::hex << max_length_pattern << std::dec << "\n";
  std::cout << "Buffer size needed (with null terminator): " << (max_length + 1) << " bytes\n";
  std::cout << "Current BUFFER_SIZE_HALF: 16 bytes\n";
  std::cout << "Safety margin: " << (16 - (max_length + 1)) << " bytes\n";

  // Now test shortest representation property
  std::cout << "\n=== Shortest Representation Analysis ===\n\n";

  // Test a specific case: does fp16->float->string preserve shortest rep?
  std::cout << "Testing if shortest representation is preserved:\n\n";

  // Example: 0.333... in fp16
  internal::half h_third(0x3555);  // Approximately 1/3
  float f_from_half = internal::half_to_float(h_third);

  char buf1[64], buf2[64];
  char* end1 = jkj::dragonbox::to_chars(double(f_from_half), buf1);
  *end1 = '\0';

  std::cout << "fp16 bits: 0x" << std::hex << h_third.value << std::dec << "\n";
  std::cout << "Converted to float: " << std::setprecision(17) << f_from_half << "\n";
  std::cout << "Dragonbox output: \"" << buf1 << "\"\n";
  std::cout << "Length: " << (end1 - buf1) << " chars\n\n";

  std::cout << "Analysis:\n";
  std::cout << "- FP16 has ~3.3 decimal digits of precision (log10(2^11))\n";
  std::cout << "- FP32 has ~7.2 decimal digits of precision (log10(2^24))\n";
  std::cout << "- When fp16 is converted to fp32, the value is exact but\n";
  std::cout << "  dragonbox outputs the shortest string that roundtrips to fp32,\n";
  std::cout << "  which may include extra digits beyond fp16 precision.\n\n";

  std::cout << "Conclusion:\n";
  std::cout << "- Current implementation: Preserves FLOAT shortest representation\n";
  std::cout << "- For true FP16 shortest representation, would need direct\n";
  std::cout << "  dragonbox implementation for binary16 format.\n";
  std::cout << "- Trade-off: Current approach is simpler and guarantees\n";
  std::cout << "  correct roundtrip through float conversion.\n";

  // Exhaustive scan for max length
  std::cout << "\n=== Exhaustive Scan (sample) ===\n\n";
  std::cout << "Scanning all 65536 fp16 bit patterns...\n";

  max_length = 0;
  size_t total_tested = 0;
  size_t length_histogram[32] = {0};

  for (uint32_t i = 0; i < 65536; i++) {
    internal::half h(static_cast<uint16_t>(i));
    float f = internal::half_to_float(h);

    char buf[64];

    // Handle special cases (inf, nan)
    if (std::isnan(f) || std::isinf(f)) {
      if (std::isnan(f)) {
        strcpy(buf, "nan");
      } else if (std::isinf(f)) {
        strcpy(buf, f > 0 ? "inf" : "-inf");
      }
    } else {
      char* end = jkj::dragonbox::to_chars(double(f), buf);
      *end = '\0';
    }

    size_t len = strlen(buf);
    if (len > max_length) {
      max_length = len;
      max_length_pattern = i;
    }

    if (len < 32) {
      length_histogram[len]++;
    }
    total_tested++;
  }

  std::cout << "Tested: " << total_tested << " patterns\n";
  std::cout << "Maximum length found: " << max_length << " characters\n";
  std::cout << "At bit pattern: 0x" << std::hex << max_length_pattern << std::dec << "\n";

  // Show the actual worst case
  internal::half worst_h(max_length_pattern);
  float worst_f = internal::half_to_float(worst_h);
  char worst_buf[64];

  if (std::isnan(worst_f) || std::isinf(worst_f)) {
    if (std::isnan(worst_f)) {
      strcpy(worst_buf, "nan");
    } else if (std::isinf(worst_f)) {
      strcpy(worst_buf, worst_f > 0 ? "inf" : "-inf");
    }
  } else {
    char* end = jkj::dragonbox::to_chars(double(worst_f), worst_buf);
    *end = '\0';
  }

  std::cout << "Worst case value: " << worst_f << "\n";
  std::cout << "Worst case string: \"" << worst_buf << "\"\n";
  std::cout << "Required buffer size (with null): " << (max_length + 1) << " bytes\n";
  std::cout << "Current buffer size: 16 bytes\n";
  std::cout << "Status: " << (max_length + 1 <= 16 ? "✓ SAFE" : "✗ UNSAFE") << "\n\n";

  std::cout << "Length distribution:\n";
  for (size_t i = 0; i < 20; i++) {
    if (length_histogram[i] > 0) {
      std::cout << "  " << i << " chars: " << length_histogram[i] << " patterns\n";
    }
  }

  return 0;
}
