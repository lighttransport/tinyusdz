// Example usage of dtoa_dragonbox with buffer size constants
// Compile: g++ -std=c++14 example_usage.cc -I ../../src/external/dragonbox/ ../../src/external/dragonbox/dragonbox_to_chars.cpp -I../../src/external -o example_usage

#include <iostream>
#include <cmath>
#include <limits>

// Include the dragonbox implementation (in real code, you'd include the header)
#include "dragonbox_to_chars.h"

// Buffer size constants (same as in print_fp.cc)
namespace internal {
  constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT = 24;
  constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE = 32;
  constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE = DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE;

  // Simple wrapper for float
  char* dtoa_dragonbox(const float f, char* buf) {
    // Handle zero specially (dragonbox doesn't handle it)
    if (f == 0.0f) {
      *buf++ = '0';
      return buf;
    }

    // Use dragonbox for non-zero values
    auto ret = jkj::dragonbox::to_chars(double(f), buf);
    return ret;
  }

  // Simple wrapper for double
  char* dtoa_dragonbox(const double d, char* buf) {
    // Handle zero specially (dragonbox doesn't handle it)
    if (d == 0.0) {
      *buf++ = '0';
      return buf;
    }

    // Use dragonbox for non-zero values
    auto ret = jkj::dragonbox::to_chars(d, buf);
    return ret;
  }
}

int main() {
  std::cout << "=== dtoa_dragonbox Usage Examples ===" << std::endl;
  std::cout << std::endl;

  // Example 1: Float conversion
  std::cout << "Example 1: Float conversion" << std::endl;
  {
    float value = 3.14159f;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';  // Null-terminate
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 2: Double conversion
  std::cout << "Example 2: Double conversion" << std::endl;
  {
    double value = 3.141592653589793;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 3: Very small number
  std::cout << "Example 3: Very small number (scientific notation)" << std::endl;
  {
    double value = 1.23e-100;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 4: Very large number
  std::cout << "Example 4: Very large number (scientific notation)" << std::endl;
  {
    double value = 9.87e+200;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 5: Negative number
  std::cout << "Example 5: Negative number" << std::endl;
  {
    float value = -2.71828f;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 6: Zero
  std::cout << "Example 6: Zero (special case)" << std::endl;
  {
    double value = 0.0;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 7: Maximum float
  std::cout << "Example 7: Maximum float value" << std::endl;
  {
    float value = std::numeric_limits<float>::max();
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 8: Minimum positive float
  std::cout << "Example 8: Minimum positive float value" << std::endl;
  {
    float value = std::numeric_limits<float>::min();
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
  }
  std::cout << std::endl;

  // Example 9: Generic buffer (works for both float and double)
  std::cout << "Example 9: Generic buffer (auto-sized)" << std::endl;
  {
    double value = 123.456;
    char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE];  // Generic size
    char* end = internal::dtoa_dragonbox(value, buffer);
    *end = '\0';
    std::cout << "  Input:  " << value << std::endl;
    std::cout << "  Output: " << buffer << std::endl;
    std::cout << "  Buffer size: " << internal::DTOA_DRAGONBOX_BUFFER_SIZE << " bytes" << std::endl;
  }
  std::cout << std::endl;

  // Example 10: Buffer size information
  std::cout << "Example 10: Buffer size constants" << std::endl;
  std::cout << "  DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT:  "
            << internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT << " bytes" << std::endl;
  std::cout << "  DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE: "
            << internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE << " bytes" << std::endl;
  std::cout << "  DTOA_DRAGONBOX_BUFFER_SIZE:        "
            << internal::DTOA_DRAGONBOX_BUFFER_SIZE << " bytes" << std::endl;

  return 0;
}
