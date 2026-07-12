# tusdview — remaining work

Tracked companion to the (local, untracked) `resume-tusdview.md` scratch notes.
This file is the durable list: what is still open, why, and what it touches.

Status as of 2026-07-13. The `--next` primvars / texturing / texture-VRAM
workstream is **done and pushed**, including GPU skinning (raster, both backends,
instanced prototypes included) and the large-scene verification (numbers in
[large-scene.md §2.9](large-scene.md)).

Also done since the last revision of this file, and no longer listed below: the
shared material-eval Phase 2 (sphere-light NEE + MIS), per-texture UV-set
selection, the tusdview BLAS-compaction port, `--vram-budget`, raster LOD for
non-instanced meshes (§2.10), and the `-vk` tiled ray dispatch (§2.11 — which
also records why device-local BVH buffers were measured and left OFF).

Done since (2026-07-12): the CUDA/HIP tracers now take the SHARED deform rather
than their own load-time bake (`poseNextDrawForTracer`, on both the interactive and
the screenshot paths — guarded by `tusdview-deform-cuda`; they still rebuild the BVH
on a new time code rather than refitting it, which is a perf question, not a
correctness one). Also: Vulkan-RT skinning without a reconvert, the two
double-counted light contributions in `lightrt-bsdf`, area-sampled rect / disk /
cylinder lights (plus the inverted UsdLux light normal that made them light
nothing), the TLAS `PREFER_FAST_BUILD` question (measured, rejected —
large-scene.md §2.12), and the `lightrt-bsdf` IBL energy (the furnace now lands at
1.00x the dome that lights it, from 2.00x).

