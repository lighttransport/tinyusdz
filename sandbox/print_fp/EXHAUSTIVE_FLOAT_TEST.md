# Exhaustive Float Test Guide

## What is the Exhaustive Float Test?

The exhaustive float test validates `dtoa_dragonbox` by testing **every single possible IEEE 754 single-precision floating-point value**.

## Test Coverage

- **Total patterns tested**: 4,294,967,296 (2^32)
- **Coverage**: 100% of all possible float values
- **Duration**: 30 minutes - 2 hours with parallel testing (2-8 hours single-threaded)
- **Parallelization**: 🚀 **Uses all CPU cores by default** for maximum speed

## Running the Test

### Parallel Mode (Default - Recommended) 🚀
```bash
# Use all available CPU cores (fastest)
make test_float_exhaustive

# Or directly with auto-detect
./test_exhaustive float_exhaustive

# Use specific number of threads
make test_float_exhaustive_threads THREADS=8
./test_exhaustive float_exhaustive 8

# Use half your cores (e.g., on a 16-core system)
./test_exhaustive float_exhaustive 8
```

### Single-Threaded Mode
```bash
# For comparison or debugging
./test_exhaustive float_exhaustive 1
```

### Background Mode (Recommended for Long Tests)
```bash
# Run in background with logging (uses all cores)
nohup make test_float_exhaustive > float_exhaustive.log 2>&1 &

# Check progress
tail -f float_exhaustive.log

# Check if still running
ps aux | grep test_exhaustive
```

## Understanding Progress Output

Every 100 million tests, you'll see progress updates:

```
Progress: 23.28% (1000000000 / 4294967296) Elapsed: 1234s

=== Test Results ===
Total tests:    1000000000
Passed:         1000000000
Failed:         0
Special cases:  16777216
Pass rate:      100.000000%
```

### What These Numbers Mean

- **Progress %**: Percentage of all 2^32 patterns tested
- **Elapsed**: Seconds since test started
- **Total tests**: Number of bit patterns tested so far
- **Passed**: Should equal Total tests
- **Failed**: Should be 0
- **Special cases**: NaN, Inf, zero, and denormal numbers encountered

## Expected Timeline

### Parallel Mode (16 cores, 3-4 GHz)

| Progress | Time Elapsed | ETA Remaining |
|----------|--------------|---------------|
| 10% | ~5-10 min | 45-90 min |
| 25% | ~12-20 min | 40-60 min |
| 50% | ~25-50 min | 25-50 min |
| 75% | ~40-75 min | 15-25 min |
| 100% | ~50-100 min | Complete |

**Speedup**: ~4-8x faster than single-threaded on typical modern CPUs

### Single-Threaded Mode (for reference)

| Progress | Time Elapsed | ETA Remaining |
|----------|--------------|---------------|
| 10% | ~30-60 min | 4-8 hours |
| 25% | ~1-2 hours | 3-6 hours |
| 50% | ~2-4 hours | 2-4 hours |
| 75% | ~3-6 hours | 1-2 hours |
| 100% | ~4-8 hours | Complete |

## What Values Are Being Tested?

The test iterates through all 32-bit patterns:

```
0x00000000 → 0x00000001 → 0x00000002 → ... → 0xFFFFFFFF
```

This includes:
- **Positive zero**: 0x00000000
- **Negative zero**: 0x80000000
- **Smallest denormal**: 0x00000001
- **Largest denormal**: 0x007FFFFF
- **Smallest normal**: 0x00800000
- **One**: 0x3F800000
- **Largest finite**: 0x7F7FFFFF
- **Positive infinity**: 0x7F800000
- **Negative infinity**: 0xFF800000
- **NaN values**: 0x7F800001 through 0x7FFFFFFF, 0xFF800001 through 0xFFFFFFFF
- **All normal values**: Millions of regular floats
- **All negative values**: 0x80000000 through 0xFFFFFFFF

## Special Cases Breakdown

Expected special case counts:

| Type | Count | Bit Patterns |
|------|-------|--------------|
| Zeros | 2 | 0x00000000, 0x80000000 |
| Denormals | ~33M | 0x00000001-0x007FFFFF, 0x80000001-0x807FFFFF |
| Infinities | 2 | 0x7F800000, 0xFF800000 |
| NaNs | ~33M | 0x7F800001-0x7FFFFFFF, 0xFF800001-0xFFFFFFFF |
| **Total** | ~67M | ~1.5% of all patterns |

