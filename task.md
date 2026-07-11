# Task: Skinning under ray tracing for tusdview

> Resume prompt. This is the last remaining GPU-skinning gap. Raster-path GPU
> skinning (skeletal + blendshape + node-animated/mixed scenes) is **done and
> committed**; the Vulkan ray-query (RT) path still falls back to the slow CPU
> reconvert. Make skinning efficient under RT.

## Resume prompt (paste to continue)

Make skeletal/blendshape **skinning work efficiently under the Vulkan ray-query
(RT) technique** in the tusdview viewer (`examples/tusdview`). Today, when RT is
active the viewer skins via the per-frame **CPU reconvert** fallback (re-runs
Tydra conversion every frame, re-uploads geometry, rebuilds the BLAS) — correct
but slow. Skin without the Tydra reconvert and update the acceleration structure
per frame instead. Build in `build/` with `ninja -j16`; verify with
`--headless --backend vk --rt --frames N --time T --screenshot out.ppm` and the
parity script (see Verification). Requires a real RT-capable GPU + RT-capable
glslang (already set up here; RTX 3070).

## Where things stand (git: branch `tusdview`)

Recent commits (newest first):
- `d590c08e` GPU skinning for mixed (skeletal + node-animated) scenes
- `0fb8c702` GPU blendshape skinning (remove CPU fallback)
- `629017c5` Add GPU skinning to tusdview (raster bone-texture path, both backends)
- `8d36d658` CPU skeletal + blendshape skinning

Skinning lives in `examples/tusdview/skinning.{hh,cc}` + `app.cc`. Raster GPU
path: rest mesh uploaded once with per-vertex joint idx/weights; per frame
`BuildGpuSkinningFrame` computes `skinMat[j] = inverse(bind)·posedWorld` →
RGBA32F bone texture (`uploadSkinningFrame`); the vertex shader does LBS.
Blendshapes morph rest verts on the CPU and re-upload via
`Renderer::updateMeshVertices`. Mixed scenes update per-mesh worlds via
`Renderer::updateMeshWorld` (`UpdateAnimatedMeshWorlds` → `BuildXformNodeFromStage`).

**The RT veto** (the thing to remove/replace) is in `App::updateSkinningEffective`
(app.cc): `if (renderer_->rayTracingActive()) { skinningReason_ = "ray tracing
uses CPU-skinned BLAS geometry"; return; }` → falls to the CPU reconvert path
(`requestReconvert` → `startReconvertAsync` → `RenderSceneAtTime` →
`finishReconvertIfReady` → `uploadScene`, which `appendMesh`-rebuilds the BLAS).

## Why RT is different

The ray-query shader (`vk/shaders/raytrace.comp`) traces against a **BLAS** built
from each mesh's vbo/ebo — it does NOT run the raster vertex shader, so the
bone-texture GPU skinning never touches RT geometry. To animate RT, the actual
**vertex positions in the vbo must change** and the **BLAS must be updated**.

## Plan

### MVP: skin into the vbo without Tydra reconvert, rebuild BLAS
1. **Eligibility**: in `updateSkinningEffective`, when `rayTracingActive()` and the
   scene is skinnable, pick a new effective mode (e.g. keep `GPU` but flag
   `skinningRtPath_`) instead of vetoing to the CPU reconvert.
2. **Per-frame skin → vbo**: reuse the CPU skinning math already in `skinning.cc`
   (the `BuildGpuSkinningFrame` bounds loop already CPU-skins every vertex with
   `composed[j] = geomBind·skinMat[j]`; factor that into a function returning the
   skinned `DrawVertex` buffer per mesh, including morph offsets + regenerated
   normals — same as the blendshape morph path). Upload via the existing
   `Renderer::updateMeshVertices`. This skips the Tydra reconvert (the expensive
   part); it is CPU skinning but only the cheap deform, not a full convert.
