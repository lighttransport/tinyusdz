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
the initial load.

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

**Note — pcp::Cache still has this limitation.** The cached `pcp::Cache` engine
(not used by `LargeSceneLoader`) computes each prim from its root-layer local
PrimSpec only and does not yet expand referenced descendants; its per-entry
composition-context model needs rework to share a context across a referenced
subtree. The `pcp_buildstage_reference_grandchildren_test` documents this (it
asserts the eager engine reconstructs grandchildren and notes pcp does not yet).

### 3.3 mmap-through-composition (SECONDARY)

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

### 3.5 Variant-content composition — FIXED (root/subLayer variant sets)

CompositionGraph records variant *selections* but `ResolveAndApplyVariants` is a
no-op, so the selected variant's *content* (child prims + their nested
references/payloads/clips) was never composed — variant-gated geometry (e.g.
baked_procedurals' `GEO`) was dropped. Two fixes:
- **Merge variantSet content across composition** (`CombinePrimSpecRec`,
  `src/composition.cc`): the `variantSet "x" = { … }` blocks live in
  `variantSets()` and were not merged when a prim received opinions from
  multiple layers, so a prim selecting a variant whose set is defined in a
  weaker sublayer lost the content. Now the per-variant content is merged.
- **Resolve authored variant selections before Compose** (`LargeSceneLoader`,
  reusing the LIVRPS `ApplyVariantSelector` + `ListVariantSelectionMaps`): the
  selected variant's content is flattened into the prim tree, after which the
  normal namespace expansion reconstructs the variant's children.

Regression: `feat-large-scene` scenario 3 (`fixture/variant/`) — variantSet
content in a weaker sublayer, selection in the root; `/P/GEO` and `/P/GEO/M`
must reconstruct.

**Remaining:** this resolves variant sets defined in the root layer stack (root
+ subLayers). A variant set introduced via a *reference* (an asset defines the
set, a stronger layer selects it) is not yet composed by the DAG — that needs
variant-aware nodes in CompositionGraph's per-prim index. Value clips themselves
(`clips` metadata) are resolved at Tydra / attribute-evaluation time
(`enable_value_clips`), not during composition.

### 3.6 Smaller items

- **Budget-by-extent**: the `Budget` policy currently sizes payloads by file
  bytes; authored `extentsHint` would let it bound by world-space coverage, but
  the payload-policy callback would need the owning `PrimSpec`.
- **ALab `.zip`**: ALab ships zipped; add a zip-backed asset resolver (or
  extract) to load it without unpacking 4 GB to disk.
- **fd/mmap bounds**: when mmap lands, cap open descriptors with an LRU on the
  layer registry (Caldera has 63k files).
- **Threaded streaming**: `load_payload`/`unload_payload` mutate the graph in
  place and are single-threaded post-load.

---

## 4. Verification

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
```
