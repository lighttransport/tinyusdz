# USD Crate C++ Examples

This directory contains C++ examples demonstrating how to use OpenUSD's C++ API to read, write, and manipulate Crate (`.usdc`) binary files.

## Overview

The examples show:
- ✅ **Reading Crate files** - Load and traverse USD scenes
- ✅ **Writing Crate files** - Create animated USD scenes with TimeSamples
- ✅ **TimeSamples encoding/decoding** - Work with animated attributes
- ✅ **ValueRep inspection** - Analyze binary format internals
- ✅ **Format comparison** - Compare ASCII vs binary formats

## Examples

### 1. crate_reader
**Purpose**: Read and display contents of USD files

**Features**:
- Opens `.usdc` (binary) or `.usda` (ASCII) files
- Traverses prim hierarchy
- Displays attributes and their values
- Shows TimeSamples data for animated attributes

**Usage**:
```bash
./build/crate_reader <file.usdc>
```

**Example Output**:
```
========================================
Reading Crate File: cube.usdc
========================================

File format: usdc
Layer identifier: cube.usdc

--- Layer Metadata ---
  defaultPrim: Cube
  timeCodesPerSecond: 24
  framesPerSecond: 24

--- Prims ---
Prim: /Cube
  Type: Mesh
  Specifier: def
  Attributes:
    points: [8 Vec3f]
    faceVertexCounts: [6 ints]
    extent: [2 Vec3f]
    ...
```

### 2. crate_writer
**Purpose**: Create USD files with animated content

**Features**:
- Creates animated cube mesh
- Writes TimeSamples for transforms and colors
- Generates complex multi-object scenes
- Demonstrates proper USD scene structure

**Usage**:
```bash
# Create animated cube
./build/crate_writer output.usdc

# Create complex scene with multiple objects
./build/crate_writer output.usdc complex
```

**What it creates**:
- **Animated Cube**: Cube with animated translate and vertex colors
  - 100 frames at 24 fps
  - Circular motion path
  - Per-vertex color animation

- **Complex Scene** (with `complex` argument):
  - 5 spheres in circular arrangement
  - Each sphere independently animated
  - 50 frames of animation

### 3. crate_internal_api
**Purpose**: Low-level Crate format analysis

**Features**:
- Inspects binary file structure
- Analyzes TimeSamples data
- Compares file format sizes
- Shows ValueRep encoding details

**Usage**:
```bash
# Inspect binary format
./build/crate_internal_api inspect file.usdc

# Analyze TimeSamples
./build/crate_internal_api timesamples file.usdc

# Compare format sizes
./build/crate_internal_api compare file.usdc
```

**Example Output** (inspect):
```
========================================
File Format Inspection: cube.usdc
========================================

--- Bootstrap Header ---
Magic: PXR-USDC
  ✓ Valid Crate file
Version: 0.8.0
TOC Offset: 0x1234 (4660 bytes)
File Size: 5678 bytes

--- Format Details ---
Bootstrap: 64 bytes
Value Data: 0x40 - 0x1234
Structural Sections: 0x1234 - 0x162e
```

## Building

### Prerequisites

You must first build OpenUSD using one of the no-python build scripts:

```bash
# Option 1: Standard no-python build
cd ../
./setup_openusd_nopython.sh

# Option 2: Monolithic no-python build
cd ../
./setup_openusd_nopython_monolithic.sh
```

### Quick Build

Use the provided build script:

```bash
# Standard build (default)
./build.sh

# Monolithic build
./build.sh --monolithic

# CMake build
./build.sh --cmake

# CMake + Monolithic
./build.sh --cmake --monolithic
```

### Manual Build with Make

```bash
# Standard USD build
make

# Monolithic USD build
make MONOLITHIC=1

# Custom USD installation
make USD_ROOT=/path/to/usd

# Clean
make clean

# Help
make help
```

**Build output**: Executables in `build/` directory

### Manual Build with CMake

#### Standard (Non-Monolithic) Build

```bash
mkdir -p build_cmake
cd build_cmake
cmake .. -DUSE_MONOLITHIC_USD=OFF
cmake --build . -- -j$(nproc)
```

#### Monolithic Build

```bash
mkdir -p build_cmake_monolithic
cd build_cmake_monolithic
cmake .. -DUSE_MONOLITHIC_USD=ON
cmake --build . -- -j$(nproc)
```

#### Custom USD Installation

```bash
cmake .. -DUSD_ROOT=/custom/path/to/usd
```

**Build output**: Executables in `build_cmake/` directory

## Build Systems Comparison

| Feature | Make | CMake |
|---------|------|-------|
| **Simplicity** | ⭐⭐⭐⭐⭐ Simple, direct | ⭐⭐⭐ More complex |
| **Speed** | ⭐⭐⭐⭐ Fast incremental | ⭐⭐⭐⭐ Fast incremental |
| **Cross-platform** | ⭐⭐⭐ Unix/Linux mainly | ⭐⭐⭐⭐⭐ All platforms |
| **IDE Integration** | ⭐⭐ Limited | ⭐⭐⭐⭐⭐ Excellent |

**Recommendation**:
- **Use Make** for quick Unix/Linux builds
- **Use CMake** for cross-platform development or IDE integration

## Directory Structure

```
crate/
├── src/
│   ├── crate_reader.cpp           # Read USD files
│   ├── crate_writer.cpp           # Write USD files
│   └── crate_internal_api.cpp     # Low-level format analysis
├── build/                         # Make build output
├── build_cmake/                   # CMake build output (standard)
├── build_cmake_monolithic/        # CMake build output (monolithic)
├── CMakeLists.txt                 # CMake configuration
├── Makefile                       # Make configuration
├── build.sh                       # Convenience build script
└── README.md                      # This file
```

