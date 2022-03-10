# Tiny USDZ library 

`TinyUSDZ` is dependency-free(depends only on C++ STL. Other 3rd-party libraries included. Yes, you don't need USD library!) USDZ/USDC/USDA library written in C++14.

![C/C++ CI](https://github.com/syoyo/tinyusdz/workflows/C/C++%20CI/badge.svg)

[![Total alerts](https://img.shields.io/lgtm/alerts/g/syoyo/tinyusdz.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/syoyo/tinyusdz/alerts/)

## Status

TinyUSDZ is currently in work-in-progress. It does not compile and run.
I'm now rewritting TinyUSDZ core to support more USD features including Composition Arcs, Skinning, Animations, Hair strands, etc. Initial working demo in 2022 Summer expected.

* [x] USDC data parse
* [ ] USDA parse
  * Initial USDA parser starting to work(see `sandbox/usda/`)
  * Support Blender-exported USDA support in 2022 Summer planned.
* [ ] Reconstuct primitive and scene graph representaion(2022 Spring expected)
  * [ ] GeomMesh
  * [ ] GeomBasisCurves(for hair/fur)
  * [ ] GeomPoints(for particles)
  * [ ] Xform
  * [ ] etc.
  * [ ] Built-in usdObj(wavefront .obj mesh support)
    * See [doc/usdObj.md](doc/usdObj.md) for details.
* [ ] Composition
  * [ ] subLayers
  * [ ] references
  * [ ] payloads(delayed load)
  * [ ] variants
  * [ ] specializers
* [ ] Write simple SDL viewer example(2022 Summer expected)
* [ ] Character animation(usdSkel, blendshapes, animations) support(2022 Summer expected)
* [ ] Vulkan raytracing viewer example
* [ ] Write iOS and Android example(2022 Fall expected)
* [ ] Polygon triangulation using mapxbo/earcut.hpp
* [ ] USDZ(USDC) writer
* [ ] USDA writer

## Notice

TinyUSDZ does not support Reality Composer file format(`.reality`) since it uses proprietary file format and we cannot understand it(so no conversion support from/to Reality also).

## Other related projects

* UsdzSharpie: C# Simple implementation of Usdz file format ( https://github.com/UkooLabs/UsdzSharpie )
* TinyGLTF: glTF 2.0 loader/saver ( https://github.com/syoyo/tinygltf )
* TinyObjLoader: Wavefront .obj loader ( https://github.com/tinyobjloader/tinyobjloader )
* USD-build-aarch64: Full USD build for AARCH64(Linux and Android): https://github.com/syoyo/USD-build-aarch64
* BlenderUSDZ: It contains their own Python USDC parser/serializer. https://github.com/robmcrosby/BlenderUSDZ

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
  * [ ] Windows ARM(should work)
* [x] WebAssembly(through Emscripten)
  * See `examples/sdlviewer/` example.
  * Multithreading is not available due to Browser's restriction.

## Requirements

* C++14 compiler
  * [x] gcc 5 or later
  * [x] Visual Studio 2019 or later(2017 may compiles)
    * [x] Can be compiled with standalone MSVC compilers(Build Tools for Visual Studio 2019)
  * [x] clang 3.4 or later https://clang.llvm.org/cxx_status.html
  * [x] llvm-ming(clang) supported

## USDZ file format

USDZ is actually the uncompressed zip file.
USDZ(ZIP) contains usdc(binary) and resources(e.g. image/auduo files)

## Security and memory budget

USDZ(USDC) is a binary format and data are compressed. To avoid out-of-bounds access, out-of-memory, and other security issues when loading malcious USDZ(e.g. USDZ file from unknown origin), TinyUSDZ has a memory budget feature to avoid out-of-memory issue.

To limit a memory usage when loading USDZ file, Please set a value `max_memory_limit_in_mb` in USDLoadOptions.

TinyUSDZ source codes are also checked by Address Sanitizer, CodeQL and Fuzzer.

## Build

### Integrate to your app

Recomended way is simply copy `src` and `include` folder to your app, and add `*.cc` files to your app's build system.

I do not recommend to use tinyusdz as a git submodule, since the repo contains lots of codes required to build TinyUSDZ examples but these are not required for your app.

### Compiler defines

* `TINYUSDZ_USE_OPENSUBDIV` : Use OpenSubviv for subdivision surface(to get smooth mesh from USD(Z) primitive). Default enabled.

### CMake

cmake build is still provided for CI build. `CMakeSettings.json` is provided for Visual Studio 2019.

Cmake project is not recommended for embedding TinyUSDZ to your app.

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

#### LLVM-MinGW build

MinGW native and cross-compiling example using llvm-mingw(clang) is provided.
See `scripts/bootstrap-cmake-mingw-win.sh` and `scripts/bootstrap-cmake-llvm-mingw-cross.sh` for details. 

One of benefit to use llvm-mingw is address sanitizer support on Windows app.

To run app(`.exe`, you'll need `libunwind.dll` and `libc++.dll` on your working directory or search path)

For Windows native build, we assume `ninja.exe` is installed on your system(You can use it from Meson package)

#### CMake build options

* `TINYUSDZ_BUILD_TESTS` : Build tests
* `TINYUSDZ_BUILD_EXAMPLES` : Build examples(note that not all examples in `examples` folder are built)
* `TINYUSDZ_WITH_OPENSUBDIV` : Use OpenSubviv to tessellate subdivision surface.
  * OpenSubdiv code is included in TinyUSDZ repo. If you want to use external OpenSubdiv repo, specity the path to OpenSubdiv using `osd_DIR` cmake environment variable.
* `TINYUSDZ_WITH_AUDIO` : Support loading audio(mp3 and wav).
* `TINYUSDZ_WITH_EXR` : Support loading EXR format HDR texture through TinyEXR.
* `TINYUSDZ_WITH_PXR_COMPAT_API` : Build with pxr compatible API.

#### clang-cl on Windows

Assume ninja.exe is installed and path to ninja.exe is added to your `%PATH%`

Edit path to MSVC SDK and Windows SDK in `bootstrap-clang-cl-win64.bat`, then

```
> bootstrap-clang-cl-win64.bat
> ninja.exe
```

### Meson

Meson build is provided for compile tests.

```
$ meson builddir
$ cd builddir
$ ninja
```

### Examples

* [Simple dump](examples/simple_dump/)
* [Simple SDL viewer](examples/sdlviewer/)
  * Separated CMake build : See [Readme](examples/sdlviewer/README.md)

See `examples` directory for more examples.

### Data format

See [prim_format.md](doc/prim_format.md) and [preview_surface.md](doc/preview_surface.md)

## Blender add-on(W.I.P)

Supprted Blender version

* 2.93 LTS(Python 3.9)

### Linux

Edit path to python interpreter in `scripts/bootstrap-cmake-linux-blender-pymodule.sh`, then

```
$ ./scripts/bootstrap-cmake-linux-blender-pymodule.sh
$ cd build
$ make
```

Copy `tinyusd_blender.*-linux-gnu.so` to blender's folder.
Recommended location is `USER` folder(`~/.config/blender/2.xx`)

Create `scripts/addons/modules` folder if required

```
mkdir -p ~/.config/blender/2.93/scripts/addons/modules
```


## TODO

* [ ] Built-in usdObj(wavefront .obj mesh) support.
  * Through tinyobjloader.
* [ ] Support Crate(binary) version 0.8.0(USD v20.11 default)
* [ ] Write parser based on the schema definition.
* [ ] Subdivision surface using OpenSubdiv.
  * [x] Build TinyUSDZ with OpenSubdiv(CPU)
  * [ ] Replace OpenSubdiv with our own subdiv library or embree3's one.
* [ ] USDA(USD Ascii) support
  * [ ] Write our own USDA parser with PEG.
  * [ ] USDA writer.
* [ ] Animation
  * [ ] Skinning(usdSkel)
  * [ ] Blend shapes
    * [ ] In-between blend shapes
* [ ] Audio play support
  * [ ] Play audio using SoLoud or miniaudio(or Oboe for Android)
  * [ ] wav(dr_wav)
  * [ ] mp3(dr_mp3)
  * [ ] m4a?
* [ ] Support AR related schema(Game-like feature added by Reality Composer?)
* [ ] Android example
* [ ] iOS example?
* [x] CPU raytracer viewer
  * sdlviewer
* [ ] Viewer with Vulkan API.
* [ ] Read USD data with bounded memory size. This feature is especially useful for mobile platform(e.g. in terms of security, memory consumption, etc)
* [ ] USDZ saver
* [ ] Support Nested USDZ
* [ ] UDIM texture support
* [ ] Support big endian architecture.

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
* tinyobjloader: MIT license.
* stb_image: public domain. 
* dr_libs: public domain. https://github.com/mackron/dr_libs
* miniaudio: public domain or MIT no attribution. https://github.com/dr-soft/miniaudio
* SDL2 : zlib license. https://www.libsdl.org/index.php
* optional-lite: BSL 1.0 license. https://github.com/martinmoene/optional-lite
* span-lite: BSL 1.0 license. https://github.com/martinmoene/span-lite
* expected-lite: BSL 1.0 license. https://github.com/martinmoene/expected-lite
* any-lite: BSL 1.0 license. https://github.com/martinmoene/any-lite
* string-view-lite: BSL 1.0 license. https://github.com/martinmoene/string-view-lite
* mapbox/earcut.hpp: ISC license. https://github.com/mapbox/earcut.hpp
* mapbox/recursive_wrapper: BSL 1.0 license. https://github.com/mapbox/variant
* par_shapes.h generate parametric surfaces and other simple shapes: MIT license https://github.com/prideout/par
* staticstruct: MIT license. https://github.com/syoyo/staticstruct
