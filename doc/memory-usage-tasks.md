# Memory Usage Investigation and Tasks

End-to-end memory analysis of TinyUSDZ: from USD file loading through Tydra RenderScene conversion to WASM/WebGL rendering. Covers measurement procedures, known hotspots, optimizations done, and remaining TODOs.

## Profiling Results (2026-03-12, refactor-2026 branch)

**Test model:** `suzanne-subd-lv6.usdc` (180 MB on disk, 12M triangulated vertices, 4M faces)

### tusdcat (Stage loading only, parse-only with `-l`)

| Metric | Value |
|--------|-------|
| Peak heap (parse only, `-l`) | **281 MB** |
| Peak heap (with USDA print) | **2.46 GB** |
| USDC parser peak (self-reported, lazy mode) | **92.3 MB** |
| USDC parser peak (self-reported, non-lazy) | **282.7 MB** |
| Stage `estimate_memory_usage()` | **217 MB** (was 4 KB before recursive fix) |

After fixing MemoryBudgetManager (report timing, balanced budget release in lazy mode, decompression buffer tracking, temp vector tracking), the self-reported peak now matches actual heap within 1 MB in non-lazy mode (282.7 MB reported vs 281 MB massif). In lazy mode (default), the reported peak of 92.3 MB reflects the per-spec concurrent high-water mark — lower than the cumulative massif peak because the scratch buffer releases between specs.

**Peak breakdown (tusdcat `-l`, parse only, 281 MB):**
- 41% (~121 MB) — `UnpackValueRep` float3 array allocation via `DecodeFieldSet`
- 23% (~69 MB) — `ReadCompressedInts<int>` decompression buffers (2 call sites, ~34 MB each)
- 35% (~104 MB) — `ReadIntArray<int>` output vectors + other allocations
- All through: `DecodeFieldSet` → `ResolveFieldValuePairs` → `BuildPropertyMap` → `ReconstructPrim<GeomMesh>`

**Key observation:** Without `-l`, tusdcat serializes the entire Stage to stdout as USDA text, pushing peak to 2.46 GB. The extra ~2.2 GB is string formatting of 12M vertices. Parse-only mode (`-l`) isolates the actual parser memory footprint at 281 MB.

### tydra_to_renderscene (Full pipeline: parse + Tydra conversion)

| Metric | Value |
|--------|-------|
| Peak heap | **808 MB** |
| Mesh vertices (triangulated) | 12,091,392 |
| Normals count | 145,096,704 (12× face-vertex, not deduped) |
| Texcoords count | 96,731,136 |

**Peak breakdown (tydra at 808 MB):**

| Component | MB | % | Function |
|-----------|---:|--:|----------|
| BuildVertexIndicesFastImpl normals | 138 | 17.1% | `vector<array<float,3>>::resize` |
| CrateReader UnpackValueRep float3 | 115 | 14.3% | `DecodeFieldSet` → `ReadCompressedInts` |
| TriangulateVertexAttribute normals | 138 | 17.1% | `vector<uint8_t>::resize` |
| TriangulateVertexAttribute texcoords | 92 | 11.4% | `vector<uint8_t>::resize` |
| TriangulatePolygon index buffers (×4) | 139 | 17.1% | `vector<uint32_t>::reserve` (4 vectors) |
| ConvertMesh misc (texcoords, indices) | 55 | 6.7% | `vector<uint32_t/array<float,3>>` |
| CrateReader decompression int arrays | 24 | 2.9% | `ConvertMesh` scratch |

**Biggest finding:** Triangulation and vertex index building together consume **~530 MB (65%)** of peak. These are temporary working buffers that could be freed incrementally or processed in streaming fashion.

## Pipeline Overview

```
File on Disk (.usdc / .usda / .usdz)
    |
    v
[Stage 1] File I/O
    ├── mmap (zero-copy, preferred)
    └── ReadWholeFile (heap copy fallback)
    |
    v
[Stage 2] USDC/USDA Parsing → Stage object
    ├── CrateReader (USDC) — MemoryBudgetManager tracks allocations
    ├── AsciiParser (USDA) — CHECK_MEMORY_USAGE macro
    └── Composition (references, payloads, sublayers)
    |
    v
[Stage 3] Tydra ConvertToRenderScene → RenderScene object
    ├── ConvertMesh: points, normals, texcoords, tangents, indices
    ├── Texture loading: image decode into buffers[]
    ├── Material conversion: shader parameter extraction
    └── Skeleton/animation conversion
    |
    v
[Stage 4] Application consumption
    ├── Native: OpenGL/Vulkan vertex upload
    └── WASM: binding.cc → JS → Three.js / WebGL2
```

