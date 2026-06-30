# tusdrender — GPU backend testing (Vulkan / Direct3D 11 / HIP)

`tusdrender` (CPU preview ray-tracer, see
[`tools/tusdrender/README.md`](../tools/tusdrender/README.md)) has several optional
**GPU compute backends** that traverse the same LightRT BVH:

- **Vulkan** (`-vk` / `-vkr`) — `src/external/lightrt/lightrt_c_vk.c` +
  `lightrt_vkew.c`, driven by `tusdr_vulkan.cc`. Runtime-loaded via vkew (no
  Vulkan SDK to build), on by default (`-DTUSDRENDER_WITH_VULKAN=ON`). `-vkr`
  uses hardware ray tracing when present, else falls back to compute trace.
- **HIP/ROCm** (`-hip`) — `src/external/lightrt/lightrt_c_hip.c` +
  `src/external/hipew/hipew.c`, driven by `tusdr_hip.cc`. Runtime-loaded via
  hipew (opens `libamdhip64` + `libhiprtc`; no ROCm SDK to build), on by default
  (`-DTUSDRENDER_WITH_HIP=ON`, non-Windows). The trace kernel
  (`src/external/lightrt/hip/trace_bvh_hip.h`) is a **direct port of
  `trace_bvh.comp`** compiled at runtime with hiprtc, so it traverses the
  serialized LRTS BVH bit-for-bit identically to the Vulkan compute path
  (`-hip` ≈ `-vk`). Verified on AMD Radeon RX 9070 XT (gfx1201).
- **Direct3D 11** (`-d3d`) — `src/external/lightrt/lightrt_c_d3d11.cpp`, driven
  by `tusdr_d3d.cc`. Windows-only, on by default
  (`-DTUSDRENDER_WITH_D3D11=ON`); d3d11/dxgi/d3dcompiler ship with the OS, so no
  SDK is needed. Its compute shader (`d3d/shaders/trace_bvh.hlsl`) is
  **decompiled from the Vulkan SPIR-V with SPIRV-Cross**, so it traverses the
  BVH bit-for-bit identically; it just runs through the (much more mature on
  older AMD cards) D3D driver. Unlike the Vulkan helper it batches the whole
  frame's rays into **one** dispatch.

```sh
# CPU reference (always correct):
tusdrender models/suzanne-pbr.usda cpu.png -rtPreview -w 320 -height 240 -autoframe
# Vulkan compute trace:
tusdrender models/suzanne-pbr.usda vk.png  -vk  -w 320 -height 240 -autoframe
# Vulkan hardware ray query (RDNA2+; else falls back to compute trace):
tusdrender models/suzanne-pbr.usda vkr.png -vkr -w 320 -height 240 -autoframe
# HIP/ROCm compute trace (AMD; kernel compiled at runtime via hiprtc):
tusdrender models/suzanne-pbr.usda hip.png -hip -w 320 -height 240 -autoframe
# Direct3D 11 compute trace (Windows):
tusdrender models/suzanne-pbr.usda d3d.png -d3d -w 320 -height 240 -autoframe
```

A correct GPU image must match the `-rtPreview` reference (same framed mesh).
The startup log prints the chosen device + path, e.g. `backend: LightRT VK
(compute trace)` / `LightRT D3D11 (compute trace, N rays in 1 dispatch)`.

## Fixed (was: "GPU backends mis-render") — 2026-06-28

The `-vk` / `-vkr` (and `-d3d`) backends previously mis-rendered — `-vk` a
**sparse, holey silhouette**, `-vkr` **fully blank** — while the CPU `-rtPreview`
was correct. This was first suspected to be an AMD **Radeon RX 570** / amdvlk
GCN/Polaris driver bug. Retesting on an NVIDIA **GeForce RTX 5060 Ti** (Linux,
driver `610.43.02`, Vulkan 1.4) reproduced the *exact same* symptoms — which
ruled out a vendor driver and pinned the fault to **CPU-side geometry/setup in
`tusdrender` itself**, not the GPU trace. Root causes (all now fixed):

1. **Polygons were not triangulated.** The GPU geometry collector in
   `tusdrender.cc` copied `faceVertexIndices` verbatim and the backends chunked
   it into groups of three. Suzanne is **468 quads + 32 tris**, so chunking the
   1968 indices by 3 produced 656 scrambled triangles instead of the 968 a proper
   fan-triangulation yields — hence the holes/sparseness. Fixed by triangulating
   with `faceVertexCounts` once, at collection (so `-vk`, `-vkr` **and** `-d3d`
   all get correct geometry).
