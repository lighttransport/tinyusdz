# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TinyUSDZ is a secure, portable, and dependency-free C++14 library for USDZ/USDC/USDA (Universal Scene Description) files. It provides robust USD format parsing, scene graph manipulation, and rendering-friendly data conversion without requiring the full pxrUSD/OpenUSD library.

## Key Architecture Components

### Core Libraries
- **Core USD Engine** (`src/tinyusdz.*`): Main library for USD parsing and scene graph operations
- **Tydra** (`src/tydra/`): "Tiny Hydra" - scene graph converter optimized for renderers, OpenGL/Vulkan/WebGL
- **ASCII Parser** (`src/ascii-parser.*`): Hand-written USDA parser (no Bison/Flex dependency)
- **Crate Reader/Writer** (`src/crate-*`): Binary USDC format support
- **Stage** (`src/stage.*`): USD scene graph representation

### Supported USD Types
- **Geometry** (`src/usdGeom.*`): Mesh, Points, Curves, Primitives (Cube, Sphere, etc.)
- **Lighting** (`src/usdLux.*`): Various light types (Distant, Dome, Sphere, etc.)
- **Materials** (`src/usdShade.*`): UsdPreviewSurface, UsdUVTexture, material networks
- **Skeletal Animation** (`src/usdSkel.*`): Bones, skinning, blend shapes
- **Composition** (`src/composition.*`): USD composition arcs (references, payloads, variants)

### Format Support
- **USDA**: ASCII format (read/write)
- **USDC**: Binary "Crate" format (read/write)
- **USDZ**: Archive format with assets
- **Additional**: JSON export, OBJ import/export, MaterialX support

## Build System

### Primary Build Method
```bash
mkdir build && cd build
cmake ..
make
```

### CMake Configuration
- **Main CMakeLists.txt**: Comprehensive build configuration with extensive options
- **Bootstrap Scripts**: `scripts/bootstrap-cmake-*.sh` for various platforms
- **Build Options**: Over 40 CMake options for customization (see CMakeLists.txt lines 64-260)

### Key CMake Options
- `TINYUSDZ_WITH_TYDRA=ON`: Enable Tydra rendering interface
- `TINYUSDZ_WITH_C_API=ON`: Enable C API for language bindings
- `TINYUSDZ_WITH_PYTHON=ON`: Enable Python bindings
- `TINYUSDZ_WITH_OPENSUBDIV=ON`: Enable subdivision surface support
- `TINYUSDZ_PRODUCTION_BUILD=ON`: Production build (no debug output)
- `TINYUSDZ_BUILD_TESTS=ON`: Build test suite
- `TINYUSDZ_BUILD_EXAMPLES=ON`: Build examples

### Platform Support
- Linux (x86_64, ARM64, 32-bit)
- Windows (MSVC, MinGW, ARM64)
- macOS (Intel, Apple Silicon)
- iOS, Android
- WebAssembly (Emscripten, WASI)

## Testing

### Test Structure
- **Unit Tests**: `tests/unit/` with acutest framework
- **Integration Tests**: `tests/usda/` and `tests/usdc/` with Python runners
- **Fuzzing**: `tests/fuzzer/` for security testing
- **Example Tests**: Various example programs as integration tests

### Running Tests
```bash
# Build with tests enabled
cmake -DTINYUSDZ_BUILD_TESTS=ON ..
make

# Run main test suite
./test_tinyusdz

# Run unit tests
./unit-test-tinyusdz

# Run USDA parser tests
python3 tests/usda/unit-runner.py --app "./test_tinyusdz" --basedir "tests/usda"
```

## Development Guidelines

### Code Style
- **C++ Standard**: C++14 (strict mode)
- **Dependencies**: Minimal external dependencies (mostly header-only)
- **Security**: Memory-safe parsing, bounded memory usage
- **Cross-platform**: Extensive platform support

### Key Source Files
- `src/tinyusdz.hh`: Main public API header
- `src/stage.hh`: USD Stage (scene graph) representation
- `src/prim-types.hh`: USD primitive type definitions
- `src/value-types.hh`: USD value type system
- `src/tydra/render-data.hh`: Renderer-friendly data structures

### Common Development Tasks

#### Build for Development
```bash
# Basic build
scripts/bootstrap-cmake-linux.sh

# Build with all features
cmake -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON -DTINYUSDZ_WITH_TYDRA=ON ..
make
```

#### Add New USD Schema Support
1. Add primitive definitions to `src/prim-types.hh`
2. Update parsers in `src/ascii-parser.cc` and `src/crate-reader.cc`
3. Add serialization support in `src/usda-writer.cc`
4. Update Tydra converters in `src/tydra/` if needed
5. Add tests in `tests/usda/` and `tests/usdc/`

#### Integration as Library
```cmake
# In your CMakeLists.txt
add_subdirectory(path/to/tinyusdz)
target_link_libraries(your_app PRIVATE tinyusdz::tinyusdz_static)
target_include_directories(your_app PRIVATE "path/to/tinyusdz/src")
```

#### Python Development
```bash
# Build Python bindings
cmake -DTINYUSDZ_WITH_PYTHON=ON ..
make
```

#### Web/WASM Development
```bash
# Build for web
scripts/bootstrap-emscripten.sh
```

## Examples and Tools

### Key Examples
- `examples/api_tutorial/`: Basic TinyUSDZ API usage
- `examples/tydra_api/`: Tydra scene conversion API
- `examples/tusdcat/`: USD file inspection tool (like pxrUSD's usdcat)
- `examples/sdlviewer/`: Interactive USD viewer with SDL

### Tools
- **tusdcat**: Parse and print USD files, composition support
- **tydra_to_renderscene**: Convert USD to renderer-friendly format
- **save_usda**: Save USD files in ASCII format

## Special Considerations

### Memory Management
- Built-in memory limits (`max_memory_limit_in_mb`)
- Security-focused parsing (no crashes on malformed input)
- Bounded buffer usage for large files

### WebAssembly Support
- Full WASM support with Emscripten
- Three.js integration via web/ directory
- WASI support for sandboxed environments

### MCP (Model Context Protocol) Support
- Experimental HTTP server for AI agent integration
- Located in `src/tydra/mcp-server.*`
- Enable with `TINYUSDZ_WITH_MCP_SERVER=ON`

## Performance Notes

- Hand-written parsers for optimal performance
- Optional multi-threading support
- Memory-mapped file I/O where possible
- Optimized for mobile/embedded platforms

## Current Development Status

This is version 0.9.0 with robust parsing and basic composition support. The library is production-ready for loading and processing USD files. Active development includes enhanced composition features, MaterialX support, and improved Tydra rendering interface.