## Stage 1: File I/O

### mmap vs Heap Loading

| Method | Peak Heap | Approach |
|--------|----------|---------|
| `MMapFile()` | ~0 | OS page-maps file, no heap copy |
| `ReadWholeFile()` | file size | `std::vector<uint8_t>` allocation |

**Used in:** `tusdcat/main.cc` (line 164), `tinyusdz.cc` (line 215)

For a 188 MB USDC file, mmap avoids 188 MB of heap allocation entirely. Fallback to `ReadWholeFile` when mmap is unavailable (some embedded platforms).

### Configuration

| Option | Default | Location |
|--------|---------|----------|
| `USDLoadOptions::max_memory_limit_in_mb` | 16384 (16 GB) | `tinyusdz.hh` |
| `USDLoadOptions::max_allowed_asset_size_in_mb` | 1024 (1 GB) | `tinyusdz.hh` |
| `USDLoadOptions::max_image_width/height` | 2048 | `tinyusdz.hh` |

## Stage 2: USDC/USDA Parsing

### Memory Budget System

`MemoryBudgetManager` (RAII, `src/memory-budget.hh`) wraps every allocation in the crate reader:

```cpp
CheckAndReserve(bytes)  // allocate from budget, fail if over limit
Release(bytes)          // return to budget
```

Every array read, string allocation, and decompression buffer goes through this. Tracked metrics:

```cpp
struct USDCMemoryUsageReport {
    uint64_t current_usage_bytes;
    uint64_t peak_usage_bytes;
    uint64_t max_budget_bytes;
    uint64_t remaining_budget_bytes;
};
```

CLI: `tusdcat --memstat model.usdc`

### Major Allocation Hotspots in CrateReader

| Data Structure | Typical Size | Notes |
|---------------|-------------|-------|
| Token array | up to 64M tokens | All field names, prim names, property names |
| Field array | up to 256M fields | Name token + field index per field |
| Spec/Fieldset array | up to 256M specs | Unpacked property values |
| Decompression buffers | variable | Reused across calls (never shrunk) |
| Live fieldsets map | large | Unpacked field/value pairs; high with lazy loading disabled |
| TimeSamples dedup cache | variable | Unified `_dedup_array_cache` (single map, ValueRep encodes type in bits 48-55) |

### Lazy Property Construction

`TINYUSDZ_USDC_LAZY` environment variable enables deferred fieldset decoding — properties are only unpacked when accessed. In the current branch, the lazy fieldset cache has been **removed** (it caused invalid cache reuse across FVPairs sharing field_ids). The lazy path now decodes directly into a per-call scratch buffer, so each `ResolveFieldValuePairs` call re-decodes from the crate data. This trades slightly more CPU for correctness and lower peak memory (no unbounded cache growth).

### USDA Parser

`ascii-parser.cc` uses the same `CHECK_MEMORY_USAGE` pattern:

```cpp
_memory_usage += nbytes;
if (_memory_usage > _max_memory_limit_bytes) { return false; }
```

Default limit: 128 GB (in `usda-reader.hh`).

### Stage Memory Estimation (FIXED)

`Stage::estimate_memory_usage()` (`stage.cc`): Now performs full recursive traversal of the Prim tree using an iterative stack-based walk. For each Prim, estimates `_data` via `Value::estimate_memory_usage()`, string members, all properties via concrete type dispatch (GeomMesh, Xform, Material, etc.), and deep attribute memory for large typed attributes (points, normals, faceVertexIndices, etc.). Reports **217 MB** for `suzanne-subd-lv6.usdc` (was 4 KB before the fix).

`Layer::estimate_memory_usage()` (`layer.cc:577`): counts PrimSpecs (recursive), metadata strings, sublayers, VariantSetSpec internals (recursive). Property values are estimated via `Property::estimate_memory_usage()`.

