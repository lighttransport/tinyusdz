# How to build

Setup Emescripten toolchain.

https://emscripten.org/docs/getting_started/downloads.html

Then build TinyUSDZ with emcmake

```
$ cd <tinyusdz>/sandbox/emscripten
$ emcmake cmake -DCMAKE_BUILD_TYPE=Release -B build_em -S ../../
```

This just build a library. No example yet.

EoL.

