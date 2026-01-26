#ifndef PARSE_FP16_HH_
#define PARSE_FP16_HH_

#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <string>

namespace fp16 {

// IEEE 754 half-precision float (fp16) format:
// - 1 sign bit
// - 5 exponent bits (bias = 15)
// - 10 mantissa bits
// Total: 16 bits

// Helper union for bit manipulation
union fp16_bits {
  uint16_t u;
  struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    uint16_t sign : 1;
    uint16_t exponent : 5;
    uint16_t mantissa : 10;
#else
    uint16_t mantissa : 10;
    uint16_t exponent : 5;
    uint16_t sign : 1;
#endif
  } parts;
};

// Convert float (fp32) to half-float (fp16)
inline uint16_t fp32_to_fp16(float value) {
  union {
    float f;
    uint32_t u;
  } f32;

  f32.f = value;

  uint32_t sign = (f32.u >> 31) & 0x1;
  uint32_t exponent = (f32.u >> 23) & 0xFF;
  uint32_t mantissa = f32.u & 0x7FFFFF;

  fp16_bits result;
  result.u = 0;

  // Handle special cases
  if (exponent == 0xFF) {
    // Infinity or NaN
    result.parts.sign = sign;
    result.parts.exponent = 0x1F;
    result.parts.mantissa = (mantissa != 0) ? (mantissa >> 13) | 0x200 : 0; // Preserve NaN payload
    return result.u;
  }

  // Convert exponent from fp32 (bias 127) to fp16 (bias 15)
  int32_t exp16 = static_cast<int32_t>(exponent) - 127 + 15;

  // Handle overflow (too large for fp16)
  if (exp16 >= 0x1F) {
    // Return infinity
    result.parts.sign = sign;
    result.parts.exponent = 0x1F;
    result.parts.mantissa = 0;
    return result.u;
  }

  // Handle underflow and denormals
  if (exp16 <= 0) {
    if (exp16 < -10) {
      // Too small, flush to zero
      result.parts.sign = sign;
      result.parts.exponent = 0;
      result.parts.mantissa = 0;
      return result.u;
    }

    // Denormalized number
    mantissa = mantissa | 0x800000; // Add implicit 1
    uint32_t shift = 1 - exp16;
    mantissa >>= (13 + shift);

    result.parts.sign = sign;
    result.parts.exponent = 0;
    result.parts.mantissa = mantissa;

    return result.u;
  }

  // Normal number
  result.parts.sign = sign;
  result.parts.exponent = exp16;
  result.parts.mantissa = mantissa >> 13; // Keep top 10 bits of mantissa

  // Round to nearest even
  uint32_t round_bit = (mantissa >> 12) & 0x1;
  uint32_t sticky_bits = mantissa & 0xFFF;

  if (round_bit && (sticky_bits || (result.parts.mantissa & 0x1))) {
    result.u++;
    // Check for overflow after rounding
    if (result.parts.exponent == 0x1F && result.parts.mantissa == 0) {
      // Rounded to infinity
      result.parts.mantissa = 0;
    }
  }

  return result.u;
}

// Convert half-float (fp16) to float (fp32)
inline float fp16_to_fp32(uint16_t value) {
  fp16_bits fp16;
  fp16.u = value;

  union {
    float f;
    uint32_t u;
  } f32;

  uint32_t sign = fp16.parts.sign;
  uint32_t exponent = fp16.parts.exponent;
  uint32_t mantissa = fp16.parts.mantissa;

  if (exponent == 0) {
    if (mantissa == 0) {
      // Zero
      f32.u = (sign << 31);
      return f32.f;
    } else {
      // Denormalized number
      // Normalize it
      int e = -1;
      uint32_t m = mantissa;
      while ((m & 0x400) == 0) {
        m <<= 1;
        e--;
      }
      m &= 0x3FF; // Remove implicit 1
      exponent = 127 + 15 + e;
      mantissa = m;
    }
  } else if (exponent == 0x1F) {
    // Infinity or NaN
    f32.u = (sign << 31) | (0xFF << 23) | (mantissa << 13);
    return f32.f;
  } else {
    // Normal number
    exponent = exponent - 15 + 127;
  }

  f32.u = (sign << 31) | (exponent << 23) | (mantissa << 13);
  return f32.f;
}

// Parse result structure
struct parse_result {
  uint16_t value;     // The parsed fp16 value
  const char* ptr;    // Pointer to character after the parsed number
  bool success;       // Whether parsing succeeded

  parse_result() : value(0), ptr(nullptr), success(false) {}
  parse_result(uint16_t v, const char* p) : value(v), ptr(p), success(true) {}
};

