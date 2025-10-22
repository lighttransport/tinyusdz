# TimeSamples Refactoring Plan

## Overview

This document outlines a comprehensive refactoring plan for `value::TimeSamples` to optimize memory usage, simplify deduplication management, and prevent dangling pointer issues. The refactoring is divided into three major phases.

## Current Architecture Issues

### Problem 1: TypedArray Deduplication Complexity
- Currently, `PODTimeSamples` uses `TypedArray<T>` for storing array data with packed pointers
- Deduplication is managed through TypedArray's dedup flag (bit 63 in packed pointer)
- When TypedArray is moved or copied, dangling pointer issues can occur
- Memory management is complex due to pointer-based deduplication

### Problem 2: Separate POD and Generic Paths
- `PODTimeSamples` exists as a separate optimization for POD types
- `value::TimeSamples` uses generic `value::Value` for non-POD types
- This dual-path approach increases code complexity and maintenance burden

### Problem 3: Storage Inefficiency
- Current implementation stores full `value::Value` objects (larger than needed)
- For types <= 8 bytes, we could use inline storage instead of indirection
- Memory overhead for small value types is significant

## Phase 1: Offset-Based Deduplication in PODTimeSamples

### Goal
Replace TypedArray pointer-based deduplication with index-based deduplication embedded in the offset table.

### Design

#### Offset Value Bit Layout (64-bit)
```
Bit 63:    Dedup flag (1 = deduplicated, 0 = original data)
Bit 62:    1D array flag (1 = array data, 0 = scalar data)
Bits 61-0: Index or offset value
```

#### Behavior

**For Original (Non-Dedup) Data:**
- Bit 63 = 0, Bit 62 indicates array/scalar
- Bits 61-0: Byte offset into `_values` buffer

**For Deduplicated Data:**
- Bit 63 = 1, Bit 62 = copy from original
- Bits 61-0: **Sample index** (not byte offset) pointing to original sample

### Example
```cpp
// Sample 0: Original data at offset 0
_offsets[0] = 0x0000000000000000  // No dedup, scalar, offset 0

// Sample 1: Original array data at offset 32
_offsets[1] = 0x4000000000000020  // No dedup, array (bit 62=1), offset 32

// Sample 2: Deduplicated from sample 1
_offsets[2] = 0xC000000000000001  // Dedup (bit 63=1), array (bit 62=1), index 1

// Sample 3: Deduplicated from sample 0
_offsets[3] = 0x8000000000000000  // Dedup (bit 63=1), scalar (bit 62=0), index 0
```

### Access Pattern
```cpp
size_t resolve_offset(size_t sample_idx) const {
    uint64_t offset_value = _offsets[sample_idx];

    if (offset_value & 0x8000000000000000ULL) {
        // Deduplicated: bits 61-0 contain original sample index
        size_t orig_idx = offset_value & 0x3FFFFFFFFFFFFFFFULL;
        return resolve_offset(orig_idx);  // Recursive resolve
    } else {
        // Original: bits 61-0 contain byte offset
        return offset_value & 0x3FFFFFFFFFFFFFFFULL;
    }
}
```

### Sorting Considerations

When `update()` sorts samples by time, we need to update dedup indices:

```cpp
void update() const {
    if (!_dirty) return;

    // 1. Create index mapping (old_idx -> new_idx)
    std::vector<size_t> sorted_indices = create_sorted_indices(_times);
    std::vector<size_t> index_map(_times.size());
    for (size_t new_idx = 0; new_idx < sorted_indices.size(); ++new_idx) {
        index_map[sorted_indices[new_idx]] = new_idx;
    }

    // 2. Sort times, blocked, and offsets
    reorder_by_indices(_times, sorted_indices);
    reorder_by_indices(_blocked, sorted_indices);
    reorder_by_indices(_offsets, sorted_indices);

    // 3. Update dedup indices in offset table
    for (uint64_t& offset_val : _offsets) {
        if (offset_val & 0x8000000000000000ULL) {
            size_t old_ref_idx = offset_val & 0x3FFFFFFFFFFFFFFFULL;
            size_t new_ref_idx = index_map[old_ref_idx];

            // Reconstruct offset value with new index
            offset_val = (offset_val & 0xC000000000000000ULL) | new_ref_idx;
        }
    }

    _dirty = false;
}
```

### Implementation Tasks

