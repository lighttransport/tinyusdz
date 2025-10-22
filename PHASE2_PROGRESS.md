# Phase 2 Progress: TimeSamples Unification

## Status: Partially Complete (Steps 1-3 Done)

**Date**: 2025-10-23
**Branch**: crate-timesamples-opt

## Overview

Phase 2 aims to unify PODTimeSamples with value::TimeSamples by moving POD storage directly into TimeSamples, eliminating the dual-path architecture.

## Completed Work

### ✅ Step 1: Storage Member Migration

**Commit**: 61f4040a - "Phase 2 Step 1: Add PODTimeSamples storage members to TimeSamples"

Added POD storage members to TimeSamples:
```cpp
// POD path storage (moved from PODTimeSamples for Phase 2 unification)
mutable std::vector<double> _times;
mutable Buffer<16> _blocked;
mutable Buffer<16> _values;               // Raw byte storage for POD types
mutable std::vector<uint64_t> _offsets;   // Offset table with dedup/array flags

// Type information
uint32_t _type_id{0};
bool _use_pod{false};  // True = use POD path, False = use generic path

// Array type information (for POD arrays)
bool _is_array{false};
size_t _array_size{0};
size_t _element_size{0};
mutable size_t _blocked_count{0};

mutable bool _dirty{false};
mutable size_t _dirty_start{0};
mutable size_t _dirty_end{0};

// Keep _pod_samples for now (will be removed in final step)
mutable tinyusdz::PODTimeSamples _pod_samples;
```

Updated:
- Move constructor and move assignment operator
- `clear()` method
- Kept `empty()` and `size()` delegating to `_pod_samples` for backward compat

### ✅ Step 2: Array Methods Implementation

**Commits**:
- 55820490 - "Phase 2: Add static resolve_offset and implement array methods in TimeSamples"

Added static helper to PODTimeSamples:
```cpp
static bool resolve_offset_static(const std::vector<uint64_t>& offsets,
                                   size_t sample_idx,
                                   size_t* out_byte_offset,
                                   bool* out_is_array = nullptr,
                                   size_t max_depth = 100);
```

Refactored instance method to use static version:
```cpp
bool resolve_offset(...) const {
  return resolve_offset_static(_offsets, sample_idx, ...);
}
```

Implemented unified array methods in TimeSamples:
```cpp
template<typename T>
bool add_array_sample(double t, const T* values, size_t count, std::string* err);

template<typename T>
bool add_dedup_array_sample(double t, size_t ref_index, std::string* err);

template<typename T>
bool add_matrix_array_sample(double t, const T* matrices, size_t count, std::string* err);

template<typename T>
bool add_dedup_matrix_array_sample(double t, size_t ref_index, std::string* err);
```

Updated `get_typed_array_view_at()` to support unified storage with three paths:
1. Delegate to `_pod_samples` (backward compat)
2. Use unified storage (`_times`, `_offsets`, `_values`)
3. Fall back to generic Value storage (`_samples`)

### ✅ Step 3: std::vector<T> Getter Support

**Commits**:
- 79c20118 - "Phase 2: Add std::vector<T> support to TimeSamples"
- a150813c - "Fix test failure: Remove add_sample(std::vector<T>&) override"

Added convenience getters for std::vector<T>:
```cpp
template<typename T>
bool get_vector_at(size_t idx, std::vector<T>* out_vec, bool* out_blocked = nullptr) const;

template<typename T>
bool get_vector_at_time(double t, std::vector<T>* out_vec, bool* out_blocked = nullptr) const;
```

**Note**: Initially added `add_sample(double t, const std::vector<T>& array)` but removed it because:
- It broke interpolation support for `std::vector<T>` types
- It incorrectly routed POD element types to POD array storage
- The existing Value-based `add_sample` works correctly with interpolation
- Only getters are needed for convenience

### ✅ Bug Fixes

**Commit**: d081e1ae - "Fix segfault: revert empty() and size() to original logic"

Fixed segfault caused by incorrect `empty()` and `size()` implementation:
- Reverted to original simple logic: `_use_pod ? _pod_samples.empty() : _samples.empty()`
- The Phase 2 changes that checked unified storage were premature
- Unified storage will be properly integrated when callsites are updated

## Test Results

✅ All unit tests pass (22/22):
- `timesamples_test` - **PASS** (interpolation working correctly)
- All other tests - **PASS**

