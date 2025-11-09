# Experimental USDC (Crate) File Writer

**Status**: Experimental / Bare Framework
**Version**: 0.1.0
**Target Crate Format**: 0.8.0 (stable, production-ready)

## Overview

This is an experimental bare-bones framework for writing USD Layer/PrimSpec data to USDC (Crate) binary format in TinyUSDZ. It implements the core structure of the Crate format without advanced optimizations.

### What's Implemented ✅

- **Bootstrap Header**: 64-byte header with "PXR-USDC" magic identifier
- **Table of Contents**: Section directory structure
- **Structural Sections**:
  - `TOKENS` - Token string pool (null-terminated blob)
  - `STRINGS` - String → token index mappings
  - `FIELDS` - Field name + value pairs
  - `FIELDSETS` - Lists of field indices
  - `PATHS` - Compressed path tree (using path-sort-and-encode library)
  - `SPECS` - Spec data (path, fieldset, type)
- **Deduplication**: Tokens, strings, paths, fields, fieldsets
- **Value Inlining**: Basic types (int32, uint32, float, bool)
- **Path Sorting**: Integration with `sandbox/path-sort-and-encode-crate` library

### What's NOT Implemented ⚠️ (Future Work)

- **Compression**: LZ4 compression for structural sections
- **Full Type Support**: Only basic inlined types currently
- **Out-of-line Values**: Complex types, arrays, dictionaries
- **Integer Compression**: Delta encoding for indices
- **Float Compression**: As-integer and lookup table encoding
- **Async I/O**: Buffered async writing
- **TimeSamples**: Animated attribute support
- **Zero-Copy**: Memory mapping optimizations
- **Validation**: Extensive error checking and safety

## Architecture

### File Format Structure

```
┌─────────────────────────────────────────┐
│ BootStrap (64 bytes)                    │ Offset: 0
│  - Magic: "PXR-USDC"                    │
│  - Version: [0, 8, 0]                   │
│  - TOC Offset                           │
├─────────────────────────────────────────┤
│ VALUE DATA (placeholder)                │ Future: out-of-line values
├─────────────────────────────────────────┤
│ TOKENS Section                          │
│  - Token count (uint64)                 │
│  - Token blob (null-terminated strings) │
├─────────────────────────────────────────┤
│ STRINGS Section                         │
│  - String count (uint64)                │
│  - TokenIndex array                     │
├─────────────────────────────────────────┤
│ FIELDS Section                          │
│  - Field count (uint64)                 │
│  - Field array (TokenIndex + ValueRep)  │
├─────────────────────────────────────────┤
│ FIELDSETS Section                       │
│  - FieldSet count (uint64)              │
│  - FieldIndex lists (null-terminated)   │
├─────────────────────────────────────────┤
│ PATHS Section                           │
│  - Path count (uint64)                  │
│  - PathIndex array (sorted, compressed) │
│  - ElementTokenIndex array              │
│  - Jump array                           │
├─────────────────────────────────────────┤
│ SPECS Section                           │
│  - Spec count (uint64)                  │
│  - Spec array (PathIndex + FieldSet +  │
│                SpecType)                │
├─────────────────────────────────────────┤
│ Table of Contents                       │ At offset from BootStrap
│  - Section count (uint64)               │
│  - Section entries (name, start, size)  │
└─────────────────────────────────────────┘
```

### Data Flow

```
1. Open()
   ├─ Create file
   └─ Write bootstrap placeholder (64 bytes)

2. AddSpec() × N
   ├─ Accumulate spec data
   ├─ Register paths (deduplication)
   └─ Register tokens (deduplication)

3. Finalize()
   ├─ Process all specs
   │  ├─ Build field tables
   │  ├─ Build fieldset tables
   │  └─ Pack values (inline or write to value data)
   │
   ├─ Write Structural Sections
   │  ├─ TOKENS (sorted token strings)
   │  ├─ STRINGS (token indices)
   │  ├─ FIELDS (deduplicated field data)
   │  ├─ FIELDSETS (deduplicated fieldset lists)
   │  ├─ PATHS (sorted and encoded path tree)
   │  └─ SPECS (spec data referencing above)
   │
   ├─ Write Table of Contents
   │  └─ Record all section offsets/sizes
   │
   └─ Write Bootstrap Header
      └─ Patch TOC offset into header

4. Close()
   └─ Finalize file I/O
```

## API Usage

### Basic Example

```cpp
#include "crate-writer.hh"

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Create writer
CrateWriter writer("output.usdc");

// Open file
std::string err;
if (!writer.Open(&err)) {
    std::cerr << "Failed to open: " << err << std::endl;
    return 1;
}

// Add root prim
Path root_path("/World", "");
crate::FieldValuePairVector root_fields;

crate::CrateValue specifier_value;
specifier_value.Set(Specifier::Def);
root_fields.push_back({"specifier", specifier_value});

writer.AddSpec(root_path, SpecType::PrimSpec, root_fields, &err);

// Add child prim
Path geom_path("/World/Geom", "");
crate::FieldValuePairVector geom_fields;

crate::CrateValue type_value;
type_value.Set(value::token("Xform"));
geom_fields.push_back({"typeName", type_value});

writer.AddSpec(geom_path, SpecType::PrimSpec, geom_fields, &err);

// Add attribute
Path attr_path("/World/Geom", "xformOp:translate");
crate::FieldValuePairVector attr_fields;

crate::CrateValue translate_value;
translate_value.Set(value::float3(1.0f, 2.0f, 3.0f));
attr_fields.push_back({"default", translate_value});

writer.AddSpec(attr_path, SpecType::AttributeSpec, attr_fields, &err);

// Finalize and write
if (!writer.Finalize(&err)) {
    std::cerr << "Failed to finalize: " << err << std::endl;
    return 1;
}

writer.Close();
```