1. **Add offset manipulation helpers**
   - `make_offset(byte_offset, is_array)` - Create non-dedup offset
   - `make_dedup_offset(sample_index, is_array)` - Create dedup offset
   - `is_dedup(offset_value)` - Check dedup flag
   - `is_array(offset_value)` - Check array flag
   - `get_byte_offset(offset_value)` - Extract offset (resolve dedup chain)
   - `get_dedup_index(offset_value)` - Extract dedup index

2. **Update `add_dedup_array_sample()` and `add_dedup_matrix_array_sample()`**
   - Replace current implementation
   - Store sample index instead of reusing offset
   - Set bit 63 to indicate deduplication

3. **Deprecate `add_typed_array_sample()`**
   - This method stores TypedArray packed pointers
   - Replace with `add_array_sample()` that stores actual data

4. **Update `get_value_at()` and array retrieval methods**
   - Implement offset resolution with dedup chain following
   - Handle indirect lookups through dedup indices

5. **Update `update()` sorting method**
   - Implement index remapping for dedup references
   - Ensure all dedup indices remain valid after sorting

6. **Update memory estimation**
   - No separate TypedArrayImpl allocations for dedup
   - Simpler calculation based on _values buffer only

### Benefits

- **No dangling pointers**: Dedup uses indices, not pointers
- **Simpler memory management**: No TypedArrayImpl lifetime tracking
- **Move-safe**: Indices remain valid after vector moves
- **Clear ownership**: _values buffer owns all data

### Risks & Mitigations

**Risk**: Dedup chain resolution adds overhead
- **Mitigation**: Most common case is 1-hop (95%+ of dedup)
- **Mitigation**: Cache last resolved offset if needed

**Risk**: Sorting becomes more complex
- **Mitigation**: Index remapping is O(n), acceptable cost
- **Mitigation**: Only happens when dirty flag is set

**Risk**: 62-bit limit on offsets (4 petabytes) and indices (4 trillion samples)
- **Mitigation**: Sufficient for all realistic use cases

## Phase 2: Unify PODTimeSamples with TimeSamples

### Goal
Eliminate the separate PODTimeSamples structure and use the offset-based approach directly in `value::TimeSamples` for all array types.

### Design

#### Current Dual Structure
```cpp
class TimeSamples {
    std::vector<Sample> _samples;      // Generic path
    PODTimeSamples _pod_samples;       // POD optimization path
    bool _use_pod;                     // Path selector
};
```

#### Proposed Unified Structure
```cpp
class TimeSamples {
    // Common storage for all types
    std::vector<double> _times;
    Buffer<16> _blocked;
    std::vector<uint64_t> _offsets;    // With dedup/array flags
    Buffer<16> _values;                // Raw byte storage

    uint32_t _type_id;
    bool _is_array;
    size_t _element_size;
    size_t _array_size;
};
```

### Array Type Support

**STL Arrays (`std::vector<T>`)**:
- Store array data inline in `_values` buffer
- Use array flag (bit 62) in offset
- Element count stored in `_array_size`

**TypedArray Deprecation**:
- Remove `add_typed_array_sample()` method
- Use `add_array_sample()` which stores actual data
- No more TypedArrayImpl pointer management

### Implementation Tasks

1. **Move offset/value storage to TimeSamples**
   - Add `_offsets`, `_values` members to TimeSamples
   - Remove `_pod_samples` member
   - Remove `_use_pod` flag

2. **Implement array methods directly in TimeSamples**
   - `add_array_sample<T>(double t, const T* values, size_t count)`
   - `add_dedup_array_sample<T>(double t, size_t ref_index)`
   - `get_array_at<T>(size_t idx, const T** out_ptr, size_t* out_count)`

3. **Support std::vector<T> sample addition**
   ```cpp
   template<typename T>
   bool add_sample(double t, const std::vector<T>& array) {
       return add_array_sample(t, array.data(), array.size());
   }
   ```

4. **Migrate existing code**
   - Update crate-reader to use new array methods
   - Update value-pprint to access unified storage
   - Remove PODTimeSamples usage from codebase

5. **Update get/query methods**
   - Single code path for POD and array types
   - Return TypedArrayView for array access (read-only)

### Benefits

- **Simplified API**: One TimeSamples class, not two code paths
- **std::vector support**: Can store std::vector<T> directly
- **Reduced complexity**: No POD vs generic branching
- **Unified sorting**: One update() implementation

### Backward Compatibility

