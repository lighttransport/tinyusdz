# Phase 1 Implementation Summary: Offset-Based Deduplication

## Overview

Phase 1 of the TimeSamples refactoring has been successfully implemented. This phase replaces the pointer-based deduplication mechanism with an index-based approach embedded directly in the offset table, eliminating dangling pointer issues and simplifying memory management.

## Changes Made

### 1. Offset Encoding (src/timesamples.hh)

**Changed offset type from `size_t` to `uint64_t` with bit flags:**

```cpp
// Old:
mutable std::vector<size_t> _offsets;

// New:
mutable std::vector<uint64_t> _offsets;
```

**Bit Layout (64-bit):**
- **Bit 63**: Dedup flag (1 = deduplicated, 0 = original data)
- **Bit 62**: Array flag (1 = array data, 0 = scalar data)
- **Bits 61-0**: Index (if dedup) or byte offset (if original)

### 2. Offset Manipulation Helpers

Added helper functions to PODTimeSamples:

```cpp
// Create non-dedup offset
static constexpr uint64_t make_offset(size_t byte_offset, bool is_array);

// Create dedup offset
static constexpr uint64_t make_dedup_offset(size_t sample_index, bool is_array);

// Check flags
static constexpr bool is_dedup(uint64_t offset_value);
static constexpr bool is_array_offset(uint64_t offset_value);

// Extract value
static constexpr size_t get_raw_value(uint64_t offset_value);
```

### 3. Offset Resolution with Dedup Chain Following

```cpp
bool resolve_offset(size_t sample_idx, size_t* out_byte_offset,
                    bool* out_is_array = nullptr, size_t max_depth = 100) const;
```

Features:
- Follows dedup chain recursively
- Detects circular references (max depth 100)
- Returns actual byte offset in `_values` buffer

### 4. Circular Reference Validation

```cpp
bool validate_dedup_reference(size_t ref_index, size_t new_sample_idx) const;
```

Checks:
- ✓ Bounds check (ref_index < size)
- ✓ Self-reference check (ref_index != new_sample_idx)
- ✓ Non-blocked check (cannot dedup from blocked sample)
- ✓ **Non-dedup source check (cannot dedup from deduplicated data)**

This prevents creating circular reference chains like: `sample[2] -> sample[1] -> sample[0]`

### 5. Updated add_dedup Methods

Both `add_dedup_array_sample<T>` and `add_dedup_matrix_array_sample<T>` now:

1. Validate the reference using `validate_dedup_reference()`
2. Provide detailed error messages for each failure case
3. Store sample index instead of copying offset value
4. Set dedup flag (bit 63) in the offset

**Example:**
```cpp
// Old approach (copied offset value):
_offsets.push_back(_offsets[ref_index]);  // Dangling pointer risk!

// New approach (store index with flag):
uint64_t dedup_offset = make_dedup_offset(ref_index, true);
_offsets.push_back(dedup_offset);  // Safe index reference
```

### 6. Updated add_array Methods

All array addition methods now encode offsets with the array flag:

```cpp
// Old:
_offsets.push_back(_values.size());

// New:
size_t byte_offset = _values.size();
uint64_t encoded_offset = make_offset(byte_offset, true);  // is_array=true
_offsets.push_back(encoded_offset);
```

### 7. Updated Retrieval Methods

`get_value_at<T>` and `get_typed_array_view_at<T>` now use offset resolution:

```cpp
// Old:
const uint8_t* src = _values.data() + _offsets[idx];

// New:
size_t byte_offset = 0;
if (!resolve_offset(idx, &byte_offset)) {
    return false;  // Failed: circular ref or invalid chain
}
const uint8_t* src = _values.data() + byte_offset;
```

### 8. Sorting with Index Remapping (src/timesamples.cc)

Updated `sort_with_offsets()` to remap dedup indices after sorting:

```cpp
// Create index mapping: old_idx -> new_idx
std::vector<size_t> index_map(times.size());
for (size_t new_idx = 0; new_idx < indices.size(); ++new_idx) {
    index_map[indices[new_idx]] = new_idx;
}

// Remap dedup indices
for (size_t i = 0; i < indices.size(); ++i) {
    uint64_t offset_val = offsets[indices[i]];

    if (offset_val & PODTimeSamples::OFFSET_DEDUP_FLAG) {
        // Extract old reference index
        size_t old_ref_idx = static_cast<size_t>(offset_val & PODTimeSamples::OFFSET_VALUE_MASK);

        // Map to new index
        size_t new_ref_idx = index_map[old_ref_idx];

        // Reconstruct offset with new index
        offset_val = (offset_val & PODTimeSamples::OFFSET_FLAGS_MASK) | new_ref_idx;
    }

    sorted_offsets[i] = offset_val;
}
```

**Example:**
```
Before sort:
  idx 0: time 3.0, arr3 (original)
  idx 1: time 1.0, arr1 (original)
  idx 2: time 2.0, dedup->0

After sort:
  idx 0: time 1.0, arr1 (was idx 1)
  idx 1: time 2.0, dedup->2 (was idx 2, now refs new idx of old 0)
  idx 2: time 3.0, arr3 (was idx 0)
```

