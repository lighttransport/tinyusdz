# Moana Island per-element benchmark — tusdrender vs. usdrecord (hdEmbree)

A head-to-head of tusdview's `tusdrender` (LightRT BVH + the `next` lazy USDC
loader) against Pixar's `usdrecord` driving the CPU **hdEmbree** Hydra delegate,
on the **independent per-element geometry** files of the Disney Moana Island
scene (`/mnt/disk1/data/island`). This is the first pass — individual elements
only, not the full assembled `island.usda` (a deliberate follow-up).

Methodology mirrors the Activision Caldera benchmark in
[`tools/tusdrender/README.md`](../tools/tusdrender/README.md) and
[`large-scene.md`](large-scene.md) §6.

## Methodology

* **Host:** AMD Ryzen Threadripper 1950X (16C/32T), 62.6 GB RAM. tusdrender auto
  memory cap ≈ 23.2 GiB (`min(32, 0.5·MemAvailable)`).
* **Resolution / framing:** `320×180`, `-autoframe` (matches Caldera).
* **Inputs:** each element's standalone `usd/elements/<name>/element.usda`
  (three-file `element → instance → geometry` pattern → `model.usd` + XGen
  `prepend payload` archives).
* **tusdrender:**
  `tusdrender <element.usda> out.png -rtPreview -stats -w 320 -height 180 -autoframe`.
  Load / triangle-stream / BVH-build / render seconds come from `-stats`; peak RSS
  and wall from `/usr/bin/time -v`.
* **usdrecord:**
  `usdrecord --renderer Embree --disableGpu --imageWidth 320 <element.usda> out.png`
  with `PYTHONPATH=$dist/lib/python LD_LIBRARY_PATH=$dist/lib
  PXR_PLUGINPATH_NAME=$dist/plugin/usd`. usdrecord prints no stage breakdown, so
  only total wall + peak RSS are recorded.
* **Harness:** [`tools/tusdrender/bench_island.py`](../tools/tusdrender/bench_island.py)
  (reproducible; writes `results.json` + `results.md`).

## Results

| element | size | tris | tusd load s | tusd bvh s | tusd render s | tusd total s | tusd RSS | usdrecord s | usdrecord RSS | speedup |
|---|---|---|---|---|---|---|---|---|---|---|
| isNaupakaA | 426.7 KB | 4.27 M | 0.01 | 0.01 | 0.01 | 0.04 | 14.7 MB | 0.74 | 174.8 MB | 18.5× |
| isGardeniaA | 2.4 MB | 0.16 M | 0.17 | 0.00 | 0.00 | 0.19 | 87.8 MB | 1.14 | 257.5 MB | 6.0× |
| isPalmDead | 3.6 MB | 0.31 M | 0.00 | 0.05 | 0.01 | 0.20 | 120.8 MB | 0.69 | 201.5 MB | 3.4× |
| isHibiscus | 5.5 MB | 1.02 M | 0.27 | 0.04 | 0.00 | 0.47 | 154.5 MB | 1.94 | 421.3 MB | 4.1× |
| isDunesA | 24.9 MB | 0.21 M | 0.24 | 0.04 | 0.00 | 0.36 | 123.6 MB | 2.56 | 704.5 MB | 7.1× |
| isIronwoodA1‡ | 102.4 MB | 0.49 M | 0.01 | 0.08 | 0.03 | 0.92 | 834.7 MB | 0.62 | 231.9 MB | 0.7× |
| isCoral | 415.2 MB | 87.49 M | 3.40 | 1.54 | 0.01 | 6.43 | 3.1 GB | 4.15 | 3.3 GB | 0.6× |
| isBeach | 713.3 MB | 4086.73 M | 0.07 | 12.74 | 0.01 | 18.59 | 7.1 GB | 73.45 | 15.2 GB | 4.0× |

*(Table refreshed 2026-06 on the current optimized build. Notable since the prior
refresh: the curve-build optimizations (slim curve storage, Morton-LBVH curve
build, parallel curve sub-BLAS, dropping the redundant per-segment TriInfo) cut
isIronwoodA1 2.39→0.92 s / 1.1 GB→835 MB; and an **O(1) `Stage::GetPrimAtPath`**
fix (it was re-deriving the prim index with an O(N) linear scan per per-mesh
material/texture lookup) cut isCoral's stream 2.47→0.62 s and total 8.2→6.4 s, and
helps every multi-mesh scene (Island full 73→66 s). The first-pass numbers are
preserved in git history.)*

