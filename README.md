# Tiny USDZ library in C++14

`TinyUSDZ` is dependency-free(depends only on C++ STL. Other 3rd-party libraries included. Yes, you don't need pxrUSD library!) USDZ/USDC/USDA library written in C++14.

## Build status

[![C/C++ CI](https://github.com/syoyo/tinyusdz/workflows/C/C++%20CI/badge.svg)](https://github.com/syoyo/tinyusdz/actions/)

[![Total alerts](https://img.shields.io/lgtm/alerts/g/syoyo/tinyusdz.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/syoyo/tinyusdz/alerts/)

## Supported platforms

|         |   Linux                                  |  Windows                              |   macOS   |  iOS   | Android |  WASM                          |
|:-------:|:---------------------------------------- |:------------------------------------- |:---------:|:------:|:-------:|:------------------------------:|
|   dev   | ✅ 64bit </br> ✅ 32bit </br> ✅ aarch64 | ✅ 64bit </br> ✅ 32bit </br> 🤔 ARM  |✅         |✅      |✅       |✅ [sandbox/wasi](sandbox/wasi) |


## Status

TinyUSDZ is near to release first version v0.8.0.
Core loading feature(both USDA and USDC) is production-grade.
(Flattened scene only(i.e, USDZ). Composition features are not supported yet)

Remaining task is to write a examples, demo and scene/render delegate(Tydra).

* [x] USDZ/USDC(Crate) parser
* [ ] USDZ/USDC(Crate) writer (Work-in-progress)
* [x] USDA parser
* [x] USDA writer
* [x] Support basic Primitives(Xform, Mesh, BasisCurves, etc.), basic Lights and Shaders(UsdPreviewSurface, UsdUVTexture, UsdPrimvarReader)

**Please see** [doc/status.md](doc/status.md) **for more details**

* [ ] Write simple SDL viewer example(2022 Winter expected)
* [ ] Write iOS and Android example(2022 Winter expected)
* [ ] Vulkan or OptiX/HIP RT raytracing viewer example
* [ ] USD <-> glTF converter example
  * There is an independent work of USD to glTF binary GLB converter: https://github.com/fynv/usd2glb
* [ ] Web demo with Three.js?
  * [ ] Three.js started to support USDZ with Ascii format, but no USDC support yet: https://github.com/mrdoob/three.js/issues/14219

### Security and memory budget

TinyUSDZ has first priority of considering security and stability.

USDZ(USDC) is a binary format and data are compressed. To avoid out-of-bounds access, out-of-memory, and other security issues when loading malcious USDZ(e.g. USDZ file from unknown origin), TinyUSDZ has a memory budget feature to avoid out-of-memory issue.

To limit a memory usage when loading USDZ file, Please set a value `max_memory_limit_in_mb` in USDLoadOptions.

TinyUSDZ source codes(and some external third party codes) are also checked by Address Sanitizer, CodeQL and Fuzzer.

#### Fuzzer 

See [tests/fuzzer](tests/fuzzer) .
For building fuzzer tests, you'll need Meson and Ninja.

If you need to deal with arbitrary USD files from unknown origin(e.g. from internet, NFT storage. Whose may contain malcious data), it is recommended to use TinyUSDZ in sandboxed environment(RunC, FlatPak, WASI(WASM)). Run in WASI is recommended at the moment(please see next section).

#### Web platform(WASM) and sandboxed environment(WASI)

TinyUSDZ does not use C++ exceptions and can be built without threads. TinyUSDZ supports WASM and WASI build. So TinyUSDZ should runs well on various Web platform(WebAssembly. No SharedArrayBuffer, Atomics and WebAssembly SIMD(which is not yet available on iOS Safari) required) and sandboxed environment(WASI. Users who need to read various USD file which possibly could contain malcious data from Internet, IPFS or blockchain storage). 

See [sandbox/wasi/](sandbox/wasi) for Building TinyUSDZ with WASI toolchain.

### Tydra

USD itself is a generic container of 3D scene data.
Tydra is an interface to Renderers/Viewers and other DCCs.
Tydra may be something like Tiny version of pxrUSD Hydra, but its API is completely different. See [src/tydra/README.md](src/tydra/README.md) for the background.

* Image color space
  * sRGB
  * Linear
  * Rec.709
  * [ ] Partial support of OCIO(OpenColor IO) through TinyColorIO https://github.com/syoyo/tinycolorio . Currently SPI3DLut only.
* More details are T.B.W.

## Notice

TinyUSDZ does not support Reality Composer file format(`.reality`) since it uses proprietary file format and we cannot understand it(so no conversion support from/to Reality also).

## Commercial support

TinyUSDZ focuses on loading/writing USDA/USDC/USDZ functionalities.
Example viewer is just for demo purpose.
Currently TinyUSDZ project is run as personal project by `syoyo` and `syoyo` does not provide commercial support as an individual.

If you need commercial support, eco-system development(e.g. plug-ins, DCC tools on top of TinyUSDZ) or production-grade USDZ model viewer(e.g. embed TinyUSDZ to your AR app, 3D NFT Android mobile viewer capable of displaying (encrypted) USDZ model), please contact Light Transport Entertainment, Inc. : https://goo.gl/forms/1p6uGcOKWGpXPHkA2 


## Projects using TinyUSDZ

* usd2glb: USD to glTF 2.0 GLB converter https://github.com/fynv/usd2glb

### Other related projects

* UsdzSharpie: C# Simple implementation of Usdz file format ( https://github.com/UkooLabs/UsdzSharpie )
* TinyGLTF: glTF 2.0 loader/saver ( https://github.com/syoyo/tinygltf )
* USD-build-aarch64: Full USD build for AARCH64(Linux and Android): https://github.com/syoyo/USD-build-aarch64
* BlenderUSDZ: It contains their own Python USDC parser/serializer. https://github.com/robmcrosby/BlenderUSDZ

## Supported platforms

* [x] Linux 64bit or later
  * [x] ARM AARCH64
  * [x] x86-64
  * [ ] RISC-V(Should work)
  * [ ] SPARC, POWER(Big endian machine). May work(theoretically)
* [x] Android arm64v8a
* [x] iOS
* [x] macOS(Arm, x86-64)
* [x] Windows 10 64bit or later
  * [ ] Windows ARM(should work)
* [x] WebAssembly
  * Emscripten
    * See [examples/sdlviewer/](examples/sdlviewer) example.
* [x] WASI(through WASI toolchain)
  * See [sandbox/wasi](sandbox/wasi)

## Requirements

* C++14 compiler
  * [x] gcc 4.9 or later
  * [x] Visual Studio 2019 or later(2017 may compiles)
    * [x] Can be compiled with standalone MSVC compilers(Build Tools for Visual Studio 2019)
  * [x] clang 3.4 or later https://clang.llvm.org/cxx_status.html
  * [x] llvm-mingw(clang) supported
  * [x] MinGW gcc supported, but not recommended(You may got compilation failure depending on your build configuration: https://github.com/syoyo/tinyusdz/issues/33 , and linking takes too much time if you use default bfd linker.). If you want to compile TinyUSDZ in MinGW environment, llvm-mingw(clang) is recommended to use.

## Build

### Integrate to your app

If you are using CMake, just include tinyusdz repo with `add_subdirectory`. 

Another way is simply copy `src` folder to your app, and add `*.cc` files to your app's build system.
All include paths are set relative from `src` folder, so you can just add include directory to `src` folder.

See `<tinyusdz>/CMakeLists.txt` and [examples/sdlviewer/CMakeLists.txt](examples/sdlviewer/CMakeLists.txt) for details.

It may not be recommend to use tinyusdz as a git submodule, since the repo contains lots of codes required to build TinyUSDZ examples but these are not required for your app.

### Compiler defines

Please see `CMake build options` and `CMakeLists.txt`. In most case same identifier is defined from cmake build options: For example if you specify `-DTINYUSDZ_PRODUCTION_BUILD=1` for cmake argument, `TINYUSDZ_PRODUCTION_BUILD` is defined.

### CMake

Cmake build is provided.

#### Linux and macOS

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

Please take a look at `scripts/bootstrap-cmake-*.sh` for some build configuraions.

#### Visual Studio

Visual Studio 2019 and 2022 are supported.

`CMakeSettings.json` is provided for Visual Studio 2019, but reccommended way is to invoke `vcsetup.bat`.
(Edit VS version in `vcsetup.bat` as you with)

#### LLVM-MinGW build

MinGW native and cross-compiling example using llvm-mingw(clang) is provided.
See `scripts/bootstrap-cmake-mingw-win.sh` and `scripts/bootstrap-cmake-llvm-mingw-cross.sh` for details. 

One of benefit to use llvm-mingw is address sanitizer support on Windows app.

To run app(`.exe`, you'll need `libunwind.dll` and `libc++.dll` on your working directory or search path)

For Windows native build, we assume `ninja.exe` is installed on your system(You can use it from Meson package)

#### CMake build options

* `TINYUSDZ_PRODUCTION_BUILD` : Production build. Do not output debugging logs.
* `TINYUSDZ_BUILD_TESTS` : Build tests
* `TINYUSDZ_BUILD_EXAMPLES` : Build examples(note that not all examples in `examples` folder are built)
* `TINYUSDZ_WITH_OPENSUBDIV` : Use OpenSubviv to tessellate subdivision surface.
  * OpenSubdiv code is included in TinyUSDZ repo. If you want to use external OpenSubdiv repo, specity the path to OpenSubdiv using `osd_DIR` cmake environment variable.
* `TINYUSDZ_WITH_AUDIO` : Support loading audio(mp3 and wav).
* `TINYUSDZ_WITH_EXR` : Support loading EXR format HDR texture through TinyEXR.
* `TINYUSDZ_WITH_PXR_COMPAT_API` : Build with pxrUSD compatible API.

#### clang-cl on Windows

Assume ninja.exe is installed and path to ninja.exe is added to your `%PATH%`

Edit path to MSVC SDK and Windows SDK in `bootstrap-clang-cl-win64.bat`, then

```
> bootstrap-clang-cl-win64.bat
> ninja.exe
```


### Examples

* [usda_parser](examples/usda_parser/) Parse USDA and print it as Ascii.
* [usdc_parser](examples/usdc_parser/) Parse USDC and print it as Ascii.
* [Simple usdz_dump](examples/simple_usdz_dump/) Parse USDZ/USDA/USDC and print it as Ascii.
* [Simple SDL viewer](examples/sdlviewer/)
  * Separated CMake build provided: See [Readme](examples/sdlviewer/README.md)

See [examples](examples) directory for more examples, but may not actively maintained except for the above examples.

### USDZ Data format

See [prim_format.md](doc/prim_format.md) and [preview_surface.md](doc/preview_surface.md)

## Blender add-on(will be removed)

There was some experiements of TinyUSDZ add-on for Blender.

But Blender's USD support is getting better.
We recommend to use Omniverse version of Blender's USD import/export feature.

https://builder.blender.org/download/experimental/

### Build Blender add-on on Linux(deprecared)

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

### Higher priority

* [ ] Built-in usdObj(wavefront .obj mesh) support.
  * Through tinyobjloader.
* [x] Support Crate(binary) version 0.8.0(USD v20.11 default)
* [ ] usdSkel utilities
  * [ ] Joint hierachy reconstruction and compute skinning matrix(usdSkel)
  * [ ] Blend shapes
    * [x] Basic Blendshapes support
    * [ ] In-between blend shapes
* [ ] Read USD data with bounded memory size. This feature is especially useful for mobile platform(e.g. in terms of security, memory consumption, etc)
* [ ] USDC writer
* [ ] Support Nested USDZ
* [ ] UDIM texture support
* [ ] MaterialX support
  * Parse XML file

### Middle priority

* [ ] Composition arcs
* [ ] Code optimization

### Lower priority

* [ ] iOS example?
* [ ] Support AR related schema(Game-like feature added by Reality Composer?)
* [ ] Audio play support
  * [ ] Play audio using SoLoud or miniaudio(or Oboe for Android)
  * [ ] wav(dr_wav)
  * [ ] mp3(dr_mp3)
  * [ ] m4a(ALAC?)
* [ ] Viewer with Vulkan API.
* [ ] Replace OpenSubdiv with our own subdiv library.
* [ ] Write parser based on the schema definition.
* [ ] Support big endian architecture.

## Python binding and prebuit packages

Python binding and prebuilt packages(uploadded on PyPI) are provided.

See [python/README.md](python/README.md) and [doc/python_binding.md](doc/python_binding.md) for details.

## CI build

CI build script is a build script trying to build TinyUSDZ in self-contained manner as much as possible(including custom Python build)

### Linux/macOS

T.B.W.

### Windows

Build custom Python,

```
> ci-build-python-lib.bat
```

then build TinyUSDZ by linking with this local Python build.

```
> ci-build-vs2022.bat
```

## License

TinyUSDZ is licensed under MIT license and Apache 2.0 license.
(Doing relicensing from MIT to Apache 2.0. Will be fully relicensed to Apache 2.0 at some point)

### Third party licenses

* pxrUSD : Apache 2.0 license. https://github.com/PixarAnimationStudios/USD
* OpenSubdiv : Apache 2.0 license. https://github.com/PixarAnimationStudios/OpenSubdiv
* lz4 : BSD-2 license. http://www.lz4.org
* cnpy(uncompressed ZIP decode/encode code) : MIT license https://github.com/rogersce/cnpy
* tinyexr: BSD license.
* tinyobjloader: MIT license.
* tinygltf: MIT license.
* tinycolorio: MIT license. https://github.com/syoyo/tinycolorio
* stb_image: public domain. 
* dr_libs: public domain. https://github.com/mackron/dr_libs
* miniaudio: public domain or MIT no attribution. https://github.com/dr-soft/miniaudio
* SDL2 : zlib license. https://www.libsdl.org/index.php
* optional-lite: BSL 1.0 license. https://github.com/martinmoene/optional-lite
* expected-lite: BSL 1.0 license. https://github.com/martinmoene/expected-lite
* mapbox/earcut.hpp: ISC license. https://github.com/mapbox/earcut.hpp
* par_shapes.h generate parametric surfaces and other simple shapes: MIT license https://github.com/prideout/par
* staticstruct: MIT license. https://github.com/syoyo/staticstruct
* MaterialX: Apache 2.0 license. https://github.com/AcademySoftwareFoundation/MaterialX
* tinyxml2: zlib license. https://github.com/leethomason/tinyxml2
* string_id: zlib license. https://github.com/foonathan/string_id
* cityhash: MIT license. https://github.com/google/cityhash
* fast_float: Apache 2.0/MIT dual license. https://github.com/fastfloat/fast_float
* jsteeman/atoi: Apache 2.0 license. https://github.com/jsteemann/atoi
* formatxx: unlicense. https://github.com/seanmiddleditch/formatxx
* ubench.h: Unlicense. https://github.com/sheredom/ubench.h
* thelink2012/any : BSL-1.0 license. https://github.com/thelink2012/any
* simple_match : BSL-1.0 license. https://github.com/jbandela/simple_match
* nanobind : BSD-3 license. https://github.com/wjakob/nanobind
* pybind11 : BSD-3 license. https://github.com/pybind/pybind11
* pystring : BSD-3 license. https://github.com/imageworks/pystring
* gulrak/filesytem : MIT license. https://github.com/gulrak/filesystem
* p-ranav/glob : MIT license. https://github.com/p-ranav/glob
* linalg.h : Unlicense. https://github.com/sgorsten/linalg
* mapbox/eternal: ISC License. https://github.com/mapbox/eternal
* bvh: MIT license. https://github.com/madmann91/bvh
* dtoa_milo.h: MIT License. https://github.com/miloyip/dtoa-benchmark
* jeaiii/itoa: MIT License. https://github.com/jeaiii/itoa
* alac: Apache 2.0 License. https://macosforge.github.io/alac/
