# Tiny USDZ library 

`TinyUSDZ` is dependency-free(depends only on C++ STL. Other 3rd-party libraries included. Yes, you don't need USD library!) USDZ/USDC/USDA library written in C++14.

![C/C++ CI](https://github.com/syoyo/tinyusdz/workflows/C/C++%20CI/badge.svg)

[![Total alerts](https://img.shields.io/lgtm/alerts/g/syoyo/tinyusdz.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/syoyo/tinyusdz/alerts/)

## Status

TinyUSDZ is currently work-in-progress. It does not yet working.
Mostly finished rewritting TinyUSDZ core to support more USD features including Composition Arcs, Skinning, Animations, Hair strands, etc.
Initial working demo in 2022 Summer(August) planned after finishing implementing scene graph reconstruction.

* [x] USDC(Crate) data parse
* [ ] Reconstuct primitive and scene graph representaion(2022 Summer expected)
  * [ ] Xform
  * [ ] GeomMesh
  * [ ] GeomBasisCurves(for hair/fur)
  * [ ] GeomPoints(for particles)
  * [ ] etc.
  * [ ] Built-in usdObj(wavefront .obj mesh support)
    * See [doc/usdObj.md](doc/usdObj.md) for details.
  * [ ] MagicaVoxel vox for Volume?
  * [ ] VDBVolume support through TinyVDBIO? https://github.com/syoyo/tinyvdbio
* [ ] USDA parser(W.I.P.)
  * Initial USDA parser starting to work(see `src/ascii-parser.cc` and `src/usda-reader.cc`)
  * Support Blender-exported USDA support in 2022 Fall planned.
* [ ] Composition
  * [ ] subLayers
  * [ ] references
  * [ ] payloads(delayed load)
  * [ ] variants/variantSets(priority is low)
  * [ ] specializers(priority is low)
* [ ] Write simple SDL viewer example(2022 Summer expected)
* [ ] Character animation(usdSkel, blendshapes, animations) support(2022 Winter expected)
* [ ] Vulkan raytracing viewer example
* [ ] Write iOS and Android example(2022 Fall expected)
* [ ] USDZ(USDC, Crate) writer
  * `src/crate-writer.cc`
* [ ] USDA writer
* [ ] USD <-> glTF converter example

## Notice

TinyUSDZ does not support Reality Composer file format(`.reality`) since it uses proprietary file format and we cannot understand it(so no conversion support from/to Reality also).

## Commercial support

TinyUSDZ focuses on loading/writing USDA/USDC/USDZ functionalities.
Example viewer is just for demo purpose.
Currently TinyUSDZ project is run as personal project by `syoyo` and `syoyo` does not provide commercial support as an individual.

If you need commercial support, eco-system development(e.g. plug-ins, authorization tools on top of TinyUSDZ) or production-grade USDZ model viewer(e.g. embed TinyUSDZ to your AR app, 3D NFT Android mobile viewer capable of displaying (encrypted) USDZ model), please contact Light Transport Entertainment, Inc. : https://goo.gl/forms/1p6uGcOKWGpXPHkA2 


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
* [ ] iOS(Should compile)
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
See `<tinyusdz>/CMakeLists.txt` for details.

I do not recommend to use tinyusdz as a git submodule, since the repo contains lots of codes required to build TinyUSDZ examples but these are not required for your app.

### Compiler defines

Please see `CMake build options` and `CMakeLists.txt`. In most case same identifier is defined from cmake build options: For example if you specify `-DTINYUSDZ_PRODUCTION_BUILD=1` for cmake argument, `TINYUSDZ_PRODUCTION_BUILD` is defined.

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

* `TINYUSDZ_PRODUCTION_BUILD` : Production build. Do not include debugging logs.
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

* [Simple usdz_dump](examples/simple_usdz_dump/)
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

### Higher priority

* [ ] Built-in usdObj(wavefront .obj mesh) support.
  * Through tinyobjloader.
* [ ] Support Crate(binary) version 0.8.0(USD v20.11 default)
* [ ] USDA(USD Ascii) support
  * [ ] Write hand-written USDA parser(W.I.P). [`src/usda-parser.cc`](src/usda-parser.cc)
  * [ ] USDA writer.
* [ ] Animation
  * [ ] Skinning(usdSkel)
  * [ ] Blend shapes
    * [ ] In-between blend shapes
* [ ] Read USD data with bounded memory size. This feature is especially useful for mobile platform(e.g. in terms of security, memory consumption, etc)
* [ ] USDZ saver
* [ ] Support Nested USDZ
* [ ] UDIM texture support
* [ ] MaterialX support
  * Parse XML file

### Lower priority

* [ ] iOS example?
* [ ] Support AR related schema(Game-like feature added by Reality Composer?)
* [ ] Audio play support
  * [ ] Play audio using SoLoud or miniaudio(or Oboe for Android)
  * [ ] wav(dr_wav)
  * [ ] mp3(dr_mp3)
  * [ ] m4a?
* [ ] Viewer with Vulkan API.
* [ ] Replace OpenSubdiv with our own subdiv library.
* [ ] Write parser based on the schema definition.
* [ ] Support big endian architecture.

## Python binding and prebuit packages

Python binding and prebuilt packages(uploadded on PyPI) are provided.

See [python/README.md](python/README.md) and [doc/python_binding.md](doc/python_binding.md) for details.

## Fuzzing test

See `tests/fuzzer/` 

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

TinyUSDZ is licensed under MIT license.

### Third party licenses

* USD : Apache 2.0 license. https://github.com/PixarAnimationStudios/USD
* OpenSubdiv : Apache 2.0 license. https://github.com/PixarAnimationStudios/OpenSubdiv
* lz4 : BSD-2 license. http://www.lz4.org
* cnpy(uncompressed ZIP decode/encode code) : MIT license https://github.com/rogersce/cnpy
* tinyexr: BSD license.
* tinyobjloader: MIT license.
* tinygltf: MIT license.
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
* mapbox/variant: BSD-3 or BSL-1.0 license. https://github.com/mapbox/variant
* ubench.h: Unlicense. https://github.com/sheredom/ubench.h
* thelink2012/any : BSL-1.0 license. https://github.com/thelink2012/any
* better-enums : BSD-2 license. https://aantron.github.io/better-enums/
* simple_match : BSL-1.0 license. https://github.com/jbandela/simple_match
* nanobind : BSD-3 license. https://github.com/wjakob/nanobind
* pybind11 : BSD-3 license. https://github.com/pybind/pybind11