`tris` = instance-expanded *triangle* count tusdrender traces (only the *unique*
prototype geometry is stored — isBeach's 4.09 B visible expand from 63 K unique
across 86 prototypes placed by 22.1 M instances; it excludes curve strands).
`speedup` = usdrecord wall ÷ tusdrender wall. ‡ = isIronwoodA1's heavy XGen is
*curves* (`xgBonsai_curves.usd`, 34 BasisCurves prims, ~3 M hair segments) ray-
traced as hair — its 0.89 s / 836 MB is the curve build, and it renders the
foliage usdrecord does (the bare-trunk-only usdrecord row is 0.58 s). The curve
path was optimized end to end: per-segment storage slimmed to endpoints + a
per-BLAS material (1.08 GB → 842 MB); the build switched from serial binned-SAH to
parallel Morton-LBVH; large prototypes split into sub-BLAS whose collapses build
concurrently; and the redundant per-segment `TriInfo` intermediate (~360 MB,
profiling showed it — not the read/transform — was the serial bottleneck) was
dropped, deriving endpoints straight from the points. Net **2.32 → 0.89 s
(−62 %)**, now 0.7× usdrecord. The residual is the parallel LightRT build/collapse.

> **PointInstancer expansion (fixed).** An earlier revision of this benchmark
> showed isBeach at 0.06 M tris because the `-rtPreview` path skipped
> UsdGeomPointInstancer geometry (the XGen ground cover, stones, shells, …).
> `tusdrender.cc` now expands every PointInstancer into the two-level BLAS/TLAS
> path: each prototype is a deduped BLAS, each visible instance a TLAS placement
> (scale·orient·translate composed with the instancer's world). This expanded
> isNaupakaA 0.02 → 4.27 M, isCoral 17.4 → 87.5 M, and isBeach 0.06 M → 4.09 B
> visible tris. Verified visually against `ref/hyperionRenders/` (isNaupakaA's
> instanced foliage clumps match the Hyperion reference).

> **Curve ray tracing (added).** The `-rtPreview` path now ray-traces
> UsdGeomBasisCurves / NurbsCurves as LightRT hair strands (round by default,
> flat/ribbon when `normals` are authored), built into the DirectScene shared by
> the flat and TLAS render paths. **Curve-prototype instancing** is supported too:
> a PointInstancer whose prototype contains curves stores them once as a **curve
> BLAS** (round hair) instanced through the same TLAS as meshes — geometry is
> deduped, not baked per instance. isIronwoodA1 now renders its bonsai foliage;
> verified visually (`isPalmRig` upright fronds, isIronwoodA1 canopy) and with a
> synthetic curve-prototype-instancer smoke test.

> **Parallel TLAS build (added).** The two-level instanced path built its TLAS
> through three serial per-instance loops (the `add_instance` fill in
> `tusdrender.cc`, plus LightRT's `tlas_fill_instances` matrix-inversion and
> `tlas_rebuild` bounds passes). All three are now multithreaded — a pre-sized
> parallel scatter with a validity prefix-scan on the tusdrender side, and
> `tri_parallel_for` chunks (gated ≥4096 instances) on the LightRT side — byte
> for byte identical output. isBeach's 22.1 M-instance bvh build dropped
> 19.3 s → 12.6 s; the remaining cost is LightRT's already-threaded SAH BVH build
> over the instances (architectural, shared with the isCoral gap).

> **Prototype double-count (fixed).** A native-instance *prototype holder* prim
> (e.g. `/isIronwoodA1/isIronwoodA2`) has its `instanceable` flag cleared during
> composition, so it was both traversed as base geometry *and* drawn through its
> instance proxy — double-counting its triangles (and rendering two overlapping
> copies). A `CollectPrototypePaths` pre-pass now gathers holder paths and
> `CollectSceneSplit` skips their subtrees. This corrected isIronwoodA1
> (0.97 → 0.49 M tris, one tree) and isHibiscus (1.28 → 1.02 M).

## Full scene — `island.usda`

The assembled scene (all 20 elements, scenegraph instancing + XGen PointInstancers
+ BasisCurves, lit by the `islandsun.exr` DomeLight) renders directly from
`usd/island.usda` through the `shotCam` at 320×180:

| metric | value |
|---|---|
| visible triangles | **5.69 B** |
| unique stored | 56.6 M |
| instances | 22.8 M (46 PointInstancers) + 52 curve instances |
| BLAS | 263 |
| wall | **1 m 33 s** |
| peak RSS | **25.9 GB** (needs `-maxMem` raised above the default `min(32, 0.5·avail)` cap, which aborts cleanly) |

This is the payoff of the PointInstancer + curve work: the whole island composes
and ray-traces directly from raw USD — 5.69 B effective triangles dedup to 56.6 M
unique. The per-triangle footprint work (follow-up 4: 4 B `TriStore` + per-BLAS
`TriMat` table + compact 3×4-float instance placements) cut peak RSS from the
initial **44 GB → 25.9 GB (−41 %)** and wall from 2 m 18 s → 1 m 33 s
(byte-identical geometry). A `usdrecord` full-scene comparison is impractical on a single host
(production references use Hyperion/RenderMan render farms); the per-element table
above characterizes the two renderers.

## Large-scene refresh — Caldera / Island / ALab (2026-06, post-optimization)

A cross-scene snapshot on the **current build**, after the compose + BVH-build +
indexed-geometry optimization work: `next` PropNameTable freeze (lock-free reads),
`M_TOP_PAD` arena tuning (kills the per-prim `mprotect` storm), bounded-parallel +
intra-threaded BLAS build, and the indexed base-group geometry stream
(`lrt_tri_scene_build_indexed`: 1× unique verts + indices instead of a 3×-expanded
soup). Same methodology as above — `320×180`, `-rtPreview -autoframe`, geom-only
(no camera/lights), `/usr/bin/time -v` for peak RSS. Auto memory cap ≈ 24.6 GiB.

| scene | input | load s | stream s | bvh s | **total** | **peak RSS** | triangles |
|---|---|---|---|---|---|---|---|
| **ALab** `alab_set01` | techvar overlay set | 1.60 | 0.63 | 0.69 | **3.2 s** | **1.59 GiB** | 19.6 M / 10.8 M unique |
| **isCoral** | `element.usda` | 3.40 | 0.62 | 1.50 | **6.4 s** | **3.1 GiB** | 17.4 M |
| **Caldera** | `caldera.flattened.usdc` | 2.99 | 6.17 | 5.37 | **16.1 s** | **12.5 GiB** | 38.8 M |
| **Island (full)** | `island.usda` | 13.3 | 24.4 | 18.1 | **65.7 s** | **22.5 GiB** | 5.68 B inst / 53.8 M unique |

The optimizations generalize across the whole large-scene ladder, not just isCoral:

* **ALab** build **6.5 → 3.2 s (−51 %)**, peak **2.6 → 1.59 GiB (−39 %)** vs the
  prior recorded geom-only set run.
* **isCoral** **14.1 → 6.4 s** and **6.3 → 3.1 GiB** — bvh 4.0 → 1.5 s
  (bounded-parallel + intra-threaded), compose lost the `find()` rwlock contention
  (PropNameTable freeze) + the arena `mprotect` storm (`M_TOP_PAD`), and the stream
  dropped 2.5 → 0.6 s from the O(1) `GetPrimAtPath` fix.
* **Island (full)** nearly saturates the ~24 GiB graceful-abort cap (22.5 GiB) and
  completes rather than OOM-killing; its stream fell 31 → 24 s from the same
  `GetPrimAtPath` fix (many small instanced meshes). Geom-only `-autoframe` here vs
  the lit `shotCam` row above, so the two aren't directly comparable.

**ALab reproduction** needs the techvar overlay (2.3.0 ships placeholder meshes;
the real geometry is the separate `techvar_assets` package):

```sh
cd /mnt/disk1/data/alab/usd
cp -al ALab-2.3.0/ALab _merged_ALab            # hardlink farm (~0 extra space)
cp -alf techvar_assets/fragment/. _merged_ALab/fragment/
tusdrender _merged_ALab/entity/alab_set01/alab_set01.usda out.png \
    -rtPreview -stats -w 320 -height 180 -autoframe
```

### NamespaceMapping pooling — compose allocation lever (commit `48aef48d`, 2026-06-21)

Profiling the isCoral compose showed `basic_string` operations at **~12 % self**
(the largest aggregate, ahead of the geometry stream) — path-string churn, not
`Value` copies (only 0.43 %). A big slice is the per-source `NamespaceMapping`:
a non-identity mapping's two path prefixes are *shared* by every `Src` in the
subtree below an arc (children inherit the parent's mapping unchanged in the
source child-build), yet the build re-copied them per prim — ~280 K redundant
long-string copies on isCoral's 140 K reference/payload-backed prims. The fix
pools the mappings in the compose `Impl` and stores a 4-byte index on each source
(`Src.map` 64 B → `map_idx` 4 B; `Src` shrinks 136 → 128 B), so the bulk
child-build copies an int. New mappings are appended only in the serial arc
expansion; the parallel opinion fill only *reads* the pool (deque → stable
addresses, lock-free). **Byte-identical** (suzanne / isCoral / isIronwoodA1 /
isBeach / isPandanusA + full `island.usda` all unchanged; `next` 23/23; smoke).

Measured before → after on the same build (`-rtPreview -w 320 -height 180`;
`sources` = the serial `[next_build]` source-resolution phase via
`TINYUSDZ_NEXT_TIMING`; wall + peak RSS from `/usr/bin/time`):

| scene | input | `sources` ms | wall s | peak RSS | md5 |
|---|---|---|---|---|---|
| **isCoral** | `element.usda` | 1739 → **1480** (−260) | 6.52 → **6.0** (−8 %) | ~3.07 GB (flat) | `d00a4931041f` |
| **ALab** | `_merged_ALab/entry.usda` | 1555 → **1360** (−195) | 4.14 → **3.77** (−9 %) | 1.95 → **1.80 GB** (−150 MB) | `3e526a58217e` |
| **Caldera** | `caldera.usda` | 572 → 571 (~0) | ~5.07 (~0) | ~3.49 GB (~0) | `563be5ec4d1d` |

The win scales with the number of prims under **non-identity** arc mappings:
solid on the two reference/payload-heavy scenes (isCoral 140 K prims; ALab ~3.3 K
prims / ~1.3 K payloads, which also sheds 150 MB resident from the smaller `Src`
and de-duplicated mappings), and a clean **no-op on Caldera** — it composes to
comparatively few prims, so most mappings are identity (`map_idx 0`, no pool
entry, nothing to dedup). No regression anywhere: with identity mappings the
pooled path is a strict 4-byte index copy. (This is the *tractable* allocation
lever; a parallel attempt at compose **path-interning** — int-keying the spec
cache — proved net-neutral because interning re-allocates the path it removes,
on this allocation-bound compose.)

## Analysis

**Light/medium elements (isNaupakaA → isDunesA, isHibiscus): clean tusdrender
wins.** 3–18× faster wall and ~2–12× lower peak RSS than hdEmbree. The
`next` loader's startup is sub-second and LightRT's preview BVH builds in tens of
milliseconds, where usdrecord pays a fixed Hydra/USD-stage + delegate spin-up
cost (~0.7 s floor even on the tiniest element) and carries a larger resident
footprint.

**isBeach — instancing pays off (2.9×).** 22.1 M instances → 4.09 B effective
triangles, but only 63 K unique are stored, so the two-level TLAS renders the
full XGen ground cover in **25 s / 7.5 GB** vs usdrecord's **73 s / 15.2 GB**.
The footprint here is the *instance bookkeeping*, not geometry: compacting
`InstanceRT` to a 3×4 float (48 B vs a 128 B `matrix4d`) and freeing the
collection-side instance lists before the TLAS build cut isBeach from 11.7 →
7.5 GB and 32 → 25 s (byte-identical). The wall is dominated by the TLAS build
over 22 M instances.

**isCoral — the geometry-bound case (0.6×, memory won).** The one element where
both renderers load the *same* heavy geometry without an instancing shortcut
(87.5 M triangles, six `isCoral_geo.usd` variants). Focus of a dedicated pass (see
[`iscoral-embree-gap`](large-scene.md) lineage): PropNameTable freeze (compose
`find()` rwlock contention), `M_TOP_PAD` (the per-prim arena `mprotect` storm),
bounded-parallel + intra-threaded BLAS build (bvh 4.0 → 1.5 s), the indexed
base-group geometry stream, and — the biggest single step — an **O(1)
`Stage::GetPrimAtPath`**: it was re-deriving the prim index with an O(N) linear
scan over all 140 K prims on *every* per-mesh material/texture lookup, ~8% of the
profile but far more in wall time (stream 2.47 → 0.62 s). Result: **14.1 → 6.4 s
and 6.3 → 3.1 GB peak — RSS *beats* usdrecord (3.1 vs 3.3 GB)**, wall ~1.6×
(6.4 vs 4.2 s). The residual is now **compose-dominated, not the builder**: load
≈3.4 s (crate-read ~1.3 + string-keyed Pcp source resolution 1.75 serial) vs the
BVH build's 1.5 s. The remaining levers are compose path-interning (vs Pixar's
interned `SdfPath`) and an Embree-class SIMD BVH builder. **Memory won on both
axes; build time ~1.6× and now a compose + builder question, not pure builder.**