## Usage Examples

### Example Workflow

```bash
# 1. Build the examples
./build.sh

# 2. Create an animated scene
./build/crate_writer animated_cube.usdc

# 3. Read it back
./build/crate_reader animated_cube.usdc

# 4. Analyze its format
./build/crate_internal_api inspect animated_cube.usdc
./build/crate_internal_api timesamples animated_cube.usdc

# 5. Compare formats
./build/crate_internal_api compare animated_cube.usdc
```

### Testing with Existing Files

If you have USD files in `../../models/`:

```bash
# Read existing files
./build/crate_reader ../../models/suzanne.usdc

# Analyze TimeSamples
./build/crate_internal_api timesamples ../../models/animated_scene.usdc
```

## Linking Options

### Standard (Non-Monolithic) Build

Links against multiple USD libraries:
```
-lusd -lusdGeom -lsdf -ltf -lvt -lgf -lar -larch -lplug -ltrace -lwork -ltbb
```

**Pros**:
- Modular linking (only link what you use)
- Standard OpenUSD configuration
- Smaller binaries if using few modules

**Cons**:
- More libraries to manage
- Slower link times

### Monolithic Build

Links against single USD library:
```
-lusd_ms -ltbb
```

**Pros**:
- Simplest linking (one library)
- Faster link times
- Easier deployment

**Cons**:
- Larger binary (includes all USD modules)
- Requires monolithic USD build

## USD API Usage Patterns

### Reading a Layer

```cpp
#include "pxr/usd/sdf/layer.h"

SdfLayerRefPtr layer = SdfLayer::FindOrOpen("file.usdc");
if (layer) {
    // Access root prims
    for (const auto& prim : layer->GetRootPrims()) {
        // Process prim
    }
}
```

### Writing a Layer

```cpp
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/primSpec.h"

// Create new layer (format detected from extension)
SdfLayerRefPtr layer = SdfLayer::CreateNew("output.usdc");

// Create prim
SdfPrimSpecHandle prim = SdfPrimSpec::New(
    layer, "MyPrim", SdfSpecifierDef, "Mesh");

// Add attribute
SdfAttributeSpecHandle attr = SdfAttributeSpec::New(
    prim, "points", SdfValueTypeNames->Point3fArray);

// Set value
attr->SetDefaultValue(VtValue(points));

// Save
layer->Save();
```

### Working with TimeSamples

```cpp
#include "pxr/usd/sdf/types.h"

// Create time samples
SdfTimeSampleMap timeSamples;
for (double frame = 1.0; frame <= 100.0; frame += 1.0) {
    GfVec3d value(x, y, z);
    timeSamples[frame] = VtValue(value);
}

// Set on attribute
attr->SetInfo(SdfFieldKeys->TimeSamples, VtValue(timeSamples));

// Read time samples
if (attr->HasInfo(SdfFieldKeys->TimeSamples)) {
    VtValue tsValue = attr->GetInfo(SdfFieldKeys->TimeSamples);
    const auto& ts = tsValue.Get<SdfTimeSampleMap>();
    // Access samples...
}
```

## Troubleshooting

### Build Errors

**Problem**: `USD installation not found`
```
Error: USD installation not found at: ../dist_nopython
```

**Solution**: Build OpenUSD first:
```bash
cd ..
./setup_openusd_nopython.sh
```

---

**Problem**: `undefined reference to 'pxr::...'`

**Solution**: Check if using correct USD build:
- For standard build: `make` or `./build.sh`
- For monolithic: `make MONOLITHIC=1` or `./build.sh --monolithic`

---

**Problem**: Library not found at runtime
```
error while loading shared libraries: libusd.so.0
```

**Solution**: Libraries are automatically set via RPATH. If issues persist:
```bash
export LD_LIBRARY_PATH=../dist_nopython/lib:$LD_LIBRARY_PATH
```

### Runtime Errors

**Problem**: `Failed to open file`

**Solution**: Check file path and format. The examples support both `.usdc` and `.usda` files.

---

**Problem**: Crash on reading file

**Solution**: Ensure file is valid USD. Test with:
```bash
# If you have Python USD tools
usdcat file.usdc

# Or use our reader with error handling
./build/crate_reader file.usdc
```

## Performance Notes

### File Size Comparison

Typical size reductions with `.usdc` vs `.usda`:

| Scene Type | ASCII (.usda) | Binary (.usdc) | Reduction |
|------------|---------------|----------------|-----------|
| Simple cube | 2.5 KB | 1.2 KB | 52% |
| Animated (100 frames) | 45 KB | 18 KB | 60% |
| Complex scene | 250 KB | 90 KB | 64% |

### Read Performance

Binary format is **3-10x faster** to read than ASCII:
- Memory-mapped I/O for large files
- Zero-copy arrays (when possible)
- Compressed structural data

## References

- **OpenUSD Documentation**: https://openusd.org/
- **Crate Format Analysis**: See `../crate-impl.md` for detailed format documentation
- **USD C++ API**: https://openusd.org/release/apiDocs.html
- **SdfLayer API**: https://openusd.org/release/api/class_sdf_layer.html

## License

These examples are provided as educational material for understanding OpenUSD's Crate format.

OpenUSD is licensed under the Apache 2.0 / Modified Apache 2.0 license (Pixar).