// Parse string to fp16
// Returns parse_result with success flag and parsed value
inline parse_result parse_fp16(const char* str, const char* end = nullptr) {
  if (!str) {
    return parse_result();
  }

  const char* p = str;
  const char* p_end = end ? end : str + strlen(str);

  // Skip leading whitespace
  while (p < p_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }

  if (p >= p_end) {
    return parse_result();
  }

  // Check for sign
  bool negative = false;
  if (*p == '-') {
    negative = true;
    p++;
  } else if (*p == '+') {
    p++;
  }

  if (p >= p_end) {
    return parse_result();
  }

  // Check for special values (inf, infinity, nan)
  if (p + 3 <= p_end && (p[0] == 'i' || p[0] == 'I') &&
      (p[1] == 'n' || p[1] == 'N') && (p[2] == 'f' || p[2] == 'F')) {

    // Check for "infinity"
    if (p + 8 <= p_end && (p[3] == 'i' || p[3] == 'I') &&
        (p[4] == 'n' || p[4] == 'N') && (p[5] == 'i' || p[5] == 'I') &&
        (p[6] == 't' || p[6] == 'T') && (p[7] == 'y' || p[7] == 'Y')) {
      p += 8;
    } else {
      p += 3;
    }

    // Infinity in fp16: exponent = 0x1F, mantissa = 0
    fp16_bits result;
    result.parts.sign = negative ? 1 : 0;
    result.parts.exponent = 0x1F;
    result.parts.mantissa = 0;

    return parse_result(result.u, p);
  }

  if (p + 3 <= p_end && (p[0] == 'n' || p[0] == 'N') &&
      (p[1] == 'a' || p[1] == 'A') && (p[2] == 'n' || p[2] == 'N')) {
    p += 3;

    // NaN in fp16: exponent = 0x1F, mantissa != 0
    fp16_bits result;
    result.parts.sign = negative ? 1 : 0;
    result.parts.exponent = 0x1F;
    result.parts.mantissa = 1; // Quiet NaN

    return parse_result(result.u, p);
  }

  // Parse numeric value
  // For simplicity and correctness, parse as double then convert to fp16
  double value = 0.0;
  bool has_digits = false;
  bool has_decimal_point = false;
  int decimal_places = 0;

  // Parse integer part
  while (p < p_end && *p >= '0' && *p <= '9') {
    value = value * 10.0 + (*p - '0');
    has_digits = true;
    p++;
  }

  // Parse decimal part
  if (p < p_end && *p == '.') {
    has_decimal_point = true;
    p++;

    while (p < p_end && *p >= '0' && *p <= '9') {
      value = value * 10.0 + (*p - '0');
      decimal_places++;
      has_digits = true;
      p++;
    }
  }

  if (!has_digits) {
    return parse_result();
  }

  // Check for additional decimal points (invalid)
  const char* check_p = p;
  while (check_p < p_end && (*check_p == ' ' || *check_p == '\t')) {
    check_p++;
  }
  if (check_p < p_end && *check_p == '.' && has_decimal_point) {
    // Multiple decimal points found - invalid
    return parse_result();
  }

  // Use lookup table for common powers of 10
  static const double pow10_table[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10
  };

  // Apply decimal places
  if (decimal_places > 0) {
    if (decimal_places < 11) {
      value /= pow10_table[decimal_places];
    } else {
      value /= std::pow(10.0, decimal_places);
    }
  }

  // Parse exponent
  if (p < p_end && (*p == 'e' || *p == 'E')) {
    p++;

    bool exp_negative = false;
    if (p < p_end && *p == '-') {
      exp_negative = true;
      p++;
    } else if (p < p_end && *p == '+') {
      p++;
    }

    int exponent = 0;
    bool has_exp_digits = false;

    while (p < p_end && *p >= '0' && *p <= '9') {
      exponent = exponent * 10 + (*p - '0');
      has_exp_digits = true;
      p++;

      // Prevent overflow in exponent
      if (exponent > 10000) break;
    }

    // Exponent must have at least one digit
    if (!has_exp_digits) {
      return parse_result();
    }

    if (exp_negative) exponent = -exponent;

    if (std::abs(exponent) < 11) {
      if (exponent >= 0) {
        value *= pow10_table[exponent];
      } else {
        value /= pow10_table[-exponent];
      }
    } else {
      value *= std::pow(10.0, exponent);
    }
  }

  if (negative) {
    value = -value;
  }

  // Convert double to fp16
  float f32_value = static_cast<float>(value);
  uint16_t fp16_value = fp32_to_fp16(f32_value);

  return parse_result(fp16_value, p);
}

// Convenience function that returns just the fp16 value
// Returns 0 on parse failure
inline uint16_t parse_fp16_value(const char* str, const char* end = nullptr) {
  parse_result result = parse_fp16(str, end);
  return result.success ? result.value : 0;
}

// Convert fp16 to string representation
inline std::string fp16_to_string(uint16_t value) {
  float f32 = fp16_to_fp32(value);

  // Check for special values
  if (std::isnan(f32)) {
    return "nan";
  }
  if (std::isinf(f32)) {
    return f32 < 0 ? "-inf" : "inf";
  }

  // Use snprintf for formatting
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.7g", f32);
  return std::string(buffer);
}

} // namespace fp16

#endif // PARSE_FP16_HH_
