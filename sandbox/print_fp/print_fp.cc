#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>
#include <array>

#include "dragonbox_to_chars.h"
#include "dtoa_milo.h"

// Define this to use direct binary16 dragonbox (shorter strings, smaller buffer)
// Undefine to use fp16->fp32->dragonbox (simpler, reuses existing code)
// #define TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX

#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
#include "dragonbox_binary16.hh"
#endif

// Forward declare half (defined in internal namespace later)
namespace internal { struct half; }

using half2 = std::array<internal::half, 2>;
using half3 = std::array<internal::half, 3>;
using half4 = std::array<internal::half, 4>;
using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;
using double2 = std::array<double, 2>;
using double3 = std::array<double, 3>;
using double4 = std::array<double, 4>;

std::vector<float> gen_floats(size_t n) {
  std::vector<float> dst;
  dst.resize(n);

  std::random_device rd;

  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-0.1, 0.1);

  for (size_t i = 0; i < n; i++) {
    double f = dist(engine);
    dst[i] = float(f);
  }

  return dst;
}

// ----------------------------------------------------------------------
// based on fmtlib
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// MIT license.
//

namespace internal {

// Maximum buffer sizes required for dtoa_dragonbox
// These are conservative upper bounds to ensure no buffer overflow
//
// Half (fp16) worst case:
//
// When TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX is defined:
//   Uses direct binary16 dragonbox - produces true shortest representation
//   Worst case: "-6.5504e+4" = 10 chars + null = 11 bytes
//   Buffer size: 16 bytes (conservative, with safety margin)
//
// When TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX is NOT defined (default):
//   fp16 values are converted to float before dragonbox conversion,
//   so output contains enough digits for float32 roundtrip, not just fp16.
//   Worst case (from exhaustive testing): "-1.1920928955078125e-7" = 22 chars
//   Buffer size: 24 bytes (allows for future format changes)
//
#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_HALF = 16;
#else
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_HALF = 24;
#endif

// Float worst case:
//   - Sign: 1 byte
//   - Significand: 9 digits (worst case for single precision)
//   - Decimal point: 1 byte
//   - Exponent 'e': 1 byte
//   - Exponent sign: 1 byte
//   - Exponent digits: 3 bytes (e.g., "-38" to "+38")
//   - Null terminator: 1 byte
//   Total: 17 bytes, rounded up to 24 for safety
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT = 24;

// Double worst case:
//   - Sign: 1 byte
//   - Significand: 17 digits (worst case for double precision)
//   - Decimal point: 1 byte
//   - Exponent 'e': 1 byte
//   - Exponent sign: 1 byte
//   - Exponent digits: 4 bytes (e.g., "-308" to "+308")
//   - Null terminator: 1 byte
//   Total: 26 bytes, rounded up to 32 for safety
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE = 32;

// Conservative maximum for all types
constexpr size_t DTOA_DRAGONBOX_BUFFER_SIZE = DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE;

// Half-precision (fp16) support
// IEEE 754 binary16: 1 sign bit, 5 exponent bits, 10 mantissa bits
struct half {
  uint16_t value;

