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

### Compiler defines

* `TINYUSDZ_USE_OPENSUBDIV` : Use OpenSubviv for subdivision surface(to get smooth mesh from USD(Z) primitive).

### CMake

cmake build is still provided, but not recommended

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

#### CMake build options

* `TINYUSDZ_BUILD_TESTS` : Build tests
* `TINYUSDZ_BUILD_EXAMPLES` : Build examples(note that not all examples in `examples` folder are built)
* `TINYUSDZ_WITH_OPENSUBDIV` : Use OpenSubviv to tessellate subdivision surface.
  * `osd_DIR` to specify the path to OpenSubdiv repo(I recommend to use https://github.com/syoyo/OpenSubdiv-aarch64 )
* `TINYUSDZ_WITH_AUDIO` : Support loading audio(mp3 and wav).
* `TINYUSDZ_WITH_EXR` : Support loading EXR format HDR texture.

#### Build with OpenSubdiv

Recommended way is to run `scripts/clone_osd.sh` to clone OpenSubdiv-aarch64 repo to `deps/OpenSudiv`, then run `scripts/bootstrap-cmake-linux-with-osd.sh` .

### Meson

Meson build is provided for testing compilation.

```
$ meson builddir
$ cd builddir
$ ninja
```

### Examples

See `examples` directory.

### Data format

#### Primitive

* [x] `visibility`
* [x] `position`(FLOAT3 only)

## TODO

* [ ] USDA(USD Ascii) support
  * [ ] Write our own USDA parser with PEG.
* [ ] Animation
  * [ ] Skinning(usdSkel)
  * [ ] Blend shapes
    * [ ] In-between blend shapes
* [ ] Audio play support
  * [ ] Play audio using SoLoud or miniaudio(or Oboe for Android)
  * [ ] wav(dr_wav)
  * [ ] mp3(dr_mp3)
  * [ ] m4a?
* [ ] Android example
* [ ] iOS example?
* [ ] CPU raytracer viewer
* [ ] Viewer with Vulkan API.
* [ ] Replace OpenSubdiv with our own subdiv library or embree3's one.
* [ ] Read USD data with bounded memory size. This feature is especially useful for mobile platform(e.g. in terms of security, memory consumption, etc)
* [ ] USDZ saver
* [ ] Support Nested USDZ
* [ ] UDIM texture support
* [ ] Support big endian

## Fuzzing test

See `tests/fuzzer/` 

## License

TinyUSDZ is licensed under MIT license.

### Third party licenses

* USD : Apache 2.0 license. https://github.com/PixarAnimationStudios/USD
* lz4 : BSD-2 license. http://www.lz4.org
* cnpy(uncompressed ZIP decode/encode) : MIT license https://github.com/rogersce/cnpy
* tinyexr: BSD license.
* stb_image: public domain. 
* dr_libs: public domain. https://github.com/mackron/dr_libs
* miniaudio: public domain or MIT no attribution. https://github.com/dr-soft/miniaudio

