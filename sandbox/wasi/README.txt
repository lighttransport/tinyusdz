WASI experiment.

## Build procedure

Setup `wasi-sdk` https://github.com/WebAssembly/wasi-sdk


Edit WASI_VERSION and path to `wasi-sdk` in `bootstrap-wasi.sh`, then

```
$ ./bootstrap-wasi.sh
$ cd build
$ make
```

## Run

Currently tested by running app with `wasmtime`.

https://github.com/bytecodealliance/wasmtime


```
$ wasmtime tinyusdz_wasi
```
