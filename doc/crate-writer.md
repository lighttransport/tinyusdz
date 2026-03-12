# USDC Crate Writer

The TinyUSDZ USDC Crate Writer (`src/crate-writer.{cc,hh}`) writes USD Stage data to binary USDC (Crate) format version 0.8.0. Status: experimental but functional with comprehensive test coverage.

## Integration Points

- Core: `src/crate-writer.cc` (3,470+ lines)
- Stage converter: `src/stage-converter.cc`
- CLI: `examples/tusdcat` with `-o/--output` option
- Unit tests: 61 tests (all passing)

## Usage

```bash
# Basic conversion
./tusdcat input.usda -o output.usdc
./tusdcat output.usdc  # Verify readback

# Flattened composition
./tusdcat --flatten --composition=r,p input.usda -o flattened.usdc
```

## Implemented Features

**Core**: Binary v0.8.0 writing, file header, TOC, LZ4 compression, value deduplication, path tree encoding.

**Data Types**: Basic types (int, float, double, bool, string, token), vectors, matrices, paths, TimeSamples with ValueRep format, ListOp<T> for references/payloads/inherits, TokenListOp, StringListOp, PathListOp, Dictionary values, VariantSelectionMap.

**Geometry**: Mesh (with animated points, normals, advanced features, blend shapes), Cone, Cylinder, Capsule, GeomPoints, GeomCamera, GeomBasisCurves, GeomNurbsCurves, GeomSubset, GeomPointInstancer (with instance offsets), Model, Scope, GPrim properties (visibility, purpose, extent, normal interpolation).

**Materials**: Material, Shader, NodeGraph, UsdPreviewSurface, UsdUVTexture, UsdPrimvarReader, UsdTransform2d, material binding.

**Lighting**: SphereLight, RectLight, DistantLight, DomeLight, multiple lights, light filters.

**Skeletal**: Skeleton, SkelAnimation, SkelRoot, skeletal binding.

**Metadata**: Prim/attribute/relationship metadata, CustomData, AssetInfo, layer metadata (framesPerSecond, startTimeCode, endTimeCode, upAxis, metersPerUnit).

**Composition**: References with customData, payloads, inherits, sublayers with LayerOffset, specializes arc.

**Safety**: Error context reporting, memory limits, filesize limits, validation (enable/disable), compression.

## Not Yet Implemented

- Separate attribute/connection specs (currently embedded in parent prim)
- Variant/VariantSet specs

## Resolved Issues

### TypeName Encoding (Fixed 2025-11-16)

Token index (uint32_t) was stored instead of the token object. Fixed in 3 locations in `src/stage-converter.cc`.

### TimeSamples Size Mismatch (Fixed 2025-11-16)

`get_samples()` returned empty when `_use_pod` was true but `_pod_samples` was empty. Fixed in `src/timesamples.hh` to fall through to unified POD storage. Also fixed TimeSamples binary format to use ValueRep structures.

### SpecTypePseudoRoot Ordering (Fixed 2025-11-16)

Spec sorting didn't guarantee PseudoRoot at index 0. Fixed comparator in `src/crate-writer.cc` to always sort PseudoRoot first with post-sort validation.

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

`_ValueHandler<T>` template deduplicates data values:
- Separate dedup maps for scalars and arrays per concrete type
- Lazy allocation (maps created on first use)
- Cleared after write

### Value Classification

1. **Always Inlined** (<=4 bytes or index types): bool, int32, float, string, token, path
2. **Conditionally Inlined**: Values that happen to fit in 4 bytes
3. **Value-Deduplicated**: Larger values hashed and stored once
4. **Array-Deduplicated**: Separate map, empty arrays always inlined

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

## Best Practices for USD Authors

- Reuse value objects rather than creating duplicates
- Use standard defaults (0, identity) that dedup well
- Share time arrays across attributes when possible

## Enhancement Roadmap

Priority order:
1. Separate attribute/connection specs (correctness)
2. Variant/VariantSet support
3. Performance optimizations (incremental writing, parallelism)
