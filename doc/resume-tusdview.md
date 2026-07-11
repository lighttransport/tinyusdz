# tusdview — remaining work

Tracked companion to the (local, untracked) `resume-tusdview.md` scratch notes.
This file is the durable list: what is still open, why, and what it touches.

Status as of 2026-07-11. The `--next` primvars / texturing / texture-VRAM
workstream is **done and pushed**, including GPU skinning (raster, both backends,
instanced prototypes included) and the large-scene verification (numbers in
[large-scene.md §2.9](large-scene.md)).

Also done since the last revision of this file, and no longer listed below: the
shared material-eval Phase 2 (sphere-light NEE + MIS), per-texture UV-set
selection, the tusdview BLAS-compaction port, `--vram-budget`, raster LOD for
non-instanced meshes (§2.10), and the `-vk` tiled ray dispatch (§2.11 — which
also records why device-local BVH buffers were measured and left OFF).

Done since (2026-07-12): Vulkan-RT skinning without a reconvert, the two
double-counted light contributions in `lightrt-bsdf`, area-sampled rect / disk /
cylinder lights (plus the inverted UsdLux light normal that made them light
nothing), and the TLAS `PREFER_FAST_BUILD` question (measured, rejected —
large-scene.md §2.12).

## Open

### 1. CUDA / HIP skinning

The Vulkan ray tracer now re-poses the retained rest vertices per frame
(`BuildNextRtDeformedVertices`) instead of re-running the converter for every time
code. The CUDA/HIP tracers still take the load-time CPU bake: they read `draw_`
geometry directly and free it once their own BVH is built, and they only render a
one-shot screenshot, where the bake IS the cheapest path. Making them animate
interactively means re-posing into their BVH, not just into `draw_`.

### 2. `lightrt-bsdf` IBL energy

The furnace test (`tool-tusdrender-light-double-count`) pins a white Lambert
surface under a uniform dome. The legacy path lands at 0.95x the dome (correct);
`lightrt-bsdf` lands at **1.32x**. The dome double count is gone — this residual
is the BSDF-mode IBL split-sum path (`EvalMaterialIblDiffuse` / `...Specular`)
handing back more energy than it should. Tighten the test's 1.6 ceiling once
fixed.

### 3. Mesh lights are invisible to the `next` loader

`LightCache::mesh` is only populated by the LEGACY flatten (`tusdr_legacy.cc`).
The next loader — which is what `-rtPreview` uses for `.usda` — never registers an
emissive `MeshLightAPI` mesh as an analytic light, so such a mesh is lit only by
whatever a BSDF bounce happens to hit. Both halves of
`tool-tusdrender-light-double-count` exist because of this split.

### 4. Cross-backend blendshape screenshot parity

Open in the broader visual-parity matrix. Note that no fixture in the tree
animates a blendshape under `--time` in *either* loader (legacy or next), which
is itself worth chasing — `models/blendshape-and-animation-test-001.usda` renders
identically at every time code.

### 5. usd-assets regression harness

Partial: the batch harness runs both tools over usd-wg/assets. Remaining is
recording and maintaining the per-machine external-asset baselines.

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
cd build && make -j16 && ctest --output-on-failure     # 70/70

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
