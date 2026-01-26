#include "parse_fp16.hh"
#include <iostream>
#include <iomanip>

void print_fp16_info(const char* str) {
  std::cout << "\nParsing: \"" << str << "\"" << std::endl;

  fp16::parse_result result = fp16::parse_fp16(str);

  if (result.success) {
    float value = fp16::fp16_to_fp32(result.value);

    fp16::fp16_bits bits;
    bits.u = result.value;

    std::cout << "  Success!" << std::endl;
    std::cout << "  FP16 value: 0x" << std::hex << std::setw(4)
              << std::setfill('0') << result.value << std::dec << std::endl;
    std::cout << "  Sign: " << bits.parts.sign << std::endl;
    std::cout << "  Exponent: " << bits.parts.exponent << std::endl;
    std::cout << "  Mantissa: " << bits.parts.mantissa << std::endl;
    std::cout << "  FP32 value: " << value << std::endl;
    std::cout << "  Roundtrip: " << fp16::fp16_to_string(result.value) << std::endl;
  } else {
    std::cout << "  Parse FAILED!" << std::endl;
  }
}

int main() {
  std::cout << "======================================" << std::endl;
  std::cout << "FP16 String Parser - Usage Examples" << std::endl;
  std::cout << "======================================" << std::endl;

  // Basic numbers
  print_fp16_info("0");
  print_fp16_info("1");
  print_fp16_info("-1");
  print_fp16_info("3.14159");
  print_fp16_info("42.5");

  // Scientific notation
  print_fp16_info("1e2");
  print_fp16_info("1.5e-3");
  print_fp16_info("-2.5e1");

  // Special values
  print_fp16_info("inf");
  print_fp16_info("-infinity");
  print_fp16_info("nan");

  // Edge cases
  print_fp16_info("65504");     // Max fp16
  print_fp16_info("65520");     // Overflow
  print_fp16_info("0.0001");    // Small value
  print_fp16_info("0.00000001"); // Underflow

  // Invalid inputs
  print_fp16_info("abc");
  print_fp16_info("1.2.3");
  print_fp16_info("");

  // Demonstrate conversion
  std::cout << "\n======================================" << std::endl;
  std::cout << "Float to FP16 Conversion Examples" << std::endl;
  std::cout << "======================================" << std::endl;

  float test_values[] = {0.0f, 1.0f, -1.0f, 3.14159f, 100.0f, 0.001f, -42.5f};

  for (float f : test_values) {
    uint16_t fp16 = fp16::fp32_to_fp16(f);
    float recovered = fp16::fp16_to_fp32(fp16);

    std::cout << "\nOriginal FP32: " << f << std::endl;
    std::cout << "FP16 (hex):    0x" << std::hex << std::setw(4)
              << std::setfill('0') << fp16 << std::dec << std::endl;
    std::cout << "Recovered:     " << recovered << std::endl;
    std::cout << "Error:         " << (f - recovered) << std::endl;
  }

  return 0;
}
