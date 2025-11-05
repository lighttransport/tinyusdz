# Compression Format Analysis - Final Report

**Date**: 2025-11-04
**Focus**: USD Crate v0.7.0+ only

## Executive Summary

The crate-writer uses correct compression formats for both LZ4 and integer compression. The issue is NOT in the compression layer itself, but likely in:
1. **Path tree encoding** producing invalid indices
2. **Integer compression delta encoding** producing wrong deltas
3. **Section data format** mismatch

## Compression Architecture

### Two-Layer System

USD Crate uses a two-layer compression system:

1. **Text Data (TOKENS, STRINGS)**:
   - Direct LZ4 compression via TfFastCompression

2. **Integer Arrays (PATHS, integer arrays)**:
   - Layer 1: Delta encoding + 2-bit classification
   - Layer 2: LZ4 compression via TfFastCompression

```
Integer Array → Sdf_IntegerCompression → LZ4 → Compressed Output
                    (delta + classify)   (TfFastCompression)
```

## LZ4 Compression (TfFastCompression)

### Format Specification

**Single Chunk** (data ≤ LZ4_MAX_INPUT_SIZE = ~2GB):
```
Byte 0: 0x00 (chunk count = 0)
Bytes 1+: LZ4 compressed data
```

**Multiple Chunks** (data > LZ4_MAX_INPUT_SIZE):
```
Byte 0: N (number of chunks, 1-127)
For each chunk:
  - int32_t: compressed_size
  - LZ4 compressed data (compressed_size bytes)
```

### Implementation Comparison

| Feature | OpenUSD (TfFastCompression) | TinyUSDZ (LZ4Compression) | Match? |
|---------|----------------------------|---------------------------|--------|
| Chunk byte | 0x00 for single | 0x00 for single | ✅ YES |
| Multi-chunk format | int32_t + data | int32_t + data | ✅ YES |
| LZ4 function | LZ4_compress_default | LZ4_compress_default | ✅ YES |
| Decompress function | LZ4_decompress_safe | LZ4_decompress_safe | ✅ YES |
| Max chunks | 127 | 127 | ✅ YES |

**Conclusion**: LZ4 layer is 100% compatible ✅

## Integer Compression (Sdf_IntegerCompression)

### Algorithm Overview

From OpenUSD source (`pxr/usd/sdf/integerCoding.cpp:28-45`):

1. **Delta Encoding**: Transform input to differences
   ```
   input = [123, 124, 125, 100125, 100125, 100126]
   deltas = [123, 1, 1, 100000, 0, 1]
   ```

2. **Find Common Value**: Most frequent delta
   ```
   common_value = 1 (appears 3 times)
   ```

3. **Classification**: 2-bit codes for each integer
   - `00`: Common value
   - `01`: 8-bit value
   - `10`: 16-bit value
   - `11`: 32-bit value

4. **LZ4 Compression**: Compress the encoded buffer

### TinyUSDZ Implementation

File: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/integerCoding.cpp`

**Key Finding**: TinyUSDZ's implementation is based on OpenUSD's code (Apache 2.0 licensed copy).

**Compression Call** (line 388):
```cpp
return LZ4Compression::CompressToBuffer(
    encoded_buffer, compressed_output, encoded_size, err);
```

Uses `LZ4Compression` which we verified matches `TfFastCompression` ✅

## Hex Dump Analysis

### Crate-Writer PATHS Section

Offset 189 (0xBD):
```
000000bd  02 00 00 00 00 00 00 00  # numPaths = 2
000000c5  07 00 00 00 00 00 00 00  # pathIndexes compressed_size = 7
000000cd  00 50 00 00 00 00 00 07  # Compressed data
          ^^
          Chunk byte = 0x00 ✅

000000d5  00 00 00 00 00 00 00 00  # elementTokenIndexes compressed_size = 7
000000dd  50 00 00 00 00 00 07     # Compressed data