### Configuration

```cpp
CrateWriter::Options opts;
opts.version_major = 0;
opts.version_minor = 8;  // Target version 0.8.0
opts.version_patch = 0;
opts.enable_compression = false;  // Not implemented yet
opts.enable_deduplication = true;

writer.SetOptions(opts);
```

## Dependencies

### Internal Dependencies

- `src/crate-format.hh` - Crate data structures (ValueRep, Index types, etc.)
- `src/prim-types.hh` - USD type definitions (Path, SpecType, etc.)
- `src/value-types.hh` - USD value types
- `sandbox/path-sort-and-encode-crate/` - Path sorting and tree encoding library

### External Dependencies

- C++17 standard library only (no external libs)

## Build

### Using CMake

```bash
cd sandbox/crate-writer
mkdir build && cd build
cmake ..
make
```

### Integration with TinyUSDZ

Add to your TinyUSDZ build:

```cmake
add_subdirectory(sandbox/crate-writer)
target_link_libraries(your_app tinyusdz crate-writer crate-encoding)
```

## Current Limitations

### Type Support

Currently only supports **inlined basic types**:
- `int32_t`, `uint32_t`
- `float`
- `bool`

**Not yet supported**:
- Strings, tokens, asset paths
- Vectors, matrices, quaternions
- Arrays
- Dictionaries
- ListOps
- TimeSamples
- Custom types

### No Compression

All sections written uncompressed. Future versions will add:
- LZ4 compression for structural sections
- Delta encoding for integer arrays
- Float compression strategies

### No Validation

Minimal error checking. Production version needs:
- Bounds checking
- Type validation
- Circular reference detection
- Corruption detection

### Performance

Not optimized for:
- Large files (>100MB)
- Many specs (>10K)
- Parallel writing

## Development Roadmap

### Phase 1: Core Types (Current)
- ✅ Basic file structure
- ✅ Path encoding integration
- ✅ Token/string/path deduplication
- ✅ Basic value inlining
- ⚠️ Limited type support

### Phase 2: Value System
- ⬜ Out-of-line value writing
- ⬜ String/Token value support
- ⬜ Vector/Matrix types
- ⬜ Array support
- ⬜ Dictionary support

### Phase 3: Compression
- ⬜ LZ4 structural compression
- ⬜ Integer delta encoding
- ⬜ Float compression strategies
- ⬜ Spec path sorting

### Phase 4: Advanced Features
- ⬜ TimeSamples support
- ⬜ ListOp support
- ⬜ Payload/Reference support
- ⬜ Async I/O
- ⬜ Validation and safety

### Phase 5: Production Ready
- ⬜ Comprehensive testing
- ⬜ Performance optimization
- ⬜ Memory efficiency
- ⬜ Error handling
- ⬜ Documentation

## Testing

### Manual Verification

Use OpenUSD tools to verify output:

```bash
# Dump crate file info
python3 /path/to/OpenUSD/pxr/usd/sdf/usddumpcrate.py output.usdc

# Convert to ASCII for inspection
usdcat output.usdc -o output.usda

# Validate file
usdchecker output.usdc
```

### Integration with TinyUSDZ

Read back the file using TinyUSDZ:

```cpp
tinyusdz::Stage stage;
std::string warn, err;
bool ret = tinyusdz::LoadUSDFromFile("output.usdc", &stage, &warn, &err);
```

## References

### Crate Format Documentation

- **`aousd/crate-impl.md`** - Comprehensive OpenUSD Crate format analysis
- **`aousd/paths-encoding.md`** - Path sorting and tree encoding details
- **`src/crate-format.hh`** - TinyUSDZ crate data structures

### Related Components

- **`sandbox/path-sort-and-encode-crate/`** - Path sorting/encoding library
- **`src/crate-reader.cc`** - TinyUSDZ crate reader (reference)
- **OpenUSD source**: `pxr/usd/sdf/crateFile.cpp` (lines 4293, full implementation)

## License

Apache 2.0

## Contributing

This is experimental code. Feedback and contributions welcome!

Key areas needing work:
1. **Type system expansion** - Implement more USD types
2. **Compression** - Add LZ4 compression
3. **Value encoding** - Complete out-of-line value writing
4. **Testing** - Add comprehensive test suite
5. **Performance** - Optimize for production use

## Status Summary

| Feature | Status | Notes |
|---------|--------|-------|
| Bootstrap header | ✅ Complete | Magic, version, TOC offset |
| Table of Contents | ✅ Complete | Section directory |
| TOKENS section | ✅ Complete | Null-terminated string blob |
| STRINGS section | ✅ Complete | Token index array |
| FIELDS section | ✅ Complete | Field deduplication |
| FIELDSETS section | ✅ Complete | Fieldset deduplication |
| PATHS section | ✅ Complete | Uses path-encode library |
| SPECS section | ✅ Complete | Basic spec writing |
| Value inlining | ⚠️ Partial | int32, uint32, float, bool only |
| Out-of-line values | ❌ TODO | Placeholder only |
| Compression | ❌ TODO | All sections uncompressed |
| Full type support | ❌ TODO | Only basic types |
| TimeSamples | ❌ TODO | Not implemented |
| Validation | ❌ TODO | Minimal error checking |
| Performance | ❌ TODO | Not optimized |

**Overall**: Functional bare framework, suitable for simple USD files with basic types.
