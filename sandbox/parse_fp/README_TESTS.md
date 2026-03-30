# String to Float Parsing Test Suite

This directory contains a comprehensive test suite for string to floating-point conversion functions, using `std::from_chars` as the reference implementation.

## Test Programs

### 1. `test_edge_cases`
**Purpose**: Unit tests for specific edge cases and problematic patterns.

**Tests include**:
- Basic integers and decimals
- Scientific notation
- Special values (inf, -inf, nan)
- Float limits and subnormal numbers
- Hexadecimal floats
- Overflow/underflow behavior
- Invalid inputs
- Rounding edge cases

**Usage**:
```bash
./test_edge_cases
```

### 2. `test_parse_fp_comprehensive`
**Purpose**: Comprehensive test suite with configurable random testing.

**Features**:
- Edge case testing
- Random float generation and testing
- Optional exhaustive 32-bit float testing
- Detailed error reporting

**Usage**:
```bash
# Basic test with 10,000 random floats (default)
./test_parse_fp_comprehensive

# Test with 100,000 random floats
./test_parse_fp_comprehensive --random 100000

# Run exhaustive test (WARNING: takes hours!)
./test_parse_fp_comprehensive --exhaustive

# Specify thread count for exhaustive test
./test_parse_fp_comprehensive --exhaustive --threads 16
```

### 3. `test_exhaustive_float32`
**Purpose**: Exhaustive testing of all 2^32 possible 32-bit float bit patterns.

**Features**:
- Tests all ~4.3 billion float bit patterns
- Multithreaded for performance
- Progress reporting with ETA
- Detailed error analysis
- Quick mode for testing subset

**Usage**:
```bash
# Quick test (first 100M patterns, ~5 minutes)
./test_exhaustive_float32 --quick

# Full exhaustive test (WARNING: takes 3-8 hours!)
./test_exhaustive_float32

# Specify thread count
./test_exhaustive_float32 --threads 32

# Stop on first error
./test_exhaustive_float32 --stop-on-error
```

## Building

### Using the provided Makefile:
```bash
# Build all tests with optimization
make -f Makefile.test all

# Build with debug flags and address sanitizer
make -f Makefile.test debug

# Clean build artifacts
make -f Makefile.test clean
```

### Manual compilation:
```bash
# Requires C++17 or later for std::from_chars
g++ -std=c++17 -O3 -march=native -pthread -I../../src test_edge_cases.cc -o test_edge_cases
g++ -std=c++17 -O3 -march=native -pthread -I../../src test_exhaustive_float32.cc -o test_exhaustive_float32
```

## Running Tests

### Quick Test Suite
Run edge cases and random tests:
```bash
make -f Makefile.test test-quick
```

### Edge Cases Only
```bash
make -f Makefile.test test-edge
```

### Exhaustive Test (Quick Mode)
Tests first 100M patterns (~5 minutes):
```bash
make -f Makefile.test test-exhaustive-quick
```

### Full Exhaustive Test
**WARNING**: This takes 3-8 hours depending on CPU!
```bash
make -f Makefile.test test-exhaustive-full
```

### Using the Test Runner Script
Interactive test runner with colored output:
```bash
./test_runner.sh
```

## Test Coverage

The test suite covers:

1. **Edge Cases**:
   - Zero (0, -0, 0.0, -0.0)
   - Infinity (inf, -inf, infinity)
   - NaN (nan, NaN, NAN)
   - Maximum/minimum float values
   - Subnormal/denormal numbers
   - Leading zeros
   - Very long mantissas
   - Hexadecimal notation

2. **Scientific Notation**:
   - Positive/negative exponents
   - Capital/lowercase 'E'
   - Edge cases like "1e-45", "1e39"

3. **Rounding**:
   - Values exactly between representable floats
   - Ties-to-even rounding
   - Mantissa precision boundaries

4. **Invalid Inputs**:
   - Empty strings
   - Non-numeric strings
   - Malformed numbers (multiple dots, incomplete exponents)

5. **Exhaustive Testing**:
   - All 2^32 possible bit patterns
   - Verification of round-trip conversion
   - Bit-exact comparison with reference

## Performance

On a modern multi-core CPU:
- Edge cases test: < 1 second
- Random test (100K): ~1 second
- Exhaustive quick (100M): ~5 minutes with 16 threads
- Full exhaustive (4.3B): 3-8 hours with 16-32 threads

## Implementation Notes

### Custom Parser Function
The tests currently compare `fast_float::from_chars` against `std::from_chars`. To test your own implementation:

1. Replace the `parse_float_custom` function in the test files with your implementation
2. Ensure your function follows the same interface:
   ```cpp
   bool parse_float_custom(const char* begin, const char* end, float& value);
   ```
3. Return `true` on successful parse, `false` on failure

### Thread Safety
The exhaustive test uses thread-local storage and atomic counters for thread-safe operation. Each thread processes an independent range of bit patterns.

### Memory Usage
Tests are designed to be memory-efficient:
- Streaming approach for exhaustive testing
- Limited error storage (first 1000 errors by default)
- No large intermediate data structures

## Interpreting Results

### Pass Criteria
A test passes if:
1. Parse success/failure matches the reference
2. For successful parses, the resulting float value is bit-exact identical
3. Special values (inf, -inf, nan) are handled correctly

### Common Issues
- **Rounding differences**: Ensure your implementation follows IEEE-754 rounding rules
- **Subnormal handling**: Very small numbers require special handling
- **Overflow/underflow**: Should produce inf/0 appropriately
- **NaN handling**: Multiple bit patterns represent NaN

## Requirements

- C++17 or later (for std::from_chars with float support)
- fast_float library (included in TinyUSDZ)
- POSIX threads support
- At least 1GB RAM
- For full exhaustive test: 4+ CPU cores recommended

## License

Part of the TinyUSDZ project. See main project LICENSE for details.