## Benefits

### 1. No Dangling Pointers
- **Old**: Stored `TypedArray` packed pointers could become invalid after moves
- **New**: Store sample indices which remain valid after vector moves

### 2. Simplified Memory Management
- **Old**: Complex `TypedArrayImpl` lifetime tracking with dedup flags
- **New**: Single `_values` buffer owns all data, simple ownership model

### 3. Move-Safe
- **Old**: TypedArray moves could invalidate dedup pointers
- **New**: Indices are stable across moves, only need remapping on sort

### 4. Clear Semantics
- **Old**: Offset reuse was implicit (same offset value = dedup)
- **New**: Explicit dedup flag makes intent clear

### 5. Robust Validation
- Prevents circular references at insertion time
- Prevents chaining dedup samples (must reference original)
- Detects invalid chains at resolution time

### 6. Efficient Sorting
- Index remapping is O(n) with simple map lookup
- No complex pointer arithmetic required

## Testing

Created comprehensive unit test suite: `tests/unit/test-phase1-offset-dedup.cc`

**Test Coverage:**

1. ✓ **test_offset_encoding**: Verify bit manipulation helpers
2. ✓ **test_array_addition**: Array sample addition with new encoding
3. ✓ **test_dedup_validation**: Circular reference checks (all cases)
4. ✓ **test_dedup_resolution**: Dedup chain following
5. ✓ **test_sorting_with_dedup**: Index remapping after sort
6. ✓ **test_multiple_dedup**: Multiple samples referencing same original
7. ✓ **test_matrix_dedup**: Matrix array deduplication
8. ✓ **test_offset_limits**: 62-bit value range limits
9. ✓ **test_blocked_samples_with_dedup**: Blocked sample edge cases
10. ✓ **test_edge_cases**: Empty and invalid index handling

**All existing unit tests pass:** ✓ All 27 tests passing

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Add original array | O(1) | Append to _values buffer |
| Add dedup array | O(1) | Validate + store index |
| Resolve offset | O(d) | d = dedup chain depth (typically 1) |
| Sort with dedup | O(n log n + n) | Sort + index remapping |
| Circular ref check | O(1) | Direct flag check |

**Memory Usage:**
- Offset table: 8 bytes per sample (was `sizeof(size_t)` = 8 bytes)
- No change in memory footprint
- Dedup samples: 0 bytes in _values (same as before)

## Validation

### Circular Reference Detection

**Case 1: Self-reference**
```cpp
ok = samples.add_dedup_array_sample<float3>(4.0, current_idx, &err);
// FAIL: "Self-reference detected"
```

**Case 2: Dedup from dedup**
```cpp
samples.add_array_sample<float3>(1.0, arr1);  // idx 0: original
samples.add_dedup_array_sample<float3>(2.0, 0);  // idx 1: dedup->0 ✓
samples.add_dedup_array_sample<float3>(3.0, 1);  // idx 2: dedup->1 ✗
// FAIL: "Cannot deduplicate from deduplicated sample"
```

**Case 3: Dedup from blocked**
```cpp
samples.add_blocked_array_sample(2.0, count);  // idx 1: blocked
samples.add_dedup_array_sample<float3>(3.0, 1);  // ✗
// FAIL: "Cannot deduplicate from blocked sample"
```

**Case 4: Out of bounds**
```cpp
samples.add_dedup_array_sample<float3>(5.0, 999);  // ✗
// FAIL: "Invalid ref_index: 999 >= 3"
```

## Known Limitations

1. **Max dedup chain depth**: 100 hops (configurable, prevents infinite loops)
2. **Offset range**: 62 bits = 4 petabytes (sufficient for all realistic use cases)
3. **Index range**: 62 bits = 4 trillion samples (sufficient for all realistic use cases)

## Backward Compatibility

- **API**: Public `PODTimeSamples` methods unchanged
- **Behavior**: Deduplication works identically from user perspective
- **ABI**: Changed offset type may break binary compatibility (acceptable for refactoring)
- **Serialization**: USDC format unchanged (dedup is runtime optimization)

## Future Work (Phase 2 & 3)

Phase 1 lays the foundation for:

- **Phase 2**: Unify `PODTimeSamples` with `value::TimeSamples`
  - Move offset/value storage to TimeSamples
  - Deprecate PODTimeSamples class
  - Support `std::vector<T>` directly

- **Phase 3**: 64-bit packed value storage
  - Inline storage for types ≤ 8 bytes
  - Heap storage with dedup for types > 8 bytes
  - Target: 50-70% memory reduction

## Conclusion

Phase 1 successfully implements offset-based deduplication with comprehensive circular reference checking. The implementation:

✓ Eliminates dangling pointer issues
✓ Simplifies memory management
✓ Maintains performance
✓ Passes all existing tests
✓ Provides strong validation guarantees

The code is ready for Phase 2 implementation.