## Stage 3: Tydra RenderScene Conversion

### Per-Vertex Memory Breakdown

For a mesh with N vertices (after index building):

| Attribute | Format | Bytes/vertex | Notes |
|-----------|--------|-------------|-------|
| positions | float3 | 12 | Always present |
| normals | float3 | 12 | Computed if missing |
| texcoords (slot 0) | float2 | 8 | Per UV set |
| texcoords (slot 1) | float2 | 8 | Optional second UV |
| tangents (packed) | uint32 / half4 | 4 / 8 | After quantization |
| tangents (legacy) | float3 | 12 | Before quantization |
| binormals (legacy) | float3 | 12 | Eliminated by quantization |
| vertex colors | float3 | 12 | If present |
| vertex opacities | float | 4 | If present |
| joint indices | int × N_bones | 16 (4 bones) | Skinned meshes |
| joint weights | float × N_bones | 16 (4 bones) | Skinned meshes |

**Index data:** `uint32_t` per face-vertex for both original and triangulated topology, plus original-to-triangulated mapping.

**Blend shapes:** Full vertex buffer per morph target (12 bytes/vertex per target for position offsets).

### Example: 100K-Vertex Skinned Character

| Component | Size | Notes |
|-----------|------|-------|
| Positions | 1.2 MB | |
| Normals | 1.2 MB | |
| Texcoords (1 set) | 0.8 MB | |
| Tangents (10_10_10_2) | 0.4 MB | Was 2.4 MB with float3+float3 |
| Joint indices (4 bones) | 1.6 MB | |
| Joint weights (4 bones) | 1.6 MB | |
| Index buffers | ~1.2 MB | |
| **Subtotal geometry** | **~8 MB** | |
| Textures (5 × 2K RGBA uint8) | **80 MB** | Dominates total |
| **Total** | **~88 MB** | |

### Texture Memory

Textures are typically the largest memory consumer. Stored in `RenderScene::buffers[]` as decoded pixel data.

| Texture Format | Size per 2K×2K | Size per 4K×4K |
|---------------|---------------|---------------|
| RGB uint8 | 12 MB | 48 MB |
| RGBA uint8 | 16 MB | 64 MB |
| RGB float32 | 48 MB | 192 MB |
| RGBA float32 | 64 MB | 256 MB |

Key config: `preserve_texel_bitdepth = true` keeps 8-bit textures as uint8 instead of converting to float32 (4x savings).

### RenderScene Memory Estimation

`RenderMesh::estimate_memory_usage()` (`render-data.cc`):
- Counts: points, indices, normals, tangents, binormals, texcoords, colors, opacities, JointAndWeight (jointIndices, jointWeights), ShapeTarget (pointIndices, pointOffsets, normalOffsets, inbetweens), MaterialSubset (usdIndices, triangulatedIndices, strings)

`RenderScene::estimate_memory_usage()` (`render-data.cc`):
- Counts: mesh details (via `RenderMesh::estimate_memory_usage()`), buffer data (textures), Node tree (recursive children, strings), RenderMaterial (strings, spectral data), AnimationClip (strings, KeyframeSampler times/values, channels), SkelHierarchy (strings, SkelNode tree, parent_joint_indices, bind_transforms, rest_transforms), TextureImage (asset_identifier)

### Memory Config Options (MeshConverterConfig)

| Option | Default | Effect |
|--------|---------|--------|
| `tangent_storage` | Packed1010102 (WASM) / PackedFp16 (native) | Packed saves 67-83% tangent memory |
| `compute_tangents_only_with_normal_map` | true | Skip tangent computation if no normal map |
| `defer_tangent_computation` | false (true in WASM binding) | Defer tangent work to reduce initial load |
| `lowmem` | false (true in WASM binding) | Free source GeomMesh after conversion (**currently commented out in code**) |
| `build_vertex_indices` | true | Dedup vertices; more temp memory, less final |
| `prefer_non_indexed` | false | Skip index building for non-indexable meshes |
| `preserve_texel_bitdepth` | false (true in WASM binding) | Keep uint8 textures, avoid float32 bloat |
| `load_texture_assets` | true | Set false to skip texture loading entirely |

