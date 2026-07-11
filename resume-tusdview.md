# tusdview Remaining Work

> **START HERE if resuming:** see
> [§0 Current session](#0-current-session-2026-07-11--next-primvars--texturing--texture-vram)
> and the [fresh-session resume prompt](#fresh-session-resume-prompt-current-work)
> at the bottom. Sections §1-§9 below are the OLDER material-eval workstream and
> are not what is in flight right now.

## 0. Current session (2026-07-11): `--next` primvars / texturing / texture VRAM

Branch `tusdview`, on top of the merge `68d008abe`. **All of this is
UNCOMMITTED** in the worktree (alongside the pre-existing large-scene WIP).

Goal: the large-scene WIP bounded *geometry* only. Two gaps made `--next` render
large scenes as untextured/flat-shaded soup, and left texture VRAM unbounded.

### Done + verified

- **Primvar resolver + weld** (`examples/tusdview/next_scene_loader.cc`).
  `FillFlatGeometry` used to accept normals/`st` **only** at `Interpolation::Vertex`,
  so the faceVarying data that production USD overwhelmingly authors was silently
  dropped (UV = (0,0), `geometricNormal = true`). Rewrote it as a `NextAttr`
  resolver covering all five interpolations via
  `RenderMesh::triangulated_face_vertex_indices`, **welding** corners into unique
  vertices (splitting only at real UV/normal seams — naive per-corner expansion
  would ~4x a quad mesh's vertex count). Also fixed the **missing V-flip** (the
  legacy path flips; `--next` did not). Emits `vertexToPoint`, threaded through
  `BakeSkinning`, `BakeBlendShapes`, `BuildMorphChannelsNext` and the wireframe
  builder — all of which previously hard-assumed `vertex i == point i`.
  *Verified:* `models/cube-mtlx-texture.usda` (faceVarying normals + `st` +
  texture) now renders **byte-identical to the legacy path** in both `--mode uv`
  and shaded. Weld log: 24 verts from 8 points on a cube = correct minimum.
- **Shared texture decoder** — new `src/tydra/next/texture-cache.{hh,cc}`
  (`tydra::next::TextureDecoder`): filesystem/`.usdz` resolve, 16→8-bit narrow,
  RGBA8 (or native channels via `force_rgba=false`), **decode-time** longest-edge
  cap + running byte budget (`sqrt` shrink), `ReplaceUdimToken`. Capping at decode
  bounds *peak RAM*, not just final VRAM. Replaces the two hand-copied decoders in
  `next_scene_loader.cc` and `tusdr_next.cc` (dead `UsdzEntryMatches` /
  `ReplaceUdimToken` deleted from the latter).
  **Registered in BOTH `src/tydra/next/CMakeLists.txt` and
  `src/next/CMakeLists.txt`** — the latter is the list the build actually uses;
  forgetting it gives an undefined-reference link error.
- **Budget-derived texture caps** — `TextureBudget` + `DeriveTextureBudget` in
  `src/tydra/next/resource-budget.hh` (first-ever consumer of the already-computed
  `gpu_texture_limit`). Wired into tusdview (`main.cc` large-scene profile) and
  tusdrender (`ApplyLargeSceneProfile`). Explicit `--texture-max-size` /
  `--texture-budget-mb` (tusdrender: `-texMaxSize` / `-texBudgetMb`) still win.
  *Verified:* a 2.2 MB texture → 16 KB under `--texture-max-size 64`, and → exactly
  1.0 MB under `--texture-budget-mb 1`; uncapped render stays byte-identical to legacy.
- **UV set naming** — `MeshConfig::default_uv_primvar` replaced by an ordered
  chain `uv_primvar_names = {st, UVMap, uv, st0, map1}`; the winning name is
  reported back on new `RenderMesh::texcoords_0_name` / `texcoords_1_name`.
  Previously a Blender-exported `UVMap` mesh read as having **no UVs at all**.
  tusdrender's ad-hoc `st`-then-`UVMap` fallback now uses the same chain.
- **`displayOpacity`** — tydra-next parks it in the generic primvar bag (it is not
  a builtin). A mesh's opacity is folded into an **alpha-adjusted material variant**
  (`materialWithAlpha`, cloned once per distinct opacity; mutating the shared
  material in place would be wrong when two meshes with different opacities share
  one). Genuinely per-vertex opacity is carried on the new
  `DrawMeshCPU::vertexAlpha` and reported via a warning (no renderer samples a
  per-vertex alpha attribute yet). *Verified* on a generated 2-quad scene: the
  0.35-opacity quad blends, the un-opacity'd control does not.

### Done + verified (2026-07-11, later session)

- **UsdShade material-binding resolution (BUG, fixed).** Verification of the
  large-scene matrix turned up ALab loading with **23 materials / 0 textures**
  (i.e. still the untextured soup §0 set out to kill). Root cause was *not* the
  primvar/texture work: `next_scene_loader.cc`'s `resolveMeshMaterial` read only
  the Mesh's **own plain `material:binding`**. Production scenes (ALab) bind
  **purpose-scoped** (`material:binding:preview` / `:full`, no plain binding) and
  on an **ancestor** Xform, so every mesh missed its binding and fell back to the
  default gray material, dropping every texture with it. Both halves failed
  independently (a plain binding on an ancestor *and* a purpose-scoped binding on
  the mesh each resolved to nothing).
  Fix: new **`tinyusdz::next::GetInheritedBoundMaterialPath`**
  (`src/next/schema/usd-shade.{hh,cc}`) — purpose fallback chain **plus** the
  ancestor walk, honoring `bindMaterialAs="strongerThanDescendants"`. It is now
  the single source of truth: tydra-next's `FindInheritedMaterialBinding`
  delegates to it (its duplicate `BindingIsStrongerThanDescendants` deleted),
  tusdview's loader uses it, and the **three** hand-rolled naive lookups in
  `tools/tusdrender/tusdr_next.cc` (lines ~953 / ~1388 / ~1621) now use it too —
  tusdrender had the identical bug. GeomSubsets use the purpose chain without the
  ancestor walk (a subset that binds nothing must keep falling back to the
  whole-mesh material).
  *Verified:* ALab full shot **23 -> 394 materials**; the single asset
  `tool_screw01` 1 -> 3 materials. Large-scene matrix unchanged (Island 1.52 s /
  462 MB, ALab 1.87 s / 527 MB, Caldera 8.30 s / 2.11 GB) — the ancestor walk
  costs nothing measurable. New checked-in regression
  **`tusdview-material-binding-inheritance`** (fixture
  `models/tusdview-material-binding-inheritance.usda` + `run-material-binding-
  inheritance.sh`): asserts a texture is resolved AND actually sampled (a flat
  render fails). Confirmed it FAILS on the pre-fix code and passes after.

- **Layer-relative asset-path anchoring (BUG, fixed).** With the binding bug fixed
  ALab resolved 394 materials but **still 0 textures**.
  `RenderSceneConverter::ResolveAssetPath` anchored *every* relative asset path to
  one `config_.asset_base_dir` (the stage root), but USD anchors an asset path to
  **the layer that authored it**. ALab's textures live in payload layers several
  directories deep (`asset inputs:file = @../../texture/....jpg@`), so anchoring at
  the stage root produced a nonexistent path and the texture was silently dropped.
  The eager path already solved this with a per-prim `current_working_path` stamp;
  tinyusdz-next had no source-layer provenance at all (`next::Layer` has no
  identifier).
  Fix (per-prim anchor id, mirroring eager's cwp — the option the user chose):
  new **`src/next/layer/asset-anchor.{hh,cc}`** interns each layer's directory
  (thread-safe; `deque` so handed-out refs survive concurrent interning; anchors
  are **absolutized** on intern — see the gotcha below). `PrimSpec` gains a
  `uint32_t asset_anchor_id` (in the padding after `specifier_`, so it costs 0
  bytes/prim). `LoadLayerFromFile` stamps every prim of a freshly-loaded layer
  (USDZ package entries deliberately excluded — they resolve inside the archive);
  `ComposeOpinions` carries it through composition (strong->weak, so the strongest
  source with an anchor wins). `ResolveAssetPath(file, anchor_id)` now joins +
  normalizes against the authoring layer, falling back to `asset_base_dir` for
  unanchored prims (root layer, USDZ) — so unanchored behavior is byte-identical.
  Also fixed the consumer side in BOTH tools: `LoadNextTexture` (tusdview) and
  tusdrender's `tex.asset_path` path preferred the **raw authored** string over
  `images[].resolved_path`, throwing the anchored path away; both now prefer the
  resolved one. tusdrender's hand-rolled resolver reads `inputs:file` straight off
  the shader prim, so its 5 `LoadTextureCached` sites go through a new
  `AnchorAssetNext(prim, path)`.
  **Gotcha worth remembering:** the anchor MUST be absolutized at intern time. A
  CWD-relative anchor works when the scene is given by absolute path but breaks on
  a relative one, because tusdview's texture decoder re-anchors relative paths
  against the *scene file's* directory and double-prefixes them. The checked-in
  fixture caught this; an absolute-path-only test would have shipped the bug.
  *Verified:* ALab full shot **394 materials, 343 textures** (was 23/0 at session
  start), `next: textures 343, decoded 1372.0 MB (cap 2048 px, budget 1920 MB, 0
  downscaled)` — this is also the first time the §0 texture-budget/decode-cap
  machinery has been exercised on a real production scene (the deferred-payload
  matrix decodes 0 textures). Single ALab asset `tool_screw01` goes from a flat
  184-colour render to 4168 colours. New checked-in regression
  **`tusdview-asset-path-anchoring`** (fixture `models/nested-look-texture/`, a
  look layer in `look/` reaching `../tex/checkerboard.png`). ctest 41/42 (only the
  known skinning failure). Large-scene matrix unchanged: Island 1.47 s / 462 MB,
  ALab 1.98 s / 528 MB, Caldera 8.42 s / 2.11 GB.

- **tusdrender LEGACY path had no texture sampling at all (BUG, fixed).** Chasing
  "tusdrender renders everything gray" found a third, unrelated bug. Routing
  (`tusdrender.cc:837`): only `.usdc`, `-rtPreview` and the GPU backends go through
  the `next` path — **every `.usda`/`.usdz` falls through to the LEGACY eager tydra
  loader**. That loader resolved and *decoded* each UsdUVTexture into
  `RenderScene::{textures,images,buffers}` and then **threw it away**:
  `tusdr_material.cc MaterialColor()` returned only `ShaderParam::value` (the
  constant fallback) and never checked `is_texture()`, `AddMeshTriangles` emitted no
  UVs, every `TriInfo::tex_id` stayed -1, and the render call passed
  `/*textures*/ nullptr, /*tri_uvs*/ nullptr`. Every `tex_id` assignment in the tool
  lived in `tusdr_next.cc`. Result: a photo-textured quad rendered a *uniform gray*.
  Fix: new `BuildLegacyTextures()` (`tusdr_legacy.cc`) decodes tydra's images
  (UInt8/UInt16/Float) into renderer `Texture`s — reusing the viewer's proven
  decoder — binds them per material (diffuse/emissive/normal/roughness/metallic/
  occlusion/opacity, with the scalar source channel from
  `UVTexture::connectedOutputChannel`), and `AddMeshTriangles` now emits `tri_uvs`
  (6 floats/tri, raw USD UVs — `Texture::sample()` does the v-flip). A textured
  channel's constant becomes white so the texel is not darkened by the 0.18 fallback.
  *Verified:* `models/texture-cat-plane.usda` goes from **saturation 0 / 25 colours
  (pure gray) to saturation 131 / 7645 colours**. New checked-in regression
  **`tool-tusdrender-legacy-texture`** (`check_legacy_texture.py`, native PNG decode,
  no Pillow); confirmed it FAILS on the pre-fix code with exactly
  "render is grayscale (max saturation 0)".
- **Legacy tydra dropped purpose-scoped material bindings (BUG, fixed).** Same class
  as the `next` bug above, in the *other* resolver: `shader-network.cc
  GetBoundMaterial()` already walked ancestors correctly, but with no purpose
  requested it tried **only** the all-purpose `material:binding`. Scenes that bind
  purpose-scoped (ALab) resolved to no material at all → default gray, texture and
  all. Now uses the same chain as next: `preview` -> all-purpose -> `full`.
  *Verified:* purpose-scoped and ancestor-bound variants all render textured through
  tusdrender; covered by the new ctest. Suite **42/43** (only the known skinning
  failure).

- **tusdrender's LEGACY path did not COMPOSE (BUG, fixed).** `tusdrender.cc:1271`
  loaded via `tinyusdz::LoadUSDFromFile`, which parses a **single layer and expands
  no composition arcs** — there was no `ComposeToFixedPoint` (tusdview's legacy
  loader has one, `scene_loader.cc:53`). Any Material or geometry contributed by a
  reference / payload / sublayer / inherit / variant simply did not exist: a look
  layer's Material was missing (mesh → default gray, textures gone) and
  payload-gated geometry rendered as "no renderable geometry".
  Fix: new `LoadStageComposedLegacy()` (`tusdr_legacy.cc`) — load the root Layer,
  `CompositeSublayers`, then LIVRPS to a fixed point (references → payloads →
  inherits → variants → specializes), then `LayerToStage`. Mirrors the viewer's
  loader, minus its deferred-payload/whitelist machinery (tusdrender's legacy path
  is eager). Variants are resolved only once refs+payloads settle (AOUSD 10.3.2.5) —
  a variant's content usually arrives THROUGH an arc. `.usdz` and arc-free layers
  keep the direct parser path (zero-copy USDC + the schemas `LayerToStage` drops).
  `allow_parent_relative_paths = true` on all three option structs, matching
  tusdview's default and next's resolver — real layer stacks reach sibling asset
  dirs with `..`, and rejecting them fails composition outright.
- **Two core asset-resolution bugs found underneath it (fixed).** With composition
  on, ALab composed but every texture still failed to resolve:
  - `SanitizeAssetPath` (`src/tydra/common-utils.cc`) splits a path into segments
    and rejoins them **without preserving the leading slash**, so an ABSOLUTE
    `/mnt/...` came back as a RELATIVE `mnt/...` and then resolved against the wrong
    base. Now preserves the root (`/`, and `//` for UNC).
  - `io::FindFile` (`src/io-util.cc`) only tried a path as-is when `search_paths` was
    EMPTY; otherwise it only tried `JoinPath(dir, path)`, which for an absolute path
    yields `<dir>//abs/path` — guaranteed to miss. So an absolute asset path failed
    to resolve whenever a search path / working dir was set. Now an absolute path is
    tried literally first. (Strictly additive: a joined absolute path never existed.)
  Both are library-wide, not tusdrender-local.
  *Verified:* ALab's `tool_screw01` through tusdrender goes from **"no renderable
  geometry" / blank** to a **textured render** (1654 colours, saturation 101, 0
  texture-load failures). The `nested-look-texture` fixture (Material reachable only
  through a reference) renders textured. ctest **42/43**; tusdview's large-scene
  matrix unchanged (Island 1.19 s / 462 MB, ALab 1.58 s / 528 MB, Caldera 6.82 s /
  2.11 GB). `tool-tusdrender-legacy-texture` now asserts all three legacy bugs
  (texture sampling, purpose-scoped binding, composed-through-a-reference material);
  each assertion was confirmed to FAIL on the corresponding pre-fix build.

- **`-autoframe` framed a FLAT scene edge-on -> black render (BUG, fixed).**
  `MakeUsdRecordCamera` (`tusdr_next.cc`) picks a fixed axis-aligned view per up
  axis — a Z-up scene is framed from **-Y**. A flat scene (a single quad, a ground
  plane, a card) has a near-zero extent along one axis, and when that axis is not
  the one being looked down, the camera looks **along** the plane: exactly edge-on,
  so the render is black. `texture-cat-plane.usda` (a Z-up quad lying in the XY
  plane, bounds `lo=(-1,-1,0) hi=(1,1,0)`) rendered pure black with
  `-rtPreview -autoframe` but correctly without it.
  Fix: detect the degenerate axis (extent <= 1e-4 of the largest) and, when it is
  not already the view axis, look **down that axis** so the scene is framed
  face-on; the two remaining extents become the focal-plane half-extents, and the
  scene's up axis supplies the roll (falling back to an in-plane axis for a card
  standing edge-up). Solid models are untouched by construction — no zero extent
  means the branch never fires.
  *Verified:* the flat scene goes from **0% frame coverage (black) to 68.6%**, and
  matches the `.usdc` (next-path) control that already framed correctly. New
  checked-in regression **`tool-tusdrender-autoframe-flat`**, which also asserts a
  solid model still frames (suzanne 26.9%, cube 68.6%); confirmed it FAILS on the
  pre-fix framing with "0.0% of the frame is covered". ctest **43/44**.
  (Not a bug, noted while checking: `texturedcube` under `-autoframe` renders a
  single flat color — that is a correct head-on view of one axis-aligned cube face,
  covering 69% of the frame, not a black render.)

### Not done — DECIDED: leave dormant

- **Phase 6, geometry tangents ("only where needed"). — user decided 2026-07-11:
  LEAVE DORMANT, do not build.** Rationale below stands; do not re-litigate. `FillFlatGeometry`
  already resolves `m.tangents` through the weld, but it is **dormant**:
  `MeshConfig::compute_tangents` defaults false, so tangents are always empty and
  cost nothing. Enabling them needs, end to end: per-mesh gating (a 2nd converter
  with `compute_tangents=true` for meshes whose bound material has a normal map,
  which also means resolving the material *before* conversion), **world-transforming
  tangents in the batch-append path**, a new VBO/binding, and shader changes in
  `gl/gl_renderer.cc` + `vk/vk_renderer.cc` + `vk/shaders/mesh.{vert,frag}`.
  **Key finding:** GL, VK and RT all already build the TBN from screen-space UV
  derivatives (`vk/shaders/mesh.frag:158-160`, `raytrace.comp:464`), so **normal
  maps render correctly now that UVs are fixed** — geometry tangents only buy
  correct handedness on mirrored/seamed UVs, at ~16 B/vertex, and they touch
  `mesh.frag`, i.e. exactly the file the sophisticated-texturing branch owns.
  Producing tangents nothing consumes would be pure waste against the memory goal,
  so this was deliberately left off pending the user's call.
- **Per-texture UV-set selection.** `RenderTexture::uv_primvar` naming the
  *secondary* set cannot be routed today: `uv1` exists only as a debug AOV (render
  mode 31), and sampling it would need a `uvSet` field on `DrawTexSampleCPU` plus
  material-buffer + shader changes across GL/VK/RT. The fallback chain above
  already covers the common single-UV-set case.

### `--next` GPU skinning gap — FIXED (2026-07-11)

`tusdview-skinning-screenshot-diff` used to fail (`skinning: requested GPU, using
CPU (scene has no skeletal skinning or blendshapes)`): the default loader is
`--next` (`main.cc:182`) and that path never filled GPU skin weights — it
CPU-baked a static pose via `BakeSkinning`. Now **44/44**, and the `--next` CPU
and GPU renders of `skintest-animated.usda` are PIXEL-IDENTICAL (0 differing
pixels), for both the `.usda` and a crate (`.usdc`) round-trip of it.

Viewer side:
- `BakeSkinning` split into a pose-independent `ResolveNextSkinBinding` (bound
  skeleton/animation, geomBind, per-vertex influences) + `PoseNextSkeleton`
  (skeleton posed at `t`). The CPU bake and the new GPU path share both.
- `SetupGpuSkinNext` emits the 4 strongest influences per vertex + a bone-matrix
  block; `BuildNextSkinningFrame` re-poses from the retained `nextSession_` stage
  each frame (mirrors `BuildNextMorphWeights`). `LoadOptions::gpuSkinning`
  (`App::wantsNextGpuSkinning`) picks GPU attributes vs the CPU bake — the ray
  tracers keep the bake, since they read the baked DrawScene geometry.
- The next loader world-bakes vertices into per-material batches, so **the world
  transform and geomBind are folded into the bone rows** (`invW · G · M · G⁻¹ · W`)
  and joint indices are ABSOLUTE. One batch can therefore hold several skinned
  meshes plus unskinned ones (the shader skips vertices with `wsum == 0`). The
  frame is packed straight from `DrawScene::nextSkels`, NOT by walking
  `draw->meshes`: the next path frees per-mesh CPU geometry after GPU upload, so
  the vertex attributes are no longer resident (packing from them uploaded an
  all-zero bone texture, which collapsed the mesh).
- The CPU bake now skins NORMALS with the same blended matrix the shader uses
  instead of regenerating a smooth normal field from the posed positions — the
  two disagree wherever the pose bends the surface.

**Three `next` UsdSkel library bugs found underneath** (`src/next/schema/usd-skel.cc`),
each of which silently degraded every skinned next scene to a rest/identity pose:
1. `GetSkelAnimationData` read the SkelAnimation's `joints` (a `token[]` ARRAY)
   with the SCALAR `as_string()`/`as_token()` accessors → always empty → the whole
   animation was dropped and every joint kept its rest transform.
2. Joint `rotations` were unpacked as `(x,y,z,w)`. next's canonical quat layout is
   **REAL-FIRST `(w,x,y,z)`** — the crate reader swizzles disk's imaginary-first
   order into it (`CrateReader::Impl::UnpackQuatf`), and ASCII parses in authored
   order. The old mapping built a garbage quaternion.
3. `GetSkeletonData` read `bindTransforms`/`restTransforms` only under the
   `primvars:skel:` prefix (that prefix belongs on the skinned MESH, not the
   Skeleton) and as FLOAT arrays though they are `matrix4d[]` (doubles) → both
   empty → identity bind pose, skewing every skinning matrix. Suspect these
   whenever a next skeleton "poses but looks wrong".

Not covered (pre-existing, unchanged): RT / CUDA / HIP keep the load-time CPU bake.

### `--next` INSTANCED-prototype skinning — FIXED (2026-07-11)

A skinned rig behind `instanceable = true` was converted but never skinned, so
every instance drew the REST pose. New ctest `tusdview-instanced-prototype-skinning`
(fixture `models/skintest-instanced.usda`, two native instances of the animated
rig) runs on **GL and Vulkan** and asserts: GPU skinning engages, BOTH instances
survive, the rig is ON SCREEN, and it ANIMATES. It fails on the old behavior
("scene has no skeletal skinning").

### Regression tests added for the bugs found (2026-07-11)

- **Core** (`tests/unit/unit-next-usdskel.cc`, ctest `unit-test-next`): the three
  `next` UsdSkel reader bugs, each verified to FAIL against the pre-fix code --
  plain `bindTransforms`/`restTransforms` as matrix4d[] doubles; SkelAnimation
  `joints` as a token[] ARRAY; quat rotations REAL-FIRST (identity reads
  (1,0,0,0), not (0,0,0,1)). Hermetic: composes a stage from an in-memory USDA.
  NOTE: this is a SEPARATE executable on purpose. Linking `tinyusdz_next` into
  `unit-test-tinyusdz` (which links `tinyusdz_static`) made that binary's
  concurrency tests fail intermittently -- the two static libs carry overlapping
  objects. Do not merge them.
- **Viewer** (`tusdview-instanced-prototype-skinning`): native instancing (both
  instances present), degenerate instanced bounds (rig on screen), and instanced
  skinning (animates), on both backends.

- Prototypes now skin: GPU (`SetupGpuSkinNext` with an IDENTITY world) or, when
  GPU skinning is off, a static `BakeSkinning` at `time` (they used to get
  neither).
- Skinned prototypes STAY INSTANCED: both flat instanced shaders now carry a bone
  path, so there is no de-instancing and no instance cap. All instances of a
  prototype share ONE bone block, which is sound because USD instancing requires
  identical composed contents (hence one skeleton, one pose), and the bones are
  PROTOTYPE-LOCAL (identity world) -- the shader applies each instance's o2w after
  skinning.
  - **GL** (`gl_renderer.cc`): joints/weights at attribs **6/7**, since 3/4/5 carry
    the per-instance o2w rows (the mesh program's skin slots). Bone texture shared
    on unit 4. The >4-influence extended stream needs attrib 5 and so has no
    instanced form: instanced prototypes use the 4-influence path only.
  - **VK** (`mesh_inst.vert`): joints/weights arrive BY DEVICE ADDRESS through the
    per-draw DrawMeta SSBO (set 6), not as vertex attributes -- the merged
    multi-draw-indirect path could not supply per-mesh vertex bindings. Skinned
    prototypes are excluded from MDI (its gl_VertexIndex indexes the MERGED
    buffer); MDI draws carry jointAddr == 0 and skip skinning. DrawMeta's
    descriptor needed `VERTEX_BIT` added -- it was fragment-only.
  - Regenerate SPIR-V after touching a VK shader:
    `bash examples/tusdview/vk/shaders/build-shaders.sh` (glslang is vendored under
    `examples/common/glslang/bin`), then commit `vk/shaders/embedded/*.spv.h`.

**Instanced-prototype scene bounds** (found via Vulkan, which rendered an EMPTY
frame while GL happened to still show the rig): `EmitInstancedProto` added only
each instance's TRANSLATION to the scene bounds, never the prototype's extent, so
a 2-instance scene's bbox was two points. Auto-framing then aimed the camera at
that degenerate box and pushed the geometry off screen. Now each placement adds
its transformed prototype BOX.

**Native-instance bug found underneath** (not skinning-specific): an instance's
children resolve to the PROTOTYPE's paths (UsdPrim follows `instance_prototype`),
so consuming an instance's mesh paths also consumed the prototype's own geometry
-- and the prototype prim, which is itself one of the authored instanceable prims
(pcp designates the first sibling and points the rest at it), was left to a
static-batching pass that could no longer see it. A 2-instance native group
therefore rendered **NOTHING**, and an N-instance group always lost one copy. The
prototype now gets its own placement in the group, and the `< 2` early-continue is
gone.

### Verification — DONE (2026-07-11)

- **Large-scene memory matrix** (Xvfb/NVIDIA, deferred-payload profiles): Island
  **1.47 s / 462 MB**, ALab **1.98 s / 528 MB**, Caldera **8.42 s / 2.11 GB** —
  all within noise of the WIP baseline (1.36 s/488 MB, 1.97 s/554 MB,
  8.29 s/2.18 GB). Weld ratios 1.00x / 1.00x / **1.07x** (Caldera: 817 534 verts
  from 761 694 points), nowhere near the ~4x a naive per-corner expansion would
  give, so the weld key is not over-splitting.
  *Caveat:* these profiles defer payloads and therefore decode **0 textures** —
  this matrix never exercised the texture cap. The ALab **full-payload** run does:
  343 textures / 1372 MB decoded, within the 1920 MB derived budget.
- `tool-tusdrender-smoke` green after the shared-decoder reroute (and after the
  binding/anchoring fixes below). Full suite **41/42**; the only failure is the
  known pre-existing `tusdview-skinning-screenshot-diff`.

---

## Older workstream (material evaluation) — sections 1-9

Status as of an earlier session. Work is on branch `tusdview` (base: `release`),
**not pushed**. That session's commits (base `ec31f934b`, newest first):

```
3ba3e23b2 docs + skinning-compare: material-eval scope, tusdview doc, RT skinning-diff opts
0ce35afcd tusdview: cross-backend renderer parity check (§9)
1c7ef11b7 tusdview(smoke): re-record model goldens for the default-material change
0d1b0f938 tusdrender: structured load-diagnostics summary (§9 parity)
9beb8697a tusdview: structured load-diagnostics summary + degraded-material policy (§9)
df2161ed8 tusdview(smoke): golden-fingerprint regression layer + JSON output
343db5f18 tests/unit: add tydra subdivision/light + usdz-reader unit tests
3acc5a7fc tusdrender: next-loader lighting/instancing updates + invisibleIds test
3b202775e tusdview: render PointInstancer/scenegraph instances in the tydra path (+ session viewer work)
6fd950251 tydra(mtlx): extend NodeGraph constant evaluator + output-channel resolution
```
(Earlier in the session, pre-`ec31f934b`: textools vendoring + texture pipeline +
DomeLight IBL — see git log and the memory file `textools-vendor-ibl.md`.)

Legend: **[done]** landed this session · **[partial]** started · **[todo]** untouched.

## 1. usd-assets regression harness — [partial]

- **[done]** Batch runner for tusdview (vk-raster/vk-rt/cuda-rt) and tusdrender
  (cpu/vk/vkr); warning buckets (load_error / timeout / backend_unavailable /
  backend_error / no_renderable / rendered_with_warnings / rendered /
  degraded_material); TSV + JSON output; curated repo-models subset;
  `examples/tusdview/tests/run-usd-assets-render-smoke.sh`.
- **[done]** Curated visual golden: coarse 12×12×3 fingerprint
  (`asset_fingerprint.py`) + checked-in baseline `models-render-goldens.tsv` +
  opt-in `tusdview-models-golden` ctest (gated on `TUSDVIEW_RUN_GOLDEN`, per-
  machine).
- **[done]** Golden harness now supports tusdrender PNG outputs without Pillow
  (`asset_fingerprint.py` has native PNG decode), a portable
  `--golden-kind coverage` silhouette metric, and a public
  `usd-assets-curated` profile. Added opt-in
  `tusdview-usd-assets-coverage-golden` ctest spanning vk-raster/vk-rt/cuda-rt
  plus tusdrender cpu/vk/vkr. Remaining: record/maintain per-machine external
  baselines and tune tolerances across more GPUs.

## 2. MaterialX evaluation — [partial]

- **[done]** `ND_standard_surface_surfaceshader` mapping already ~complete
  (`ConvertMtlxStandardSurfaceToOpenPBRSurface`). NodeGraph const-eval ops:
  swizzle, separate2/3/4 with output-channel resolution, combine2/3, smoothstep,
  saturate, generic ifgreater/ifgreatereq/ifequal (render-data-material-mtlx.cc);
  unsupported-node diagnostic surfaced.
- **[done]** `MtlxConstVal` now carries four components; constant folding reads
  `color4f`/`float4`/`float2`, evaluates `ND_constant_color4`, and supports
  `ND_combine4_*` + `ND_separate4_*` in the typed MaterialX evaluator. Covered
  by `tydra_renderscene_mtlx_nodegraph_ops_test`.
- **[done]** Non-material-local/shared and nested NodeGraphs now have checked-in
  Tydra conversion coverage (`tydra_renderscene_mtlx_nonlocal_nodegraph_test`),
  so a Material can fold constants from a sibling graph and from a graph output
  that forwards through another graph.
- **[done]** MaterialX interface constants now resolve without fallback warnings
  across scalar/color role conversions (`tydra_renderscene_mtlx_interface_inputs_test`):
  material-interface floats can broadcast to color params, and color/float3
  values can feed scalar params.
- **[done]** MaterialX `ND_geompropvalue_*` texture-coordinate chains now have
  checked-in coverage (`tydra_renderscene_mtlx_geomprop_texture_test`) proving
  renderer-facing `UVTexture::varname_uv` preserves the authored primvar name.
- **[done]** MaterialX texture color-space semantics now have checked-in
  coverage (`tydra_renderscene_mtlx_texture_colorspace_test`): color inputs
  synthesize sRGB while scalar/data inputs synthesize Raw.
- **[done]** MaterialX `filename` resolution is now covered for filesystem vs
  USDZ-internal textures by `tool-tusdrender-mtlx-usdz-texture-equivalence`,
  which renders an `ND_image_color3` NodeGraph through
  `--materialResolver tydra-next` in both package modes, asserts the texture is
  actually sampled, and compares pixels.
- **[done]** MaterialX `<UDIM>` filename behavior is now covered for filesystem
  vs USDZ-internal tiles by `tool-tusdrender-mtlx-usdz-udim-equivalence`. The
  `tydra-next` converter now follows NodeGraph output forwarding before texture
  extraction, so `ND_image_*` nodes behind MaterialX NodeGraph outputs are seen
  by tusdrender instead of falling back to default gray.
- **[done]** Continued the larger shared material-eval layer work (§9):
  experimental `-materialShading lightrt-bsdf` now uses `bsdf_sample` for a
  bounded indirect continuation bounce in addition to the existing `bsdf_eval`
  direct/IBL paths.

## 3. Texture behavior — [partial]

- **[done]** (earlier this session) textools resize, BC1/3/7, first VK mips,
  wrap-aware filtering, split-sum IBL.
- **[done]** tusdrender material resolver harness now has an opt-in strict mode
  (`tool-tusdrender-material-resolver-strict`, gated by
  `TUSDR_RUN_MATERIAL_RESOLVER_STRICT`) that fails on resolver diffs across the
  curated `usd-assets` material/texture set: wrap/transform, texture-coordinate
  routing, normal bias/scale, roughness/alpha tests, texture format tests,
  USDZ-embedded textures, and production-style material assets.
- **[done]** CI-small decoded texture normalization now covers synthetic
  grayscale `UInt8` and `UInt16` RGB sources through `BuildDrawScene`
  (`tusdview-texture-pipeline`), so 16-bit decoded PNG-like buffers no longer
  get skipped.
- **[done]** CI-small decoded texture normalization also covers EXR-like
  `Float` RG and `Half` RGBA decoded buffers through `BuildDrawScene`.
- **[done]** USDZ-embedded vs filesystem texture equivalence now has a
  checked-in headless tusdrender regression
  (`tool-tusdrender-usdz-texture-equivalence`) that generates a PNG + textured
  quad locally, renders both package modes, and compares decoded output pixels.
- **[done]** CMYK JPEG texture decode now has a checked-in headless tusdrender
  regression (`tool-tusdrender-cmyk-jpeg-texture`) with an embedded tiny CMYK
  JPEG payload, so it does not rely on external assets or image-generation tools
  at test runtime.
- **[done]** tusdrender scalar texture scale/bias now survives into hit-time
  roughness/metallic/occlusion/opacity/clearcoat sampling. Added
  `tool-tusdrender-opacity-texture-alpha`, which generates an RGBA opacity map
  connected through `UsdUVTexture.outputs:a` and verifies the transparent render
  changes across the quad. Connected `inputs:opacity` values are now treated as
  fallbacks instead of multiplied with the texture connection.

## 4. Transparency — [done]

- **[done]** Two-pass sorted alpha in tusdview GL + VK; `opacity`/
  `opacityThreshold` verified; AlphaBlend guard scene + `tusdview-transparency`.
- **[done]** tusdrender next-path const-opacity material blending fixed. The
  recursive opacity continuation ray now uses a local surface epsilon for znear
  instead of the primary camera near plane, so close geometry behind translucent
  surfaces is no longer skipped. Added shared fixture
  `models/tusdview-transparency.usda` and `tool-tusdrender-transparency`.
- **[done]** Shared transparency fixture now includes three sorted layers
  (opaque red, translucent green, translucent blue). Both `tusdview-transparency`
  and `tool-tusdrender-transparency` assert all three layers contribute.
- **[done]** External AlphaBlendSortTest now has opt-in active coverage via
  `tool-tusdrender-alpha-blend-sort-external` (requires `USD_ASSETS_ROOT`). It
  uses a non-autoframe camera and asserts a stable non-uniform translucent layer
  signal from the public production-style transparent-card asset.
- **[done]** tusdrender transparency now has an order-specific generated
  regression: the shared blue-front fixture must be blue-dominant in the
  blended overlap, and a generated green-front variant must become
  green-dominant. This catches order-insensitive blending, not just missing
  alpha.

## 5. Geometry — [partial]

- **[done]** Analytic-prim load crash fixed (Cone/Cylinder/Capsule Vertex
  interpolation); constant `primvars:displayColor` now consumed
  (`has_authored_displayColor` + mesh_build replication).
- **[done]** Uniform-interpolation `primvars:displayColor` now expands per face
  in `mesh_build.cc`; `tusdview-geometry-primvar` covers the shared-point case
  where indexed vertices must be expanded to preserve per-face color.
- **[done]** Renderer-facing tusdrender subdivision coverage added:
  `tool-tusdrender-subdivision-schemes` verifies level-1 Catmull-Clark,
  bilinear, loop, and authored-crease meshes render with expected triangle
  counts.
- **[done]** Mesh-build numeric vertex attribute decoding now handles non-float
  `VertexAttributeFormat`s used by viewer geometry packing. Coverage in
  `tusdview-geometry-primvar` exercises `Half2` UVs, `Dvec3` displayColor, and
  `Ushort3` tangent data.
- **[partial]** Deep subdivision parity validation (Catmull-Clark/Loop/
  bilinear/crease/tessellation) against external/reference renderers.
- **[done]** Renderer-facing subdivision coverage now also validates level-2
  Catmull-Clark, bilinear, Loop, and crease triangle counts. A generated
  non-planar Catmull-Clark grid compares plain vs infinitely-creased level-2
  renders and requires a stable visual difference, so crease tags are no longer
  covered only by topology counts.

## 6. Instancing and composition — [done]

- **[done]** Tydra path renders PointInstancer + native scenegraph instances
  (`BuildDrawInstances`, world-space prototype bounds); invisibleIds/inactiveIds
  honored; references/payloads/sublayers verified; `tusdview-tydra-pointinstancer`.
- **[done]** no-renderable fixture tracking is now covered by
  `tusdview-no-renderable-tracking`; the smoke harness classifies tusdrender's
  "found no renderable Mesh triangles" diagnostic as `no_renderable`.
- **[done]** PointInstancer per-instance `primvars:displayColor`/
  `primvars:displayOpacity` now surface through legacy tydra `RenderInstance`
  and tydra-next `RenderPointInstancer`/`RenderPointInstanceDraw`. The legacy
  tusdview bridge uploads per-instance display colors and splits material
  override batches; tydra-next duplicate-mesh mode bakes draw material/color into
  expanded meshes. GL and Vulkan raster now upload per-instance opacity, keep it
  through frustum culling, and route translucent instance batches through
  blended passes. Vulkan RT and the shared CUDA/HIP RT kernel now carry instance
  opacity in the per-instance tint record and use it for the opacity AOV plus
  the existing clear-color blend approximation for translucent RT hits. Added
  PointInstancer displayOpacity AOV coverage to `run-vk-rt-aov.sh` and
  `run-cuda-aov.sh`; the focused Vulkan RT probe rendered distinct 0.25/1.0
  opacity regions on the local llvmpipe RT device.

## 7. Lighting and cameras — [partial]

- **[done]** (earlier this session) DomeLight split-sum IBL in GL + VK raster +
  RT-miss env background.
- **[done]** `tusdview-lighting` now verifies MirroredBall and Angular DomeLight
  texture-format metadata survives RenderScene -> DrawScene conversion with
  decoded env textures.
- **[done]** tusdrender next-path purpose classification now reports all four
  purpose buckets (`default`/`render`/`proxy`/`guide`) and is covered by
  `tool-tusdrender-purpose-filtering`.
- **[done]** tusdrender authored camera selection is covered by
  `tool-tusdrender-camera-modes`, which renders two named cameras with distinct
  center-pixel views.
- **[done]** viewer-side DrawScene purpose metadata now has checked-in coverage
  in `tusdview-geometry-primvar`: inherited Xform purpose, authored Mesh
  purpose, default fallback, and backend `PurposeId` mapping.
- **[todo]** full-res RT env texture; StandardShaderBall parity (emissive
  geometry, wall materials, environment, shadows); camera tusdview-vs-tusdrender
  visual parity; GPU visual purpose-filtering parity.

## 8. Animation and skinning — [partial]

- **[done]** RT skinning fixed (CPU-skin forced for tracers); GPU blendshape
  morph; `tusdview-skinning-screenshot-diff` + `-vk-rt-` variants.
- **[done]** Time-sampled transform rendering now has checked-in headless
  tusdrender coverage (`tool-tusdrender-timecode-transform-animation`): `-frames`
  renders two time codes, confirms geometry animation triggers per-frame BVH
  rebuilds, and verifies the frame colors change as the animated Xform moves.
- **[done]** Time-sampled `points` rendering now has checked-in headless
  tusdrender coverage (`tool-tusdrender-timecode-points-animation`) using a
  default point sample plus frame-specific samples; frame colors confirm the mesh
  deformation is re-evaluated per time code.
- **[done]** Time-sampled authored `normals` rendering now has checked-in
  headless tusdrender coverage (`tool-tusdrender-timecode-normals-animation`).
  This also fixed the `-frames` animated-geometry predicate so animated normals
  rebuild the smooth-shading side buffer instead of reusing frame 1 normals.
- **[done]** `tydra-next` mesh conversion now preserves authored
  `skel:blendShapeTargets` into `RenderMesh::blend_shapes`, including sparse
  `pointIndices` expansion (`TestRenderConverterBlendShapeTargets`).
- **[done]** tusdrender next-path blendshape deformation is now visually covered
  by `tool-tusdrender-timecode-blendshape-animation`: `SkelAnimation`
  `blendShapeWeights` move a red foreground mesh between frames and reveal a blue
  background, proving frame rebuilds apply authored BlendShape offsets.
- **[done]** next-schema `GetSkelAnimationData` now reads token-array
  `blendShapes` and `joints`, covered by `test_schemas_ext`.
- **[todo]** Cross-backend tusdview/tusdrender blendshape screenshot parity is
  still open for the broader visual-parity matrix.

## 9. Renderer parity — [partial]

- **[done]** Structured `load summary:` diagnostics in **both** tools
  (degraded_material / missing_texture / unsupported_mtlx / skipped); degraded-
  material policy: `assign_default_material` keeps a broken material loadable and
  flags it (`tusdview-degraded-material`). Cross-backend **geometry** parity via
  drawn-triangle count (`tusdview-backend-parity`) — pixel comparison measured
  unreliable across backends; coverage silhouette is informational only.
- **[done]** Shared material-eval Phase 0 + Phase-1 spike: promoted the
  LightRT/OpenPBR parameter block into `src/tydra/openpbr-params.hh`, added a
  shared `tydra::next::RenderMaterial -> LightRtOpenPBRParams` converter, and
  routed tusdrender-next through it behind `--materialResolver
  legacy|tydra-next|compare`. Added the opt-in
  `tool-tusdrender-material-resolver` harness for curated `usd-assets` coverage.
- **[partial, HEADLINE]** the **shared material-evaluation layer** — see
  `doc/shared-material-eval-scope.md`. Phase 2 has started: tusdrender now links
  LightRT `mtlxrender/bsdf.c` and exposes experimental `-materialShading
  lightrt-bsdf`, which evaluates direct-light/headlight response through
  `bsdf_eval`. With `-materialResolver tydra-next`, the full shared OpenPBR block
  is carried through opt-in per-material side tables (flat path and TLAS BLAS
  tables); legacy resolution falls back to the existing slim fields. The
  diffuse irradiance and prefiltered reflection IBL terms now route through
  `bsdf_eval` in this mode, and opaque hits trace a bounded `bsdf_sample`
  continuation bounce for indirect response. The sampled path is covered by
  `tool-tusdrender-material-shading-bsdf-sample`, which renders a reflective
  quad over a red background and verifies the experimental BSDF mode shifts the
  surface toward the sampled background. Shared-resolver fallback is now reported
  by tusdrender's structured load summary and covered by
  `tool-tusdrender-degraded-material`. Remaining Phase 2 work: MIS/next-event
  weighting for the sampled path before considering it as a default. Also:
  cross-backend/tool screenshot golden.

## Suggested next item

Continue the **shared material-evaluation layer**
(`doc/shared-material-eval-scope.md`) with the rest of Phase 2: add MIS/next-
event weighting around the new `bsdf_sample` continuation path, then broaden
golden coverage for the experimental mode.

## Verification commands

```bash
# build (main dir @build, wasm @web/build)
cd build && make -j16 tusdview tusdrender unit-test-tinyusdz

# core viewer + material + parity ctests
ctest --output-on-failure -R \
  'tusdview-|tool-tusdrender-|unit-test-tinyusdz|feat-mtlx-'
# strict external texture/material parity (requires /mnt/disk01/work/usd-assets)
USD_ASSETS_ROOT=/mnt/disk01/work/usd-assets TUSDR_RUN_MATERIAL_RESOLVER_STRICT=1 \
  ctest -R tool-tusdrender-material-resolver-strict --output-on-failure
# opt-in per-machine golden (needs matching GPU baseline)
TUSDVIEW_RUN_GOLDEN=1 ctest -R tusdview-models-golden --output-on-failure

# prove the OFF build path
cmake -S . -B build_textools_off -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTINYUSDZ_BUILD_GUI_VIEWER=ON -DTINYUSDZ_BUILD_EXAMPLES=ON \
  -DTINYUSDZ_BUILD_TOOLS=ON -DTINYUSDZ_WITH_TEXTOOLS=OFF -DTINYUSDZ_BUILD_TESTS=ON
cmake --build build_textools_off --target tusdview -j16
```

Notes: clangd diagnostics in this repo are known false positives — trust
`make`/ctest. `build_textools_off/` is NOT gitignored — never `git add -A`.

## Fresh-session resume prompt (CURRENT work)

Copy this into a fresh coding-agent session to continue §0 (the `--next`
primvars / texturing / texture-VRAM work):

```text
Continue the tusdview `--next` primvars/texturing/VRAM work in this repo,
branch `tusdview`, on top of merge 68d008abe.
Read resume-tusdview.md SECTION 0 first (that is the in-flight work; sections 1-9
are an older material-eval workstream). The worktree is a dirty WIP with my
changes UNCOMMITTED and NOT pushed — do not revert unrelated edits, do not
`git add -A` (build_textools_off/ is not gitignored), and do not push.

Already done and verified (see §0 for detail): faceVarying/all-interpolation
primvar resolver + vertex welding + the missing V-flip in FillFlatGeometry
(next_scene_loader.cc); a shared decode-time texture decoder
(src/tydra/next/texture-cache.{hh,cc}) with a longest-edge cap + byte budget,
used by BOTH tusdview and tusdrender; budget-derived texture caps
(DeriveTextureBudget in resource-budget.hh); a UV-set-name fallback chain
(st/UVMap/uv/st0/map1) in tydra-next MeshConfig; and displayOpacity folded into
per-opacity material variants. Build is green; ctest is 39/40.

Do NOT re-litigate those. The remaining work, in order:

1. VERIFY (highest value). Rerun the large-scene memory matrix under Xvfb/NVIDIA
   per doc/tusdview.md, assets at /mnt/disk1/data/{island/usd/island.usda,
   alab/_merged_ALab/entry.usda,caldera/caldera.usda}. Baseline to beat/hold:
   Island 1.36 s / 488 MB, Caldera 8.29 s / 2.18 GB, ALab 1.97 s / 554 MB. Watch
   the new log lines `next: weld N vertices from M points (Kx)` and
   `next: textures N, decoded X MB (cap ...)`. A weld ratio near the
   corners-per-point count means the weld key is too strict and the memory win is
   gone. Also re-run tool-tusdrender-smoke (tusdrender now decodes through the
   shared decoder).

2. DECIDE on geometry tangents (§0 "Not done"). They are currently dormant and
   free. Enabling them means a 2nd converter for normal-mapped meshes, tangent
   world-transform in the batch-append path, a new VBO/binding, and edits to
   vk/shaders/mesh.{vert,frag} + gl_renderer.cc + vk_renderer.cc. But GL/VK/RT
   ALREADY derive the TBN from screen-space UV derivatives, so normal maps render
   correctly now that UVs are fixed — tangents only fix handedness on mirrored
   UVs, cost ~16 B/vertex, and touch mesh.frag, which the separate
   sophisticated-texturing branch owns. ASK THE USER before building this.

3. --next GPU skinning: FIXED (see the section above). ctest is now 44/44.
   Remaining next-path skinning gaps: instanced prototypes still don't skin, and
   RT/CUDA/HIP keep the load-time CPU bake.

Build: cd build && make -j16 tusdview tusdrender && ctest --output-on-failure.
Note: tydra-next sources must be listed in BOTH src/tydra/next/CMakeLists.txt and
src/next/CMakeLists.txt — the latter is the one the build actually uses.
clangd diagnostics in this repo are false positives; trust make/ctest.
```

## Reproducing / resuming prompt (older material-eval work, for another machine)

Copy this into a fresh coding-agent session on the other PC:

```text
You are continuing tusdview/tusdrender rendering-parity work in
/mnt/nvme02/work/tinyusdz-repo/tusdview (or wherever the repo is), branch
`tusdview` (base `release`), NOT pushed. Read AGENTS.md, resume-tusdview.md, and
doc/shared-material-eval-scope.md first. The worktree is a dirty WIP; do not
revert unrelated edits.

State: §4 Transparency, §6 Instancing/composition are done. §1 harness, §2
MaterialX const-eval, §5 Geometry, §9 renderer-parity diagnostics + cross-backend
geometry parity are partially done (see resume-tusdview.md per-section
done/todo). A golden-fingerprint harness (tusdview-models-golden, opt-in via
TUSDVIEW_RUN_GOLDEN) and a cross-backend geometry-parity harness
(tusdview-backend-parity) exist and are the verification loop for material work.

Current focus: the SHARED MATERIAL-EVALUATION LAYER (doc/shared-material-eval-
scope.md). Today there are 4 material struct families (tydra RenderMaterial,
tusdview DrawMaterialCPU + DrawLightRtOpenPBRCPU, tusdrender TriMat, lightrt
OpenPBRParams), ~4 resolvers, and ~6-7 BRDFs. tusdrender's default `next` path
(tusdr_next.cc ResolveMeshMaterialNext) hand-rolls a UsdPreviewSurface resolver
and never touches RenderMaterial, so it cannot do OpenPBR. The full OpenPBR BSDF
(src/external/lightrt/mtlxrender/bsdf.c) exists but only bakes constants for
tusdview; tusdrender does not even compile it.

Recommended next implementation: Phase 0 + a Phase-1 spike from the scope doc.
(0) Promote the OpenPBR param block (OpenPBRParams / DrawLightRtOpenPBRCPU) to a
shared header both tools include; no behavior change. (1) Behind a flag, route
tusdrender-next through tydra::next::RenderSceneConverter (src/tydra/next/render-
converter.hh) -> RenderMaterial -> the shared block, filling it with the mapping
lifted from BakeLightRtOpenPBR. Diff against the hand-rolled resolver on the
curated models with tusdview-backend-parity + the tusdrender smoke to MEASURE the
tydra-next converter's coverage gap (UDIM, wrap modes, scalar-texture channels,
displacement) BEFORE deleting ResolveMeshMaterialNext. Keep TriMat's slim memory
form for huge scenes (expand to the block at shade time). Then Phase 2: compile
bsdf.c into tusdrender and route its CPU integrator (tusdr_integrator.cc Shade)
through bsdf_eval/bsdf_sample; Phase 3 aligns the GLSL/CUDA BRDFs to the same
param contract (largest, lowest priority).

Useful verification commands are in resume-tusdview.md. Commit each phase
separately (WIP branch); the golden baseline will need re-recording whenever a
material change intentionally shifts a curated model's render (opt-in golden does
not auto-catch it). clangd diagnostics are false positives; build_textools_off/
is not gitignored (never git add -A).
```
