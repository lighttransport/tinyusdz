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
| isNaupakaA | 426.7 KB | 4.27 M | 0.01 | 0.01 | 0.01 | 0.04 | 15.0 MB | 0.71 | 175.9 MB | 17.8× |
| isGardeniaA | 2.4 MB | 0.16 M | 0.17 | 0.00 | 0.00 | 0.19 | 87.8 MB | 1.13 | 258.0 MB | 5.9× |
| isPalmDead | 3.6 MB | 0.31 M | 0.00 | 0.05 | 0.01 | 0.20 | 120.8 MB | 0.64 | 203.1 MB | 3.2× |
| isHibiscus | 5.5 MB | 1.02 M | 0.27 | 0.04 | 0.00 | 0.47 | 154.8 MB | 1.89 | 425.1 MB | 4.0× |
| isDunesA | 24.9 MB | 0.21 M | 0.23 | 0.04 | 0.00 | 0.36 | 123.7 MB | 2.67 | 732.5 MB | 7.4× |
| isIronwoodA1‡ | 102.4 MB | 0.49 M | 0.00 | 0.08 | 0.03 | 0.89 | 835.9 MB | 0.58 | 230.9 MB | 0.7× |
| isCoral | 415.2 MB | 87.49 M | 3.41 | 1.48 | 0.01 | 8.23 | 3.0 GB | 4.08 | 3.3 GB | 0.5× |
| isBeach | 713.3 MB | 4086.73 M | 0.07 | 12.81 | 0.01 | 18.43 | 7.1 GB | 70.92 | 15.2 GB | 3.8× |

*(Table refreshed 2026-06 on the current optimized build — the same large-scene
work in the [refresh section](#large-scene-refresh--caldera--island--alab-2026-06-post-optimization),
plus the curve-build optimizations (slim curve storage, Morton-LBVH curve build,
parallel curve sub-BLAS, and dropping the redundant per-segment TriInfo) that cut
isIronwoodA1 2.39→0.89 s and 1.1 GB→836 MB — closing most of the gap to usdrecord
(0.7× wall). The first-pass numbers are preserved in git history.)*

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
| **ALab** `alab_set01` | techvar overlay set | 1.61 | 0.65 | 0.67 | **3.2 s** | **1.58 GiB** | 19.6 M / 10.8 M unique |
| **isCoral** | `element.usda` | 3.45 | 2.53 | 1.55 | **8.2 s** | **3.11 GiB** | 17.4 M |
| **Caldera** | `caldera.flattened.usdc` | 3.05 | 6.11 | 6.60 | **17.4 s** | **12.5 GiB** | 38.8 M |
| **Island (full)** | `island.usda` | 13.4 | 31.1 | 18.7 | **73.2 s** | **22.5 GiB** | 5.68 B inst / 53.8 M unique |

The optimizations generalize across the whole large-scene ladder, not just isCoral:

* **ALab** build **6.5 → 3.2 s (−51 %)**, peak **2.6 → 1.58 GiB (−39 %)** vs the
  prior recorded geom-only set run.
* **isCoral** **14.1 → 8.2 s** and **6.3 → 3.1 GiB** vs the first-pass row above —
  the bvh build alone went 4.0 → 1.55 s (bounded-parallel + intra-threaded), and
  the compose lost the `find()` rwlock contention (PropNameTable freeze) and the
  arena `mprotect` storm (`M_TOP_PAD`).
* **Island (full)** nearly saturates the ~24 GiB graceful-abort cap (22.5 GiB) and
  completes rather than OOM-killing; geom-only `-autoframe` here vs the lit
  `shotCam` row above, so the two aren't directly comparable.

**ALab reproduction** needs the techvar overlay (2.3.0 ships placeholder meshes;
the real geometry is the separate `techvar_assets` package):

```sh
cd /mnt/disk1/data/alab/usd
cp -al ALab-2.3.0/ALab _merged_ALab            # hardlink farm (~0 extra space)
cp -alf techvar_assets/fragment/. _merged_ALab/fragment/
tusdrender _merged_ALab/entity/alab_set01/alab_set01.usda out.png \
    -rtPreview -stats -w 320 -height 180 -autoframe
```

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

**isCoral — the geometry-bound case (0.5×, memory now won).** The one element
where both renderers load the *same* heavy geometry without an instancing shortcut
(87.5 M triangles, six `isCoral_geo.usd` variants). This was the focus of a
dedicated optimization pass (see [`iscoral-embree-gap`](large-scene.md) lineage):
PropNameTable freeze (compose `find()` rwlock contention), `M_TOP_PAD` (the
per-prim arena `mprotect` storm), bounded-parallel + intra-threaded BLAS build
(bvh 4.0 → 1.56 s), and the indexed base-group geometry stream. Result: **14.1 →
8.2 s and 6.3 → 3.0 GB peak — RSS now *beats* usdrecord (3.0 GB vs 3.3 GB)**,
while wall is ~1.9× (8.2 s vs 4.3 s). The residual is the geometry build itself:
even with compose at zero, the LightRT triangle stream + single-shot BVH (≈4.1 s)
already exceed Embree's *entire* 3.96 s pipeline on 17.4 M genuinely-unique
triangles — a builder-class gap (LightRT vs Embree's SIMD BVH), not an
optimization gap. **Memory: parity/win on both axes; build time: the remaining
architectural lever.**

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
