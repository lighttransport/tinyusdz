# TypedArray Factory Functions - Migration Examples

This document shows concrete examples of migrating existing code to use the new factory functions.

## Example 1: Deduplication Cache in crate-reader.cc

### Before (Current Code)
```cpp
// In UnpackTimeSampleValue_IntArray
TypedArray<int32_t> typed_arr;

// Check dedup cache
auto it = _dedup_int32_array.find(value_rep);
if (it != _dedup_int32_array.end()) {
    // Reuse cached array
    typed_arr = TypedArray<int32_t>(it->second.get(), true);  // ❌ What does 'true' mean?
} else {
    // Read new array
    std::vector<int32_t> arr;
    if (!ReadIntArray(value_rep, &arr, &err)) {
        return false;
    }

    // Store in cache
    auto* impl = new TypedArrayImpl<int32_t>(arr.data(), arr.size());
    _dedup_int32_array[value_rep] = TypedArray<int32_t>(impl, false);

    // Return as dedup
    typed_arr = TypedArray<int32_t>(impl, true);  // ❌ Confusing flags
}
```

### After (With Factory Functions)
```cpp
// In UnpackTimeSampleValue_IntArray
TypedArray<int32_t> typed_arr;

// Check dedup cache
auto it = _dedup_int32_array.find(value_rep);
if (it != _dedup_int32_array.end()) {
    // ✅ Clear: This is a deduplicated (shared) array
    typed_arr = MakeDedupTypedArray(it->second.get());
} else {
    // Read new array
    std::vector<int32_t> arr;
    if (!ReadIntArray(value_rep, &arr, &err)) {
        return false;
    }

    // ✅ Clear: Create owned array and store in cache
    auto* impl = new TypedArrayImpl<int32_t>(arr.data(), arr.size());
    _dedup_int32_array[value_rep] = MakeOwnedTypedArray(impl);

    // ✅ Clear: Return as deduplicated reference
    typed_arr = MakeDedupTypedArray(impl);
}
```

**Benefits:**
- Intent is immediately clear from function names
- No mysterious boolean flags to decode
- Easier for code reviewers to understand

---

## Example 2: Memory-Mapped File Reading

### Before
```cpp
// Memory map a USD file
int fd = open("large_model.usdc", O_RDONLY);
void* mmap_ptr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

// Read array section from mmap'd memory
float* float_data = reinterpret_cast<float*>(
    static_cast<uint8_t*>(mmap_ptr) + array_offset
);
size_t element_count = array_size / sizeof(float);

// ❌ Not clear this is a non-owning view
TypedArrayImpl<float> arr(float_data, element_count, true);  // What does 'true' mean?
```

### After
```cpp
// Memory map a USD file
int fd = open("large_model.usdc", O_RDONLY);
void* mmap_ptr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

// Read array section from mmap'd memory
float* float_data = reinterpret_cast<float*>(
    static_cast<uint8_t*>(mmap_ptr) + array_offset
);
size_t element_count = array_size / sizeof(float);

// ✅ Clear: This is a view over memory-mapped data
TypedArrayImpl<float> arr = MakeTypedArrayMmap(float_data, element_count);
```

---

## Example 3: Creating Owned Arrays from Data

### Before
```cpp
// Copy data into owned array
std::vector<double> source_data = LoadFromFile();

// ❌ Unclear ownership semantics
auto* impl = new TypedArrayImpl<double>(source_data.data(), source_data.size());
TypedArray<double> arr(impl, false);  // What does 'false' mean?
```

### After (Option 1: Explicit Steps)
```cpp
// Copy data into owned array
std::vector<double> source_data = LoadFromFile();

// ✅ Clear: Create implementation, then wrap as owned
auto* impl = new TypedArrayImpl<double>(source_data.data(), source_data.size());
TypedArray<double> arr = MakeOwnedTypedArray(impl);
```

### After (Option 2: One-Liner)
```cpp
// Copy data into owned array
std::vector<double> source_data = LoadFromFile();

// ✅ Even clearer: Single function does everything
TypedArray<double> arr = CreateOwnedTypedArray(source_data.data(), source_data.size());
```

---

## Example 4: Temporary View for Processing

### Before
```cpp
// Create temporary view for processing without copying
float external_buffer[10000];
PopulateBuffer(external_buffer);

// ❌ Third parameter meaning unclear
TypedArrayImpl<float> view(external_buffer, 10000, true);

// Process data
ProcessArray(view);

// external_buffer is still valid here
```

### After
```cpp
// Create temporary view for processing without copying
float external_buffer[10000];
PopulateBuffer(external_buffer);

// ✅ Clear: This is a non-owning view
TypedArrayImpl<float> view = MakeTypedArrayView(external_buffer, 10000);

// Process data
ProcessArray(view);

// external_buffer is still valid here
```

