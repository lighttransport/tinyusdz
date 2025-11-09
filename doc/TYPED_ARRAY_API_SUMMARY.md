# TypedArray Factory API - Summary

## Quick Reference

### For TypedArray (Smart Pointer Wrapper)

| Function | Purpose | Deletes on Destruction? |
|----------|---------|------------------------|
| `MakeOwnedTypedArray(ptr)` | Owned array | ✅ Yes |
| `MakeDedupTypedArray(ptr)` | Deduplicated/cached | ❌ No |
| `MakeSharedTypedArray(ptr)` | Shared among owners | ❌ No |
| `MakeMmapTypedArray(ptr)` | Memory-mapped | ❌ No |

### For TypedArrayImpl (Array Implementation)

| Function | Purpose | Copies Data? |
|----------|---------|-------------|
| `MakeTypedArrayCopy(data, size)` | Copy data | ✅ Yes |
| `MakeTypedArrayView(data, size)` | Non-owning view | ❌ No |
| `MakeTypedArrayMmap(data, size)` | Mmap view | ❌ No |
| `MakeTypedArrayReserved<T>(capacity)` | Empty with capacity | N/A |

### Combined Convenience Functions

| Function | Purpose |
|----------|---------|
| `CreateOwnedTypedArray(data, size)` | Create owned copy in one call |
| `CreateOwnedTypedArray<T>(size)` | Create owned array with size |
| `CreateOwnedTypedArray<T>(size, value)` | Create owned array with default value |
| `CreateDedupTypedArray(ptr)` | Wrap as deduplicated |
| `CreateMmapTypedArray(data, size)` | Create mmap view in one call |
| `DuplicateTypedArray(source)` | Deep copy TypedArray |
| `DuplicateTypedArrayImpl(source)` | Deep copy TypedArrayImpl |

## Common Patterns

### Pattern 1: Deduplication Cache (Most Common)

```cpp
// Check cache
auto it = _dedup_float_array.find(value_rep);
if (it != _dedup_float_array.end()) {
    // Found in cache - return deduplicated reference
    return MakeDedupTypedArray(it->second.get());
} else {
    // Not in cache - read, store, and return
    auto* impl = new TypedArrayImpl<float>(data, size);
    _dedup_float_array[value_rep] = MakeOwnedTypedArray(impl);
    return MakeDedupTypedArray(impl);
}
```

### Pattern 2: Memory-Mapped Files

```cpp
float* mmap_data = static_cast<float*>(mmap_ptr);
TypedArray<float> arr = CreateMmapTypedArray(mmap_data, count);
// arr doesn't own mmap_data, just references it
```

### Pattern 3: Owned Array Creation

```cpp
// One-liner
TypedArray<double> arr = CreateOwnedTypedArray(source_data.data(), source_data.size());

// Or with default value
TypedArray<int> arr = CreateOwnedTypedArray<int>(1000, 42);
```

### Pattern 4: Temporary View

```cpp
float buffer[1000];
PopulateBuffer(buffer);
auto view = MakeTypedArrayView(buffer, 1000);
ProcessData(view);
// buffer still valid
```

### Pattern 5: Deep Copy

```cpp
TypedArray<T> original = GetSharedArray();
TypedArray<T> copy = DuplicateTypedArray(original);
// Modify copy independently
```

## Design Principles

1. **Self-Documenting Names**: Function names clearly indicate intent
2. **No Boolean Flags**: Avoid confusing `true`/`false` parameters
3. **Consistent Naming**:
   - `Make*` = Create by value
   - `Create*` = Allocate and wrap
   - Suffixes indicate ownership/semantics
4. **Backward Compatible**: Existing constructors still work
5. **Type Safe**: Compiler catches misuse

## Comparison: Old vs New

### Old Way (Boolean Flags)
```cpp
TypedArray<T>(ptr, true);   // ❌ What does 'true' mean?
TypedArray<T>(ptr, false);  // ❌ What does 'false' mean?
TypedArrayImpl<T>(data, size, true);  // ❌ Copy or view?
```

### New Way (Named Functions)
```cpp
MakeDedupTypedArray(ptr);       // ✅ Clear: deduplicated
MakeOwnedTypedArray(ptr);       // ✅ Clear: owned
MakeTypedArrayView(data, size); // ✅ Clear: non-owning view
```

## When to Use Each Function

### Use `MakeOwnedTypedArray` when:
- You're creating a new array that should be owned by the TypedArray
- The array will be deleted when TypedArray is destroyed
- Example: Loading data from a file

### Use `MakeDedupTypedArray` when:
- The array is stored in a deduplication cache
- Multiple TypedArrays reference the same underlying data
- The cache manages the lifetime
- Example: USD Crate format deduplication

### Use `MakeSharedTypedArray` when:
- Same as `MakeDedupTypedArray`, but clearer for non-dedup use cases
- Multiple owners share the same data
- Lifetime managed externally

### Use `MakeMmapTypedArray` when:
- Working with memory-mapped files
- Data lives in external memory you don't own
- Example: Zero-copy file reading

### Use `MakeTypedArrayCopy` when:
- You want to copy data into a TypedArrayImpl
- You own the copy and can modify it
- Example: Loading configuration data

### Use `MakeTypedArrayView` when:
- You want a temporary non-owning view
- Original data lifetime is guaranteed
- Example: Processing data in a buffer

### Use `DuplicateTypedArray` when:
- You need an independent copy
- Modifications shouldn't affect the original
- Example: Snapshot for undo/redo

## Implementation Notes

- All functions are inline templates
- Zero runtime overhead compared to direct constructors
- Can be added without breaking existing code
- Optional shorter aliases available with `TINYUSDZ_USE_SHORT_TYPED_ARRAY_NAMES`

## Files

- **Proposal**: `doc/TYPED_ARRAY_FACTORY_PROPOSAL.md`
- **Implementation**: `doc/typed-array-factories.hh`
- **Migration Examples**: `doc/TYPED_ARRAY_MIGRATION_EXAMPLES.md`
- **This Summary**: `doc/TYPED_ARRAY_API_SUMMARY.md`

## Next Steps

To integrate into the codebase:

1. Copy functions from `doc/typed-array-factories.hh` to end of `src/typed-array.hh`
2. Update `src/crate-reader.cc` dedup code to use `MakeDedupTypedArray`
3. Update `src/timesamples.hh` to use `MakeDedupTypedArray`
4. (Optional) Gradually migrate other uses

No breaking changes required!
