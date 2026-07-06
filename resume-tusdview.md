# tusdview Remaining Work

Status as of the current session. Work is on branch `tusdview` (base: `release`),
**not pushed**. This session's commits (base `ec31f934b`, newest first):

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

## Reproducing / resuming prompt (for another machine)

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