### WASM-Specific Memory Limits

| Build | Default Limit |
|-------|--------------|
| 32-bit WASM | 2 GB |
| 64-bit WASM (MEMORY64) | 8 GB |

Set via `max_memory_limit_mb_` in `binding.cc`.

## Stage 4: Application Consumption (WASM Binding)

The WASM binding (`web/binding.cc`) converts RenderMesh data to JS-accessible typed arrays:

- **Tangent cache:** `tangents4_cache_[mesh_id]` — vec4 float unpacked from packed format (created on demand per mesh)
- **Reordered mesh cache:** `reordered_mesh_cache_[mesh_id]` — vertex data reordered for draw calls

These caches add temporary memory on top of the RenderScene data. They are invalidated when `computeMeshTangents()` is called.

## Measurement Procedures

### 1. tusdcat Memory Report

```bash
./build/examples/tusdcat/tusdcat --memstat model.usdc
```

Reports USDC parser current/peak/budget memory. Shows Stage `estimate_memory_usage()`.

### 2. tydra_to_renderscene

```bash
./build/examples/tydra_to_renderscene/tydra_to_renderscene --memstat model.usdc
```

Reports Stage `estimate_memory_usage()` after loading and `RenderScene::estimate_memory_usage()` after Tydra conversion. Use `--nodump` to suppress USDA output.

### 3. Tangent Benchmark

```bash
cd tests/feat/tangent && make
./bench_tangent --quality --sizes 32,128,512,1024 --ico-levels 3,5,7
```

Reports per-mesh: computation speed, working memory, quality, quantization error, storage comparison.

### 4. Valgrind/Massif Heap Profiling

```bash
# Peak heap profile
valgrind --tool=massif --pages-as-heap=no \
  ./build/examples/tydra_to_renderscene/tydra_to_renderscene model.usdc
ms_print massif.out.<pid>

# Leak detection
valgrind --leak-check=full --show-leak-kinds=all \
  ./build/examples/tusdcat/tusdcat model.usdc
```

Existing massif snapshots: `massif.out.*` in repo root.

### 5. WASM Browser Profiling

- Chrome DevTools → Memory → Take heap snapshot before/after USD load
- Performance tab → Memory checkbox for real-time allocation timeline
- Compare `defer_tangent_computation` on/off, `lowmem` on/off

### 6. C++ API

```cpp
// After loading
size_t stage_mem = stage.estimate_memory_usage();

// After conversion
size_t scene_mem = render_scene.estimate_memory_usage();
for (const auto &mesh : render_scene.meshes) {
    size_t mesh_mem = mesh.estimate_memory_usage();
}
```

## Known Issues

### MemoryBudgetManager Blind Spots (FIXED)

Previously the self-reported USDC parser memory was 49 KB for a 180 MB file producing 281 MB actual heap peak (~5700× undercount). Fixed by:
1. Moving `GetMemoryUsageReport()` to after `ReconstructStage()` (report was captured before property reconstruction)
2. Balanced budget release in lazy mode (scratch buffer releases budget when reused)
3. Persistent decompression buffer tracking (only reserve growth delta, never release)
4. Temporary vector tracking in `ReadFloatArray`/`ReadDoubleArray`/`ReadHalfArray`

Now reports 282.7 MB in non-lazy mode (matches massif within 1 MB) and 92.3 MB in lazy mode (per-spec concurrent high-water mark).

### TypedArray Ownership (RESOLVED)

**Docs:** `doc/TYPED_ARRAY_REVIEW_2025.md` removed (described issues that have been fixed)

The `TypedArrayPtr<T>` class (packed 64-bit smart pointer with dedup flag in bit 63) has been **removed** as dead code. The dedup caches were previously refactored to store `size_t` indices instead of `TypedArrayPtr` objects, so the ownership/copy-semantics bugs documented in `TYPED_ARRAY_REVIEW_2025.md` no longer apply. The 22 separate type-specific dedup caches have been consolidated into a single unified `_dedup_array_cache` map (ValueRep encodes type in bits 48-55, so keys from different types never collide). ~18 dead scalar dedup caches were also removed.

### lowmem Flag (IMPLEMENTED)

