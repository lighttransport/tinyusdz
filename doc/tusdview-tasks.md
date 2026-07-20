# tusdview / tusdrender — open tasks & working notes

Single source of truth for in-flight work on the two renderers and the texture
program. Supersedes the old `doc/tusdview-audit.md` (full audit) and the
untracked `resume-tex.md` (KTX2 resume) — done history was dropped on merge; it
lives in git history if needed. Updated 2026-07-20.

The original two programs documented below -- (A) the **tusdview/tusdrender
audit** and (B) the **texture-compression / KTX2** work -- are complete.  The
active work is now the USD rendering-fidelity roadmap below.  This file is the
canonical task list; the repository-root `tasks.md` is historical planning
input and must not be used as evidence that an unchecked feature is missing.

---

## Active USD rendering-fidelity roadmap

Completion policy: shared schema/Tydra extraction is preferred over viewer-only
duplication.  Each feature lands in OpenGL and Vulkan raster first, then in
Vulkan ray query and the shared CUDA/HIP tracer.  Unsupported inputs must
produce structured diagnostics with their material/prim path rather than
silently becoming the default material.

| Area | Default `--next` | Legacy | GL/VK raster | VK/CUDA/HIP RT |
|---|---|---|---|---|
| PreviewSurface core semantics | supported | supported | supported | supported |
| OpenPBR/MaterialX advanced lobes | degraded | degraded | unsupported | partial constants |
| Ordinary/UDIM texture mips | supported | supported | supported | trilinear footprint LOD |
| Mesh/analytic/subdivision/instancing | supported | supported | supported | supported |
| Points and Basis/NURBS/Hermite curves | extracted | partial ribbons | GL raster | solid proxies |
| Authored camera | perspective + orthographic | perspective + orthographic | filmback/offset/exposure | filmback/offset/exposure |
| USD lights | full shared extraction | full extraction | linked multi-light + dome IBL | multi-light + dome IBL |

### P1 -- Material

- [ ] Add one backend-neutral real-time PBR material block, populated
  identically by the default `--next` and legacy loaders.
- [ ] Cover base/diffuse, metalness, specular workflow/IOR, occlusion, coat,
  emission, opacity/cutout, normal, coat normal, and displacement for
  UsdPreviewSurface, OpenPBR, and MaterialX standard-surface graphs.
- [ ] Preserve successfully evaluated inputs when a graph degrades and report
  unsupported real-time lobes (transmission, subsurface, sheen, anisotropy,
  thin film, dispersion, volume) explicitly. Structured, path-qualified
  diagnostics now cover these advanced lobes in both loaders and the default
  next-core carrier retains the previously missing thin-film, dispersion, and
  extended anisotropy inputs; full degraded-graph preservation remains open.

### P2 -- Texture

- [ ] Give every PBR texture input the same image/UDIM, channel, color-space,
  UV-set, transform, scale/bias, and wrap descriptor; add occlusion and coat
  semantic slots without re-aliasing independent metallic/roughness inputs.
- [x] Carry complete ordinary/compressed/UDIM mip chains into the RT texture
  table and use ray-footprint LOD plus trilinear filtering in Vulkan, CUDA, and
  HIP.
- [ ] Pin packed-map, sparse-UDIM, scalar-color-space, and external/USDZ parity
  for every new slot.

### P3 -- Shading

- [x] Replace the raster preview's half-Lambert/Phong approximation with
  linear-light Cook-Torrance GGX (Smith masking + Schlick Fresnel),
  energy-conserving diffuse/specular separation, and a second coat lobe.
- [x] Apply occlusion only to indirect light and evaluate the same lobes for
  DomeLight IBL; keep AOV and alpha behavior unchanged.
- [ ] Match the evaluated material response in Vulkan ray query and the shared
  CUDA/HIP tracer after GL/Vulkan raster parity is pinned. Occlusion and the
  coat weight/color/roughness, specular-workflow color, and dedicated coat-normal
  maps now reach every raster and RT backend with independent UV routing and
  scale/bias (plus channel selection for scalar slots). Full ordinary/UDIM
  visual parity coverage remains open.