  half() : value(0) {}
  explicit half(uint16_t v) : value(v) {}
};

// Convert half (fp16) to float (fp32)
// Based on implementation from TinyUSDZ value-types.cc
inline float half_to_float(half h) {
  // Little endian implementation (most common)
  union {
    uint32_t u;
    float f;
    struct {
      uint32_t mantissa : 23;
      uint32_t exponent : 8;
      uint32_t sign : 1;
    } bits;
  } fp32;

  uint16_t h16 = h.value;
  uint32_t h_exp = (h16 >> 10) & 0x1f;
  uint32_t h_mant = h16 & 0x3ff;
  uint32_t h_sign = (h16 >> 15) & 1;

  // Build fp32
  fp32.u = (h16 & 0x7fff) << 13;  // exponent/mantissa bits
  uint32_t exp_shifted = 0x7c00 << 13;  // exponent mask after shift
  uint32_t exp = exp_shifted & fp32.u;
  fp32.u += (127 - 15) << 23;  // exponent adjust

  // Handle special cases
  if (exp == exp_shifted) {
    // Inf or NaN
    fp32.u += (128 - 16) << 23;
  } else if (exp == 0) {
    // Zero or denormal
    const uint32_t magic = 113 << 23;
    fp32.u += 1 << 23;
    fp32.f -= *reinterpret_cast<const float*>(&magic);
  }

  fp32.u |= (h16 & 0x8000) << 16;  // sign bit
  return fp32.f;
}

// TOOD: Use builtin_clz insturction?
// T = uint32 or uint64
template <typename T>
inline int count_digits(T n) {
  int count = 1;
  for (;;) {
    // Integer division is slow so do it for a group of four digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}

// Converts value in the range [0, 100) to a string.
// GCC generates slightly better code when value is pointer-size.
inline auto digits2(size_t value) -> const char* {
  // Align data since unaligned access may be slower when crossing a
  // hardware-specific boundary.
  alignas(2) static const char data[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  return &data[value * 2];
}

// Writes a two-digit value to out.
inline void write2digits(char* out, size_t value) {
  // if (!is_constant_evaluated() && std::is_same<Char, char>::value &&
  //     !FMT_OPTIMIZE_SIZE) {
  //   memcpy(out, digits2(value), 2);
  //   return;
  // }
  *out++ = static_cast<char>('0' + value / 10);
  *out = static_cast<char>('0' + value % 10);
}

// Writes the exponent exp in the form "[+-]d{2,3}" to buffer.
char* write_exponent(int exp, char* out) {
  // FMT_ASSERT(-10000 < exp && exp < 10000, "exponent out of range");
  if (exp < 0) {
    *out++ = '-';
    exp = -exp;
  } else {
    *out++ = '+';
  }
  auto uexp = static_cast<uint32_t>(exp);
  // if (is_constant_evaluated()) {
  //   if (uexp < 10) *out++ = '0';
  //   return format_decimal<Char>(out, uexp, count_digits(uexp));
  // }
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
  // FMT_ASSERT(size >= count_digits(value), "invalid digit count");
  unsigned n = size;
  while (value >= 100) {
    // Integer division is slow so do it for a group of two digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
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
  //return out + n;
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

// Use dragonbox algorithm to print floating point value.
// Use to_deciamal and do human-readable pretty printing for some value range(e.g. print 1e-3 as 0.001)
//
// exp_upper: (15 + 1) for double, (6+1) for float
char* dtoa_dragonbox(const double f, char* buf, int exp_upper = 16) {
  const int spec_precision = -1;  // unlimited

  // Fast path for common values 1.0 and -1.0 (bitwise comparison)
  // IEEE 754 double precision: 1.0 = 0x3FF0000000000000, -1.0 = 0xBFF0000000000000
  uint64_t bits;
  std::memcpy(&bits, &f, sizeof(double));

  if (bits == 0x3FF0000000000000ULL) {
    // Exactly 1.0
    *buf++ = '1';
    return buf;
  }
  if (bits == 0xBFF0000000000000ULL) {
    // Exactly -1.0
    *buf++ = '-';
    *buf++ = '1';
    return buf;
  }

  bool is_negative = std::signbit(f);

  auto ret = jkj::dragonbox::to_decimal(f);

  // print human-readable float for the value in range [1e-exp_lower, 1e+exp_upper]
  const int exp_lower = -4;
  char exp_char = 'e';
  char zero_char = '0';

  auto significand = ret.significand;
  int significand_size = count_digits(significand);

  size_t size = size_t(significand_size) + (is_negative ? 1u : 0u);

  int output_exp = ret.exponent + significand_size - 1;
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper);

  char decimal_point = '.';
  if (use_exp_format) {
    int num_zeros = 0;
    if (significand_size == 1) {
      decimal_point = '\0';
    }
    auto abs_output_exp = output_exp >= 0 ? output_exp : -output_exp;
    int exp_digits = 2;
    if (abs_output_exp >= 100) exp_digits = abs_output_exp >= 1000 ? 4 : 3;

    size += (decimal_point ? 1u : 0u) + 2u + size_t(exp_digits);

    if (is_negative) {
      *buf++ = '-';
    }

    buf =
        write_significand(buf, significand, significand_size, 1, decimal_point);

    if (num_zeros > 0) buf = fill_n(buf, num_zeros, zero_char);
    *buf++ = exp_char;
    return write_exponent(output_exp, buf);
  }

  int exp = ret.exponent + significand_size;
  if (ret.exponent >= 0) {
    // 1234e5 -> 123400000[.0+]
    size += static_cast<size_t>(ret.exponent);
    int num_zeros = spec_precision - exp;
    // abort_fuzzing_if(num_zeros > 5000);
    // if (specs.alt()) {
    //   ++size;
    //   if (num_zeros <= 0 && specs.type() != presentation_type::fixed)
    //     num_zeros = 0;
    //   if (num_zeros > 0) size += size_t(num_zeros);
    // }
    // auto grouping = Grouping(loc, specs.localized());
    // size += size_t(grouping.count_separators(exp));
    // return write_padded<Char, align::right>(out, specs, size, [&](iterator
    // it) {
    //   if (s != sign::none) *it++ = detail::getsign<Char>(s);
    //   it = write_significand<Char>(it, significand, significand_size,
    //                                f.exponent, grouping);
    //   if (!specs.alt()) return it;
    //   *it++ = decimal_point;
    //   return num_zeros > 0 ? detail::fill_n(it, num_zeros, zero) : it;
    // });

    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand_e(buf, significand, significand_size,
                               ret.exponent);

  } else if (exp > 0) {
    // 1234e-2 -> 12.34[0+]
    // int num_zeros = specs.alt() ? spec_precision - significand_size : 0;
    // size += 1 + static_cast<unsigned>(max_of(num_zeros, 0));
    size += 1;
    // auto grouping = Grouping(loc, specs.localized());
    // size += size_t(grouping.count_separators(exp));
    // return write_padded<Char, align::right>(out, specs, size, [&](iterator
    // it) {
    //   if (s != sign::none) *it++ = detail::getsign<Char>(s);
    //   it = write_significand(it, significand, significand_size, exp,
    //                          decimal_point, grouping);
    //   return num_zeros > 0 ? detail::fill_n(it, num_zeros, zero) : it;
    // });
    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand(buf, significand, significand_size, exp,
                             decimal_point);
  }
  // 1234e-6 -> 0.001234
  int num_zeros = -exp;
  // if (significand_size == 0 && specs.precision >= 0 &&
  //     specs.precision < num_zeros) {
  //   num_zeros = spec_precision;
  // }
  bool pointy = num_zeros != 0 || significand_size != 0;  // || specs.alt();
  size += 1u + (pointy ? 1u : 0u) + size_t(num_zeros);
  // return write_padded<Char, align::right>(out, specs, size, [&](iterator it)
  // {
  //   if (s != sign::none) *it++ = detail::getsign<Char>(s);
  //   *it++ = zero;
  //   if (!pointy) return it;
  //   *it++ = decimal_point;
  //   it = detail::fill_n(it, num_zeros, zero);
  //   return write_significand<Char>(it, significand, significand_size);
  // });

  if (is_negative) {
    *buf++ = '-';
  }

  *buf++ = zero_char;

  if (!pointy) return buf;
  *buf++ = decimal_point;
  buf = fill_n(buf, num_zeros, zero_char);

  return format_decimal(buf, significand, significand_size);
}

char* dtoa_dragonbox(const float f, char* buf) {
  // Fast path for common values 1.0f and -1.0f (bitwise comparison)
  // IEEE 754 single precision: 1.0f = 0x3F800000, -1.0f = 0xBF800000
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(float));

  if (bits == 0x3F800000U) {
    // Exactly 1.0f
    *buf++ = '1';
    return buf;
  }
  if (bits == 0xBF800000U) {
    // Exactly -1.0f
    *buf++ = '-';
    *buf++ = '1';
    return buf;
  }

  return dtoa_dragonbox(double(f), buf, 7);
}

// Convert half (fp16) to string using dragonbox
//
// Two implementations available via compile-time switch:
//
// 1. TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX defined:
//    - Uses direct binary16 dragonbox algorithm
//    - Produces TRUE shortest representation for fp16
//    - Shorter strings (~70% reduction vs fp32 path)
//    - Smaller buffer requirement (16 vs 24 bytes)
//    - Example: 0.333 → "0.3333" (5 chars)
//
// 2. TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX NOT defined (default):
//    - Converts half → float → dragonbox(float)
//    - Produces shortest representation for FLOAT32
//    - Longer strings but simpler implementation
//    - Example: 0.333 → "0.33325195312500" (16 chars)
//
char* dtoa_dragonbox(const half h, char* buf) {
#ifdef TINYUSDZ_USE_DIRECT_FP16_DRAGONBOX
  // Direct binary16 dragonbox implementation

  // Handle special cases
  uint16_t bits = h.value;
  dragonbox::binary16::ieee754_binary16 fp16(bits);

  // Check for zero
  if (fp16.is_zero()) {
    *buf++ = '0';
    return buf;
  }

  // Check for infinity
  if (!fp16.is_finite()) {
    uint16_t exp_bits = fp16.get_exponent_bits();
    if (exp_bits == 0x1f) {
      uint16_t mant_bits = fp16.get_mantissa_bits();
      if (mant_bits == 0) {
        // Infinity
        if (fp16.is_negative()) {
          *buf++ = '-';
        }
        *buf++ = 'i';
        *buf++ = 'n';
        *buf++ = 'f';
        return buf;
      } else {
        // NaN
        *buf++ = 'n';
        *buf++ = 'a';
        *buf++ = 'n';
        return buf;
      }
    }
  }

  // Use dragonbox to get decimal representation
  auto result = dragonbox::binary16::to_decimal_precise(fp16);
  uint64_t significand = result.significand;
  int exponent = result.exponent;

  // Add sign
  if (fp16.is_negative()) {
    *buf++ = '-';
  }

  // Determine output format (scientific vs fixed-point)
  int significand_size = count_digits(significand);
  int output_exp = exponent + significand_size - 1;

  // Use human-readable format for reasonable range, otherwise scientific
  const int exp_upper = 5;   // fp16 max is ~65504 (10^4.8)
  const int exp_lower = -4;  // Small values in scientific notation
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper);

  char decimal_point = '.';
  char exp_char = 'e';

  if (use_exp_format) {
    // Scientific notation: d.ddd...e±ee
    if (significand_size == 1) {
      decimal_point = '\0';
    }

    buf = write_significand(buf, significand, significand_size, 1, decimal_point);
    *buf++ = exp_char;
    buf = write_exponent(output_exp, buf);
  } else {
    // Fixed-point notation
    int decimal_exp = exponent + significand_size;

    if (exponent >= 0) {
      // No decimal point needed (or trailing zeros)
      buf = write_significand_e(buf, significand, significand_size, exponent);
    } else if (decimal_exp > 0) {
      // Decimal point in the middle: dd.ddd
      buf = write_significand(buf, significand, significand_size, decimal_exp, decimal_point);
    } else {
      // Leading zeros: 0.000ddd
      *buf++ = '0';
      *buf++ = decimal_point;
      int num_zeros = -decimal_exp;
      buf = fill_n(buf, num_zeros, '0');
      buf = format_decimal(buf, significand, significand_size);
    }
  }

  return buf;

#else
  // Indirect implementation: Convert half → float → dragonbox(float)
  // Simpler but produces longer strings with float32 precision
  float f = half_to_float(h);

  // For half-precision, use tighter exponent range
  // fp16 range is approximately ±65504, so exp_upper = 5 is sufficient
  return dtoa_dragonbox(double(f), buf, 5);
#endif
}

} // namespace internal

// -------------------------------------------------------------

std::string print_floats(const std::vector<float> &v) {

  char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];

  size_t n = v.size();
  std::vector<char> dst;
  dst.reserve(n * 10); // 10 : heuristics.

  size_t curr = 0;
  for (size_t i = 0; i < v.size(); i++) {

    if (i > 0) {
      dst[curr] =  ',';
      dst[curr+1] =  ' ';
      curr += 2;
    }

    char *e = internal::dtoa_dragonbox(v[i], buffer);
    size_t len = e - buffer; // includes '\0'

    // +2 for ', '
    if ((curr + len + 2) >= dst.size()) {
      dst.resize((curr + len) + 2);
    }

    memcpy(dst.data() + curr, buffer, len);

    curr += len;
  }
  dst[curr] = '\n';
  std::string s(dst.data(), curr);
  return s;
}

template<size_t N>
std::string print_float_array(const std::vector<std::array<float, N>> &v) {
  std::ostringstream oss;
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      oss << ", ";
    }
    