2. **`-vkr` was fed the wrong vertices.** The ray-query BLAS expects a de-indexed
   triangle soup (9 floats/tri, `VK_INDEX_TYPE_NONE`), but `tusdr_vulkan.cc`
   passed the *indexed* unique-vertex array — over-reading and building garbage
   geometry, so every ray missed → blank. Fixed by expanding the indexed mesh to
   a soup before `lrt_vk_rtx_scene_build`.
3. **One GPU round-trip per pixel** (and, on `-vkr`, an acceleration-structure
   rebuild per pixel) made it minutes-slow at 320×240. Fixed by generating all
   primary rays and tracing the whole frame in **one batched dispatch** (build the
   AS once); 320×240 now finishes in well under a second.
4. **The GPU dispatch used a different camera** (the tilted auto-fit
   `MakeCameraFrame`) than `-rtPreview` (the `MakeUsdRecordCamera` record camera),
   so the same scene framed differently. Fixed by resolving the camera through the
   same path as `ResolveCameraNext`.

Verified on the NVIDIA RTX 5060 Ti: `-vk` and `-vkr` now render the solid,
correctly-framed Suzanne (968 triangles, same dimensions as `-rtPreview`), and
the two paths agree to the byte (compute trace vs hardware ray query). The
`tool-tusdrender-smoke` ctest now asserts this — matching triangle count vs the
CPU reference, non-blank, matching dimensions, and `-vk` ≈ `-vkr` — see
`tools/tusdrender/check_tusdrender_smoke.py` (`check_vulkan`). The GPU path
flat-shades with geometric normals, so it is darker than the CPU image; that is a
shading-model difference, not a geometry error.

```sh
# A correct GPU image now matches the -rtPreview framing/silhouette:
tusdrender scene.usda cpu.png -rtPreview -w 320 -height 240 -autoframe
tusdrender scene.usda vk.png  -vk        -w 320 -height 240 -autoframe
tusdrender scene.usda vkr.png -vkr       -w 320 -height 240 -autoframe
```

### Debugging Vulkan with the Khronos validation layers

If a future change regresses the Vulkan path, the **Khronos validation layers**
give a definitive reason. Build/run them with the helper added for tusdview
(`scripts/setup-vulkan-validation.ps1 -FromSource` on Windows, or build from
source on Linux — see [`doc/tusdview.md`](tusdview.md)); the layer works for any
Vulkan app:

```sh
VK_LAYER_PATH=/path/to/Vulkan-ValidationLayers/build/layers \
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  tusdrender scene.usda vk.png -vk -w 320 -height 240 -autoframe 2>&1 |
  grep -E 'VUID|Validation'
```

Note the layer output (and tusdrender's own diagnostics) goes to stderr/stdout;
capture both streams.

## Follow-on: per-instance LOD on the `-vk` / `-vkr` GPU backends

The CPU two-level TLAS path supports per-instance view-dependent LOD (`-rtLod`:
distant prototypes → shared box proxy, sub-pixel → cull; see the tusdrender
README and `tusdr_rt_lod.{hh,cc}`). The `-vk` / `-vkr` GPU backends do **not** yet,
because they consume a different geometry representation:

- `RunRTPreviewNext` (CPU) builds a **two-level** structure — one BLAS per
  prototype placed by a LightRT TLAS over an `InstanceRT` list (`tusdr_next.cc`
  `ExtractAndBuildBVH`). `-rtLod` hooks the TLAS instance-fill loop: it classifies
  each placement, drops Culled ones (`valid[i]=0`), and rewrites Proxy ones to a
  shared box BLAS via `BoxFitO2W`.
- `RunVulkanLightRT` / the `-vk`/`-vkr` path build a **single, fully
  world-space-flattened** indexed triangle BLAS (`CollectRTPreviewMeshesNext` in
  `tusdrender.cc` bakes every prim to world space) and trace one flat scene. There
  is no instance list / TLAS to attach per-instance LOD to.

To bring `-rtLod` to `-vkr`, the GPU path must first become two-level:

1. Reuse the CPU collectors (`CollectSceneSplit` / `CollectPointInstancer`) to
   produce the prototype BLAS set + `InstanceRT` list instead of the flattened
   soup, building per-prototype `lrt_vk` BLAS and a GPU TLAS
   (`lrt_vk_rtx_*` / `lrt_tlas_*` GPU equivalents).
2. Run the **same** `tusdr_rt_lod` selection (it is pure and Vulkan-free) at GPU
   TLAS build, emitting the Full/Proxy/Cull instance set + the shared box BLAS.
3. The shader hit path must resolve a two-level (instance → BLAS) hit, matching the
   CPU `ResolveTLASHit`, instead of the current flat-BLAS hit.

This is a larger change (a GPU two-level build + shader hit-resolution rework),
tracked separately from the CPU `-rtLod` landing. The selection logic itself is
already shared-ready (`tusdr_rt_lod` has no GPU/Vulkan dependency).
