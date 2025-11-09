# TypedArray TimeSamples Feature Test

This test verifies the implementation of `TypedArray<T>` support for TimeSamples with value deduplication.

## Feature Overview

The TypedArray-based TimeSamples implementation provides:

1. **Memory-efficient storage**: Uses `TypedArray<T>` instead of `std::vector<T>` for array data
2. **Deduplication support**: Reuses cached array data when the same ValueRep appears multiple times
3. **Backwards compatibility**: Existing `std::vector<T>` overloads continue to work
4. **POD optimization path**: Integrates with TinyUSDZ's POD-optimized TimeSamples storage

## Supported Types

### POD Array Types (with TypedArray)
- `int32_t[]`, `uint32_t[]`, `int64_t[]`, `uint64_t[]`
- `half[]`, `float[]`, `double[]`

### Composite Array Types (still using std::vector)
- `half2[]`, `half3[]`, `half4[]`
- `float2[]`, `float3[]`, `float4[]`
- `double2[]`, `double3[]`, `double4[]`
- `quath[]`, `quatf[]`, `quatd[]`
- `matrix2d[]`, `matrix3d[]`, `matrix4d[]`

## Building

### Using Make (Standalone)

```bash
cd tests/feat/typed-array-timesamples
make
```

### Running the Test

```bash
make test
```

Or run directly:

```bash
./test-typed-array-timesamples
```

### Cleaning

```bash
make clean
```

## Test Coverage

The test program verifies:

1. **TypedArray deduplication**: Adding the same `TypedArray<T>` at multiple time samples
2. **std::vector compatibility**: Ensuring existing vector-based API still works
3. **Scalar values**: Testing POD scalar value storage
4. **Multiple types**: Testing int32, uint32, int64, uint64, float, and double

## Implementation Details

### Key Files Modified

- `src/crate-reader.hh`: Updated dedup cache maps to use `TypedArray<T>`
- `src/timesamples.hh`: Added `TypedArray<T>` overloads for `add_array_sample_pod`
- `src/crate-reader-timesamples.cc`: Updated UnpackTimeSampleValue_* functions
  - INT32, UINT32, INT64, UINT64: Use `ReadIntArrayTyped`
  - HALF: Convert from std::vector to TypedArray
  - FLOAT: Use `ReadFloatArrayTyped`
  - DOUBLE: Use `ReadDoubleArrayTyped`

### Deduplication Cache

The deduplication cache in `CrateReader` stores decoded values keyed by `ValueRep`:

```cpp
std::unordered_map<crate::ValueRep, TypedArray<T>, crate::ValueRep::Hash> _dedup_<type>_array;
```

When the same `ValueRep` appears multiple times, the cached `TypedArray<T>` is reused,
avoiding redundant file reads and memory allocations.

## Benefits

1. **Reduced memory usage**: Deduplicated arrays are stored once and referenced multiple times
2. **Faster parsing**: Cached arrays avoid redundant file I/O and decompression
3. **Cleaner code**: TypedArray provides a consistent interface for both owned and view data
4. **Future-ready**: TypedArray supports mmap views for even more memory efficiency

## See Also

- `src/typed-array.hh` - TypedArray implementation
- `src/timesamples.hh` - TimeSamples POD optimization
- `src/crate-reader-timesamples.cc` - Crate format TimeSamples reader
