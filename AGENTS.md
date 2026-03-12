# AGENTS.md

Guidance for AI coding agents (Claude Code, Copilot, Cursor, etc.) working in this repository.

## Project Overview

TinyUSDZ is a secure, portable, dependency-free C++14 library for parsing and writing USD (Universal Scene Description) files in USDA (ASCII), USDC (binary/Crate), and USDZ (zip archive) formats. Security-focused alternative to Pixar's pxrUSD with minimal dependencies. No C++ exceptions; error handling via `nonstd::expected`.

## Repository Layout

```
src/                    Core library sources
  ascii-parser.{hh,cc}    Hand-written USDA parser
  crate-reader.{hh,cc}    USDC binary (Crate) reader
  crate-writer.{hh,cc}    USDC binary writer (experimental)
  usda-reader.{hh,cc}     High-level USDA reading
  usdc-reader.{hh,cc}     High-level USDC reading
  usda-writer.{hh,cc}     USDA writer (production)
  usdc-writer.{hh,cc}     USDC writer (experimental)
  pprinter.{hh,cc}        Pretty-printer (Stage/Prim -> USDA text)
  tinyusdz.{hh,cc}        Main API (LoadUSDFromFile, etc.)
  stage.{hh,cc}            USD Stage (scene graph)
  prim-types.{hh,cc}       Primitive type definitions
  value-types.{hh,cc}      Value type system
  composition.{hh,cc}      Composition arcs (references, payloads)
  usdGeom.{hh,cc}          Geometry prims (Mesh, Sphere, etc.)
  usdShade.{hh,cc}         Materials and shaders
  usdSkel.{hh,cc}          Skeletal animation
  tydra/                   Tydra framework (USD -> render-ready data)
    render-data.{hh,cc}      Convert Stage to OpenGL/Vulkan scene
    scene-access.{hh,cc}     Scene traversal and query APIs
    texture-util.{hh,cc}     Texture loading / colorspace
tests/
  unit/                  Acutest-based unit tests (unit-*.cc)
  usda/                  USDA test fixture files
  usdc/                  USDC test fixture files
  parse_usd/             Python-driven parse test runner
  tydra_to_renderscene/  Tydra conversion tests
  compare-usda.js        Roundtrip comparison (tusdcat vs usdcat)
  run-usdcat-compare.sh  Batch roundtrip test runner
examples/                Standalone example apps (separate builds)
  tusdcat/                 USD cat/dump tool
models/                  Test USD files for development
doc/                     Documentation
scripts/                 Build/bootstrap scripts for various platforms
web/                     WebAssembly/JavaScript bindings and demos
sandbox/                 Experimental tooling and prototypes
```

## Build Commands

```bash
# Native build (Linux/macOS)
mkdir build && cd build
cmake .. -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
make -j16

# Or use bootstrap script
./scripts/bootstrap-cmake-linux.sh
cd build && make -j16

# WASM build
cd web && mkdir build && cd build
emcmake cmake ..
make
```

Build folder: `build/` (native), `web/build/` (WASM).

### Key CMake Options

- `TINYUSDZ_BUILD_TESTS=ON` - Build unit tests
- `TINYUSDZ_BUILD_EXAMPLES=ON` - Build example apps
- `TINYUSDZ_PRODUCTION_BUILD=ON` - Disable debug logging
- `TINYUSDZ_WITH_TYDRA=ON` - Tydra framework (default ON)
- `TINYUSDZ_WITH_EXR=ON` - EXR/HDR texture support
- `TINYUSDZ_WITH_AUDIO=ON` - Audio file loading (mp3/wav)
- `TINYUSDZ_WITH_OPENSUBDIV=ON` - Subdivision surfaces

## Testing

```bash
# Run unit tests via ctest (from build/)
ctest --output-on-failure

# Run roundtrip comparison (tusdcat output vs pxrUSD usdcat)
USDCAT_PATH=~/local/USD/dist/bin/usdcat TUSDCAT_PATH=./build/tusdcat \
  bash tests/run-usdcat-compare.sh

# Compare individual file
node tests/compare-usda.js --detailed-diff \
  --tusdcat ./build/tusdcat --usdcat ~/local/USD/dist/bin/usdcat \
  tests/usda/somefile.usda

# Python parse test runner
cd tests/parse_usd && python runner.py --path ../../models
```

## Key Data Flow

1. **Load**: `LoadUSDFromFile()` -> format detection -> parser -> `Stage`
2. **Compose**: `Stage` -> composition arcs -> flattened scene graph
3. **Convert**: `Stage` -> Tydra -> `RenderScene` (for rendering)
4. **Write**: `Stage` -> writer -> output file (USDA or USDC)

## Coding Conventions

- C++14 baseline (C++17 allowed in build system)
- `.cc`/`.hh` extensions
- No C++ exceptions (`nonstd::expected` for errors)
- Build with `-Weverything -Werror` (clang); suppress specific warnings via pragmas
- PascalCase for types, camelCase for functions
- `DCOUT()` macro for debug logging (compiled out in production builds)
- Headers must be self-contained

## Security

- Memory budget controls: `USDLoadOptions::max_memory_limit_in_mb`
- Bounds checking in all parsers
- Fuzzer targets in `tests/fuzzer/`
- Always validate untrusted USD input with memory limits

## Commit Style

Concise imperative subjects (e.g. "Fix double-quoting in USDC metadata"). Body optional for context. Reference issues with `#123`. Default branch for PRs: `release`.
