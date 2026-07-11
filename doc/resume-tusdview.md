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

### 3. LEGACY loader deform: a DOUBLE deform on the `--time` load path

The old note here — "the legacy path renders
`models/blendshape-and-animation-test-001.usda` identically at every time code" —
is STALE. It morphs, and it animates. What it does not do is agree with itself.

`--camera` now works on the legacy loader too (`FindLegacyCamera`; it used to warn
"need --next" and auto-fit). That was the blocker: without a camera both loaders
could agree on, the two could not be compared at all — auto-fit framing swamps
every pixel difference.

With that, comparing in `--mode depth` through one camera (the deform-parity
harness, on `examples/tusdview/tests/deform-*.usda`):

- at REST the legacy and next loaders agree exactly (mean depth diff 0.02);
- at a posed time code they diverge, and legacy disagrees with ITSELF: its
  `--skinning cpu` bake and its GPU/RT paths land in different places (mean 3.1 on
  `deform-skin-xform`, where the whole deform only moves depth by 3.5).

The proven part. `ConvertStageToScene` (scene_loader.cc, the `std::isfinite(timecode)`
branch — i.e. every `--time` load) calls `DeformSkinnedMeshes`, which BAKES the pose
into `render.meshes`. `BuildRtSkinnedMeshVertices` then skins those already-baked
vertices AGAIN, every frame. Numerically, on `deform-skin-xform` at t=20, rest
vertex 4 = (-1, 3, 1):

    bake  -> (-1.799,  1.384, 1.0)   # one 60 deg bend about the joint pivot: correct
    RT    -> (-0.799, -0.116, 1.0)   # the same bend applied to THAT: two bends

Not yet explained, and the reason this is still open: with an IDENTITY SkelRoot the
raster GPU path matches the CPU bake EXACTLY (0.000), while RT still double-deforms
— so the raster shader is somehow not double-applying, even though it is fed the
same baked vertices and the same (correct, SkelRoot-independent) bone matrices. Only
`dm.world` differs between the two fixtures. Resolve that before fixing: the shape of
the fix (keep the rest pose for the paths that deform downstream, vs. drop the
load-time bake) depends on it.

The `--next` loader is self-consistent here (GPU deform == CPU bake, byte-identical
in depth, on all three deform fixtures), so it is the reference.

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

### 4. usd-assets regression harness — DONE, but the baseline is per-machine

`tusdview-usd-assets-golden` compares every asset in the corpus against
`examples/tusdview/tests/usd-assets-goldens.tsv` (280 assets, 255 of which render
in each of vk-raster and vk-rt; the other 25 are load errors and carry no
fingerprint). The batch harness could always RENDER the corpus — what it had no
answer for was "did this change what any of it looks like?", so a render that
silently turned a scene black still passed.

The fingerprint is a COVERAGE bitfield (a silhouette), not a pixel hash: it
survives the GPU/driver lighting differences that would make a checked-in hash
useless anywhere but the machine that recorded it. Geometry, composition and
material-binding regressions move the silhouette; a shading nudge does not.

Opt-in twice over — it needs the corpus (`USD_ASSETS_ROOT`) and
`TUSDVIEW_RUN_GOLDEN=1`, and SKIPs without either. ~11 min on an RTX 5060 Ti.
Refresh deliberately, and READ THE DIFF before committing it:

```bash
TUSDVIEW_RUN_GOLDEN=1 tests/tusdview/run-usd-assets-batch.sh \
  --golden-kind coverage \
  --update-golden examples/tusdview/tests/usd-assets-goldens.tsv
```

Recorded 2026-07-12 on an RTX 5060 Ti (driver 610.43.02). A different GPU will
likely need its own baseline; that is why the gate exists.

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
