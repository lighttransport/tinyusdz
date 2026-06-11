# Build And Examples

This page collects build, integration, testing, and code-example notes that used
to live in the top-level README.

## Native Build

TinyUSDZ uses CMake and requires a C++17 compiler.

```sh
cmake -S . -B build -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

The bootstrap scripts under [scripts/](../scripts) contain platform-specific
configurations for Linux, macOS, Windows, MinGW, Emscripten, and other
development setups.

For Emscripten:

```sh
emcmake cmake -S web -B web/build
cmake --build web/build --parallel
```

## CMake Integration

TinyUSDZ can be included directly in another CMake project:

```cmake
add_subdirectory(/path/to/tinyusdz tinyusdz)

target_include_directories(your_app PRIVATE /path/to/tinyusdz/src)
target_link_libraries(your_app PRIVATE tinyusdz::tinyusdz_static)
```

When TinyUSDZ is added as a subdirectory, tools, tests, and examples are disabled
by default. The namespaced static target `tinyusdz::tinyusdz_static` is the
recommended link target. The legacy alias `tinyusdz_static` also exists.

TinyUSDZ does not generate source files before the build. The USDA parser is
hand-written C++ and does not require Bison, Flex, PEG tools, or code generation.

## Common Options

- `TINYUSDZ_BUILD_TESTS`: build C++ tests.
- `TINYUSDZ_BUILD_EXAMPLES`: build configured example applications.
- `TINYUSDZ_BUILD_TOOLS`: build tools such as low-level crate dumpers.
- `TINYUSDZ_PRODUCTION_BUILD`: disable debug logging and avoid full path leakage
  in diagnostics.
- `TINYUSDZ_WITH_TYDRA`: build the renderer-friendly Tydra conversion layer.
- `TINYUSDZ_WITH_C_API`: build the C API, used by language bindings.
- `TINYUSDZ_WITH_USDMTLX`: build MaterialX support.
- `TINYUSDZ_WITH_AUDIO`: build MP3/WAV audio loading support.
- `TINYUSDZ_WITH_ALAC_AUDIO`: build ALAC/M4A support.
- `TINYUSDZ_WITH_BUILTIN_IMAGE_LOADER`: build bundled image loaders.
- `TINYUSDZ_TSD_VERIFY_WITH_OSD`: build the tinysubdiv (src/tsd) vs OpenSubdiv
  verification test. Requires `OpenSubdiv_ROOT` pointing at an OpenSubdiv
  source checkout (no pre-build needed). The subdivision library itself
  (src/tsd) is dependency-free and always built.
- `TINYUSDZ_WITH_ZSTD_COMPRESSION`: enable zstd compression support.
- `TINYUSDZ_WITH_COROUTINE`: enable C++20 coroutine support.
- `TINYUSDZ_WITH_MCP_SERVER`, `TINYUSDZ_WITH_QJS`, `TINYUSDZ_WITH_WAMR`,
  `TINYUSDZ_WITH_GEOGRAM`: optional experimental or feature-specific modules.

See [../CMakeLists.txt](../CMakeLists.txt) for the complete option list and
defaults.

## Testing

From a build directory configured with `TINYUSDZ_BUILD_TESTS=ON`:

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build -R unit-test-tinyusdz --output-on-failure
```

Direct Acutest invocation is also supported after building:

```sh
./build/unit-test-tinyusdz crate_writer_cone_test
```

See [testing-cpp.md](testing-cpp.md) for the C++ test layout, fixture policy, and
roundtrip comparison commands.

## Python Build

For local Python extension development:

```sh
python -m pip install -e . --no-build-isolation
python -m pytest python/tests -q
```

The editable build uses [setup.py](../setup.py) as a CMake-backed build driver
and writes native artifacts under `build_py_ext/`.

For end-user Python package usage, see [../python/README.md](../python/README.md).
For package build details, see [python_binding.md](python_binding.md).

## Tools And Examples

- [../examples/tusdcat](../examples/tusdcat): parse USDA/USDC/USDZ, print USDA,
  inspect crate fields, flatten composition, and convert USDA to USDC or USDA.
- [../examples/api_tutorial](../examples/api_tutorial): C++ API tutorial for
  constructing and inspecting USD stage data.
- [../examples/tydra_api](../examples/tydra_api): Tydra scene query and
  conversion tutorial.
- [../examples/asset_resolution](../examples/asset_resolution): custom I/O and
  asset resolver tutorial.
- [../examples/file_format](../examples/file_format): custom file-format
  handler tutorial.
- [../examples/progressive_composition](../examples/progressive_composition):
  progressive composition example.
- [../examples/usddiff](../examples/usddiff): USD diff utility.
- [../examples/usd_to_gltf](../examples/usd_to_gltf): USD to glTF example.
- [../examples/openglviewer](../examples/openglviewer),
  [../examples/sdlviewer](../examples/sdlviewer), and
  [../examples/optixviewer](../examples/optixviewer): viewer examples.

Not every directory under `examples/` is built by the top-level CMake build, and
some viewer examples have their own setup requirements.

## Minimal C++ Load

```cpp
#include "tinyusdz.hh"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  std::string filename = argc > 1 ? argv[1] : "input.usd";

  tinyusdz::Stage stage;
  std::string warn;
  std::string err;
  tinyusdz::USDLoadOptions options;
  options.max_memory_limit_in_mb = 512;

  if (!tinyusdz::LoadUSDFromFile(filename, &stage, &warn, &err, options)) {
    if (!warn.empty()) {
      std::cerr << "WARN: " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR: " << err << "\n";
    }
    return EXIT_FAILURE;
  }

  if (!warn.empty()) {
    std::cerr << "WARN: " << warn << "\n";
  }

  for (const tinyusdz::Prim &prim : stage.root_prims()) {
    std::cout << prim.element_name() << "\n";
  }

  return EXIT_SUCCESS;
}
```

Include [../src/pprinter.hh](../src/pprinter.hh) and
[../src/value-pprint.hh](../src/value-pprint.hh) when you need
`tinyusdz::to_string()` or stream-style pretty-printing.

## Writing USDA And USDC

```cpp
#include "tinyusdz.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"

tinyusdz::Stage stage;
std::string warn;
std::string err;

tinyusdz::usda::SaveAsUSDA("out.usda", stage, &warn, &err);
tinyusdz::usdc::SaveAsUSDCToFile("out.usdc", stage, &warn, &err);
```

USDA writing is the stable text output path. USDC writing is available for Crate
v0.8.0-style binary output; see [crate-writer.md](crate-writer.md) for current
coverage and fidelity notes.
