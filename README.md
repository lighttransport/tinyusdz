# Tiny USDZ loader

`TinyUSDZ` is dependency-free(depends only on C++ STL. Yes, you don't need USD library!) USDZ loader library written in C++11.

## Status

TinyUSDZ is currently in alpha stage. Not usable.

* [x] USDC data parse
* [ ] Reconstuct scene graph representaion(2020 April expected)
* [ ] Write simple OpenGL viewer example(2020 April expected)
* [ ] Animation(usdSkel) support(2020 Summer expected)
* [ ] Vulkan raytracing viewer example
* [ ] USDZ writer

## Supported platforms

* [x] Linux 64bit or later
  * [x] ARM AARCH64
  * [x] x86-64
  * [ ] RISC-V(Should work)
  * [ ] SPARC, POWER(Big endian machine). May work(theoretically)
* [x] Android arm64v8a
* [ ] iOS(Should work)
* [x] macOS
* [x] Windows 10 64bit or later
  * [x] Windows ARM should work

## Requirements

* C++11

## USDZ file format

USDZ is actually the uncompressed zip file.
USDZ(ZIP) contains usdc(binary) and resources(e.g. image/auduo files)

## Build

### Integrate to your app

Recomended way is simply copy `src` and `include` folder to your app, and add `*.cc` files to your app's build system.

### Compiler defines

* `TINYUSDZ_USE_OPENSUBDIV` : Use OpenSubviv for subdivision surface(to get smooth mesh from USD(Z) primitive).

### CMake

cmake build is still provided for CI build. Cmake project is not recommended for embedding TinyUSDZ to your app.

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
* `TINYUSDZ_WITH_EXR` : Support loading EXR format HDR texture through TinyEXR.

#### Build with OpenSubdiv

Recommended way is to run `scripts/clone_osd.sh` to clone OpenSubdiv-aarch64 repo to `deps/OpenSudiv`, then run `scripts/bootstrap-cmake-linux-with-osd.sh`.

### Meson

Meson build is provided for compile tests.

```
$ meson builddir
$ cd builddir
$ ninja
```

### Examples

[Simple dump](examples/simple_dump/)
[Simple OpenGL viewer](examples/openglviewer/)

See `examples` directory for more examples.

### Data format

See [prim_format.md](doc/prim_format.md) and [preview_surface.md](doc/preview_surface.md)

## TODO

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
* cnpy(uncompressed ZIP decode/encode code) : MIT license https://github.com/rogersce/cnpy
* tinyexr: BSD license.
* stb_image: public domain. 
* dr_libs: public domain. https://github.com/mackron/dr_libs
* miniaudio: public domain or MIT no attribution. https://github.com/dr-soft/miniaudio

