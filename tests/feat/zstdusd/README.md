# Zstd USD Compression Feature Tests

This directory contains feature tests for zstd-compressed USD file support in TinyUSDZ.

## Overview

TinyUSDZ supports file-level zstd compression for USD files. This allows:
- Reading zstd-compressed USDA/USDC/USDZ files (automatic detection)
- Writing zstd-compressed USD files (via `USDWriteOptions` or `.zst` extension)

## Building

```bash
make
```

## Running Tests

```bash
make test
```

Or directly:

```bash
./test_zstd_usd
```

## Test Cases

1. **Magic Number Detection** - Verifies zstd magic number detection
2. **Compression Round-trip** - Basic compress/decompress cycle
3. **Compression Levels** - Tests different compression levels (1-22)
4. **Corrupt Data Handling** - Error handling for invalid data
5. **GetCompressBound** - Tests the compress bound calculation
6. **USDA Round-trip** - Full USDA compression/decompression cycle
7. **IsZstdCompressed Wrapper** - Tests the tinyusdz namespace wrapper
8. **Load Compressed USDA from Memory** - Tests LoadUSDFromMemory with zstd data
9. **Memory Budget Enforcement** - Tests memory limit checking
10. **Write Compressed USDA** - Tests SaveAsUSDA with compression

## Requirements

- C++14 compiler
- TinyUSDZ source tree
- Zstd library (bundled in src/external/zstd.c)

## Notes

- The tests use the amalgamated single-file zstd library
- ZSTD_DISABLE_ASM=1 is set to avoid assembly-related linking issues
