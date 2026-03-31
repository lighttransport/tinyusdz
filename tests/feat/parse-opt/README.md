# Parse Optimization Benchmark

This benchmark tests the performance of optimized array parsing in TinyUSDZ's ASCII parser.

It supports two execution profiles:

- Full profile: default when you run `bench-parse-opt` directly
- Quick profile: reduced workload used by `ctest` via `bench-parse-opt --quick`

## Features

The benchmark generates synthetic test data and measures parsing performance for:

### Array Types
- **float[]** - Simple float arrays
- **float3[]** - Vector arrays with 3 components
- **double[]** - Double precision arrays
- **matrix4d[]** - 4x4 matrix arrays

### TimeSamples
- **float[] timeSamples** - Animated float arrays
- **float3[] timeSamples** - Animated point arrays

### Complete USDA Files
- Full USDA file parsing with realistic mesh data

## Building

```bash
cmake -S . -B build -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build build -j16 --target bench-parse-opt
```

## Running

```bash
./build/bench-parse-opt
```

Quick profile:

```bash
./build/bench-parse-opt --quick
```

The `ctest` target is wired to the quick profile:

```bash
cd build
ctest -R bench-parse-opt --output-on-failure
```

## Optimizations Tested

The benchmark exercises the following optimizations:

1. **Zero-copy array scanning** - Direct pointer access to input buffer instead of character-by-character string building
2. **fast_float parsing** - Using the fast_float library for optimal float/double parsing
3. **Pointer-based lexing** - Avoiding temporary std::string allocations during lexing

## Profiles

The full profile keeps the larger synthetic cases for manual performance work.

The quick profile scales down the largest arrays and time-sample cases so it can remain inside the regular `ctest` suite without dominating total suite time.

## Expected Performance

On a Ryzen 3900X with -O2 optimization:
- **float[] parsing**: ~2-5 ms per 100K elements
- **float3[] parsing**: ~5-10 ms per 100K vectors
- **matrix4d[] parsing**: ~10-20 ms per 10K matrices
- **TimeSamples**: Proportional to total data size

## Output Format

```
=== Float Array Parsing ===

Array size: 100000 elements
  Generated data size: 1234567 bytes
  Parse time: 4.5 ms
```

The benchmark reports:
- Array size (element count)
- Generated data size (bytes)
- Parse time in milliseconds

## Notes

- The benchmark uses synthetic random data generated at runtime
- Timings exclude data generation time, only measuring parse performance
- Results may vary based on CPU, memory, and compiler optimization level
- The quick profile is intended for test-suite smoke coverage, not stable performance tracking
