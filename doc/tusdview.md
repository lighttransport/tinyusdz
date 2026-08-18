# tusdview — build & GPU debugging

`tusdview` is the interactive OpenGL/Vulkan USD viewer example
(`examples/tusdview/`). This page covers building it and the Vulkan
**validation-layer** workflow used to debug the GPU/threaded paths. For the
feature set, CLI flags, config and MCP server, see
[`examples/tusdview/README.md`](../examples/tusdview/README.md); for the
render-thread design see
[`examples/tusdview/doc/threading-stage2.md`](../examples/tusdview/doc/threading-stage2.md).

## Build

GUI deps (OpenGL/GLFW, optionally Vulkan) are opt-in so headless CI is
unaffected:

```sh
cmake -S . -B build_ninja -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTINYUSDZ_BUILD_EXAMPLES=ON -DTINYUSDZ_WITH_TYDRA=ON \
  -DTINYUSDZ_BUILD_GUI_VIEWER=ON
cmake --build build_ninja -j16 --target tusdview
```

- **Vulkan** is ON by default (`-DTUSDVIEW_WITH_VULKAN=ON`) and needs **no Vulkan
  SDK, no `libvulkan-dev`, and no `glslangValidator`**: the API is resolved at
  runtime via the vendored **volk** meta-loader (`dlopen`), the headers are
  vendored (`examples/common/vulkan-headers`), and the shaders ship as committed
  **embedded SPIR-V** (`vk/shaders/embedded/*.spv.h`). Only the runtime Vulkan
  loader (`libvulkan.so.1`) + an ICD are needed to *run*. Look for
  `tusdview: Vulkan backend ENABLED (... embedded SPIR-V, ray query ON)` in the
  configure log. Force a GL-only build with `-DTUSDVIEW_WITH_VULKAN=OFF`.
  (Regenerate the embedded SPIR-V with `vk/shaders/build-shaders.sh` after a
  shader change — that script is the only thing that needs `glslangValidator`.)
  Common PNG/JPEG/BMP/TGA decoding uses the vendored fuzz-tested nanoimage
  backend by default (`-DTINYUSDZ_WITH_NANOIMAGE=ON`); TinyEXR v3 remains the
  default OpenEXR backend and preserves fp16/HDR data for the texture pipeline.
  The raster descriptor ABI uses four bound sets (material images, per-mesh
  deformation data, frame data, and material parameters), matching Vulkan's
  guaranteed `maxBoundDescriptorSets` minimum. This includes software devices
  such as llvmpipe that expose only eight bound sets.
- **Vulkan ray tracing** (`--rt`) is compiled in whenever
  `vk/shaders/embedded/raytrace_comp.spv.h` is present (it is, by default) and
  activates at runtime on a GPU exposing `VK_KHR_acceleration_structure` +
  `VK_KHR_ray_query`; otherwise it falls back to rasterization. Regenerating that
  header is the only step that needs a `GL_EXT_ray_query`-capable glslang (≈
  glslang ≥ 11); build one once with `examples/common/build-glslang.sh` if you
  edit `vk/shaders/raytrace.comp`.
  Vulkan, CUDA, and HIP RT consume the same semantic texture table (base color,
  metallic, roughness, normal, emissive, opacity, occlusion, specular color,
  and coat weight/color/roughness), including UV1, transforms, sRGB decode,
  compressed-only sources, sparse UDIMs, and native Ptex face-local sampling.
  Masked texels are rejected during traversal. Complete ordinary/compressed/
  UDIM mip chains use trilinear filtering: ray-differential footprint LOD in
  Vulkan and projected-triangle LOD in CUDA/HIP. Raster-style anisotropy
  remains future work.
- **Threaded render path** (a dedicated render thread owns the GL context /
  Vulkan queue so the UI never blocks on the GPU) is enabled by default for
  interactive Vulkan and can be overridden with `--threaded` or
  `--no-threaded`. It is gated behind this build option:

  ```sh
  cmake -S . -B build_ninja -DTUSDVIEW_ENABLE_GL_THREAD=ON   # + the flags above
  ```

  See [`threading-stage2.md`](../examples/tusdview/doc/threading-stage2.md). When
  OFF, the thread flags are no-ops and the single-threaded path is unchanged.

## Vulkan run test (verified working on NVIDIA)

The Vulkan backend is exercised end-to-end by the **`tusdview-vk-render`** ctest
(`examples/tusdview/tests/run-vk-render.sh`): it renders headless via
`--backend vk` and `--rt` (ray query) and asserts a non-blank frame. The
`--headless` path is Vulkan-only and renders offscreen, so the test needs **no
window / X server**; it SKIPs (return 77) when no Vulkan device is available, so
headless CI without a GPU stays green.

```sh
cd build && ctest -R tusdview-vk-render --output-on-failure
# or directly:
examples/tusdview/tests/run-vk-render.sh
```

Set `TUSDVIEW_RENDER_TIMEOUT` (default `30s`) to bound each tiny headless render
in the Vulkan test harness when debugging flaky driver paths.

**Verified working — NVIDIA GeForce RTX 5060 Ti (Linux, driver `610.43.02`,
Vulkan 1.4), 2026-06-28.** Both rasterization and hardware ray query render the
full scene correctly (raster vs ray-query agree to <1 LSB mean), at the default
1469×1284 with no resolution-dependent hang. This is tusdview's **own** Vulkan
backend (`examples/tusdview/vk/`); the separate LightRT GPU trace used by
`tusdrender -vk/-vkr` also renders correctly on this GPU after its geometry/setup
fixes — see [`doc/tusdrender.md`](tusdrender.md).

## CUDA ray-tracing run test (verified working on NVIDIA)

`--cuda` traces the loaded scene's BVH on the GPU using the **CUDA driver API +
NVRTC loaded at runtime via cuew** (`examples/common/cuew/`) — there is **no
link-time dependency on the CUDA toolkit**; only the NVIDIA driver
(`libcuda.so.1`) and an `libnvrtc.so` need to be present to *run*. The CUDA path
owns the screenshot (it bypasses the rasterized capture) and supports the
`RenderMode` AOV/debug views, selectable headless with `--mode <name>` (shaded,
normals, bvh-heatmap, material-id, uv, depth, …).

The default **shaded** view uses the same lighting model as the `tusdrender
-vk/-vkr` GPU previews (`tools/tusdrender/tusdr_vulkan.cc`) so the two ray-traced
previews agree: a camera-oriented shading normal, then `0.05 ambient + 0.8·key +
0.35·camera-headlight` with the shared fixed key light `(0.5, 0.8, 0.6)` and no
cast shadows. (The traced AOV modes — `ao`, `soft-shadow` — still do their own
GI/visibility tracing.)

```sh
# headless, no window/X server needed:
./build/tusdview --headless --cuda --frames 4 --screenshot out.ppm model.usda
# a debug AOV instead of the shaded image:
./build/tusdview --headless --cuda --mode normals     --frames 4 --screenshot n.ppm model.usda
./build/tusdview --headless --cuda --mode bvh-heatmap --frames 4 --screenshot h.ppm model.usda
```

The NVRTC-generated PTX is cached across processes. The default location is the
platform cache directory under `tusdview/cuda` (`$XDG_CACHE_HOME/tusdview/cuda`
or `~/.cache/tusdview/cuda` on Linux). Use `--cuda-cache-dir PATH` to select a
different directory, for example a job-local CI cache. Entries are keyed by the
kernel source, NVRTC version, virtual architecture, and compiler options, then
validated by the CUDA driver before use. Invalid entries are discarded and
rebuilt atomically.

During host BLAS/TLAS construction, `--cuda` prints a progress line every five
seconds with the current phase and completed/total work. This makes a massive
scene's geometry, instance-assembly, TLAS, or upload bottleneck explicit even
when the synchronous capture has not produced its render report yet.

NVRTC 13.x has a severe optimizer-time regression for this kernel when targeting
virtual architectures 100 and newer. On those compiler/device combinations,
tusdview emits forward-compatible optimized `compute_90` PTX instead; CUDA 12.x
and older retain their highest supported architecture. This changes only the
runtime-compiled CUDA tracer and does not affect Vulkan embedded SPIR-V.

