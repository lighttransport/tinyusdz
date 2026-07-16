# Tasks — resume hub

Fresh-context resume prompts for the open `tinyusdz` workstreams. Pick one,
paste its **Resume prompt**, and go. Newest completion at top.

---

## ✅ Recently completed (don't re-open)

- **refactor-next M3 (pcp hot-map perf) — DONE** (branch `dev`, 2026-07-16).
  Landed as open-addressed caches/memos (the full u32 rekeying stayed
  unnecessary): `StackSpecCache` per-stack Specs memo, self-resetting
  `Src::specs_`/`SpecsFor`, `SrcCache` for sources_cache, and a new
  `arc_target_memo_` (per-arc external-target resolution keyed by
  anchor+asset+expr-vars **fingerprint** — pointer identity never hits).
  Island/ALab/Caldera load+compose **−14–16%**, byte-identical (4 scenes),
  parallel==serial, TSan-clean; gates 36/36 + 37/37. Remaining compose cost is
  first-load layer parsing (parallelized by `--compose-threads`), not maps —
  the pcp string-map lever is exhausted on dev. Detail:
  `doc/memory-and-performance.md` §M3.

- **refactor-next Phase-5 leftovers — DONE** (branch `dev`, 2026-07-16).
  Exploration found most once-open Phase-5 items already landed (I1 instance-key
  variant-sel+offset, I2 tri-state `instanceable`, I4 PointInstancer compute API,
  M5 GraftSubtree child-walk — the `memory-and-performance.md` "reverted" note was
  stale). Closed the one genuine gap: **I3 instance↔prototype path-translation
  API** (`Cache::TranslatePathToPrototype`/`TranslatePathFromPrototype` in
  `src/next/pcp/cache.{hh,cc}`, with nested-instance recursion) + nested-instancing
  tests (`test_pcp.cc:test_path_translation`, two-level rewrite) + PointInstancer
  `ComputeMaskAtTime`. Docs reconciled (`doc/refator-next.md`,
  `doc/memory-and-performance.md`). Gates: build-next **36/36**, build **37/37**.
  (M3, then still deferred, landed the same day — see the entry above.)

- **next-vs-pxr flatten-differential burn-down — DONE** (branch `dev`, 2026-07-16).
  All untagged composition divergences fixed; gate **756 pass / 0 untagged xfail /
  0 FAIL** of 798 (build-next 36/36, build 37/37). 13 cases fixed this campaign;
  the last arc (this session) landed relocate/inherit/variant/specialize cases —
  see commits `65272912a`, `b2ff45f49`, `07227930c` and `doc/next-pcp-relocate-remaining.md`.
  The 26 remaining xfail entries are all `INTENTIONAL:`/`ORACLE-` tagged (pxr
  nondeterminism / known non-bugs), not defects. Detail in memory `aousd-pxr-diff-gates`.

---

## Open workstream A — Skinning under ray tracing (tusdview)  [PRIMARY]

> Branch `tusdview`. Raster-path GPU skinning (skeletal + blendshape +
> node-animated/mixed) is **done and committed**; the Vulkan ray-query (RT) path
> still falls back to the slow per-frame CPU reconvert. Make skinning efficient
> under RT. Requires a real RT-capable GPU + RT glslang (RTX 3070 here).

### Resume prompt (paste to continue)

Make skeletal/blendshape **skinning work efficiently under the Vulkan ray-query
(RT) technique** in the tusdview viewer (`examples/tusdview`). Today, when RT is
active the viewer skins via the per-frame **CPU reconvert** fallback (re-runs
Tydra conversion every frame, re-uploads geometry, rebuilds the BLAS) — correct
but slow. Skin without the Tydra reconvert and update the acceleration structure
per frame instead. Build in `build/` with `ninja -j16`; verify headless with
`--backend vk --rt --frames N --time T --screenshot out.ppm` and the parity
script (see Verification).

**The RT veto to remove/replace** is in `App::updateSkinningEffective` (app.cc,
~L549): `if (renderer_->rayTracingActive()) { skinningReason_ = "ray tracing uses
CPU-skinned BLAS geometry"; return; }` → falls to the CPU reconvert path
(`requestReconvert` → `startReconvertAsync` → `RenderSceneAtTime` →
`finishReconvertIfReady` → `uploadScene`, which `appendMesh`-rebuilds the BLAS).

**Why RT differs:** the ray-query shader (`vk/shaders/raytrace.comp`) traces a
**BLAS** built from each mesh's vbo/ebo — it does NOT run the raster vertex
shader, so bone-texture GPU skinning never touches RT geometry. To animate RT,
the vbo positions must change AND the BLAS must be updated.

### Plan