- Keep `get_pod_storage()` as deprecated accessor
- Return const view of internal storage
- Gradual migration path for existing code

## Phase 3: 64-bit Packed Value Storage

### Goal
Optimize storage for small value types using inline 64-bit representation, similar to Crate's `ValueRep`.

### Design Philosophy

**Size-Based Strategy:**

1. **Types <= 8 bytes**: Store inline in 64-bit value slot
   - No deduplication needed (small values are cheap to copy)
   - No pointer indirection
   - Examples: float, double, int, float2, half4, etc.

2. **Types > 8 bytes**: Store in _values buffer with offset
   - Use dedup flag for shared data
   - Array flag for 1D arrays
   - Examples: float3, float4, matrix types, arrays

### Unified Storage Structure

```cpp
class TimeSamples {
    std::vector<double> _times;
    Buffer<16> _blocked;        // ValueBlock flags
    std::vector<uint64_t> _data; // Packed values or offset+flags

    // Optional: Only allocated for large types or arrays
    Buffer<16> _values;         // Heap storage for >8 byte types

    uint32_t _type_id;
    bool _uses_heap;            // True if _values is used
};
```

### Data Encoding (64-bit)

#### Small Types (<= 8 bytes): Inline Storage
```
Bits 63-0: Actual value data (bit-cast to uint64_t)
```

Examples:
- `float` (4 bytes): Stored in lower 32 bits
- `double` (8 bytes): Stored in all 64 bits
- `float2` (8 bytes): Stored in all 64 bits
- `int32_t` (4 bytes): Stored in lower 32 bits
- `half4` (8 bytes): Stored in all 64 bits

#### Large Types (> 8 bytes) or Arrays: Offset + Flags
```
Bit 63:    Dedup flag
Bit 62:    Array flag
Bit 61:    Heap flag (1 = offset to _values, always 1 for large types)
Bits 60-0: Offset or sample index (61-bit range)
```

### Type Classification

```cpp
enum class StorageMode {
    INLINE,      // <= 8 bytes, stored in _data directly
    HEAP_SCALAR, // > 8 bytes, offset into _values
    HEAP_ARRAY   // Array data, offset into _values
};

StorageMode get_storage_mode(uint32_t type_id, bool is_array) {
    if (is_array) return HEAP_ARRAY;

    size_t type_size = get_type_size(type_id);
    if (type_size <= 8) return INLINE;

    return HEAP_SCALAR;
}
```

### Example Encoding

```cpp
// Sample 0: float value 3.14f
_data[0] = 0x4048F5C3  // float bits in lower 32 bits

// Sample 1: double value 2.71828
_data[1] = 0x4005BF0A8B145769  // double bits in all 64 bits

// Sample 2: float3 value at heap offset 0
_data[2] = 0x2000000000000000  // Heap flag (bit 61), offset 0

// Sample 3: float3 deduplicated from sample 2
_data[3] = 0xE000000000000002  // Dedup + Heap flags, ref index 2

// Sample 4: float[] array at heap offset 12
_data[4] = 0x600000000000000C  // Array + Heap flags, offset 12

// Sample 5: Blocked sample
_blocked[5] = 1, _data[5] = 0  // Blocked flag set, data ignored
```

### Access Patterns

```cpp
template<typename T>
bool get_value_at(size_t idx, T* out) const {
    if (_blocked[idx]) {
        *out = T{}; // Default value for blocked
        return true;
    }

    uint64_t data_val = _data[idx];

    if constexpr (sizeof(T) <= 8 && !is_array_type<T>) {
        // INLINE: Direct bit-cast from _data
        std::memcpy(out, &data_val, sizeof(T));
        return true;
    } else {
        // HEAP: Resolve offset and copy from _values
        size_t offset = resolve_heap_offset(idx);
        std::memcpy(out, _values.data() + offset, sizeof(T));
        return true;
    }
}

size_t resolve_heap_offset(size_t idx) const {
    uint64_t data_val = _data[idx];

    if (data_val & 0x8000000000000000ULL) {
        // Dedup: Follow reference chain
        size_t ref_idx = data_val & 0x1FFFFFFFFFFFFFFFULL;
        return resolve_heap_offset(ref_idx);
    } else {
        // Direct: Extract offset
        return data_val & 0x1FFFFFFFFFFFFFFFULL;
    }
}
```

### Deduplication Strategy

**Small Types (INLINE):**
- No deduplication tracking needed
- Storing multiple copies is cheaper than managing dedup

