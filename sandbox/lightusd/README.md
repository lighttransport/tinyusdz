# LightUSD

**LightUSD is a low-level USD API, much like Vulkan is for graphics.**

While TinyUSDZ provides a high-level, batteries-included framework with extensive abstractions, LightUSD takes a different approach: it exposes the fundamental building blocks of USD with minimal overhead, giving you direct control over every aspect of scene description parsing, manipulation, and serialization.

LightUSD is targetting 3D content creation/DCC framework for 3D genAI, 3D VLM/LLM application.

## Philosophy

Just as Vulkan gives graphics programmers explicit control over GPU resources, memory allocation, and rendering pipelines, LightUSD gives USD developers explicit control over:

- **Memory layout** - No hidden allocations, predictable memory usage
- **Parsing strategy** - Stream, chunk, or load-all-at-once
- **Composition** - Build your own composition engine or use the provided PCP implementation
- **Value resolution** - Direct access to raw attribute data without automatic type coercion
- **Threading** - No global state, no hidden thread pools

This makes LightUSD ideal for:

- **WebAssembly/Browser** - Minimal binary size, async-friendly architecture
- **Embedded systems** - No exceptions, no RTTI, predictable resource usage
- **Game engines** - Direct integration without impedance mismatch
- **Tools** - Fast iteration with explicit control over caching
- **Security-critical applications** - Auditable codebase, bounded resource consumption

## Key Features

- **Zero dependencies** - Pure C++17(C++20 ready), no external libraries required
- **No exceptions, no RTTI** - Suitable for `-fno-exceptions -fno-rtti` builds
- **Explicit error handling** - `Result<T>` type for all fallible operations
- **Format support** - USDA (ASCII), USDC (Crate binary), USDZ (ZIP archive)
- **Progressive loading** - Stream large files without loading everything into memory
- **WebAssembly ready** - First-class Emscripten support with Asyncify/JSPI
- **Render-ready output** - Direct conversion to GPU-friendly vertex buffers

## Core Concepts

```
Layer          - A single USD file (the "document")
Stage          - A composed view of one or more layers
Prim           - A scene graph node (mesh, xform, material, etc.)
Property       - Attribute (typed value) or Relationship (path reference)
Path           - Hierarchical identifier (/World/Geometry/Cube)
Token          - Interned string for fast comparison
Value          - Type-erased container for any USD value type
TimeSamples    - Animated values keyed by time code
```

## Quick Start

```cpp
#include "lightusd/lightusd.hh"

using namespace lightusd::v1;

// Parse a USDA string
auto result = read_usda_string(R"(
#usda 1.0
def Xform "World" {
    def Mesh "Cube" {
        float3[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
    }
}
)");

if (!result.ok()) {
    // Handle parse error
    std::cerr << result.error_summary << "\n";
    return;
}

Stage& stage = result.stage;

// Traverse the scene graph
for (size_t i = 0; i < stage.root_prim_count(); ++i) {
    const Prim* prim = stage.root_prim(i);
    std::cout << "Prim: " << prim->path().string() << "\n";
}

// Get a specific prim by path
auto prim_result = stage.get_prim_at_path(Path("/World/Cube"));
if (prim_result) {
    const Prim* cube = prim_result.value();

    // Read attribute
    if (auto* points_attr = cube->find_attribute("points")) {
        auto points = points_attr->get<std::vector<Vec3f>>();
        if (points) {
            std::cout << "Vertex count: " << points->size() << "\n";
        }
    }
}
```

## Streaming / Progressive Loading

Load large scenes without blocking - parse structure first, then load geometry on-demand:

```cpp
#include "lightusd/streaming_loader.hh"

using namespace lightusd::v1;

// Create loader with cache
auto cache = std::make_shared<AssetCache>();
cache->set_max_size(64 * 1024 * 1024);  // 64 MB

StreamingLoader loader;
loader.set_asset_cache(cache);
loader.set_time_budget_ms(16);  // Budget per frame

// Phase 1: Parse structure (fast - hierarchy only)
auto result = loader.parse_structure(data, size, "scene.usda");
if (!result) { /* handle error */ }

// Discovered prims are available immediately
for (const auto& skel : result.value()) {
    printf("%s (%s)\n", skel.path.c_str(), skel.type_name.c_str());
}

// Phase 2: Request geometry with priorities
loader.request_load({"/World/Hero", LoadPriority::Immediate, 0.0, true});
loader.request_load({"/World/Background", LoadPriority::Low, 0.0, true});

// Phase 3: Process in render loop (non-blocking)
while (rendering) {
    loader.process_queue();  // Respects time budget

    // Collect ready geometry
    while (loader.has_ready_prims()) {
        auto geom = loader.take_ready_prim();
        upload_to_gpu(geom->positions, geom->indices);
    }
}
```

