# FP16 (Half-Float) String Parser

Efficient implementation of string to IEEE 754 half-precision float (fp16) conversion.

## Overview

This implementation provides a complete solution for parsing strings into 16-bit half-precision floating-point values. The fp16 format is widely used in graphics programming, machine learning, and scientific computing where memory bandwidth and storage are critical.

## Features

- **Complete IEEE 754 Half-Precision Support**
  - 1 sign bit
  - 5 exponent bits (bias = 15)
  - 10 mantissa bits
  - Range: ±6.55×10⁴ (normalized), ±6.10×10⁻⁵ (smallest positive normal)
  - Precision: ~3-4 decimal digits

- **Full Feature Set**
  - Normal and denormalized numbers
  - Positive and negative infinity
  - NaN (Not-a-Number)
  - Signed zero
  - Proper rounding (round to nearest even)

- **String Parsing Features**
  - Decimal notation: `3.14159`, `-2.71828`
  - Scientific notation: `1.5e-3`, `2.0E+2`
  - Special values: `inf`, `-infinity`, `nan`
  - Leading whitespace handling
  - Sign handling (`+` and `-`)

- **Robust Error Handling**
  - Invalid input detection
  - Multiple decimal point detection
  - Incomplete exponent detection
  - Overflow to infinity
  - Underflow to zero or denormal

## Files

- `parse_fp16.hh` - Header-only implementation of fp16 parsing and conversion
- `test_parse_fp16.cc` - Comprehensive test suite with 1000+ test cases
- `Makefile` - Build configuration

## API Reference

### Core Functions

#### `parse_fp16(const char* str, const char* end = nullptr)`

Parse a string to fp16 format.

```cpp
fp16::parse_result result = fp16::parse_fp16("3.14159");
if (result.success) {
    uint16_t fp16_value = result.value;
    const char* end_ptr = result.ptr;  // Points to character after parsed number
}
```

**Parameters:**
- `str` - Pointer to null-terminated string or start of string
- `end` - Optional end pointer (uses strlen if nullptr)

**Returns:**
- `parse_result` structure containing:
  - `value` - The parsed fp16 value (uint16_t)
  - `ptr` - Pointer to character after parsed number
  - `success` - Boolean indicating parse success

#### `parse_fp16_value(const char* str, const char* end = nullptr)`

Convenience function that returns just the fp16 value.

```cpp
uint16_t value = fp16::parse_fp16_value("1.5");  // Returns 0 on failure
```

#### `fp32_to_fp16(float value)`

Convert a 32-bit float to 16-bit half-float.

```cpp
uint16_t half = fp16::fp32_to_fp16(3.14159f);
```

#### `fp16_to_fp32(uint16_t value)`

Convert a 16-bit half-float to 32-bit float.

```cpp
float full = fp16::fp16_to_fp32(half);
```

#### `fp16_to_string(uint16_t value)`

Convert fp16 to string representation.

```cpp
std::string str = fp16::fp16_to_string(half);
```

## Usage Examples

### Basic Parsing

```cpp
#include "parse_fp16.hh"

// Parse a simple number
auto result = fp16::parse_fp16("3.14159");
if (result.success) {
    float value = fp16::fp16_to_fp32(result.value);
    std::cout << "Parsed: " << value << std::endl;
}

// Parse with scientific notation
auto result2 = fp16::parse_fp16("1.5e-3");

// Parse special values
auto inf = fp16::parse_fp16("inf");
auto nan = fp16::parse_fp16("nan");
```

### Conversion Between Formats

```cpp
// Float to fp16 to string roundtrip
float original = 42.5f;
uint16_t fp16_val = fp16::fp32_to_fp16(original);
std::string str = fp16::fp16_to_string(fp16_val);

// Parse string back to fp16
auto parsed = fp16::parse_fp16(str.c_str());
float recovered = fp16::fp16_to_fp32(parsed.value);
```

### Batch Processing

```cpp
std::vector<std::string> numbers = {"1.0", "2.5", "3.14159", "-1.5e2"};
std::vector<uint16_t> fp16_values;

for (const auto& num_str : numbers) {
    auto result = fp16::parse_fp16(num_str.c_str());
    if (result.success) {
        fp16_values.push_back(result.value);
    }
}
```

