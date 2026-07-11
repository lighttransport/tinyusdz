# tusdview — remaining work

Tracked companion to the (local, untracked) `resume-tusdview.md` scratch notes.
This file is the durable list: what is still open, why, and what it touches.

Status as of 2026-07-11. The `--next` primvars / texturing / texture-VRAM
workstream is **done and pushed**, including GPU skinning (raster, both backends,
instanced prototypes included) and the large-scene verification (numbers in
[large-scene.md §2.9](large-scene.md)).

Also done since the last revision of this file, and no longer listed below: the
shared material-eval Phase 2 (sphere-light NEE + MIS), per-texture UV-set
selection, the tusdview BLAS-compaction port, `--vram-budget`, raster LOD for
non-instanced meshes (§2.10), and the `-vk` tiled ray dispatch (§2.11 — which
also records why device-local BVH buffers were measured and left OFF).

Done since (2026-07-12): Vulkan-RT skinning without a reconvert, the two
double-counted light contributions in `lightrt-bsdf`, area-sampled rect / disk /
cylinder lights (plus the inverted UsdLux light normal that made them light
nothing), the TLAS `PREFER_FAST_BUILD` question (measured, rejected —
large-scene.md §2.12), and the `lightrt-bsdf` IBL energy (the furnace now lands at
1.00x the dome that lights it, from 2.00x).

## Open

### 1. CUDA / HIP skinning

The Vulkan ray tracer now re-poses the retained rest vertices per frame
(`BuildNextRtDeformedVertices`) instead of re-running the converter for every time
code. The CUDA/HIP tracers still take the load-time CPU bake: they read `draw_`
geometry directly and free it once their own BVH is built, and they only render a
one-shot screenshot, where the bake IS the cheapest path. Making them animate
interactively means re-posing into their BVH, not just into `draw_`.

### 2. Mesh lights on the LEGACY `-rtPreview` path

The `next` gaps are closed (`tool-tusdrender-mesh-light-gaps`): `inputs:normalize`
now divides the radiance by the emitting world area, and an `instanceable`
emissive mesh registers one light per PLACEMENT (it registered none — mesh lights
were collected from the flat triangle list, which an instanced prototype is
deliberately not in). Both are asserted as equivalences, not thresholds:
normalized(area 4, intensity 40) == plain(intensity 10), and instanced == the same
lamps written out in full.

What is left is the legacy loader, and the earlier note here — "the emitter's
material tint differs, so next lights a floor ~3x brighter" — was wrong about the
cause. `-legacyLoad -rtPreview` goes through `AddRTPreviewMesh`, which is a bare
geometry flatten: every triangle gets a hardcoded 0.55 gray, no material, no
emission, and no mesh light at all. A mesh light there does not light anything —
raising its intensity 10x changes nothing, and the floor is lit only by the
fallback camera headlight. (The material-aware legacy code, `CollectAllGeometry` /
`MeshLightEmission`, is a different entry point and does honor `is_area_light` and
`light_normalize`; the rtPreview path simply does not use it.)

`next` is the DEFAULT loader and `-legacyLoad` is the opt-out compatibility path,
so this is a known limitation of a fallback rather than a live bug. Rebuilding the
material pipeline inside `AddRTPreviewMesh` would be re-implementing what the next
path already does. Decide before doing it whether the legacy rtPreview path is
worth keeping at all.

### 3. Cross-backend blendshape screenshot parity

Open in the broader visual-parity matrix. The `--next` half is fixed (a
non-instanced blendshaped mesh now morphs; `tusdview-blendshape-morph`), but the
LEGACY Tydra path still renders
`models/blendshape-and-animation-test-001.usda` identically at every time code —
same symptom, different loader, not yet diagnosed.

Note also that the GPU morph path skins the REST normal (only positions are
morphed), so it differs from the CPU bake in SHADING, not geometry. That is by
design on both the instanced and the static path. The two also frame the scene
differently under auto-fit: the GPU path pads the mesh box by `morphExtent` (so a
morphed mesh is never frustum-culled) while the bake's box is exact. So
`tusdview-blendshape-morph` compares them in `--mode depth` through a fixed USD
camera -- geometry only, no shading, no framing. An earlier silhouette-IoU check
was vacuous: the luma>25 mask covered the whole frame, because the background grid
is brighter than that.

That same GPU-deform-vs-CPU-bake comparison now runs over three fixtures the suite
had no coverage for at all (`tests/tusdview/check-deform-parity.py`,
`examples/tusdview/tests/deform-*.usda`): skinning under a rotated/scaled parent,
a morphed mesh split into two batches by material-bind GeomSubsets, and a mesh
that is blendshaped AND skinned at the same time code. The last two found live
bugs (a morph applied in the wrong space, and the CPU bake skinning before it
morphed). Anything else that deforms geometry belongs in that harness.

### 4. usd-assets regression harness

Partial: the batch harness runs both tools over usd-wg/assets. Remaining is
recording and maintaining the per-machine external-asset baselines.

## Dormant by decision (2026-07-11) — do not re-litigate

### Geometry tangents

`FillFlatGeometry` already resolves `m.tangents` through the weld, but
`MeshConfig::compute_tangents` defaults false, so tangents are always empty and
cost nothing.

Do not build without asking. GL, Vulkan and RT all derive the TBN from
screen-space UV derivatives (`vk/shaders/mesh.frag`, `raytrace.comp`), so normal
maps render correctly today. Real tangents only buy correct handedness on
mirrored/seamed UVs, cost ~16 B/vertex against a memory-reduction goal, and would
need per-mesh gating (a second converter pass for meshes whose bound material has
a normal map, i.e. resolving the material *before* conversion), tangent
world-transform in the batch-append path, a new VBO/binding, and shader edits in
`gl/gl_renderer.cc` + `vk/vk_renderer.cc` + `vk/shaders/mesh.{vert,frag}` —
`mesh.frag` being the file the separate sophisticated-texturing branch owns.

## Verification

```bash
cd build && make -j16 && ctest --output-on-failure     # 70/70

# large-scene matrix (see large-scene.md §2.9 for the numbers to hold)
CALDERA=/mnt/disk1/data/caldera/caldera.usda \
ISLAND=/mnt/disk1/data/island/usd/island.usda \
ALAB=/mnt/disk1/data/alab/_merged_ALab/entry.usda \
  bash examples/tusdview/tests/run-large-scene-profiles.sh
```

Notes: tydra-next sources must be listed in BOTH `src/tydra/next/CMakeLists.txt`
and `src/next/CMakeLists.txt` — the latter is the one the build actually uses.
clangd diagnostics in this repo are false positives; trust `make` / ctest.
`build_textools_off/` is NOT gitignored — never `git add -A`.
