// Comprehensive test comparing direct binary16 dragonbox vs fp32 path
// Compile twice: once with and once without TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX

#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

// Include the implementation with appropriate define
// To test: compile with -DTINYUSDZ_USE_DIRECT_FP16_DRAGONBOX and without

#include "dragonbox_to_chars.h"

#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
#include "dragonbox_binary16.hh"
#endif

namespace internal {

// Duplicate the half struct and conversion
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

// Helper functions from print_fp.cc
template <typename T>
inline int count_digits(T n) {
  int count = 1;
  for (;;) {
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}

inline auto digits2(size_t value) -> const char* {
  alignas(2) static const char data[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  return &data[value * 2];
}

inline void write2digits(char* out, size_t value) {
  *out++ = static_cast<char>('0' + value / 10);
  *out = static_cast<char>('0' + value % 10);
}

char* write_exponent(int exp, char* out) {
  if (exp < 0) {
    *out++ = '-';
    exp = -exp;
  } else {
    *out++ = '+';
  }
  auto uexp = static_cast<uint32_t>(exp);
  if (uexp >= 100u) {
    const char* top = digits2(uexp / 100);
    if (uexp >= 1000u) *out++ = top[0];
    *out++ = static_cast<char>(top[1]);
    uexp %= 100;
  }
  const char* d = digits2(uexp);
  *out++ = static_cast<char>(d[0]);
  *out++ = static_cast<char>(d[1]);
  return out;
}

inline char* fill_n(char* p, int n, char c) {
  for (int i = 0; i < n; i++, p++) {
    *p = c;
  }
  return p;
}

inline void format_decimal_impl(char* out, uint64_t value, uint32_t size) {
  unsigned n = size;
  while (value >= 100) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value % 100));
    value /= 100;
  }
  if (value >= 10) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value));
  } else {
    out[--n] = static_cast<char>('0' + value);
  }
}

inline char* format_decimal(char* out, uint64_t value, uint32_t num_digits) {
  format_decimal_impl(out, value, num_digits);
  return out + num_digits;
}

inline char* write_significand_e(char* out, uint64_t significand,
                                 int significand_size, int exponent) {
  out = format_decimal(out, significand, significand_size);
  return fill_n(out, exponent, '0');
}

inline char* write_significand(char* out, uint64_t significand,
                               int significand_size, int integral_size,
                               char decimal_point) {
  if (!decimal_point) return format_decimal(out, significand, significand_size);
  out += significand_size + 1;
  char* end = out;
  int floating_size = significand_size - integral_size;
  for (int i = floating_size / 2; i > 0; --i) {
    out -= 2;
    write2digits(out, static_cast<std::size_t>(significand % 100));
    significand /= 100;
  }
  if (floating_size % 2 != 0) {
    *--out = static_cast<char>('0' + significand % 10);
    significand /= 10;
  }
  *--out = decimal_point;
  format_decimal(out - integral_size, significand, integral_size);
  return end;
}

// The actual conversion function with both implementations
char* dtoa_dragonbox(const half h, char* buf) {
#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
  // Direct binary16 implementation
  uint16_t bits = h.value;
  dragonbox::binary16::ieee754_binary16 fp16(bits);

  if (fp16.is_zero()) {
    *buf++ = '0';
    return buf;
  }

  if (!fp16.is_finite()) {
    uint16_t exp_bits = fp16.get_exponent_bits();
    if (exp_bits == 0x1f) {
      uint16_t mant_bits = fp16.get_mantissa_bits();
      if (mant_bits == 0) {
        if (fp16.is_negative()) *buf++ = '-';
        strcpy(buf, "inf");
        return buf + 3;
      } else {
        strcpy(buf, "nan");
        return buf + 3;
      }
    }
  }

  auto result = dragonbox::binary16::to_decimal_precise(fp16);
  uint64_t significand = result.significand;
  int exponent = result.exponent;

  if (fp16.is_negative()) *buf++ = '-';

  int significand_size = count_digits(significand);
  int output_exp = exponent + significand_size - 1;

  const int exp_upper = 5;
  const int exp_lower = -4;
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper);

  char decimal_point = '.';
  char exp_char = 'e';

  if (use_exp_format) {
    if (significand_size == 1) decimal_point = '\0';
    buf = write_significand(buf, significand, significand_size, 1, decimal_point);
    *buf++ = exp_char;
    buf = write_exponent(output_exp, buf);
  } else {
    int decimal_exp = exponent + significand_size;
    if (exponent >= 0) {
      buf = write_significand_e(buf, significand, significand_size, exponent);
    } else if (decimal_exp > 0) {
      buf = write_significand(buf, significand, significand_size, decimal_exp, decimal_point);
    } else {
      *buf++ = '0';
      *buf++ = decimal_point;
      int num_zeros = -decimal_exp;
      buf = fill_n(buf, num_zeros, '0');
      buf = format_decimal(buf, significand, significand_size);
    }
  }

  return buf;
#else
  // Indirect fp32 path
  float f = half_to_float(h);

  if (f == 0.0f || !std::isfinite(f)) {
    if (f == 0.0f) {
      *buf++ = '0';
      return buf;
    } else if (std::isnan(f)) {
      strcpy(buf, "nan");
      return buf + 3;
    } else {
      if (f < 0) *buf++ = '-';
      strcpy(buf, "inf");
      return buf + 3;
    }
  }

  auto ret = jkj::dragonbox::to_chars(double(f), buf);
  return ret;
