# tusdview / tusdrender — open tasks & working notes

Single source of truth for in-flight work on the two renderers and the texture
program. Supersedes the old `doc/tusdview-audit.md` (full audit) and the
untracked `resume-tex.md` (KTX2 resume) — done history was dropped on merge; it
lives in git history if needed. Updated 2026-07-13.

Two programs run here: (A) the **tusdview/tusdrender audit** and (B) the
**texture-compression / KTX2** work. The audit backlog is empty; the texture
program has web/Basis follow-ups left.

---

## OPEN tasks

### Audit (A)

- **T12 [medium] — tusdview `--next` material gaps.** `next_scene_loader.cc`
  (~2043–2076) reads only baseColor / metal-rough / emissive / normal. It does
  NOT read `opacity.texture_id`, does not sample the stored varying
  `displayOpacity` (~3322), and ignores `use_specular_workflow` / `specular_color`
  / `ior`. So an RGB diffuse + separate grayscale `inputs:opacity` mask with
  `opacityThreshold` cuts out in tusdrender but renders opaque in tusdview; a
  `useSpecularWorkflow=1, ior=1.33` material is honored by tusdrender and falls
  back to dielectric IOR 1.5 in tusdview. Fix: read the opacity texture, sample
  `displayOpacity`, honor the specular workflow + ior. Pin with a headless
  cutout + a specular-vs-metallic render.

- **Cross-tool parity oracle (highest-value test, not built).** Nothing renders
  the *same* scene through both binaries and asserts agreement — exactly why the
  tusdrender-next-only findings (R1/R2/R4) went unnoticed. Build
  `models/parity-material-uv-subset.usda` (one mesh, two UV sets — `st` full +
  `uvSet1` a `[0,0.25]` sub-tile; a checkerboard bound via `varname="uvSet1"`;
  three `materialBind` GeomSubsets → R/G/B; one DistantLight) + a registered
  harness that renders `tusdview --headless` (Vulkan) and `tusdrender -rtPreview`
  and asserts three distinct colored regions (R1), the zoomed `uvSet1` crop (R2),
  and region-mean agreement (catches R9/R10/T11-class drift going forward).

### Texture / KTX2 (B)

- **Web scene-texture routing:** route real USD scene textures through the
  compressed path in `getTextureFromUSD` (`TinyUSDZLoaderUtils.js` L404-534) —
  needs the tinyusdz WASM to expose uni bytes / link textools. The standalone
  `texcomp` demo already proves the pipeline.
- **Basis Universal / KHR_texture_basisu** transcoder — the "Basis later" seam.
- **Land tinyexr PR #259** (`texpipe-ktx2-zstd-writer`, the KTX2 Zstd writer).

---

## Status snapshot

- **Audit (A): backlog EMPTY** except T12 + the parity oracle above. All other
  findings fixed or refuted, pushed to `tusdview` (latest `79d0c8dcd`). Highlights
  of the last pass: R10 light `normalize` + dome `texture:format`; T8 doubleSided
  on `--next` + VK back-face cull; R12 doubleSided cull in the tracer; **T11 +
  GL↔VK unification** — linear sRGB workflow (sRGB textures upload `_SRGB`,
  `linearToSrgb` OETF on the shaded output; GL adopted VK's derivative-TBN normal
  map, VK adopted GL's soft-headlight diffuse — GL==VK byte-identical now; RT got
  the encode too). T7 (Facing AOV) refuted.
- **Texture (B): all 5 phases + kept-compressed / Zstd / HDR-BC6H follow-ups DONE,
  verified, pushed.** tinyusdz `tusdview` through `fe30d477c`; tinyexr
  `texcomp-ktx2-reader` merged (PR #258), `texpipe-ktx2-zstd-writer` open
  (PR #259). textools/tusdview ctests are re-registered (`textools-*`,
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
> (backlog empty except **T12** — `--next` opacity-texture/displayOpacity/specular
> -workflow/ior — and the **cross-tool parity oracle**) and the KTX2 texture
> program (done + pushed; remaining: web scene-texture routing, Basis interop,
> land tinyexr PR #259). Native build `@build -j16`, gate with `ctest`
> (`DISPLAY=:0` for GPU). Mind the gotchas above — especially the SDK-glslang
> requirement for `raytrace_comp.spv.h`. Pre-push audit + permission every time.