### P4 -- Geom/Prim support

- [x] Feed Tydra `RenderPoints` and `RenderCurves` into the default `--next`
  DrawScene instead of limiting visible geometry to meshes.
- [x] Render Points in OpenGL as world-sized camera-facing instanced quads with
  widths, displayColor/displayOpacity, materials, animation-time evaluation,
  purpose filtering, and material/prim/mesh/purpose AOV ids.
- [x] Render Basis/NURBS/Hermite curves in OpenGL as camera-facing ribbons using
  Tydra's tessellated centerlines and interpolated widths/colors/opacity.
- [ ] Add the matching Vulkan raster point/ribbon pipeline and GL/Vulkan image
  parity coverage.
- [ ] Extend viewport click/region picking, framing, visibility, and selection
  highlights from mesh-only indices to native Points/Curves carriers.
- [x] Use width-aware octahedron/tube proxy geometry for Points/Curves in
  Vulkan ray query and the shared CUDA/HIP tracer, preserving color and
  opacity.

### P5 -- Camera

- [x] Preserve authored perspective/orthographic projection, full filmback,
  aperture offsets, clipping range, and exposure in both loaders.
- [x] Add `--camera-conform fit|crop|horizontal|vertical|none` (default `fit`),
  with matching config and GUI controls, and use the same projection in all
  backends and headless rendering.
- [ ] Keep depth of field, shutter/motion blur, stereo, and arbitrary clipping
  planes as documented future work.

### P6 -- Lighting

- [x] Make default `--next` light extraction retain the complete shared
  DrawLightCPU data used by legacy conversion, including normalize, shaping,
  diffuse/specular multipliers, shadows, and light links.
- [x] Replace the single derived preview light with linked multi-light direct
  evaluation for distant, point/sphere, rect, disk, and cylinder lights while
  preserving DomeLight IBL. Raster evaluation is bounded to the first 16
  supported lights in stage order, diagnoses truncation, applies per-mesh
  collection masks, and currently evaluates finite area lights at their
  representative point with authored shaping.
- [x] Add one deterministic 2048-square raster shadow map (first enabled
  DistantLight, otherwise first shaped SphereLight, in stage order) with 3x3
  PCF. Landed in GL and Vulkan raster and pinned by
  `tusdview-raster-shadow-map`, which requires a *bounded* dark band (lit floor
  on both sides) so a collapsed light-space transform that darkens the whole
  floor cannot pass. Verified discriminating: the same fixture with
  `shadow:enable = 0` scores 0 shadow rows on both backends versus 27 (GL) and
  33 (VK) enabled. Direct and shadow collections now use independent per-mesh
  masks in GL and Vulkan. Ordinary and UDIM base-alpha/separate-opacity masks
  cut holes in the GL/Vulkan shadow pass, and Vulkan instanced prototypes now
  cast through a dedicated deformation-aware shadow pipeline. The checked-in
  regression now requires an opaque bounded band, zero rows for constant and
  sparse-UDIM rejected cutouts, and the same bounded band from a PointInstancer
  prototype. Shadows for the remaining light types stay future work.
- [x] Use per-light visibility rays for shadows in Vulkan ray query and the
  shared CUDA/HIP tracer. RT instances now carry independent direct-light and
  shadow collection masks, so linked meshes receive only the authored lights
  and only authored shadow casters can occlude them. Selective membership is
  exact for the first 32 stage-order light records; later records retain
  collection-all behavior.
- [x] Diagnose rather than silently approximate GeometryLight, PortalLight,
  IES, and emissive-mesh light sampling until dedicated implementations land.

### Acceptance gates

- [ ] Shared extraction tests prove `--next`/legacy equivalence for material,
  texture, camera, and light records.
- [ ] Checked-in fixtures cover a PBR material grid, packed/UDIM minification,
  Points and all curve families, perspective/orthographic lens shift,
  multi-light linking, and raster shadows. The linked red/blue/magenta raster
  fixture is checked in and compares GL/Vulkan output. The generated raster
  shadow regression covers the current baseline; a checked-in alpha-cutout and
  instancing fixture is still outstanding.