The **`examples/tusdview/tests/run-cuda-render.sh`** harness exercises this
end-to-end; it SKIPs (return 77) when no NVIDIA device / NVRTC is available so
non-NVIDIA CI stays green. It is not a registered ctest — run it by hand:

```sh
TUSDVIEW=./build/tusdview examples/tusdview/tests/run-cuda-render.sh
```

When `--camera` selects an authored perspective camera with positive
`focusDistance` and `fStop`, Vulkan ray query and the shared CUDA/HIP kernel use
a deterministic thin-lens model. The aperture radius is derived from USD's
tenths-of-a-scene-unit focal length, and rays converge on the authored focus
plane. Vulkan progressively accumulates lens samples while `--rt-samples`
controls CUDA/HIP sampling. Orthographic cameras and `fStop = 0` retain the
exact pinhole path. `tusdview-camera-dof-{vulkan,cuda}` compare the two modes
and capability-skip when their backend is unavailable.

## HIP/ROCm ray-tracing run test (verified working on AMD)

`--hip` is the AMD counterpart of `--cuda`: it traces the same scene BVH and runs
the **same trace kernel** (shared via `examples/tusdview/raytracer_kernel_src.txt`),
but on the **HIP runtime + hiprtc loaded at runtime via hipew**
(`src/external/hipew/`) — **no link-time ROCm dependency**; only `libamdhip64`
and `libhiprtc` need to be present to *run*. The kernel is compiled at runtime
with hiprtc (which auto-targets the live device's gfx arch — no `--offload-arch`
needed). Like `--cuda`, the HIP path owns the screenshot and supports all
`--mode` AOV/debug views.

```sh
# headless, no window/X server needed:
./build/tusdview --headless --hip --frames 4 --screenshot out.ppm model.usda
./build/tusdview --headless --hip --mode normals --frames 4 --screenshot n.ppm model.usda
# anti-aliased screenshot: N Halton sub-pixel samples averaged on the host
# (works for --cuda too; default 1 = off):
./build/tusdview --headless --hip --rt-samples 4 --frames 4 --screenshot aa.ppm model.usda
```

The **`examples/tusdview/tests/run-hip-render.sh`** harness exercises this
end-to-end; it SKIPs (return 77) when no AMD/ROCm device is available so
non-AMD CI stays green. Like the CUDA harness it is not a registered ctest:

```sh
TUSDVIEW=./build/tusdview examples/tusdview/tests/run-hip-render.sh
```

Verified on AMD Radeon RX 9070 XT (gfx1201).

**Verified working — NVIDIA GeForce RTX 5060 Ti (Linux, driver `610.43.02`,
NVRTC 12/13), 2026-06-28.** Renders the full scene (suzanne, 968 tris) non-blank
at 1469×1284. The debug AOVs were spot-checked too: `--mode normals` produces
smoothly varying RGB-encoded shading normals, and `--mode bvh-heatmap`
(traversal cost, rmode 27) shows the nested BLAS/TLAS node bounds with elevated
cost clustering on the silhouette. `--mode depth` gives a correct near→far ramp
(dark, since the range is spread by `depthScale`), `--mode uv` shows the R=u/G=v
UV gradient, and `--mode material-id` is a single uniform color (suzanne is one
material, so one id) — all correct.

## Runtime backend switch (Render Technique) + CPU RT

**View ▸ Render Technique** switches the active render technique live, without
restarting: GL Raster, Vulkan Raster, Vulkan RT, CUDA RT, HIP RT, CPU RT. GL⇄Vulkan
tears down and rebuilds the `Renderer` and re-uploads the scene from `draw_`;
Vulkan Raster⇄Vulkan RT is a same-instance `setRayTracing()` toggle (no reupload);
CUDA RT/HIP RT/CPU RT trace externally into an RGBA8 buffer and composite it over
whichever window owner (GL or Vulkan) is active via `Renderer::uploadViewportImage()`
— both `GLRenderer` and `VulkanRenderer` implement it, so any of the three overlay
techniques works under either owner. A switch that fails (unavailable device,
Vulkan RT unsupported, `--threaded`/`TUSDVIEW_ENABLE_GL_THREAD` active) logs a
warning and reverts to the previous technique; it never crashes. CUDA/HIP menu
entries gray out only *after* a failed switch attempt (`App::cudaProbe_`/
`hipProbe_`), not proactively at startup — both tracers' `init()` compiles an
NVRTC/hiprtc kernel, which is too slow to probe speculatively.

`--cuda` / `--hip` without `--headless` now drive the viewport interactively
(build once, retrace on the orbit camera) instead of only writing a one-shot
screenshot; the screenshot path is unchanged for `--headless --screenshot`.

**CPU RT** (`--cpu-rt`, or `R` while the viewport is hovered — toggles it on/off,
restoring the prior technique) is a new backend, `examples/tusdview/cpu/
cpu_raytracer.{hh,cc}`. It reuses `BuildHostScene()` (`rt_scene_build.hh`, the
same DrawScene→world-space-triangle-soup flattener the CUDA/HIP tracers already
use) and builds one BVH over the result with the vendored `lightrt_c` library
(`src/external/lightrt/lightrt_c_tri.*`) — the same CMake target
`tools/tusdrender` already defines, linked into `tusdview` via a guarded
`add_subdirectory`. Tracing runs on a `std::thread` pool, one sample per pixel,
flat Lambertian + a hard shadow ray (Normals/MaterialId/Depth AOVs also
supported); there is no refit (unlike HIP, `lightrt_c_tri` has no per-vertex BVH
refit API), so a deformable scene's re-pose always rebuilds — the same
simplification `CudaRayTracer` already accepts. **Known limitations (v1):** no
textures/materials (flat/vertex-color shading only), no progressive
accumulation (each frame is a fresh 1-sample trace), and no headless one-shot
`--cpu-rt --screenshot` path yet (a headless run falls back to the normal
raster screenshot).

```sh
./build/tusdview model.usda                    # GL raster; View > Render Technique to switch live
./build/tusdview --cpu-rt model.usda            # start on CPU RT; R toggles it off/on
./build/tusdview --backend vk --cuda model.usda # interactive windowed CUDA RT
```

## Subdivision surfaces

tusdview can request Tydra's conversion-time subdivision surface refinement for
UsdGeomMesh prims whose `subdivisionScheme` is not `none`. This is CPU
refinement before draw-scene upload, so it works consistently with Vulkan
raster, Vulkan ray query, CUDA RT, HIP RT, screenshots, animation re-evaluation,
and AOV/debug modes.

```sh
# Scene-wide fallback level.
./build_ninja/tusdview --subdivision-level 1 model.usda

# Per-prim override. Repeat the flag for more prims.
./build_ninja/tusdview --subdivision-prim /World/Body=2 model.usda

# Auto-fit based per-prim levels from projected screen coverage.
./build_ninja/tusdview --subdivision-auto --subdivision-auto-max-level 3 model.usda
```

The scene-wide level is the fallback; `--subdivision-prim /Prim/Path=N` overrides
one mesh prim. `--subdivision-auto` first loads the base scene, estimates each
mesh's projected radius in pixels from the auto-fit camera and viewport height,
then re-converts once with per-prim overrides up to
`--subdivision-auto-max-level` (default 3, hard cap 10). Explicit per-prim
overrides win over auto levels.

The same options can be placed in the startup config:

```json
{
  "subdivision_level": 0,
  "subdivision_auto": true,
  "subdivision_auto_max_level": 3,
  "subdivision_prim_levels": {
    "/World/Body": 2
  }
}
```

### Windows (llvm-mingw)

tusdview cross/host-builds with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
(clang + the UCRT) using the bundled toolchain file. No Vulkan SDK or
`glslangValidator` is needed: the Vulkan API is resolved at runtime via the
vendored **volk** meta-loader and the shaders ship as pre-compiled **embedded
SPIR-V** (`vk/shaders/embedded/`), so the Vulkan backend — including ray query —
is on by default and the `.exe` is self-contained (the MinGW C++/unwind/pthread
runtime is statically linked; opt out with `-DTUSDVIEW_STATIC_RUNTIME=OFF`).

