# WASM Debug Mode with Source Maps

This document explains how to build TinyUSDZ WASM with source maps for better debugging.

## Quick Start

```bash
# Build with source maps
./bootstrap-linux-debug-sourcemap.sh
cd build_debug_sourcemap
make -j8

# The output will include:
# - js/src/tinyusdz/tinyusdz.js
# - js/src/tinyusdz/tinyusdz.wasm
# - js/src/tinyusdz/tinyusdz.wasm.map  (source map)
```

## What You Get

When building with `-DTINYUSDZ_WASM_DEBUG=ON`, you get:

1. **Source Maps** (`-gsource-map`)
   - Maps WASM code back to C++ source files
   - Enables viewing C++ source in browser DevTools

2. **Enhanced Stack Traces** (`-sDEMANGLE_SUPPORT=1`)
   - C++ function names are demangled
   - Shows actual function names instead of mangled symbols

3. **Runtime Checks**
   - `-sASSERTIONS=2` - Runtime assertion checks
   - `-sSTACK_OVERFLOW_CHECK=2` - Detect stack overflows
   - `-sSAFE_HEAP=1` - Detect memory access errors

## Browser Setup

1. Open Chrome DevTools (F12)
2. Go to Settings (⚙️) → Preferences
3. Enable "Enable JavaScript source maps"
4. When an abort/crash occurs, you'll see C++ source lines in the stack trace

## Example Output

### Without Source Maps:
```
RuntimeError: abort(Error) at Error
    at abort (wasm-function[1234]:0x5678)
```

### With Source Maps:
```
RuntimeError: abort(MaterialX conversion failed) at Error
    at abort (src/tydra/materialx.cc:142:5)
    at convertOpenPBR (src/tydra/materialx.cc:89:12)
    at setupMesh (binding.cc:345:3)
```

## Manual Build

If you prefer to configure manually:

```bash
mkdir build_debug_sourcemap
cd build_debug_sourcemap

emcmake cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTINYUSDZ_WASM_DEBUG=ON

make -j8
```

## CMake Options

- `TINYUSDZ_WASM_DEBUG=ON` - Enable source maps and enhanced debugging
- `TINYUSDZ_WASM64=ON` - Use WASM64/Memory64 (optional, requires newer browsers)

## File Size Impact

Source maps increase file sizes:

| Build Type | .wasm Size | .wasm.map Size |
|------------|------------|----------------|
| Release    | ~2-5 MB    | N/A            |
| Debug      | ~5-10 MB   | N/A            |
| Debug + SourceMap | ~5-10 MB | ~15-30 MB |

**Note:** The `.wasm.map` file is only loaded when DevTools is open, so it doesn't affect production performance.

## Troubleshooting

### Source maps not loading

1. Check that `.wasm.map` file exists next to `.wasm` file
2. Verify the web server serves `.map` files (check MIME type)
3. Check browser console for CORS errors
4. Verify `--source-map-base http://localhost:5173/` matches your server URL

### Source files not showing

1. The source map contains paths relative to the build directory
2. Ensure source files haven't moved since build
3. Check browser DevTools → Sources tab for file tree

### Different dev server port

Edit `web/CMakeLists.txt` line 141:
```cmake
--source-map-base http://localhost:YOUR_PORT/
```

## Production Builds

For production, use the standard build without source maps:

```bash
./bootstrap-linux.sh
cd build
make -j8
```

This produces optimized, small WASM files without debug overhead.
