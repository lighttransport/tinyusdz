# Build And Examples

This page collects build, integration, testing, and code-example notes that used
to live in the top-level README.

## Native Build

LightUSD uses CMake and requires a C++17 compiler.

```sh
cmake -S . -B build -DLIGHTUSD_BUILD_TESTS=ON -DLIGHTUSD_BUILD_EXAMPLES=ON
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

LightUSD can be included directly in another CMake project:

```cmake
add_subdirectory(/path/to/lightusd lightusd)

target_include_directories(your_app PRIVATE /path/to/lightusd/src)
target_link_libraries(your_app PRIVATE lightusd::lightusd_static)
```

When LightUSD is added as a subdirectory, tools, tests, and examples are disabled
by default. The namespaced static target `lightusd::lightusd_static` is the
recommended link target. The legacy alias `lightusd_static` also exists.

LightUSD does not generate source files before the build. The USDA parser is
hand-written C++ and does not require Bison, Flex, PEG tools, or code generation.

## Common Options

- `LIGHTUSD_BUILD_TESTS`: build C++ tests.
- `LIGHTUSD_BUILD_EXAMPLES`: build configured example applications.
- `LIGHTUSD_BUILD_TOOLS`: build tools such as low-level crate dumpers.
- `LIGHTUSD_PRODUCTION_BUILD`: disable debug logging and avoid full path leakage
  in diagnostics.
- `LIGHTUSD_WITH_TYDRA`: build the renderer-friendly Tydra conversion layer.
- `LIGHTUSD_WITH_C_API`: build the C API, used by language bindings.
- `LIGHTUSD_WITH_USDMTLX`: build MaterialX support.
- `LIGHTUSD_WITH_AUDIO`: build MP3/WAV audio loading support.
- `LIGHTUSD_WITH_ALAC_AUDIO`: build ALAC/M4A support.
- `LIGHTUSD_WITH_BUILTIN_IMAGE_LOADER`: build bundled image loaders.
- `LIGHTUSD_TSD_VERIFY_WITH_OSD`: build the tinysubdiv (src/tsd) vs OpenSubdiv
  verification test. Requires `OpenSubdiv_ROOT` pointing at an OpenSubdiv
  source checkout (no pre-build needed). The subdivision library itself
  (src/tsd) is dependency-free and always built.
- `LIGHTUSD_WITH_ZSTD_COMPRESSION`: enable zstd compression support.
- `LIGHTUSD_WITH_COROUTINE`: enable C++20 coroutine support.
- `LIGHTUSD_WITH_MCP_SERVER`, `LIGHTUSD_WITH_QJS`, `LIGHTUSD_WITH_WAMR`,
  `LIGHTUSD_WITH_GEOGRAM`: optional experimental or feature-specific modules.

See [../CMakeLists.txt](../CMakeLists.txt) for the complete option list and
defaults.

## Testing

From a build directory configured with `LIGHTUSD_BUILD_TESTS=ON`:

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build -R unit-test-lightusd --output-on-failure
```

Direct Acutest invocation is also supported after building:

```sh
./build/unit-test-lightusd crate_writer_cone_test
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

- [../examples/lusdcat](../examples/lusdcat): parse USDA/USDC/USDZ, print USDA,
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
- [../tools/lusddiff](../tools/lusddiff): USD diff utility (Tydra diff-and-compare API).
- [../examples/usd_to_gltf](../examples/usd_to_gltf): USD to glTF example.

Not every directory under `examples/` is built by the top-level CMake build, and
some viewer examples have their own setup requirements.

## Minimal C++ Load

```cpp
#include "lightusd.hh"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  std::string filename = argc > 1 ? argv[1] : "input.usd";

  lightusd::Stage stage;
  std::string warn;
  std::string err;
  lightusd::USDLoadOptions options;
  options.max_memory_limit_in_mb = 512;

  if (!lightusd::LoadUSDFromFile(filename, &stage, &warn, &err, options)) {
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

  for (const lightusd::Prim &prim : stage.root_prims()) {
    std::cout << prim.element_name() << "\n";
  }

  return EXIT_SUCCESS;
}
```

Include [../src/pprinter.hh](../src/pprinter.hh) and
[../src/value-pprint.hh](../src/value-pprint.hh) when you need
`lightusd::to_string()` or stream-style pretty-printing.

## Writing USDA And USDC

```cpp
#include "lightusd.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"

lightusd::Stage stage;
std::string warn;
std::string err;

lightusd::usda::SaveAsUSDA("out.usda", stage, &warn, &err);
lightusd::usdc::SaveAsUSDCToFile("out.usdc", stage, &warn, &err);
```

USDA writing is the stable text output path. USDC writing is available for Crate
v0.8.0-style binary output; see [crate-writer.md](crate-writer.md) for current
coverage and fidelity notes.