```sh
export LLVM_MINGW_DIR=/path/to/llvm-mingw-…-ucrt-x86_64   # read by the toolchain file
export PATH="$LLVM_MINGW_DIR/bin:$PATH"                    # + ninja on PATH

cmake -S . -B build-llvm-mingw -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-win64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DTINYUSDZ_BUILD_EXAMPLES=ON -DTINYUSDZ_WITH_TYDRA=ON \
  -DTINYUSDZ_BUILD_GUI_VIEWER=ON
cmake --build build-llvm-mingw -j16 --target tusdview
```

- Builds clean under clang's `-Weverything -Werror`; no `-DTINYUSDZ_NO_WERROR=ON`
  needed.
- Add `-DTINYUSDZ_BUILD_TOOLS=ON --target tusdrender` to also build the CPU/Vulkan
  preview ray-tracer (`tools/tusdrender/`), which is likewise self-contained under
  MinGW.
- Look for `tusdview: Vulkan backend ENABLED (runtime-loaded via volk; embedded
  SPIR-V, ray query ON)` in the configure log to confirm the Vulkan + RT path.

## Texture pipeline + DomeLight IBL (vendored textools)

With `TINYUSDZ_WITH_TEXTOOLS=ON` (default; see
`src/external/textools/README.tinyusdz.md`) tusdview uses the vendored
tir/texcomp/texpipe/envmap libraries:

- Texture resize (`--texture-max-size`, `--texture-budget-mb`) runs through
  `tir` (sRGB-aware, premultiplied-alpha filtering) instead of stb.
- Adaptive GPU block compression (`--texture-compress auto`, using `texcomp`
  and selecting a supported BC/ASTC/ETC2 format; `--texture-compress off`
  disables it) and content-aware CPU mip chains (`--texture-mips on`, via
  `texpipe`: sRGB-correct filtering, alpha-coverage preservation for Mask
  materials, normal-map renormalization, variance-aware roughness-channel
  minification, wrap-mode-aware filter edges). GL uploads the precomputed
  levels instead of `glGenerateMipmap`; Vulkan gains real mip chains (it
  previously sampled only level 0). Whether resize and compression run at all
  is governed by `--texture-fit`, below.

### `--texture-fit`: how hard to work to fit GPU memory

Shrinking and block-compressing textures is by far the most expensive part of a
texture-heavy load -- ALab `alab_set01` spent **362 s of a 395 s load** encoding
507 textures (2028 MB -> 507 MB BC7) on a card with 13 GiB free. Skipping just
the compression takes that scene to a 116 s load (texture stage 362 s -> 73 s,
the remainder being the mip chains, which are still built -- see below). It is only
worth paying when the scene would not otherwise fit, so the viewer compares the
scene's estimated resident footprint (**geometry + decoded textures**, since both
share the device) against a threshold:

```
--texture-fit modest|default|aggressive|never|always|<N>[KMG]
```

| value | threshold | behaviour |
| --- | --- | --- |
| `modest` | 33% of VRAM | leave textures alone only when clearly small |
| `default` | 66% of VRAM | the default |
| `aggressive` | 90% of VRAM | process only when nearly full |
| `never` | unbounded | never resize/compress |
| `always` | 0 | always resize/compress (pre-policy behaviour) |
| `<N>G` | absolute | fixed threshold, ignores card size |

Fractions are of total VRAM **capacity**, not currently-free VRAM, so the same
scene decides the same way on the same card regardless of what else is running;
`--vram-budget G` rehearses a different card. Fractional thresholds are
additionally clamped to 60% of the host memory limit, because decoded textures
are resident in host RAM while loading (`never`/`always`/absolute are explicit
user intent and are not clamped).

Under a threshold policy the decoder's longest-edge cap is left **off** and the
byte budget becomes the only limiter: its live residency accounting shrinks an
individual image only if the running total actually crosses the threshold, so a
scene that fits is never touched. A resolution floor keeps that budget soft --
a scene that crosses it mid-decode goes blurrier rather than losing textures.
`--texture-fit never` zeroes both the cap and the budget, which is also the
precondition that re-enables the KTX2 zero-copy passthrough. `--full-fidelity`
implies `never` unless `--texture-fit` is given explicitly.

**Mip generation deliberately does not follow this policy.** It keeps a narrow
budget of its own (25% of resident VRAM), because skipping mips makes every
later frame sample minified textures at full resolution and thrashes the texture
cache -- widening the two together once took the texture-semantic AOV suite from
52 s to over 300 s. Compression and resize are load-time costs; missing mips are
a per-frame cost forever.

Every load prints the decision, so it is diagnosable from a log:

```
next: texture-fit=default (66% of VRAM) 507 textures, 2028.0 MiB decoded +
  827.2 MiB geometry = 2855.2 MiB vs 10765.3 MiB threshold (VRAM 16311.0 MiB,
  comfort 1920.0 MiB); skip compression, keep mips; decoder max_edge=0,
  0 downscaled
```
- `--dome-ibl off|low|high` (default high) bakes DomeLight split-sum IBL at
  load via the `envmap` library from the HDR float envmap (EXR half/float and
  Radiance HDR decode correctly now): GGX-prefiltered specular cube chain,
  diffuse irradiance cube, and BRDF LUT. GL and Vulkan raster shade the
  ambient term with it; the VK ray-query and CUDA/HIP RT paths sample the dome
  environment on ray miss. The bake logs its wall time
  (`[tusdview] dome IBL bake ...`), typically ~0.3-0.5 s.
- GL and Vulkan raster also evaluate the first 16 supported authored direct
  lights in stage order. Distant and finite sphere/point, rect, disk, and
  cylinder lights honor diffuse/specular multipliers, shaping, and per-mesh
  light-link collections; excess supported lights produce a truncation
  diagnostic. They share one deterministic 2048-square shadow map for the first
  enabled DistantLight, otherwise the first finite light; the pass uses 3x3 PCF,
  honors direct versus shadow light-link collections, alpha cutouts, and Vulkan
  instanced prototypes. Finite area shapes still use their representative
  position in raster. Vulkan ray query and the shared CUDA/HIP tracer instead
  take deterministic surface samples for sphere, disk, rect, and cylinder
  emitters; progressive samples converge their penumbrae, while zero-sized
  lights preserve the point-emitter path. Exact omnidirectional finite-light
  raster shadows remain future work.

`tusdrender -ibl envmap` opts the offline renderer into the same envmap-library
precompute (default stays the built-in reference; measured parity on a dome
test scene: mean |diff| 0.16/255, 0.06% of channels >32/255).

## Headless screenshots (CI / no display)

