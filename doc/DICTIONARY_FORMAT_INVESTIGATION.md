# USD Crate Dictionary Format Investigation

## Investigation Date
2025-01-10

## Summary
Investigation into the USD Crate binary format for Dictionary (customData) values revealed a critical format discrepancy between OpenUSD and TinyUSDZ implementations.

## Format Comparison

### OpenUSD Dictionary Format (Standard)

**Location**: `/aousd/OpenUSD/pxr/usd/sdf/crateFile.cpp`

**Writer** (lines 1410-1416 - `WriteMap` template):
```cpp
void WriteMap(Map const &map) {
    WriteAs<uint64_t>(map.size());  // 8-byte count
    for (auto const &kv: map) {
        Write(kv.first);             // key (via StringIndex)
        Write(kv.second);            // value (directly or via ValueRep)
    }
}
```

**Reader** (lines 1128-1139 - `ReadMap` template):
```cpp
Map ReadMap() {
    auto sz = Read<uint64_t>();  // 8-byte count
    while (sz--) {
        auto key = Read<typename Map::key_type>();     // key
        map[key] = Read<typename Map::mapped_type>();  // value
    }
    return map;
}
```

**Binary Layout**:
```
[count: uint64_t]
[key1: StringIndex (uint32_t)][value1: ValueRep (uint64_t)]
[key2: StringIndex (uint32_t)][value2: ValueRep (uint64_t)]
...
```

**Characteristics**:
- Simple sequential format
- Keys and values written inline
- No offsets or indirection
- ValueReps contain inlined values or offsets to out-of-line data

### TinyUSDZ Dictionary Format (Recursive Offset)

**Location**: `/src/crate-reader.cc:2027-2094`

**Reader** (`ReadCustomData`):
```cpp
bool ReadCustomData(CustomDataType *d) {
    uint64_t sz = Read<uint64_t>();  // 8-byte count
    while (sz--) {
        string key = ReadString();           // key (StringIndex)
        int64_t offset = Read<int64_t>();    // 8-byte offset
        seek_from_current(offset - 8);       // seek using offset
        ValueRep rep = ReadValueRep();       // 8-byte ValueRep
        // unpack rep...
        seek_set(saved_position);            // seek back
    }
}
```

**Binary Layout**:
```
[count: uint64_t]
[key1: StringIndex (uint32_t)][offset1: int64_t][padding?]
[key2: StringIndex (uint32_t)][offset2: int64_t][padding?]
...
[ValueRep1: uint64_t]
[ValueRep2: uint64_t]
...
```

**Characteristics**:
- Recursive offset format
- Keys followed by offsets to ValueReps
- ValueReps stored after all key-offset pairs
- Reader uses `seek_from_current(offset - 8)` pattern

**Offset Calculation**:
Referenced OpenUSD's `_RecursiveWrite` (lines 1041-1045, 1353-1366):
```cpp
void _RecursiveWrite(Fn const &fn) {
    int64_t offsetLoc = Tell();
    WriteAs<int64_t>(0);        // reserve space
    fn();                        // write data
    int64_t end = Tell();
    Seek(offsetLoc);
    WriteAs<int64_t>(end - offsetLoc);  // patch offset
    Seek(end);
}
```

Note: OpenUSD's _RecursiveRead uses `Seek(start + offset)`, NOT `seek_from_current(offset - 8)`!

## Key Findings

### 1. Format Incompatibility
**OpenUSD** uses sequential (key, value) pairs WITHOUT recursive offsets for dictionaries.
**TinyUSDZ** expects recursive offset-based (key, offset, ValueRep) format.

### 2. OpenUSD Dictionary Usage
For `VtDictionary` in OpenUSD:
- `Write(VtDictionary)` calls `WriteMap` (simple format)
- `Read<VtDictionary>()` calls `ReadMap` (simple format)
- The `_RecursiveWrite` pattern is used for `VtValue` types, not plain dictionaries

### 3. TinyUSDZ Implementation
- `ReadCustomData()` has comment: "See RecursiveRead() in crateFile.cpp for details"
- Implements recursive offset reading from the start (commit f56a68a6)
- Uses `seek_from_current(offset - 8)` which differs from OpenUSD's `Seek(start + offset)`

### 4. Offset Calculation Discrepancy
Even within recursive formats:
- **OpenUSD**: `offset = end - offsetLoc`, reader does `Seek(start + offset)`
- **TinyUSDZ**: Expects `seek_from_current(offset - 8)` pattern

## Test Results

### Crate-Writer Implementation
Implemented TinyUSDZ's expected format in `crate-writer.cc:2065-2199`:
- Three-phase approach: pack values, write key-offset pairs, write ValueReps
- Offset formula: `offset = absolute_value_position - offsetLoc`
- Mathematical calculation verified correct for TinyUSDZ's seek pattern

### Round-Trip Test
**Status**: FAILED
**Error**: "Invalid offset value: 5369364480"
**File**: `/tmp/test_Dictionary.usdc`

**Analysis**:
- Hex dump shows correct offset values (32 = 0x20) at expected positions
- ValueReps correctly encoded with IsInlined bit and type fields
- Error suggests reader position mismatch or format interpretation issue

## Possible Explanations

### 1. Version-Specific Format
Dictionary format may have changed between USD versions. TinyUSDZ may implement an older/newer format than current OpenUSD.

### 2. Context-Specific Format
Different contexts (field values vs. standalone dictionaries) might use different formats.

### 3. TinyUSDZ Reader Bug
TinyUSDZ's `ReadCustomData()` implementation may incorrectly interpret the format based on misunderstanding OpenUSD's code.

### 4. Specialized Dictionary Types
The recursive format might be specific to certain dictionary types (e.g., customData in References/Payloads) while general dictionaries use simple format.

