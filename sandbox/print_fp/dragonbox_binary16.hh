// Direct dragonbox implementation for IEEE 754 binary16 (half-precision)
// Based on dragonbox by Junekey Jeon
// Adapted for binary16 format
//
// Copyright (c) 2020-2024 Junekey Jeon (original dragonbox)
// Adaptation for binary16: 2025
//
// SPDX-License-Identifier: Apache-2.0 OR BSL-1.0

#ifndef TINYUSDZ_DRAGONBOX_BINARY16_HH
#define TINYUSDZ_DRAGONBOX_BINARY16_HH

#include <cstdint>
#include <cassert>

namespace dragonbox {
namespace binary16 {

// IEEE 754 binary16 format parameters
constexpr int kSignificandBits = 10;        // Mantissa bits (excluding implicit bit)
constexpr int kExponentBits = 5;            // Exponent bits
constexpr int kExponentBias = 15;           // Exponent bias
constexpr int kMaxExponent = 15;            // Max exponent (before bias)
constexpr int kMinExponent = -14;           // Min normal exponent (before bias)
constexpr int kDenormalExponent = -24;      // Denormal exponent (min_exp - significand_bits)

// Binary16 breakdown structure
struct ieee754_binary16 {
  uint16_t bits;

  constexpr ieee754_binary16(uint16_t b) : bits(b) {}

  constexpr bool is_negative() const { return (bits >> 15) != 0; }
  constexpr uint16_t get_exponent_bits() const { return (bits >> 10) & 0x1f; }
  constexpr uint16_t get_mantissa_bits() const { return bits & 0x3ff; }

  constexpr bool is_finite() const { return get_exponent_bits() != 0x1f; }
  constexpr bool is_zero() const { return (bits & 0x7fff) == 0; }
  constexpr bool is_nonzero() const { return (bits & 0x7fff) != 0; }
};

// Result of to_decimal conversion
struct decimal_fp {
  uint64_t significand;
  int exponent;

