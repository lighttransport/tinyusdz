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

FIXED: the DOUBLE deform. `ConvertStageToScene` (scene_loader.cc, the
`std::isfinite(timecode)` branch — i.e. every animated `--time` load) BAKES the pose
into `render.meshes` via `DeformSkinnedMeshes`. The rest-pose load that exists to
stop the shader posing it a second time (`gpuRestLoad`) was gated on an EXPLICIT
`--skinning gpu` — but the default is **Auto**, so the default path baked the pose
and then `updateSkinningEffective` picked GPU anyway, and the vertex shader (or, under
`--rt`, `BuildRtSkinnedMeshVertices`) deformed the already-deformed geometry. A
60-degree bend rendered as 120. Numerically, on `deform-skin-xform` at t=20, rest
vertex 4 = (-1, 3, 1):

    bake -> (-1.799,  1.384, 1.0)   one 60 deg bend about the joint pivot: correct
    RT   -> (-0.799, -0.116, 1.0)   that bend applied to THAT: two bends

`gpuRestLoad` now covers every mode that may deform downstream (`wantsGpuSkinningLoad()`
— Auto as much as GPU); the existing not-eligible fallback re-renders with the CPU
bake, so Auto lands correctly either way. Guarded by `tusdview-legacy-double-deform`
(mesh silhouette in depth through a fixed camera: 0.43 before, 0.96 after).

The earlier confusion, recorded so it is not re-derived: an explicit `--skinning gpu`
run took the rest-load path and looked fine, which made the raster path seem innocent.
It was the DEFAULT that was broken.

### The ~4% residual: it was the BOUNDS, not the deform

Closed. With the double deform gone the legacy paths still disagreed (mesh
silhouette IoU 0.96 raster vs its own CPU bake). Rendering in `--mode geom-normal`
showed the mesh itself was already IDENTICAL (mean 0.12 inside the silhouette) —
the difference was the ground grid and the depth ramp, both of which scale with the
scene bounds.

`PlaceDrawMesh` took a mesh's world box from the 8 corners of its LOCAL AABB pushed
through the world matrix. That is a strict superset — rotate a box and its
axis-aligned hull grows — and it disagreed with `UpdateMeshBoundsFromVertices`,
which re-derives a TIGHT box from the vertices once GPU skinning poses a mesh. So
the same scene got a loose box under CPU skinning and a tight one under GPU, and the
grid and the depth normalization moved between two paths drawing identical geometry.
It now takes the box from the vertices, like everything else does.

Legacy raster and the CPU bake are now BYTE-IDENTICAL (0.000). Legacy RT still
differs by 0.377 — but `--next` RT differs from `--next` raster by 0.401, so that is
the inherent RT-vs-raster edge difference, not a deform bug.

### The bounds, part 2: the two LOADERS did not agree either

Also closed, and it was three separate holes in the same invariant — *the scene box
is the box of the geometry you are actually drawing*. It is not cosmetic: the ground
grid is sized from it, `--mode depth` is normalized by it, and the auto-fit frames
on it, so any of these made two paths render identical geometry differently.

1. `next` built the SCENE box from the 8 corners of each mesh's local bbox pushed
   through its world matrix — the same loose-under-rotation box `PlaceDrawMesh` had
   (its per-BATCH boxes were already tight, from the vertices). It now re-derives
   the scene box from the batches' own vertex boxes after the batches are flushed.
2. `next` never refreshed the box after the deform: the loader uploads REST
   vertices and the pose happens in the shader, so an animated `--time` load framed,
   gridded and depth-normalized against a pose it was not showing.
   `BuildNextPosedSceneBounds` (called from `updateNextDeformFrameIfNeeded`) now
   re-derives it from the POSED vertices — the same ones the RT path uploads, so
   raster and RT cannot drift. Per-MESH boxes are deliberately left alone: a skinned
   batch keeps the conservative whole-scene box so a moving rig cannot cull or LOD
   itself out of the frame.
3. The LEGACY raster bounds pass (`BuildGpuSkinningFrame`) re-posed only SKINNED
   meshes, so a blendshaped mesh kept its rest box. It now morphs a scratch copy
   first — from the half-precision GPU channels, since `dm.morphs` is freed once
   those are built.

4. The INSTANCED PROTOTYPE path (`EmitInstancedProto`) was the one deform emitter
   with no fixture, and was called exempt from the world-bake bugs "by
   construction" — it is, its vertices stay prototype-LOCAL and the placement lives
   in the instance matrix. But that is exactly what the posed-bounds pass got wrong:
   it grew the scene box from those local vertices *without* pushing them through
   each instance's matrix, so a 2-instance scene boxed both copies on top of the
   origin. `deform-instanced-proto.usda` (two instances of one skinned+blendshaped
   external reference, one of them rotated and scaled) went 1.218 → **0.000** against
   the CPU bake. The deform itself was already right (mesh IoU 0.996 before the fix)
   — it was purely the box.

On `deform-skin-xform` (rotated + non-uniformly scaled SkelRoot) at t=20, through a
fixed camera, legacy vs next full-frame mean depth diff: **1.259 → 0.028** (mesh IoU
0.963 → 0.998). Asserted by `check-deform-parity.py`, which now also compares the
two LOADERS (`MAX_LOADER_DIFF`) on every deform fixture, not just next against its
own CPU bake.

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

Refreshed 2026-07-12 for the PointInstancer quaternion fix, and it earned its keep:
it flagged `full_assets/OpenChessSet` (in BOTH modes, which is what a real geometry
change looks like), and the render showed why -- every PAWN was missing. The pawns
are the PointInstancer'd pieces, and the misread orientation flipped them upside
down THROUGH the board, hiding them underneath. Two `intent-vfx` teapot layouts (a
field of instanced teapots, ~800 of them) also shifted, within tolerance: their
per-instance rotations are simply right now. Nothing else in the 280 moved.

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