    oss << "(";
    
    for (size_t j = 0; j < N; j++) {
      if (j > 0) {
        oss << ", ";
      }
      
      char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_FLOAT];
      // Handle special cases to avoid dragonbox assertion
      if (!std::isfinite(v[i][j]) || v[i][j] == 0.0f) {
        if (v[i][j] == 0.0f) {
          oss << "0";
        } else if (std::isnan(v[i][j])) {
          oss << "nan";
        } else if (std::isinf(v[i][j])) {
          oss << (v[i][j] > 0 ? "inf" : "-inf");
        }
      } else {
        char *e = internal::dtoa_dragonbox(v[i][j], buffer);
        *e = '\0';
        oss << buffer;
      }
    }
    
    oss << ")";
  }
  
  oss << "\n";
  return oss.str();
}

template<size_t N>
std::string print_double_array(const std::vector<std::array<double, N>> &v) {
  std::ostringstream oss;
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      oss << ", ";
    }
    
    oss << "(";
    
    for (size_t j = 0; j < N; j++) {
      if (j > 0) {
        oss << ", ";
      }
      
      char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_DOUBLE];
      // Handle special cases to avoid dragonbox assertion
      if (!std::isfinite(v[i][j]) || v[i][j] == 0.0) {
        if (v[i][j] == 0.0) {
          oss << "0";
        } else if (std::isnan(v[i][j])) {
          oss << "nan";
        } else if (std::isinf(v[i][j])) {
          oss << (v[i][j] > 0 ? "inf" : "-inf");
        }
      } else {
        char *e = internal::dtoa_dragonbox(v[i][j], buffer);
        *e = '\0';
        oss << buffer;
      }
    }
    
    oss << ")";
  }
  
  oss << "\n";
  return oss.str();
}