```sh
# Vulkan, no X server at all (renders into an offscreen image + reads it back):
./build_ninja/tusdview --headless --frames 8 --screenshot out.png model.usdz
./build_ninja/tusdview --headless --rt --frames 8 --screenshot rt.png model.usdz

Vulkan ray-query splits scenes larger than the device's per-TLAS instance limit
into as many as four TLAS chunks (about 67 million instances on common NVIDIA
drivers). Primary, alpha, linked-shadow, AO, and soft-shadow rays traverse every
chunk while preserving the global instance ID. It refuses a partial RT render if
the four-chunk capacity is exceeded; use explicit `--rt-lod` for a degraded
preview instead.

# Save deterministic intermediate convergence images at frames 4, 8, ... .
# The same controls work for raster and Vulkan ray-query; {frame} expands to a
# zero-padded fixed-frame index and the JSON report records count/sample state.
./build_ninja/tusdview --headless --rt --frames 16 \
  --checkpoint-every 4 --checkpoint-pattern 'checkpoints/shot-{frame}.png' \
  --screenshot shot-final.png --render-report shot.json model.usdz

# Large-scene realtime presets: resolve Vulkan/LOD/budget flags and render a
# deferred-payload overview first. Payload roots with authored extents use them;
# otherwise compact marker boxes preserve the composed spatial distribution.
# The automatic profile also postpones DomeLight IBL precompute.
./build_ninja/tusdview --headless --large-scene-profile instance-heavy \
  --frames 8 --screenshot island.ppm /path/to/island.usda

# Explicit complete/offline-quality load (higher latency and memory):
./build_ninja/tusdview --headless --large-scene-profile instance-heavy \
  --load-payloads --dome-ibl high --frames 1 --screenshot island-full.ppm \
  /path/to/island.usda

# Optional local harness. Set any scene path env var that exists on the machine.
CALDERA=/path/to/caldera.usda ISLAND=/path/to/island.usda ALAB=/path/to/alab.usda \
  bash examples/tusdview/tests/run-large-scene-profiles.sh
# Use TUSDVIEW_SCENE_TIMEOUT=10m (default) to bound each large-scene run.

# Interactive prefab benchmark (first useful frame + full upload):
SCENE=/path/to/caldera/map_source/prefabs/.../st_main.usd \
  bash examples/tusdview/tests/run-large-scene-prefab-benchmark.sh

# NVIDIA hardware-accelerated GL under Xvfb (not llvmpipe):
GPU_ACCEL=nvidia SCENE=/path/to/caldera/map_source/prefabs/.../st_main.usd \
  PROFILE=balanced ENDPOINT=upload FULL_FIDELITY=1 \
  bash examples/tusdview/tests/run-large-scene-prefab-benchmark.sh

The benchmark log separates render-data setup into composition, stage
traversal, non-mesh extraction, render-prim collection, geometry estimation,
mesh conversion/flattening/batching, and finalization. `first geometry
produced` and `full scene uploaded and presented` include the backend upload
path. With `GPU_ACCEL=nvidia`, the harness applies the PRIME/GLVND variables
described below and the log must report an NVIDIA renderer; otherwise the run
is not a hardware-GL measurement.

Measured on NVIDIA GeForce RTX 5060 Ti (16 GiB), driver 610.57.04, Xvfb,
OpenGL, deferred payloads, and eight compose/conversion workers:

| Entry scene | Compose | Stage/extract/setup | Mesh convert/batch | Full CPU conversion | Full upload/present | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|
| Caldera `chem_factory_01.usd` | 15.27 s | 4.97 s | 7.03 s | 29.72 s | 29.84 s | 4.14 GiB |
| Caldera `superterrrain/st_main.usd` (full fidelity) | 70.72 s | 35.0 s | 50.12 s | 158.08 s | 159.21 s | 17.8 GiB |
| Island `island.usda` (deferred root) | 0.012 s | 0.003 s | 0.001 s | 0.369 s | 0.546 s | 166 MiB |
| ALab `entry.usda` (deferred root) | 0.433 s | 0.085 s | 0.041 s | 0.969 s | 1.036 s | 256 MiB |

The Island and ALab deferred-root values measure lazy entry-layer setup, not
full-payload conversion. Caldera's setup before mesh conversion is about 4.97
s, while mesh conversion/batching is 7.03 s. GPU upload/presentation adds
about 0.12 s after CPU conversion for this deferred-payload case.

The subsequent generic batching optimization caches inherited back-purpose
material bindings by parent path, avoiding repeated ancestor walks for sibling
meshes. A repeat hardware-GL Caldera run with `--compose-opinion-batch 1024`
measured 6.89 s mesh conversion/batching, 27.72 s full CPU conversion, 27.91 s
full upload/presentation, and 4.07 GiB peak RSS. This is a cold-process single
run; use repeated runs when comparing small differences.

Batch selection then moved from ordered-map lookup to a hashed composite key,
while a separate encounter-order list preserves deterministic draw ordering.
The follow-up hardware run measured 6.83 s mesh conversion/batching, 27.35 s
full CPU conversion, 27.60 s full upload/presentation, and 4.07 GiB peak RSS.

The exact Caldera `superterrrain/st_main.usd` root is a materially larger
workload: 2,175,050 composed prims and 851,543 mesh records, ultimately
collapsing to 39 guide-purpose draws. Its cold full-fidelity path is therefore
not yet under the 20-second target; the dominant next targets are opinion
composition (41.16 s inside the stage build) and conversion of the large mesh
record set (50.12 s). The loader now skips conversion when the generic geometry
preflight reports zero resident bytes, and avoids duplicate child-name
composition for instanceable prims; this run showed that the Caldera records
all have nonzero estimates, so those guards do not materially change this
scene's result.

The mesh-path follow-up also caches inherited animated-transform state during
render extraction and uses an exact plain-static batching path for meshes with
no authored feature streams. On the same cold run this reduced mesh
convert/flatten/batch from 50.12 s to 48.95 s; 27,075 meshes used the plain
path and 824,468 still required feature handling. Larger conversion waves and
16 workers were tested as generic overrides, but remained about 46–49 s, so
the next optimization should target the dominant feature path itself.

# Production Island matrix with schema-validated JSON reports. This is also a
# CTest entry that cleanly skips when ISLAND_USD is absent. Override the default
# 720p gl/vk/vk-rt matrix during a quick smoke as shown here:
ISLAND_USD=/path/to/island.usda ISLAND_CAMERA=/island/cam/shotCam \
ISLAND_CAPTURE_SIZES=320x200 ISLAND_CAPTURE_BACKENDS=vk \
  ctest --test-dir build_ninja -R tusdview-island-production-smoke -V

# Interactive large-scene profiles persist their compact composition preview.
# Force a cold rebuild when validating preview generation, or disable it:
./build_ninja/tusdview --large-scene-profile instance-heavy \
  --preview-cache refresh /path/to/island.usda
./build_ninja/tusdview --large-scene-profile instance-heavy \
  --preview-cache off /path/to/island.usda
# Optional storage controls (default limit: 8 GiB):
./build_ninja/tusdview --large-scene-profile instance-heavy \
  --preview-cache-dir /fast/cache --preview-cache-max-gb 4 /path/to/island.usda

# OpenGL (needs a window/context) on a headless host — wrap in a virtual X server
# with a 24-bit visual:
xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build_ninja/tusdview --backend gl --frames 8 --screenshot out.png model.usdz
```

The shared automatic policy targets a 32 GiB host and a 16 GiB GPU: 30 GiB
process headroom, an 8 GiB effective VRAM cap, and a 512 MiB upload-staging cap.
On an RTX 5060 Ti through the NVIDIA/Xvfb path below, first proxy frames measured
1.36 s / 0.49 GB peak RSS (Island), 8.29 s / 2.18 GB (Caldera), and 1.97 s /
0.55 GB (ALab). A 120-frame Island proxy run on a 1920x1080 Xvfb surface
measured 75.0 ms present/readback p95 (81.3 ms maximum); Vulkan CPU
record/submit p95 was 0.3 ms.

The preview cache key includes the composition, payload/reference, variant,
parent-path, and time-code choices. Its manifest records every physical layer
dependency, including package archives, and rejects an entry if any dependency
size or modification time changes. The cached preview is display-only: the
authoritative composed stage replaces it when background composition finishes.
Use `--timing` to report cache validation/load/store time separately from
composition and first useful frame.

Interactive loads use the content-name-agnostic `balanced` workload preset by
default. `instance-heavy` favors repeated prototypes and bounded Ptex startup;
`procedural-heavy` favors curves/points and asynchronous texture setup. No
dataset path or prim name selects policy. Use `--large-scene-profile off` to
restore ordinary behavior; headless and fixed-frame runs remain unchanged
unless a preset is explicitly requested.

All scheduling policy remains overrideable. The CLI exposes
`--compose-opinion-batch`, `--instance-chunk-samples`,
`--mesh-convert-chunk-prims`, `--mesh-convert-chunk-mb`, and
`--curve-parallel-min-prims`, in addition to the composition/conversion thread
counts. Zero selects a hardware/budget-derived value. The Scene panel's
**Load scheduling** section exposes the same values; changes apply on
File > Reload. Explicit CLI values initialize those GUI controls.

