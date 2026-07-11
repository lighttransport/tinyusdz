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

## Open

### 1. RT / CUDA / HIP skinning

The last `--next` skinning gap, and the biggest item here. Raster (GL + Vulkan,
instanced prototypes included) skins on the GPU; the ray-tracing backends still
take the load-time CPU bake, so every new time code costs a full re-bake of the
posed geometry.

### 2. `lightrt-bsdf`: two double-counted light contributions

Known, unfixed, from the NEE work:

- the dome light is counted twice, and
- emissive meshes are counted twice.

Both need `Shade()` split into an emitted term and a reflected term; today it is
a single analytic direct-lighting evaluator, so the BSDF-side and light-side
contributions overlap. Regression anchors: `tool-tusdrender-sphere-light-nee`,
`tool-tusdrender-material-shading-bsdf-sample`.

### 3. Rect / disk / cylinder lights are still punctual

Only sphere lights are area-sampled (cone sampling + power-heuristic MIS). The
others collapse to a point, so they cast hard shadows regardless of size. Needs
tangent axes plumbed through `PreviewLight`, then a per-shape sampler and pdf.

### 4. TLAS `PREFER_FAST_BUILD` for settle rebuilds

The last P2 VRAM follow-on from the [large-scene.md](large-scene.md) §2.8 review,
and the lowest-value one: deprioritized once BLAS compaction and RT LOD landed.

### 5. Cross-backend blendshape screenshot parity

Open in the broader visual-parity matrix.

### 6. usd-assets regression harness

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