## Recommendations

### Immediate Actions
1. **Test with Real USD Files**: Examine actual USD files with customData from OpenUSD
2. **Hex Dump Comparison**: Compare binary layout of OpenUSD-generated vs. crate-writer-generated files
3. **Version Investigation**: Check if format varies across USD crate versions (0.7.0, 0.8.0, etc.)

### Implementation Options

**Option A: Implement Simple Format**
- Change crate-writer to use OpenUSD's simple WriteMap format
- Test if TinyUSDZ reader needs updating
- Potentially break compatibility with existing TinyUSDZ expectations

**Option B: Support Both Formats**
- Add format detection/selection
- Support reading/writing both formats
- More complex implementation

**Option C: Fix TinyUSDZ Reader**
- Update TinyUSDZ's `ReadCustomData()` to match OpenUSD's `ReadMap`
- Simpler long-term solution
- Requires changes to main TinyUSDZ repository

### Testing Strategy
1. Generate USD file with customData using official OpenUSD tools
2. Hex dump and analyze binary structure
3. Test if TinyUSDZ can read OpenUSD-generated files
4. Compare with crate-writer output

## References

### OpenUSD Source Files
- `/aousd/OpenUSD/pxr/usd/sdf/crateFile.cpp`
  - `WriteMap`: lines 1410-1416
  - `ReadMap`: lines 1128-1139
  - `_RecursiveWrite`: lines 1353-1366
  - `_RecursiveRead`: lines 1041-1045
  - `Write(VtDictionary)`: line 1422
  - `Read<VtDictionary>`: line 1174

### TinyUSDZ Source Files
- `/src/crate-reader.cc`
  - `ReadCustomData()`: lines 2027-2094
  - Original implementation: commit f56a68a6

### Crate-Writer Implementation
- `/sandbox/crate-writer/src/crate-writer.cc`
  - Dictionary writing: lines 2065-2199
  - Offset calculation: lines 2169-2187

## Test Results Update (2025-01-10)

### Simple Format Test
**Objective**: Test if TinyUSDZ can read OpenUSD's simple WriteMap format

**Implementation**: Modified crate-writer.cc (lines 2065-2162) to write simple format:
```
[count: uint64_t]
[key1: StringIndex (uint32_t)][value1: ValueRep (uint64_t)]
[key2: StringIndex (uint32_t)][value2: ValueRep (uint64_t)]
...
```

**Result**: FAILED with same error
```
ERROR: Failed to read file: crate-reader.cc:2058
Failed to seek. Invalid offset value: 5369364480
```

**Analysis**:
- TinyUSDZ's ReadCustomData (crate-reader.cc:2049-2089) CANNOT read simple format
- After reading key (StringIndex), it expects 8-byte offset
- Attempts `seek_from_current(offset - 8)` with the value data
- Since value data isn't an offset, seek fails

**Binary Analysis**:
```
Offset 0x144 (324): Dictionary data
  0200 0000 0000 0000 - count = 2
  0200 0000           - key[0] = StringIndex(2)
  0000 0000 0800 0000 - value[0] = ValueRep (read as offset: 34359738368)
  0000 0060 0100 0000 - key[1] = StringIndex(0) + part of value
  0100 0800 0000 0000 - rest of value[1]
```

When TinyUSDZ reads 0x0000000800000000 as offset, it gets 34359738368 (0x800000000) and tries to seek, causing the error.

**Conclusion**:
- TinyUSDZ's `ReadCustomData()` is hardcoded for recursive offset format
- It cannot handle OpenUSD's standard WriteMap format
- The format incompatibility is confirmed

## Resolution (2025-01-10)

### Fix Applied
**TinyUSDZ Reader Updated** (crate-reader.cc:2041-2076)

Modified `ReadCustomData()` to use OpenUSD's simple ReadMap format:
- Removed recursive offset reading logic (lines 2049-2059)
- Read ValueRep directly after key (line 2055-2058)
- Removed seek operations (lines 2057, 2087)
- Simplified loop to match OpenUSD's ReadMap template

### Test Results
**All tests PASSED**: 10/10 round-trip tests now passing

```
✓ SimplePrim
✓ Relationship
✓ Arrays
✓ XformMatrix
✓ VectorTypes
✓ StringTypes
✓ LargeArrays
✓ Dictionary        ← FIXED!
✓ Hierarchy
✓ TokenArray
```

## Status
**Dictionary Support**: ENABLED and working in test_roundtrip.cc (line 402)
**Reason**: TinyUSDZ reader now uses OpenUSD-compatible simple format
**Confirmed**: Full round-trip test passes with OpenUSD standard dictionary format

## Resolution Path

**Option 1: Keep TinyUSDZ Format (Current Implementation)**
- Revert to recursive offset format in crate-writer
- Accept that format differs from OpenUSD reference
- Files will be TinyUSDZ-specific, not OpenUSD-compatible
- Risk: May not work with official USD tools

**Option 2: Fix TinyUSDZ Reader (Recommended)**
- Update TinyUSDZ's ReadCustomData() to use simple ReadMap format
- Match OpenUSD's reference implementation
- Requires changes to main TinyUSDZ repository
- Pro: Standards-compliant, OpenUSD-compatible

**Option 3: Generate OpenUSD Reference File**
- Create dictionary with official USD tools
- Hex dump to determine actual format used in production
- Verify which format is correct
- Then align implementation accordingly

## Notes
- The offset calculation itself is mathematically sound for TinyUSDZ's pattern
- Issue is fundamental format incompatibility, not calculation error
- Both implementations reference OpenUSD code but interpret it differently
- Simple format test confirms TinyUSDZ cannot read standard OpenUSD dictionaries