The viewer also publishes a bounded raw-root preview before the authoritative
PCP namespace build. On the Caldera `st_main.usd` prefab this cold preview was
about 0.3 s; the authoritative stage then replaces it when composition
finishes. The preview cache remains the fast path for repeated opens (about
1.9 s including cache validation). Use the benchmark's
`PREVIEW_CACHE=refresh` to populate the cache deliberately. Very large
prefabs may exceed the host memory budget with `--load-payloads`; use the
default deferred payload mode for interactive inspection.

An explicitly selected USD camera can also drive conversion order.
Leaf meshes without extents inherit the nearest model/district extent for this
ranking; conservative in-view bounds are converted before off-camera and
unbounded fallback work. Interactive material batches are limited to 512K
vertices, allowing those camera-relevant results to upload incrementally instead
of waiting for a multi-million-triangle scene-wide batch.

The interactive `procedural-heavy` preset starts with a bounded refinement tier because its
complete texture and baked-procedural working set exceeds typical GPU memory.
Payloads are initially deferred. When loaded, Curves are tessellated minimally
and sampled as complete strands (up to 64 curve prims and 100,000 strands), so
hair topology and per-point attributes remain valid. Textures are decoded with a
512px edge ceiling, compressed online to a supported block format, and uploaded
without an eager mip pyramid. Headless runs and explicit texture CLI overrides
are not reduced.

Ordinary ALab filesystem textures use stable placeholder slots during material
conversion. After geometry has been published, a bounded four-worker stage
decodes, resizes, and CPU-encodes supported GPU block formats; ready payloads are
sent individually to the render thread. This is asynchronous decode/compression
and incremental GPU upload, not GPU-compute encoding. Archive, UDIM, Ptex, and
kept-compressed KTX2 inputs remain on their specialized synchronous paths until
their readers have independent concurrent ownership.

`.png` and `.ppm` outputs are both supported; `--frames N` loads synchronously so
screenshots are deterministic. Pixel-compare two screenshots (e.g. backend or
threaded-vs-single parity) with PIL/numpy:

The Vulkan/CUDA/HIP RT shaded modes sample base color, independent metallic and
roughness, normal, emissive, and opacity images, including sparse UDIM and
compressed-only inputs decoded into the shared mipmapped table. Vulkan uses
ray-footprint LOD and CUDA/HIP use projected-triangle LOD, both with trilinear
filtering. Raster texture resize/compression behavior is unchanged.

```sh
python3 -c "from PIL import Image; import numpy as np; \
a=np.array(Image.open('a.png').convert('RGB')).astype(int); \
b=np.array(Image.open('b.png').convert('RGB')).astype(int); \
print('maxdiff', int(np.abs(a-b).max()))"
```

### OpenGL from a Codex sandbox

In the Codex managed sandbox, `DISPLAY=:0` may point at a real desktop X socket
but still fail at GLFW permission/auth time:

```text
glfw error 65544: X11: Failed to open display :0
```

The usual `xvfb-run` wrapper can also fail if the host's `/tmp/.X11-unix`
directory is not root-owned inside the sandbox view:

```text
_XSERVTransmkdir: Owner of /tmp/.X11-unix should be set to root
Fatal server error: Cannot establish any listening sockets
```

For a correctness screenshot, start Xvfb directly outside the sandbox with Unix
sockets disabled and TCP enabled, then run `tusdview` against that display:

```sh
# Run outside the sandbox / with command escalation, and keep it running:
Xvfb :88 -screen 0 1280x720x24 -ac -nolisten unix -nolisten local -listen tcp

# In another command, also outside the sandbox if client TCP connect is blocked:
env DISPLAY=localhost:88 \
  ./build_ninja/tusdview --backend gl --frames 8 \
  --screenshot /tmp/tusdview_gl.ppm model.usdz
```

If the captured viewport is unexpectedly tiny, pass a temporary config with an
explicit window size:

```json
{
  "window_size": { "width": 1280, "height": 720 },
  "recent_scenes": []
}
```

Then run:

```sh
env DISPLAY=localhost:88 \
  ./build_ninja/tusdview --config /tmp/tusdview_1280.json --backend gl \
  --frames 4 --screenshot /tmp/tusdview_gl.ppm model.usdz
```

Stop the temporary Xvfb server after the run and confirm it is gone:

```sh
pgrep -a Xvfb || true
```

### Vulkan on NVIDIA PRIME/offload under Xvfb

On hybrid/offload systems, the default Vulkan device visible inside a headless
or sandboxed session may be Mesa/llvmpipe even when an NVIDIA GPU is installed.
There are two separate selections to make:

* OpenGL uses the NVIDIA GLVND/PRIME environment variables while Xvfb supplies
  the window system.
* Vulkan needs an explicit device selector when automatic enumeration chooses
  llvmpipe. GLVND variables do not select a Vulkan physical device.

For a direct viewer run, wrap it in Xvfb, pass the GLVND environment, and use
the `--vk-device nvidia` command-line option:

```sh
xvfb-run -a env \
  __NV_PRIME_RENDER_OFFLOAD=1 \
  __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  TUSDVIEW_RT_DEBUG=1 \
  ./build_ninja/tusdview --backend vk --vk-device nvidia --rt \
  --frames 1 --size 64x64 tests/usda/anytype-001.usda
```

Startup logs print `renderer`, `GPU`, and `API` for both OpenGL and Vulkan, so a
headless run can reject llvmpipe without requiring `glxinfo`.

## Instance-heavy scenes and raster LOD

Scenes built from millions of small instances are limited by per-instance
geometry throughput, not by shading or memory placement. Two Moana island
elements at 1920x1080, Vulkan raster:

| Element | Instances | Drawn tris/frame | Frame |
| --- | ---: | ---: | ---: |
| `isCoral` | 3,495,916 | 442,645,322 | 136ms |
| `isDunesB` | 19,830,239 | 9,091,066,066 | 1154ms |

Nothing is culled at those framings (`instances N/N visible`), and isCoral's
frame time is flat from 480x270 to 1920x1080, confirming it is not
fragment-bound. isDunesB sustains ~7.9 B tris/s and isCoral ~3.3 B tris/s --
the GPU is working efficiently, the scenes are simply asking for too much.
isCoral is the slower of the two per triangle because its instances are
smaller (126 tris each versus 458), so per-instance cost dominates.

`--raster-lod` is the lever: it keeps instances at or above
`--raster-lod-full-px` in real geometry, draws a box proxy down to
`--raster-lod-cull-px`, and culls below that.

| isDunesB | Frame | Quality |
| --- | ---: | --- |
| no LOD | 1154ms | reference |
| `--raster-lod-full-px 48` (old default) | 0.99ms | vegetation becomes white boxes |
| `--raster-lod-full-px 4`, `8` or `16` | 6.4ms | visually faithful |

The default is 8. The quality cliff is between 16 and 48: 4, 8 and 16 all
render isDunesB identically (21,445,786 tris), while 48 collapses it to 5,655
proxies. isCoral is baseline-identical at 4 (0.80ms) and marginally blockier at
8 (0.51ms), so use `--raster-lod-full-px 4` for quality-critical work. Lowering
the value is the safe direction -- it draws more real geometry, costing speed
rather than fidelity.

## `--mode-sweep`: many AOVs from one load

```
tusdview scene.usda --backend vk --rt --headless --frames 4 \
  --mode-sweep albedo,metallic,roughness,emissive \
  --screenshot out-{mode}.ppm
```

Renders each mode from a single load, writing `--screenshot` once per mode with
`{mode}` substituted (the placeholder is required). Each mode gets the same
frame budget it would get in its own process, so the images are **byte-identical**
to running `--mode N` times -- verified 9/9 on Vulkan raster and Vulkan RT.

The win is startup, not rendering. A small headless case spends ~90% of its time
creating and destroying the Vulkan device (measured: `ioctl` 40%, ICD `dlopen`
~14%, shutdown 11%, all pipeline creation only 1-3%), so N modes of one scene
cost N device creations. Nine modes of a two-triangle scene: 6.20 s as separate
processes, 0.72 s as one sweep (8.6x).

