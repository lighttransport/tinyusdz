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
| Stage `estimate_memory_usage()` | 4 KB |

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
| TimeSamples dedup caches | variable | 22 type-specific caches (int32, half, float, double, quat, matrix, etc.) |

### Lazy Property Construction

`TINYUSDZ_USDC_LAZY` environment variable enables deferred fieldset decoding — properties are only unpacked when accessed. In the current branch, the lazy fieldset cache has been **removed** (it caused invalid cache reuse across FVPairs sharing field_ids). The lazy path now decodes directly into a per-call scratch buffer, so each `ResolveFieldValuePairs` call re-decodes from the crate data. This trades slightly more CPU for correctness and lower peak memory (no unbounded cache growth).

### USDA Parser

`ascii-parser.cc` uses the same `CHECK_MEMORY_USAGE` pattern:

```cpp
_memory_usage += nbytes;
if (_memory_usage > _max_memory_limit_bytes) { return false; }
```

Default limit: 128 GB (in `usda-reader.hh`).

### Stage Memory Estimation

`Stage::estimate_memory_usage()` (`stage.cc:937`): counts `sizeof(Stage)`, `StageMetas`, root node names/paths, prim ID cache. **Incomplete** — does not recurse into Prim properties, children, or metadata.

`Layer::estimate_memory_usage()` (`layer.cc:577`): counts PrimSpecs (recursive), metadata strings, sublayers. **Incomplete** — misses nested Property values and complex PrimMeta.

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

`RenderMesh::estimate_memory_usage()` (`render-data.cc:11891`):
- Counts: points, indices, normals, tangents, binormals, texcoords, colors, opacities
- **Missing:** JointAndWeight internals, ShapeTarget data, MaterialSubset indices

