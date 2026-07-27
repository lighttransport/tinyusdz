# Shared material-evaluation layer — scope

Scoping doc for resume-tusdview.md §9's headline item: *"Move toward a shared
material-evaluation layer for tusdview and tusdrender."* This is the largest
single effort in the remaining work. This document maps the current divergent
state, proposes a target architecture, and lays out a phased, individually-
shippable migration plan with verification hooks.

## Why now

The §1 golden-fingerprint harness (`tusdview-models-golden`) and the §9
cross-backend geometry-parity harness (`tusdview-backend-parity`) that already
landed give this refactor a **verification loop**: every phase below can be
checked for per-model visual regression and cross-backend geometry parity. That
is what makes a material-eval unification tractable rather than open-ended.

## Current state — four parallel material stories

There is no single material path. Four representations, ~4 resolvers, and ~6–7
BRDFs coexist.

### Material struct families (4)

| Struct | Where | Fidelity |
|---|---|---|
| `tydra::RenderMaterial` (`surfaceShader` + `openPBRShader` + `nodeGraphJson`) | `src/tydra/render-data-shader.hh:359` | **Richest** — full MaterialX OpenPBR superset |
| `DrawMaterialCPU` + `tydra::LightRtOpenPBRParams` | `examples/tusdview/gpu_scene.hh:324` / `:285` | Full OpenPBR (the baked block) |
| `TriMat` / `ResolvedMat` / `MeshJobNext` | `tools/tusdrender/tusdr_math.hh:346` | **UsdPreviewSurface-level** — no transmission / subsurface / sheen / thin-film |
| `OpenPBRParams` (C) | `src/external/lightrt/mtlxrender/mtlx_eval.h:15` | Full OpenPBR — the BSDF's input contract |

`tydra::LightRtOpenPBRParams` is a near 1:1 C++ mirror of the C `OpenPBRParams`. These
two are the de-facto OpenPBR interchange format — but only tusdview populates
them.

### Resolvers: "USD → shading inputs" (4 independent)

1. **tusdview / tydra** (default): `BuildDrawMaterials` (`mesh_build.cc:1050`) →
   `BakeLightRtOpenPBR` (`lightrt_mtlx_bridge.cc:914`), which for a MaterialX
   graph calls the **real lightrt evaluator** `mtlx_eval_surface`
   (`lightrt_mtlx_bridge.cc:895`).
2. **tusdview / next**: displayColor-only inline (`next_scene_loader.cc:174-190`).
   A geometry-preview path — one gray default material, vertex colors. Distinct
   by design (speed/memory over fidelity).
3. **tusdrender / next** (DEFAULT): `ResolveMeshMaterialNext`
   (`tusdr_next.cc:837`) — a **hand-rolled UsdPreviewSurface resolver** on the
   `tinyusdz::next` API that reads `inputs:diffuseColor` / `inputs:roughness` /
   … directly. **Never touches `RenderMaterial`.** This is the deepest
   divergence and the reason tusdrender cannot do OpenPBR.
4. **tusdrender / legacy**: `RenderSceneConverter::ConvertToRenderScene` →
   `RenderMaterial` accessors (`tusdr_material.cc`, `tusdr_legacy.cc`) — but only
   pulls a few fields into the same slim `TriMat`.

### BRDFs (6–7 independent)

- tusdview: GL GLSL (`common/light3d/material.cpp`), VK raster
  (`vk/shaders/mesh.frag`), VK ray-query (`vk/shaders/raytrace.comp`), CUDA/HIP
  (`raytracer_kernel.inc`) = **4**. Raster reads legacy scalar slots; RT/CUDA/HIP
  read the **baked** OpenPBR constants (not a live BSDF), ad-hoc coat/spec only.
- tusdrender: full IBL/GGX path-tracer `Shade()` (`tusdr_integrator.cc:522` +
  `tusdr_lighting.cc`) and a flat headlight GPU-hit shader `ShadeAndWrite*`
  (`tusdr_gpu_common.cc:287`) = **2**.
- lightrt `bsdf.c` (`bsdf_sample`/`bsdf_eval`) = **1** genuine full OpenPBR BSDF —
  **but used only to bake constants in tusdview; no backend shades with it, and
  tusdrender does not even compile it** (`tools/tusdrender/CMakeLists.txt` links
  `lightrt_c` traversal only, not `mtlxrender/`).

### What is already shared

- The LightRT ray-**traversal** core (`lightrt_c*.c`) — both tools' RT paths.
- `tydra::RenderMaterial` is *available* to both; both *legacy* paths use it, but
  both *default* paths bypass it.