**Large Types (HEAP):**
- Deduplication via bit 63 in _data
- Same index-based approach as Phase 1

**Rationale:**
- Deduplication overhead only worthwhile for expensive-to-copy data
- float3 (12 bytes) vs dedup overhead (index lookup): dedup wins
- float (4 bytes) vs dedup overhead: inline wins

### Memory Savings Example

**Before (current implementation):**
```
10,000 samples of float:
- 10,000 × 8 bytes (time) = 80 KB
- 10,000 × ~40 bytes (Value object) = 400 KB
Total: ~480 KB
```

**After (packed storage):**
```
10,000 samples of float:
- 10,000 × 8 bytes (time) = 80 KB
- 10,000 × 8 bytes (inline data) = 80 KB
- 10,000 × 1 bit (blocked) ≈ 1.2 KB
Total: ~161 KB (66% reduction)
```

### Implementation Tasks

1. **Type size classification**
   - Add `get_storage_mode(type_id)` helper
   - Map all USD types to INLINE or HEAP

2. **Update add_sample methods**
   ```cpp
   template<typename T>
   bool add_sample(double t, const T& value) {
       _times.push_back(t);
       _blocked.push_back(0);

       if constexpr (sizeof(T) <= 8) {
           // INLINE path
           uint64_t packed = 0;
           std::memcpy(&packed, &value, sizeof(T));
           _data.push_back(packed);
       } else {
           // HEAP path
           size_t offset = _values.size();
           _values.resize(offset + sizeof(T));
           std::memcpy(_values.data() + offset, &value, sizeof(T));

           uint64_t packed = 0x2000000000000000ULL | offset; // Heap flag
           _data.push_back(packed);
       }
   }
   ```

3. **Update get_value_at methods**
   - Check storage mode
   - Use inline or heap access path
   - Handle dedup resolution

4. **Update deduplication methods**
   - Only for HEAP types
   - Set dedup flag in _data[idx]
   - Store reference index

5. **Update update() sorting**
   - Sort times, blocked, data together
   - Remap dedup indices (heap types only)
   - No special handling for inline types

6. **Optimize memory layout**
   - Only allocate _values when first heap type added
   - Keep _data tightly packed

7. **Update serialization/deserialization**
   - USDC writer: pack values appropriately
   - USDA writer: format based on storage mode

### Benefits

- **Memory efficiency**: 50-70% reduction for small types
- **Cache efficiency**: All small values in _data array
- **No indirection**: Inline types accessed directly
- **Selective dedup**: Only used where beneficial

### Compatibility

- **API preserved**: Same public interface
- **Type support**: All existing USD types supported
- **Migration**: Transparent to users

### Performance Characteristics

| Operation | Before | After | Notes |
|-----------|--------|-------|-------|
| Add float sample | O(1) + alloc | O(1) inline | 2-3x faster |
| Add float3 sample | O(1) + alloc | O(1) heap | Similar |
| Get float sample | O(1) + deref | O(1) memcpy | 2x faster |
| Get float3 sample | O(1) + deref | O(1) + resolve | Similar |
| Dedup float | O(1) | N/A (no dedup) | Not needed |
| Dedup float3 | O(1) | O(1) | Same |
| Sort samples | O(n log n) | O(n log n) | Same complexity |
| Memory usage | ~40 bytes/sample | ~16 bytes/sample | 60% reduction |

## Implementation Roadmap

### Phase 1: Offset-Based Deduplication (2-3 weeks)
**Week 1:**
- [ ] Implement offset bit manipulation helpers
- [ ] Add unit tests for offset encoding/decoding
- [ ] Update PODTimeSamples::add_dedup_array_sample()

**Week 2:**
- [ ] Implement offset resolution with dedup chain
- [ ] Update PODTimeSamples::update() with index remapping
- [ ] Add comprehensive tests for sorting + dedup

**Week 3:**
- [ ] Update crate-reader to use new dedup API
- [ ] Update value-pprint to handle new offset format
- [ ] Performance testing and optimization

### Phase 2: Unify PODTimeSamples (2-3 weeks)
**Week 1:**
- [ ] Move offset/value storage to TimeSamples
- [ ] Implement array methods in TimeSamples
- [ ] Add std::vector<T> support

**Week 2:**
- [ ] Migrate crate-reader usage
- [ ] Migrate value-pprint usage
- [ ] Update all TimeSamples callsites

