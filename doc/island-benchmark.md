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
| isNaupakaA | 426.7 KB | 4.27 M | 0.01 | 0.02 | 0.01 | 0.05 | 18.0 MB | 0.70 | 175.4 MB | 14.0× |
| isGardeniaA | 2.4 MB | 0.16 M | 0.17 | 0.01 | 0.00 | 0.19 | 87.0 MB | 1.11 | 256.1 MB | 5.8× |
| isPalmDead | 3.6 MB | 0.31 M | 0.00 | 0.10 | 0.01 | 0.28 | 132.0 MB | 0.63 | 202.1 MB | 2.2× |
| isHibiscus | 5.5 MB | 1.02 M | 0.31 | 0.08 | 0.00 | 0.48 | 158.3 MB | 1.84 | 418.1 MB | 3.8× |
| isDunesA | 24.9 MB | 0.21 M | 0.31 | 0.09 | 0.01 | 0.52 | 152.8 MB | 2.71 | 717.0 MB | 5.2× |
| isIronwoodA1‡ | 102.4 MB | 0.49 M | 0.01 | 0.15 | 0.02 | 2.27 | 1.1 GB | 0.62 | 232.6 MB | 0.3× |
| isCoral | 415.2 MB | 87.49 M | 4.54 | 4.00 | 0.01 | 14.11 | 6.3 GB | 3.99 | 3.2 GB | 0.3× |
| isBeach | 713.3 MB | 4086.73 M | 0.08 | 12.59 | 0.01 | 18.22 | 7.1 GB | 71.44 | 15.2 GB | 3.9× |

`tris` = instance-expanded *triangle* count tusdrender traces (only the *unique*
prototype geometry is stored — isBeach's 4.09 B visible expand from 63 K unique
across 86 prototypes placed by 22.1 M instances; it excludes curve strands).
`speedup` = usdrecord wall ÷ tusdrender wall. ‡ = isIronwoodA1's heavy XGen is
*curves* (`xgBonsai_curves.usd`, 34 BasisCurves prims) now ray-traced as hair —
its 2.27 s / 1.1 GB is the curve-scene build, and it renders the foliage usdrecord
does (the bare-trunk-only usdrecord row is 0.62 s).

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

## Analysis

**Light/medium elements (isNaupakaA → isDunesA, isHibiscus): clean tusdrender
wins.** 2.9–14× faster wall and 2–9× lower peak RSS than hdEmbree. The
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

**isCoral — the honest loss (0.3×).** The one element where both renderers load
the *same* heavy geometry without an instancing shortcut (87.5 M triangles, six
`isCoral_geo.usd` variants). After the per-triangle footprint work (follow-up 4)
tusdrender is **~3× slower** (12.8 s vs 4.3 s) and uses **~1.5× the RAM**
(4.85 GB vs 3.2 GB, down from 6.5 GB). The cost is load (4.6 s) + preview-BVH
build (4.4 s): the `next` triangle stream and the single-shot LightRT builder are
not competitive with Embree's mature parallel BVH and its tighter in-memory
geometry layout. **This is the case to optimize** — actual-geometry-bound scenes,
not loader-bound ones.

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
