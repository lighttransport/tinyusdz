# Crate File Deduplication - Comprehensive Report

## Target

OpenUSD v25.08
(Crate format 0.8.0)

## Overview

The USD Crate file format implements a sophisticated multi-level deduplication system to minimize file size and optimize memory usage. Deduplication occurs during the **write phase** when packing data into the binary format.

**Source File**: `pxr/usd/sdf/crateFile.cpp`

**Key Principle**: Write each unique value exactly once, then reference it by offset or index.

---

## Deduplication Levels

### 1. Structural Deduplication (Global)

Implemented in `_PackingContext` (lines 896-1033), these tables deduplicate fundamental structural elements across the entire file:

| Table | Type | Purpose | Location |
|-------|------|---------|----------|
| `tokenToTokenIndex` | `unordered_map<TfToken, TokenIndex>` | Dedup all tokens | Line 1013 |
| `stringToStringIndex` | `unordered_map<string, StringIndex>` | Dedup all strings | Line 1014 |
| `pathToPathIndex` | `unordered_map<SdfPath, PathIndex>` | Dedup all paths | Line 1015 |
| `fieldToFieldIndex` | `unordered_map<Field, FieldIndex>` | Dedup all fields | Line 1016 |
| `fieldsToFieldSetIndex` | `unordered_map<vector<FieldIndex>, FieldSetIndex>` | Dedup field sets | Line 1019-1020 |

**Initialization**: These tables are populated in parallel during `_PackingContext` construction (lines 917-973).

**Persistence**: Structural elements are written to dedicated sections:
- `TOKENS` section (line 227)
- `STRINGS` section (line 228)
- `FIELDS` section (line 229)
- `FIELDSETS` section (line 230)
- `PATHS` section (line 231)

### 2. Value-Level Deduplication (Per-Type)

Implemented in `_ValueHandler<T>` template (lines 1593-1737), this deduplicates actual data values:

```cpp
template <class T>
struct _ValueHandler : _ValueHandlerBase {
    // Dedup map for scalar values
    std::unique_ptr<std::unordered_map<T, ValueRep, _Hasher>> _valueDedup;

    // Dedup map for array values
    std::unique_ptr<std::unordered_map<VtArray<T>, ValueRep, _Hasher>> _arrayDedup;
};
```

**Key Characteristics**:
- One dedup map **per concrete type** (e.g., separate maps for `int`, `float`, `GfVec3f`)
- Lazy allocation - maps created on first use (lines 1612-1615, 1658-1661)
- Cleared after write via `Clear()` method (lines 1724-1731)

---

## Value Classification

### Category 1: Always Inlined (No Dedup Needed)

**Definition** (lines 239-246):
```cpp
template <class T>
struct _IsAlwaysInlined : std::integral_constant<
    bool, sizeof(T) <= sizeof(uint32_t) && _IsBitwiseReadWrite<T>::value> {};

// Special cases always inlined:
template <> struct _IsAlwaysInlined<string> : std::true_type {};
template <> struct _IsAlwaysInlined<TfToken> : std::true_type {};
template <> struct _IsAlwaysInlined<SdfPath> : std::true_type {};
template <> struct _IsAlwaysInlined<SdfAssetPath> : std::true_type {};
```

**Examples**:
- `bool`, `uint8_t`, `int32_t`, `float` (≤4 bytes + bitwise)
- `string`, `TfToken`, `SdfPath`, `SdfAssetPath` (via index lookup)

**Storage**: Value stored directly in `ValueRep.payload` (32 bits for small types, index for strings/tokens/paths)

**Structural Dedup**: While inlined in ValueReps, the underlying strings/tokens/paths are still deduplicated in their respective tables.

### Category 2: Conditionally Inlined

Some values of a type might fit in 4 bytes even if the type is larger.

**Implementation** (lines 1602-1609):
```cpp
// Try to encode value in 4 bytes
uint32_t ival = 0;
if (_EncodeInline(val, &ival)) {
    auto ret = ValueRepFor<T>(ival);
    ret.SetIsInlined();
    return ret;  // No dedup needed
}
```

**Use Case**: Optimizes storage for values that happen to be small, even if the type allows larger values.

### Category 3: Value-Deduplicated

Values too large to inline are deduplicated.

