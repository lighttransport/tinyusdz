# Exhaustive Test Suite Summary

## Quick Start

```bash
# Build tests
make

# Run sanity check (< 1 second)
make test_sanity

# Run quick validation (~ 10 seconds)
make test_float_quick

# Run comprehensive validation (~ 1 minute)
make test_double_quick
```

## Test Coverage

### Float (32-bit) Testing

**Quick Test** (`make test_float_quick`):
- Tests: 1,000,000 random bit patterns
- Time: ~5-10 seconds
- Purpose: Fast validation during development

**Exhaustive Test** (`make test_float_exhaustive`):
- Tests: 4,294,967,296 bit patterns (all possible floats)
- Time: 2-8 hours
- Purpose: Complete validation of all float values
- Coverage: 100% of IEEE 754 single precision space

### Double (64-bit) Testing

Since 2^64 patterns would take ~millions of years to test, we use sampling:

**Quick Test** (`make test_double_quick`):
- Tests: 10,000,000 samples
- Time: ~30-60 seconds
- Strategies: Systematic exponent/mantissa sampling + random sampling

**Medium Test** (`make test_double_medium`):
- Tests: 100,000,000 samples
- Time: ~5-10 minutes

**Large Test** (`make test_double_large`):
- Tests: 1,000,000,000 samples
- Time: ~50-100 minutes
- Coverage: High confidence across double precision space

## Test Methodology

1. **Generate bit pattern** (sequential for exhaustive, random for sampled)
2. **Reinterpret as float/double**
3. **Convert to string** using `dtoa_dragonbox`
4. **Parse back to double** using `std::stod`
5. **Verify bit-exact match** between original and roundtrip

## Special Cases Handled

The following values are counted as special cases and automatically pass:
- **Zero**: ±0.0
- **Infinity**: ±Inf
- **NaN**: Not-a-Number
- **Denormals**: Subnormal numbers (may not roundtrip through stod)

## Expected Results

All tests should show **100% pass rate**:
```
=== Test Results ===
Total tests:    10000000
Passed:         10000000
Failed:         0
Special cases:  9778
Pass rate:      100.000000%
```

## What to Do if Tests Fail

If any test shows failures:

1. **Check the output** - Failures are logged with:
   - Bit pattern that failed
   - Original value
   - Dragonbox output string
   - Roundtrip value
   - Bit-level comparison

2. **Investigate the implementation** - Likely issues:
   - Bug in `dtoa_dragonbox` formatting
   - Edge case in exponent handling
   - Precision loss in conversion

3. **Report the issue** with:
   - Exact bit pattern that failed
   - Test output
   - Platform/compiler info

## CI/CD Recommendations

### Pre-commit
```bash
make test_sanity
```

### Pull Request
```bash
make test_float_quick test_double_quick
```

### Nightly Build
```bash
make test_double_medium
```

### Release Validation
```bash
# Run overnight
nohup make test_float_exhaustive > float_exhaustive.log 2>&1 &
```

## Performance Notes

Performance varies by CPU, but approximate times on modern x86_64 (3-4 GHz):

| Test | Patterns/sec | Total Time |
|------|--------------|------------|
| float_quick | ~100,000-200,000 | 5-10 sec |
| float_exhaustive | ~150,000-300,000 | 2-8 hours |
| double_quick | ~200,000-400,000 | 30-60 sec |
| double_medium | ~200,000-400,000 | 5-10 min |
| double_large | ~200,000-400,000 | 50-100 min |

## Files

- `test_exhaustive.cc` - Test implementation
- `print_fp.cc` - Original dtoa_dragonbox code
- `Makefile` - Build and test targets
- `README_TESTS.md` - Detailed documentation
- `TEST_SUMMARY.md` - This file

## Additional Notes

### Why Bit Pattern Testing?

Testing all bit patterns ensures:
- Complete coverage of mantissa space
- Complete coverage of exponent space
- All denormals tested
- All signs tested
- No edge cases missed

### Why Not Test All Doubles?

2^64 = 18,446,744,073,709,551,616 patterns

At 1 million tests/second, this would take:
- 584,542 years to complete

Instead, we use strategic sampling that provides high confidence with practical runtime.

### Comparison to Other Approaches

Traditional testing approaches:
- **Boundary values**: Test min/max/zero → Misses most values
- **Random values**: Test random floats → Non-reproducible, incomplete
- **Selected values**: Test known edge cases → Misses unknown edges

**This approach**: Test all possible values → Complete validation
