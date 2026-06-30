# tusdrender

CPU preview ray-tracer for USD scenes (LightRT BVH + the `next` lazy USDC
loader). Renders large, reference- and instance-composed scenes directly, within
a bounded memory budget.

## Quick start

```sh
tusdrender scene.usd out.png -rtPreview -w 960 -height 540 -autoframe
```

Common flags:

| flag | meaning |
|------|---------|
| `-rtPreview` | ray-traced preview (the `next` loader; default for USDC) |
| `-vk` / `-vkr` / `-d3d` | GPU backends — Vulkan compute / Vulkan ray query / Direct3D 11 compute — see [`doc/tusdrender.md`](../../doc/tusdrender.md) for status + testing |
| `-w N -height N` | image size (`-height` omitted → from camera aspect) |
| `-autoframe` | usdrecord-style auto camera framing |
| `-camera <path>` | render through a named `UsdGeomCamera` |
| `-mask <prim,...>` | restrict to these prim subtrees |
| `-complexity low\|med\|high\|veryhigh` | subdivision preset |
| `-smooth` | interpolate authored normals (smooth shading) |
| `-displaceScale <f>` | `UsdPreviewSurface` displacement multiplier (default 1.0; `-noDisplace` disables) |
| `-maxMem <GiB>` | memory cap override (default `min(32, 0.5·MemAvailable)`) |
| `-stats` | print mesh/triangle/memory/timing stats |

## Composition, instancing, memory

* **Direct composition** — reference/payload/sublayer/variant/instancing arcs are
  resolved in place (full PCP engine), so reference-composed scenes (e.g. Caldera
  prefab stubs) render directly with no external `usdcat --flatten` step.
  Self-contained / pre-flattened inputs skip composition and render
  byte-identically to before.
* **Two-level (instanced) BVH** — native instances are placed by a LightRT TLAS
  with each prototype's geometry built once (BLAS), so a scene with millions of
  *visible* triangles only stores the *unique* prototype geometry.
* **Per-instance view-dependent LOD (`-rtLod`)** — at TLAS build time each mesh
  placement is classified from the resolved camera: distant prototypes collapse to
  a shared unit-box BLAS (box-fit onto the prototype AABB), sub-pixel placements are
  dropped, near ones keep the real BLAS. Parity with the interactive viewer's
  `--rt-lod` (`tusdr_rt_lod.{hh,cc}`). Tunables: `-rtLodFullPx` (promote-to-full
  radius, def 64), `-rtLodCullPx` (drop radius, def 2), `-rtLodNoProxy` (Full-or-Cull
  only). Off by default. **Offline caveat:** a path tracer needs off-screen geometry
  for shadows/reflections/GI, so frustum culling is a separate opt-in
  (`-rtLodFrustumCull`, faster but changes lighting); proxy/sub-pixel are softer
  approximations. Applies to the CPU two-level TLAS path (`-rtPreview`). The
  `-vk`/`-vkr` GPU backends flatten the scene to a single world-space BLAS (no
  instance list), so per-instance LOD there needs a two-level GPU build first — a
  documented follow-on (see `doc/tusdrender.md`).
* **Shading** — bound `UsdPreviewSurface` (diffuse/normal/roughness/metallic/
  emissive/occlusion textures); for unmaterialed geometry, `primvars:displayColor`
  / `displayOpacity` are honored — constant (per-mesh) and per-vertex/faceVarying/
  uniform (barycentrically interpolated per hit), with opacity blended see-through.
  Geom-only assets (e.g. Animal Logic ALab) render in color with transparent
  glass. UsdGeomBasisCurves/NurbsCurves render as LightRT hair. `-smooth`
  interpolates authored `normals` for smooth shading (default is per-face
  geometric normals, which keeps the lean 4 B/triangle instanced footprint).
* **Displacement** — `UsdPreviewSurface inputs:displacement` (constant or a
  height texture, honoring the `UsdUVTexture` `scale`/`bias`) offsets each vertex
  along its normal before the BVH build — coarse (no extra geometry), so it works
  on the CPU path tracer and the `-vk`/`-vkr` backends. `-displaceScale <f>` tunes
  the amount; `-noDisplace` turns it off (byte-identical to before).
