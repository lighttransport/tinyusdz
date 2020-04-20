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

## Other related projects

* TinyGLTF: glTF 2.0 loader/saver ( https://github.com/syoyo/tinygltf )
* TinyObjLoader: Wavefront .obj loader ( https://github.com/tinyobjloader/tinyobjloader )
* USD-build-aarch64: Full USD build for AARCH64(Linux and Android): https://github.com/syoyo/USD-build-aarch64

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

* C++11 compiler
  * [x] gcc 4.8.5(CentOS 7 default) or later
  * [x] Visual Studio 2017 or later(2015 may OK)
  * [x] clang 3.8 or later

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
  * OpenSubdiv code is included in TinyUSDZ repo. If you want to use external OpenSubdiv repo, specity the path to OpenSubdiv using `osd_DIR` cmake environment variable.
* `TINYUSDZ_WITH_AUDIO` : Support loading audio(mp3 and wav).
* `TINYUSDZ_WITH_EXR` : Support loading EXR format HDR texture through TinyEXR.

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

* [ ] Subdivision surface using OpenSubdiv.
  * [ ] Replace OpenSubdiv with our own subdiv library or embree3's one.
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
* OpenSubdiv : Apache 2.0 license. https://github.com/PixarAnimationStudios/OpenSubdiv
* lz4 : BSD-2 license. http://www.lz4.org
* cnpy(uncompressed ZIP decode/encode code) : MIT license https://github.com/rogersce/cnpy
* tinyexr: BSD license.
* stb_image: public domain. 
* dr_libs: public domain. https://github.com/mackron/dr_libs
* miniaudio: public domain or MIT no attribution. https://github.com/dr-soft/miniaudio