**Pack Algorithm** (lines 1611-1625):
```cpp
// Lazy allocate dedup map
if (!_valueDedup) {
    _valueDedup.reset(new typename decltype(_valueDedup)::element_type);
}

// Try to insert value
auto iresult = _valueDedup->emplace(val, ValueRep());
ValueRep &target = iresult.first->second;

if (iresult.second) {
    // First occurrence - write to file
    target = ValueRepFor<T>(writer.Tell());
    writer.Write(val);
}

return target;  // Existing or new offset
```

**How It Works**:
1. Hash the value and check map
2. If **new**: Write to file, store offset in map
3. If **duplicate**: Return existing offset
4. All duplicates reference same file location

### Category 4: Array Deduplication

Arrays use a separate dedup map.

**Pack Algorithm** (lines 1651-1680):
```cpp
ValueRep PackArray(_Writer w, VtArray<T> const &array) {
    auto result = ValueRepForArray<T>(0);

    // Empty arrays always inlined (payload = 0)
    if (array.empty())
        return result;

    // Check array dedup map
    if (!_arrayDedup) {
        _arrayDedup.reset(new typename decltype(_arrayDedup)::element_type);
    }

    auto iresult = _arrayDedup->emplace(array, result);
    ValueRep &target = iresult.first->second;

    if (iresult.second) {
        // First occurrence - write array
        if (writeVersion < Version(0,5,0)) {
            // Old format
        } else {
            // Possibly compressed
            target = _WritePossiblyCompressedArray(w, array, writeVersion, 0);
        }
    }
    return target;
}
```

**Special Cases**:
- Empty arrays: Always inlined with payload=0 (lines 1654-1656)
- Compressed arrays: Deduped at compressed representation level (lines 1675-1676)

---

## Implementation Details

### ValueRep Structure

The `ValueRep` is the core structure storing value references:

```cpp
struct ValueRep {
    uint64_t payload;  // Offset in file OR inlined value
    TypeEnum type;
    bool isInlined;
    bool isArray;
    bool isCompressed;
};
```

**Usage**:
- **Inlined**: `payload` contains the value directly (or index)
- **Not inlined**: `payload` contains file offset
- **Dedup benefit**: Multiple ValueReps can share same offset

### Hashing Strategy

Dedup maps use `_Hasher` (line 1013-1014, 1733):

```cpp
std::unordered_map<T, ValueRep, _Hasher>
```

**Requirements for Type T**:
- Must be hashable via `_Hasher`
- Must have equality comparison
- Must be copyable (for map storage)

### Memory Management

**Lazy Allocation** (lines 1612-1615):
```cpp
if (!_valueDedup) {
    _valueDedup.reset(new typename decltype(_valueDedup)::element_type);
}
```
- Maps only created when first non-inlined value encountered
- Reduces memory for files with only inlined values

**Cleanup** (lines 1724-1731):
```cpp
void Clear() {
    if constexpr (!_IsAlwaysInlined<T>::value) {
        _valueDedup.reset();
    }
    if constexpr (_SupportsArray<T>::value) {
        _arrayDedup.reset();
    }
}
```

---

## Deduplication Workflow

### Write Phase

```
1. CrateFile::_PackValue(VtValue)
   ↓
2. Determine type T from VtValue
   ↓
3. Get _ValueHandler<T> for this type
   ↓
4. Check if value can be inlined
   ↓
   YES → Store in ValueRep.payload (4 bytes)
   ↓
   NO → Check _valueDedup map
         ↓
         EXISTS → Return existing ValueRep with offset
         ↓
         NEW → Write value, store offset in map
               ↓
               Return new ValueRep
```

### Read Phase

```
1. CrateFile::UnpackValue(ValueRep)
   ↓
2. Check ValueRep.isInlined
   ↓
   YES → Extract value from payload
   ↓
   NO → Seek to payload offset
        ↓
        Read value from file
```

**Key Insight**: Dedup is transparent to readers - they just follow offsets.

---

## Array Compression Integration

### Combined Optimization (Version 0.5.0+)

Arrays can be both **deduplicated** and **compressed** (lines 1673-1677):

```cpp
if (writeVersion >= Version(0,5,0)) {
    target = _WritePossiblyCompressedArray(w, array, writeVersion, 0);
}
```

**Compression Types** (lines 1786-1893):

1. **Integer Compression** (int, uint, int64, uint64)
   - Uses `Sdf_IntegerCompression` / `Sdf_IntegerCompression64`
   - Minimum array size: 16 elements (line 1740)

2. **Float Compression** (GfHalf, float, double)
   - **As Integers**: If all values exactly representable as int32 (lines 1828-1848)
   - **Lookup Table**: If few distinct values (<1024, ≤25% of size) (lines 1850-1886)
   - **Uncompressed**: Otherwise