3. **BLAS update**: `updateMeshVertices` currently just memcpys the vbo (raster is
   gated non-RT). For RT, after rewriting the vbo, mark the mesh's BLAS stale and
   rebuild it. Today `buildBlas` uses `MODE_BUILD` + `PREFER_FAST_TRACE`
   (vk_renderer.cc:1564); `rebuildTlas` (1636) builds all BLAS when `tlasDirty_`.
   Simplest: add a `Renderer::refreshSkinnedAccel()` (or set `tlasDirty_` +
   per-mesh `blasDirty`) so the next trace rebuilds BLAS+TLAS from the updated
   vbo. Wire it from `App::updateGpuSkinningFrameIfNeeded` on the RT path.

### Optimization (after MVP works): BLAS refit + GPU-compute skinning
- Build BLAS with `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR` and use
  `MODE_UPDATE` (refit) instead of full rebuild when only vertices moved
  (topology unchanged) — much cheaper per frame.
- Optionally move skinning to a **GPU compute pass** that writes skinned vertices
  into the vbo (reusing the bone-matrix texture + joint attrs), so the CPU never
  touches per-vertex data. Then refit the BLAS from the GPU-written vbo.

## Key references
- RT BLAS/TLAS: `vk/vk_renderer.cc` `buildBlas` (1564, `MODE_BUILD`,
  `PREFER_FAST_TRACE`), `rebuildTlas` (1636, `tlasDirty_` gate, trace-time at
  2159), `appendMesh` BLAS-input usage (~1500). RT toggle / `rayTracingActive()`
  in renderer.hh + vk_renderer; `--rt` in main.cc.
- Skinning math: `skinning.cc` `BuildSkinningMatrices` (skinMat = inverse(bind)·
  posedWorld), `BuildGpuSkinningFrame` (the per-vertex CPU skin in its bounds
  loop is exactly the deform to reuse), morph path (`MorphTargetCPU`,
  `RegenNormalsOriented`). `Renderer::updateMeshVertices` (GL/VK, already exists).
- `App::updateSkinningEffective` / `updateGpuSkinningFrameIfNeeded` (app.cc) —
  eligibility + per-frame driver. `lastRtActiveForSkinning_` already tracks RT
  state so toggling `--rt` / View ▸ Ray tracing re-evaluates the path.
- The current CPU-under-RT fallback works (just slow): `RenderSceneAtTime` +
  `finishReconvertIfReady` + `uploadScene` rebuilds geometry+BLAS each frame.
  Keep it as the correctness oracle for parity.

## Verification
- Build: `cd build && ninja -j16`; `ctest` stays 23/23.
- RT animates: `build/tusdview --headless --backend vk --rt --frames 4 --time 12
  --screenshot rt12.ppm models/skintest-animated.usda` differs from `--time 1`
  (skinning visible under RT). Compare RT-GPU vs RT-CPU-fallback screenshots —
  should match closely (same geometry, RT shading). The parity harness
  `tests/tusdview/compare-skinning-screenshots.py` runs windowed via xvfb; add an
  RT variant or compare `--skinning gpu`(new RT path) vs `--skinning cpu` with
  `--rt`.
- Performance: the RT skinned path should not run the Tydra reconvert each frame
  (check it is not calling `RenderSceneAtTime` on the RT skinning path).
- Test assets: `models/skintest-animated.usda` (skeletal),
  `models/blendshape-and-animation-test-001.usda` (skel+blendshape). For a mixed
  RT case, a skinned asset with an animated SkelRoot translate (the phase used
  `skintest-animated` with `xformOp:translate.timeSamples` 1→(0,0,0),
  48→(4,0,0)).

## Notes / gotchas (learned in the raster phase)
- `points` is `std::vector<vec3>` (TYDRA_USE_CHUNKED_ARRAY OFF). Sampler `times`
  are **time codes**, not seconds. Matrix `a*b` applies `a` first (row-vector).
- VK per-frame buffer/AS updates here use `vkDeviceWaitIdle` (see
  `uploadSkinningFrame`, `updateMeshVertices`) — simple and safe for a viewer.
- Camera framing must use the **posed** bounds: `applyLoaded` poses the GPU frame
  after upload, then fits the camera (don't reintroduce a rest-bounds fit).