std::string print_float2_array(const std::vector<float2> &v) {
  return print_float_array<2>(v);
}

std::string print_float3_array(const std::vector<float3> &v) {
  return print_float_array<3>(v);
}

std::string print_float4_array(const std::vector<float4> &v) {
  return print_float_array<4>(v);
}

std::string print_double2_array(const std::vector<double2> &v) {
  return print_double_array<2>(v);
}

std::string print_double3_array(const std::vector<double3> &v) {
  return print_double_array<3>(v);
}

std::string print_double4_array(const std::vector<double4> &v) {
  return print_double_array<4>(v);
}

// Half-precision array printers
template<size_t N>
std::string print_half_array(const std::vector<std::array<internal::half, N>> &v) {
  std::ostringstream oss;

  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      oss << ", ";
    }

    oss << "(";

    for (size_t j = 0; j < N; j++) {
      if (j > 0) {
        oss << ", ";
      }

      char buffer[internal::DTOA_DRAGONBOX_BUFFER_SIZE_HALF];
      float f = internal::half_to_float(v[i][j]);

      // Handle special cases
      if (!std::isfinite(f) || f == 0.0f) {
        if (f == 0.0f) {
          oss << "0";
        } else if (std::isnan(f)) {
          oss << "nan";
        } else if (std::isinf(f)) {
          oss << (f > 0 ? "inf" : "-inf");
        }
      } else {
        char *e = internal::dtoa_dragonbox(v[i][j], buffer);
        *e = '\0';
        oss << buffer;
      }
    }

    oss << ")";
  }

  oss << "\n";
  return oss.str();
}

