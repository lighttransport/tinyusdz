# TypedArrayView TimeSamples Test

This test verifies the functionality of `TypedArrayView` methods in `TimeSamples` for both binary array storage and `std::vector` storage.

## What It Tests

### TimeSamples with binary array storage
- `get_typed_array_view_at<T>(size_t idx)` - Returns a view of array data at a specific index
- `get_typed_array_view_at_time<T>(double t)` - Returns a view of array data at a specific time
- Proper handling of non-existent times (returns empty view)

### TimeSamples
- `get_typed_array_view_at<T>(size_t idx)` - Returns a view for std::vector data
- `get_typed_array_view_at_time<T>(double t)` - Returns a view for std::vector data
- Proper handling of blocked samples
- Support for both TypedArray and std::vector storage

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
Testing TypedArrayView methods in TimeSamples...

Testing TimeSamples TypedArrayView for binary float arrays...
  ✓ get_typed_array_view_at(0) works
  ✓ get_typed_array_view_at(1) works
  ✓ get_typed_array_view_at_time(1.0) works
  ✓ get_typed_array_view_at_time(2.0) works
  ✓ get_typed_array_view_at_time(3.0) returns empty for non-existent
  ✓ get_typed_array_view_at_time(5.0) returns empty for non-existent

Testing TimeSamples TypedArrayView with std::vector storage...
  ✓ get_typed_array_view_at(0) works for std::vector
  ✓ get_typed_array_view_at(2) returns empty for blocked
  ✓ get_typed_array_view_at_time(1.0) works for std::vector

✅ All tests passed!
```

## Implementation Notes

The `TypedArrayView` provides a lightweight, non-owning view over array data stored in TimeSamples. This allows efficient access to time-sampled array data without copying.

Key features:
- Zero-copy access to array data
- Returns const-qualified views for safety
- Handles blocked values (ValueBlock) by returning empty views
- Works with multiple storage types (`TypedArray`, `std::vector`, raw binary-storage arrays)