3. **Other Types**: Uncompressed

**Dedup + Compression**:
- Arrays deduplicated at **compressed representation** level
- Two identical arrays compressed the same way → same offset
- Different compression of same logical array → different entries (rare)

---

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Lookup in dedup map | O(1) average | Hash map lookup |
| Insert in dedup map | O(1) average | Hash map insert |
| Hash computation | O(n) | n = value size (array length, etc.) |
| Write value | O(n) | Only on first occurrence |

### Space Complexity

**Memory Overhead**:
- Per type: `sizeof(unordered_map) + entries * (sizeof(T) + sizeof(ValueRep))`
- For large arrays: Can be significant
- Mitigated by: Lazy allocation, cleared after write

**File Size Savings**:
- Highly data-dependent
- Best case: Many duplicates → linear reduction
- Worst case: All unique → small overhead (map structure)

### Real-World Benefits

**High Dedup Scenarios**:
1. **Tokens/Strings**: USD uses many repeated property names, type names
2. **Paths**: Hierarchical paths share prefixes (deduplicated)
3. **Default Values**: Many attributes share defaults (e.g., `GfVec3f(0,0,0)`)
4. **Time Samples**: Common time arrays across multiple attributes
5. **Metadata**: Repeated dictionary entries

**Low Dedup Scenarios**:
1. Unique geometry data (positions, normals)
2. Random/noise values
3. Unique identifiers

---

## Code Examples

### Example 1: String Deduplication

```cpp
// Writing three properties with same string value
crate->Set(path1, "documentation", VtValue("Hello"));  // Written at offset 1000
crate->Set(path2, "documentation", VtValue("Hello"));  // Reuses offset 1000
crate->Set(path3, "comment", VtValue("Hello"));        // Reuses offset 1000

// File contains "Hello" exactly once
```

**Process**:
1. First "Hello" → Added to `stringToStringIndex` → StringIndex(42)
2. Second "Hello" → Found in map → StringIndex(42)
3. String written once to STRINGS section

### Example 2: Array Deduplication

```cpp
VtArray<float> zeros(1000, 0.0f);

crate->Set(path1, "data", VtValue(zeros));  // Compressed as integers, offset 5000
crate->Set(path2, "data", VtValue(zeros));  // Reuses offset 5000
crate->Set(path3, "data", VtValue(zeros));  // Reuses offset 5000

// Array written and compressed exactly once
```

**Process**:
1. First array → Compressed via integer encoding → Write at 5000
2. Insert into `_arrayDedup[zeros]` → ValueRep(offset=5000, compressed=true)
3. Subsequent arrays → Map lookup → Same ValueRep

### Example 3: VtValue Recursion

For nested VtValues (e.g., VtValue containing VtDictionary containing VtValues):

```cpp
// Prevent infinite recursion (lines 1239-1253)
auto &recursionGuard = _LocalUnpackRecursionGuard::Get();
if (!recursionGuard.insert(rep).second) {
    TF_RUNTIME_ERROR("Recursive VtValue detected");
    return VtValue();
}
result = crate->UnpackValue(rep);
recursionGuard.erase(rep);
```

**Protection**: Thread-local set prevents circular references in corrupt files.

---

## Version History Impact

### Version 0.0.1 → 0.5.0
- Basic deduplication
- Arrays stored uncompressed
- 32-bit array sizes

### Version 0.5.0
- **Integer array compression** (lines 1786-1809)
- Dedup maps store compressed representations
- No rank storage for arrays (always 1D)

### Version 0.6.0
- **Float array compression** (lines 1811-1893)
- Lookup table encoding
- Integer encoding for floats

### Version 0.7.0
- **64-bit array sizes** (lines 1799-1801, 1837-1839)
- Enables larger arrays
- Dedup still works with larger arrays

### Version 0.8.0+
- SdfPayloadListOp deduplication (lines 1485-1491)
- Layer offset support in payloads

---

## Thread Safety

### Write Path
**Not Thread-Safe** - Single-threaded packing:
- `_PackingContext` populated serially (parallel initialization, lines 917-973)
- Dedup maps modified during sequential value packing
- `_BufferedOutput` uses `WorkDispatcher` for async writes

### Read Path
**Thread-Safe with Caveats**:
- Immutable structures after file open
- `_sharedTimes` dedup uses `tbb::spin_rw_mutex` (lines 1267-1288)
- Zero-copy arrays: Concurrent reads safe, but mapping destruction requires synchronization

