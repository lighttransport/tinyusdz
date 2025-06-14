See <tinyusdz>/.github/workflows/wasmPublish.yml or `bootstrap-linux.sh` to build wasm binary.

wasm binary will be output to `dist` folder.

# Note

* asyncify is disabled since it increases code size ~2.5x

# Code size

tinyusdz.wasm

2025/05. emsdk 4.0.8. -Oz : 1.6 MB
2025/06. emsdk 4.0.9. -Oz : 1.9 MB

with asyncify: 4 MB

# zstd compression

we recommend to use zstd compression for wasm binary.
for example, 1.9MB tinyusdz wasm can be compressed to 400KB with `-19` compression level.

## Prepare wasm.zstd

```
$ zstd -19 tinyusdz.wasm
```

See demo/TinyUSDZLoader.js to load zstd compressed wasm.

# stack size

128k => ok.
64k => ok.

# Demo app

See `demo` folder.
