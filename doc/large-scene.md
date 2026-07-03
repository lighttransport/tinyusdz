# Loading large USD scenes in TinyUSDZ

Production USD scenes are 10–20 GB on disk, spread across thousands of payloaded
files. This document analyzes three publicly-available scenes — **Moana Island**
(Disney), **ALab** (Animal Logic), and **Caldera** (Activision) — and describes
how TinyUSDZ loads them within a bounded RAM budget (target: **fit a parse into
16 GB**, geometry deferred), plus the remaining implementation work.

See also [instancing.md](instancing.md) for the Island instancing analysis.

## Why it is hard

| Challenge | Consequence |
|---|---|
| Tens of thousands of files | per-file open/parse cost; must parse each once |
| Geometry behind `payload` arcs | must be deferred, not loaded eagerly |
| Deep reference/variant nesting | composition must be lazy and bounded |
| Cross-directory `..`-relative asset paths | resolver must anchor each arc to its layer's directory |
| Multi-GB single files (splats, point caches) | must stream / mmap, not buffer wholesale |

The lever that makes these scenes fit is **payload deferral** (load the
composition structure, leave geometry on disk) combined with **parse-once layer
sharing** and **LOD/proxy variant selection** — *not* mmap (see §Roadmap).

---

## 1. Scene structures

### 1.1 Moana Island (`/mnt/disk1/data/island`, ~17 GB)

Root `usd/island.usda` (~22 KB) — a single `Xform "island"` (kind `assembly`)
with 20 element children via `prepend references`. Each element uses a
three-file pattern (`element.usda` → `instance.usda` → `geometry.usda`) with
`prepend payload` to binary `model.usd` + XGen archives.

| Metric | Value |
|---|---|
| Composition style | scenegraph instancing + payloads |
| `instanceable = true` prototypes | 60 |
| component instances | 101 |
| `references` / `payload` | 3,924 / 214 |
| top-level `PointInstancer` | 0 (XGen instancing baked in binary `.usd`) |
| largest file | `xgGroundCover.usd` 652 MB |

### 1.2 ALab v2.3.0 (`/mnt/disk1/data/alab/v2.3.0.zip`, ~4 GB USD in-zip)

Root `ALab/entry.usda` (309 B) composes via **subLayers**. Entities use
subLayers for department contributions (`modelling`/`surfacing`/`preview`) +
`references` into `fragment/` + **variant-selected payloads** for geometry LOD
(`geo` = base / render_high / display_high / collision / …), `instanceable`
assembly references, and `inherits`/`specializes` against `__class__`.

| Metric | Value |
|---|---|
| Composition style | subLayers + references + variants + payloads |
| USD files | ~4,298 (mostly tiny `.usda` metadata; geometry in `.usdc` payloads) |
| unique entities | 380 |
| `PointInstancer` | 0 (reference-based instancing) |
| largest files | `alab_sdr_splat.usdc` 537 MB, `alab_hdr_splat.usdc` 382 MB |

ALab is the canonical "payload everything" scene: a typical working set loads
only the chosen geometry LOD per asset (~80 % of geometry stays deferred).

### 1.3 Caldera (`/mnt/disk1/data/caldera`, 9.7 GB)

Root `caldera.usda` (text) → 4 subLayers
(`map_source/mp_wz_island.usd` + `cameras`/`breadcrumbs`/`endpoints`). The root
also authors `over` prims selecting the `districtLod = "proxy"` variant for each
map district. **63,684 files, all binary crate except the text root.**

| Metric | Value |
|---|---|
| Composition style | subLayers + variant-driven LOD + payload-deferred geometry |
| `payload` | 17,299 |
| `variantSet` (LOD `districtLod = proxy\|full`) | 8,066 |
| `references` | 4,252 |
| `PointInstancer` | 14 (clutter) |
| geometry | `assets/xmodel/generated_splined/` 4.9 GB (25.6k crate files) behind payloads |
| proxies | `assets/xmodel/generated_proxies/` 177 MB (loaded for `districtLod=proxy`) |
| player data | `layers/breadcrumbs.usd` 43 MB + `endpoints.usd` 27 MB (`Points`) |

References are `..`-relative across directories (e.g.
`@../../xmodel/.../model.geo.usd@`), which the loader must allow.

### Memory-fit summary

| Scene | LoadNone / proxy working set | Full |
|---|---|---|
| Island | structure only, << 1 GB | ~17 GB |
| ALab | metadata ~0.9 GB | textures 7.3 GB + geometry |
| Caldera | structure ~1.4 GB (measured) | ~10 GB |

All three fit a structural parse well under 16 GB when payloads are deferred.

---

## 2. The TinyUSDZ loading strategy

### 2.1 `LargeSceneLoader` (`src/large-scene-loader.{hh,cc}`)

A one-call, memory-bounded loader that wires the existing primitives together
and **keeps the composition graph alive** so geometry can be streamed in later:

```cpp
tinyusdz::LargeSceneLoadOptions opts;
opts.payload_mode = PayloadMode::LoadNone;       // defer all geometry payloads
opts.allow_parent_relative_paths = true;         // Caldera '..' paths
opts.dedup_layers = true;                        // parse each file once

tinyusdz::LargeSceneLoader loader;
loader.Load("caldera.usda", opts, &warn, &err);

loader.stage();                                  // composed structure
loader.deferred_payload_paths();                 // geometry not yet loaded
loader.load_payload(path, &warn, &err);          // stream one in on demand
loader.rebuild_stage(&warn, &err);
```

Pipeline: `LoadLayerFromFile` (root) → `CompositeSublayers` (L phase) →
`CompositionGraph::Compose` with a payload policy → `BuildStage`. The loader
owns the resolver, `pcp::LayerRegistry`, flattened root, and `CompositionGraph`
together so `load_payload`/`unload_payload`/`deferred_payload_paths` work after
the initial load. Loading or unloading a payload now refreshes the payload
node's own arcs plus the composed descendant PrimIndices under that prim before
`rebuild_stage`, so children that exist only inside streamed geometry appear on
load and disappear again on unload; nested relative arcs inside the streamed
payload resolve against the payload file's directory; deferred-payload queries
report only payloads still in the deferred state.

### 2.2 Payload modes