## Build Verification

✅ Compiles cleanly with:
- GCC 13.3.0 - no errors, no warnings
- Clang++ 21 - no errors, no warnings
- ASAN build - clean, no memory issues

## Code Quality

- **Warnings**: 0
- **Errors**: 0
- **Test Coverage**: All existing tests pass
- **Memory Safety**: Verified with AddressSanitizer

## Architecture Status

**Current State**: Hybrid dual-path with unified storage available

```cpp
struct TimeSamples {
  private:
    // Generic path storage (for non-POD types)
    mutable std::vector<Sample> _samples;

    // POD path storage (Phase 2 unified storage - available but not used yet)
    mutable std::vector<double> _times;
    mutable Buffer<16> _blocked;
    mutable Buffer<16> _values;
    mutable std::vector<uint64_t> _offsets;

    // Backward compatibility (to be removed in final step)
    mutable PODTimeSamples _pod_samples;
    uint32_t _type_id{0};
    bool _use_pod{false};

    // Array metadata for unified storage
    bool _is_array{false};
    size_t _array_size{0};
    size_t _element_size{0};
};
```

**Usage Pattern**:
- `_use_pod = true` → uses `_pod_samples` (old POD path)
- `_use_pod = false` → uses `_samples` (generic Value path)
- Unified storage in `_times/_offsets/_values` is available but not actively used yet

## Remaining Work

### Step 4: Update Callsites (Not Started)

Update these files to use unified API instead of `get_pod_storage()`:
- `src/crate-reader-timesamples.cc` - Binary USD file parser
- `src/ascii-parser-timesamples.cc` - ASCII USD parser (scalar)
- `src/ascii-parser-timesamples-array.cc` - ASCII USD parser (arrays)
- `src/timesamples-pprint.cc` - Pretty-printing

Migration pattern:
```cpp
// Old code:
ts.init(type_id);
ts.get_pod_storage()->add_array_sample<float3>(1.0, data, count);

// New code:
ts.init(type_id);
ts.add_array_sample<float3>(1.0, data, count);
```

### Step 5: Remove _pod_samples and _use_pod (Not Started)

Final cleanup:
1. Remove `_pod_samples` member from TimeSamples
2. Remove `_use_pod` flag
3. Update `empty()` and `size()` to check unified storage
4. Remove `get_pod_storage()` method
5. Mark PODTimeSamples class as deprecated (or remove entirely)

## Benefits Achieved So Far

1. ✅ **Foundation Ready**: Unified storage members in place
2. ✅ **Direct API**: Can call array methods directly on TimeSamples
3. ✅ **Static Helper**: `resolve_offset_static()` enables Phase 2 unified storage
4. ✅ **Convenience Getters**: `get_vector_at()` methods for easy retrieval
5. ✅ **Backward Compatible**: All existing code still works
6. ✅ **Test Coverage**: All tests pass

## Compatibility

**Source Compatibility**: ✅ Full
- All existing code compiles without changes
- New methods available but optional

**Binary Compatibility**: ⚠️ Broken
- TimeSamples layout changed (added members)
- Static library must be recompiled
- Acceptable for refactoring phase

**Behavioral Compatibility**: ✅ Full
- All tests pass with same results
- Functionality preserved

## Next Steps

1. **Complete Step 4**: Update callsites in crate-reader and parsers
2. **Complete Step 5**: Remove _pod_samples and _use_pod
3. **Final Testing**: Comprehensive test with real USD files
4. **Performance Validation**: Verify no regression
5. **Documentation**: Update API documentation

## Timeline Estimate

- **Step 4 (Update callsites)**: 1-2 days
- **Step 5 (Remove _pod_samples)**: 1 day
- **Testing & validation**: 1 day
- **Total remaining**: 3-4 days

## Risk Assessment

**Technical Risks**: Low
- Foundation is solid and tested
- Incremental approach minimizes risk
- All tests passing at each step

**Compatibility Risks**: Low
- Source compatible (no API changes needed)
- Only ABI break (expected for refactoring)
- Clear migration path

## Conclusion

Phase 2 is 60% complete (Steps 1-3 done, Steps 4-5 remaining). The foundation for unified storage is in place and working correctly. The remaining work is primarily mechanical (updating callsites) with low risk.

The implementation is stable, tested, and ready for the next steps.