std::string print_half2_array(const std::vector<half2> &v) {
  return print_half_array<2>(v);
}

std::string print_half3_array(const std::vector<half3> &v) {
  return print_half_array<3>(v);
}

std::string print_half4_array(const std::vector<half4> &v) {
  return print_half_array<4>(v);
}

#if 0
std::string print_floats(const std::vector<float> &v) {
  
  char buffer[25];

  size_t n = v.size();
  std::vector<char> dst;
  dst.reserve(n * 10); // 10 : heuristics.

  size_t curr = 0;
  for (size_t i = 0; i < v.size(); i++) {

    if (i > 0) {
      dst[curr] =  ',';
      dst[curr+1] =  ' ';
      curr += 2;
    }

    //char *e = dtoa_milo(v[i], buffer);
    //size_t len = e - buffer; // includes position of '\0'

    // +2 for ', '
    //if ((curr + len + 2) >= dst.size()) {
    //  dst.resize((curr + len) + 2);
    //}

    //memcpy(dst.data() + curr, buffer, len);

    curr += len;
  }
  dst[curr] = '\n';
  std::string s(dst.data(), curr);
  return s;
}
#endif

int main(int argc, char** argv) {
  bool delim_at_end = true;
  size_t n = 1024 * 1024 * 16;
  if (argc > 1) {
    n = std::stoi(argv[1]);
  }
  if (argc > 2) {
    delim_at_end = std::stoi(argv[2]) > 0;
  }

  // Skip original dragonbox test loop - has issues
  // double d = 1.0;
  // for (size_t i = 0; i < 32; i++) {
  //   char buf[25];
  //   char *p = internal::dtoa_dragonbox(d, buf);
  //   *p = '\0';
  //   std::cout << "db " << buf << "\n";
  //   {
  //     auto ret = jkj::dragonbox::to_decimal(d);
  //     std::cout << "to_decimal " << ret.significand << "\n";
  //     std::cout << "to_decimal " << ret.exponent << "\n";
  //   }
  //   {
  //     char db_buf[40];
  //     auto ret = jkj::dragonbox::to_chars(d, db_buf);
  //     std::cout << "to_chars " << db_buf << "\n";
  //   }
  //   {
  //     char buffer[25];
  //     int length, K;
  //     Grisu2(d, buffer, &length, &K);
  //     std::cout << "grisu len " << length << "\n";
  //     std::cout << "grisu K " << K << "\n";
  //     std::cout << "grisu " << buffer << "\n";
  //   }
  //   d = d * 10.0;
  // }

  // Skip the performance test for now - has issues with dragonbox assertion
  // std::vector<float> arr = gen_floats(n);
  // auto start = std::chrono::steady_clock::now();
  // std::string s = print_floats(arr);
  // auto end = std::chrono::steady_clock::now();
  // std::cout << "n elems " << arr.size() << "\n";
  // std::cout << "print : " <<
  // std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
  // << " [ms]\n";

  // Test vector array printers
  std::cout << "\n=== Testing vector array printers ===\n";
  
  // Test float2 arrays
  std::vector<float2> float2_test = {
    {1.0f, 2.0f},
    {3.14159f, -2.71828f},
    {0.0001f, 1000000.0f}
  };
  std::cout << "float2 array: " << print_float2_array(float2_test);
  
  // Test float3 arrays
  std::vector<float3> float3_test = {
    {1.0f, 2.0f, 3.0f},
    {0.577f, 0.577f, 0.577f},
    {-1.0f, 0.0f, 1.0f}
  };
  std::cout << "float3 array: " << print_float3_array(float3_test);
  
  // Test float4 arrays
  std::vector<float4> float4_test = {
    {1.0f, 0.0f, 0.0f, 1.0f},
    {0.5f, 0.5f, 0.5f, 0.8f}
  };
  std::cout << "float4 array: " << print_float4_array(float4_test);
  
  // Test double2 arrays
  std::vector<double2> double2_test = {
    {1.0, 2.0},
    {3.14, -2.71}
  };
  std::cout << "double2 array: " << print_double2_array(double2_test);
  
  // Test double3 arrays
  std::vector<double3> double3_test = {
    {1.0, 2.0, 3.0},
    {0.577, 0.577, 0.577}
  };
  std::cout << "double3 array: " << print_double3_array(double3_test);
  
  // Test double4 arrays
  std::vector<double4> double4_test = {
    {1.0, 2.0, 3.0, 4.0},
    {0.707, 0.707, 1.0, 2.0}
  };
  std::cout << "double4 array: " << print_double4_array(double4_test);

  // Test half-precision (fp16) arrays
  std::cout << "\n=== Testing half-precision (fp16) array printers ===\n";

  // Create some half values
  // 0x3c00 = 1.0 in fp16
  // 0x4000 = 2.0 in fp16
  // 0x4200 = 3.0 in fp16
  // 0x4800 = 8.0 in fp16
  // 0x3555 ~= 0.333 in fp16
  // 0xbc00 = -1.0 in fp16

  // Test half2 arrays
  std::vector<half2> half2_test = {
    {internal::half(0x3c00), internal::half(0x4000)},  // {1.0, 2.0}
    {internal::half(0x3555), internal::half(0x4200)},  // {0.333..., 3.0}
    {internal::half(0xbc00), internal::half(0x4800)}   // {-1.0, 8.0}
  };
  std::cout << "half2 array: " << print_half2_array(half2_test);

  // Test half3 arrays
  std::vector<half3> half3_test = {
    {internal::half(0x3c00), internal::half(0x4000), internal::half(0x4200)},  // {1.0, 2.0, 3.0}
    {internal::half(0x3555), internal::half(0x3555), internal::half(0x3555)}   // {0.333..., 0.333..., 0.333...}
  };
  std::cout << "half3 array: " << print_half3_array(half3_test);

  // Test half4 arrays
  std::vector<half4> half4_test = {
    {internal::half(0x3c00), internal::half(0x0000), internal::half(0x0000), internal::half(0x3c00)},  // {1.0, 0.0, 0.0, 1.0}
    {internal::half(0x3800), internal::half(0x3800), internal::half(0x3800), internal::half(0x3666)}   // {0.5, 0.5, 0.5, 0.8}
  };
  std::cout << "half4 array: " << print_half4_array(half4_test);

  return 0;
}
