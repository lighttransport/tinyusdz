# Crate-Writer Test Results

**Date**: 2025-11-04
**Branch**: crate-writer-2025

## Summary

Successfully completed crate-writer build integration and initial testing. The crate-writer successfully generates USDC files with proper structure, though compatibility issues remain with OpenUSD's decompression.

## Completed Tasks ✅

### 1. Fixed Build System
**Issue**: Undefined reference to `tinyusdz::crate::Section::Section()`

**Solution**:
- Added missing constructor implementation in `src/crate-format.cc:18-25`:
  ```cpp
  Section::Section(char const *name, int64_t start, int64_t size)
    : start(start), size(size) {
    memset(this->name, 0, sizeof(this->name));
    if (name) {
      strncpy(this->name, name, kSectionNameMaxLength);
    }
  }
  ```
- Updated `sandbox/crate-writer/CMakeLists.txt` to link against TinyUSDZ static library
- Successfully built `example_write` and `simple_write` executables

**Files Modified**:
- `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/crate-format.cc`
- `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/sandbox/crate-writer/CMakeLists.txt`

### 2. Investigated Value Type Error
**Error**: `"Unsupported value type for out-of-line storage"`

**Root Cause**:
- Occurs in `WriteValueData()` function (crate-writer.cc:2525)
- The function handles out-of-line values (arrays, dictionaries, ListOps, TimeSamples)
- `Specifier` enum type requires out-of-line storage but lacks a handler

**Supported Out-of-Line Types**:
- Scalar/vector arrays (int, float, double, etc.)
- Dictionaries
- PathListOp, ReferenceListOp, PayloadListOp
- VariantSelectionMap
- TimeSamples

**Unsupported**: Specifier enums, ListOps for integer types

### 3. Created Working Example
**Approach**: Created minimal example using only inline types to avoid unsupported features

**File**: `sandbox/crate-writer/examples/simple_write.cc`
- Single attribute spec with int32 default value
- No Specifier fields (causes unsupported type error)
- 495 byte output file

**Build**:
```bash
cd sandbox/crate-writer/build
cmake .. && make simple_write
```

**Run**:
```bash
./simple_write simple_output.usdc
```

**Output**:
```
Creating minimal USDC file: simple_output.usdc
File opened successfully
Added attribute: /testAttr = 42
Finalizing file...
File finalized successfully
SUCCESS: Created USDC file: simple_output.usdc
File size: 495 bytes
```

### 4. Validation Results

**Test Script**: `tests/test_openusd_validation.py`

**OpenUSD Version**: v0.25.8

**Validation Tests**:
- ✅ File Existence - PASSED
- ✅ USDC Format Header - PASSED (valid `PXR-USDC` magic bytes)
- ❌ Stage Opening - FAILED

**Errors**:
```
Error: Failed to decompress data, possibly corrupt? LZ4 error code: -10
Error: Corrupt path index in crate file (0 repeated)
Error: Failed to open layer @simple_output.usdc@
```

## File Structure Analysis

### Hex Dump (first 64 bytes):
```
00000000  50 58 52 2d 55 53 44 43  00 08 00 00 00 00 00 00  |PXR-USDC........|
00000010  27 01 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |'...............|
00000020  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000030  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```

### File Sections (from TOC):
- **TOKENS**: offset=72, size=44 bytes
- **STRINGS**: offset=116, size=8 bytes
- **FIELDS**: offset=124, size=41 bytes
- **FIELDSETS**: offset=165, size=24 bytes
- **PATHS**: offset=189, size=53 bytes
- **SPECS**: offset=242, size=53 bytes

### Header Validation:
- Magic: `PXR-USDC` ✅
- Version: 0.8.0 ✅
- TOC offset: 0x127 (295 bytes) ✅

## Issues Found

### 1. LZ4 Decompression Error
**Error Code**: -10 (LZ4_ERROR_GENERIC)