**Rejected for CUDA and HIP.** Those backends trace after the frame loop and
write the image themselves, while the sweep captures the in-loop viewport.
Combining them used to produce N raster images labelled as ray-traced AOVs plus
one file named with the literal `{mode}` from the tracer's own write, so the
viewer now refuses the combination with an error rather than emitting wrong
output. Run one mode per invocation for those backends.

## Machine-readable capability line

Every run prints a versioned capability line after renderer init, and again
whenever what it reports changes (ray tracing toggled, backend switched):

```
[tusdview] caps: v1 backend=vulkan device=discrete rt=hardware rt_available=1 gpu="NVIDIA GeForce RTX 5060 Ti"
```

| key | values |
| --- | --- |
| `backend` | `gl`, `vulkan`, `unknown` |
| `device` | `discrete`, `integrated`, `virtual`, `cpu`, `gpu`, `other`, `unknown` |
| `rt` | `off`, `hardware`, `software` (the mode currently active) |
| `rt_available` | `0`, `1` (device capability, independent of whether RT is on) |
| `gpu` | quoted device name; last, because it is the only value that can contain spaces |

**This is a test contract: do not reword it, only extend it.** New keys append
before `gpu=`; a breaking change takes a new version (`v2`), so a parser can
reject what it does not understand instead of silently matching nothing.

It exists because the test suite used to infer hardware from prose log lines.
When `Vulkan ray tracing (ray query) enabled.` was reworded to
`Vulkan ray tracing enabled (hardware ray query).`, seven tests stopped
matching and reported missing hardware on a machine that had it -- while still
exiting 0 or "skipped", so the suite looked green. Two more did the same when a
profile was renamed and when a software-Vulkan probe asked whether llvmpipe was
*installed* rather than *selected*. Tests now grep `caps: v1 .*rt=hardware` and
`caps: v1 .*device=cpu` instead of driver marketing strings.

## Vulkan raster profiling

`TUSDVIEW_TIME_GPU=1` prints a per-pass GPU breakdown from timestamp queries,
plus the geometry actually submitted, once per frame:

```
[gpu] pre-main(shadow+copies)=0.00ms  main-offscreen=1.95ms  swap+imgui=0.07ms  total=2.02ms
[gpu] submitted: 3 draws, 3812328 tris, viewport 1371x876
```

It pairs with the CPU-side timers `TUSDVIEW_TIME_FRAME` (whole present) and
`TUSDVIEW_TIME_PRESENT` (previous-frame GPU wait vs CPU record+submit). All
three are off unless set, and cost nothing when off.

The `submitted:` line counts the non-instanced mesh loop only; geometry drawn by
the instanced pass is not tallied, so an instanced scene can legitimately report
very few triangles.

Mesh geometry must live in VRAM: per-frame vertex fetch from host memory crosses
PCIe on every draw, which cost 86ms versus 2ms on a 3.8M triangle scene. Buffers
get there two ways.

Static mesh geometry (never rewritten after upload -- not CPU-skinned, no GPU
morph, no persistent mapping) above a size floor goes into a **device-local
pool**:
block sub-allocated like the host pool, because one `vkAllocateMemory` per
buffer reproduces the allocation storm pooling exists to prevent, and filled by
staging copies batched into a single submit, because `endOneShot` stalls the
queue. The floor keeps scenes built from many small meshes from paying a per-buffer
staged copy for a fetch saving too small to measure. It is hardware dependent
and probed once:

| Device | Floor | Why |
| --- | --- | --- |
| host-visible VRAM heap present (ReBAR, integrated) | 256KB | Buffers under the floor still reach VRAM via that heap, so the floor only picks the mechanism. |
| no such heap (small BAR) | 64KB | Under the floor means system RAM, so the floor decides how much geometry is resident at all. |

Measured on Moana island `elements/isCoral` (13638 draws), sweeping
`TUSDVIEW_VBO_DEVLOCAL_MIN_KB`. With ReBAR, frame time is flat for every floor
from 256KB to 0 while floor=0 triples load time (34.3s -> 122.3s, 151932
buffers). Rehearsing a small-BAR card with `TUSDVIEW_VBO_NO_REBAR=1`, the same
scene gives 295.7ms at 256KB (the host-only path is 372ms), 165.4ms at 64KB,
163.6ms at 16KB and 148.5ms at 4KB -- the knee is 64KB, worth 1.8x for +0.9s of
load where 4KB costs +10s for another 10%.

`TUSDVIEW_VBO_DEVLOCAL_MIN_KB` overrides the floor (in KiB) and
`TUSDVIEW_VBO_NO_REBAR=1` rehearses a card without a host-visible VRAM heap;
both exist for sweeps like the one above.

Absolute frame times drift between sessions with GPU clock state -- the same
build measured isCoral at 79ms and 138ms on different runs -- so compare a
device-local and host-only pair measured back to back, not across sessions.

Everything else -- dynamic meshes, persistently mapped instance buffers, and
static buffers below the floor -- stays host-visible, but prefers a heap that is
`DEVICE_LOCAL` as well as host-visible (ReBAR, or an integrated GPU) so it still
lands in VRAM where possible. Pure `TRANSFER_SRC` staging buffers are excluded
from that preference: they are read once, and a large texture upload would
otherwise exhaust a small BAR heap and push real geometry back onto the slow
path. `TUSDVIEW_VBO_HOST=1` forces everything host-only, the A/B lever for
confirming a suspected vertex-fetch stall.

The variables used by the viewer test harnesses have narrower meanings:

| Variable | Consumer | Meaning |
| --- | --- | --- |
| `__NV_PRIME_RENDER_OFFLOAD=1` | NVIDIA GLVND | Route an OpenGL context to the NVIDIA GPU. |
| `__GLX_VENDOR_LIBRARY_NAME=nvidia` | NVIDIA GLVND | Select NVIDIA's GLX implementation. |
| `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json` | NVIDIA GLVND | Select NVIDIA's EGL vendor when multiple vendors are installed. |
| `TUSDVIEW_VK_DEVICE=nvidia` | Tydra/tusdview shell harnesses | Forward `--vk-device nvidia` to Vulkan viewer runs. Direct `tusdview` uses the CLI option itself. |
| `TUSDVIEW_NVIDIA_OFFLOAD=1` | USD-assets smoke harness | Enable the NVIDIA GLVND environment for viewer invocations. |
| `TUSDVIEW_XVFB=1` | USD-assets smoke harness | Force the harness to wrap windowed viewer modes in `xvfb-run -a`. |
| `TUSDVIEW_XVFB=external` | USD-assets smoke harness | Use the caller's existing `DISPLAY`, for an externally started Xvfb. |

Use `TUSDVIEW_XVFB=0` for true headless Vulkan/CUDA/HIP modes; those modes do
not need X11. Use `TUSDVIEW_XVFB=external` only after setting `DISPLAY` to a
known-good X server, for example `DISPLAY=localhost:88` when Xvfb was started
with `-nolisten unix -listen tcp`.

The GL/VK screenshot ctests (`tusdview-skinning-screenshot-diff`,
`tusdview-instanced-prototype-skinning`, `tusdview-uv-set-routing`) apply this
recipe **automatically**: `tests/tusdview/gpu_backend.py` detects the loaded GPU
driver (`detect_gpu()` → `nvidia` via `/proc/driver/nvidia/version`, `amd` via
`/sys/module/amdgpu` or `/dev/kfd`, or none) and, on the Xvfb fallback, sets the
NVIDIA offload environment for GL and passes `--vk-device <vendor>` for Vulkan.
A plain `ctest` therefore exercises hardware GL+VK on NVIDIA and hardware VK on
AMD, with no environment setup; without a GPU driver the tests keep their
software-renderer skip. (AMD GL under Xvfb stays llvmpipe — radeonsi needs DRI
on the X server itself, so only the Vulkan half reaches hardware there.)

A successful run prints the selected hardware device and ray-query support, for
example:

