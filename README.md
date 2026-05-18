# TinyUSDZ

TinyUSDZ is a portable C++17 library for reading and writing USD scene files
without requiring Pixar OpenUSD at runtime. It supports USDA, USDC (Crate), and
USDZ, and is built for applications that need a small dependency footprint,
bounded-memory loading, and predictable behavior on desktop, mobile, and web
targets.

For v1.0.0, TinyUSDZ is focused on USD infrastructure for generative AI,
physical AI, and agentic 3D DCC workflows, with a scalable architecture that can
run across embedded systems, the web, desktop tools, and HPC pipelines.

[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-pink?logo=github)](https://github.com/sponsors/lighttransport)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/lighttransport/tinyusdz)
[![npm version](https://img.shields.io/npm/v/tinyusdz.svg)](https://www.npmjs.com/package/tinyusdz)

<p align="center">
 <a href="https://lighttransport.github.io/tinyusdz/demos/">
   <img src="screenshots/demos.jpg" width="800px" alt="TinyUSDZ web demos">
 </a>
</p>

## v1.0.0 Highlights

- USD Physics: rigid bodies, collision APIs, joints, physics materials, and
  MuJoCo/Newton-oriented schema extensions for simulation workflows.
- USD MaterialX: MaterialX XML parsing, OpenPBR Surface, Autodesk Standard
  Surface, UsdPreviewSurface, color-space conversion, and renderer-friendly
  material extraction.
- USD Animation and Skinning: time samples, xform animation, UsdSkel skeletons,
  skinning data, blend shapes, and Tydra render-scene animation export.
- USD Composition (experimental): subLayers, references, payloads, inherits,
  variants, nested variants, and composition graph tooling.
- MCP (experimental): Model Context Protocol support for agent and tool
  integration around USD assets.
- Agentic 3D DCC foundation: compact USD parsing, authoring, inspection, and
  render-scene conversion APIs designed for tools that need to reason about,
  transform, and generate 3D scene data programmatically.

## Current Status

- USDA, USDC, and USDZ loading are the core production paths. Parsers are
  hand-written C++ and include bounds checks for malformed input.
- USDA writing is production-oriented. USDC writing is available for Crate
  v0.8.0-compatible output and is still treated as experimental; see
  [doc/crate-writer.md](doc/crate-writer.md).
- The scene model covers common USD domains including UsdGeom, UsdLux, UsdShade,
  UsdSkel, UsdPhysics, UsdMedia, Apple preliminary AR schemas, composition arcs,
  variants, metadata, relationships, connections, and time samples. See
  [doc/api-status.md](doc/api-status.md) for the detailed schema matrix.
- Tydra converts a USD `Stage` into renderer-friendly data for OpenGL, Vulkan,
  Three.js, and similar engines. It includes mesh/material extraction, animation,
  skinning, tangent generation, texture metadata, lights, cameras, and memory
  estimation.
- MaterialX support includes XML parsing, OpenPBR Surface, Autodesk Standard
  Surface, UsdPreviewSurface, color-space conversion, and JavaScript/Three.js
  material conversion paths. See [doc/materialx.md](doc/materialx.md).
- WebAssembly and JavaScript bindings live under [web/](web/), with Three.js
  integration documented in [doc/threejs.md](doc/threejs.md).

TinyUSDZ is not a full replacement for the OpenUSD runtime.
Renderer/DCC-specific domains such as UsdRi, UsdHydra, UsdUI, and UsdProc are
out of scope; unsupported typed prims are preserved as generic prim data where
possible.

## Python Package

Python bindings are available as the `tinyusdz` package on PyPI:

```sh
python -m pip install tinyusdz
```

The current package targets CPython 3.11+ with abi3 wheels. It exposes
`load()`, `loads()`, `load_bytes()`, `Stage`, `Prim`, `Attribute`, `Value`,
authoring helpers, USD save/export support, and Tydra render-scene conversion.
NumPy is not required, but array values and render buffers support Python's
buffer protocol for zero-copy NumPy views when NumPy is installed.

```python
import tinyusdz

stage = tinyusdz.load("scene.usdz")
for prim in tinyusdz.traverse(stage):
    print(prim.type_name, prim.name)

scene = tinyusdz.tydra.convert_to_render_scene(stage)
print(len(scene.meshes()), len(scene.materials()))
```

See [python/README.md](python/README.md) for package usage and
[doc/python_binding.md](doc/python_binding.md) for build and maintenance notes.

## Build And Examples

Build commands, CMake integration, common options, test commands, and C++ code
examples have moved to [doc/build-and-examples.md](doc/build-and-examples.md).

Useful entry points:

- `examples/tusdcat`: parse, inspect, compose, and convert USD files.
- `examples/api_tutorial`: author and inspect USD data with the C++ API.
- `examples/tydra_api`: query and convert USD data through Tydra.
- `examples/asset_resolution`: load USD data through custom I/O.
- `examples/file_format`: register a custom file-format handler.
- [doc/testing-cpp.md](doc/testing-cpp.md): C++ test infrastructure.

## Platform Notes

TinyUSDZ is developed for Linux, Windows, macOS, iOS, Android, WebAssembly
(Emscripten), and WASI-style sandboxed builds. A C++17 compiler and CMake are
the normal native build requirements. C++20 is only needed for coroutine support
when `TINYUSDZ_WITH_COROUTINE` is enabled.

The public C++ API is not internally synchronized. Applications that access
`Stage`, `Prim`, `Layer`, or related objects from multiple threads must provide
their own locking.

## Security

TinyUSDZ is intended to load untrusted USD-family files with explicit resource
limits. Use `USDLoadOptions::max_memory_limit_in_mb` when loading files from
unknown origins. Parser code is exercised with unit tests, AddressSanitizer,
CodeQL, and fuzzing; fuzzer sources live in [tests/fuzzer](tests/fuzzer).

For hostile or internet-sourced assets, prefer a sandboxed process or WASI-style
environment in addition to memory limits.

## Documentation

- [doc/api-status.md](doc/api-status.md): OpenUSD schema coverage.
- [doc/composition.md](doc/composition.md): composition support.
- [doc/variant.md](doc/variant.md): variants and nested variants.
- [doc/timesamples.md](doc/timesamples.md): time-sample evaluation.
- [doc/skinning.md](doc/skinning.md): skeletal animation and skinning.
- [doc/tydra-tangent.md](doc/tydra-tangent.md): tangent generation.
- [doc/usd-physics.md](doc/usd-physics.md): UsdPhysics support.
- [doc/mcp.md](doc/mcp.md): MCP server work.
- [doc/ci.md](doc/ci.md): release and publishing notes.

## License

TinyUSDZ is primarily licensed under Apache 2.0. Some helper code is MIT or
similarly permissive. Third-party dependency notices are listed in
[LICENSE.3rdparty](LICENSE.3rdparty).

TinyUSDZ does not support Apple's `.reality` Reality Composer format.