**Week 3:**
- [ ] Remove PODTimeSamples class
- [ ] Clean up deprecated APIs
- [ ] Update documentation

### Phase 3: 64-bit Packed Storage (3-4 weeks)
**Week 1:**
- [ ] Design type size classification
- [ ] Implement inline vs heap detection
- [ ] Add encoding/decoding helpers

**Week 2:**
- [ ] Implement INLINE path for add_sample
- [ ] Implement INLINE path for get_value_at
- [ ] Add unit tests for all small types

**Week 3:**
- [ ] Implement HEAP path with selective dedup
- [ ] Update sorting with hybrid storage
- [ ] Add comprehensive integration tests

**Week 4:**
- [ ] Performance benchmarking
- [ ] Memory profiling
- [ ] Optimization and tuning

## Testing Strategy

### Unit Tests
- Offset encoding/decoding (all bit patterns)
- Dedup chain resolution (1-hop, multi-hop)
- Index remapping during sorting
- Storage mode classification
- Inline value encoding/decoding
- Heap value with dedup

### Integration Tests
- Round-trip: write USDC → read → verify
- Large datasets (10K+ samples)
- Mixed type scenarios
- Dedup + sorting combinations

### Performance Tests
- Memory usage comparison (before/after)
- Add sample throughput
- Get sample throughput
- Sort performance
- Dedup overhead measurement

### Regression Tests
- Existing USDA/USDC test suite
- Parse all models in `models/` directory
- Compare output with baseline

## Migration Guide

### For Internal Code

**Phase 1:**
```cpp
// Before
pod_samples.add_typed_array_sample(time, typed_array);

// After
pod_samples.add_array_sample(time, typed_array.data(), typed_array.size());
```

**Phase 2:**
```cpp
// Before
if (ts.is_using_pod()) {
    const PODTimeSamples* pod = ts.get_pod_storage();
    // Use pod methods
}

// After
// Direct TimeSamples methods
ts.get_array_at(idx, &ptr, &count);
```

**Phase 3:**
```cpp
// Before and After: No API changes
// Internal storage changes are transparent
```

### For External Users

- **API stability**: Public TimeSamples API remains unchanged
- **Binary compatibility**: May break ABI (major version bump)
- **Serialization format**: USDC format unchanged (Crate-compatible)

## Risk Assessment

### High Risk
- **Phase 3 sorting complexity**: Hybrid storage adds branching
  - Mitigation: Extensive testing, performance benchmarks

### Medium Risk
- **Phase 1 dedup chain bugs**: Index remapping is error-prone
  - Mitigation: Comprehensive unit tests, assertions

- **Phase 2 migration scope**: Many callsites to update
  - Mitigation: Incremental migration, keep deprecated APIs temporarily

### Low Risk
- **Memory savings**: May not reach projected 60%
  - Mitigation: Profile real-world data, adjust strategy

- **Performance regression**: Offset resolution overhead
  - Mitigation: Benchmark early, optimize hot paths

## Success Criteria

### Phase 1
- [ ] All existing tests pass
- [ ] No memory leaks (valgrind clean)
- [ ] Dedup still works correctly after sorting
- [ ] Performance: <5% regression on add/get operations

### Phase 2
- [ ] PODTimeSamples fully removed from codebase
- [ ] std::vector<T> samples can be added
- [ ] Code complexity reduced (measured by cyclomatic complexity)
- [ ] Documentation updated

### Phase 3
- [ ] Memory usage reduced by 50%+ for small types
- [ ] Performance improved or neutral for inline types
- [ ] All USD types supported (100+ types)
- [ ] USDC round-trip passes all tests

## Future Enhancements

### Compression
- Apply LZ4/Zstd to _values buffer for large datasets
- Transparent decompression on access

### SIMD Optimization
- Vectorized search in _times array
- Parallel offset resolution for bulk operations

### Memory Mapping
- Support mmap for _values buffer
- Zero-copy reading from USDC files

### Incremental Sorting
- Insertion sort for small dirty ranges
- Avoid full sort when only a few samples added

## Conclusion

This three-phase refactoring will significantly improve `value::TimeSamples`:

1. **Phase 1**: Solves dangling pointer issues, simplifies dedup
2. **Phase 2**: Unifies code paths, reduces complexity
3. **Phase 3**: Optimizes memory, improves performance

The end result will be a more maintainable, efficient, and safer TimeSamples implementation that scales well to large production scenes.