---

## Zero-Copy Integration

### Zero-Copy Deduplication (lines 1912-1963)

Arrays can be **deduplicated** at the ValueRep level while still supporting **zero-copy** reads:

```cpp
if (zeroCopyEnabled &&
    numBytes >= MinZeroCopyArrayBytes &&  // ≥2048 bytes
    /* properly aligned */) {

    void const *addr = reader.src.TellMemoryAddress();
    *out = VtArray<T>(
        foreignSrc,
        static_cast<T *>(const_cast<void *>(addr)),
        size, /*addRef=*/false);
}
```

**Key Points**:
- Multiple ValueReps can point to same mmap region
- `_FileMapping::_Impl::ZeroCopySource` tracks outstanding references (lines 460-471)
- On mapping destruction, copy-on-write detachment (lines 490-523)

**Dedup Benefit**: Multiple attributes with same large array share:
1. Single file offset (dedup)
2. Single mmap region (zero-copy)
3. Minimal memory overhead

---

## Environment Variables

### USDC_ENABLE_ZERO_COPY_ARRAYS (lines 127-132)
```cpp
TF_DEFINE_ENV_SETTING(
    USDC_ENABLE_ZERO_COPY_ARRAYS, true,
    "Enable the zero-copy optimization for numeric array values...");
```
**Impact on Dedup**: Disabled zero-copy still benefits from dedup (reads from same offset).

### USD_WRITE_NEW_USDC_FILES_AS_VERSION (lines 111-117)
**Impact on Dedup**: Older versions have fewer compression options, affecting array dedup effectiveness.

---

## Limitations and Edge Cases

### 1. Type Granularity
Dedup is **per-type**: `VtArray<int>` and `VtArray<float>` use separate maps even if values numerically identical.

### 2. Floating Point Precision
IEEE floats: `0.0` and `-0.0` are distinct in memory but may hash the same - implementation dependent.

### 3. Compression Variance
Same array might compress differently based on:
- Version flags
- Size thresholds
- Content patterns

This can prevent dedup of logically identical arrays.

### 4. Map Memory Growth
For files with many unique large arrays, dedup maps can consume significant RAM during write.

### 5. No Inter-File Dedup
Each file write creates fresh dedup maps. Common values across files stored separately.

---

## Best Practices

### For USD Authors

1. **Reuse Values**: Prefer referencing same value objects rather than creating duplicates
2. **Common Defaults**: Use standard default values (0, 1, identity matrices) that dedup well
3. **Shared Time Samples**: Reuse time arrays across attributes when possible
4. **Token Interning**: Use TfToken for repeated strings

### For Implementation

1. **Monitor Memory**: Large dedup maps can OOM on huge files
2. **Version Selection**: Use latest version for best compression+dedup
3. **Profiling**: Check dedup effectiveness with file size metrics
4. **Clear Maps**: Ensure `Clear()` called after write to free memory

---

## Debugging and Diagnostics

### Checking Dedup Effectiveness

1. **Compare File Size**: Measure size with/without likely duplicates
2. **Section Sizes**: Inspect TOKENS/STRINGS sections for redundancy
3. **Memory Profiling**: Monitor `_valueDedup`/`_arrayDedup` sizes during write

### Common Issues

**Symptom**: File larger than expected
- **Cause**: Values not hashing/comparing correctly
- **Solution**: Verify `_Hasher` implementation for type

**Symptom**: High memory during write
- **Cause**: Too many unique large arrays
- **Solution**: Write in chunks, or accept lack of dedup

**Symptom**: Slow writes
- **Cause**: Hash computation expensive for large arrays
- **Solution**: Profile hash function, consider size limits

---

## Summary

The Crate deduplication system provides:

✅ **Multi-level dedup**: Structural (global) + Value (per-type)
✅ **Automatic**: Transparent to API users
✅ **Efficient**: O(1) lookup, lazy allocation
✅ **Integrated**: Works with compression and zero-copy
✅ **Versioned**: Evolves with format capabilities

**Result**: Significant file size reduction for typical USD data with shared tokens, paths, defaults, and time samples, while maintaining fast read/write performance.

---

## References

- **Source**: `pxr/usd/sdf/crateFile.cpp`
- **Key Types**: `_PackingContext`, `_ValueHandler<T>`, `ValueRep`
- **Key Methods**: `Pack()`, `PackArray()`, `_PackValue()`
- **Sections**: TOKENS, STRINGS, FIELDS, FIELDSETS, PATHS, SPECS