**Location**: OpenUSD's `TfFastCompression::DecompressFromBuffer()`

**Probable Cause**:
- Incorrect LZ4 compression format
- Missing or malformed compression chunk headers
- Incompatible compression parameters

**Debug Output Shows**:
```
CompressData: inputSize=17, compressedSize=19, maxCompressedSize=33
First 16 bytes after chunk byte: f0 02 74 65 73 74 41 74 74 72 00 64 65 66 61 75
Compressed blob size (with chunk byte) = 20 bytes
```

### 2. Path Index Corruption
**Error**: "Corrupt path index in crate file (0 repeated)"

**Location**: `Sdf_CrateFile::_ReadCompressedPaths()`

**Probable Cause**:
- Incorrect path tree encoding
- Missing or malformed path indices
- Compression/decompression mismatch

## Test Infrastructure

### OpenUSD Builds Available:
1. **No-Python Monolithic**: `dist_nopython_monolithic/` (47MB libusd_ms.so)
2. **Standard Python**: `dist/` (51 libraries)
3. **Monolithic Python**: `dist_monolithic/` (49MB libusd_ms.so)

### Test Files:
- **OpenUSD Reference Files** (valid):
  - `openusd_reference.usdc` - 1,162 bytes, 4 prims
  - `openusd_complex.usdc` - 1,682 bytes, 4 prims with animation
- **Crate-Writer Output** (has issues):
  - `simple_output.usdc` - 495 bytes, 1 attribute

## Next Steps for Debugging

### High Priority:
1. **Fix LZ4 Compression Format**
   - Compare compression output with OpenUSD reference files
   - Verify chunk byte format matches OpenUSD expectations
   - Check LZ4 compression level and parameters

2. **Fix Path Section Encoding**
   - Investigate path tree encoding in `path-sort-and-encode-crate` library
   - Compare PATHS section with OpenUSD reference files
   - Verify path indices are correctly generated

3. **Add Specifier Support**
   - Implement out-of-line serialization for Specifier enums
   - Add to `WriteValueData()` function after TimeSamples handler
   - Test with proper prim specs

### Medium Priority:
4. **Binary Format Comparison**
   - Hex dump comparison: crate-writer vs OpenUSD output
   - Section-by-section format validation
   - Identify all format discrepancies

5. **Add More Value Type Support**
   - IntListOp, UIntListOp, Int64ListOp, UInt64ListOp
   - Complex nested structures
   - All 50+ USD value types

### Low Priority:
6. **Round-Trip Testing**
   - Read USD → Write with crate-writer → Read with OpenUSD
   - Verify data integrity through full pipeline
   - Automated test suite

## Build Commands

### Build TinyUSDZ:
```bash
cd /mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/build
cmake .. -DTINYUSDZ_BUILD_TESTS=OFF -DTINYUSDZ_BUILD_EXAMPLES=OFF
make -j8 tinyusdz_static
```

### Build Crate-Writer:
```bash
cd /mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/sandbox/crate-writer/build
cmake ..
make -j8
```

### Run Examples:
```bash
cd tests
../build/simple_write simple_output.usdc
```

### Validate Output:
```bash
source ../../../aousd/setup_env_monolithic.sh
python3 test_openusd_validation.py simple_output.usdc
```

## Conclusions

### Achievements:
- ✅ Build system fixed and working
- ✅ Crate-writer generates files with valid structure
- ✅ All sections present (TOKENS, STRINGS, FIELDS, FIELDSETS, PATHS, SPECS)
- ✅ Proper file header and table of contents
- ✅ Test infrastructure complete

### Remaining Issues:
- ❌ LZ4 compression format incompatible with OpenUSD
- ❌ Path section encoding has issues
- ❌ Specifier enum not supported for out-of-line storage

### Status:
**Partially Functional** - The crate-writer successfully generates structurally valid USDC files, but format details prevent OpenUSD from reading them. The core infrastructure is working; refinement of compression and encoding is needed for full compatibility.
