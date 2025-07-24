# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TinyUSDZ is a secure, portable, dependency-free C++14 library for parsing USD (Universal Scene Description) files in USDA (ASCII), USDC (binary/Crate), and USDZ (zip archive) formats. It's designed as a security-focused alternative to Pixar's pxrUSD library with minimal dependencies.

## Common Build Commands

### Linux/macOS Development
```bash
# Basic CMake build
mkdir build && cd build
cmake ..
make

# Or use the bootstrap script
./scripts/bootstrap-cmake-linux.sh
cd build && make

# Build with specific options
cmake -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON ..
```

### Windows Development
```bash
# Generate Visual Studio solution
vcsetup.bat

# For cross-compilation with clang-cl
bootstrap-clang-cl-win64.bat
ninja.exe
```

### Running Tests
```bash
# Run unit tests (after building with TINYUSDZ_BUILD_TESTS=ON)
./build/test_tinyusdz

# Run parse tests with Python runner
cd tests/parse_usd
python runner.py --path ../../models

# Run Tydra conversion tests
cd tests/tydra_to_renderscene  
python runner.py
```

### Build Examples
```bash
# Build specific examples
cd examples/sdlviewer
mkdir build && cd build
cmake ..
make

# Or enable examples in main build
cmake -DTINYUSDZ_BUILD_EXAMPLES=ON ..
```

## Code Architecture

### Core Library Structure (`src/`)

The library is organized into several key components:

**Parsers and Readers:**
- `ascii-parser.{hh,cc}` - Hand-written USDA ASCII format parser
- `crate-reader.{hh,cc}` - USDC binary (Crate) format parser  
- `usda-reader.{hh,cc}` - High-level USDA reading interface
- `usdc-reader.{hh,cc}` - High-level USDC reading interface

**Writers:**
- `usda-writer.{hh,cc}` - USDA ASCII format writer (production ready)
- `usdc-writer.{hh,cc}` - USDC binary format writer (work-in-progress)
- `crate-writer.{hh,cc}` - Low-level Crate binary format writer

**Core Data Types:**
- `tinyusdz.{hh,cc}` - Main API entry points and data loading functions
- `stage.{hh,cc}` - USD Stage class (similar to scene graph)
- `prim-types.{hh,cc}` - USD primitive type definitions
- `value-types.{hh,cc}` - USD value type system
- `usdGeom.{hh,cc}` - Geometry primitives (Mesh, Sphere, etc.)
- `usdShade.{hh,cc}` - Material and shader definitions
- `usdSkel.{hh,cc}` - Skeletal animation support

**Tydra Framework (`src/tydra/`):**
- `render-data.{hh,cc}` - Convert USD to OpenGL/Vulkan-friendly scene data
- `scene-access.{hh,cc}` - High-level scene traversal and query APIs
- `texture-util.{hh,cc}` - Texture loading and colorspace conversion

**Composition System:**
- `composition.{hh,cc}` - USD composition arcs (references, payloads, etc.)
- `asset-resolution.{hh,cc}` - Asset path resolution system

### Key Data Flow

1. **Loading**: `LoadUSDFromFile()` → format detection → parser → Stage object
2. **Composition**: Stage → composition system → flattened scene graph  
3. **Conversion**: Stage → Tydra → RenderScene (for rendering)
4. **Writing**: Stage → writer → output file

### Security Architecture

The library implements multiple security layers:
- Memory budget controls via `USDLoadOptions::max_memory_limit_in_mb`
- Bounds checking in all parsers (especially `crate-reader.cc`)
- Fuzzer-tested parsing code (`tests/fuzzer/`)
- No C++ exceptions used (uses `nonstd::expected` for error handling)

## Important Build Options

- `TINYUSDZ_PRODUCTION_BUILD=ON` - Disable debug logging for production
- `TINYUSDZ_WITH_OPENSUBDIV=ON` - Enable subdivision surface support
- `TINYUSDZ_WITH_TYDRA=ON` - Include Tydra conversion framework (default ON)
- `TINYUSDZ_WITH_AUDIO=ON` - Support audio file loading (mp3/wav)
- `TINYUSDZ_WITH_EXR=ON` - Enable EXR/HDR texture support via TinyEXR
- `TINYUSDZ_BUILD_TESTS=ON` - Build unit tests
- `TINYUSDZ_BUILD_EXAMPLES=ON` - Build example applications

## Working with USD Data

### Basic Loading Pattern
```cpp
tinyusdz::Stage stage;
std::string warn, err;
bool ret = tinyusdz::LoadUSDFromFile("model.usd", &stage, &warn, &err);
```

### Tydra Conversion Pattern  
```cpp
tinyusdz::tydra::RenderScene renderScene;
tinyusdz::tydra::RenderSceneConverter converter;
bool ret = converter.ConvertToRenderScene(stage, &renderScene);
```

### Security Considerations
- Always set memory limits when loading untrusted USD files
- Use WASM/WASI builds for maximum security isolation
- Enable fuzzing builds when developing new parsing features

## Project Structure Notes

- `models/` - Test USD files for development
- `examples/` - Standalone example applications with separate build systems
- `tests/` - Unit tests and parsing verification scripts
- `scripts/` - Build configuration scripts for various platforms
- `web/` - WebAssembly/JavaScript bindings and demos
- `python/` - Python binding code (experimental)