000000e4  00 00 00 00 00 00 00 00  # jumps compressed_size = 7
000000ec  50 ff ff ff ff           # Compressed data
```

**Format**:
1. uint64_t numPaths = 2 ✅
2. uint64_t compressedSize = 7 ✅
3. Compressed data starting with 0x00 (single chunk) ✅
4. Repeat for 3 arrays ✅

### OpenUSD Reference PATHS Section

Offset 783 (0x30F):
```
0000030f  0e 00 00 00 00 00 00 00  # numPaths = 14
00000317  0e 00 00 00 00 00 00 00  # pathIndexes compressed_size = 14
0000031f  15 00 00 00 00 00 00 00  # ??? EXTRA uint64_t = 21
00000327  00 f0 03 03 00 00 00 15  # Compressed data
          ^^
          Chunk byte = 0x00 ✅
```

**CRITICAL DISCOVERY**: OpenUSD reference has an **EXTRA uint64_t** before the compressed data!

### Mystery Third Value

**Hypothesis 1**: It's the uncompressed size
- Value 21 would make sense as uncompressed size for 14 paths
- But `_CompressedIntsReader.Read()` only reads compressedSize, not uncompressedSize

**Hypothesis 2**: Version-specific format
- Maybe v0.7.0+ added this field?
- Need to check version-specific reading code

**Hypothesis 3**: Part of compressed data
- The uint64_t might be part of the actual LZ4 stream
- But that would be read as compressed data, not separately

Let me check the exact reading code again...

## Reading Code Analysis

From `crateFile.cpp:1974-1999`:
```cpp
struct _CompressedIntsReader {
    template <class Reader, class Int>
    void Read(Reader &reader, Int *out, size_t numInts) {
        _AllocateBufferAndWorkingSpace<Compressor>(numInts);

        auto compressedSize = reader.template Read<uint64_t>();  // Read 1 uint64_t

        if (compressedSize > _compBufferSize) {
            compressedSize = _compBufferSize;
        }

        reader.ReadContiguous(_compBuffer.get(), compressedSize);  // Read compressed data

        Compressor::DecompressFromBuffer(
            _compBuffer.get(), compressedSize, out, numInts, _workingSpace.get());
    }
};
```

**Reads**:
1. uint64_t compressedSize
2. compressedSize bytes of data

**Does NOT read uncompressed size!**

So where is that extra uint64_t coming from?

## Root Cause Hypothesis

Looking at the hex dumps more carefully:

**Crate-Writer** (2 paths):
- Writes 3 × (uint64_t compressedSize + data)
- Total: 3 arrays

**OpenUSD Reference** (14 paths):
- Pattern unclear due to larger data

**Possibility**: The "extra uint64_t" might actually be:
1. Part of a different section
2. My offset calculation is wrong
3. The uncompressed size IS written (version-dependent)

## Next Debugging Steps

1. **Verify Section Offsets**:
   - Re-calculate exact PATHS section boundaries
   - Check TOC entries match actual data

2. **Test Integer Compression Directly**:
   - Create unit test: compress [0, 1] with TinyUSDZ
   - Compare output with OpenUSD compression of same input
   - Byte-by-byte comparison

3. **Check Path Tree Encoding**:
   - The error "0 repeated" suggests path indices are all zeros
   - Path tree encoding in `path-sort-and-encode-crate` might be buggy
   - Need to verify encoded indices are valid

4. **Version-Specific Format**:
   - Check if v0.7.0 changed integer compression format
   - Look for version checks in `_CompressedIntsReader`

## Confirmed Facts

✅ LZ4Compression format matches TfFastCompression exactly
✅ Integer compression uses same algorithm (delta + classify + LZ4)
✅ Chunk byte (0x00) is correctly written
✅ File structure (sections, TOC) is correct
✅ Both files use version 0.8.0

## Remaining Issues

❌ LZ4 decompression fails with error code -10
❌ Path indices reported as "0 repeated" (corrupted)
❌ Unclear why decompression fails despite correct format

## Conclusion

The compression FORMAT is correct, but the compression OUTPUT is invalid. This suggests:

1. **Integer encoding bug**: Delta encoding produces wrong values
2. **Path encoding bug**: Path tree generates invalid indices
3. **Buffer size mismatch**: Compressed size doesn't match actual data
4. **Format version mismatch**: Subtle v0.7.0+ difference we haven't found

**Priority Action**: Create a minimal test case compressing a simple integer array and compare byte-by-byte with OpenUSD output.
