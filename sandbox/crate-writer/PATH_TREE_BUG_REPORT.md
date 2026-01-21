# Path Tree Encoding Bug Report

**Date**: 2025-11-04
**Issue**: Duplicate path indices causing "0 repeated" error in OpenUSD
**Status**: ROOT CAUSE IDENTIFIED

## Summary

The crate-writer generates USDC files that fail OpenUSD validation with the error:
```
Corrupt path index in crate file (0 repeated)
```

After comprehensive investigation, I've identified **TWO bugs**:

### Bug #1: Missing uint64_t in PATHS Section ✅ FIXED

**Location**: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/sandbox/crate-writer/src/crate-writer.cc:824-838`

**Issue**: OpenUSD v0.4.0+ expects TWO uint64_t values at the start of the PATHS section:
1. Total number of paths (for resizing `_paths` vector)
2. Number of encoded paths in the tree structure

Crate-writer was only writing ONE value.

**OpenUSD Code Reference** (`crateFile.cpp:3637` and `3717`):
```cpp
// Line 3637: Read total paths and resize vector
_paths.resize(reader.template Read<uint64_t>());

// Line 3717: Read number of encoded paths for tree
size_t numPaths = reader.template Read<uint64_t>();
```

**Fix Applied**:
```cpp
// Write total path count
uint64_t path_count = static_cast<uint64_t>(tree.size());
if (!Write(path_count)) {
  if (err) *err = "Failed to write total path count";
  return false;
}

// Write the same value again for numEncodedPaths
if (!Write(path_count)) {
  if (err) *err = "Failed to write encoded path count";
  return false;
}
```

**Result**: File size increased from 495 to 503 bytes. Format now matches OpenUSD expectations.

### Bug #2: Duplicate Path Indices ❌ NOT FIXED

**Location**: `sandbox/path-sort-and-encode-crate` library (in `EncodePaths` function)

**Issue**: The `pathlib::EncodePaths()` function generates duplicate path indices.

**Debug Output**:
```
DEBUG: Path tree has 2 path indices:
  pathIndex[0] = 0
  pathIndex[1] = 0   <-- DUPLICATE!
```

**Expected**: Unique indices like `[0, 1]` or any distinct values

**OpenUSD Validation Code** (`crateFile.cpp:3731-3744`):
```cpp
vector<bool> seenIndexes(_paths.size());
for (const uint32_t pathIndex: pathIndexes) {
    if (pathIndex >= _paths.size() || seenIndexes[pathIndex]) {
        TF_RUNTIME_ERROR(
            "Corrupt path index in crate file (%u %s)",
            pathIndex,
            pathIndex >= _paths.size()
            ? TfStringPrintf(">= %zu", _paths.size()).c_str()
            : "repeated");   <-- TRIGGERS HERE
        return;
    }
    seenIndexes[pathIndex] = true;
}
```

**Root Cause**: The `path-sort-and-encode-crate` library's path tree encoding algorithm is incorrectly assigning the same index to different paths.

## Path Tree Encoding Background

USD uses a hierarchical path tree encoding where:
- Each path in the scene has a unique index into the `_paths` vector
- The tree structure encodes parent-child relationships
- Three arrays define the tree:
  1. **pathIndexes**: Where to store each decoded path in `_paths`
  2. **elementTokenIndexes**: The path component token for each node
  3. **jumps**: Tree traversal information (siblings/children)

**Critical Requirement**: `pathIndexes` must contain UNIQUE values (no duplicates allowed)

## Test Case

**Input Paths**:
```
/ (root)
/testAttr (attribute)
```

**Current (Buggy) Output**:
```
pathIndexes = [0, 0]  // WRONG - duplicates!
elementTokenIndexes = [0, 0]
jumps = [-1, -1]
```

**Expected Output** (example):
```
pathIndexes = [0, 1]  // Unique indices
elementTokenIndexes = [0, 1]  // Token indices for path components
jumps = [-1, -1]  // No siblings/children
```

## OpenUSD Reference Comparison

**OpenUSD Reference File** (14 paths):
```
Total paths: 14
Encoded paths: 14
PathIndexes compressed size: 21 bytes
Compressed data: 00 f0 03 03 00 00 00 15 45 41 05 00 01 07 f7 07 f7 04 f7 01 01
```

The compressed data encodes 14 **unique** path indices.

## Investigation Summary

### What Works ✅
1. ✅ LZ4 compression format (100% compatible with OpenUSD)
2. ✅ Integer compression format (Sdf_IntegerCompression)
3. ✅ File structure (header, boot, TOC, all sections)
4. ✅ PATHS section format (after fix)
5. ✅ SPECS section format
6. ✅ TOKENS, STRINGS, FIELDS, FIELDSETS sections

### What's Broken ❌
1. ❌ `path-sort-and-encode-crate` library generates duplicate path indices
2. ❌ Path tree encoding algorithm needs fixing

## Next Steps

### Priority 1: Fix path-sort-and-encode-crate Library

The bug is in: `sandbox/path-sort-and-encode-crate/src/tree-encode.cc`

**Required Fix**:
- Ensure each path in the input gets a unique index in the output
- Path indices should be 0..(numPaths-1) without duplicates
- Maintain correct parent-child relationships in the tree structure

### Priority 2: Add Unit Tests

Create tests for `path-sort-and-encode-crate`:
```cpp
// Test 1: Simple two-path case
input: ["/", "/testAttr"]
expected pathIndexes: [0, 1] (no duplicates)

// Test 2: Hierarchical paths
input: ["/", "/World", "/World/Cube"]
expected pathIndexes: [0, 1, 2] (no duplicates)

// Test 3: Multiple attributes
input: ["/", "/attr1", "/attr2", "/attr3"]
expected pathIndexes: [0, 1, 2, 3] (all unique)
```

### Priority 3: Validate Against OpenUSD

Once fixed, validate that:
1. OpenUSD can open the file without errors
2. Paths are correctly decoded
3. Scene hierarchy matches expectations

## Files Modified

1. ✅ `sandbox/crate-writer/src/crate-writer.cc:824-838` - Added missing uint64_t for PATHS section
2. ✅ `sandbox/crate-writer/tests/test_integer_compression.cc` - Created compression unit test
3. ✅ `sandbox/crate-writer/CMakeLists.txt` - Added test build configuration

## Documentation Created

1. `LZ4_INVESTIGATION.md` - Initial compression investigation
2. `COMPRESSION_ANALYSIS.md` - Detailed compression format analysis
3. `FINAL_ANALYSIS.md` - Comprehensive findings with test results
4. `PATH_TREE_BUG_REPORT.md` (this file) - Root cause analysis

## Conclusion

**The compression investigation is complete** - compression is working correctly.

**The root cause is identified** - `path-sort-and-encode-crate` library generates duplicate path indices.

**Next action required** - Fix the path tree encoding algorithm in the `path-sort-and-encode-crate` library to ensure unique path indices.
