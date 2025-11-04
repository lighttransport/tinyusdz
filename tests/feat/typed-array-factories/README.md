# TypedArray Factory Functions Test

This test verifies the TypedArray factory functions that provide clearer, more intuitive interfaces for creating TypedArray instances.

## Overview

The factory functions provide self-documenting alternatives to the boolean flag constructors:

### Before (Confusing)
```cpp
TypedArray<T>(ptr, true);   // ❌ What does 'true' mean?
```

### After (Clear)
```cpp
MakeDedupTypedArray(ptr);   // ✅ Clear: deduplicated array
```

## Factory Functions Tested

### TypedArray Wrappers (4 functions)
- `MakeOwnedTypedArray()` - Owned array (will delete)
- `MakeDedupTypedArray()` - Deduplicated array (won't delete)
- `MakeSharedTypedArray()` - Shared array (won't delete)
- `MakeMmapTypedArray()` - Memory-mapped array (won't delete)

### TypedArrayImpl Creation (4 functions)
- `MakeTypedArrayCopy()` - Copy data into owned storage
- `MakeTypedArrayView()` - Non-owning view over external memory
- `MakeTypedArrayMmap()` - Non-owning view for mmap data
- `MakeTypedArrayReserved()` - Empty array with reserved capacity

### Convenience Functions (7 functions)
- `CreateOwnedTypedArray()` - 3 overloads for creating owned arrays
- `CreateDedupTypedArray()` - Wrap as deduplicated
- `CreateMmapTypedArray()` - Create mmap in one call
- `DuplicateTypedArray()` - Deep copy TypedArray
- `DuplicateTypedArrayImpl()` - Deep copy TypedArrayImpl

## Building

### Using Make (Standalone)

```bash
# Build
make

# Build and run tests
make test

# Clean
make clean
```

### Manual Compilation

```bash
g++-13 -std=c++14 -I../../../src test-typed-array-factories.cc -o test-typed-array-factories
./test-typed-array-factories
```

## Test Coverage

The test suite includes:

1. **Ownership Tests**
   - Verify owned arrays delete on destruction
   - Verify dedup arrays don't delete on destruction
   - Verify dedup flag is set correctly

2. **Data Integrity Tests**
   - Verify data is copied correctly
   - Verify views reference original data
   - Verify modifications affect/don't affect original

3. **View Tests**
   - Verify view mode is set correctly
   - Verify views don't own memory
   - Verify modifications through views work

4. **Convenience Function Tests**
   - Verify combined operations work correctly
   - Verify duplication creates independent copies

5. **Real-World Pattern Tests**
   - Deduplication cache pattern
   - Memory-mapped file pattern
   - Temporary view pattern

## Expected Output

```
Testing TypedArray Factory Functions

Testing MakeOwnedTypedArray... ✓ PASS
Testing MakeDedupTypedArray... ✓ PASS
Testing MakeSharedTypedArray... ✓ PASS
Testing MakeMmapTypedArray... ✓ PASS
Testing MakeTypedArrayCopy... ✓ PASS
Testing MakeTypedArrayView... ✓ PASS
Testing MakeTypedArrayMmap... ✓ PASS
Testing MakeTypedArrayReserved... ✓ PASS
Testing CreateOwnedTypedArray_data... ✓ PASS
Testing CreateOwnedTypedArray_size... ✓ PASS
Testing CreateOwnedTypedArray_value... ✓ PASS
Testing CreateDedupTypedArray... ✓ PASS
Testing CreateMmapTypedArray... ✓ PASS
Testing DuplicateTypedArray... ✓ PASS
Testing DuplicateTypedArrayImpl... ✓ PASS
Testing deduplication_pattern... ✓ PASS

----------------------------------------
Total: 16 tests
Passed: 16
Failed: 0

✓ All tests passed!
```

## Implementation Location

The factory functions are implemented in:
- **File**: `src/typed-array.hh`
- **Lines**: 2376-2614
- **Functions**: 15 factory functions

## Documentation

Complete documentation available in `doc/`:
- `FACTORY_FUNCTIONS_INTEGRATION.md` - Integration summary
- `TYPED_ARRAY_FACTORY_PROPOSAL.md` - Detailed proposal
- `TYPED_ARRAY_MIGRATION_EXAMPLES.md` - Before/after examples
- `TYPED_ARRAY_ARCHITECTURE.md` - Architecture details
- `TYPED_ARRAY_API_SUMMARY.md` - Quick reference

## Requirements

- C++14 compiler (tested with g++-13)
- No external dependencies beyond standard library
- Header-only implementation (zero runtime overhead)

## Benefits

✅ **Self-Documenting** - Function names clearly indicate intent
✅ **Type-Safe** - No confusing boolean flags
✅ **Zero Overhead** - All inline, same performance
✅ **Backward Compatible** - Existing code still works
✅ **Easy Migration** - Can adopt gradually

## Common Usage Patterns

### Deduplication Cache
```cpp
auto it = _dedup_float_array.find(value_rep);
if (it != _dedup_float_array.end()) {
    return MakeDedupTypedArray(it->second.get());
} else {
    auto* impl = new TypedArrayImpl<float>(data, size);
    _dedup_float_array[value_rep] = MakeOwnedTypedArray(impl);
    return MakeDedupTypedArray(impl);
}
```

### Memory-Mapped Files
```cpp
float* mmap_data = static_cast<float*>(mmap_ptr);
TypedArray<float> arr = CreateMmapTypedArray(mmap_data, count);
// arr doesn't own mmap_data, just references it
```

### Temporary Views
```cpp
float buffer[1000];
PopulateBuffer(buffer);
auto view = MakeTypedArrayView(buffer, 1000);
ProcessData(view);
// buffer still valid after view destruction
```

## Test Status

- **Status**: ✅ All 16 tests passing
- **Last Updated**: 2025-01-09
- **Compiler**: g++-13
- **Standard**: C++14