## Target architecture

One representation, one resolver, one reference BSDF; per-backend shaders reduced
to a shared **param contract + math**, since GLSL / CUDA / C cannot literally
share source.

```
USD ──► tydra RenderMaterial (canonical, OpenPBR) ──► Shared OpenPBR param block
                                                        (promote OpenPBRParams /
                                                         tydra::LightRtOpenPBRParams to
                                                         a shared header)
                                                              │
                        ┌─────────────────────────────────────┼───────────────────┐
                        ▼                                       ▼                   ▼
             lightrt bsdf.c (shared CPU/RT BSDF)      GLSL BRDF (raster/VK)   CUDA/HIP BRDF
             ← tusdview RT + tusdrender CPU            ← same param contract   ← same contract
```

The natural seam is **between "material resolve" and struct population**: today 4
resolvers feed 4 struct families. Collapse to one resolver
(`RenderMaterial → shared param block`) feeding one struct, consumed by all
backends.

## Phased plan (each phase ships + verifies independently)

### Phase 0 — Promote the shared OpenPBR param block (low risk)
Move `OpenPBRParams` / `tydra::LightRtOpenPBRParams` into a shared header both tools
include. No behavior change; both tools compile against one struct.
- Verify: builds (ON + `TEXTOOLS=OFF`), all ctests unchanged.

### Phase 1 — Unify the resolver (high value, medium risk)
Make **tusdrender's next path consume `RenderMaterial`** instead of
`ResolveMeshMaterialNext`. Candidate seam already exists:
`src/tydra/next/render-converter.hh` (`RenderSceneConverter`) and
`ConvertToRenderMaterial` (`src/tydra/next/materialx.hh:181`). Fill the shared
param block from `RenderMaterial` (reuse `BakeLightRtOpenPBR`'s mapping, lifted
into a shared function). tusdrender gains OpenPBR (transmission/subsurface/
sheen/thin-film) for free.
- **Risk**: the tydra-next material converter was noted as *less complete*
  (falls back to gray for non-local graphs) — tusdrender-next hand-rolls for a
  reason. Validate it covers the hand-rolled resolver's cases (UDIM, wrap modes,
  scalar-texture channels, displacement) before deleting the old path; keep both
  behind a flag during bring-up.
- **Risk**: `TriMat` is deliberately slim (memory-optimized side table for huge
  scenes). Don't bloat it — carry the slim form in the BLAS, expand to the shared
  block at shade time, or gate the full block on non-huge scenes.
- Verify: `tusdview-backend-parity` (geometry unchanged), `tusdview-models-golden`,
  `tool-tusdrender-*`, the usd-assets smoke buckets.

### Phase 2 — Shared CPU/RT BSDF (medium)
Compile `mtlxrender/bsdf.c` into tusdrender and route `Shade()`
(`tusdr_integrator.cc:522`) through `bsdf_eval`/`bsdf_sample`. tusdview's RT path
can likewise shade live from the block instead of the ad-hoc baked-constant
preview. One BRDF for both tools' CPU/RT.
- Verify: golden + parity; expect *intended* shading shifts → re-record goldens.

### Phase 3 — GPU BRDF alignment (large, lower priority)
GLSL (raster/VK-RT) and CUDA/HIP can't share `bsdf.c` source. Align them to the
same OpenPBR param contract + math (port the lobes, or codegen from one
definition). Reduces divergence; full pixel parity across GPU/CPU is not the
goal — geometry parity + bounded visual golden is.

## Effort & sequencing

- Phase 0: small. Phase 1: the crux — medium, gated bring-up. Phase 2: medium.
  Phase 3: large, incremental, optional.
- Ship 0→1→2 in order; each is independently valuable (1 alone gives tusdrender
  OpenPBR + deletes a whole resolver). Phase 3 per-backend as capacity allows.

## Non-goals / explicitly out of scope

- Byte-identical cross-backend pixels (measured infeasible — see §9 parity
  findings; legitimate lighting/tone differences of L1 16–3200).
- Unifying the tusdview-next displayColor-only preview into the full path — it is
  a deliberate speed/memory trade-off.
- Spectral (LTE) rendering, carried by `RenderMaterial` but consumed nowhere yet.

## First concrete step

Phase 0 + a spike of Phase 1 on **one** asset: run tusdrender-next through
`tydra::next::RenderSceneConverter → RenderMaterial → shared block` behind a flag,
and diff against the hand-rolled resolver on the curated models via
`tusdview-backend-parity` + tusdrender smoke. That measures the tydra-next
converter's real coverage gap before committing to deleting `ResolveMeshMaterialNext`.
