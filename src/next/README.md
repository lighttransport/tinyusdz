# TinyUSDZ Next - Architecture Redesign

## Overview

The `src/next/` directory contains a complete redesign of TinyUSDZ's core architecture, focusing on:

- **Minimal template usage** - Replace 78+ `DEFINE_TYPE_TRAIT` macros with runtime type dispatch
- **Fast compilation times** - O(1) table lookup instead of 65+ if-else chains
- **Low header dependencies** - Minimal includes, forward declarations where possible
- **Clean separation of concerns** - Modular design with clear boundaries

API breakage and feature deletion are acceptable for this redesign. The goal is a clean, maintainable codebase that can be extended incrementally.

## Current Status

### Completed Components

| Component | Status | Files |
|-----------|--------|-------|
| Type System | ✅ Complete | `types/type-id.hh`, `types/type-info.{hh,cc}`, `types/value.{hh,cc}` |
| Path | ✅ Complete | `prim/path.{hh,cc}` |
| Property Index | ✅ Complete | `layer/property-index.{hh,cc}` |
| PrimSpec | ✅ Complete | `layer/prim-spec.{hh,cc}` |
| Layer | ✅ Complete | `layer/layer.{hh,cc}` |
| Stage | ✅ Complete | `stage/stage.{hh,cc}` |
| Lexer | ✅ Complete | `parser/lexer.{hh,cc}` |
| Value Parser | ✅ Complete | `parser/value-parser.{hh,cc}` |
| ASCII Parser | ✅ Complete | `parser/ascii-parser.{hh,cc}` |
| USDA Reader | ✅ Complete | `reader/usda-reader.{hh,cc}` |
| Crate Format | ✅ Complete | `crate/crate-format.{hh,cc}` |
| Crate Reader | ✅ Complete | `crate/crate-reader.{hh,cc}` |
| USDC Reader | ✅ Complete | `reader/usdc-reader.{hh,cc}` |
| Value Printer | ✅ Complete | `writer/value-printer.{hh,cc}` |
| Prim Printer | ✅ Complete | `writer/prim-printer.{hh,cc}` |
| USDA Writer | ✅ Complete | `writer/usda-writer.{hh,cc}` |
| Crate Writer | ✅ Basic | `crate/crate-writer.{hh,cc}` |
| USDC Writer | ✅ Basic | `writer/usdc-writer.{hh,cc}` |
| UsdGeomMesh | ✅ Complete | `schema/geom-mesh.{hh,cc}` |
| UsdGeomXform | ✅ Basic | `schema/geom-xform.{hh,cc}` |

### Build Status

```bash
# Build the library
cd src/next/build
cmake -DTINYUSDZ_NEXT_BUILD_TESTS=ON ..
make -j8

# Run tests
./test_tinyusdz_next  # Basic type system tests
./test_layer          # Layer/PrimSpec tests
./test_stage          # Stage tests
./test_usdc_reader    # USDC reading tests
./test_writer         # USDA writer tests
./test_usdc_writer    # USDC writer tests
```

## Architecture

### Type System (`types/`)

```
TypeId (enum) ──► TypeInfo (runtime metadata) ──► Value (type-erased storage)
```

- **TypeId** - Simple enum with ~50 USD types (no templates)
- **TypeInfo** - Runtime type information with function pointers for operations
- **Value** - 136-byte inline storage with Small Buffer Optimization (SBO)

Key design: All type dispatch happens at runtime via table lookup, not compile-time templates.

### Layer System (`layer/`)

```
Layer
├── prims_: vector<PrimSpec>     (flat storage, cache-friendly)
├── root_indices_: vector<uint32_t>
├── path_to_index_: unordered_map
└── meta_: LayerMeta

PrimSpec
├── name_, type_id_, specifier_, path_
├── props_: PropIndex           (O(1) property lookup)
├── child_indices_: vector<uint32_t>
├── relationships_: unordered_map
└── meta_: PrimSpecMeta
```

Key design: Flat prim storage with indices instead of pointers. No separate Prim class needed - PrimSpec serves both roles.

### Stage System (`stage/`)

```
Stage
├── root_layer_: unique_ptr<Layer>
├── sub_layers_: vector<unique_ptr<Layer>>
└── meta_: StageMeta

UsdPrim (lightweight handle)
├── spec_: const PrimSpec*
├── layer_: const Layer*
└── index_: uint32_t
```

Key design: Stage wraps Layer(s) and provides UsdPrim handles for traversal. Non-copyable due to unique_ptr members.

### Parser System (`parser/`)

```
Lexer ──► ValueParser ──► AsciiParser ──► LayerBuilder ──► Stage
```

- **Lexer** - Simple tokenizer with line/column tracking
- **ValueParser** - Function pointer table for type-specific parsing (no templates)
- **AsciiParser** - PIMPL pattern, uses LayerBuilder internally

### Crate System (`crate/`)

```
CrateReader                          CrateWriter
├── Read sections                    ├── Build token table
├── Decode ValueRep                  ├── Build path table
├── Build prims via LayerBuilder     ├── Build fields/specs
└── Return Stage                     └── Write sections + TOC
```