Done since (2026-07-13), all committed, none pushed: the proxy/render purpose
supersede (both loaders), and a family of **silent data-loss bugs in the writers**
found by sweeping fixtures through `tusdcat` — see [Open](#open) for what is left
of that sweep, and [Prompts for a fresh session](#prompts-for-a-fresh-session) to
pick it up cold.

## Prompts for a fresh session

Paste one of these verbatim. Each is self-contained: it says what is broken, how
to reproduce it, and how to know when it is fixed. Read the section it points at
before starting — the reasoning there is the part that is expensive to re-derive.

**1. Finish the crate-writer round-trip sweep** (the biggest known correctness
hole; see [The crate writer drops data](#the-crate-writer-drops-data-133-of-422-fixtures)):

> The tinyusdz crate (.usdc) writer silently drops authored data. Reproduce with
> the sweep in doc/resume-tusdview.md ("The crate writer drops data"): 133 of 422
> `tests/usda` fixtures do not survive `usda -> usdc -> usda` intact. Known
> categories: a TYPELESS prim (`def "bora"`) comes back as `def Model "bora"`
> (the writer invents a typeName); Camera `shutter:open` / `shutter:close` are
> dropped; `hidden` metadata, `bindMaterialAs` and some `material:binding`
> relationships are dropped. Fix them one category at a time, smallest first.
> Each fix must come with a mutation-verified assertion in
> `tests/run-scope-imageable-roundtrip.sh` (revert the fix, watch the test fail),
> and must not regress the 87-test suite or raise the fixture count. Do not
> "fix" a diff by making the printer match the writer — the printer is already a
> fixed point (all 422 fixtures re-print identically); the writer is what is
> wrong.

**2. Get the pxr reference comparison running** (this whole workstream is
currently self-referential — see the caveat in that section):

> `tests/run-usdcat-compare.sh` diffs tusdcat against pxr's usdcat, but pxr is not
> installed at the path it expects (`/home/syoyo/local/USD/dist/bin/usdcat`), so
> it cannot run here. Every round-trip claim we have verifies only that tinyusdz's
> own reader and writer agree with EACH OTHER — a bug they share is invisible.
> Install/point at a pxr build and run the comparison over `tests/usda`, then
> triage what it finds.

**3. Mesh lights on the legacy `-rtPreview` path** — see the section below; decide
whether that path is worth keeping before building anything.

## Open

### The crate writer drops data (133 of 422 fixtures)

`.usdc` is not a faithful round-trip today. Sweep, from the repo root:

```bash
for f in tests/usda/*.usda; do
  a=$(mktemp); c=$(mktemp --suffix=.usdc); d=$(mktemp)
  ./build/tusdcat "$f" > "$a" 2>/dev/null || continue
  ./build/tusdcat --output-format usdc -o "$c" "$f" >/dev/null 2>&1
  ./build/tusdcat "$c" > "$d" 2>/dev/null
  cmp -s "$a" "$d" || echo "DIFF $f"
  rm -f "$a" "$c" "$d"
done | wc -l          # 133 as of 2026-07-13 (was 140)
```

The USDA printer is a FIXED POINT — all 422 fixtures re-print identically — so a
diff here is the crate writer losing data, not the printer being creative. Known
remaining categories, in rough order of how much they matter:

- a TYPELESS prim (`def "bora"`, 49 lines) comes back as `def Model "bora"`: the
  writer invents a typeName where USD has none.
- Camera `shutter:open` / `shutter:close` are never written.
- `hidden` metadata, `bindMaterialAs`, and some `material:binding` relationships.

**Why this kept happening:** the extraction was copy-pasted per prim type, so each
new prim type was one omission away from losing data — and several did. Three
rounds of this are already fixed (below); prefer a shared helper over another copy.

CAVEAT on all of it: the checks verify that tinyusdz's reader and writer agree
with each other. A bug they SHARE is invisible to them. The pxr comparison
(`tests/run-usdcat-compare.sh`) is what would catch that, and it cannot run here —
pxr is not installed at the path it wants.

### Mesh lights on the LEGACY `-rtPreview` path

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

## Closed — kept for the reasoning, not because they are open

The deform/bounds work below is DONE and guarded by tests. It stays written down
because each item cost a wrong turn that is easy to re-take.

### `proxy` and `render` are ALTERNATIVES, not two things to draw

tusdview drew both, so the stand-in landed on top of the geometry it stands in for.
It stayed invisible until upstream FIXED `Sphere.radius` (2 -> USD's default 1):
intent-vfx's `simpleAsset` authors a bare Cube as the proxy for a bare Sphere, and
with the radius correct that cube (size 2) exactly encloses that sphere (radius 1),
so the asset rendered as a blank box. The lesson is the diagnosis, not the fix: the
"regression" was an upstream CORRECTION exposing a latent bug of ours, and
restoring the old radius would have "fixed" the picture by keeping a bug.

A proxy is now superseded by render-purpose geometry (both loaders); one that
stands in for nothing still draws. The supersede is scoped to the MODEL root —
nearest ancestor with an authored kind other than `group` — because the two are
alternatives of the same ASSET. Scoping to any shared ancestor instead DELETES
real geometry, which the usd-assets goldens caught: Apple's
`stage_composition/purpose.usda` sits four unrelated cubes under one Scope, one of
them proxy-purpose, and a shared-ancestor rule dropped it. Guarded by
`tusdview-purpose-proxy-supersede` (all three cases, both loaders).

### The writers silently dropped authored data

Three rounds, each found by sweeping fixtures through `tusdcat` rather than by
reading code. All fixed and guarded by `scope-imageable-roundtrip`, which now
sweeps ALL 26 imageable prim types through both `usda -> usda` and
`usda -> usdc -> usda`:

1. **Scope**: `visibility` was parsed into a typed field and then never printed or
   written — it vanished on round-trip. `purpose` was never parsed into its typed
   field at all (`Scope::purpose` was dead; it read back as Default forever) and
   survived only by accident, by falling through into the generic `props` map.
2. **Lights, Volume, Material, NodeGraph**: the lights and Volume lost BOTH
   attributes through the crate writer; Material/NodeGraph lost `purpose` through
   both writers. Volume's was the silliest — it DOES call
   `ExtractGPrimProperties`, but was missing from that function's cast list, so it
   found no gprim and wrote nothing.
3. **Light transforms** (the worst): NO light extractor wrote xformOps. Lights are
   Xformable but were missing from `ExtractXformOpsFromXformable`'s cast list, so
   a scene written to `.usdc` came back with **every light at the world origin**.
   `SkelRoot`, `Skeleton` and `Volume` were missing there too. Also: `inputs:exposure`
   was copy-pasted per light and five of them forgot it; RectLight never wrote its
   texture; neither DomeLight wrote `inputs:texture:format`.

Also: an AUTHORED opinion equal to the schema fallback (`visibility = "inherited"`)
is now written. It is not the same as no opinion — an authored opinion blocks
weaker ones during composition, so dropping it because it "looks like the default"
silently changes what the layer means.

### LEGACY loader deform: a DOUBLE deform on the `--time` load path

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

### A PointInstancer's orientations are REAL-FIRST

The next stage stores quats real-first (w, x, y, z) — crate is imaginary-first on
disk and the reader swizzles on load. `InstanceTRS` read the four floats of
`orientations` as (x, y, z, w), in BOTH the tusdview next loader and tusdrender, so
a 30-degree Z rotation decoded as a ~150-degree X rotation and the identity fallback
(0,0,0,1) was a 180-degree flip. In usd-wg/assets' OpenChessSet that flipped every
PointInstancer'd PAWN through the board, where it rendered hidden underneath.

Worth remembering HOW it surfaced: `next` was self-consistent across raster, RT and
CUDA (all 0.000 against its own CPU bake — they share the placement code, so they
were wrong together). Only the cross-LOADER check caught it. A backend-vs-backend
comparison cannot see a bug that lives upstream of all the backends.

The rest of the class was audited and is clean: `xformOp:orient` (scene-access.cc)
reads w,x,y,z, the next loader's SkelAnimation rotations are real-first,
`PackSnorm8Quaternion`'s identity is {1,0,0,0}, and render-data.hh documents
`orientations` as "quatf real,imaginary xyz". Guarded by
`tool-tusdrender-instancer-orientation` (an equivalence: a quaternion-oriented
instance must render byte-identically to the same rotation as an xformOp) and
`tusdview-deform-pointinstancer`.

## Tooling

### usd-assets regression harness — the baseline is per-machine

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

Refreshed again 2026-07-13, for upstream's `Sphere.radius` correction and the
proxy/render supersede. It earned its keep a SECOND time: it flagged
`stage_composition/purpose.usda`, and that flag was the supersede rule being too
broad — it was deleting a proxy cube that nothing superseded. Read the diff; a
fingerprint that moves on an asset your change should not have touched is the
point of the thing. (`normalsTypes` under vk-rt moves ~11 bits, over the tol of 8:
a receding row of small cubes makes a thin, framing-sensitive silhouette. Verified
by rendering it — geometry is correct.)

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
cd build && make -j16 && ctest --output-on-failure     # 87/87

# The GL/Vulkan tests need a display; headless, on NVIDIA:
# xvfb-run -a env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
#   ctest --output-on-failure

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
