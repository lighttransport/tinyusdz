# TinyUSDZ JS/WASM

## Demo app

See `demo` folder.
It uses npm package of `tinyusdz`, so no WASM building required.

## Technical note on JS/WASM version of TinyUSDZ

Please see wiki https://github.com/lighttransport/tinyusdz/wiki/WASM-and-JavaScript-module

## For library developers

See `js` folder for JS codes.

## Building WASM module

Emscripten and emcmake required.
TinyUSDZ is beging built with C++20 to use C++20 coruntine to support async over JS/WASM boundary, without requiring sASYNCIFY and JSPI(JavaScript Promise Integration)

### Standard WASM32 build (2GB memory limit)

```bash
$ ./bootstrap-linux.sh
$ cd build
$ make
```

### WASM64/MEMORY64 build (8GB memory limit)

```bash
$ rm -rf build
$ emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DTINYUSDZ_WASM64=ON -Bbuild
$ cd build
$ make
```

### Memory Limit Defaults

- **WASM32 (standard)**: 2GB default memory limit
- **WASM64 (MEMORY64)**: 8GB default memory limit

The JavaScript wrapper automatically uses the appropriate native default based on the build architecture.

**Note**: WASM64/MEMORY64 requires browsers with MEMORY64 support (Chrome 109+, Firefox 102+ with flags enabled).

wasm module(tinyusdz.js and tinyusdz.wasm) will be output to `js/src/tinyusdz` folder.

See also: `bootstrap-examples.sh` for build configuration examples.

## Known Issues

### shared_ptr TimeSamples dedup breaks skeletal animation (2026-02)

Commit `243928d9` ("Add shared_ptr TimeSamples dedup, move semantics, and half_to_float LUT") introduced a shared_ptr-based TimeSamples deduplication optimization in `primvar.hh` and `usdc-reader.cc`. This optimization causes SkelAnimation attributes (translations, rotations, scales) to lose their TimeSamples data during prim reconstruction, resulting in 0 animation channels/samplers.

**Symptom**: Skeletal animations (e.g. CesiumMan.usdz) load as static meshes with no animation playback. Debug output shows `translations.has_timesamples()=0, has_value()=0, authored()=1`.

**Root cause**: The mutable `ConvertToAnimatable` overload in `prim-reconstruct.cc` and the COW (copy-on-write) shared TimeSamples mechanism in `PrimVar` interact incorrectly during SkelAnimation property parsing. The branch was reset to `c62dc69a` (the last known good commit before the optimization).

**Files involved**: `src/primvar.hh`, `src/prim-reconstruct.cc`, `src/usdc-reader.cc`, `src/crate-reader.hh`.

## Note

* asyncify is disabled since it increases code size ~2.5x

### Code size

tinyusdz.wasm

2025/05. emsdk 4.0.8. -Oz : 1.6 MB
2025/06. emsdk 4.0.9. -Oz : 1.9 MB

### zstd compression

we recommend to use zstd compression for wasm binary in the deployment.
for example, 1.9MB tinyusdz wasm can be compressed to 400KB with `-19` compression level.

### Prepare wasm.zstd

```
$ zstd -19 tinyusdz.wasm
```

See js/src/tinyusdz/TinyUSDZLoader.js to how to load zstd compressed wasm.

### stack size

128k => ok.
64k => ok.

## npm packaging

See `npm` folder and `<tinyusdz>/.github/workflows/wasmPublish.yml` for npm publish.