### Writer System (`writer/`)

```
Stage/Layer ──► PrintLayer() ──► USDA string
Stage/Layer ──► WriteUSDA()  ──► USDA file
Stage/Layer ──► WriteUSDC()  ──► USDC file (via CrateWriter)
```

## File Structure

```
src/next/
├── CMakeLists.txt
├── README.md                    # This file
│
├── types/                       # Core type system
│   ├── type-id.hh              # TypeId enum (~200 lines)
│   ├── type-info.hh            # TypeInfo struct
│   ├── type-info.cc            # Type registry implementation
│   ├── value.hh                # Value class with SBO
│   └── value.cc                # Value implementation
│
├── prim/                        # USD primitives
│   ├── path.hh                 # Path class
│   ├── path.cc
│   ├── attribute.hh            # (minimal, for future use)
│   ├── attribute.cc
│   ├── prim.hh                 # (minimal, for future use)
│   └── prim.cc
│
├── layer/                       # Layer system
│   ├── property-index.hh       # PropNameTable, PropSlot, PropIndex
│   ├── property-index.cc
│   ├── prim-spec.hh            # PrimSpec, TypeNameTable, ValueStorage
│   ├── prim-spec.cc
│   ├── layer.hh                # Layer, LayerBuilder
│   └── layer.cc
│
├── stage/                       # Stage system
│   ├── stage.hh                # Stage, UsdPrim, StageBuilder
│   └── stage.cc
│
├── parser/                      # ASCII parsing
│   ├── lexer.hh
│   ├── lexer.cc
│   ├── value-parser.hh
│   ├── value-parser.cc
│   ├── ascii-parser.hh
│   └── ascii-parser.cc
│
├── crate/                       # Binary format
│   ├── crate-format.hh         # Crate structures, ValueRep
│   ├── crate-format.cc
│   ├── stream-reader.hh        # Binary stream helper
│   ├── crate-reader.hh
│   ├── crate-reader.cc
│   ├── crate-writer.hh
│   └── crate-writer.cc
│
├── reader/                      # High-level readers
│   ├── usda-reader.hh
│   ├── usda-reader.cc
│   ├── usdc-reader.hh
│   └── usdc-reader.cc
│
├── writer/                      # Writers
│   ├── value-printer.hh
│   ├── value-printer.cc
│   ├── prim-printer.hh
│   ├── prim-printer.cc
│   ├── usda-writer.hh
│   ├── usda-writer.cc
│   ├── usdc-writer.hh
│   └── usdc-writer.cc
│
└── schema/                      # Schema convenience APIs
    ├── geom-mesh.hh            # UsdGeomMesh wrapper
    ├── geom-mesh.cc
    ├── geom-xform.hh           # UsdGeomXform wrapper
    └── geom-xform.cc
```

## TODO Tasks

### High Priority

- [x] **TimeSamples Support** ✅ COMPLETE
  - ✅ Time sample parsing in USDA parser
  - ✅ Time sample writing in USDA writer
  - ✅ `GetValueAtTime()` / `GetTimeSampleTimes()` on UsdPrim
  - ✅ `HasTimeSamples()` query API
  - [ ] Time sample writing in USDC writer (basic structure only)

- [x] **Connection Support** ✅ COMPLETE
  - ✅ Parse attribute connections (`.connect`)
  - ✅ Store connections in PrimSpec (as string value with kFlagConnection)
  - ✅ Write connections in USDA writer