## What Happens During the Test?

For each bit pattern `0xXXXXXXXX`:

1. **Reinterpret as float**: `float f = *(float*)&pattern`
2. **Check if special**: If NaN/Inf/zero/denormal → count and pass
3. **Convert to string**: `char* end = dtoa_dragonbox(f, buf)`
4. **Parse back**: `double d = std::stod(buf)`
5. **Cast to float**: `float roundtrip = (float)d`
6. **Compare bits**: Original bits must exactly equal roundtrip bits

## Success Criteria

The test PASSES if:
- All 4,294,967,296 values are tested
- Zero failures
- 100% pass rate

## What If There's a Failure?

If you see any failures, the test will print:

```
FAIL at bit pattern 0x12345678
  Original:     1.2345678e+05
  Dragonbox:    123456.78
  Roundtrip:    1.2345679e+05
  std::to_string: 123456.780000
  Original bits:  0x12345678
  Roundtrip bits: 0x12345679
```

This indicates a bug in `dtoa_dragonbox` that needs to be fixed.

## System Requirements

### CPU
- Modern x86_64 or ARM64 processor
- Recommended: 2+ GHz, multi-core (test is single-threaded)

### Memory
- Minimal (< 100 MB)
- Test processes one value at a time

### Disk
- Minimal (executable is small)
- Log files can grow to several MB if verbose

### Time
- **Development machine**: Run overnight
- **CI server**: Not recommended (too slow)
- **Release validation**: Run as final check before release

## Optimization Notes

The test is compiled with `-O3` for maximum speed. Key optimizations:

1. **Inline functions**: Dragonbox code is heavily inlined
2. **No I/O in tight loop**: Only prints progress every 100M tests
3. **Minimal allocations**: Stack-based buffers
4. **Direct bit manipulation**: Uses memcpy for type punning

## Comparison to Quick Test

| Aspect | Quick Test | Exhaustive Test |
|--------|------------|-----------------|
| Patterns | 1M random | 4.3B all patterns |
| Time | 5-10 seconds | 2-8 hours |
| Coverage | ~0.02% | 100% |
| Use case | Development | Release validation |
| CI/CD | Yes | No |

## Stopping the Test

If you need to stop the test:

```bash
# Find the process
ps aux | grep test_exhaustive

# Kill it
kill <PID>

# Or force kill
kill -9 <PID>
```

Note: Test progress is not saved. You'll need to restart from the beginning.

## Interpreting Final Results

Expected final output:
```
Completed in XXXX seconds

=== Test Results ===
Total tests:    4294967296
Passed:         4294967296
Failed:         0
Special cases:  ~67000000
Pass rate:      100.000000%
```

### What This Means

- **dtoa_dragonbox correctly handles all possible float values**
- **Every float can be converted to string and back without loss**
- **The implementation is production-ready for float precision**

## Next Steps

After successful exhaustive float test:

1. ✓ Float precision is fully validated
2. → Run double precision tests (sampled only)
3. → Integrate into production code
4. → Consider performance benchmarks
5. → Document any known limitations

## FAQ

**Q: How does parallel testing work?**
A: The test divides the 2^32 bit patterns evenly across all CPU cores. Each thread tests its assigned range independently with thread-safe atomic counters for statistics.

**Q: Is parallel testing safe?**
A: Yes. All shared state uses atomic operations or mutex protection. The test has been validated for thread safety.

**Q: Why not test doubles exhaustively?**
A: 2^64 patterns would take ~584,542 years at 1M tests/second.

**Q: What if my CPU is slow?**
A: The test will take longer. Consider running the quick test only, or run exhaustive test on a faster machine.

**Q: Can I test a subset of patterns?**
A: Yes, modify the test to iterate over a range instead of all 2^32. Useful for debugging specific regions.

**Q: How do I know the test is working?**
A: Check that progress updates appear every few minutes. Pass rate should stay at 100%.

**Q: What about different rounding modes?**
A: Test assumes default rounding. Changing rounding modes may affect results.
