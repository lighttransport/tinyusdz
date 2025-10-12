# ValueView Compact Implementation Test

This test verifies the functionality of the compact 16-byte `ValueView` class implementation.

## What It Tests

### Memory Layout
- Verifies that `ValueView` is exactly 16 bytes in size
- Confirms the memory layout: pointer(8) + type_id(4) + flags(1) + padding(3)

### Core Functionality
- Default constructor creates an invalid view
- Direct construction from concrete type pointers using `template<typename T> ValueView(const T* ptr)`
- Direct typed access using `template<typename T> const T* view()` method
- Storage type tracking with flags (vector vs TypedArray detection)

### Type System Integration
- Proper type_id storage and retrieval
- Role type conversions (e.g., `point3f` ↔ `float3`)
- Underlying type ID mapping for role types

### Container Support
- `std::vector<T>` specialization with FLAG_IS_VECTOR
- `TypedArray<T>` specialization with FLAG_IS_TYPED_ARRAY
- `as_view<T>()` method for array data access

### Additional Features
- Reset methods for changing viewed data
- Equality operators for comparing views
- Storage flag queries (`is_vector()`, `is_typed_array()`)

## Building and Running

```bash
# Build the test
make

# Build and run the test
make test

# Clean build artifacts
make clean
```

## Expected Output

When successful, you should see:
```
Testing compact ValueView implementation...

Size of ValueView: 16 bytes
✓ ValueView is exactly 16 bytes

✓ Default constructor works
✓ Direct construction from float* works
  - view<float>() returns correct value: 3.14
✓ Direct construction from std::vector<double>* works
  - is_vector() = 1
✓ as_view<int>() works for std::vector
✓ reset() methods work
✓ Role type handling works (point3f -> float3)
✓ Storage flags work correctly
✓ Equality operators work
✓ Member layout confirms 16-byte structure

✅ All compact ValueView tests passed!

Summary:
- ValueView size: 16 bytes (exactly 16)
- Supports direct construction from T*
- Supports direct view<T>() method
- Tracks storage type (vector/TypedArray) with flags
- Memory layout: pointer(8) + type_id(4) + flags(1) + padding(3) = 16 bytes
```

## Implementation Notes

The compact `ValueView` provides a highly efficient, cache-friendly view object that:
- Stores type information inline (no need to dereference for type_id)
- Uses only 16 bytes (fits in 2 cache words)
- Supports zero-copy access to typed data
- Maintains type safety through template methods
- Tracks storage type for proper array handling

Key design decisions:
- `const void*` pointer for type-erased storage
- 32-bit type_id for USD type system compatibility
- 8-bit flags for storage type metadata
- 3 bytes padding ensures 16-byte alignment
- Template methods provide type-safe access without virtual functions