#endif
}

} // namespace internal

int main() {
#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
  std::cout << "=== Testing DIRECT BINARY16 Dragonbox ===" << std::endl;
#else
  std::cout << "=== Testing FP32 PATH (fp16->fp32->dragonbox) ===" << std::endl;
#endif
  std::cout << std::endl;

  // Test cases
  struct TestCase {
    uint16_t bits;
    const char* description;
  };

  TestCase tests[] = {
    {0x0000, "Zero"},
    {0x8000, "Negative zero"},
    {0x3c00, "1.0"},
    {0xbc00, "-1.0"},
    {0x4000, "2.0"},
    {0x3800, "0.5"},
    {0x3555, "~0.333"},
    {0x7bff, "Max (65504)"},
    {0xfbff, "Min (-65504)"},
    {0x0400, "Min normal"},
    {0x0001, "Min subnormal"},
    {0x7c00, "+Infinity"},
    {0xfc00, "-Infinity"},
    {0x7e00, "NaN"},
  };

  size_t total_length = 0;
  size_t max_length = 0;
  uint16_t max_pattern = 0;

  std::cout << std::left << std::setw(20) << "Description"
            << std::setw(12) << "Bits (hex)"
            << std::setw(15) << "Float value"
            << std::setw(20) << "Output string"
            << "Length" << std::endl;
  std::cout << std::string(75, '-') << std::endl;

  for (const auto& tc : tests) {
    internal::half h(tc.bits);
    float f = internal::half_to_float(h);

    char buf[64];
    char* end = internal::dtoa_dragonbox(h, buf);
    *end = '\0';

    size_t len = strlen(buf);
    total_length += len;
    if (len > max_length) {
      max_length = len;
      max_pattern = tc.bits;
    }

    std::cout << std::left << std::setw(20) << tc.description
              << "0x" << std::hex << std::setw(10) << tc.bits << std::dec
              << std::setw(15) << f
              << "\"" << std::setw(18) << buf << "\""
              << len << std::endl;
  }

  std::cout << std::endl;
  std::cout << "Statistics for test cases:" << std::endl;
  std::cout << "  Average length: " << (total_length / (sizeof(tests)/sizeof(tests[0]))) << " chars" << std::endl;
  std::cout << "  Maximum length: " << max_length << " chars (0x" << std::hex << max_pattern << std::dec << ")" << std::endl;

  // Exhaustive scan
  std::cout << std::endl << "Running exhaustive scan of all 65536 bit patterns..." << std::endl;

  total_length = 0;
  max_length = 0;
  size_t length_histogram[32] = {0};

  for (uint32_t i = 0; i < 65536; i++) {
    internal::half h(static_cast<uint16_t>(i));

    char buf[64];
    char* end = internal::dtoa_dragonbox(h, buf);
    *end = '\0';

    size_t len = strlen(buf);
    total_length += len;

    if (len > max_length) {
      max_length = len;
      max_pattern = i;
    }

    if (len < 32) {
      length_histogram[len]++;
    }
  }

  std::cout << std::endl;
  std::cout << "Exhaustive scan results:" << std::endl;
  std::cout << "  Total patterns: 65536" << std::endl;
  std::cout << "  Average length: " << std::fixed << std::setprecision(2)
            << (double(total_length) / 65536.0) << " chars" << std::endl;
  std::cout << "  Maximum length: " << max_length << " chars" << std::endl;
  std::cout << "  Worst case pattern: 0x" << std::hex << max_pattern << std::dec << std::endl;

  // Show worst case
  internal::half worst_h(max_pattern);
  float worst_f = internal::half_to_float(worst_h);
  char worst_buf[64];
  char* worst_end = internal::dtoa_dragonbox(worst_h, worst_buf);
  *worst_end = '\0';

  std::cout << "  Worst case value: " << worst_f << std::endl;
  std::cout << "  Worst case string: \"" << worst_buf << "\"" << std::endl;

#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
  std::cout << "  Required buffer: " << (max_length + 1) << " bytes (16 bytes allocated)" << std::endl;
  std::cout << "  Status: " << (max_length + 1 <= 16 ? "✓ SAFE" : "✗ UNSAFE") << std::endl;
#else
  std::cout << "  Required buffer: " << (max_length + 1) << " bytes (24 bytes allocated)" << std::endl;
  std::cout << "  Status: " << (max_length + 1 <= 24 ? "✓ SAFE" : "✗ UNSAFE") << std::endl;
#endif

  std::cout << std::endl << "Length distribution:" << std::endl;
  for (size_t i = 0; i < 25; i++) {
    if (length_histogram[i] > 0) {
      std::cout << "  " << std::setw(2) << i << " chars: "
                << std::setw(6) << length_histogram[i] << " patterns";

      // Show percentage bar
      int bar_len = (length_histogram[i] * 50) / 65536;
      std::cout << " [";
      for (int j = 0; j < bar_len; j++) std::cout << "#";
      for (int j = bar_len; j < 50; j++) std::cout << " ";
      std::cout << "]";

      std::cout << " " << std::fixed << std::setprecision(1)
                << (100.0 * length_histogram[i] / 65536.0) << "%"
                << std::endl;
    }
  }

  return 0;
}