`MeshConverterConfig::lowmem` now frees source GeomMesh arrays (points, normals, faceVertexIndices, faceVertexCounts) after Tydra conversion via `set_value({})`. Enabled by default in WASM binding.

### Deferred Tangent + Index Build Race (FIXED)

Commit `d29d804a`. When `defer_tangent_computation=true` and mesh had a normal map, vertex index building was skipped, causing garbled textures. Fixed by reordering the defer check before the index build decision.

### Lazy Fieldset Cache Removed (FIXED)

The `_lazy_fieldset_cache` in usdc-reader caused invalid cache reuse when different FVPairs shared field_ids, producing corrupted `xformOpOrder` type comparisons and other reconstruction failures. Replaced with direct scratch-buffer decode per call.

## Optimizations Done

| Optimization | Savings | Where |
|-------------|---------|-------|
| mmap file loading | ~file_size heap | `tusdcat`, `tinyusdz.cc` |
| Tangent quantization (10_10_10_2) | 83% tangent storage | `render-data.cc`, `tangent-quantize.hh` |
| Tangent quantization (Fp16x4) | 67% tangent storage | Same |
| Zero-copy tangent computation | ~200 MB on large meshes | `render-data.cc` |
| Skip tangent for non-normal-map meshes | Full tangent cost | `render-data.cc` |
| Deferred tangent computation (WASM) | Initial load reduction | `render-data.cc`, `binding.cc` |
| `preserve_texel_bitdepth` | Up to 4x texture memory | `binding.cc` config |
| CrateReader buffer reuse | Reduced peak during decompress | `crate-reader.cc` |
| CrateReader streaming decompress | Reduced peak for >1M element arrays | `crate-reader.cc` |
| Bone count reduction | Reduced joint/weight arrays | `render-data.cc` |
| Remove lazy fieldset cache | Eliminates unbounded cache growth, fixes correctness | `usdc-reader.cc` |
| Fix MemoryBudgetManager tracking | 49 KB → 282.7 MB reported (matches massif) | `crate-reader.cc`, `usdc-reader.cc`, `tusdcat/main.cc` |
| Normal quantization (SNorm8x3 default, also SNorm16x3 / 10_10_10_2) | 75% normal storage (SNorm8) | `render-data.cc`, `tangent-quantize.hh` |
| Quantized normal dedup before triangulation | Eliminates 138 MB triangulation buffer for smooth meshes | `render-data.cc` |
| `lowmem` GeomMesh freeing | ~115 MB source data freed post-conversion | `render-data.cc` |
| Consolidate 22 dedup caches → 1 | Reduced hash map overhead, simpler code | `crate-reader.hh`, `crate-reader-timesamples.cc` |
| Remove TypedArrayPtr dead code | ~290 lines removed, eliminates ownership confusion | `typed-array.hh`, `timesamples.hh`, `timesamples-pprint.cc` |
| Remove ~18 dead scalar dedup caches | Reduces CrateReader struct size | `crate-reader.hh` |
| TimeSamples move in lazy mode | Eliminates deep copy of TimeSamples in lazy property construction | `usdc-reader.cc` |
| Fix Stage `estimate_memory_usage()` | 4 KB → 217 MB (full recursive Prim tree walk) | `stage.cc`, `prim-types.cc`, `value-types.cc` |
| TimeSamples `estimate_memory_usage()` | Covers `_array_values`, `_value_array_storage`, `_value_array_refs` | `timesamples.hh` |
| CrateReader decompression buffer shrink | ~68 MB reclaimed after parsing | `crate-reader.hh`, `usdc-reader.cc` |
| Free triangulation intermediates | ~161 MB unconditional + ~112 MB with lowmem | `render-data.cc` |
| Free vertex_output fields after set_buffer | Reduces peak overlap in BuildVertexIndicesImpl | `render-data.cc` |
| Free dedup buckets after vertex dedup | Reduces peak in BuildVertexIndicesImpl | `render-data.cc` |
| Complete RenderMesh memory estimation | JointAndWeight, ShapeTarget, MaterialSubset internals | `render-data.cc` |
| Complete RenderScene memory estimation | Node tree, RenderMaterial, AnimationClip, SkelHierarchy, TextureImage | `render-data.cc` |
| Complete Layer memory estimation | VariantSetSpec internals (recursive PrimSpec trees) | `layer.cc` |

