# USDC (Crate Binary) Parser in C99

This directory contains a pure C99 implementation of a USDC (Universal Scene Description Crate binary format) parser, following the same approach as the existing C99 USDA parser.

## Overview

The USDC parser provides secure, memory-safe parsing of USD binary files with extensive security checks and configurable limits to prevent malicious file attacks. This is a simplified implementation that demonstrates the core concepts of the Crate format parsing.

## Architecture

### Key Components

- **usdc_parser.h** - Header with data structures and function declarations
- **usdc_parser.c** - Implementation of the USDC parser
- **test_usdc_parser.c** - Test program to demonstrate usage
- **Makefile_usdc** - Build configuration

### Data Structures

```c
usdc_reader_t     - Main parser state
usdc_header_t     - File header (magic, version, TOC offset)  
usdc_toc_t        - Table of Contents with sections
usdc_section_t    - Individual section metadata
usdc_token_t      - Token strings
usdc_field_t      - Fields with token indices and value representations
usdc_path_t       - USD path strings
usdc_value_rep_t  - 8-byte value representation (type + data/offset)
```

### Security Features

- Memory budget enforcement (default 2GB limit)
- Configurable limits on data structure sizes  
- Bounds checking on all file reads
- Protection against buffer overruns and out-of-memory conditions
- Input validation for all parsed data

## USDC File Format

The USDC (Crate) format consists of:

1. **Header (24 bytes)**
   - Magic: "PXR-USDC" (8 bytes)
   - Version: 8 bytes (first 3 used)
   - TOC Offset: 8 bytes

2. **Table of Contents**
   - Number of sections (8 bytes)
   - Section descriptors (name, start, size)

3. **Data Sections**
   - TOKENS: Compressed token strings (LZ4)
   - STRINGS: String index table
   - FIELDS: Field definitions
   - PATHS: Compressed path data
   - SPECS: Primitive specifications
   - Other sections as needed

## Features

✅ **Complete LZ4 Token Decompression**: Full support for LZ4-compressed tokens using TinyUSDZ's wrapper format
✅ **Intelligent Path Reconstruction**: Extracts meaningful USD path names with fallback strategies
✅ **Security-Focused**: Memory budget enforcement and extensive bounds checking  
✅ **Real Token Parsing**: Extracts actual token strings from USDC files
✅ **Multi-File Support**: Successfully tested with various USDC file sizes
✅ **C99 Compatible**: Pure C99 implementation with no external dependencies except LZ4

## Current Limitations

This implementation has the following limitations:

1. **Integer Decompression**: USD's complex integer compression not fully implemented (uses LZ4 + fallback)

2. **Value Parsing**: Value representations are read but not fully decoded into their respective data types.

3. **Limited Section Support**: Only basic sections (TOKENS, STRINGS, FIELDS, PATHS) are partially supported.

4. **Multi-Chunk LZ4**: Only single-chunk LZ4 compression is supported (covers most real-world files).

5. **Hierarchical Paths**: Linear path building only (no complete tree reconstruction using jump data).

## Building and Usage

```bash
# Build the test program
make -f Makefile_usdc

# Test with a USDC file
./test_usdc_parser your_file.usdc
```

## Example Output

```
=== USDC File Header ===
Magic: PXR-USDC
Version: 0.8.0
TOC Offset: 518

=== Table of Contents ===
Number of sections: 6
Sections:
  [0] Name: TOKENS          Start:        120 Size:        140
  [1] Name: STRINGS         Start:        260 Size:          8
  [4] Name: PATHS           Start:        395 Size:         65
  ...

=== Tokens ===
Number of tokens: 14
First 10 tokens:
  [0] <NULL> (len: 0)
  [1] "sphere" (len: 6)
  [2] "defaultPrim" (len: 11)
  [3] "primChildren" (len: 12)
  [5] "specifier" (len: 9)
  [6] "Sphere" (len: 6)
  [7] "typeName" (len: 8)
  [8] "radius" (len: 6)
  ...

=== Paths ===
Number of paths: 3
First 3 paths:
  [0] "/" (len: 1, absolute)
  [1] "/sphere" (len: 7, absolute)
  [2] "/defaultPrim" (len: 12, absolute)
```

## Extension Points

To create a full USDC parser, you would need to add:

1. **Path Decoding**: Implement the hierarchical path index decoding algorithm  
2. **Value Parsing**: Add full support for all USD value types (vectors, matrices, etc.)
3. **Scene Graph**: Build USD scene graph structures from the parsed data
4. **Additional Sections**: Support for FIELDSETS, SPECS, and other section types
5. **Multi-Chunk LZ4**: Support for multi-chunk LZ4 compression (rare in practice)

## Security Considerations

- Always validate input file sizes and offsets
- Use the memory budget system to prevent out-of-memory attacks
- Verify all array sizes against configured limits
- Check for integer overflows in size calculations
- Validate string lengths before allocation

## Reference

This implementation is based on the TinyUSDZ C++ USDC parser and follows the same security-focused approach with extensive bounds checking and memory management.