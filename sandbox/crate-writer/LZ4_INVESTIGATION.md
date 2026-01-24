# LZ4 Compression Investigation

**Date**: 2025-11-04

## Issue Summary

Crate-writer successfully generates USDC files with proper structure, but OpenUSD fails to decompress them with:
- LZ4 error code: -10 (LZ4_ERROR_GENERIC)
- "Corrupt path index in crate file (0 repeated)"

## OpenUSD Compression Formats

OpenUSD uses TWO different compression schemes in USDC files:

### 1. TfFastCompression (LZ4) - For text data
**Used in**: TOKENS, STRINGS sections
**Format**:
```
- 1 byte: chunk_count (0 = single chunk, N = multiple chunks)
- If chunk_count == 0:
  - LZ4 compressed data
- If chunk_count > 0:
  - For each chunk:
    - int32_t: compressed_size
    - LZ4 compressed data
```

**Implementation**: `pxr/base/tf/fastCompression.cpp`

**Compression**:
```cpp
// Single chunk mode (inputSize <= LZ4_MAX_INPUT_SIZE)
compressed[0] = 0;  // chunk count = 0
LZ4_compress_default(input, compressed+1, inputSize, maxSize);
```

**Decompression**:
```cpp
int nChunks = *compressed++;
if (nChunks == 0) {
    LZ4_decompress_safe(compressed, output, compressedSize-1, maxOutputSize);
}
```

### 2. Sdf_IntegerCompression - For integer arrays
**Used in**: PATHS section (pathIndexes, elementTokenIndexes, jumps), integer arrays
**Format**:
```
- uint64_t: compressed_size
- Compressed integer data (proprietary format)
```

**Implementation**: `pxr/usd/sdf/integerCoding.cpp`

**Usage in PATHS**:
```cpp
// Read format:
numPaths = Read<uint64_t>();
_CompressedIntsReader cr;
cr.Read(reader, pathIndexes.data(), numPaths);  // Reads uint64_t compressedSize internally
cr.Read(reader, elementTokenIndexes.data(), numPaths);
cr.Read(reader, jumps.data(), numPaths);
```

## Crate-Writer Implementation Analysis

### TOKENS Section (crate-writer.cc:537-574)

**Format Written**:
```cpp
1. uint64_t token_count
2. uint64_t uncompressed_size
3. uint64_t compressed_size
4. LZ4 compressed data (with chunk byte)
```

**OpenUSD Expected** (crateFile.cpp:3554-3595):
```cpp
1. uint64_t numTokens
2. uint64_t uncompressedSize    ✅ MATCHES
3. uint64_t compressedSize      ✅ MATCHES
4. LZ4 compressed data          ✅ MATCHES
```

✅ **TOKENS format is CORRECT**

### PATHS Section (crate-writer.cc:800-918)

**Format Written**:
```cpp
1. uint64_t path_count
2. For pathIndexes array:
   - uint64_t compressed_size
   - Compressed data (Usd_IntegerCompression)
3. For elementTokenIndexes array:
   - uint64_t compressed_size
   - Compressed data
4. For jumps array:
   - uint64_t compressed_size
   - Compressed data
```

**OpenUSD Expected** (crateFile.cpp:3708-3750):
```cpp
1. uint64_t numPaths
2. _CompressedIntsReader.Read() for pathIndexes
   - Reads uint64_t compressedSize internally
   - Reads compressed data
3. _CompressedIntsReader.Read() for elementTokenIndexes
4. _CompressedIntsReader.Read() for jumps
```

✅ **PATHS format is CORRECT**

## Hex Dump Comparison

### Crate-Writer Output (simple_output.usdc)

**TOKENS Section** (offset 72):
```
00000048  02 00 00 00 00 00 00 00  # token_count = 2
00000050  11 00 00 00 00 00 00 00  # uncompressed_size = 17
00000058  14 00 00 00 00 00 00 00  # compressed_size = 20
00000060  00 f0 02 74 65 73 74 41  # Chunk byte (0x00) + compressed data
          ^^
          Chunk count = 0 (single chunk) ✅
```

**PATHS Section** (offset 189):
```
000000bd  02 00 00 00 00 00 00 00  # path_count = 2
000000c5  07 00 00 00 00 00 00 00  # pathIndexes compressed_size = 7
000000cd  00 50 00 00 00 00 00 07  # Compressed integer data
```

### OpenUSD Reference (openusd_reference.usdc)

**TOKENS Section** (offset unknown, need to check):
Similar format with chunk byte = 0x00

**PATHS Section** (offset 783):
```
0000030f  0e 00 00 00 00 00 00 00  # numPaths = 14
00000317  0e 00 00 00 00 00 00 00  # compressed_size = 14
0000031f  15 00 00 00 00 00 00 00  # ??? Extra uint64_t = 21
00000327  00 f0 03 03 00 00 00 15  # Compressed data
```

**DISCREPANCY FOUND**: OpenUSD reference has an EXTRA uint64_t (value=21) before the compressed data!

## Root Cause Analysis

The error "Failed to decompress data, possibly corrupt? LZ4 error code: -10" suggests OpenUSD is trying to decompress data that isn't LZ4-compressed at all.

**Hypothesis**: The PATHS section is NOT using LZ4. It uses Sdf_IntegerCompression (a different algorithm).

But wait - the error message mentions LZ4 decompression failing. Let me check if there's confusion about which section is failing...

## Error Message Analysis

```
Error in 'TfFastCompression::DecompressFromBuffer' at line 102
Error in 'Sdf_CrateFile::_ReadCompressedPaths' at line 3735
```

The LZ4 decompression is called FROM `_ReadCompressedPaths`! This is strange because PATHS should use integer compression, not LZ4.

### Checking crateFile.cpp Line 3735

Need to see what's actually calling TfFastCompression in the PATHS reading code...

## Next Steps

1. ✅ Verify TOKENS section format matches OpenUSD exactly
2. ✅ Verify PATHS section uses Sdf_IntegerCompression (not LZ4)
3. ❌ **FIND**: Why is _ReadCompressedPaths calling TfFastCompression?
4. **TODO**: Check if there's a version-dependent format difference
5. **TODO**: Compare actual integer compression output byte-by-byte

## Potential Issues

### 1. Version Mismatch
Crate-writer targets v0.8.0, reference file might be different version.
- Check boot section version numbers
- Check format changes between versions

### 2. Integer Compression Implementation
TinyUSDZ's `Usd_IntegerCompression` might not match OpenUSD's `Sdf_IntegerCompression` exactly.
- Need to verify compression algorithm compatibility
- Check if encoding/decoding produce identical results

### 3. Path Tree Encoding
The `path-sort-and-encode-crate` library might have issues:
- Path sorting algorithm
- Tree encoding algorithm
- Index generation

## Conclusion

The LZ4 compression format in TOKENS appears correct. The issue is likely in:
1. Integer compression format mismatch in PATHS section
2. Path tree encoding producing invalid indices
3. Version-specific format differences

**Priority**: Investigate why OpenUSD is calling LZ4 decompression for PATHS when it should use integer decompression.