- [ ] GL/Vulkan raster images agree within the focused-test tolerances before
  the corresponding Vulkan/CUDA/HIP RT task is closed.
- [ ] Run the curated external usd-assets golden sweep when the corpus is
  mounted; missing external data remains a skip, not a normal-test failure.
- [ ] Run focused tusdview tests, full native CTest, and the large-scene
  first-display/VRAM comparison.  Regenerate embedded SPIR-V with the documented
  SDK glslang whenever Vulkan shader sources change.

---

## Active-roadmap verification evidence

Latest focused verification on 2026-07-20:

- The resumed material/shadow implementation builds after regenerating Vulkan
  shaders. `tusdview_lightrt_bridge_test`, `tusdview_openpbr_material_test`,
  `tusdview_lighting_test`, and `tusdview_texture_pipeline_test` pass. The
  bridge test pins the expanded raster and RT coat scale/bias ABI.
- `tusdview-raster-shadow-map` now renders both an opaque blocker and the same
  blocker rejected by `opacityThreshold`; GL and Vulkan must show a bounded
  shadow only for the opaque case. `tusdview-vk-render` also passes with the
  material-aware shadow fragment shader and expanded RT descriptor ABI.
- Vulkan shader regeneration and `tusdview-vk-render` cover the dedicated
  instanced shadow pipeline, 25-binding material set, and coat-normal raster/RT
  layouts. `tusdview_lightrt_bridge_test` pins the 54-vec4 raster and 155-float
  RT coat-normal descriptor offsets.
- The expanded `tusdview-raster-shadow-map` oracle reports 27 bounded shadow
  rows for both ordinary and PointInstancer casters and zero rows for constant
  and sparse-UDIM rejected masks on Vulkan. Backend-unavailable cases remain
  skips, but an instanced/UDIM subcase may not silently skip after its baseline
  backend has rendered.

- Native `tusdview` and `test_tydra_next` build successfully.
- The twelve focused backend/material/camera/non-mesh CTests pass, including
  Vulkan raster, Vulkan ray query, CUDA deformation, GL carrier image output,
  opacity, back-face materials, transparency, and camera CLI behavior.
- All seven registered headless tusdview unit executables pass; the lighting
  test includes point/curve RT proxy topology and opacity assertions.
- All eleven tests carrying the `tusdview` CTest label pass. This label now
  includes the seven headless unit executables plus the checked non-mesh
  extraction and OpenGL carrier-render regressions, so `ctest -L tusdview`
  exercises both CPU records and visible Points/Curves output.
- The complete configured native CTest run covers 190 tests: 185 pass and five
  external/backend-dependent tests skip, with no failures. The new
  unsupported-lobe regression passes in the focused, labeled, and complete
  gates. An earlier complete run's sole reported failure,
  `tusdview-rt-skinning`, was a comparator false positive: decoded CPU/GPU
  images differ at one gray pixel by one 8-bit level while the animated poses
  otherwise agree.  The regression now compares decoded pixels and permits at
  most that single-pixel/one-LSB floating-point roundoff.  The saved failing
  pair passes the corrected comparator, and the corrected test passes on the
  NVIDIA Vulkan RT device. Together with the fresh complete run, this satisfies
  the full native CTest portion of the acceptance gate.
- `test_tydra_next` directly covers Points/Curves opacity interpolation, all
  curve families, light-link collections, and unauthored light shape defaults.
- The checked `tests/usda/tusdview-nonmesh-points-curves.usda` fixture retains
  one Points prim and Basis/Hermite/NURBS curves and renders through OpenGL;
  Vulkan ray query builds four corresponding BLAS carriers.