## Reproduce

```sh
python3 tools/tusdrender/bench_island.py \
    --island /mnt/disk1/data/island \
    --dist   /mnt/nvme02/work/tinyusdz-repo/OpenUSD/dist \
    --bin    build_ninja/tools/tusdrender/tusdrender \
    --out    /tmp/island_bench
# force a graceful-abort row by capping memory on the heavy element:
python3 tools/tusdrender/bench_island.py --elements isCoral \
    --tusd-extra='-maxMem 2' --skip-usdrecord
```

## Follow-ups

1. ~~**PointInstancer expansion** in the `-rtPreview` path.~~ **Done.**
2. ~~**Curve ray tracing** (BasisCurves/NurbsCurves) + curve-prototype
   instancing.~~ **Done** — see the curve note above.
3. ~~**Curve instancing dedup.**~~ **Done** — instanced curve prototypes are now a
   deduped **curve BLAS** (round hair) instanced through the same TLAS as meshes
   (`lrt_roundcurve_scene_build` returns an `lrt_tri_scene*` the TLAS accepts;
   `Blas` carries an `is_curve` tag + per-segment `curve_info`, resolved in
   `ResolveTLASHit`). Geometry is stored once, not baked per instance. (Instanced
   curves are treated as round; per-instance flat/ribbon curves are not separated.)