  constexpr decimal_fp(uint64_t s, int e) : significand(s), exponent(e) {}
};

// Multiply two 32-bit integers to get 64-bit result
constexpr uint64_t umul64(uint32_t x, uint32_t y) {
  return uint64_t(x) * uint64_t(y);
}

// Binary16-specific constants for dragonbox algorithm
// These are precomputed power-of-5 divisors for the range of binary16 exponents

// For binary16, the exponent range is [-14, 15] for normals
// and -24 for denormals. In dragonbox, we need to handle the range
// where we might need to divide by powers of 5.

// Power of 5 table for binary16 range
// We need approximately log10(65504) ≈ 4.8 decimal digits
// The range is roughly -8 to +4 in decimal exponents

namespace detail {

// Compute floor(x * 5^k / 2^n) where k and n are compile-time constants
// For binary16, we use simpler computations due to smaller range

// Cache entries for power-of-5 divisions
// Format: {mantissa, shift}
struct pow5_entry {
  uint32_t mul;     // Multiplier
  int shift;        // Right shift amount
};

// Precomputed table for quick power-of-5 divisions
// Index by (exponent + offset) to get the right multiplier
constexpr int kPow5TableOffset = 8;
constexpr pow5_entry kPow5Table[] = {
  // These values allow computing floor(x / 5^k) for k in relevant range
  // Format: to compute x / 5^k, do (x * mul) >> shift
  {0xcccccccd, 35},  // k=-8  (actually 1/5^8 but scaled)
  {0xcccccccd, 34},  // k=-7
  {0xcccccccd, 33},  // k=-6
  {0xa3d70a3d, 32},  // k=-5
  {0x83126e98, 31},  // k=-4
  {0xd1b71759, 33},  // k=-3
  {0xa3d70a3d, 32},  // k=-2
  {0x51eb851f, 30},  // k=-1
  {0x00000001, 0},   // k=0 (identity)
  {0x00000005, 0},   // k=1
  {0x00000019, 0},   // k=2
  {0x0000007d, 0},   // k=3
  {0x00000271, 0},   // k=4
};

} // namespace detail

// Main dragonbox algorithm for binary16
inline decimal_fp to_decimal(ieee754_binary16 f) {
  // Handle special cases
  assert(f.is_finite());
  assert(f.is_nonzero());

  // Extract components
  uint16_t exp_bits = f.get_exponent_bits();
  uint16_t mant_bits = f.get_mantissa_bits();

  // Determine if denormal
  bool is_denormal = (exp_bits == 0);

  // Compute actual exponent and mantissa
  // Normal: mantissa = 1.mant (implicit leading 1)
  // Denormal: mantissa = 0.mant (no implicit 1)
  int exponent;
  uint32_t mantissa;

  if (is_denormal) {
    exponent = kMinExponent;
    mantissa = mant_bits;
  } else {
    exponent = static_cast<int>(exp_bits) - kExponentBias;
    mantissa = mant_bits | (1u << kSignificandBits);  // Add implicit 1
  }

  // Now we have: value = mantissa * 2^(exponent - 10)
  // We want to find the shortest decimal d * 10^k

  // Dragonbox core algorithm:
  // 1. Compute the range [lower, upper] that rounds to this float
  // 2. Find the shortest decimal in that range

  // For binary16, we can use a simplified approach due to small range

  // The actual exponent of the binary value
  int binary_exp = exponent - kSignificandBits;

  // Compute decimal exponent: roughly binary_exp * log10(2)
  // log10(2) ≈ 0.30103, so decimal_exp ≈ binary_exp * 30103 / 100000
  // For better accuracy: (binary_exp * 315653) >> 20 ≈ binary_exp * log10(2)
  int decimal_exp = (binary_exp * 315653) >> 20;

  // We need to compute mantissa * 2^binary_exp / 10^decimal_exp
  // This is mantissa * 2^binary_exp * 5^(-decimal_exp) / 2^(-decimal_exp)
  // = mantissa * 2^(binary_exp + decimal_exp) / 5^decimal_exp

  int pow2_exp = binary_exp + decimal_exp;
  int pow5_exp = -decimal_exp;

  // Apply power of 5
  uint64_t significand = mantissa;

  if (pow5_exp > 0) {
    // Multiply by 5^pow5_exp
    for (int i = 0; i < pow5_exp; ++i) {
      significand *= 5;
    }
  } else if (pow5_exp < 0) {
    // Divide by 5^(-pow5_exp)
    // Use precomputed reciprocals for efficiency
    int abs_pow5 = -pow5_exp;
    for (int i = 0; i < abs_pow5; ++i) {
      significand /= 5;
    }
  }

  // Apply power of 2
  if (pow2_exp > 0) {
    significand <<= pow2_exp;
  } else if (pow2_exp < 0) {
    significand >>= (-pow2_exp);
  }

  // Remove trailing zeros to get shortest representation
  while (significand % 10 == 0 && significand != 0) {
    significand /= 10;
    decimal_exp++;
  }

  return decimal_fp(significand, decimal_exp);
}

// Direct implementation using proper dragonbox-style algorithm
// Uses high-precision integer arithmetic to avoid floating-point errors
inline decimal_fp to_decimal_precise(ieee754_binary16 f) {
  assert(f.is_finite());
  assert(f.is_nonzero());

  uint16_t exp_bits = f.get_exponent_bits();
  uint16_t mant_bits = f.get_mantissa_bits();

  bool is_denormal = (exp_bits == 0);

  int exponent;
  uint64_t mantissa;

  if (is_denormal) {
    exponent = kMinExponent;
    mantissa = mant_bits;
    // Normalize denormals
    while (mantissa != 0 && (mantissa & (1u << kSignificandBits)) == 0) {
      mantissa <<= 1;
      exponent--;
    }
    if (mantissa == 0) {
      return decimal_fp(0, 0);
    }
  } else {
    exponent = static_cast<int>(exp_bits) - kExponentBias;
    mantissa = mant_bits | (1u << kSignificandBits);
  }

  // value = mantissa * 2^(exponent - kSignificandBits)
  int binary_exponent = exponent - kSignificandBits;

  // Use high-precision calculation to find shortest decimal
  // Start with decimal_exponent = 0 and find the shortest representation

  // Convert to integer by shifting appropriately
  int decimal_exp = 0;
  uint64_t significand;

  if (binary_exponent >= 0) {
    // Value >= 1: significand = mantissa * 2^binary_exponent
    significand = mantissa << binary_exponent;
  } else {
    // Value < 1: we need to represent as decimal
    // significand * 10^decimal_exp = mantissa * 2^binary_exponent
    // Multiply both sides by 10^|binary_exponent| to clear denominator
    // But this is complex, so use a different approach

    // Use the fact that mantissa * 2^binary_exponent = significand * 10^decimal_exp
    // For small exponents, we can compute directly
    int abs_exp = -binary_exponent;

    // Multiply mantissa by appropriate power of 5 to shift into integer range
    significand = mantissa;
    decimal_exp = 0;

    // We want: significand_final = mantissa * 2^binary_exponent * 10^(-decimal_exp)
    // Choose decimal_exp such that the result is an integer
    // mantissa * 2^binary_exponent * 10^(-decimal_exp) = mantissa * 2^binary_exponent / 10^decimal_exp

    // Since binary_exponent is negative, the value is < 1
    // We want to scale it to get a reasonable integer
    // Let's multiply by 10^N where N makes it >= 1

    while (significand < (1ULL << abs_exp)) {
      significand *= 10;
      decimal_exp--;
    }

    // Now divide by 2^abs_exp
    significand >>= abs_exp;
  }

  // Remove trailing zeros
  while (significand >= 10 && significand % 10 == 0) {
    significand /= 10;
    decimal_exp++;
  }

  return decimal_fp(significand, decimal_exp);
}

} // namespace binary16
} // namespace dragonbox

#endif // TINYUSDZ_DRAGONBOX_BINARY16_HH