* **Lighting** — UsdLux finite lights (Rect/Sphere/Disk/Cylinder/Distant) are
  collected from the composed stage and shaded with soft area falloff; DomeLight
  is image-based lighting (`--env` overrides it). Scenes with no lights fall back
  to a camera headlight. This lights interiors a dome can't reach (e.g. ALab's
  shot lighting rig, which renders the full lit shot directly from `entry.usda`).
* **Memory cap** — a process budget of `min(32 GiB, 0.5 × system MemAvailable)`
  (override with `-maxMem`). When a scene would exceed it, tusdrender aborts
  cleanly with an actionable message (raise `-maxMem`, narrow with `-mask`, or
  lower `-complexity`) instead of being OOM-killed.

## Validated benchmark — Activision Caldera maps

All six maps rendered directly from their raw `.usd` (no pre-flatten), 320×180,
`-autoframe`, on a host with ~49 GiB available (auto cap 24.5 GiB). "visible" is
the instance-expanded triangle count actually traced; "unique" is the prototype
geometry stored once.

| scene          | visible tris | unique stored | dedup | instances | peak RSS | wall  |
|----------------|--------------|---------------|-------|-----------|----------|-------|
| restaurant     | 2.4 M        | 764 K         | 3.2×  | 1.8 K     | 0.4 GB   | 1.2 s |
| power_station  | 19.8 M       | 5.7 M         | 3.5×  | 11 K      | 2.4 GB   | 6.9 s |
| beachhead      | 160 M        | 19.9 M        | 8.0×  | 105 K     | 9.9 GB   | 39 s  |
| phosphate_mine | 126 M        | 28.2 M        | 4.5×  | 73 K      | 11.7 GB  | 41 s  |
| airfield       | 163 M        | 35.3 M        | 4.6×  | 104 K     | 17.1 GB  | 64 s  |
| capital        | **195.7 M**  | 43.1 M        | 4.5×  | 136 K     | 21.3 GB  | 81 s  |

Notes:

* Instancing dedup is what keeps these in memory: capital's 195.7 M visible
  triangles would need ~40 GB as a flat array + BVH (which overflows the LightRT
  builder), but only 43.1 M unique are stored → 21.3 GB.
* These RSS figures scale with the host. On a 32 GiB machine the cap is ~16 GiB,
  so airfield/capital abort gracefully (use `-maxMem`/`-mask`) rather than
  OOM-killing.

## Moana Island per-element benchmark

A head-to-head against Pixar `usdrecord` (CPU **hdEmbree**) on the Disney Moana
Island per-element geometry files lives in
[`doc/island-benchmark.md`](../../doc/island-benchmark.md), with the reproducible
harness [`bench_island.py`](bench_island.py). Summary: tusdrender is 2.9–14×
faster with 2–9× lower RSS on light/medium elements. UsdGeomPointInstancer
geometry (the XGen ground cover / foliage) is expanded through the two-level
BLAS/TLAS path, so isBeach renders its 4.09 B effective triangles (22 M instances,
63 K unique) in 32 s / 11.7 GB — **2.2× faster and lower RAM than usdrecord**
(73 s / 15.2 GB). UsdGeomBasisCurves / NurbsCurves are ray-traced as LightRT hair
strands; curve-prototype PointInstancers store the curve geometry once (a curve
BLAS) and instance it through the same TLAS as meshes, so isIronwoodA1's bonsai
foliage renders. The instanced (TLAS) path is memory-lean: the per-triangle
record is a 4 B material id (material in a per-BLAS table; positions read from the
LightRT-aliased vertex soup), and instance placements are a compact 3×4 float.
This cut isCoral 6.5 → 4.85 GB and isBeach 11.7 → 7.5 GB (both byte-identical).
The one genuinely geometry-bound element (isCoral, 87.5 M tris, no instancing
shortcut) is still ~3× slower than Embree at ~1.5× the RAM (memory-bandwidth
bound; the gap is architectural). The full
assembled `island.usda` (all 20 elements + XGen + curves + DomeLight) renders
directly from raw USD: **5.69 B effective triangles** (22.8 M instances, 56.6 M
unique) in **1 m 33 s / 25.9 GB** (`-maxMem`). See the doc's analysis and
follow-ups.
