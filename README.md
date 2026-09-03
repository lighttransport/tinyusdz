# LightUSD

LightUSD is a portable C++17 library for reading and writing USD scene files
without requiring Pixar OpenUSD at runtime. It supports USDA, USDC (Crate), and
USDZ, and is built for applications that need a small dependency footprint,
bounded-memory loading, and predictable behavior on desktop, mobile, and web
targets.

For v1.0.0, LightUSD is focused on USD infrastructure for generative AI,
physical AI, and agentic 3D DCC workflows, with a scalable architecture that can
run across embedded systems, the web, desktop tools, and HPC pipelines.

[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-pink?logo=github)](https://github.com/sponsors/lighttransport)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/lighttransport/lightusd)
[![npm version](https://img.shields.io/npm/v/lightusd.svg)](https://www.npmjs.com/package/lightusd)

<p align="center">
 <a href="https://lighttransport.github.io/lightusd/demos/">
   <img src="screenshots/demos.jpg" width="800px" alt="LightUSD web demos">
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

LightUSD is not a full replacement for the OpenUSD runtime.
Renderer/DCC-specific domains such as UsdRi, UsdHydra, UsdUI, and UsdProc are
out of scope; unsupported typed prims are preserved as generic prim data where
possible.

## Python Package

Python bindings are available as the `lightusd` package on PyPI:

```sh
python -m pip install lightusd
```

The current package targets CPython 3.11+ with abi3 wheels. It exposes
`load()`, `loads()`, `load_bytes()`, `Stage`, `Prim`, `Attribute`, `Value`,
authoring helpers, USD save/export support, and Tydra render-scene conversion.
NumPy is not required, but array values and render buffers support Python's
buffer protocol for zero-copy NumPy views when NumPy is installed.

```python
import lightusd

stage = lightusd.load("scene.usdz")
for prim in lightusd.traverse(stage):
    print(prim.type_name, prim.name)

scene = lightusd.tydra.convert_to_render_scene(stage)
print(len(scene.meshes()), len(scene.materials()))
```

See [python/README.md](python/README.md) for package usage and
[doc/python_binding.md](doc/python_binding.md) for build and maintenance notes.

## Build And Examples

Build commands, CMake integration, common options, test commands, and C++ code
examples have moved to [doc/build-and-examples.md](doc/build-and-examples.md).

Useful entry points:

- `examples/lusdcat`: parse, inspect, compose, and convert USD files.
- `examples/api_tutorial`: author and inspect USD data with the C++ API.
- `examples/tydra_api`: query and convert USD data through Tydra.
- `examples/asset_resolution`: load USD data through custom I/O.
- `examples/file_format`: register a custom file-format handler.
- [doc/testing-cpp.md](doc/testing-cpp.md): C++ test infrastructure.

## Platform Notes

LightUSD is developed for Linux, Windows, macOS, iOS, Android, WebAssembly
(Emscripten), and WASI-style sandboxed builds. A C++17 compiler and CMake are
the normal native build requirements. C++20 is only needed for coroutine support
when `LIGHTUSD_WITH_COROUTINE` is enabled. Cross-compiling pure Win32/Win64
binaries on Linux with clang-cl + the MSVC SDK (runnable under WINE) is
documented in [doc/wine_cl.md](doc/wine_cl.md).

The public C++ API is not internally synchronized. Applications that access
`Stage`, `Prim`, `Layer`, or related objects from multiple threads must provide
their own locking.

## Security

LightUSD is intended to load untrusted USD-family files with explicit resource
limits. Use `USDLoadOptions::max_memory_limit_in_mb` when loading files from
unknown origins. Parser code is exercised with unit tests, AddressSanitizer,
CodeQL, and fuzzing; fuzzer sources live in [tests/fuzzer](tests/fuzzer).

For hostile or internet-sourced assets, prefer a sandboxed process or WASI-style
environment in addition to memory limits.

## Documentation

- [doc/README.md](doc/README.md): index of all documentation.
- [doc/api-status.md](doc/api-status.md): OpenUSD schema coverage.
- [doc/composition.md](doc/composition.md): composition (LIVRPS arcs), instancing, and variants.
- [doc/pcp.md](doc/pcp.md): composition-graph engine API reference.
- [doc/instancing.md](doc/instancing.md): OpenUSD instancing model and the island-scaling plan.
- [doc/timesamples.md](doc/timesamples.md): time-sample evaluation and deduplication.
- [doc/usdLux.md](doc/usdLux.md): lighting schemas and Tydra conversion.
- [doc/usd-physics.md](doc/usd-physics.md): UsdPhysics, MuJoCo, and Newton support.
- [doc/materialx.md](doc/materialx.md): MaterialX and OpenPBR material support.
- [doc/skinning.md](doc/skinning.md): skeletal animation and skinning.
- [doc/tydra-tangent.md](doc/tydra-tangent.md): tangent generation.
- [doc/memory-and-performance.md](doc/memory-and-performance.md): memory profile and performance notes.
- [doc/benchmarks.md](doc/benchmarks.md): Island and mid-scale scene benchmarks.
- [doc/large-scene.md](doc/large-scene.md): loading 10-20 GB production scenes within a RAM budget.
- [doc/sanitizers.md](doc/sanitizers.md): ASan/TSan build and run notes.
- [doc/refactor-next.md](doc/refactor-next.md): `src/next` optimization and hardening roadmap.
- [doc/mcp.md](doc/mcp.md): MCP server work.
- [doc/ci.md](doc/ci.md): release and publishing notes.

Completed or superseded documents live in [doc/archive/](doc/archive/).

## License

LightUSD is primarily licensed under Apache 2.0. Some helper code is MIT or
similarly permissive. Third-party dependency notices are listed in
[LICENSE.3rdparty](LICENSE.3rdparty).

LightUSD does not support Apple's `.reality` Reality Composer format.