## TODO Tasks

### Critical (profiling-validated, highest impact)

- [x] **Free triangulation and vertex-build intermediates** — DONE. Triangulation intermediates (`triangulatedToOrigFaceVertexIndexMap`, `triangulatedFaceCounts`) freed unconditionally after step 4 attribute triangulation. Pre-triangulation topology (`usdFaceVertexCounts`, `usdFaceVertexIndices`) freed under `lowmem` guard when mesh is triangulated. In `BuildVertexIndicesImpl`, dedup buckets freed after dedup loop, and each `vertex_output` field freed immediately after `set_buffer()` copies it to avoid source+destination peak overlap.
- [x] **Fix MemoryBudgetManager blind spots** — DONE. Reported 49 KB when actual heap was 281 MB. Fixed: report timing, lazy budget release, decompression buffer tracking, temp vector tracking. Now reports 282.7 MB non-lazy (matches massif) / 92.3 MB lazy (concurrent high-water mark).
- [x] **Implement `lowmem` GeomMesh freeing** — DONE. The `lowmem` config flag now frees source GeomMesh arrays (points, normals, faceVertexIndices, faceVertexCounts) after Tydra conversion via `set_value({})`. Enabled by default in WASM binding.
- [x] **Fix TypedArray ownership model** — DONE. `TypedArrayPtr<T>` dead code removed entirely. Dedup caches already use `size_t` indices. 22 array caches consolidated into 1 unified map. ~18 dead scalar caches removed.

### High Priority

- [x] **Deduplicate normals before triangulation** — DONE. Added `TryQuantizedNormalDedup` fallback: when exact-epsilon `TryConvertFacevaryingToVertex` fails, quantizes normals to 10-bit SNORM (pack_normal_1010102) and compares packed uint32 values. This catches subdivision surface limit normals where the same logical normal differs by floating-point noise. On success, converts face-varying→vertex, eliminating the 138 MB triangulation buffer entirely for smooth-shaded meshes.
- [x] **Pre-size triangulation output vectors** — DONE (was stale). `TriangulatePolygon` already reserves exact sizes: `estimatedTriangles = numFaceVertexIndices - 2 * numFaces`.
- [x] **Add `--memstat` to `tydra_to_renderscene`** — DONE. Reports Stage estimate after load and RenderScene estimate after Tydra conversion.
- [x] **Complete `estimate_memory_usage()` for JointAndWeight** — DONE. Now counts `jointIndices` and `jointWeights` vector capacities.
- [x] **Complete `estimate_memory_usage()` for ShapeTarget** — DONE. Now counts `pointIndices`, `pointOffsets`, `normalOffsets`, strings, and `InbetweenShapeTarget` inbetweens.
- [x] **Complete `estimate_memory_usage()` for MaterialSubset** — DONE. Now counts `usdIndices`, `triangulatedIndices`, and strings.
- [x] **Add RenderMaterial detailed estimation** — DONE. Now counts strings, spectral data vectors. ShaderParam fields are POD (no heap allocs).
- [x] **Add AnimationClip detailed estimation** — DONE. Now counts strings, KeyframeSampler times/values vectors, AnimationChannel array.
- [x] **Add SkelHierarchy detailed estimation** — DONE. Now counts strings, recursive SkelNode tree, `parent_joint_indices`, `bind_transforms`, `rest_transforms`, `anim_ids`.
- [ ] **Measure end-to-end WASM memory** on real-world models (skinned character USDZ) with browser DevTools. Compare before/after tangent quantization and deferred loading.
- [ ] **Add Hybrid to `TangentComputationMethod` enum** — implemented in `fast-mikktspace.hh` but not wired into config enum or dispatch.

### Medium Priority