- The checked `tests/usda/tusdview-raster-multilight-links.usda` fixture proves
  additive red/blue/magenta direct-light accumulation, per-mesh light-link
  masks, focused GL/Vulkan mean-color agreement, and default/legacy Vulkan
  agreement. The shared packer unit also pins the 16-light stage-order bound
  and DistantLight direction convention. Legacy streaming now resolves
  CollectionAPI after its final mesh table exists and dispatches every USD
  light schema; a DistantLight path-expression unit pins that behavior. Scenes
  with authored light-link collections retain source-prim batch identity so
  collection membership cannot be lost to static batching.
- `tusdview-unsupported-realtime-lobes` loads the same OpenPBR material through
  default and legacy conversion and requires one structured diagnostic naming
  transmission, subsurface, sheen/fuzz, thin-film, anisotropy, and dispersion.
  The neutral next-core record retains these authored inputs instead of
  discarding them before a future evaluator can consume them.
- Shell syntax and `git diff --check` pass.

These results do not yet satisfy the unchecked large-scene performance,
external usd-assets, or GL/Vulkan image-parity gates.

---

## Task status

### Audit (A)

- ~~**T12 [medium] — tusdview `--next` material gaps.**~~ **DONE 2026-07-15.**
  The loader reads
  `use_specular_workflow` / `specular_color` / `ior` (UsdPreviewSurface) and
  `specular_ior` (OpenPBR) into `DrawMaterialCPU`; both raster backends compute
  F0 = specularColor (spec workflow) or dielectric-from-ior (metallic), unified
  GL↔VK (`computeF0`). VK routes it through a new `specParams` vec4 in the set-6
  SSBO (workflow flag folded into ior's sign, so no push-constant lane); GL uses
  three uniforms. Pinned by `tusdview-specular-workflow`. Separate scalar
  opacity textures now work in GL/VK raster for ordinary images and UDIMs,
  including channel selection, UV transforms/UV-set routing, and scale/bias.
  The base-rgb/same-texture-alpha graph is sampled once rather than squared.
  Both rasterizers use one scene-wide 100-column UDIM lookup atlas, replacing
  the per-slot LUT samplers that previously crossed radeonsi's shader-complexity
  cliff. Missing opacity tiles are opaque. Varying `displayOpacity` is packed
  with displayColor as RGBA and consumed by GL/VK raster, Vulkan ray query, and
  the shared CUDA/HIP tracer; constant values still fold into per-mesh material
  variants, multiplicatively with authored material opacity. The opacity AOV is
  GL/VK-identical and includes the separate mask plus varying vertex and
  PointInstancer opacity. Prototype vertex alpha also participates in instanced
  batch transparency classification in both raster backends, pinned by the
  point-instanced opacity-ramp case in `tusdview-opacity-material`.

- ~~Cross-tool parity oracle~~ **DONE (2026-07-14)** — `models/parity-material-uv-subset.usda`
  + `tools/tusdrender/tests/run-cross-tool-parity.sh`, registered as
  `tusdview-tusdrender-parity`. Four coplanar quads, each bound through a
  `materialBind` GeomSubset: red | green | blue | (texture cropped via `uvSet1`
  onto a yellow quadrant). Renders `tusdview --headless` (Vulkan) and
  `tusdrender -rtPreview` and asserts both give `r g b y` AND agree region-by-region.
  It found **two real tusdview `--next` bugs on its first run** (both fixed, both
  now pinned by it — verified by reverting each and watching it fail):
  1. **Constant-color materials grayed out.** `BakeLightRtOpenPBR` →
     `BakeUsdPreviewSurface` read via a PARAM MAP (`mat.params`) that only the
     LEGACY loader populates, then copied its own defaults back over the material —
     so on the (default) `--next` path every untextured constant-color material lost
     its color. Fixed by seeding from `DrawMaterialCPU`'s direct fields first.
  2. **The secondary UV set never reached the GPU.** `uv0` rides inside `DrawVertex`,
     but `uv1` is a PARALLEL array, and the batch builder appended vertices without
     it — so every batched draw carried an EMPTY `uv1` and any texture routed to
     `uvSet1` sampled one fixed coordinate (GL: the corner texel; VK: the averaged
     mip). Fixed by carrying `uv1` through both batch-append paths (`anyUv1`,
     mirroring `anyColor`). Affected GL *and* VK.
     **`tusdview-uv-set-routing` was pinning this bug**: it asserted the `uvSet1`
     render is "3x flatter than st", which is only true when uv1 is broken (a 4x
     zoom of a checkerboard is still a checkerboard — same contrast, bigger cells).
     Its second assertion is now a contrast FLOOR (the crop must not be flat).
     Do not re-add the flatness ratio.

### Texture / KTX2 (B)

- ~~**Web scene-texture routing.**~~ **DONE locally 2026-07-15.** The main
  tinyusdz WASM module links textools and exposes RGBA8 -> `uni` plus
  `uni` -> BC7/ASTC/ETC2/RGBA8 bindings. `getTextureFromUSD` sends both
  native-decoded and ordinary browser-decoded scene images through the best
  supported GPU block format, with linear/sRGB variants, pre-encode Y flip,
  and the original uncompressed fallback. Three.js receives its recognized base
  compressed-format constant; `texture.colorSpace` selects the sRGB upload.
- ~~**Basis Universal / KHR_texture_basisu** transcoder.~~ **DONE locally
  2026-07-15.** Undecoded external and embedded `.ktx2` scene textures route
  through a lazy Three.js `KTX2Loader` for ETC1S/UASTC. Applications may supply
  a renderer-configured loader or use the automatic WebGL capability probe;
  decoded tinyexr `uni` textures retain the Basis-free fast path. Embedded
  headers distinguish private `uni` from real Basis/standard ASTC before
  dispatch, and a Chrome render/readback regression pins decoded pixels.
- ~~**Land tinyexr PR #259** (`texpipe-ktx2-zstd-writer`, the KTX2 Zstd
  writer).~~ **DONE 2026-07-15.** Merged into TinyEXR `release` as `1b10661` and
  re-synced into `src/external/textools`; upstream `make tools-test`, upstream's
  standalone Three.js KTX2Loader test, native textools/tusdview tests, and the
  four tusdview WASM/browser texture gates all pass.