---

## Example 5: Deep Copy for Independent Modification

### Before
```cpp
// Need to duplicate an array for independent modification
TypedArray<int32_t> original = GetSharedArray();

// ❌ Manual duplication is verbose
TypedArray<int32_t> copy;
if (original && !original.empty()) {
    auto* impl = new TypedArrayImpl<int32_t>(original.data(), original.size());
    copy = TypedArray<int32_t>(impl, false);
}

// Modify copy independently
for (size_t i = 0; i < copy.size(); ++i) {
    copy[i] *= 2;
}
```

### After
```cpp
// Need to duplicate an array for independent modification
TypedArray<int32_t> original = GetSharedArray();

// ✅ Clear and concise
TypedArray<int32_t> copy = DuplicateTypedArray(original);

// Modify copy independently
for (size_t i = 0; i < copy.size(); ++i) {
    copy[i] *= 2;
}
```

---

## Example 6: Pre-allocated Array with Reserved Capacity

### Before
```cpp
// Pre-allocate array for streaming data
TypedArrayImpl<double> buffer;
buffer.reserve(10000);  // ❌ Two-step process

for (const auto& chunk : data_stream) {
    for (double value : chunk) {
        buffer.push_back(value);
    }
}
```

### After
```cpp
// Pre-allocate array for streaming data
// ✅ Clear intent: reserved capacity
TypedArrayImpl<double> buffer = MakeTypedArrayReserved<double>(10000);

for (const auto& chunk : data_stream) {
    for (double value : chunk) {
        buffer.push_back(value);
    }
}
```

---

## Example 7: Creating Array with Default Values

### Before
```cpp
// Create array filled with default values
size_t count = 1000;
float default_value = 1.0f;

// ❌ Multi-step process
auto* impl = new TypedArrayImpl<float>(count, default_value);
TypedArray<float> arr(impl, false);
```

### After
```cpp
// Create array filled with default values
size_t count = 1000;
float default_value = 1.0f;

// ✅ Single clear function call
TypedArray<float> arr = CreateOwnedTypedArray<float>(count, default_value);
```

---

## Example 8: Migration of PODTimeSamples Code

### Before (from timesamples.hh)
```cpp
// Retrieve from dedup storage
TypedArrayImpl<T>* ptr = nullptr;

uint64_t ptr_bits = _packed_data & PTR_MASK;
if (ptr_bits & (1ULL << 47)) {
    ptr_bits |= 0xFFFF000000000000ULL;
}
ptr = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);

// ❌ Unclear why 'true' is used
*typed_array = TypedArray<T>(ptr, true);
```

### After
```cpp
// Retrieve from dedup storage
TypedArrayImpl<T>* ptr = nullptr;

uint64_t ptr_bits = _packed_data & PTR_MASK;
if (ptr_bits & (1ULL << 47)) {
    ptr_bits |= 0xFFFF000000000000ULL;
}
ptr = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);

// ✅ Clear: This is a deduplicated array (won't be deleted)
*typed_array = MakeDedupTypedArray(ptr);
```

---

## Summary Table

| Use Case | Before | After | Function |
|----------|--------|-------|----------|
| Owned array | `TypedArray(ptr, false)` | `MakeOwnedTypedArray(ptr)` | ✅ Clear |
| Dedup array | `TypedArray(ptr, true)` | `MakeDedupTypedArray(ptr)` | ✅ Clear |
| Mmap view | `TypedArrayImpl(ptr, size, true)` | `MakeTypedArrayMmap(ptr, size)` | ✅ Clear |
| Data copy | `TypedArrayImpl(ptr, size)` | `MakeTypedArrayCopy(ptr, size)` | ✅ Clear |
| Non-owning view | `TypedArrayImpl(ptr, size, true)` | `MakeTypedArrayView(ptr, size)` | ✅ Clear |
| Deep copy | Manual allocation + copy | `DuplicateTypedArray(original)` | ✅ Clear |
| Create owned from data | Multi-step | `CreateOwnedTypedArray(data, size)` | ✅ Clear |
| Create with capacity | `TypedArrayImpl` + `reserve()` | `MakeTypedArrayReserved<T>(capacity)` | ✅ Clear |
| Create with default | Multi-step | `CreateOwnedTypedArray(size, value)` | ✅ Clear |

## Migration Strategy

1. **Phase 1**: Add factory functions to `typed-array.hh`
2. **Phase 2**: Update `crate-reader.cc` deduplication code
3. **Phase 3**: Update `timesamples.hh` PODTimeSamples code
4. **Phase 4**: Update any mmap-related code
5. **Phase 5**: (Optional) Mark old constructors as deprecated

No breaking changes - all existing code continues to work!
