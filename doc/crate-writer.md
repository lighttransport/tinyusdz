# USDC Crate Writer

The TinyUSDZ USDC Crate Writer writes USD Stage data to binary USDC (Crate) format version 0.8.0. Status: experimental but functional with comprehensive test coverage.

## Integration Points

- Core writer: `src/crate-writer.cc` (sections/TOC/compression), `src/crate-writer-values.cc` (value-data encoding + dedup), `src/crate-writer-inline.cc` (`TryInlineValue`), `src/crate-writer.hh`
- Stage converter: `src/stage-converter.cc` (Stage/Prim/property → crate spec+field model)
- CLI: `examples/tusdcat` with `-o/--output` option
- Unit tests: `tests/unit/unit-crate-writer.cc` (69 test functions)

## Usage

```bash
# Basic conversion
./tusdcat input.usda -o output.usdc
./tusdcat output.usdc  # Verify readback

# Flattened composition
./tusdcat --flatten --composition=r,p input.usda -o flattened.usdc
```

## Implemented Features

**Core**: Binary v0.8.0 writing, file header, TOC, LZ4 compression, NaN-aware value deduplication (XXH3), path tree encoding.

**Data Types**: Basic types (int, float, double, bool, string, token), vectors, matrices, paths, TimeSamples with ValueRep format, ListOp<T> for references/payloads/inherits, TokenListOp, StringListOp, PathListOp, Dictionary values, VariantSelectionMap.

**Geometry**: Mesh (with animated points, normals, advanced features, blend shapes), Cone, Cylinder, Capsule, GeomPoints, GeomCamera, GeomBasisCurves, GeomNurbsCurves, GeomSubset, GeomPointInstancer (with instance offsets), Model, Scope, GPrim properties (visibility, purpose, extent, normal interpolation).

**Materials**: Material, Shader, NodeGraph, UsdPreviewSurface, UsdUVTexture, UsdPrimvarReader, UsdTransform2d, material binding.

**Lighting**: SphereLight, RectLight, DistantLight, DomeLight, multiple lights, light filters.

**Skeletal**: Skeleton, SkelAnimation, SkelRoot, skeletal binding.

**Metadata**: Prim/attribute/relationship metadata, CustomData, AssetInfo, layer metadata (framesPerSecond, startTimeCode, endTimeCode, upAxis, metersPerUnit).

**Composition**: References (single + multiple) with customData, payloads, inherits, sublayers with LayerOffset, specializes arc, variantSets/variantSelection.

**Specs**: Separate `Attribute`, `Relationship`, `Connection`, `VariantSet`, and `Variant` specs (no longer embedded in the parent prim — see the `AddSpec(..., SpecType::*)` calls in `stage-converter.cc`). Property `variability` (Uniform) is written for uniform attributes.

**Safety**: Error context reporting, memory limits, filesize limits, validation (enable/disable), compression.

## Resolved Issues

### TypeName Encoding

Token index (uint32_t) was stored instead of the token object. Fixed to store the `value::token` (see `stage-converter.cc`, "Store the token, not the index!").

### SpecTypePseudoRoot Ordering

Spec sorting didn't guarantee PseudoRoot at index 0. The comparator in `crate-writer.cc` always sorts PseudoRoot first, with post-sort validation that the first spec is the PseudoRoot.

### Token Index 0 Sentinel

Index 0 is reserved for the pxrUSD magic token `;-)` before any real token is registered. Because `-0 == 0`, a property/path element at index 0 would otherwise be misread (the path-encoding bit games make element 0 ambiguous).

### TimeSamples Binary Format

TimeSamples are written using embedded `ValueRep` structures (indirection format matching pxrUSD). The earlier POD-vs-`_samples` storage split has been removed — `TimeSamples` now uses a single unified storage (see memory-and-performance.md / the refactor below).

### Dictionary (customData) Binary Format

