# USDC JavaScript Parser

A pure JavaScript implementation of a USDC (USD Binary/Crate) parser, based on the C++ implementation in TinyUSDZ.

## Overview

USDC (Universal Scene Description Crate) is Pixar's binary format for USD files. This format provides efficient storage and fast loading of USD data through:

- Binary encoding of all data types
- LZ4 compression for large data sections
- Structured table-of-contents for random access
- Optimized layout for streaming and memory efficiency

## Features

- **Binary Reader**: Safe, bounds-checked binary data reading from ArrayBuffer/Uint8Array
- **Header Parsing**: Complete USDC file header validation and parsing
- **Table of Contents**: Section discovery and navigation
- **Data Types**: Support for all USD primitive types (bool, int, float, vectors, matrices, etc.)
- **Value Representation**: Decoding of USD's packed value representation format
- **Security**: Memory limits and bounds checking to prevent malicious file attacks
- **Cross-platform**: Works in both Node.js and browser environments

## Files

- `binary-reader.js` - Binary data reader utilities
- `usdc-parser.js` - Main USDC parser implementation  
- `usdc-value-reader.js` - Value deserialization system
- `usdc-test.js` - Test suite with mock data
- `usdc-example.js` - Usage examples and file analysis
- `README-USDC.md` - This documentation

## Usage

### Node.js

```javascript
const { UsdcParser } = require('./usdc-parser.js');
const fs = require('fs');

// Read USDC file
const fileData = fs.readFileSync('model.usdc');
const parser = new UsdcParser(fileData);

// Parse the file
const layer = parser.parse();

if (layer) {
    console.log('Parse successful!');
    console.log(`Tokens: ${layer.tokens.length}`);
    console.log(`Strings: ${layer.strings.length}`);
    console.log(`Fields: ${layer.fields.length}`);
    console.log(`Specs: ${layer.specs.length}`);
} else {
    parser.getErrors().forEach(err => console.error(err.toString()));
}
```

### Browser

```html
<script src="binary-reader.js"></script>
<script src="usdc-parser.js"></script>
<script src="usdc-value-reader.js"></script>
<script>
    // Load file via fetch or file input
    fetch('model.usdc')
        .then(response => response.arrayBuffer())
        .then(buffer => {
            const parser = new UsdcParser(buffer);
            const layer = parser.parse();
            // ... use layer
        });
</script>
```

## USDC File Structure

A USDC file consists of:

1. **Header** (24 bytes)
   - Magic: "PXR-USDC" (8 bytes)
   - Version: Major.Minor.Patch (8 bytes)
   - TOC Offset: Pointer to table of contents (8 bytes)

2. **Data Sections**
   - Variable-length sections containing compressed/uncompressed data
   - Common sections: TOKENS, STRINGS, FIELDS, FIELDSETS, SPECS, PATHS

3. **Table of Contents**
   - Located at end of file
   - Maps section names to file offsets and sizes

## Data Types Supported

### Primitive Types
- `bool`, `uchar`, `int`, `uint`, `int64`, `uint64`
- `half`, `float`, `double`
- `string`, `token`, `asset_path`

### Vector Types
- `vec2f`, `vec3f`, `vec4f` (32-bit float vectors)
- `vec2d`, `vec3d`, `vec4d` (64-bit double vectors)  
- `vec2i`, `vec3i`, `vec4i` (32-bit integer vectors)
- `vec2h`, `vec3h`, `vec4h` (16-bit half vectors)

### Matrix Types
- `matrix2d`, `matrix3d`, `matrix4d`

### Quaternions
- `quath`, `quatf`, `quatd`

### Complex Types
- `dictionary` - Key-value mappings
- `time_samples` - Time-varying data
- Various `list_op` types for USD composition

## Value Representation

USDC uses a 64-bit packed representation for values:

```
Bits 63-56: Flags (array, inlined, compressed)
Bits 55-48: Type ID
Bits 47-0:  Payload (data or offset)
```

Flags:
- Bit 63: Is Array
- Bit 62: Is Inlined (small values stored directly)
- Bit 61: Is Compressed (LZ4 compressed data)

## Security Features

The parser implements extensive security measures:

- **Memory Budget**: Configurable limit (default 2GB) to prevent OOM attacks
- **Bounds Checking**: All reads are bounds-checked
- **Size Limits**: Maximum limits on all data structures:
  - 64M tokens/strings
  - 256M fields/specs/paths
  - 64MB maximum string length
- **Recursion Limits**: Prevention of infinite recursion in value parsing
- **Invalid Data Handling**: Graceful handling of malformed files

## Current Limitations

1. **Advanced USD Features**: Missing composition, relationships, variants
2. **Value Reading**: Basic implementation - needs complete data section handling
3. **Complex Types**: Limited support for dictionaries and composition types
4. **Performance**: Not optimized for very large files
5. **Memory Mapping**: No support for streaming/lazy loading

## Example Output

```
=== USDC Layer Analysis ===

Layer Statistics:
  Tokens: 1247
  Strings: 234
  Fields: 3891
  Field Sets: 567
  Specs: 89
  Paths: 123
  Memory used: 1.23 MB

Sample Tokens:
  [0] "def"
  [1] "Mesh"
  [2] "points"
  [3] "faceVertexIndices"
  [4] "normals"

Sample Paths:
  [0] /
  [1] /World
  [2] /World/Geometry
  [3] /World/Geometry/Cube
```

## LZ4 Decompression

The parser includes a complete pure JavaScript LZ4 decoder:

```javascript
// LZ4 decompression is automatic in USDC parsing
const { UsdcParser } = require('./usdc-parser.js');
const parser = new UsdcParser(usdcFileData);
const layer = parser.parse(); // LZ4 decompression handled automatically

// Direct LZ4 usage if needed
const { lz4Decompress } = require('./lz4-decoder.js');
const decompressed = lz4Decompress(compressedData);
```

## Running Tests

```bash
cd sandbox/jsparser
node usdc-test.js    # Run test suite
node usdc-example.js # Run examples
```

## Debugging

The parser provides extensive debugging capabilities:

```javascript
// Enable debug output
const parser = new UsdcParser(buffer);
const layer = parser.parse();

// Check for errors and warnings
console.log('Errors:', parser.getErrors());
console.log('Warnings:', parser.getWarnings());

// Analyze binary data
const reader = new BinaryReader(buffer);
console.log(reader.hexDump(0, 256)); // First 256 bytes
```

## Performance Tips

1. **Memory Management**: Monitor `parser.memoryUsed` for large files
2. **Streaming**: For very large files, consider processing in chunks
3. **Lazy Loading**: Only parse sections you need
4. **Worker Threads**: Use Web Workers/Worker Threads for large files

## References

- [USD Documentation](https://openusd.org/)
- [TinyUSDZ Project](https://github.com/lighttransport/tinyusdz)
- [Pixar USD Crate Format](https://github.com/PixarAnimationStudios/USD)
- [LZ4 Compression](https://lz4.github.io/lz4/)

## License

Based on TinyUSDZ implementation, Apache 2.0 License.