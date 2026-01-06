# TypedArray Factory Functions Proposal

## Problem

Currently, creating TypedArray instances for different use cases (deduplication, mmap, owned) requires understanding the internal flag system and manually managing the dedup flag parameter. This leads to code like:

```cpp
// Current: Not immediately clear what the 'true' means
TypedArray<T>(ptr, true);   // Is this dedup? mmap? owned?

// Current: Constructor from raw memory requires knowing view mode
TypedArrayImpl<T>(data, size, true);  // What does 'true' mean here?
```

## Proposed Solution

Add descriptive factory functions that make the intent explicit and simplify common use cases.

### 1. Factory Functions for TypedArray (Smart Pointer Wrapper)

```cpp
// For owned arrays (will be deleted by TypedArray)
template<typename T>
TypedArray<T> MakeOwnedTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, false);  // dedup_flag = false
}

// For deduplicated arrays (shared, won't be deleted)
template<typename T>
TypedArray<T> MakeDedupTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, true);  // dedup_flag = true
}

// Alias for clarity (same as MakeDedupTypedArray)
template<typename T>
TypedArray<T> MakeSharedTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, true);  // dedup_flag = true
}

// For memory-mapped arrays (non-owning, won't be deleted)
template<typename T>
TypedArray<T> MakeMmapTypedArray(TypedArrayImpl<T>* ptr) {
    return TypedArray<T>(ptr, true);  // dedup_flag = true (same as dedup)
}
```

### 2. Factory Functions for TypedArrayImpl (Array Implementation)

```cpp
// Create array with owned copy of data
template<typename T>
TypedArrayImpl<T> MakeTypedArrayCopy(const T* data, size_t count) {
    return TypedArrayImpl<T>(data, count);  // Copies data
}

// Create non-owning view over external memory
template<typename T>
TypedArrayImpl<T> MakeTypedArrayView(T* data, size_t count) {
    return TypedArrayImpl<T>(data, count, true);  // is_view = true
}

// Create array for memory-mapped data (non-owning view)
template<typename T>
TypedArrayImpl<T> MakeTypedArrayMmap(T* data, size_t count) {
    return TypedArrayImpl<T>(data, count, true);  // is_view = true
}

// Create empty array with specified capacity
template<typename T>
TypedArrayImpl<T> MakeTypedArrayReserved(size_t capacity) {
    TypedArrayImpl<T> arr;
    arr.reserve(capacity);
    return arr;
}
```

### 3. Combined Convenience Functions

For the common pattern of creating both implementation and wrapper:

```cpp
// Create owned TypedArray from data copy
template<typename T>
TypedArray<T> CreateOwnedTypedArray(const T* data, size_t count) {
    auto* impl = new TypedArrayImpl<T>(data, count);
    return MakeOwnedTypedArray(impl);
}

// Create deduplicated TypedArray from existing implementation
template<typename T>
TypedArray<T> CreateDedupTypedArray(const TypedArrayImpl<T>& source) {
    // Implementation pointer is owned by dedup cache, mark as shared
    return MakeDedupTypedArray(const_cast<TypedArrayImpl<T>*>(&source));
}

// Create mmap TypedArray over external memory
template<typename T>
TypedArray<T> CreateMmapTypedArray(T* data, size_t count) {
    auto* impl = new TypedArrayImpl<T>(data, count, true);  // View mode
    return MakeMmapTypedArray(impl);
}
```

## Usage Examples

### Example 1: Deduplication Cache (Current Use Case)

**Before:**
```cpp
// In crate-reader.cc, not immediately clear what's happening
auto it = _dedup_int32_array.find(value_rep);
if (it != _dedup_int32_array.end()) {
    // Reuse cached array - mark as dedup to prevent deletion
    typed_arr = TypedArray<int32_t>(it->second.get(), true);  // What does 'true' mean?
}
```

**After:**
```cpp
// Clear intent: this is a deduplicated/shared array
auto it = _dedup_int32_array.find(value_rep);
if (it != _dedup_int32_array.end()) {
    typed_arr = MakeDedupTypedArray(it->second.get());
}
```

### Example 2: Memory-Mapped File Data

**Before:**
```cpp
// Unclear if this is a view or copy
TypedArrayImpl<float> arr(mmap_ptr, mmap_size, true);  // What does 'true' mean?
```

**After:**
```cpp
// Explicit: non-owning view over mmap'd memory
TypedArrayImpl<float> arr = MakeTypedArrayMmap(mmap_ptr, mmap_size);
```

### Example 3: Creating Owned Array from Data

**Before:**
```cpp
// Ownership unclear
auto* impl = new TypedArrayImpl<double>(data, count);
TypedArray<double> arr(impl, false);  // What does 'false' mean?
```

**After:**
```cpp
// Clear ownership semantics
TypedArray<double> arr = CreateOwnedTypedArray(data, count);
```

## Implementation Location

Add these functions to `src/typed-array.hh` at the end of the file, after the existing helper functions (around line 1200).

## Benefits

1. **Self-Documenting**: Function names clearly indicate intent (Owned, Dedup, Mmap, View, Copy)
2. **Type Safety**: No boolean flags that could be confused
3. **Consistency**: Uniform naming convention across the codebase
4. **Ease of Use**: Simpler API for common patterns
5. **Maintainability**: Easier to understand and modify code later

## Migration Path

1. Add new factory functions to `typed-array.hh`
2. Keep existing constructors for backward compatibility
3. Gradually migrate existing code to use factory functions
4. Eventually deprecate raw boolean flag constructors (optional)

## Naming Conventions

- `Make*`: Returns an object by value
- `Create*`: Creates new heap-allocated objects and wraps them
- Suffixes:
  - `*Owned`: TypedArray will delete the implementation
  - `*Dedup`: Shared/deduplicated, won't be deleted
  - `*Shared`: Alias for Dedup (clearer for some use cases)
  - `*Mmap`: Memory-mapped, non-owning
  - `*View`: Non-owning view (for TypedArrayImpl)
  - `*Copy`: Copies data (for TypedArrayImpl)

## Alternative Naming

If you prefer shorter names:

```cpp
// Shorter alternatives
template<typename T>
TypedArray<T> OwnedArray(TypedArrayImpl<T>* ptr);

template<typename T>
TypedArray<T> SharedArray(TypedArrayImpl<T>* ptr);

template<typename T>
TypedArray<T> MmapArray(TypedArrayImpl<T>* ptr);
```

## Questions for Discussion

1. Do you prefer `Make*` or `Create*` naming for the factory functions?
2. Should `Dedup` and `Mmap` be separate functions or the same (they have identical implementation)?
3. Would you like even shorter names like `OwnedArray()` instead of `MakeOwnedTypedArray()`?
4. Should we add a `MakeDuplicateTypedArray()` function that deep-copies an existing TypedArray?
