# USDZ Loader for three.js experiment

## Status 

Very early stage. cli nodejs execution is getting working.

## Building

Requires emscripten + cmake(emcmake)

See `bootstrap-emscripten-linux.sh` for details.

## Simple example

We use `bun` + TypeScript.

Copy `tinyusdz.js` and `tinyusdz.wasm`, and USD files to `simple` folder.
Then in `simple` folder,

```
$ bun install
```

Then run

```
$ bun run dev
```