---

## Status snapshot

- **Audit (A): backlog EMPTY** (T12 and the parity oracle are DONE). All other
  findings fixed or refuted, pushed to `tusdview` (latest `79d0c8dcd`). Highlights
  of the last pass: R10 light `normalize` + dome `texture:format`; T8 doubleSided
  on `--next` + VK back-face cull; R12 doubleSided cull in the tracer; **T11 +
  GL↔VK unification** — linear sRGB workflow (sRGB textures upload `_SRGB`,
  `linearToSrgb` OETF on the shaded output; GL adopted VK's derivative-TBN normal
  map, VK adopted GL's soft-headlight diffuse — GL==VK byte-identical now; RT got
  the encode too). T7 (Facing AOV) refuted. The standalone `src/next` gate also
  found a post-merge UV-slot edge locally: a material-referenced set already in
  `texcoords_1` was not normalized to primary. Promotion now swaps it into slot 0
  while preserving the displaced set in slot 1, pinned by
  `TestMaterialParityFixes`.
- **Texture (B): all 5 phases + kept-compressed / Zstd / HDR-BC6H follow-ups DONE,
  plus web scene-texture routing and Basis interop done locally.** tinyusdz
  `tusdview` through `fe30d477c`; TinyEXR's reader and Zstd writer work are both
  merged (`release` `1b10661`, PRs #258/#259) and the vendored textools snapshot
  is synchronized. textools/tusdview ctests are re-registered (`textools-*`,
  `tusdview_texture_pipeline_test`).

## By-design residuals (NOT todo — documented so they aren't re-opened)

- tusdrender TLAS/instanced path faceforwards (no per-prim filter in
  `lrt_tlas_intersect1`); doubleSided cull is flat-path only.
- VK MDI instanced batch draws many prototypes per indirect call, so it can't
  vary cull mode — stays no-cull (the per-mesh instanced fallback does cull).
- RT (ray-query) keeps its own real-shadow lighting; it only shares the sRGB
  output encode with the raster backends, not the diffuse model.

---

## Gotchas (learned the hard way — still live)

- **`--next` is the DEFAULT tusdview loader** and builds its DrawScene textures
  itself (`next_scene_loader.cc` + tydra-next cache), bypassing `mesh_build`'s
  `BuildDrawTextures` / `ApplyTextureRuntimeOptions`. A feature wired only into
  the legacy path is silently inert by default.
- **Anything PARALLEL to `DrawMeshCPU::vertices` must be appended by hand in the
  batch builder** (`next_scene_loader.cc`, both the single-material fast path AND
  the multi-material GeomSubset path). Position/normal/uv0 ride inside `DrawVertex`
  and travel for free; `uv1`, `vertexColors`, `vertexAlpha`, `tangents`, skin and
  morph attributes do NOT. Forget one and the batched draw silently carries an
  EMPTY buffer — the shader then reads a default (uv1 = one fixed coordinate) and
  the failure looks like a *shader* bug, not a loader one. Follow the `anyColor` /
  `anyUv1` pattern: back-fill the vertices already in the batch, then push per
  vertex (zeros for meshes that lack the attribute).
- **`raytrace_comp.spv.h` must be regenerated with the SDK glslang 14.0.0**
  (`build-vulkan-sdk-1.3.275.0/src/Vulkan-ValidationLayers/external/glslang/build/install/bin/glslang`),
  NOT the PATH `/usr/bin/glslang` 15.1.0 — the latter emits ray-query SPIR-V that
  **segfaults radv at pipeline creation**. Run
  `bash examples/tusdview/vk/shaders/build-shaders.sh <that-glslang>`, keep only
  the `.spv.h` you actually edited, `git checkout --` the rest (they're full-file
  glslang-version churn).
- **Vendored textools stays pristine:** fix upstream at `~/work/tinyexr`, then
  re-copy into `src/external/`. `TINYUSDZ_WITH_ZSTD_COMPRESSION` is defined
  PRIVATEly on the core lib — textools consumers (tusdview, usd-texcomp) must
  re-declare it.
- **`uni` bitstream CHANGED** (tinyexr `release`). Old `.ktx2` are rejected
  (TC_ERROR_CORRUPT); regenerate authored uni `.ktx2` with `usd-texcomp`.
- `mesh_build.cc` wraps helpers in an anonymous namespace (~L33-2574); anything
  declared in `mesh_build.hh` must be defined OUTSIDE it or the call is ambiguous.
- **Never push without the AGENTS.md pre-push audit + explicit permission** every
  time (credential grep + gitleaks + trufflehog over the exact range, personal
  paths in messages AND diffs, private asset names, artifacts/binaries).

## Build & verify

- Native: `@build`, `make -j16`; `cd build && ctest --output-on-failure`. GPU
  tests need a real device — prefer `DISPLAY=:0 ctest`; software GL (Xvfb/llvmpipe)
  skips them (see `tests/tusdview/gpu_backend.py`).
- Roundtrip: `bash tests/run-usdcat-compare.sh`.
- Texture: `build/textools_*_test`,
  `build/examples/tusdview/tusdview_texture_pipeline_test`; textools upstream
  `cd ~/work/tinyexr && make tools-test`.
- The render goldens (`examples/tusdview/tests/*-goldens.tsv`) are
  coverage/silhouette fingerprints (robust to shading nudges), per-machine,
  regenerated with `--update-golden`.

## Resuming prompt

> Read `doc/tusdview-tasks.md`. Two programs: the tusdview/tusdrender audit
> (backlog empty; T12 and the cross-tool parity oracle are done) and the KTX2
> texture program (all local and upstream work complete; TinyEXR PR #259 merged
> and vendored at `release` `1b10661`). Native build `@build -j16`, gate with `ctest`
> (`DISPLAY=:0` for GPU). Mind the gotchas above — especially the SDK-glslang
> requirement for `raytrace_comp.spv.h`. Pre-push audit + permission every time.