4. **isCoral footprint — profiled (bandwidth-bound) + material-table refactor
   done.** Thread-scaling (1→8→32 threads) showed **all three phases plateau at
   ~4.5–4.7 s by 8 threads on a 16C/32T host → memory-bandwidth bound, not CPU
   bound**, so footprint is the lever. **Done** — the instanced (TLAS) BLAS no
   longer store a 124 B `TriInfo` per triangle. Per-mesh material moved to a
   per-BLAS **`TriMat`** table (one entry per mesh-job); positions are read from
   the BLAS **vertex soup** (`Blas::vertices`, which LightRT aliases so it's
   already resident) and the normal recomputed at hit time, so the per-triangle
   record (**`TriStore`**) is just a **4 B `mat_id`** (down from 124 B). Combined
   to a full `TriInfo` in `ResolveTLASHit` via `CombineTriMat`. Per-material
   resolution is also cached. Result on isCoral: tracked buffers **4.92 →
   1.29 GiB**, peak RSS **6.5 → 4.85 GB**, and faster (14.9 → 12.8 s, confirming
   bandwidth-bound). **Byte-identical** renders (md5-verified on flat + TLAS +
   curve-instancing scenes). The flat/legacy path keeps full `TriInfo` (those
   scenes are small). Remaining gap to Embree's 3.2 GB / mature BVH is
   architectural.
5. ~~**Native-instance curve placement.**~~ **Done** — curves under a scenegraph
   (`instanceable`) prototype are now placed per native instance via the deduped
   curve BLAS (`ReserveCurveProto` shared by the PointInstancer and native-instance
   paths). isIronwoodA1's instanced copy now gets foliage too (verified visually).
   (isIronwoodA1's 2× trunk is a separate, pre-existing quirk specific to its nested
   payload+instanceable structure, not a general double-count.)
6. ~~**Full-scene `island.usda`** benchmark.~~ **Done** — see the Full scene
   section above (5.69 B tris, 2 m 18 s, 44 GB).