Dictionaries (`customData`, and any nested dict value) use a recursive offset
layout, matching what the reader expects (`ReadCustomData` in
`src/crate-reader-values.cc`; `RecursiveRead` in pxrUSD's `crateFile.cpp`):

```
uint64_t count
repeat count times:
  StringIndex  key       // 4 bytes
  int64_t      offset    // 8 bytes, relative to the byte AFTER this field
  ValueRep     value     // 8 bytes (or points to an out-of-line value)
```

`offset == 8` means the `ValueRep` immediately follows the offset field; the
reader seeks `offset - 8` from the current position to reach the value.

Write ordering matters: the writer first **reserves** `8 + 20*count` bytes for
the dict frame (8 for the count + 4+8+8 per entry), packs each value (nested
out-of-line writes then land *after* the reserved frame), and finally seeks
back to backfill the count, keys, offsets, and `ValueRep`s
(`src/crate-writer-values.cc`). Writing the entries inline first — as OpenUSD's
`WriteMap` does — produces a stream this reader cannot parse: it misreads a
`ValueRep` as an offset and fails with `Invalid offset value: <huge number>`.

## Debugging

```bash
# Enable debug output
export TINYUSDZ_ENABLE_DCOUT=1
./tusdcat input.usda -o output.usdc

# Hex dump header
xxd -l 128 output.usdc
```

---

# Crate Format Deduplication (pxrUSD Reference)

Analysis of the deduplication system in OpenUSD v25.08 (`pxr/usd/sdf/crateFile.cpp`). This serves as reference for TinyUSDZ's crate writer implementation.

**Principle**: Write each unique value exactly once, reference by offset or index.

## Deduplication Levels

### 1. Structural (Global)

Tables in `_PackingContext` deduplicate fundamental elements:

| Table | Purpose |
|-------|---------|
| `tokenToTokenIndex` | All tokens |
| `stringToStringIndex` | All strings |
| `pathToPathIndex` | All paths |
| `fieldToFieldIndex` | All fields |
| `fieldsToFieldSetIndex` | Field sets |

Written to dedicated sections: TOKENS, STRINGS, FIELDS, FIELDSETS, PATHS.

### 2. Value-Level (Per-Type)

**OpenUSD reference**: `_ValueHandler<T>` template deduplicates data values with separate dedup maps for scalars and arrays per concrete type, lazy allocation, cleared after write.

**TinyUSDZ implementation**: out-of-line values (including TimeSamples) are deduplicated using NaN-aware hashing (`NanAwareHash` in `crate-writer.hh`). This follows the OpenUSD `TfHash` pattern where +0.0 and -0.0 are treated as identical (both canonicalized to zero bits before hashing). The hash function is XXH3_64bits (from xxHash v0.8.3), which is 1.1x–12.3x faster than FNV-1a depending on buffer size (see benchmark below). Collision verification uses NaN-aware byte equality (`buffers_equal`).

Dedup map type: `unordered_multimap<size_t, ValueDedupEntry>` keyed by XXH3 hash, with byte content stored for collision verification.

Float/double element types: canonicalize +0/-0, then XXH3 on the full buffer.
Non-float types (int, string, token, half): XXH3 on raw bytes directly.

### Value Classification

TinyUSDZ inlining lives in `CrateWriter::TryInlineValue` (`crate-writer-inline.cc`). A `ValueRep` carries a 48-bit payload; a value is inlined iff it fits:

1. **Always inlined**: token / string / AssetPath (as their TOKENS/STRINGS index), bool, int32, uint32, half, float, and the `Specifier`/`Permission`/`Variability` enums.
2. **Conditionally inlined**: int64 / uint64 — only when the value fits in 48 bits; otherwise written out-of-line.
3. **Never inlined**: double (64 bits > 48-bit payload), vectors, matrices, and all arrays — written to the VALUE section.
4. **Value-deduplicated**: out-of-line values hashed (NaN-aware XXH3) and stored once.
5. **Array-deduplicated**: separate path; empty arrays inlined.

## Array Compression (v0.5.0+)

Arrays can be both deduplicated and compressed:

- **Integer**: `Sdf_IntegerCompression` (min 16 elements)
- **Float**: As integers if exactly representable, lookup table if few distinct values (<1024), otherwise uncompressed
- **Other types**: Uncompressed

## Performance

| Operation | Complexity |
|-----------|-----------|
| Dedup lookup | O(1) average |
| Hash computation | O(n) for value size n |
| Write value | O(n) only on first occurrence |

### XXH3 vs FNV-1a Throughput (NaN-aware, buffer canonicalize + hash)

Benchmark: `tests/feat/hash/hash_bench.cc`, 1M iterations, clang -O2.

| Buffer type | FNV-1a | XXH3 | Speedup |
|-------------|--------|------|---------|
| float3 (12B) | 1,173 ms | 1,075 ms | 1.1x |
| float[8] (32B) | 3,282 ms | 1,486 ms | 2.2x |
| float[100] (400B) | 48,140 ms | 9,289 ms | 5.2x |
| float[1000] (4KB) | 458,282 ms | 63,954 ms | 7.2x |
| matrix4d (128B) | 14,189 ms | 1,953 ms | 7.3x |
| int32[100] (400B) | 42,944 ms | 3,477 ms | 12.3x |

Zero collisions for both at 1M unique random inputs.

## Best Practices for USD Authors

- Reuse value objects rather than creating duplicates
- Use standard defaults (0, identity) that dedup well — +0.0 and -0.0 now dedup automatically
- Share time arrays across attributes when possible

## Enhancement Roadmap

Separate attribute/connection/relationship specs and Variant/VariantSet authoring are now implemented (see Implemented Features). Remaining:

- Performance optimizations (incremental writing, parallelism).
- Broader binary-compatibility verification against OpenUSD-written files.