## Building and Testing

### Build Tests

```bash
make test_parse_fp16
```

### Run Tests

```bash
./test_parse_fp16
```

or

```bash
make test
```

### Clean

```bash
make clean
```

## Test Coverage

The comprehensive test suite includes:

1. **Basic Parsing** (13 tests)
   - Zero, positive, negative numbers
   - Integer and decimal values
   - Small and large values

2. **Special Values** (9 tests)
   - Positive and negative infinity
   - NaN (quiet NaN)
   - Case-insensitive parsing

3. **Exponent Notation** (12 tests)
   - Positive and negative exponents
   - Large and small exponents
   - Mixed case 'e' and 'E'

4. **Overflow and Underflow** (9 tests)
   - Maximum representable value
   - Overflow to infinity
   - Underflow to zero
   - Denormal numbers

5. **Invalid Input** (8 tests)
   - Empty strings
   - Non-numeric input
   - Multiple decimal points
   - Incomplete numbers

6. **Whitespace Handling** (5 tests)
   - Leading spaces, tabs, newlines
   - Mixed whitespace

7. **Boundary Values** (21 tests)
   - Powers of 2
   - Smallest/largest normal numbers
   - Subnormal numbers

8. **Conversion Roundtrip** (1000 tests)
   - Random value testing
   - Format conversion integrity

## Performance Characteristics

- **Parsing Speed**: O(n) where n is the number of digits
- **Memory**: Zero heap allocations (stack-only)
- **Lookup Tables**: Uses pre-computed power-of-10 table for common exponents
- **Precision**: Full fp16 precision maintained
- **Rounding**: Round-to-nearest-even (banker's rounding)

## Implementation Details

### Rounding Strategy

The implementation uses "round to nearest, ties to even" (banker's rounding):
- If the value is exactly halfway between two representable values, round to the one with an even mantissa
- This minimizes cumulative rounding errors in repeated calculations

### Overflow/Underflow Handling

- **Overflow**: Values too large for fp16 are converted to ±infinity
- **Underflow**: Very small values are either:
  - Converted to denormal numbers (if within denormal range)
  - Flushed to zero (if smaller than smallest denormal)

### Special Value Encoding

- **Zero**: `0x0000` (positive), `0x8000` (negative)
- **Infinity**: `0x7C00` (positive), `0xFC00` (negative)
- **NaN**: `0x7C01` to `0x7FFF` (positive), `0xFC01` to `0xFFFF` (negative)

### Denormal Numbers

Denormal (subnormal) numbers allow gradual underflow:
- Exponent = 0, mantissa ≠ 0
- Range: ±5.96×10⁻⁸ to ±6.10×10⁻⁵
- Provides graceful degradation near zero

## IEEE 754 Half-Precision Format

```
Bit layout (16 bits total):
[S][EEEEE][MMMMMMMMMM]
 │    │         └─ 10-bit mantissa (fraction)
 │    └─────────── 5-bit exponent (bias = 15)
 └──────────────── 1-bit sign
```

### Value Calculation

For normalized numbers:
```
value = (-1)^sign × 2^(exponent - 15) × (1.mantissa)
```

For denormalized numbers (exponent = 0):
```
value = (-1)^sign × 2^(-14) × (0.mantissa)
```

## Limitations

- **Range**: Much smaller than fp32 (±65504 vs ±3.4×10³⁸)
- **Precision**: ~3-4 decimal digits vs ~7 for fp32
- **No intermediate precision**: Parsing goes through double precision internally, which may introduce slight rounding differences compared to native fp16 arithmetic

## Use Cases

- **Graphics Programming**: Color values, texture coordinates
- **Machine Learning**: Neural network weights and activations
- **Scientific Computing**: Memory-constrained simulations
- **Data Compression**: Reduced storage for floating-point arrays
- **USD/USDA Files**: Efficient storage of geometric and material data

## References

- IEEE 754-2008 Standard for Floating-Point Arithmetic
- [Wikipedia: Half-precision floating-point format](https://en.wikipedia.org/wiki/Half-precision_floating-point_format)

## License

Same as TinyUSDZ (Apache 2.0)