**MVP — skin into the vbo without Tydra reconvert, rebuild BLAS**
1. **Eligibility**: in `updateSkinningEffective`, when `rayTracingActive()` and the
   scene is skinnable, pick a new effective mode (keep `GPU`, flag `skinningRtPath_`)
   instead of vetoing to the CPU reconvert.
2. **Per-frame skin → vbo**: reuse the CPU skinning math in `skinning.cc` — the
   `BuildGpuSkinningFrame` bounds loop already CPU-skins every vertex
   (`composed[j] = geomBind·skinMat[j]`); factor it into a function returning the
   skinned `DrawVertex` buffer per mesh (incl. morph offsets + regenerated
   normals, same as the blendshape morph path). Upload via existing
   `Renderer::updateMeshVertices`. Skips the reconvert (the expensive part).
3. **BLAS update**: after rewriting the vbo, mark the mesh's BLAS stale and
   rebuild. `buildBlas` uses `MODE_BUILD`+`PREFER_FAST_TRACE` (vk_renderer.cc:1564);
   `rebuildTlas` (1636) rebuilds all BLAS on `tlasDirty_`. Add a
   `Renderer::refreshSkinnedAccel()` (or set `tlasDirty_` + per-mesh `blasDirty`)
   and wire it from `App::updateGpuSkinningFrameIfNeeded` on the RT path.

**Optimization (after MVP): BLAS refit + GPU-compute skinning**
- Build BLAS with `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR` and use
  `MODE_UPDATE` (refit) when only vertices moved (topology unchanged).
- Move skinning to a **GPU compute pass** writing skinned verts into the vbo
  (reuse bone-matrix texture + joint attrs), then refit BLAS from GPU-written vbo.

### Key references
- RT AS: `vk/vk_renderer.cc` `buildBlas` (1564), `rebuildTlas` (1636, trace-time
  ~2159), `appendMesh` BLAS input (~1500); `rayTracingActive()` in renderer.hh +
  vk_renderer; `--rt` in main.cc.
- Skinning: `skinning.cc` `BuildSkinningMatrices` (skinMat = inverse(bind)·posedWorld),
  `BuildGpuSkinningFrame` (per-vertex CPU skin to reuse), morph path
  (`MorphTargetCPU`, `RegenNormalsOriented`). `Renderer::updateMeshVertices` exists.
- `App::updateSkinningEffective` / `updateGpuSkinningFrameIfNeeded` (app.cc);
  `lastRtActiveForSkinning_` tracks RT state so toggling re-evaluates the path.
- CPU-under-RT fallback is the correctness oracle (`RenderSceneAtTime` +
  `finishReconvertIfReady` + `uploadScene`).

### Verification
- Build `cd build && ninja -j16`; `ctest` stays 23/23.
- RT animates: `build/tusdview --headless --backend vk --rt --frames 4 --time 12
  --screenshot rt12.ppm models/skintest-animated.usda` differs from `--time 1`.
  Compare RT-GPU vs RT-CPU-fallback screenshots — should match closely.
- Parity harness `tests/tusdview/compare-skinning-screenshots.py` (windowed via
  xvfb); add an RT variant or compare new RT `--skinning gpu` vs `--skinning cpu --rt`.
- Perf: the RT skinned path must NOT call `RenderSceneAtTime` each frame.
- Assets: `models/skintest-animated.usda` (skeletal),
  `models/blendshape-and-animation-test-001.usda` (skel+blendshape); mixed RT case
  = skinned asset with animated SkelRoot translate.

### Gotchas (from the raster phase)
- `points` is `std::vector<vec3>` (TYDRA_USE_CHUNKED_ARRAY OFF). Sampler `times`
  are **time codes**, not seconds. Matrix `a*b` applies `a` first (row-vector).
- VK per-frame buffer/AS updates use `vkDeviceWaitIdle` (`uploadSkinningFrame`,
  `updateMeshVertices`) — simple/safe for a viewer.
- Camera framing must use the **posed** bounds (`applyLoaded` poses then fits).

---

---

## Open workstream C — intentional-tag triage (flatten differential)  [LOW]

### Resume prompt
Audit the 26 `INTENTIONAL:`/`ORACLE-` tagged entries in
`tests/next/next-pxr-flatten-xfail.txt` to see whether any were mis-tagged and are
now fixable (the untagged burn-down is complete, so the tagged set is the only
remaining differential surface). For each, re-derive pxr's pcp.txt prim stack and
confirm the divergence is genuinely pxr-nondeterminism / oracle-limited vs. a real
next bug. Reclassify or fix accordingly; keep the tag taxonomy from memory
`aousd-pxr-diff-gates`. Hard bar: gate stays ≥756 pass / 0 untagged / 0 FAIL,
build-next 36/36, build 37/37.