| Mode | Behavior |
|---|---|
| `LoadNone` | defer every payload (lightest; geometry streamed later) — default |
| `LoadAll` | eager (may exceed RAM on huge scenes) |
| `Budget(mb)` | load payloads until a byte budget of asset file sizes is reached, then defer the rest |

The policy is a `std::function<bool(const Path&, const Payload&)>` passed to
`CompositionGraphOptions::payload_policy`; deferred payloads are recorded in the
graph and retrievable via `GetDeferredPayloadPaths()`.

### 2.3 Parse-once layer sharing

With `dedup_layers`, the loader installs `pcp::LayerRegistry`
(`src/pcp/layer-registry.hh`) through the new `CompositionGraphOptions`
layer-loading seam (`load_layer_fn` / `load_layer_userdata`). Each referenced
file is resolved + parsed exactly once and shared across every prim that
references it — essential for the tens of thousands of repeated prefab
references in Caldera.

### 2.4 `..`-relative paths

`CompositionGraphOptions::allow_parent_relative_paths` (added in this work)
threads through the reference/payload/`LoadPayload` asset-path validation so
`..` segments are permitted and delegated to the resolver. Without it the DAG
engine rejected such arcs (Caldera composes to almost nothing).

### 2.5 Example / benchmark

`examples/large-scene-load/` loads a scene and reports process RSS (from
`/proc/self/status` VmRSS — the trustworthy peak gauge), deferred-payload count,
unique files parsed, and estimated Stage memory; with `--load-some=N` it streams
in deferred payloads to demonstrate on-demand geometry.

```
large-scene-load <scene.usd[a]> [--mode=none|all|budget] [--budget-mb=N]
                                [--no-dedup] [--load-some=N] [--unload]
```

### 2.6 Measured result (Caldera)

`large-scene-load caldera.usda --mode=none` composes the full proxy-mode
structure — **32,811 prims, 122 files parsed, 373 deferred payloads** — in
**~1.7 GiB / ~3 s**, well within the 16 GB budget. `--load-some=N` streams the
deferred proxy geometry on demand.

**ALab** (`ALab/entry.usda`, the `mk020_0281` shot) composes **3,293 prims /
1,271 deferred geometry payloads in ~120 MiB**; the set asset alone
(`entity/alab_set01/alab_set01.usda`) composes **3,251 prims / 1,264 payloads**.
ALab's asset-centric structure needed three further composition fixes (§3.4):
(a) composing a *referenced/payload* layer's own subLayers — ALab entity files
just sublayer their department layers, so without this the referenced content is
empty; (b) accumulating `references`/`payload` **list-ops** across a sublayer
stack — a shot's department layers each `prepend references` to `/root`, and only
the strongest survived before; (c) marking a referenced prim as having specs when
it has children (so a reference target that is just a parent of geometry expands).

The optional **`baked_procedurals`** package (value-clip + multi-payload baked
character hair/cloth; `main.usda`) composes its full structure — multi-payload
prims and the value-clip `GEO` behind the `alfro=render` variant — in a few MiB,
with the multi-GB binaries and the value-clip layer deferred. This needed the
variant-content fix (§3.5).

### 2.7 RenderScene optimization for realtime viewers

Payload deferral solves structural loading, but realtime web/native viewers have
a second pressure point after composition: authored scenes can expand to thousands
of duplicate materials, many equivalent texture records, thousands of draw-call
mesh nodes, and tens of thousands of transform-only prim nodes. TinyUSDZ now has
opt-in `RenderSceneConverterConfig` switches for this viewer-oriented path:

| Option | Default | Effect |
|---|---:|---|
| `dedup_materials_by_texture_identity` | `false` | Deduplicate equivalent render materials by full shader parameters and canonical texture identity. Also enables a source-graph signature cache so duplicate source materials can reuse an existing render material ID before full conversion. |
| `merge_meshes` | `false` | Merge compatible meshes sharing a material to reduce draw calls. |
| `merge_meshes_bake_transform` | `true` | Bake mesh node world transforms into vertex data while merging so meshes under different transforms can still aggregate. |
| `flatten_optimized_render_tree` | `false` | Replace the authored prim hierarchy with a compact render-only root containing renderable meshes/lights/cameras in world space. This intentionally breaks prim-tree fidelity. |

All destructive optimizations are disabled by default. Existing conversion paths
therefore preserve the authored prim tree and one-render-material-per-converted
source material unless an app explicitly opts into the viewer path.

The material optimization has two stages:

1. A conservative source-graph signature cache compares authored Material /
   Shader / NodeGraph payloads with local material paths normalized. A cache hit
   reuses the canonical render material ID and skips full material conversion.
2. A post-conversion material/texture compaction pass remaps mesh material IDs,
   material subset IDs, instance material IDs, and shader texture IDs to
   canonical render objects. Texture identity uses resolved image ID, wrapping,
   UDIM linkage/remap, scale/bias, output channel, UV primvar, and transform.

Mesh merging is applied after material/texture compaction. It only merges meshes
that are safe for render aggregation: no skeletal skinning, no blend-shape
targets, no per-face material subset map, and compatible vertex attribute
layouts. When transform baking is enabled, packed direction attributes that
cannot be transformed as float3 are dropped from the merged mesh so consumers can
recompute them rather than reading corrupted normals/tangents.

Flattened render tree mode is separate from mesh merging. It keeps renderable
nodes only and assigns each kept node its previous world matrix as its new local
matrix under a synthetic root. This substantially reduces JS/renderer object
construction overhead in viewer-only workflows, but it should not be used by
tools that need authored prim paths, hierarchy editing, or exact selection
round-tripping.

The web MaterialX demo exposes the same switches as URL/query/UI settings:

```
nativeMaterialDedup=true
nativeMeshMerge=true
nativeMeshMergeBakeTransform=true
nativeFlattenRenderTree=true
meshAggregation=off|material
```

On a representative flattened large scene with resized PNG textures, the fully
native viewer path reduced render mesh nodes from a few thousand to a few
hundred, compacted the JS node tree from tens of thousands of nodes to hundreds,
kept the final material count stable, and reduced Three.js construction from
roughly a second to a few hundred milliseconds. Source-material signature caching
also reduced native material conversion from roughly second-scale to
double-digit milliseconds in that workload. Treat these as shape-of-improvement
numbers rather than a portable benchmark; exact results depend on material
duplication, mesh compatibility, and hierarchy depth.

---

## 3. Roadmap (remaining implementation)