## C++20 Coroutines (Async/Await)

For WASM builds with JSPI or Asyncify, use coroutines for clean async code:

```cpp
#include "lightusd/coro_fetch.hh"

using namespace lightusd::v1;

// Async fetch with co_await
Task<bool> load_scene(const std::string& url) {
    // Fetch USD file (suspends WASM stack until complete)
    FetchResult usd = co_await coro_fetch(url);
    if (!usd.ok) co_return false;

    // Fetch textures in parallel
    auto textures = co_await coro_fetch_all({
        "diffuse.png", "normal.png", "roughness.png"
    });

    co_return true;
}

// Generator for progressive loading
Generator<PrimGeometry> load_prims(StreamingLoader& loader) {
    while (loader.pending_count() > 0) {
        loader.process_queue(1);
        while (loader.has_ready_prims()) {
            co_yield *loader.take_ready_prim();
        }
    }
}
```

Build with `-DLIGHTUSD_COROUTINE=ON -DLIGHTUSD_WASM_JSPI=ON`

See [examples/async_loader.cc](examples/async_loader.cc) for a complete example.

## Modules

| Module | Description |
|--------|-------------|
| `types.hh` | Basic types (Vec2, Vec3, Vec4, Matrix4, Quaternion) |
| `token.hh` | Interned strings for fast comparison |
| `path.hh` | USD path handling with variant selection |
| `value.hh` | Type-erased value container |
| `attribute.hh` | Typed attribute with time samples |
| `relationship.hh` | Path-based references |
| `prim.hh` | Scene graph node |
| `stage.hh` | Composed scene graph |
| `layer.hh` | Single USD file representation |
| `usda_reader.hh` | USDA (ASCII) parser |
| `usda_writer.hh` | USDA (ASCII) writer |
| `usdc_reader.hh` | USDC (Crate binary) parser |
| `usdz_archive.hh` | USDZ (ZIP) archive handler |
| `streaming_loader.hh` | Progressive/async loading |
| `render_data.hh` | GPU-ready mesh conversion |
| `pcp_*.hh` | Prim Cache Population (composition) |
| `schema.hh` | JSON-based schema validation |
| `clips.hh` | Value Clips support |
| `primvar.hh` | Primvar interpolation handling |
| `lightexr.hh` | Minimal EXR texture loader |
| `lighthdr.hh` | Minimal HDR/RGBE texture loader |

## Build

```bash
mkdir build && cd build
cmake ..
make -j8
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `LIGHTUSD_BUILD_TESTS` | ON | Build unit tests |
| `LIGHTUSD_BUILD_EXAMPLES` | ON | Build examples |
| `LIGHTUSD_PRODUCTION_BUILD` | OFF | Disable debug output |
| `LIGHTUSD_DEBUG_PRINT` | OFF | Enable DCOUT macro |

### WebAssembly (Emscripten)

```bash
emcmake cmake -DLIGHTUSD_WASM_JSPI=ON ..
emmake make -j8
```

| Option | Description |
|--------|-------------|
| `LIGHTUSD_WASM_ASYNCIFY` | Enable Asyncify for async JS calls |
| `LIGHTUSD_WASM_JSPI` | Enable JSPI (more efficient, Chrome 109+) |
| `LIGHTUSD_COROUTINE` | Enable C++20 coroutine API |

## Comparison with TinyUSDZ

| Aspect | TinyUSDZ | LightUSD |
|--------|--------|----------|
| Philosophy | High-level framework | Low-level toolkit |
| Dependencies | None, but some external libraries embedded | None |
| Binary size | ~10+ MB | < 1 MB |
| Exceptions | No | No |
| RTTI | No | No |
| Threading | Built-in(W.I.P | User-controlled |
| Memory | Automatic management | Explicit control |
| WebAssembly | Supported, but no async | First-class support |
| Composition | Integrated | Modular, optional |

## License

Apache-2.0 and some part in MIT

## Related Projects

- [TinyUSDZ](https://github.com/lighttransport/tinyusdz) - The parent project providing additional features
