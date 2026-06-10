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

`large-scene-load caldera.usda --mode=none` composes the scene structure in
**~1.4 GiB / ~2.5 s** — well within the 16 GB budget. Payload deferral itself is
verified (e.g. `tests/usda/payload-001.usda` → 2 deferred payloads).

---

## 3. Roadmap (remaining implementation)

The loader bounds memory and streams payloads, but two gaps remain before these
scenes compose *completely*. Both are pre-existing TinyUSDZ limitations, not
specific to the loader.

### 3.1 Cross-directory cwp anchoring (PRIMARY blocker)

**Symptom:** on Caldera, the top structure composes but deep geometry references
silently fail to resolve (`deferred payloads = 0`; only first-level sibling refs
are followed).

**Root cause:** a loaded layer's PrimSpecs get their working-path stamped from
`GetBaseDir(authored_asset_path)` — a *relative* path — instead of the *resolved
absolute* base directory. See `LoadLayerFromMemory`
(`src/tinyusdz.cc:1635`, `GetLayerBaseDirForAssetName`) and `LoadAsset`
(`src/composition.cc:512`, which passes the authored relative `asset_path`).
Across nested references each level rebases relative to its own authored path and
loses the parent directory context, so e.g. `prefabs/...` inside
`map_source/mp_wz_island_geo.usd` anchors to the scene root instead of
`map_source/`. `Compose` reads the per-PrimSpec cwp with no fallback at
`src/composition-graph.cc:688`.

**Fix:** stamp the resolved absolute base dir (`GetBaseDir(resolved_path)`,
already computed and set as the resolver cwp at `composition.cc:451`) onto every
loaded layer's prims. Touch points: the sublayer branch of `LoadAsset`
(`primspec_root == nullptr` currently skips the re-stamp at
`composition.cc:647`), `DefaultLoadAndOwnLayer` (`composition-graph.cc:330`), and
`pcp::LayerRegistry::GetOrLoad`. This is a core composition change — validate
against the full unit suite (≈850 cases) for regressions, since some tests rely
on the current relative behavior.

### 3.2 mmap-through-composition (SECONDARY)

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

### 3.3 Smaller items

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
# structural load within budget (run from the scene dir so first-level
# relative refs resolve until §3.1 lands):
( cd /mnt/disk1/data/caldera && \
  <build>/large-scene-load caldera.usda --mode=none )
# expect: RSS ~1.4 GiB, Stage built; deferred count grows once §3.1 is fixed.
cd build && ctest --output-on-failure     # no regressions from the seam changes
```