```text
[tusdview] Vulkan device[0]: NVIDIA GeForce RTX 3070 (discrete, driver NVIDIA, rt yes)
[tusdview] Vulkan ray tracing (ray query) enabled.
```

If `--vk-device nvidia` fails and the candidate list only shows `llvmpipe`,
the offload environment did not reach the Vulkan loader/driver. Verify that the
NVIDIA GLVND vendor file exists and that the command is running under Xvfb (or
a real X session) with the offload variables above.

For the complete native CTest viewer coverage, configure with
`-DTINYUSDZ_TUSDVIEW_NVIDIA_OFFLOAD=ON`. At configure time CMake checks the
NVIDIA kernel device, the GLVND vendor file, and `vulkaninfo --summary`. When
the Vulkan summary contains an NVIDIA physical device, CMake attaches the GL
offload environment and `TUSDVIEW_VK_DEVICE=nvidia` to every `tusdview-*` test
(including tests that launch their own `xvfb-run`). If only the GL pieces are
visible, it attaches GL offload but leaves Vulkan device selection automatic;
this avoids turning a software fallback into a hard Vulkan initialization
failure. Unrelated tests are unchanged.

```sh
cmake -S . -B build_ninja -G Ninja \
  -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_GUI_VIEWER=ON \
  -DTINYUSDZ_TUSDVIEW_NVIDIA_OFFLOAD=ON
cmake --build build_ninja -j16
xvfb-run -a -s "-screen 0 1280x800x24" \
  ctest --test-dir build_ninja -R '^tusdview' --output-on-failure
```

The configure log reports whether the NVIDIA Vulkan device was confirmed. Do
not add `TUSDVIEW_VK_DEVICE=nvidia` manually when that probe fails: verify the
driver/ICD visibility first, or let the test skip/fall back to its normal
software-device policy. To run the entire native CTest matrix under the same
headless display, replace `-R '^tusdview'` with no `ctest` filter; the standalone
`next` and OpenUSD comparison commands remain as shown in
[`doc/testing-cpp.md`](testing-cpp.md).

### External usd-assets smoke rendering

`examples/tusdview/tests/run-usd-assets-render-smoke.sh` enumerates USD-family
files under an externally supplied asset root and renders each file through the
selected tusdview/tusdrender modes:

- Vulkan raster: `--headless --backend vk`
- Vulkan ray query: `--headless --backend vk --rt`
- CUDA RT: `--headless --cuda`
- tusdrender CPU preview: `-rtPreview`
- tusdrender Vulkan preview: `-vk`
- tusdrender Vulkan ray query: `-vkr`

The harness writes a TSV result table and classifies each pass as `rendered`,
`rendered_with_warnings`, `no_renderable`, `load_error`, `timeout`,
`backend_unavailable`, or `backend_error`. It is registered as
`tusdview-usd-assets-broad-smoke`, but skips unless `USD_ASSETS_ROOT` and
`TUSDVIEW_RUN_USD_ASSETS_BROAD=1` are set.
The tusdrender modes use `-autoframe` and the renderer's existing scene-light /
headlight fallback. Use `--profile usd-assets-curated` for the short material
set or `--profile usd-assets-broad` for a bounded cross-section spanning
composition, primvars, subdivision, analytic geometry, transforms, textures,
transparency, MaterialX, large composed scenes, and animated/skinned USDZ
assets. `--time CODE` evaluates both tusdview and tusdrender at a representative
animation time. The broad profile intentionally avoids rendering every helper
layer, since material-only and camera-only layers mostly add expected
`no_renderable` results rather than fidelity coverage. External gates use
`--require-profile-complete`, so a renamed or incomplete corpus cannot silently
reduce the advertised coverage.

```sh
USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW_RUN_USD_ASSETS_BROAD=1 \
  ctest --test-dir build_ninja -R tusdview-usd-assets-broad-smoke -V
```

The runner can also be invoked directly for one backend while iterating:

```sh
USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW=./build_ninja/tusdview \
  examples/tusdview/tests/run-usd-assets-render-smoke.sh \
    --profile usd-assets-broad --modes vk-raster --time 1
```

Animation has a separate cross-time gate. It renders representative transform,
point-interpolation, and skinned USDZ samples at time codes 0 and 1, then fails
if an available backend produces no image changes at all:

```sh
USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW_RUN_USD_ASSETS_ANIMATION=1 \
  ctest --test-dir build_ninja -R tusdview-usd-assets-animation-smoke -V
```

On an NVIDIA PRIME/offload host where Vulkan otherwise sees only llvmpipe, use
the same offload/Xvfb path as the single-scene command above:

```sh
USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW=./build_ninja/tusdview \
  TUSDVIEW_NVIDIA_OFFLOAD=1 \
  TUSDVIEW_XVFB=1 \
  TUSDVIEW_VK_DEVICE=nvidia \
  examples/tusdview/tests/run-usd-assets-render-smoke.sh
```

If `xvfb-run` itself cannot create a usable local Unix socket because
`/tmp/.X11-unix` is not root-owned in the sandbox view, start Xvfb externally as
described in [OpenGL from a Codex sandbox](#opengl-from-a-codex-sandbox), then
run the harness against that display:

```sh
DISPLAY=localhost:88 \
  USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW=./build_ninja/tusdview \
  TUSDVIEW_NVIDIA_OFFLOAD=1 \
  TUSDVIEW_XVFB=external \
  TUSDVIEW_VK_DEVICE=nvidia \
  examples/tusdview/tests/run-usd-assets-render-smoke.sh
```

Limit the run while iterating:

```sh
USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW_USD_ASSETS_LIMIT=8 \
  TUSDVIEW_USD_ASSETS_MODES=vk-raster,vk-rt,cuda-rt,tusdr-cpu,tusdr-vk,tusdr-vkr \
  examples/tusdview/tests/run-usd-assets-render-smoke.sh
```

## Headless HW-accelerated GL (NVIDIA) + GPU profiling

Under `xvfb-run`, the **OpenGL** backend falls back to **llvmpipe** (Mesa software
rendering) — fine for correctness/screenshots, but ~50–90 ms/frame and useless for
profiling. On an NVIDIA host, route the GL context to the discrete GPU with the
PRIME render-offload env vars:

```sh
export __NV_PRIME_RENDER_OFFLOAD=1 \
       __GLX_VENDOR_LIBRARY_NAME=nvidia \
       __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
xvfb-run -a ./build_ninja/tusdview --backend gl --frames 8 --screenshot out.png model.usdz
# verify: glGetString(GL_RENDERER) now reports the GPU, not llvmpipe:
xvfb-run -a glxinfo | grep "OpenGL renderer"   # -> NVIDIA GeForce ... (was: llvmpipe)
```

- The **Vulkan** backend does not use these GLVND variables for device
  selection. It normally auto-selects a suitable device; when Xvfb or a
  container makes llvmpipe win, pass `--vk-device nvidia` (or let the test
  harness forward `TUSDVIEW_VK_DEVICE=nvidia`) after confirming the device with
  `vulkaninfo --summary`.
- **Caveat — wall-clock is misleading here.** PRIME renders on the NVIDIA GPU but
  *presents* through the software xvfb X server, so each frame pays a GPU→X blit
  (~89 ms/frame observed) that has nothing to do with draw cost. **Measure GPU-side,
  not with wall-clock**: `GL_TIME_ELAPSED` timer queries around the draw (GL), or
  `vkCmdWriteTimestamp` around the render pass (VK). A timer query bracketing just
  `drawMeshes` isolates a feature's GPU cost (e.g. the blendshape morph vertex
  shader) from the clear/grid/present overhead.

These vars are how the GPU blendshape-morph optimizations were profiled on an
RTX 3070 (the active-channel skip makes the morph's GPU cost scale with the number
of *active* targets, not the total target count).

### `tusdquicklook` on a constrained NVIDIA GPU

The lightweight Quick Look viewer has its own offscreen GL 3.3 path and a
separate `--max-gpu-mem` residency cap (512 MiB by default). Its GL context is
created lazily, and a reported free-VRAM value is reduced by a 256 MiB driver
reserve before the scene is accepted. This keeps a 2 GiB GPU usable for the
preview without allowing the driver to consume the whole device:

