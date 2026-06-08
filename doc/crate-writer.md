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

**Core**: Binary v0.8.0 writing, file header, TOC, LZ4 compression, NaN-aware value deduplication (XXH3), path tree encoding, integer-array compression. Tagged float/double-array compression via a runtime writer option (see [below](#tagged-floatdouble-array-compression); off by default).

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

Arrays can be both deduplicated and compressed. The decision is made per array at
write time:

- **Integer** (`int`/`uint`/`int64`/`uint64`): `Sdf_IntegerCompression` when the
  array has ≥ `MinCompressedArraySize` (= **16**) elements; otherwise raw. No code
  byte — the element type already tells the reader the layout.
- **Floating point** (`half`/`float`/`double`, crate ≥ 0.6.0, ≥ 16 elements):
  *tagged* compression — integer encoding if every value is exactly an int32,
  else a lookup table if there are few distinct values, else uncompressed. See
  [Tagged Float/Double Array Compression](#tagged-floatdouble-array-compression).
- **Other types**: Uncompressed.

For *which* values are eligible to be written out-of-line at all (vs. inlined in
the 48-bit ValueRep payload), see [Value Classification](#value-classification).

### Tagged Float/Double Array Compression

This is the format OpenUSD calls "compressed floating point arrays" (crate
version 0.6.0+). Reference: `_WritePossiblyCompressedArray<float/double/GfHalf>`
in `pxr/usd/sdf/crateFile.cpp` (OpenUSD v25.08). The matching TinyUSDZ reader is
`ReadFloatArray`/`ReadDoubleArray`/`ReadHalfArray` in `src/crate-reader-arrays.cc`.

#### When OpenUSD creates a compressed float/double array

For a `VtArray<float|double|GfHalf>`, OpenUSD decides in this exact order:

1. **Gate.** If the crate version is `< 0.6.0`, or the array has fewer than
   `MinCompressedArraySize` (**16**) elements → write **uncompressed** (raw
   contiguous values, ValueRep *not* marked compressed, no code byte).

2. **Integer encoding — code `'i'`.** Test every element with
   `isIntegral(fp) = (int32_min <= fp && fp <= int32_max) && T(int32_t(fp)) == fp`.
   If *all* elements pass, the array is rewritten as a compressed `int32` stream.
   The reader reconstructs each value as `T(int32)`. Notes:
   - The range check happens *before* the cast (avoids UB on NaN/Inf/out-of-range).
   - `-0.0` satisfies `T(int32_t(-0.0)) == -0.0` (since `+0.0 == -0.0`), so the
     `'i'` path **collapses `-0.0` to `+0.0`**. This is intrinsic to the encoding.

3. **Lookup table — code `'t'`.** Otherwise OpenUSD builds a table of distinct
   values, bailing out as soon as it would exceed
   `maxLutSize = min(array.size() / 4, 1024)` distinct entries (a profitability
   bound; the 1024 ceiling also bounds the linear-search cost). If the table fits,
   the array is written as the table plus a compressed `uint32` index stream.
   OpenUSD finds duplicates with `operator==`, so it merges `+0.0`/`-0.0` and never
   merges `NaN` (each `NaN` is distinct, which usually pushes a `NaN`-heavy array
   over `maxLutSize` and into the uncompressed fallback).

4. **Uncompressed fallback.** If neither `'i'` nor `'t'` applies, write raw values
   with the ValueRep *not* marked compressed (so no code byte).

The ValueRep's `IsCompressed` bit is what selects the branch on read: when set and
`count >= 16`, the reader expects a code byte; otherwise it reads raw values.

#### Wire format

Element `count` is always written first. Then, only when the ValueRep is marked
compressed *and* `count >= MinCompressedArraySize`:

```
uint64  count
int8    code                     // 'i' or 't'
  'i':  <compressed int32 stream> // uint64 compSize + compSize bytes (Sdf_IntegerCompression)
  't':  uint32  lutSize
        T[lutSize]               // raw little-endian lookup-table entries
        <compressed uint32 stream> // uint64 compSize + compSize bytes (the indices)
```

If `count < 16` (even on the "compressed" path) or the ValueRep is not marked
compressed, the payload is simply `uint64 count` followed by raw `T[count]` — no
code byte.

#### TinyUSDZ implementation (runtime option, default off)

TinyUSDZ writes float/double arrays **uncompressed by default**. The tagged
encoding above is implemented and toggled by a runtime writer option (no compile
flag), so the default output is unchanged:

| Layer | Flag (default `false`) |
|-------|------------------------|
| Low-level crate writer | `CrateWriter::Options::enable_float_array_compression` |
| High-level USDC writer | `USDWriteOptions::compress_float_arrays` (→ `SaveAsUSDCToFile`) |
| `tusdcat` CLI | `--compress-float-arrays` |

```cpp
tinyusdz::USDWriteOptions wopts;
wopts.compress_float_arrays = true;  // default false
tinyusdz::usdc::SaveAsUSDCToFile("out.usdc", stage, &warn, &err, wopts);
```
```bash
tusdcat --compress-float-arrays input.usda -o out.usdc   # default: off
```

- Helpers: `CrateWriter::WriteCompressedFloatArray` /
  `WriteCompressedDoubleArray` (`src/crate-writer.cc`), invoked from the
  `std::vector<float>` / `std::vector<double>` branches of `WriteValueData`
  (`src/crate-writer-values.cc`). Compression is attempted only when both
  `Options.enable_float_array_compression` *and* `Options.enable_compression` are
  set (the latter gates the integer-stream compressor the `'i'`/`'t'` payloads use).
- Matches OpenUSD: `MinCompressedArraySize = 16`, the `'i'` integral test with the
  pre-cast range guard, and the `min(count/4, 1024)` LUT bound.
- Difference from OpenUSD (still Pixar-readable): the LUT is keyed on the **raw bit
  pattern** rather than `operator==`, so `-0.0` and `NaN` round-trip *exactly*
  through the `'t'` path (OpenUSD merges `±0.0` and explodes the LUT on `NaN`). The
  `'i'` path still collapses `-0.0 → +0.0`, identical to OpenUSD.
- `half[]` arrays are left uncompressed by TinyUSDZ even with the option on (already
  2 bytes/element; only `float[]`/`double[]` are handled).
- Verified: `crate_writer_float_double_array_compression_roundtrip_test` runs every
  data pattern with the option both ON and OFF (`'i'`/`'t'`/raw, bit-exact) plus a
  file-size-shrinks assertion; Pixar `usdcat` reads both `'i'` and `'t'` TinyUSDZ
  output with exact values.

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
