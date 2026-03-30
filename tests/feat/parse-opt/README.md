# Parse Optimization Benchmark

This benchmark tests the performance of optimized array parsing in TinyUSDZ's ASCII parser.

## Features

The benchmark generates synthetic test data and measures parsing performance for:

### Array Types
- **float[]** - Simple float arrays (1K to 1M elements)
- **float3[]** - Vector arrays with 3 components (1K to 100K vectors)
- **double[]** - Double precision arrays (1K to 1M elements)
- **matrix4d[]** - 4x4 matrix arrays (100 to 10K matrices)

### TimeSamples
- **float[] timeSamples** - Animated float arrays (10-100 frames × 1K-10K elements)
- **float3[] timeSamples** - Animated point arrays (10-100 frames × 1K-10K points)

### Complete USDA Files
- Full USDA file parsing with realistic mesh data (10K points)

## Building

```bash
make
```

This will automatically build the required libtinyusdz_static.a library if needed.

## Running

```bash
make run
```

Or directly:
```bash
./test-parse-opt
```

## Optimizations Tested

The benchmark exercises the following optimizations:

1. **Zero-copy array scanning** - Direct pointer access to input buffer instead of character-by-character string building
2. **fast_float parsing** - Using the fast_float library for optimal float/double parsing
3. **Pointer-based lexing** - Avoiding temporary std::string allocations during lexing

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

- The benchmark uses random data generation with a fixed seed for reproducibility
- Timings exclude data generation time, only measuring parse performance
- Results may vary based on CPU, memory, and compiler optimization level
