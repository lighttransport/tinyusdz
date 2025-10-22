# Fast Path Optimization for 1.0 and -1.0

## Overview

Added a fast path optimization to `dtoa_dragonbox` for the common values `1.0` and `-1.0` using bitwise comparison instead of the full Dragonbox algorithm.

## Implementation

### Double Precision (print_fp.cc:207-222)

```cpp
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
```

### Single Precision (print_fp.cc:349-364)

```cpp
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
```

## Performance Results

Benchmark with 10 million iterations (test_fastpath_perf.cc):

- **WITH fast path**: 26 ms
- **WITHOUT fast path**: 605 ms
- **Speedup**: **23.27x faster**

## IEEE 754 Bit Patterns

### Double Precision (64-bit)
- `1.0` = `0x3FF0000000000000`
  - Sign: 0, Exponent: 1023 (biased), Mantissa: 0
- `-1.0` = `0xBFF0000000000000`
  - Sign: 1, Exponent: 1023 (biased), Mantissa: 0

### Single Precision (32-bit)
- `1.0f` = `0x3F800000`
  - Sign: 0, Exponent: 127 (biased), Mantissa: 0
- `-1.0f` = `0xBF800000`
  - Sign: 1, Exponent: 127 (biased), Mantissa: 0

## Why This Optimization Matters

1. **Common values**: 1.0 and -1.0 are extremely common in graphics, mathematics, and scientific computing (identity matrices, normalization, scaling factors, etc.)

2. **O(1) vs O(n) complexity**: Bitwise comparison is constant time, while Dragonbox involves:
   - Floating-point to decimal conversion
   - Digit counting
   - String formatting
   - Multiple conditional branches

3. **No precision loss**: Using exact bitwise comparison ensures we only trigger the fast path for the exact values, not approximations

## Testing

All existing tests pass with the fast path enabled:
- ✅ test_decimal: 64/64 tests pass
- ✅ test_shortest_and_ranges: 75/75 tests pass
- ✅ test_fastpath_perf: Correctness verified, 23x speedup measured

## Use Cases

This optimization is particularly beneficial for:
- 3D graphics (transformation matrices, identity matrices)
- Machine learning (weight initialization, normalization)
- Physics simulations (unity scaling factors)
- Any application that frequently serializes 1.0 or -1.0 values
