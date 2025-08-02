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

See <tinyusdz>/.github/workflows/wasmPublish.yml or

```
$ ./bootstrap-linux.sh
$ cd build
$ make
```

wasm module(tinyusdz.js and tinyusdz.wasm) will be output to `js/src/tinyusdz` folder.

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


