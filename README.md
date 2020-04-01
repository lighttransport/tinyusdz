# Tiny USDZ loader

`TinyUSDZ` is dependency-free(depends only on C++ STL. Yes, you don't need USD library!) USDZ loader library written in C++11.

## Requirements

* C++11

## USDZ file format

USDZ is a uncompressed zip file.
USDZ contains usdc(binary) and resources(e.g. image file)

## Build

### Integrate to your app

Recomended way is simply copy `src` and `include` folder to your app, and add `*.cc` files to your app's build system.

### CMake

cmake build is still provided, but not recommended

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

#### Build options

* `TINYUSDZ_BUILD_TESTS` : Build tests
* `TINYUSDZ_BUILD_EXAMPLES` : Build examples(note that not all examples in `examples` folder are built)
* `TINYUSDZ_USE_OPENSUBDIV` : Use OpenSubviv to tessellate subdivision surface.

### Meson

Meson build is provided for testing compilation.

```
$ meson builddir
$ cd builddir
$ ninja
```

### Examples

See `examples` directory.


## TODO

* [ ] Android example
* [ ] CPU raytracer viewer
* [ ] Viewer with Vulkan API.
* [ ] Replace OpenSubdiv with our own subdiv library or embree3's one.
* [ ] Read USD data with bounded memory size. This feature is especially useful for mobile platform(e.g. in terms of security, memory consumption, etc)
* [ ] USDZ saver
* [ ] Support Nested USDZ
* [ ] UDIM texture support

## Fuzzing test

See `tests/fuzzer/` 

## License

TinyUSDZ is licensed under MIT license.

### Third party licenses

* USD : Apache 2.0 license. https://github.com/PixarAnimationStudios/USD
* lz4 : BSD-2 license. http://www.lz4.org
* cnpy(uncompressed ZIP decode/encode) : MIT license https://github.com/rogersce/cnpy

