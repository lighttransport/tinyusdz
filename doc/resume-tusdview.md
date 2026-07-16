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

Done since (2026-07-13): the proxy/render purpose supersede (both loaders), a
family of **silent data-loss bugs in the writers** found by sweeping fixtures
through `tusdcat`, the **typeless-prim-invents-a-`Model`-typeName** bug (sole
cause of 68 of the then-133 round-trip failures, 133 -> 65), the **Camera
`shutter:open`/`shutter:close` written under the wrong attribute name** bug
(65 -> 64), the **`apiSchemas` list-op delete/`None`/prepend-order** bugs
(64 -> 60), and **five `rel`/relationship bugs** — varying/custom dropped,
`bindMaterialAs` dropped, `proxyPrim` dropped, and a
material-binding-collection namespace order swap (60 -> 50) — all pushed.
**variant-statement metadata dropped** (`active`/`hidden`/`kind`/`variantSets`
on a variant, 50 -> 41) is pushed too, as are the
**shader-connection-baked-to-constant** bugs (43 -> 39; also ported to
physics-2026-fix2).

Done since (2026-07-14), all pushed on BOTH branches: **the sweep is finished —
0 of 427 fixtures fail**, including Group B ("authored but empty" now has a
home: per-bucket `has_*` flags on `ListOp<T>`, `set_authored()` on
`value::TimeSamples`). **OpenUSD is installed** (`~/work/OpenUSD/dist`) and the
cross-check found the bug class our own tests cannot see — reader/writer bugs
they SHARE: empty-timeSamples inlined as payload=0 (pxr read it as file offset
0 -> "Corrupt asset"), four variant path-encoding wire bugs, `subLayers`
written as a type pxr silently IGNORES (must be the StringVector crate type,
with `subLayerOffsets` ALWAYS alongside), and authored typeNames re-typed
(`point3f[] points` -> `float3[]`, `token inputs:varname` -> `string`).
Unregistered metadata is now preserved at ALL levels (layer/prim/attr/rel) as
the UNREGISTERED_VALUE wire type. A **usdchecker parity pass** in
`tests/run-usdcat-compare.sh` pins the lot: usdchecker must report the SAME
validator rules on our written .usdc as on the source .usda (425 compared, 0
failed). Printer comparison: 0 different. pxr read sweep: 0 of 427. Checks
22-27 in `run-scope-imageable-roundtrip.sh` pin it all, mutation-verified. See
[Closed](#closed--kept-for-the-reasoning-not-because-they-are-open).

### Shader connections baked down to constants (43 -> 39 fixtures) — FIXED 2026-07-14

The worst bug of the sweep, because it did not drop inert metadata — it silently
**rewrote the asset's shading network** into a different, constant-valued one that
still loads and still renders:

```usda
    token inputs:wrapS.connect = </mat/pbr.inputs:wrapSDriver>
->  token inputs:wrapS = "useMetadata"
```

Two independent causes, both in `src/sconv-shader.cc`:

1. A shader input is `authored()` when it is **declared**, with or without a
   value, and `TypedAttributeWithFallback::get_value()` silently returns the
   **schema fallback** when no value was authored. Every typed input except
   `inputs:st` / `inputs:file` / `inputs:in` / `inputs:normal` called
   `get_value()` straight off `authored()` without first asking
   `is_value_empty()`, and never passed the attribute's connections through to
   `add_input_spec()` — which has accepted an optional `connections` argument all
   along. So a connection-only input was written as its fallback CONSTANT and the
   connection was lost. Fixed by giving all 10 affected inputs the shape
   `inputs:st` already had (`useSpecularWorkflow`, `opacityMode`; UsdUVTexture
   `wrapS`/`wrapT`/`fallback`/`sourceColorSpace`/`scale`/`bias`/`uv_set`/
   `uv_set_name`; UsdTransform2d `rotation`/`scale`/`translation`).
2. A Material terminal is a `TypedConnection` whose `has_value()` IS its
   connection count (`_targetPaths.size()`), so `AddMaterialOutputSpecs`' 
   `has_value()` gate dropped a DECLARED-but-unconnected terminal outright
   (`def Material "mymat" { token outputs:surface }` vanished on write). The three
   copy-pasted surface/displacement/volume blocks are now one helper.

Guarded by a mutation-verified `shader-connect-usdc` check that asserts both that
the connections survive AND that none came back as its fallback constant.

**Also ported to `physics-2026-fix2`** (`d35d78ffb`, on the local worktree at
`~/work/tinyusdz/physics-2026-fix2`, branch `physics-2026-fix2-local`, NOT
pushed): the bug is in shared core, and that branch had it too (140 -> 136 of its
423 fixtures; its baseline is higher because it lacks this branch's other writer
fixes). The `run-scope-imageable-roundtrip.sh` harness does not exist there, so
the assertion did not come across — and it could not simply be copied, because
that branch also still lacks the Material/NodeGraph `purpose` fix the harness
checks.

### Merging `physics-2026-fix2` (read before you merge it again)

That branch and this one have **independently rewritten `src/next`**, so `git`'s
"clean" auto-merge is not to be trusted. This has bitten once already: commit
`e9bd3aeff` ("Repair the physics-2026-fix2 merge") records a merge that
auto-resolved clean, kept BOTH sides' copies of similar blocks, mixed the two
sides' APIs, and did not compile. 15 `src/next` files change on both sides but
only 5 conflict — the 10 silent ones are the hazard.

Merged again on 2026-07-13. What needed hand-resolution, and why:

- **`stat` vs `lstat` (`resolver/asset-resolver.cc`) — a direct behavioral
  contradiction.** This branch uses `stat` so symlinked assets resolve (ALab
  depends on it, `2b3fd37ec`); physics-2026-fix2 deliberately went back to
  `lstat` with a TOCTOU rationale. **Keep `stat`** (decided 2026-07-13). Check
  this every merge — it will keep coming back.
- **`variant_overrides_by_path` does not exist on physics-2026-fix2 at all.** It
  powers `--variant` (tusdrender) and the viewer's variant overrides, and it was
  built on the OLD pcp. They rewrote pcp underneath it. The auto-merge grafted
  the seeding block into their new `ExpandList` correctly, but the CALL SITES are
  a trap: their cache key is now the composite `SourcesKey(stack, path)`, whereas
  `root_prim_path` must be the bare **prim path** (`p.str()`). Passing the cache
  key silently makes the override lookup never match.
- **`PromoteMaterialUVPrimvars` is 2-arg ON PURPOSE here.** Their 3-arg
  `default_uv` version still derives the secondary UV set as `<primary> + "1"` —
  the exact bug `53415635e` fixed by comparing against the mesh's real
  `texcoords_0_name`/`texcoords_1_name`. Do not "restore" their signature or the
  `default_uv_primvar` config field; keep their `retain_geometry` /
  `ReleaseMeshGeometry` addition alongside it.
- **Their pcp now remaps `prototype_root` -> `instance_root`**, so an instance's
  children resolve to the INSTANCE's paths, not the prototype's. `tusdview`'s
  native-instancing pass in `next_scene_loader.cc` assumed the opposite (its own
  comment said so), so the prototype's mesh paths stopped being consumed and the
  static-batching pass drew that geometry a SECOND time — 2 meshes for a
  1-prototype/2-instance scene. Caught by `tusdview-blas-compaction` (which
  asserts one BLAS per prototype); fixed by consuming the prototype root's mesh
  paths explicitly. **This is the test that will catch a bad merge here — do not
  skip it.**

Their `usda: accept layer-level reorder and unregistered metadata` (`8e77cd30d`)
lands on the READ side of two still-open sweep categories; the writer still drops
both (the 2 new `aousd-*` fixtures below).

## Prompts for a fresh session

Paste one of these verbatim. Each is self-contained: it says what is broken, how
to reproduce it, and how to know when it is fixed. Read the section it points at
before starting — the reasoning there is the part that is expensive to re-derive.

**1. (DONE 2026-07-14)** The crate-writer round-trip sweep is finished — 0 of
427 — and the pxr cross-check runs (OpenUSD at `~/work/OpenUSD/dist`). The
verification battery to hold, from the repo root, on BOTH branches (tusdview
and the `physics-2026-fix2` worktree at `~/work/tinyusdz/physics-2026-fix2`):

> ```bash
> bash tests/run-scope-imageable-roundtrip.sh      # 31 checks, self-skips pxr ones
> USDCAT_PATH=$HOME/work/OpenUSD/dist/bin/usdcat bash tests/run-usdcat-compare.sh
>   # printer comparison must be 0 different AND the usdchecker parity pass
>   # must be 0 failed (it exits 1 otherwise)
> cd build && ctest --output-on-failure
> ```
> Plus the two sweeps in the "Closed" section (writer round-trip and pxr read),
> both 0/427. Any new writer/reader work must keep all of these green, and any
> new wire-format fix needs a mutation-verified check in
> run-scope-imageable-roundtrip.sh (the pxr-facing ones are checks 22-27).

**2. Audit the remaining hand-rolled writer paths for role-typeName loss**
(small, self-contained):

> ConvertValue degrades role types (point3f[] -> float3[] crate value), so any
> writer site that pushes a converted value WITHOUT also emitting a
> `<name>.typeName` field re-types the attribute on the wire. The shared
> helpers (EmitTypedAnimatableAttr, ExtractAnimatableDefault,
> AddTypedArrayAttribute, AddArrayAttribute) and the EXTRACT_* macros in
> sconv-physics.cc / sconv-ar.cc are fixed; sconv-geom.cc's 15 direct sites are
> fixed. Two sites were inspected and left alone (token[] blendShapes, xformOp
> defaults — no role types flow through them). If a new fixture shows
> `point3f`/`vector3f`/`normal3f`/`color3f`/`texCoord2f` coming back as the
> underlying float type, the writer site is missing the typeName emission —
> grep for "Preserve the role spelling" for the pattern. Also note: the
> connection-only non-conformant-type path in ParseTypedAttributeUnified
> (prim-reconstruct-common.inc) deliberately does NOT record the authored
> spelling; revisit only with a fixture in hand.

**3. Mesh lights on the legacy `-rtPreview` path** — see the section below; decide
whether that path is worth keeping before building anything.

## Open

### The crate writer drops data — CLOSED 2026-07-14 (0 of 427; kept for the sweep commands and reasoning)

`.usdc` round-trips all 427 fixtures byte-identically, and pxr reads every
crate we write (pxr read sweep 0/427). Sweep, from the repo root:

```bash
for f in tests/usda/*.usda; do
  a=$(mktemp); c=$(mktemp --suffix=.usdc); d=$(mktemp)
  ./build/tusdcat "$f" > "$a" 2>/dev/null || continue
  ./build/tusdcat --output-format usdc -o "$c" "$f" >/dev/null 2>&1
  ./build/tusdcat "$c" > "$d" 2>/dev/null
  cmp -s "$a" "$d" || echo "DIFF $f"
  rm -f "$a" "$c" "$d"
done | wc -l          # 0 of 427 as of 2026-07-14 (was 3, 6, 10, 12, 17, 21,
                      # 50, 60, 64, 65, 133, 140; the physics-2026-fix2 merge
                      # ADDED 5 fixtures at the 41-of-422 point)

# and the pxr READ sweep — the one that catches bugs our reader/writer SHARE:
for f in tests/usda/*.usda; do
  c=$(mktemp --suffix=.usdc)
  ./build/tusdcat --output-format usdc -o "$c" "$f" >/dev/null 2>&1 \
    && ! $HOME/work/OpenUSD/dist/bin/usdcat "$c" >/dev/null 2>&1 \
    && echo "PXR CANNOT READ: $f"
  rm -f "$c"
done                  # 0 of 427 as of 2026-07-14
```

The USDA printer is a FIXED POINT — all 427 fixtures re-print identically — so a
diff here is the crate writer losing data, not the printer being creative.

The fixture count is NOT the measure of this work any more. The last three
rounds of fixes moved it by zero, because the bugs they closed were LATENT — real
data loss that no fixture happened to exercise. Checks 19-21 in
run-scope-imageable-roundtrip.sh, and the probe described below, are what measure
those.

Nothing remains. Group B (below) was the last group, fixed with explicit
authored flags. Every per-typed-attribute omission this sweep found is fixed.
Do NOT go looking for another one-line writer branch — there isn't one.

**CORRECTION, and the most useful thing on this page.** An earlier version of
these notes claimed the reader "cannot handle a typeName-only attribute spec" and
filed 3 fixtures under it. **That was wrong.** The reader reads such a spec fine
(`token outputs:surface` on a Material has always proved it). It was the WRITER,
committing the single mistake that accounts for most of this entire sweep:

> An attribute is `authored()` the moment it is DECLARED, with or without a
> value, and `TypedAttributeWithFallback::get_value()` silently returns the
> SCHEMA FALLBACK when no value was authored. So a writer that calls
> `get_value()` straight off `authored()` INVENTS a value the author never wrote
> — `double radius` is written as `double radius = 2`.
>
> This is not cosmetic. **An authored opinion is a STRONG opinion**, so the
> fabricated value wins over the weaker opinions it should have deferred to
> during composition: it silently changes what a layered scene resolves to.

The predicates, since they are easy to get backwards:
  - `TypedAttributeWithFallback::has_value()` is `!_empty` — **TRUE even when
    unauthored**. It is NOT an authored test. Neither is `TypedAttribute`'s.
  - `authored()` = declared. `is_value_empty()` = declared, but no value given.
  - Both together are what you want: authored + non-empty means "really has a
    value"; authored + empty means "emit a bare declaration".

`CrateWriter::EmitTypedAnimatableAttr` (sconv-detail.hh) is the fix, and the ONLY
correct way to emit a `TypedAttributeWithFallback<Animatable<T>>`: it emits the
value, or a bare declaration if there is none, plus the attribute's metadata block
and its connections. It derives the declared type from `T`, so a call site cannot
name the wrong one. **Never call `ExtractAnimatableDefault()` off `authored()`
again — that IS the bug.** All of sconv-geom.cc (16 sites) and sconv-light.cc (26
sites) now route through it.

Found by PROBING, not reading — declare a value-less attribute on each writer,
round-trip it, and see what comes back with a value it never had. That probe is
worth re-running after any new writer lands (checks 19-21 in
run-scope-imageable-roundtrip.sh are the pinned version of it). It caught 8 latent
shape attributes (Cylinder/Cone/Capsule/Plane), every UsdLux light input,
GeomCamera's focalLength/clippingRange/fStop, GPrim's doubleSided, and a
declared-but-value-less `extent` being DROPPED (`has_value()` is not an authored
test) — none of which any fixture exercised. sconv-skel/physics/media/ar probe
clean.

**Group B — "authored but empty" — FIXED 2026-07-14.** Three fixtures failed
for one reason: there was no way to record "authored, but empty". The fix is
exactly the flag the note below asked for: per-bucket `has_*` flags on
`ListOp<T>` (set by `Set*Items()`, `Has*Items()` = flag || size) and
`set_authored()`/`authored()` on `value::TimeSamples` — whose four HAND-WRITTEN
copy/move special members must each copy the new member or it is silently
dropped on every copy (that trap was mutation-verified). The crate format
needed NO change (per-bucket presence bits always existed). Original analysis
kept below.
  - `timesamples-empty-001`: an authored `timeSamples = {}` is dropped, because
    `PrimVar::has_timesamples()` is literally `ts.size() > 0`.
  - `rel-003` / `listop-delete-000`: a list-edit qualifier on a value-LESS
    relationship (`append rel myval`, `delete rel myheight`), because
    `ListOp<T>::Has*Items()` is likewise `.size() > 0`.
  Both want the SAME fix — an explicit authored/"was set" flag on the container
  (`value::TimeSamples`, and per-bucket on `ListOp<T>`) — and `ListOp<T>` has
  many users (variantSets, references, payloads, apiSchemas, inherits,
  specializes, connections, relationships), so verify the READ side
  (`DecodeListOp`) before touching it. Note `DecodeListOp` now emits the buckets
  in USD's canonical order (delete, add, prepend, append) — that order is what
  gets PRINTED, since a crate ListOp does not record the order the qualifiers
  were authored in.

Historical note, all FIXED, do not re-derive: Camera shutter naming; apiSchemas
list-ops; five rel/relationship bugs; variant-statement metadata; shader
connections baked to constants; unauthored fallbacks invented (Skeleton
visibility/purpose, GeomSubset elementType, physics:invertFilteredGroups,
mediaOffset); Mesh `skel:blendShapes` namespace; stage metadata
(kilogramsPerUnit, autoPlay/playbackMode, empty customLayerData); `reorder
nameChildren`/`properties`; blocked xformOp written as zero; timeSamples on the
typed writers; prim metadata (sdrMetadata, unregistered) and attribute metadata
(customData, colorSpace, bare strings); SkelRoot xformOps; mesh
`subsetFamily:<name>:familyType`; typed-attribute `.connect`; non-conformant
shader terminal types; listOp qualifier print order.

NOTE on bare strings in an attribute's metadata block (`double x = 1 ( """m""" )`):
these ARE the comment in USD -- the two ASCII spellings are one Sdf field, and
only the ASCII parser knows which was used (it parks the bare form in
`AttrMeta::stringData`, the `comment = ...` form in `AttrMeta::comment`). They are
written as the STANDARD `comment` field. An earlier pass invented a private
`stringData` crate field for them; that was replaced, because pxr could not have
read it. The cost is that the ASCII SPELLING is not preserved across a crate
round-trip (a bare string comes back bare, a `comment = ` stays only if it was
authored that way in ASCII) -- pxr does not preserve it either. The attribute
printer now honors `has_comment_prefix`, which the PRIM printer already did.

**Why this kept happening:** the extraction was copy-pasted per prim type, so each
new prim type was one omission away from losing data — and most of them were.
That is why the fixes converged on SHARED HELPERS
(`EmitTypedAnimatableAttr`, `EmitAttrMetas`, `EmitAttrConnections`,
`EmitAttrDeclaration`, all in sconv-detail.hh) rather than another copy: a new
prim type that routes through them cannot repeat any of this. Prefer extending a
helper to hand-rolling a field push.

CAVEAT on all of it, now RESOLVED: the checks above verify that tinyusdz's
reader and writer agree with each other — a bug they SHARE is invisible to
them. OpenUSD is now installed at `~/work/OpenUSD/dist` and exactly that bug
class turned up FIVE times: the empty-timeSamples payload=0 encoding, the
variant path-encoding family, the ignored `subLayers` value type (+ mandatory
`subLayerOffsets`), re-typed role typeNames, and dropped unregistered
metadata. All fixed and pinned: checks 22-27 in
`run-scope-imageable-roundtrip.sh` (they self-skip without pxr), the printer
comparison at 0 different, and the usdchecker parity pass (same validator
rules on our .usdc as on the source .usda — 425 compared, 0 failed) in
`tests/run-usdcat-compare.sh`.

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

### A typeless prim invented a `Model` typeName on write

`def "bora"` (no typeName authored) came back as `def Model "bora"` after a
`.usdc` round-trip. Sole cause of 68 of the (then) 133 fixture failures.

`src/stage-converter.cc` has two identical fallback blocks that, when a prim's
AUTHORED typeName (`prim_type_name()`) is empty, fall back to
`prim.type_name()` — the registered C++ label of whatever struct backs the
prim's value. That fallback exists on purpose: in-memory, programmatically
built prims (tydra-built `Material`/`Shader`) have an empty authored typeName
too but a real schema, and without the fallback the writer would drop their
typeName and type-specific properties entirely. The bug: a genuinely typeless
prim is ALSO represented internally by a catch-all `Model` struct, whose
`type_name()` unconditionally returns the literal string `"Model"` — so the
fallback resurrected a typeName the prim never had. Fix: exclude `Model`
specifically from the fallback, since a `Model`-backed prim already carries
the correct empty string via `prim_type_name()`. Guarded by a new
`typeless-usdc` check in `tests/run-scope-imageable-roundtrip.sh` covering
`def`/`over`/`class` (all three specifiers share the fallback);
mutation-verified by reverting the fix and confirming the check reproduces
`def Model "W"` / `over Model "child"` / `class Model "TheClass"`.

### Camera `shutter:open`/`shutter:close` written under the wrong attribute name

Written, not dropped — but under the wrong name. `src/sconv-geom.cc`'s
`CrateWriter::ExtractCameraProperties` writes every other Camera attribute
(`exposure`, `focalLength`, `horizontalAperture`, ...) under its plain USD
name, which is correct for those. `shutter:open`/`shutter:close` are the two
Camera attributes that ARE namespaced in the schema (`usdGeom.hh`,
`prim-property-tables.hh`), and the writer used the same plain-name
convention for them too (`add_double_attr("shutterOpen", ...)`). The reader
never finds a namespaced `shutter:open` back, and the printer falls back to
the schema default of `0.0` for both — an authored motion-blur interval
silently became `0/0` on round-trip. Fix: write the namespaced strings
directly; the field-name mechanism already handles colons fine elsewhere
(`inputs:texture:file` in `sconv-light.cc`), so this was a two-string change,
not a structural one. Guarded by a new `camera-shutter-usdc` check in
`tests/run-scope-imageable-roundtrip.sh`; mutation-verified by reverting the
fix and confirming the check reproduces `shutterClose`/`shutterOpen` (no
colon) in place of `shutter:close`/`shutter:open`.

### `apiSchemas` list-op deletes, `None`, and prepend order dropped/scrambled on write

`CrateWriter::ExtractPrimMeta`'s `apiSchemas` block (`src/stage-converter.cc`)
rebuilt a single `ListOp<value::token>` from the RESOLVED view
(`APISchemas::names`/`unknownSchemas`), which can only ever express one
explicit-or-prepend op. Three losses fell out of that: `delete apiSchemas =
[...]` was silently dropped (no "deleted" bucket read at all); `apiSchemas =
None` (`explicitlyEmpty`) wrote nothing, because both resolved vectors are
empty for it and the old `if (!schema_tokens.empty())` guard skipped the
field entirely — losing the distinction between "explicitly empty" and "no
opinion"; and `prepend` order was scrambled because known (`names`) and
unknown (`unknownSchemas`) schemas live in separate vectors with no
interleave record, so the old code always emitted unknown-then-known
regardless of authoring order.

Fix: `APISchemas::authoredOps` already exists and is populated by both
readers (`usda-reader-impl.hh`, `usdc-reader-prim.cc`'s `ToAPISchemas`) — the
verbatim authored op list, kept specifically so a writer could reproduce the
original `SdfTokenListOp` losslessly, but the crate writer never consulted
it. Now it replays every `authoredOps` entry through the matching `ListOp`
setter (mirroring the existing `convert_path_listop`/`convert_ref_listop`/
`convert_payload_listop` pattern used for references/payload/inherits/
specializes a few lines below) and handles `explicitlyEmpty` as its own case.
The old resolved-view logic is kept as a fallback for `APISchemas` built
programmatically (e.g. via the C API) with no authored-op history.
`FlattenAppliedSchemas`/`BakeAppliedSchemaListOp` (`composition.cc`) already
collapses `authoredOps` to one op, but only under `tusdcat --flatten`
(`examples/tusdcat/main.cc`), never during ordinary load/compose/write — so
this plain round-trip sees `authoredOps` intact. Guarded by a new
`apischemas-usdc` check in `tests/run-scope-imageable-roundtrip.sh` covering
all three cases in one fixture; mutation-verified by reverting the fix and
confirming the check reproduces all three symptoms at once.

### Five separate `rel`/relationship bugs

All in `ConvertRelationshipToFields` (`src/stage-converter.cc`) and its
callers, plus one in the collection-material-binding writer
(`src/sconv-geom.cc`):

1. `varying rel` was hardcoded to `Variability::Uniform`, ignoring
   `Relationship::is_varying_authored()` (a real field both readers already
   populate). Fixed by writing `Varying` when authored.
2. `custom rel` was never written: `ConvertRelationshipToFields` only ever
   took a bare `Relationship`, never the enclosing `Property`'s
   `has_custom()` — unlike `ConvertAttributeToFields`, which already takes
   an explicit `is_custom` parameter for this reason. Added the same
   parameter and threaded `prop.has_custom()` through both call sites that
   have a `Property` (`ConvertPropertyToFields`, `ConvertVariantToFields`);
   the two Collection includes/excludes call sites pass `false` (no
   `Property` wrapper there).
3. `bindMaterialAs` metadata on a RELATIONSHIP (e.g.
   `material:binding = </X> (bindMaterialAs = "...")`) was written for
   attribute metadata but never for relationship metadata — the far more
   common case, since it is a UsdShade relationship-strength qualifier.
4. `rel proxyPrim` (every GPrim's proxy-geometry relationship) was parsed
   into a typed field and never re-emitted at all — same shape as the
   Scope visibility/purpose bug, found in `ExtractGPrimProperties` (which
   already handles visibility/purpose/extent/orientation but stopped short
   of `proxyPrim`).
5. `material:binding:collection:<name>:<purpose>` order: the writer
   concatenated `<name>:<purpose>` when a purpose is present; the correct
   order is `<purpose>:<name>`. Only the no-purpose case happened to come
   out right (there was nothing to put first). A same-shaped sibling
   printer function (`pprint-meta.cc`'s `materialBindingCollectionMap()`
   loop) uses variable names that look like the opposite convention at
   first read -- the actual outer/inner role only became clear by testing
   against the two fixture lines. Don't trust a sibling function's variable
   names without checking a live example.

Guarded by a new `relationships-usdc` check in
`tests/run-scope-imageable-roundtrip.sh` covering all five in one fixture.
The first draft of that check put the collection-material-binding
relationships on a typeless prim, which bypasses the buggy typed
`MaterialBinding` map entirely (typeless prims store
`material:binding:collection:*` as generic named relationships) -- it passed
even with the bug reverted. Moved those two relationships onto an `Xform`
prim (which inherits the typed `MaterialBinding` interface) so
mutation-verification actually exercises the buggy code path.

**Left for later, likely not fixable without a deeper change:** two related
fixtures (`rel-003`, `listop-delete-000`) author a list-edit qualifier
(`append`/`delete`) on a value-less relationship (`append rel myval`,
`delete rel myheight` -- zero target paths). `ListOp<T>`'s `Has*Items()`
methods are purely `.size() > 0`, so an empty `SetDeletedItems({})` is
indistinguishable from never calling it -- `DecodeListOp` (the reader) then
sees no populated bucket and hard-ERRORS with "`targetPaths` is empty"
rather than silently dropping the qualifier. This looks like a genuine gap
in `ListOp<T>`'s data model (no way to record "this bucket was authored, but
empty"), not a simple writer oversight; fixing it would mean adding
per-bucket "was set" tracking to `ListOp<T>` and touching every one of its
many users (variantSets, references, payloads, apiSchemas, inherits,
specializes, connections, relationships). Left alone -- verify the read-side
`DecodeListOp`/`ListOp<T>` change first if picking this up.

### Variant-statement metadata dropped on write (50 -> 41 fixtures)

A variant statement carries its own Prim metadata block, and the readers
populate it just like a Prim's:

```usda
variantSet "geo" = {
    "a" (
        active = true
        hidden = true
        kind = "component"
    ) { ... }

    "b" (
        prepend variantSets = "sub"   # a variant that NESTS a variantSet
    ) { ... }
}
```

`Variant` (`src/core/variant-types.hh`) stores this as a full `PrimMeta` behind
`metas()`. But `ConvertVariantToFields` (`src/stage-converter.cc`) wrote exactly
one field to the Variant spec -- `specifier` -- and never called
`ExtractPrimMeta` at all, so every variant-statement metadatum dropped on write.
The variant itself, its properties, its prim children, and its nested
variantSets all survived; only the metadata block vanished. Note the nested case
is a double loss: the nested variantSet's CONTENT was written (the recursion at
the bottom of `ConvertVariantToFields` handles it), but the `variantSets = "sub"`
metadatum that declares it was not.

Fix was one line -- call the existing `ExtractPrimMeta(variant.metas(),
v_fields)` right after the `specifier` field. `ExtractPrimMeta` already handles
`active`/`hidden`/`kind`/`variantSets` (and `customData`, `assetInfo`,
`apiSchemas`, ...), so there was nothing to hand-roll; the variant path had
simply never been wired to it. Fixed all 9 remaining `variantSet*` /
`feat-nested-variantset` fixtures at once, no regressions. Guarded by a
mutation-verified `variant-meta-usdc` check in
`tests/run-scope-imageable-roundtrip.sh`.

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
