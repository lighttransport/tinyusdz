# Phase 2 Implementation Plan: Unify PODTimeSamples with TimeSamples

## Goal

Eliminate the dual-path storage in `value::TimeSamples` by moving PODTimeSamples storage directly into TimeSamples, creating a single unified implementation that works for all types.

## Current Architecture (After Phase 1)

```cpp
// value::TimeSamples - dual storage
struct TimeSamples {
  private:
    std::vector<Sample> _samples;       // Generic Value-based path
    PODTimeSamples _pod_samples;        // POD optimization path
    bool _use_pod;                      // Flag to choose path
    uint32_t _type_id;
    bool _dirty;
};

// PODTimeSamples - optimized storage (Phase 1 complete)
struct PODTimeSamples {
  private:
    std::vector<double> _times;
    Buffer<16> _blocked;
    Buffer<16> _values;                 // Raw byte storage
    std::vector<uint64_t> _offsets;     // With dedup/array flags (Phase 1)
    uint32_t _type_id;
    bool _is_stl_array;
    bool _is_typed_array;
    size_t _array_size;
};
```

## Target Architecture (Phase 2)

```cpp
// Unified TimeSamples - single storage path
struct TimeSamples {
  private:
    // Storage for ALL types (POD and non-POD)
    std::vector<double> _times;
    Buffer<16> _blocked;
    Buffer<16> _values;                 // Raw byte storage
    std::vector<uint64_t> _offsets;     // With dedup/array flags

    uint32_t _type_id;
    bool _is_array;                     // True for array types
    size_t _array_size;                 // Element count for arrays
    size_t _element_size;               // Bytes per element

    // Generic path for non-POD types (strings, tokens, dicts, etc.)
    std::vector<Sample> _samples_generic;
    bool _use_generic_path;             // True for non-POD types

    mutable bool _dirty;
};
```

## Implementation Steps

### Step 1: Move Storage Members to TimeSamples ✓

**Add to TimeSamples private section:**
```cpp
// POD storage (moved from PODTimeSamples)
Buffer<16> _values;
std::vector<uint64_t> _offsets;
bool _is_array{false};
size_t _array_size{0};
size_t _element_size{0};

// Keep generic path for non-POD types
std::vector<Sample> _samples_generic;  // Renamed from _samples
bool _use_generic_path{false};         // Replaces _use_pod (inverted logic)
```

**Update constructors:**
- Initialize new members
- Update move/copy constructors to handle all members

### Step 2: Add Direct Array Methods to TimeSamples ✓

Implement these methods directly in TimeSamples (no delegation to PODTimeSamples):

```cpp
// Array sample addition
template<typename T>
bool add_array_sample(double t, const T* values, size_t count, std::string* err = nullptr);

template<typename T>
bool add_matrix_array_sample(double t, const T* matrices, size_t count, std::string* err = nullptr);

// Deduplication
template<typename T>
bool add_dedup_array_sample(double t, size_t ref_index, std::string* err = nullptr);

template<typename T>
bool add_dedup_matrix_array_sample(double t, size_t ref_index, std::string* err = nullptr);

// Array retrieval
template<typename T>
TypedArrayView<const T> get_typed_array_view_at(size_t idx) const;

template<typename T>
TypedArrayView<const T> get_typed_array_view_at_time(double t) const;
```

### Step 3: Add std::vector<T> Support ✓

```cpp
// Add sample from std::vector
template<typename T>
bool add_sample(double t, const std::vector<T>& array, std::string* err = nullptr) {
    return add_array_sample(t, array.data(), array.size(), err);
}

// Get sample as std::vector
template<typename T>
bool get_vector_at(size_t idx, std::vector<T>* out_vec, bool* out_blocked = nullptr) const {
    auto view = get_typed_array_view_at<T>(idx);
    if (view.size() == 0) {
        if (out_blocked) *out_blocked = true;
        return false;
    }
    out_vec->assign(view.data(), view.data() + view.size());
    if (out_blocked) *out_blocked = false;
    return true;
}
```