The loader bounds memory and streams payloads. Cross-directory cwp anchoring
(§3.1) and referenced-prim descendant reconstruction (§3.2) are now **fixed** —
Caldera composes its full proxy-mode structure (32,811 prims, geometry deferred)
in ~1.7 GiB. The items below are remaining enhancements.

### 3.1 Cross-directory cwp anchoring — FIXED

**Was:** a loaded layer's PrimSpecs got their working-path stamped from
`GetBaseDir(authored_asset_path)` — a *relative* path — instead of the *resolved
absolute* base directory, so nested references lost their parent-directory
context (e.g. `prefabs/...` inside `map_source/mp_wz_island_geo.usd` anchored to
the scene root instead of `map_source/`). A second case: when sublayer
flattening merged a stronger `over` (carrying the root layer's cwp) onto a weaker
`def` that authored the reference (e.g. Caldera's root `over mp_wz_island_paths`
over `mp_wz_island.usd`'s `def`), the merged prim kept the root cwp and
mis-anchored the arc.

**Fix (implemented):**
- Pass the **resolved** path (not the authored relative path) to
  `LoadLayerFromMemory` at every composition load site, so a loaded layer's
  prims are stamped with their resolved directory: `LoadAsset`
  (`src/composition.cc`), `DefaultLoadAndOwnLayer` (`src/composition-graph.cc`),
  and `pcp::LayerRegistry::GetOrLoad` (`src/pcp/layer-registry.cc`).
- In sublayer merge (`CombinePrimSpecRec`, `src/composition.cc`), when a stronger
  `over` absorbs a weaker `def`/`class`, adopt the **defining** layer's
  asset-resolution anchor (cwp + search paths) so reference/payload arcs authored
  in the defining layer resolve against the correct directory.

Regression test: `tests/feat/large-scene/` (`feat-large-scene`) uses a
filename-collision fixture (a malformed decoy `leaf.usda` in the root dir vs. the
real one in `sub/`) so the parse-once registry's parse count only goes non-zero
when the reference anchors to the correct directory — it fails if either fix is
reverted. Full unit suite (853 cases) unaffected.

### 3.2 Referenced-prim descendant reconstruction — FIXED

**Was:** `CompositionGraph` built a `PrimIndex` per prim by walking only the
**root-layer** namespace (`BuildPrimIndex` DFS over the root-layer PrimSpec's
children) and `ScanArcsAndEnqueueTasks` scanned only a prim's *own* arcs. Child
prims introduced *by a reference* (e.g. `paths.usd`'s `mp_wz_island_geo`, which
itself references `geo.usd`) were never added to the namespace, so their arcs
were never followed and they never reached the Stage. Caldera's chain
(`mp_wz_island` → `paths` → `geo` → prefabs → proxy payloads) stopped a few
levels in (≈6 files parsed, 0 deferred payloads).

**Fix (implemented, `src/composition-graph.cc`):** `BuildPrimIndex` now expands
the **composed** namespace. After building a prim's index it gathers the union
of child names across *all* of the index's nodes (`GatherComposedChildNames`),
and builds each child's index by **descending every node of the parent index one
namespace level** (`PrimIndexBuilder::BuildChildFrom` / `SeedDescendedNodes`):
each descended node keeps its parent's arc type / layer stack / strength so the
child inherits the parent's reference & payload arcs, and the descended prims'
own arcs are then scanned and followed (which pulls nested references such as
`geo.usd`). This reconstructs referenced descendants *and* follows their nested
arcs, recursively.

**Result:** Caldera (proxy, LoadNone) composes **32,811 prims / 122 files / 373
deferred payloads** in **~1.7 GiB / ~3 s** — the full structural composition,
geometry deferred. Regression test `feat-large-scene` asserts a referenced
prim's grandchild (`/P/M`) is reconstructed. Full unit suite (853) unaffected.

`pcp::Cache::BuildStage` now mirrors this namespace expansion for stage
reconstruction: it starts from cached root PrimIndices, descends each composed
parent index with `PrimIndexBuilder::BuildChildFrom`, and composes temporary
child indices for referenced descendants. Lazy `ComputePrimIndex()` remains
local to explicitly requested prim paths, but full-stage reconstruction now
matches the eager engine for referenced grandchildren. Regression:
`pcp_buildstage_reference_grandchildren_test`. Deferred PCP payload streaming now
also reprocesses the newly loaded payload node, so references/payloads authored
inside a streamed payload layer are visible immediately after `LoadPayload`.
Regression: `pcp_external_payload_load_reprocesses_nested_arcs_test`.

### 3.3 mmap-through-composition / fd bounds (PARTIAL)

mmap zero-copy (`USDLoadOptions::mmap_zero_copy`) defers large uncompressed
float arrays to disk, but only via `LoadUSDCFromFile(... Stage*)`. It does **not**
flow through composition: `Layer` has no mmap storage, and
`BuildStage`/`ComposePrimSpecFromIndex` (`composition-graph.cc:1389`) deep-copy
PrimSpec arrays into the Stage. For the 16 GB target this does not matter
(deferred geometry never enters the Stage), but to keep *loaded* geometry on disk
the project would need: an mmap table on `Layer`, `LoadUSDCLayerFromMemory`
honoring `mmap_zero_copy` against a file-backed mmap, non-copying
(move/alias) array handling in `ComposePrimSpecFromIndex`, and
`Stage::adopt_mmap_*` after `BuildStage`. A pragmatic interim: stream a single
on-demand payload via `LoadUSDCFromFile(... mmap_zero_copy=true)` into a side
Stage.

The descriptor-bound part is now implemented: `AssetResolutionResolver` tracks a
per-resolver concurrent open file-descriptor / asset-handle budget
(`max_file_descriptors`, default **1024**). `LargeSceneLoadOptions` forwards this
limit into sublayer/reference/payload loading. `open_asset()` reports a hard
error when the budget is reached instead of falling through to an OS-level
`EMFILE` failure, and the composition graph treats that specific resolver error
as fatal even when ordinary missing references/payloads are skippable. The
current filesystem and mmap paths close descriptors before returning from each
open, so this is a guard on concurrent opens, not an LRU for long-lived mappings.

### 3.4 Asset-centric composition (ALab) — FIXED

ALab loads assets via subLayer-aggregated entity files and layers shots from
many department sublayers. Three fixes make it compose:
- **Compose a referenced/payload layer's own subLayers**
  (`src/pcp/layer-registry.cc`, `LayerRegistry::GetOrLoad`): when a
  reference/payload target aggregates its prims through its own `subLayers` (the
  ALab entity pattern), compose them after load — otherwise the referenced
  content is empty. Mirrors the LIVRPS `LoadAsset`.
- **`references`/`payload` list-op accumulation across sublayers**
  (`src/composition.cc`, `CombinePrimSpecRec`): these are list-ops, so opinions
  from each layer in a sublayer stack accumulate (USD prepend/append) rather than
  the stronger whole-value winning. A shot whose department layers each
  `prepend references` to `/root` now keeps them all. The newly-introduced arcs
  are anchored to the layer that authored them (generalizing the over+def
  cwp rule to def+def).
- **Child-bearing reference targets expand** (`src/composition-graph.cc`,
  `GatherComposedChildNames`): a reference target that authors no props/metas of
  its own but has children (a parent of geometry) is no longer skipped during
  namespace expansion.

Regression: `feat-large-scene` scenario 2 (`fixture/multi/`) covers all three —
two sublayers each `prepend references` to `/P`, one asset aggregating geometry
through its own subLayers; both `/P/MA` and `/P/MB` must reconstruct.

### 3.5 Variant-content composition — FIXED

CompositionGraph recorded variant *selections* but `ResolveAndApplyVariants` was
a no-op, so the selected variant's *content* (child prims + their nested
references/payloads/clips) was never composed — variant-gated geometry (e.g.
baked_procedurals' `GEO`, or ALab characters behind their `geo`/`alfro`
variants) was dropped. Two fixes:
- **Merge variantSet content across composition** (`CombinePrimSpecRec`,
  `src/composition.cc`): the `variantSet "x" = { … }` blocks live in
  `variantSets()` and were not merged when a prim received opinions from
  multiple layers, so a prim selecting a variant whose set is defined in a
  weaker sublayer lost the content. Now the per-variant content is merged.
- **Compose variant content in the DAG** (`PrimIndexBuilder::ApplyVariantContent`,
  `src/composition-graph.cc`): after a prim's arcs are evaluated, compose the
  variant selection (strongest per set across nodes) and, for every node that
  authors a matching variant set, resolve the selected variant
  (`VariantSelectPrimSpec`, injecting the composed selection) and repoint the
  node at the resolved PrimSpec — **materialized in a synthetic layer** owned by
  the composition context (since `find_primspec_at` has no variant-path
  support). The variant's child prims then enter the namespace and the normal
  expansion follows their arcs. Because the selection and the variant set may be
  authored on different nodes, this handles **both** variant sets defined in the
  root layer stack **and** variant sets introduced via a reference (asset
  defines the set, a stronger layer selects it — the ALab character pattern).
- **Re-scan selected variant self-arcs** (`PrimIndexBuilder::FinishBuild` /
  `ApplyVariantContent`, `src/composition-graph.cc`): after a node is repointed
  at its resolved variant PrimSpec, the DAG re-scans that PrimSpec and drains the
  task queue again. This covers variant options that author composition arcs on
  the selected prim itself (e.g. a `prepend references` or `prepend payload` in
  the variant option metadata), not only arcs on child prims.

Regression: `feat-large-scene` scenarios 3 (`fixture/variant/`, set in a weaker
sublayer) and 4 (`fixture/variant-ref/`, set in a referenced asset) — both
require `/P/GEO` and `/P/GEO/M` to reconstruct. Scenario 5
(`fixture/variant-self-arc/`) requires a selected variant's self-reference to
compose `/P/M`, a selected variant's self-payload to compose `/Q/PM` in
`LoadAll`, that same payload to be recorded as deferred on `/Q` in `LoadNone`,
and `load_payload("/Q")` / `unload_payload("/Q")` to add/remove `/Q/PM` after
`rebuild_stage`. Scenario 6 (`fixture/deferred-nested/`) streams a deferred
payload from a subdirectory; that payload references `./leaf.usda`, and
`load_payload("/P")` must compose `/P/Nested/M`, proving lazy payload parsing
uses the resolved payload path for cwd stamping and re-scans arcs authored on
the newly loaded payload node. On the ALab shot this composes the character
geometry (3,293 → 4,560 prims, 1,271 → 2,540 deferred payloads); Caldera is
unchanged (32,811). Value clips themselves (`clips` metadata) are resolved at
Tydra / attribute-evaluation time (`enable_value_clips`), not during
composition.

### 3.6 Smaller items

- **Budget-by-extent**: the `Budget` policy currently sizes payloads by file
  bytes; authored `extentsHint` would let it bound by world-space coverage, but
  the payload-policy callback would need the owning `PrimSpec`.
- **Layer-owned mmap**: true mmap-through-composition still needs Layer/Stage
  ownership plumbing for mapped crate buffers. If future code keeps file handles
  open for long-lived mappings, add an LRU on top of the existing descriptor
  budget.
- **Threaded streaming**: `load_payload`/`unload_payload` mutate the graph in
  place and are single-threaded post-load.

---

## 4. UnrealEngine USD Stage exports

A fourth scene class beyond the three above: UE's "Export Level → USD" writes a
binary-crate root with thousands of per-instance `prepend references` into
per-asset `SM_*.usd` files (geometry inline under a `variantSet "LOD"`, one
selected LOD) plus `MI_*.usd` UsdPreviewSurface materials and 2k-textures.
Multi-GB scenes (1–3 GB on disk, 250–750 textures) compose to 4k–13k meshes.
Two UE-specific path pathologies, and the fixes (commit `3f314f0be`):

- **Escaping parent-relative arcs**: references are authored against the export
  machine's directory layout (e.g. `@../../../../../USD_Exports/<Scene>/Assets/
  mesh.usd@`), which resolves outside the scene root and silently composed
  nothing. The resolver now applies a **suffix fallback** on a literal-resolution
  miss: strip the un-anchorable prefix (leading `../` runs, Windows drive,
  `/`), then retry progressively shorter path suffixes — longest first, down to
  the basename — against the search paths (`Assets/mesh.usd` matches under the
  scene root). Default on; `tusdcat --no-asset-path-fallback` opts out. Each
  rebase is surfaced as a deduped warning, and total misses warn once per path.
- **Drive-prefixed paths** (`@F:/USD_Exports/...@`): with
  `allow_parent_relative_paths`, the security validator now demotes the drive
  prefix to a relative path instead of rejecting, and the same suffix fallback
  rebases it.

The fallback lives in `AssetResolutionResolver::resolve()` so every consumer —
tusdcat/tusdzconvert flatten, `CompositionGraph`, and the wasm in-memory
resolver (browser folder upload) — inherits it with no duplicated logic.

## 5. USDZ conversion at scale (texture pipeline + wasm heap)

Findings from converting multi-GB scene folders to USDZ (native `tusdzconvert`,
the Node/wasm CLI, and headless-Chrome in-tab):

### 5.1 Texture re-encode parallelization (commit `627d0472b`)

PNG re-encode of hundreds of 2k textures was the wall-clock bottleneck
(sequential single-thread: 15–44 min). Now:

- **Native**: texture packing runs in three phases — sequential dedupe/UDIM/
  asset reads → `ProcessTexture` on a `std::thread` pool → sequential archive
  naming/stats — byte-identical output to the sequential order.
  `UsdzConvertOptions::num_threads` (0 = all cores), `tusdzconvert -numThreads`.
  Pair with `-DTINYUSDZ_WITH_FPNGE=ON` to replace the scalar-fpng encoder
  (`FPNG_NO_SSE=1`). A 731-texture 2.5 GB scene: **33 s** vs 43 m 53 s via the
  sequential wasm CLI.
- **Node CLI**: `--texture-codec js` runs PNG decode / gamma-aware box resize /
  encode on a `worker_threads` pool (pngjs + node's native zlib); non-PNG falls
  back to the wasm path. 14–25× on PNG-heavy scenes, ~25–35 % larger output
  (zlib-6 vs fpng).
- **Browser**: the same `textureProcessor` hook (now driven with bounded
  concurrency) accepts a `createImageBitmap` + OffscreenCanvas processor
  (`web/js/src/texture-processor-browser.mjs`): 5–9× over in-tab wasm fpng.
  Benchmarked hw (ANGLE/Vulkan) vs SwiftShader with
  `web/js/tests/bench-usdzconvert-browser.mjs`: **a wash** — Chrome's image
  codecs are CPU-side either way; only the sub-second canvas raster touches the
  GPU. The pipeline is codec-bound, not raster-bound.

### 5.2 wasm large-heap fixes (commits `3f314f0be`, `627d0472b`)

- wasm32's 2 GB ceiling cannot hold a composed multi-GB scene folder; wasm64 is
  required (`TINYUSDZ_WASM64=1`; in-tab needs Chrome ≥133 memory64).
- wasm64 `MAXIMUM_MEMORY` raised 8 GB → **16 GB** (address-space reservation
  only, with `ALLOW_MEMORY_GROWTH`): the in-heap folder flatten of a 2.5 GB
  scene requests ~10 GB.
- `-sMALLOC=emmalloc` → **dlmalloc** for wasm64: emmalloc asserts
  (`debug_region_is_consistent` in `emmalloc_free`) once the heap grows past
  ~8 GB; dlmalloc completes the identical run.
- Node's `fs.writeFileSync` caps a single write at 2³¹−1 bytes; the CLI writes
  >2 GiB USDZ outputs in 1 GiB `fs.writeSync` chunks.

With these, a 2.5 GB UE-export folder converts to a 2.67 GB passthrough USDZ
in-process on wasm64 in ~64 s (and validates by re-loading).

---

## 6. Full-flatten memory (next/pcp engine)

§2's `LargeSceneLoader` bounds memory by **deferring payloads**. The separate
`next/pcp` engine (`src/next`, exercised by `next_usdcat -f`) does the opposite:
it produces a **fully composed, self-contained flattened layer** — like
`usdcat --flatten` — holding every composed PrimSpec, value array, and time
sample in RAM with no deferral. So peak RSS scales with the *flattened-content*
size, not the on-disk scene size.

Measured under a hard 32 GB cgroup cap
(`systemd-run --user --scope -p MemoryMax=32G -p MemorySwapMax=0 /usr/bin/time -v ...`):

| Scene (full flatten) | next RSS | next time | usdcat RSS | usdcat time | flatten lines |
|---|---|---|---|---|---|
| Kitchen_set (Pixar) | 0.13 GB | <1 s | — | — | ~66 k |
| ALab (Animal Logic) | **0.06 GB** | 0.8 s | 0.13 GB | 1.4 s | ~36 k |
| Caldera (Activision) | **2.3 GB** | 45 s | 3.1 GB | 76 s | ~3.45 M |
| Moana Island (Disney) | **8.3 GB** | 1 m 48 s | 8.5 GB | ~5 m | ~6.1 M |

next now flattens **at or below usdcat's RSS on every scene, and faster** — a
5.2× cut on Caldera (was 11.8 GB) and 3.2× on Island (was 26.6 GB), with
**byte-identical output** (verified by `cmp` against the pre-optimization flatten).

The peak used to be dominated by three avoidable residencies on the write path,
now all closed (the lazy-ValueRef read path keeps crate arrays as byte-range
references into the memory-mapped source until they are actually needed):

- **Whole output text held in RAM.** `next_usdcat -f` built the entire multi-GB
  USDA string before a single write. It now **streams** per-prim to stdout via
  `WriteUSDA(std::ostream&, ...)` — the serialized text never coexists with the
  composed stage.
- **Every lazy geometry array materialized in place during write.** Printing a
  value went through the const array accessors, which decode the crate-backed
  array into a resident `std::vector` *in place* (and delete the lazy ref), so by
  end-of-write every array was simultaneously resident. The writer now decodes a
  lazy array into a **throwaway temporary** (`Value::materialized_copy()`),
  bounding resident decoded-array memory to ~one array.
- **Time samples eagerly decoded at parse time.** `TimeSampleStorage::add_dedup`
  hashed each sample value, and `Value::hash()` forces a materialize — so every
  array-valued time sample was decoded into the heap at parse and stayed resident
  for the stage lifetime (Caldera: ~3.4 M of ~3.45 M flatten lines are
  time-sample data — the dominant cost). Lazy samples now **skip content dedup**
  and stay byte-range references until the writer decodes them one at a time.

All three preserve (and slightly improve) speed: less allocation, no redundant
decode at parse/clone, no giant-string copy. The §2 bounded loader is still the
path to use when geometry must stay on disk entirely; the difference here is that
a *full* flatten no longer carries gratuitous peak overhead.

### 6.1 Native `next_usdcat` vs current `tusdcat` (2026-06-29)

The table below is a TinyUSDZ-vs-TinyUSDZ snapshot of the native full-compose
path on the public large scenes. `next_usdcat` uses the `src/next` PCP engine and
streams the flattened USDA directly to a `FILE*`; `tusdcat` is the current
library pipeline. Both write USDA to `/dev/null` so the output text is generated
but not stored on disk. The `next_usdcat` numbers use a Release next build with
chunked value streaming in the ASCII writer.

Commands:

```sh
/usr/bin/time -v env TINYUSDZ_NEXT_TIMING=1 \
  build-next-release/next_usdcat -f -o /dev/null <root.usda>

/usr/bin/time -v \
  build_ninja/tusdcat -f --memstat --output-format=usda -o /dev/null <root.usda>
```

For trusted scenes with individual referenced USD layers larger than the current
512 MiB composition safety cap, current `tusdcat` has an explicit opt-in cap
relaxation:

```sh
/usr/bin/time -v \
  build_ninja/tusdcat -f --relax-asset-cap --memstat \
  --output-format=usda -o /dev/null <root.usda>
```

`--relax-asset-cap` raises the per-composition-layer cap to 8 GiB. Use
`--max-composition-asset-mb=N` when a tighter scene-specific cap is preferred.
The default remains the 512 MiB security-policy cap for untrusted inputs.

| Scene | `next_usdcat` load+compose | `next_usdcat` total | `next_usdcat` max RSS | current `tusdcat` result |
|---|---:|---:|---:|---|
| Caldera `caldera.usda` | 3.53 s | 9.71 s | 2.93 GiB | 1:46.7 total, 10.37 GiB max RSS |
| Moana Island `island.usda` | 15.38 s | 43.42 s | 9.67 GiB | failed after 11.6 s: `xgGroundCover.usd` exceeds the 512 MiB per-asset cap |
| ALab `ALab/entry.usda` | 0.33 s | 0.36 s | 62 MiB | not comparable: resolver missed a referenced ALab asset and built only a 43 KiB stage |

With `--relax-asset-cap`, current `tusdcat` passes the Moana Island cap failure:
the run completed composition in 8 iterations, wrote USDA to `/dev/null`, and
reported Stage in-use memory of 7.23 GiB. The measured full current pipeline was
9:09.98 elapsed with 30,416,912 KiB max RSS on a RelWithDebInfo build. The run is
still much slower and higher-memory than `next_usdcat`, but it no longer fails at
`xgGroundCover.usd` solely because that layer is 683,530,559 bytes.

The table above is the pre-optimization snapshot; the ASCII writer was then
optimized substantially (§6.2). After that work `next_usdcat` streams the
flattened USDA for Caldera at ~850–925 MB/s and for Island at ~1000 MB/s (auto
threads), with the Island write phase dropping from 26.17 s to ~11.7 s and peak
RSS from 9.67 GiB to ~8.0 GiB. The Island run completes with non-fatal warnings
for several XGen USDC layers whose arrays exceed the current `max_array_elements`
guard, so it is a large-scene stress result rather than a full-fidelity Island
flatten.

For a pure USDC reader comparison, the pre-flattened Caldera crate isolates parse
memory from composition:

```sh
/usr/bin/time -v env TINYUSDZ_NEXT_TIMING=1 \
  build-next-release/next_usdcat -l <caldera-build>/caldera.flattened.usdc

/usr/bin/time -v \
  build_ninja/tusdcat -l --memstat <caldera-build>/caldera.flattened.usdc
```

| Input | Parser | Load/elapsed | Max RSS | Notes |
|---|---|---:|---:|---|
| `caldera.flattened.usdc` | `next_usdcat -l` | 2.57 s load / 3.26 s elapsed | 1.77 GiB | 53,972 prims; warns for unsupported string arrays |
| `caldera.flattened.usdc` | `tusdcat -l --memstat` | 7.38 s elapsed | 3.34 GiB | Stage in-use memory 2.53 GiB; USDC parser peak 189.8 MiB |

The current `next` reader is therefore faster and lower-memory on this crate
parse (~2.3x faster elapsed, ~47 % lower max RSS). On the full Caldera
compose+stream path, `next` is both faster (~11x wall-clock) and lower-memory
(~3.5x RSS).

### 6.2 ASCII (USDA) writer optimizations

On a fully-composed large scene the **write phase dominates** total wall-clock
(Caldera ~57 %, Island ~63 % before this work). The `src/next` USDA writer
(`src/next/writer/{value-printer,usda-writer,dtoa}.{hh,cc}`) was optimized in two
passes. Every change is **byte-identical** to the serial writer — verified by
`cmp`/sha256 against the prior binary across the 615-file `tests/usda` +
`tests/usdc` corpus and on Caldera + Island (10.18 GB hash match) + ALab, in both
serial and threaded (4/8/16) modes. Output to `/dev/null`, Release build, on a
32-thread / 16-core workstation:

| Scene (flatten write phase) | Before | After |
|---|---:|---:|
| Caldera (4.25 GB) | 728 MB/s | **~850–925 MB/s** |
| Island (10.18 GB) | 371 MB/s, 26.2 s | **~1000 MB/s, ~11.7 s** |
| Island peak RSS | 9.67 GiB | **~8.0 GiB** |

#### Pass 1 — per-element number formatting

Large arrays are the bulk of the bytes, so per-scalar formatting is the serial
hot loop. Two byte-identical changes:

- **No `num_` round-trip.** Each scalar used to format into a stack buffer, append
  to a reused member `std::string`, then be copied *again* into the chunk buffer.
  It now formats once into a stack buffer and appends directly. New
  `dtos_to(char*, float/double)` (`writer/dtoa`) and `IntTo`/`UIntTo(char*, …)`
  (`strfmt.hh`); `ChunkedStream` drops the `num_` member. Serial writer
  +~16 % (Caldera 218 → 253 MB/s, Island piped 162 → 176 MB/s).
- **Indent cache.** `WriteIndent` emits the indentation prefix in one write from a
  `thread_local` cached padding string instead of `depth` tiny writes.

#### Pass 2 — parallel writer rearchitecture

A profile (a debug switch that skips array-value formatting) showed **~95 % of
Island's write phase was array formatting running serially on the main thread**.
The previous parallel design split work by **subtree frontier**, which only
parallelized *leaf* subtrees — but Island's geometry lives in *interior* prims,
whose arrays were serialized on the main thread regardless of thread count (write
scaled only ~1.6× from 1→8 threads).

The writer was rebuilt around a **document-ordered task list**:

1. **Phase 1 (serial, cheap):** one pre-order walk of the whole stage emits cheap
   structural bytes inline as `Text` tasks and **offloads every array value**
   (≥ 256 elements) as a task referencing the borrowed array. No giant array is
   formatted here, so the walk is fast (~1.6 s on Island).
2. **Phase 2 (parallel):** a worker pool formats **byte-balanced segments** (each
   a contiguous task run) while the main thread writes the finished segment
   buffers **in document order**. Synchronization is lock-free (atomics + a
   bounded look-ahead window that back-pressures workers and bounds in-flight
   memory) — no per-task mutex/condvar, so it scales to ~250 k tasks.

The critical refinement is that **no single giant array may pin one worker** (a
single 44 M-element array was an 8 s serial segment). Giant arrays are split into
element-range chunks formatted concurrently:

- **Directly chunkable arrays** — non-lazy, or *uncompressed*-lazy that can be
  aliased zero-copy from the memory-mapped crate (new `CanBorrowLazyFlat`,
  `types/value-view`) — are chunked **in place** with no decode or copy.
- **Giant compressed-lazy** numeric arrays are **decoded once** into an owned
  buffer (held in a deque, freed as soon as their chunks are consumed, so peak
  memory stays bounded) and then chunked.
- **Half-backed types** (`half`, `quath`, …) widen to float via `as_float_array`,
  so Island's 21 M-element `quath` orientation arrays chunk through the float
  range printer too. This was the last serial bottleneck.

New `value-printer` API: `ArrayElementCount`, `IsChunkableType`,
`IsChunkableArray`, and `PrintArrayRangeToStream` (formatting an element range is
byte-identical to the full array printer — the foundation of chunk splitting). The
serial path (`TINYUSDZ_NEXT_NUM_THREADS=1`) is untouched and remains the
correctness oracle. The auto worker cap was raised 8 → 16 now that the balanced
design scales with cores (both scenes still improve at 16+).

Tests: `test_array_range_split_parity` (range reconstruction == full print across
types and cut points) and `test_parallel_writer_parity` (1-thread vs 8-thread
byte-identical) in `tests/next/test_writer.cc`.

The remaining serial cost is the ~6 s Phase-1 build walk on Island (not yet
overlapped with Phase 2) — the next available lever.

## 7. Verification

```
cd build && cmake --build . -j16
# cross-directory cwp anchoring regression test (§3.1):
cd build && ctest -R feat-large-scene --output-on-failure
# structural load within budget (absolute path works from any cwd now):
<build>/large-scene-load /mnt/disk1/data/caldera/caldera.usda --mode=none
# expect: ~32,811 total prims, 373 deferred payloads, RSS ~1.7 GiB.
# --load-some=N streams deferred proxy geometry on demand.
cd build && ctest --output-on-failure     # no regressions (2 pre-existing
                                           # MaterialX failures are unrelated)
# suffix-fallback unit tests (§4):
cd build && ./unit-test-tinyusdz ioutil_asset_path_suffix_candidates_test \
                                 comp_reference_suffix_fallback_test
# browser conversion bench (§5):
cd web/js && xvfb-run -a node tests/bench-usdzconvert-browser.mjs --hw \
  --scene <scene-dir> --root <root.usd> --case browser:png:1024
```

## 6. 2026-07 next-parser parallel load speeds

The `src/next` loaders (branch `next-refactor`) now parse the flattened
benchmark scenes at (8 threads, inputs on tmpfs, byte-identical to serial):

- Caldera USDA 4.25 GB: **2.0 s (~2.1 GB/s)**; its 285 MB USDC: **0.85 s**
- Island USDA 9.28 GB: **5.5 s (~1.7 GB/s)**; its 2.37 GB USDC: **2.6 s**
- Merged full-geometry ALab: compose+flatten-write 1.31 GB USDA in **5.1 s**

Escape hatches: `--no-async-arrays`, `--no-parallel-prims`,
`-DTINYUSDZ_NEXT_DISABLE_PARALLEL_BUILD_STAGE`. See doc/refator-next.md
(2026-07 section) for the mechanism list.

## 8. 2026-07 next-core (freestanding I/O split) stream benchmark

After the `next_core` freestanding split (`src/next/io` filesystem utility; USDC
mmap and every `*FromFile` routed through `tinyusdz::next::io` under
`TINYUSDZ_NEXT_ENABLE_FILEIO`), the full compose+stream-write path was
re-benchmarked to confirm **no speed/RSS regression and byte-identical output**.
Release build, `TINYUSDZ_NEXT_ENABLE_THREAD=ON`, 32-thread/16-core workstation,
inputs on `/mnt/disk1/data`, output streamed to `/dev/null`:

```sh
/usr/bin/time -v env TINYUSDZ_NEXT_TIMING=1 \
  build-next-release/next_usdcat -f --timing -o /dev/null <root.usda>
```

| Scene (full flatten) | load+compose | write | write throughput | total | max RSS | flatten bytes |
|---|---:|---:|---:|---:|---:|---:|
| Caldera `caldera.usda` | 2.49 s | 4.57 s | 887 MB/s | 7.06 s | **2.72 GiB** | 4.25 GB |
| Island `island.usda`   | 8.08 s | 5.87 s | 1507 MB/s | 13.95 s | **5.38 GiB** | 9.28 GB |
| ALab (merged) `entry.usda` | 2.11 s | 0.57 s | 2209 MB/s | 2.68 s | **0.94 GiB** | 1.31 GB |

All three are **at or below** the §6.1/§6.2/§7 stream-next numbers (Caldera write
still ~850–925 MB/s band; Island now 1507 MB/s / 5.38 GiB vs the earlier
~1000 MB/s / ~8.0 GiB; ALab 2.68 s vs 5.1 s) — the freestanding split did not
regress the streaming path, and the crate reader confirms it is mmap-backed
(`[next_crate_read] … mmap=1`). Island still emits 19 non-fatal `max_array_elements`
warnings for XGen USDC layers (large-scene stress result, not a full-fidelity
Island flatten).

**Byte-identical parity:** the streamed USDA for each scene was `sha256`-compared
between the current tree and a session-start baseline (`930b432c`, before the
TU-split / C-style-de-templating / freestanding-I/O work). All three match
exactly (Caldera `9d680d8c…`, Island `8fef4b2c…`, ALab `992471c3…`), and the
current output is deterministic run-to-run. So the entire freestanding /
zero-copy-mmap / C-style refactor series is behavior-preserving on the public
large scenes.

### 8.1 Profile-driven optimizations (2026-07)

`perf record --call-graph dwarf` on the two heaviest scenes:

**Caldera is write-bound** (write ≈ 65 % of total). The USDA write phase spent
~17 % in `__memmove` alone — a per-scalar stack-buffer→`std::string::_M_append`
copy in `ChunkedStream`. Formatting each number **directly into the chunk buffer
tail** (raw `char[]` + cursor; `IntTo`/`UIntTo`/`dtos_to` write into `buf_+len_`)
removes that copy. Byte-identical (writer parity tests + scene sha256 unchanged):

| Scene write phase | before | after |
|---|---:|---:|
| Caldera | 4.57 s (887 MB/s) | **3.94 s (~1020 MB/s)** |
| Island  | 5.87 s (1507 MB/s) | **5.13 s (~1725 MB/s)** |

Remaining write cost is now the float formatter itself (`zmij::write_usd_fast`
~27 % on Island, whose 21 M-element `quath` orientations widen to float) — near
the practical dtoa floor.

**Island is compose-bound** (`build_stage` ≈ 6.2 s; write 5.1 s). Time-sliced
perf of the compose phase shows it is dominated by **std::string-keyed map +
small-allocation churn**: ~16 % `malloc`/`free` (+ ~20 % kernel page-fault
handling from heap/mmap growth), `Cache::Impl::Specs` string lookups ~7.7 %,
`std::hash<string>` ~5.5 %, and ~1.3 % just freeing the cache maps in `~Impl()`.
The single-threaded `[next_warm] discover` frontier walk is another ~1.2 s serial
stretch. Investigating the arena/id-keying lever revealed two things: (1) the read-side
`Specs` is **already memoized** (`Src.specs_`), and interning a site is itself a
string hash, so id-keying only *moves* hashes rather than removing them — no net
win; and (2) most of the allocation is **inherent** (materializing millions of
composed `PrimSpec`s for a full non-deferred flatten) and a per-`Impl` `pmr`
arena conflicts with `MergeSources`, which *steals (moves)* each worker's
`vector<Src>` into the main `Impl`.

So the real bottleneck is the **general allocator**, not the keying. Confirmed by
swapping glibc `ptmalloc` for a drop-in thread-caching allocator (byte-identical
— an allocator does not change output):

| total (best-of-3) | glibc | **tcmalloc_minimal** |
|---|---:|---:|
| Caldera | 6.42 s | **5.17 s (−19 %)** |
| Island  | 12.70 s | **9.84 s (−22 %)** |
| ALab    | 2.68 s | **1.92 s (−28 %)** |

tcmalloc cut *both* compose (−25…31 %) and write (−16…18 %); Caldera RSS
2.72→2.12 GiB. jemalloc helped compose but slowed the ASCII write, so tcmalloc is
preferred. This is exposed as the CMake option **`TINYUSDZ_NEXT_WITH_TCMALLOC`**
(OFF by default) which links `tcmalloc_minimal` (else `tcmalloc`, else `mimalloc`,
else `jemalloc`) FIRST into the CLI tools; the library itself never forces an
allocator. Runtime equivalent for any build:
`LD_PRELOAD=libtcmalloc_minimal.so.4`. A hand-rolled compose arena was therefore
*not* pursued — the drop-in allocator captures the same win globally (compose +
write + PrimSpec building) with zero compose-engine risk.

**Tuning the allocator to this workload (mimalloc).** The flatten process is
short-lived and allocation-*accumulating* (compose materializes millions of
`PrimSpec`s that stay live until the write drains them), so an allocator that
**returns freed memory to the OS mid-run** (`madvise(MADV_DONTNEED)` / purge) just
burns syscalls and re-faults. Findings (best-of-3 total, byte-identical, sha256
unchanged):

| total / peak RSS | tcmalloc | mimalloc *default* | **mimalloc `MIMALLOC_PURGE_DELAY=-1`** |
|---|---:|---:|---:|
| Caldera | 5.21 s / 2.13 GiB | 5.99 s | **4.16 s (−20 % vs tcmalloc) / 3.18 GiB** |
| Island  | 9.75 s / 5.77 GiB | 10.68 s | 9.63 s / 7.45 GiB |

- **tcmalloc needs no tuning** — `TCMALLOC_RELEASE_RATE=0`,
  `TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES` (256 MiB…1 GiB), and
  `TCMALLOC_AGGRESSIVE_DECOMMIT=0` were all a wash (`tcmalloc_minimal` already
  avoids aggressive decommit). It stays the robust default: system-available,
  lowest RSS, zero env.
- **mimalloc's *default* purges** and is *slower* than tcmalloc; with purging off
  (`MIMALLOC_PURGE_DELAY=-1`) it is the **fastest allocator on churn-heavy compose
  (Caldera −20 %)**, a tie on geometry-heavy Island — but trades **more RSS** that
  grows with scene size (+1 GiB Caldera, +1.7 GiB Island, since freed memory is
  never returned). `MIMALLOC_EAGER_COMMIT=1` / a finite `PURGE_DELAY` added
  nothing (the process finishes before any purge would fire).
- **Recommendation:** keep tcmalloc as the linked default (robust, low RSS). For a
  batch flatten that is compose-bound and RSS-comfortable within budget, prefer
  **mimalloc + `MIMALLOC_PURGE_DELAY=-1`** (build mimalloc, then
  `LD_PRELOAD=libmimalloc.so MIMALLOC_PURGE_DELAY=-1`, or install `libmimalloc.so`
  so the CMake option links it). The portable takeaway is workload-shaped, not
  allocator-specific: **disable allocator memory-return for this short-lived
  accumulate-then-drain process.**
