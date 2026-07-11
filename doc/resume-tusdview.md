# tusdview — remaining work

Tracked companion to the (local, untracked) `resume-tusdview.md` scratch notes.
This file is the durable list: what is still open, why, and what it touches.

Status as of 2026-07-11. The `--next` primvars / texturing / texture-VRAM
workstream is **done and pushed**, including GPU skinning (raster, both
backends, instanced prototypes included) and the large-scene verification
(numbers in [large-scene.md §2.9](large-scene.md)).

## Open — in progress

### 1. Shared material-evaluation layer, Phase 2 (MIS / next-event estimation)

See [shared-material-eval-scope.md](shared-material-eval-scope.md). Phase 1
landed: a shared OpenPBR parameter block (`src/tydra/openpbr-params.hh`) and a
`tydra::next::RenderMaterial -> LightRtOpenPBRParams` converter, so tusdview,
tusdrender and lightrt evaluate one material description instead of four.

Remaining: MIS / next-event weighting around the `bsdf_sample` continuation
path, then broaden golden coverage for the experimental mode
(`-materialShading lightrt-bsdf`). Regression anchor:
`tool-tusdrender-degraded-material`.

### 3. Per-texture UV-set selection

`RenderTexture::uv_primvar` can name a *secondary* UV set, but nothing can route
it: `uv1` exists only as a debug AOV (render mode 31). Sampling it needs a
`uvSet` field on `DrawTexSampleCPU`, plus material-buffer and shader changes
across GL / Vulkan / RT.

The `st` / `UVMap` / `uv` / `st0` / `map1` fallback chain already covers the
common single-UV-set case, so this only bites multi-UV-set assets (lightmaps,
decal sets).

### 5. P2 VRAM follow-ons

From the §2.8 review in [large-scene.md](large-scene.md), all unimplemented:

- **tusdview BLAS-compaction port** — the lightrt fork compacts by default
  (caldera `-vkInstanced` 1051→525 MiB; island 7153→5408 MiB). tusdview's own RT
  path does not: island RT sits at 4.2 GiB, estimated ~2.7 GiB compacted. Best
  value of the five.
- **Device-local node/block buffers** for the `-vk` compute path.
- **Tiled ray/hit dispatch.**
- **TLAS `PREFER_FAST_BUILD`** for settle rebuilds.
- **`--vram-budget <GiB>` umbrella flag** deriving `--max-gpu-mem`, texture
  budgets and auto `--rt-lod` from the memory-budget query (today each is a
  separate knob).
- ~~**Raster LOD for non-instanced meshes**~~ — DONE. Island `--raster-lod` now
  draws 15.2 M tris instead of 40.3 M (84 400 of 100 801 meshes; the rest are
  sub-pixel-culled or collapsed to box proxies). The prerequisite turned out to
  be a loader bug: the next static-batch path handed every non-instanced mesh the
  running *scene*-bounds accumulator as its AABB, so no unique mesh could ever be
  outside the frustum or small on screen — the per-mesh frustum cull was a no-op
  too. Regression test `tusdview-noninstanced-lod`.

## Open — not started

### Geometry tangents — DORMANT BY DECISION (2026-07-11), do not re-litigate

`FillFlatGeometry` already resolves `m.tangents` through the weld, but
`MeshConfig::compute_tangents` defaults false, so tangents are always empty and
cost nothing.

Do not build without asking. GL, Vulkan and RT all derive the TBN from
screen-space UV derivatives (`vk/shaders/mesh.frag`, `raytrace.comp`), so normal
maps render correctly today. Real tangents only buy correct handedness on
mirrored/seamed UVs, cost ~16 B/vertex against a memory-reduction goal, and
would need per-mesh gating (a second converter pass for meshes whose bound
material has a normal map, i.e. resolving the material *before* conversion),
tangent world-transform in the batch-append path, a new VBO/binding, and shader
edits in `gl/gl_renderer.cc` + `vk/vk_renderer.cc` + `vk/shaders/mesh.{vert,frag}`
— `mesh.frag` being the file the separate sophisticated-texturing branch owns.

### RT/CUDA/HIP skinning

The last `--next` skinning gap. Raster (GL + Vulkan, instanced prototypes
included) skins on the GPU; the ray-tracing backends still take the load-time
CPU bake, so every new time code costs a full re-bake.

### Cross-backend blendshape screenshot parity

Open in the broader visual-parity matrix.

### usd-assets regression harness

Partial: the batch harness runs both tools over usd-wg/assets. Remaining is
recording and maintaining the per-machine external-asset baselines.

## Verification

```bash
cd build && make -j16 && ctest --output-on-failure     # 46/46

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