`RenderScene::estimate_memory_usage()` (`render-data.cc:11955`):
- Counts: mesh details (via `RenderMesh::estimate_memory_usage()`), buffer data (textures)
- **Missing:** RenderMaterial internals, AnimationClip keyframes, SkelHierarchy internals

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
./build/examples/tydra_to_renderscene/tydra_to_renderscene model.usdc
```

Currently does NOT report memory. **TODO:** Add `--memstat` flag that prints `RenderScene::estimate_memory_usage()` breakdown.

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

### TypedArray Ownership (CRITICAL)

**Docs:** `doc/TYPED_ARRAY_REVIEW_2025.md`, `doc/MEMORY_LEAK_FIX_COMPLETE.md`

`TypedArray<T>` has broken copy semantics: copies are marked as dedup references to prevent double-free, creating use-after-free vulnerability. Interim fix: manual cleanup in `CrateReader::~CrateReader()` destructor.

- **Memory impact:** ~75 MB leak per load (typical animated model), ~10 GB for large production scenes
- **Affected:** 22 dedup caches in `crate-reader-timesamples.cc`
- **Long-term fix:** Migrate to `std::shared_ptr<TypedArrayImpl<T>>`

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
| Normal quantization (10_10_10_2) | 67% normal storage | `render-data.cc`, `tangent-quantize.hh` |
| Quantized normal dedup before triangulation | Eliminates 138 MB triangulation buffer for smooth meshes | `render-data.cc` |
| `lowmem` GeomMesh freeing | ~115 MB source data freed post-conversion | `render-data.cc` |

## TODO Tasks

### Critical (profiling-validated, highest impact)

- [ ] **Streaming triangulation to eliminate 530 MB peak** — `TriangulateVertexAttribute` and `BuildVertexIndicesFastImpl` together allocate 4 massive temporary vectors (normals ×2, texcoords, index buffers) that dominate 65% of the 808 MB peak. Process faces in chunks and write directly to the output RenderMesh, freeing each chunk's working set before the next. Estimated savings: ~400 MB on the 12M-vertex test case.
- [x] **Fix MemoryBudgetManager blind spots** — DONE. Reported 49 KB when actual heap was 281 MB. Fixed: report timing, lazy budget release, decompression buffer tracking, temp vector tracking. Now reports 282.7 MB non-lazy (matches massif) / 92.3 MB lazy (concurrent high-water mark).
- [x] **Implement `lowmem` GeomMesh freeing** — DONE. The `lowmem` config flag now frees source GeomMesh arrays (points, normals, faceVertexIndices, faceVertexCounts) after Tydra conversion via `set_value({})`. Enabled by default in WASM binding.
- [ ] **Fix TypedArray ownership model** — migrate from manual cleanup to `std::shared_ptr<TypedArrayImpl<T>>`. Affects 22 dedup caches. Fixes ~75 MB leak per load. See `doc/TYPED_ARRAY_REVIEW_2025.md`.

### High Priority

- [x] **Deduplicate normals before triangulation** — DONE. Added `TryQuantizedNormalDedup` fallback: when exact-epsilon `TryConvertFacevaryingToVertex` fails, quantizes normals to 10-bit SNORM (pack_normal_1010102) and compares packed uint32 values. This catches subdivision surface limit normals where the same logical normal differs by floating-point noise. On success, converts face-varying→vertex, eliminating the 138 MB triangulation buffer entirely for smooth-shaded meshes.
- [x] **Pre-size triangulation output vectors** — DONE (was stale). `TriangulatePolygon` already reserves exact sizes: `estimatedTriangles = numFaceVertexIndices - 2 * numFaces`.
- [ ] **Add `--memstat` to `tydra_to_renderscene`** — call `RenderScene::estimate_memory_usage()` with per-mesh and per-texture breakdown. Currently no memory reporting at all.
- [ ] **Complete `estimate_memory_usage()` for JointAndWeight** (`render-data.cc:11938`) — skinning data not counted.
- [ ] **Complete `estimate_memory_usage()` for ShapeTarget** (`render-data.cc:11943`) — blend shape data not counted.
- [ ] **Complete `estimate_memory_usage()` for MaterialSubset** (`render-data.cc:11949`) — material subset indices not counted.
- [ ] **Add RenderMaterial detailed estimation** — `RenderScene::estimate_memory_usage()` only counts `sizeof(RenderMaterial)`, not internal shader parameters, texture references, or string data.
- [ ] **Add AnimationClip detailed estimation** — keyframe data and channel storage not counted.
- [ ] **Add SkelHierarchy detailed estimation** — bone hierarchy, bind poses, inverse bind matrices not counted.
- [ ] **Measure end-to-end WASM memory** on real-world models (skinned character USDZ) with browser DevTools. Compare before/after tangent quantization and deferred loading.
- [ ] **Add Hybrid to `TangentComputationMethod` enum** — implemented in `fast-mikktspace.hh` but not wired into config enum or dispatch.

### Medium Priority

- [ ] **Move-from CrateValue during Property reconstruction** — `ParseProperty` now accepts `allow_move_from_fvs` but many call sites still copy large arrays. Audit all `BuildPropertyMap` → `ParseProperty` paths to ensure large `vector<float3>` etc. are moved, not copied.
- [ ] **Complete Stage/Layer memory estimation** — `Stage::estimate_memory_usage()` doesn't recurse into Prim properties, children, or metadata. `Layer::estimate_memory_usage()` misses nested Property values.
- [ ] **Add PrimVar `estimate_memory_usage()` to Attribute** — PrimVar has the method (`primvar.hh:503`) but Attribute doesn't fully utilize it.
- [ ] **Profile texture memory separately** — textures dominate total memory. Add per-texture size reporting to `estimate_memory_usage()` output.
- [ ] **Benchmark deferred vs immediate tangent on WASM** — measure initial load time and peak memory difference.
- [ ] **Reduce WASM default memory limit** — 8 GB for 64-bit WASM may be too permissive for web deployment. Profile real workloads to set tighter defaults.
- [x] **Normal quantization** — DONE. Packs normals to INT_2_10_10_10_REV (4 bytes/vertex, 67% savings). Default on WASM, opt-in on native via `MeshConverterConfig::normal_storage`.

### Low Priority

- [ ] **Three.js packed tangent `BufferAttribute`** — Three.js doesn't natively support `GL_INT_2_10_10_10_REV`. Investigate `InterleavedBufferAttribute` or custom WebGL calls to avoid unpack-to-float in binding.
- [ ] **Memory-based CrateWriter** — `usdc-writer.cc` TODO for in-memory USDC writing (currently uses temp file).
- [ ] **Multi-threaded tangent computation** — Hybrid and FastMikkTSpace are single-threaded. Parallelize per-face derivative phase.
- [ ] **Composition memory tracking** — `composition.hh` has `max_memory_limit_mb` but no usage reporting for reference/payload resolution overhead.
- [ ] **CrateReader decompression buffer shrink** — decompression buffers grow but never shrink. Add `shrink_to_fit()` after large decompression sequences.

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
| `examples/tusdcat/main.cc` | mmap loading, `--memstat` reporting |
| `examples/tydra_to_renderscene/to-renderscene-main.cc` | Conversion example (needs `--memstat`) |
| `tests/feat/tangent/bench_tangent.cc` | Tangent memory/quality benchmark |
| `doc/TYPED_ARRAY_REVIEW_2025.md` | TypedArray memory safety issues |
| `doc/tydra-tangent.md` | Tangent computation and quantization docs |