- [ ] **Complete USDC Writer** (partial)
  - [x] Basic section structure (TOKENS, STRINGS, FIELDS, SPECS, PATHS)
  - [ ] Implement integer compression (USD's custom encoding)
  - [ ] Implement LZ4 compression for large arrays
  - [ ] Add proper fieldset encoding for full pxrUSD compatibility
  - [ ] Test roundtrip with pxrUSD tools

- [ ] **Composition Arcs**
  - [ ] Implement reference resolution
  - [ ] Implement payload loading
  - [ ] Implement inherit/specialize flattening
  - [ ] Implement variant selection

### Medium Priority

- [x] **Schema Support** (partial)
  - [x] Add UsdGeomMesh convenience API (`schema/geom-mesh.hh`)
  - [x] Add UsdGeomXform convenience API (`schema/geom-xform.hh`)
  - [ ] Add UsdShadeMaterial convenience API
  - [ ] Add UsdSkelSkeleton convenience API
  - [ ] Consider code generation for schema classes
  - [ ] Add token array support for xformOpOrder

- [x] **Attribute Metadata** (partial)
  - [x] Parse attribute qualifiers (custom, uniform, varying)
  - [x] Store in PropSlot flags
  - [x] Write qualifiers in USDA output
  - [ ] Parse interpolation metadata

- [ ] **Error Recovery**
  - [ ] Add error recovery in USDA parser
  - [ ] Continue parsing after errors
  - [ ] Collect multiple errors before failing

- [ ] **Debug Output**
  - [ ] Add configurable debug/trace logging
  - [x] Memory usage reporting (Layer::memory_usage(), Stage::GetMemoryUsage())
  - [x] Statistics (Layer::stats(), Stage::GetStats())

### Low Priority

- [ ] **USDZ Support**
  - Add zip archive reading
  - Add zip archive writing
  - Handle embedded assets

- [ ] **Performance Optimization**
  - Profile and optimize hot paths
  - Consider SIMD for array operations
  - Optimize string interning

- [ ] **API Polish**
  - Add iterator support for prim traversal
  - Add query API for finding prims by type
  - Add modification API for editing Stage

- [ ] **Documentation**
  - Add API documentation comments
  - Add usage examples
  - Add migration guide from old API

### Testing

- [ ] Add fuzz testing for parsers
- [ ] Add roundtrip tests (USDA → Stage → USDA)
- [ ] Add roundtrip tests (USDC → Stage → USDC)
- [ ] Add comparison tests with pxrUSD output
- [ ] Add performance benchmarks

## Design Decisions

### Why no templates?

The original TinyUSDZ used heavy template metaprogramming (78+ `DEFINE_TYPE_TRAIT` macros) which caused:
- Slow compilation (template instantiation in every TU)
- Large object files (3.5-4.4MB for parser)
- Hard to understand error messages
- Difficult to extend

The new design uses runtime type dispatch via function pointer tables, which:
- Compiles faster (no template instantiation)
- Produces smaller object files
- Has predictable performance (O(1) lookup)
- Is easier to debug and extend

### Why flat prim storage?

Storing prims in a flat `vector<PrimSpec>` instead of a tree:
- Better cache locality (sequential memory access)
- Simpler memory management (no tree node allocation)
- Easier serialization (indices instead of pointers)
- Children accessed via indices (still O(1))

### Why unified PrimSpec?

The old design had both `PrimSpec` and `Prim` classes with conversion between them. The new design uses `PrimSpec` for both roles:
- Less code duplication
- No conversion overhead
- Simpler mental model
- `UsdPrim` is just a lightweight handle

### Why move-only Stage/Layer?

Using `unique_ptr` for layer ownership makes:
- Ownership semantics explicit
- No accidental copying of large data
- Clear lifetime management
- Efficient transfer of ownership

## Metrics

| Metric | Old (value-types.hh) | New (type-id.hh + value.hh) |
|--------|---------------------|----------------------------|
| Header size | 76KB | ~8KB |
| Template instantiations | 233+ | 0 |
| Type dispatch | 65+ if-else | O(1) table lookup |
| Object file size | 3.5-4.4MB | ~500KB |

## Usage Example

```cpp
#include "next/reader/usda-reader.hh"
#include "next/writer/usda-writer.hh"
#include "next/stage/stage.hh"

using namespace tinyusdz::next;

// Load USDA
LoadResult result = LoadUSDAFromFile("model.usda");
if (!result.success) {
  std::cerr << "Error: " << result.error_summary << "\n";
  return 1;
}

Stage stage = std::move(result.stage);

// Traverse prims
stage.Traverse([](const UsdPrim& prim) {
  std::cout << prim.GetPath().str() << " : " << prim.GetTypeName() << "\n";
  return true;  // continue
});

// Write USDA
WriteUSDAToFile("output.usda", stage);

// Write USDC
WriteUSDCToFile("output.usdc", stage);
```

## Using Schema APIs

```cpp
#include "next/schema/geom-mesh.hh"
#include "next/schema/geom-xform.hh"

using namespace tinyusdz::next;

// Get all meshes from stage
auto meshes = GetAllMeshes(stage);

for (const UsdGeomMesh& mesh : meshes) {
  // Get topology
  auto faceVertexCounts = mesh.GetFaceVertexCounts();
  auto faceVertexIndices = mesh.GetFaceVertexIndices();

  // Get geometry
  auto points = mesh.GetPoints();
  size_t numPoints = mesh.GetPointCount();

  // Get UVs if present
  if (mesh.HasUVs()) {
    auto uvs = mesh.GetUVs();
  }

  // Check for animation
  if (mesh.HasAnimatedPoints()) {
    auto times = mesh.GetPointsTimeSamples();
    for (double t : times) {
      auto pointsAtTime = mesh.GetPointsAtTime(t);
    }
  }
}

// Work with transforms
UsdPrim xformPrim = stage.GetPrimAtPath("/World/Character");
UsdGeomXform xform(xformPrim);

if (xform.IsValid()) {
  float tx, ty, tz;
  if (xform.GetTranslation(&tx, &ty, &tz)) {
    // Use translation
  }

  float matrix[16];
  xform.ComputeLocalTransform(matrix);
}
```

## Building a Stage Programmatically

```cpp
#include "next/stage/stage.hh"

using namespace tinyusdz::next;

StageBuilder builder;
builder.SetDefaultPrim("World");
builder.SetUpAxis("Y");

LayerBuilder& layer = builder.GetLayerBuilder();

layer.begin_prim("World", "Xform");
layer.end_prim();

layer.begin_prim("Cube", "Mesh");
layer.add_property("points", Value::MakeFloat3Array({...}));
layer.add_property("faceVertexCounts", Value::MakeIntArray({...}));
layer.end_prim();

layer.finalize();

Stage stage = builder.Build();
```
