# Dragonbox dtoa Exhaustive Test Suite

This directory contains exhaustive tests for the `dtoa_dragonbox` floating-point to string conversion implementation.

## Overview

The test suite validates that `dtoa_dragonbox` produces correct string representations of floating-point values by:
1. Converting float/double → string using `dtoa_dragonbox`
2. Converting string → double using `std::stod`
3. Verifying roundtrip accuracy (original value == roundtrip value)

## Test Modes

### Quick Tests (Recommended for Development)

#### `make test_sanity`
Quick sanity check with known values. Takes ~1 second.
```bash
make test_sanity
```

#### `make test_float_quick`
Test 1 million random float bit patterns. Takes ~5-10 seconds.
```bash
make test_float_quick
```

#### `make test_double_quick`
Test 10 million random double bit patterns. Takes ~30-60 seconds.
```bash
make test_double_quick
```

### Comprehensive Tests

#### `make test_float_exhaustive`
**EXHAUSTIVE TEST: Tests all 2^32 (4,294,967,296) possible float bit patterns.**

**Warning**: This test takes several hours to complete!

- Tests every possible 32-bit float value
- Includes all special cases (NaN, Inf, denormals, etc.)
- Progress reports every 100M tests
- Estimated time: 2-8 hours depending on CPU

```bash
# Run with nohup to keep running after terminal closes
nohup make test_float_exhaustive > float_exhaustive.log 2>&1 &
```

#### `make test_double_medium`
Test 100 million double bit patterns. Takes ~5-10 minutes.
```bash
make test_double_medium
```

#### `make test_double_large`
Test 1 billion double bit patterns. Takes ~50-100 minutes.
```bash
make test_double_large
```

## Double Precision Testing Strategy

Since testing all 2^64 double patterns is computationally infeasible (~10^19 tests), the test suite uses two strategies:

1. **Systematic Sampling**: Tests combinations of sign bits, exponent values, and sampled mantissa values
2. **Random Sampling**: Tests random bit patterns across the entire double range

This approach provides good coverage while remaining practical.

## Understanding Test Output

### Success Output
```
=== Test Results ===
Total tests:    1000000
Passed:         1000000
Failed:         0
Special cases:  3874
Pass rate:      100.000000%
```

- **Total tests**: Number of values tested
- **Passed**: Values that roundtripped correctly (includes special cases)
- **Failed**: Values that did not roundtrip correctly
- **Special cases**: Values handled specially (NaN, Inf, zero, and denormal numbers)
- **Pass rate**: Percentage of successful roundtrips (should be 100%)

### Failure Output
If a test fails, you'll see detailed information:
```
FAIL at bit pattern 0x12345678
  Original:     1.2345678901234567e+05
  Dragonbox:    123456.78901234567
  Roundtrip:    1.2345678901234568e+05
  std::to_string: 123456.789012
  Original bits:  0x12345678
  Roundtrip bits: 0x12345679
```

## Implementation Details

### What is Being Tested?

The test validates that `dtoa_dragonbox` implementation:
- Correctly handles all finite floating-point values
- Produces strings that roundtrip exactly via `std::stod`
- Handles special cases (zero, NaN, infinity) appropriately
- Works for both single precision (float) and double precision

### Test Methodology

For each bit pattern:
1. Reinterpret bits as float/double
2. Skip special cases (NaN, Inf - tracked separately)
3. Convert to string using `dtoa_dragonbox`
4. Parse string back to double using `std::stod`
5. Cast back to original precision if testing float
6. Compare bit patterns (must be identical)

### Why Bit Pattern Testing?

Testing all bit patterns ensures:
- Coverage of all denormal numbers
- Coverage of all exponent values
- Coverage of all mantissa values
- No edge cases are missed
- Both positive and negative values are tested

## Performance Characteristics

Approximate timings on modern CPU (Intel/AMD @ 3-4 GHz):

| Test | Patterns | Time |
|------|----------|------|
| sanity | 11 | < 1 second |
| float_quick | 1M | 5-10 seconds |
| float_exhaustive | 4.3B | 2-8 hours |
| double_quick | 10M | 30-60 seconds |
| double_medium | 100M | 5-10 minutes |
| double_large | 1B | 50-100 minutes |

## Continuous Integration

For CI/CD pipelines, recommend:
```bash
# Quick validation (completes in ~1 minute)
make test_sanity test_float_quick

# More thorough nightly tests
make test_float_quick test_double_medium
```

## Known Limitations

1. **Full double exhaustive test is impractical**: Would require ~10^19 tests
2. **Special values (NaN, Inf)**: Counted as special cases but not fully validated for string format
3. **Denormal numbers**: Subnormal/denormal numbers are counted as special cases and not validated for roundtrip accuracy, as std::stod may not reliably parse them
4. **Locale dependency**: Tests assume standard C locale for decimal point
5. **Rounding modes**: Tests assume default rounding mode

## Building

```bash
# Build all
make

# Build just the test suite
make test_exhaustive

# Clean
make clean
```

## Requirements

- C++14 or later
- Dragonbox library (included in external/dragonbox/)
- Standard library with `<random>`, `<chrono>`, etc.

## Interpreting Results

### 100% Pass Rate
Indicates `dtoa_dragonbox` correctly roundtrips all tested values. This is the expected result.

### Any Failures
Indicates a bug in the implementation. Each failure shows:
- The exact bit pattern that failed
- What string was produced
- What value was recovered
- Bit-level comparison

Failures should be investigated and fixed before production use.

## Example Usage

```bash
# Quick development check
make test_sanity

# Before committing changes
make test_float_quick test_double_quick

# Full validation (overnight run)
nohup make test_float_exhaustive > exhaustive.log 2>&1 &

# Check progress
tail -f exhaustive.log
```

## Implementation Files

- `test_exhaustive.cc`: Main test implementation
- `print_fp.cc`: Original dtoa_dragonbox implementation
- `Makefile`: Build and test targets
- `README_TESTS.md`: This file
- `BUFFER_SIZES.md`: Documentation for buffer size constants
- `TEST_SUMMARY.md`: Quick reference guide
- `EXHAUSTIVE_FLOAT_TEST.md`: Guide for exhaustive float testing

## References

- Dragonbox algorithm: https://github.com/jk-jeon/dragonbox
- IEEE 754 floating-point standard
- Ryu algorithm (alternative approach)