### Step 4: Update Existing Methods ✓

Modify existing methods to use unified storage:

**`add_sample<T>(double t, const T& value)`:**
- Check if T is POD type using `is_pod_type_id()`
- If POD: use offset-based storage in `_values`
- If not: use `_samples_generic` with Value wrapper

**`get_sample_at(size_t idx)`:**
- Check `_use_generic_path`
- If false: convert from POD storage to Value
- If true: return from `_samples_generic`

**`update()`:**
- Call sorting logic on appropriate storage
- For POD types: sort `_times`, `_blocked`, `_offsets` together
- For generic: sort `_samples_generic`

### Step 5: Helper Methods ✓

```cpp
private:
    // Get element size for a type_id
    static size_t get_element_size_for_type(uint32_t type_id);

    // Resolve offset to byte offset (from Phase 1)
    bool resolve_offset(size_t sample_idx, size_t* out_byte_offset,
                       bool* out_is_array = nullptr) const;

    // Check if type should use generic path
    static bool should_use_generic_path(uint32_t type_id);
```

### Step 6: Migration Strategy ✓

**Deprecated but kept for compatibility:**
```cpp
// Keep as deprecated accessor
[[deprecated("Use direct methods instead of get_pod_storage")]]
PODTimeSamples* get_pod_storage() {
    // Return nullptr - force users to migrate
    return nullptr;
}
```

**Migration guide for users:**
```cpp
// Old code:
TimeSamples ts;
ts.init(type_id);
ts.get_pod_storage()->add_array_sample<float3>(1.0, data, count);

// New code:
TimeSamples ts;
ts.init(type_id);
ts.add_array_sample<float3>(1.0, data, count);
```

### Step 7: Remove PODTimeSamples Dependency ✓

**Final cleanup:**
1. Remove `_pod_samples` member from TimeSamples
2. Remove `_use_pod` flag (replaced by `_use_generic_path`)
3. Update all internal methods to use unified storage
4. Mark PODTimeSamples class as deprecated
5. Eventually remove PODTimeSamples entirely (Phase 2.5 or later)

## Code Changes Summary

### Files Modified

**src/timesamples.hh:**
- Move storage members from PODTimeSamples to TimeSamples
- Add array methods directly to TimeSamples
- Add std::vector<T> support
- Update constructors and member functions
- Deprecate get_pod_storage()

**src/timesamples.cc:**
- Implement unified update() sorting
- Move helper functions to TimeSamples context

**Usage in codebase:**
- `src/crate-reader-timesamples.cc` - Update to use new API
- `src/ascii-parser-timesamples.cc` - Update to use new API
- `src/ascii-parser-timesamples-array.cc` - Update to use new API
- `src/timesamples-pprint.cc` - Update to access unified storage
- `tests/unit/unit-timesamples.cc` - Update test cases

## Benefits

1. **Simplified API**: Single code path, no more `get_pod_storage()`
2. **std::vector support**: Can directly store `std::vector<T>`
3. **Reduced branching**: No `_use_pod` checks everywhere
4. **Easier maintenance**: One implementation to maintain
5. **Better type safety**: Templates work directly on TimeSamples
6. **Foundation for Phase 3**: Ready for packed storage optimization

## Testing Strategy

1. **Unit tests**: All existing tests must pass
2. **New tests**: Add tests for std::vector<T> support
3. **Integration tests**: Test with real USD files
4. **Performance**: Verify no regression vs Phase 1

## Timeline

**Week 1**: Storage member migration + basic array methods
**Week 2**: std::vector support + update existing methods
**Week 3**: Migration of callsites + testing + cleanup

## Compatibility

**Source compatibility**: ✅ Mostly preserved
- Existing `add_sample()` calls work unchanged
- `get_pod_storage()` deprecated but still compiles (returns nullptr)

**Binary compatibility**: ❌ Broken
- TimeSamples layout changed
- Requires recompilation

**Behavioral compatibility**: ✅ Full
- Same functionality, different implementation
- All tests should pass
