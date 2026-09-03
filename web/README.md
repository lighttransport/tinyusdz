# LightUSD JS/WASM

## Demo app

See `demo` folder.
It uses npm package of `lightusd`, so no WASM building required.

## Technical note on JS/WASM version of LightUSD

Please see wiki https://github.com/lighttransport/lightusd/wiki/WASM-and-JavaScript-module

## For library developers

See `js` folder for JS codes.

## Building WASM module

Emscripten and `emcmake` are required. LightUSD uses C++20 coroutines for
async work across the JS/WASM boundary without requiring Asyncify or JSPI.

### Module variants: legacy / next

LightUSD has two USD cores. The published loader is next-first and the build
produces separate legacy and next products:

| Variant | CMake option | Module (`js/src/lightusd/`) | Contents | .wasm (MinSizeRel) |
|---|---|---|---|---|
| **legacy** | `-DLIGHTUSD_WASM_PRODUCT=legacy` | `lightusd.js/.wasm` | Classic loader and legacy Tydra RenderScene conversion | varies by toolchain |
| **next** | `-DLIGHTUSD_WASM_PRODUCT=next` | `lightusd_next.js/.wasm` | next-core + Tydra-next (USDA/USDC/USDZ parse, composition, render extraction, streaming) | varies by toolchain |
| **combined** (compatibility-only) | `-DLIGHTUSD_WASM_PRODUCT=combined` | `lightusd_combined.js/.wasm` | Both APIs in one module for transition testing; not the published default | varies by toolchain |

Each variant emits a distinctly-named module, so builds never clobber each
other's output. `LIGHTUSD_WASM64=ON` combines with any variant and appends
`_64` to the module name (`lightusd_64`, `lightusd_next_64`, or
`lightusd_combined_64`).

- **legacy** (`src/*.cc`) is the shipped npm loader: mature reader + Tydra
  RenderScene conversion.
- **next** (`src/next/`) is the standalone AOUSD-aligned core (parser, crate,
  PCP composition, evaluation, validation, and writers). The next product
  exposes it through Tydra-next and `RenderStream`.

### Standard WASM32 build (2GB memory limit)

Build the legacy product:

```bash
$ ./bootstrap-linux.sh
$ ninja -C build          # or: cmake --build build
```

### Next product

```bash
# next -> js/src/lightusd/lightusd_next.js/.wasm
$ emcmake cmake -S web -B web/cmake-build-next -G Ninja \
    -DLIGHTUSD_WASM_PRODUCT=next -DCMAKE_BUILD_TYPE=MinSizeRel
$ cmake --build web/cmake-build-next
```

### WASM64/MEMORY64 build (8GB memory limit)

```bash
$ ./bootstrap-linux-wasm64.sh
$ ninja -C build_64       # or: cmake --build build_64
```

### Build type & generator (.wasm size)

The package build script uses the **Ninja** generator with
**`CMAKE_BUILD_TYPE=MinSizeRel`**. MinSizeRel
applies emscripten's link-time `-Oz` (and drops runtime assertions), which is
what keeps the module small — the `.wasm` is roughly **~5MB**. A plain
`Release` build does **not** apply link-time size optimization (emscripten links
at `-O0`, pulling debug system libs + assertions), producing a **~13MB** `.wasm`
from the same code. Always ship MinSizeRel.

For an experimental speed-over-size build (larger `.wasm`, no `-Oz`/assertions),
use `bootstrap-linux-release.sh` / `bootstrap-linux-wasm64-release.sh`. The
`*-ninja.sh` scripts build the same MinSizeRel config into separate
`build_ninja` / `build_64_ninja` directories.

### Memory Limit Defaults

- **WASM32 (standard)**: 2GB default memory limit
- **WASM64 (MEMORY64)**: 8GB default memory limit

The JavaScript wrapper automatically uses the appropriate native default based on the build architecture.

**Note**: WASM64/MEMORY64 requires browsers with MEMORY64 support (Chrome 109+, Firefox 102+ with flags enabled).

The generated modules are written to `web/js/src/lightusd/`. Use
`web/npm/build-wasm.sh` for the reproducible legacy + next wasm32/wasm64 build.

## Known Issues

## Note

* asyncify is disabled since it increases code size ~2.5x

### Code size

lightusd.wasm

2025/05. emsdk 4.0.8. -Oz : 1.6 MB
2025/06. emsdk 4.0.9. -Oz : 1.9 MB

### zstd compression

we recommend to use zstd compression for wasm binary in the deployment.
for example, 1.9MB lightusd wasm can be compressed to 400KB with `-19` compression level.

### Prepare wasm.zstd

```
$ zstd -19 lightusd.wasm
```

See js/src/lightusd/LightUSDLoader.js to how to load zstd compressed wasm.

### stack size

128k => ok.
64k => ok.

## npm packaging

Use `web/npm` for packaging metadata and staging scripts.

```bash
cd web/npm
npm run build
npm run validate
```

The GitHub publish workflow is in `<lightusd>/.github/workflows/wasmPublish.yml`.