- [x] **Move-from CrateValue during Property reconstruction** — DONE. `ParseProperty` now moves TimeSamples in lazy mode (scratch buffer is local, safe to move) and copies in non-lazy mode (shared `_live_fieldsets`).
- [x] **Complete Stage memory estimation** — DONE. `Stage::estimate_memory_usage()` now recursively walks entire Prim tree, estimating `_data`, string members, all properties via concrete type dispatch, and deep attribute memory. Reports 217 MB (was 4 KB).
- [x] **Add PrimVar `estimate_memory_usage()` to Attribute** — DONE. `Attribute::estimate_memory_usage()` now delegates to `_var.estimate_memory_usage()` which calls `Value::estimate_memory_usage()` + `TimeSamples::estimate_memory_usage()`.
- [x] **Complete Layer memory estimation** — DONE. `EstimatePrimSpecMemory` now recursively estimates `VariantSetSpec` internals (variant name strings, nested PrimSpec trees). Property values were already covered.
- [ ] **Profile texture memory separately** — textures dominate total memory. Add per-texture size reporting to `estimate_memory_usage()` output.
- [ ] **Benchmark deferred vs immediate tangent on WASM** — measure initial load time and peak memory difference.
- [ ] **Reduce WASM default memory limit** — 8 GB for 64-bit WASM may be too permissive for web deployment. Profile real workloads to set tighter defaults.
- [x] **Normal quantization** — DONE. Packs normals to INT_2_10_10_10_REV (4 bytes/vertex, 67% savings). Default on WASM, opt-in on native via `MeshConverterConfig::normal_storage`.

### Low Priority

- [ ] **Three.js packed tangent `BufferAttribute`** — Three.js doesn't natively support `GL_INT_2_10_10_10_REV`. Investigate `InterleavedBufferAttribute` or custom WebGL calls to avoid unpack-to-float in binding.
- [ ] **Memory-based CrateWriter** — `usdc-writer.cc` TODO for in-memory USDC writing (currently uses temp file).
- [ ] **Multi-threaded tangent computation** — Hybrid and FastMikkTSpace are single-threaded. Parallelize per-face derivative phase.
- [ ] **Composition memory tracking** — `composition.hh` has `max_memory_limit_mb` but no usage reporting for reference/payload resolution overhead.
- [x] **CrateReader decompression buffer shrink** — DONE. Added `ShrinkDecompressionBuffers()` to CrateReader that swap-with-empty frees both decompression buffers and releases their budget. Called after `BuildLiveFieldSets()` in non-lazy mode and after `ReconstructStage()` in lazy mode. USDC parser current usage drops to 0 bytes after parsing.

## Key Files

| File | Purpose |
|------|---------|
| `src/tinyusdz.hh` | `USDLoadOptions` (memory limits, asset limits) |
| `src/memory-budget.hh` | `MemoryBudgetManager` RAII class |
| `src/usdc-reader.hh` | `USDCMemoryUsageReport`, memory tracking API |
| `src/crate-reader.cc` | Budget-checked allocations, buffer reuse, streaming decompress |
| `src/stage.cc` | `Stage::estimate_memory_usage()` |
| `src/layer.cc` | `Layer::estimate_memory_usage()` |
| `src/value-types.cc` | `Value::estimate_memory_usage()` |
| `src/prim-types.cc` | Property/Attribute/Relationship estimation |
| `src/primvar.hh` | `PrimVar::estimate_memory_usage()` |
| `src/tydra/render-data.hh` | `MeshConverterConfig` (all memory-related options) |
| `src/tydra/render-data.cc` | `RenderMesh/RenderScene::estimate_memory_usage()`, `QuantizeMeshTangents()` |
| `src/tydra/tangent-quantize.hh` | Packed tangent formats |
| `src/tydra/common-types.hh` | `MemoryConfig` struct |
| `web/binding.cc` | WASM memory config, tangent cache, deferred computation |
| `src/timesamples.hh` | `TimeSamples::estimate_memory_usage()`, sample storage |
| `examples/tusdcat/main.cc` | mmap loading, `--memstat` reporting |
| `examples/tydra_to_renderscene/to-renderscene-main.cc` | Conversion example with `--memstat` reporting |
| `tests/feat/tangent/bench_tangent.cc` | Tangent memory/quality benchmark |
| `doc/TYPED_ARRAY_REVIEW_2025.md` | Removed (TypedArrayPtr ownership issues resolved) |
| `doc/tydra-tangent.md` | Tangent computation and quantization docs |