```sh
xvfb-run -a -s "-screen 0 800x600x24" env \
  __NV_PRIME_RENDER_OFFLOAD=1 \
  __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  ./build_ninja/tusdquicklook model.usdz --backend gl --max-gpu-mem 512 \
    --frames 4 --size 640x480 --screenshot /tmp/tusdquicklook.png --verbose
```

The Quick Look NVIDIA smoke is registered as
`example-tusdquicklook-nvidia-smoke`; it returns CTest skip code 77 when Xvfb
or a live NVIDIA driver is unavailable.

### `tusdquicklook` MCP control

The interactive Quick Look app also exposes a small local MCP server. Use
`--mcp-stdio` for newline-delimited JSON-RPC on stdin/stdout,
`--mcp-http[=PORT]` for the HTTP endpoint (default `8765`), or `--mcp` for
both. For example:

```sh
./build_ninja/tusdquicklook model.usdz --mcp-http=8765
```

POST MCP requests to `http://localhost:8765/mcp`. The available tools are
`load_usd`, `get_scene_info`, `list_prims`, `viewport`, `screenshot`,
`render_settings`, and `quit`. Tool calls are executed on the normal quicklook
UI thread, and `get_scene_info` reports progressive load/render state. The
interface is intended for trusted local clients and is not authenticated; it
cannot be combined with the short-lived `--screenshot` mode.

## Vulkan validation layers (debugging the Vulkan/threaded paths)

The Vulkan backend (raster, ray query, and the experimental `--threaded` render
thread) is best debugged under the Khronos validation layers. Distro packages are
often absent, so build them from source once. **Match the layer release to the
installed loader** (`vulkaninfo --summary | grep "Instance Version"`, e.g.
`1.3.275`):

```sh
# 1. Clone at the matching SDK tag (use your loader's version).
git clone --branch vulkan-sdk-1.3.275.0 --depth 1 \
  https://github.com/KhronosGroup/Vulkan-ValidationLayers.git
cd Vulkan-ValidationLayers

# 2. Fetch + build the dependencies (SPIRV-Tools, glslang, …). ~1–2 min.
python3 scripts/update_deps.py --dir external --arch x64 --config release \
  --generator Ninja

# 3. Configure + build the layer itself.
cmake -S . -B build -G Ninja -C external/helper.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build -j

# -> build/layers/libVkLayer_khronos_validation.so + VkLayer_khronos_validation.json
```

### Windows (one-shot script)

On Windows, `scripts/setup-vulkan-validation.ps1` obtains the validation layer
for you. `-FromSource` clones + builds Vulkan-ValidationLayers (it auto-fetches a
new enough Ninja — the layers' C++20 modules need Ninja ≥ 1.11 — and a clean
CPython for `update_deps.py`); the default tries the prebuilt LunarG SDK
(needs admin). It writes `activate-vulkan-validation.ps1`; dot-source it, then run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-vulkan-validation.ps1 -FromSource
. .\activate-vulkan-validation.ps1
.\build-msvc\Release\tusdview.exe --headless --frames 2 --screenshot out.ppm models\cube.usdc 2>&1 |
  Select-String VUID
```

This is how the AMD `vkCmdDrawIndexed` crash was root-caused to
`VUID-VkPushConstantRange-size-00298` (a 256-byte push-constant block on a device
whose `maxPushConstantsSize` is only 128). The repo-root `vk_layer_settings.txt`
enables GPU-Assisted Validation (set `VK_LAYER_SETTINGS_PATH` to it).

### Linux (build from source)

Run tusdview under it by pointing the loader at the built layer directory:

```sh
export VK_LAYER_PATH=/path/to/Vulkan-ValidationLayers/build/layers
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
./build_ninja/tusdview --backend vk --rt --frames 8 --screenshot out.png model.usdc \
  2>&1 | grep -E 'VUID|Validation'
```

Errors are printed to **stderr** as `VUID-…` messages naming the exact API call
and object. A clean run prints none. `--threaded` exercises the render thread (run
under `xvfb-run` if no display).

### GPU-Assisted Validation (shader faults / device loss)

Plain validation is CPU-side. When the symptom is a corrupt/blank frame or
`VK_ERROR_DEVICE_LOST` (which surfaces as `vkWaitForFences` returning `-4`), the
fault is usually a shader reading an out-of-bounds / unbound descriptor — invisible
to plain validation. Enable **GPU-Assisted Validation (GPU-AV)**, which
instruments the shaders, via a settings file:

```sh
cat > vk_layer_settings.txt <<'EOF'
khronos_validation.validate_gpu_based = GPU_BASED_GPU_ASSISTED
khronos_validation.gpuav_descriptor_checks = true
khronos_validation.gpuav_buffer_oob = true
EOF
export VK_LAYER_SETTINGS_PATH=$PWD/vk_layer_settings.txt   # + VK_LAYER_PATH / VK_INSTANCE_LAYERS above
./build_ninja/tusdview --backend vk --rt --frames 8 --screenshot out.png model.usdc 2>&1 \
  | grep -iE 'GPU-AV|index out of bounds|descriptor'
```

GPU-AV reports the faulting **pipeline, shader stage, instruction index and
fragment/thread coordinate**, e.g.
`(set = 0, binding = 0) Index of 0 used to index descriptor array of length 0 …
Stage = Fragment`. This is how the threaded VK ray-tracing blank-capture bug was
root-caused — see the "Validation-layer findings" section of
[`threading-stage2.md`](../examples/tusdview/doc/threading-stage2.md) for the full
worked example (a missing `ImDrawData::Textures` clone left the ImGui font
descriptor empty → device loss).

### Bounded native Ptex residency

The `--next` texture path keeps a coarse face-complete Ptex atlas inside
`--texture-budget-mb`. If fitting all requested face mips would exceed that
budget, it also reserves a fixed physical page cache (up to 32 MiB per texture,
and always inside the same total budget). Requested-quality faces are decoded
from the retained compressed `.ptx` source and uploaded over subsequent frames;
the alpha-only face table is patched after each upload. Reusing a slot first
redirects its old face to the permanent coarse fallback, so every face remains
valid during streaming and eviction.

Use `--ptex-initial-faces N` to control startup work (`0` restores eager/all
faces) and `--ptex-cache-mb N` to set the per-texture physical cache. Explicit
values override large-scene profile defaults, making production captures and
memory-limit rehearsals reproducible.

Raster demand is camera-driven at mesh granularity. A mesh contributes its
source-face ids only after its world bounds enter the current view; off-camera
meshes retain their delta-varint-compressed ids and cause no Ptex decode. A
later camera move admits newly visible meshes. Page inflation runs on a
background worker; completed pages are assigned deterministic LRU slots and
uploaded on the context thread in an independent 2 ms frame slice. Parsed Ptex
face/level tables are retained per texture rather than reopened for every page.
Vulkan/CUDA/HIP RT captures still request the
complete admitted face set because their ray footprint is not represented by
the raster visibility mask.

OpenGL raster, Vulkan raster, and Vulkan ray query share this mutable atlas
path. Mutable GL atlases sample level zero only, avoiding stale generated mips;
Vulkan uses partial transfer updates with explicit layout transitions. Atlases
that already fit at requested quality reserve no page-cache memory.

Render reports expose `ptex_requested_meshes`, `ptex_requested_faces`,
`ptex_gpu_physical_slots`, `ptex_gpu_page_uploads`,
`ptex_gpu_page_misses`, `ptex_gpu_page_evictions`, and the launched/completed
asynchronous page-job counts. For a small diagnostic
scene that would not naturally need paging, set
`TUSDVIEW_PTEX_FORCE_RESIDENCY=1` together with a constrained (for example,
64 MiB) `--texture-budget-mb` value to exercise replacement deterministically.
When authored mip levels are insufficient to keep every face represented, the
fallback builder performs an additional bounded coarse resample; native source
data remains available for requested-quality page upgrades.
