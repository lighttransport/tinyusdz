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
- **Vulkan ray tracing** (`--rt`) is compiled in whenever
  `vk/shaders/embedded/raytrace_comp.spv.h` is present (it is, by default) and
  activates at runtime on a GPU exposing `VK_KHR_acceleration_structure` +
  `VK_KHR_ray_query`; otherwise it falls back to rasterization. Regenerating that
  header is the only step that needs a `GL_EXT_ray_query`-capable glslang (≈
  glslang ≥ 11); build one once with `examples/common/build-glslang.sh` if you
  edit `vk/shaders/raytrace.comp`.
- **Experimental threaded render path** (a dedicated render thread owns the GL
  context / Vulkan queue so the UI never blocks on the GPU; opt-in `--threaded`)
  is gated behind a default-OFF option:

  ```sh
  cmake -S . -B build_ninja -DTUSDVIEW_ENABLE_GL_THREAD=ON   # + the flags above
  ```

  See [`threading-stage2.md`](../examples/tusdview/doc/threading-stage2.md). When
  OFF, `--threaded` is a no-op and the single-threaded path is unchanged.

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

The **`tusdview-cuda-render`** ctest exercises this end-to-end; it SKIPs
(return 77) when no NVIDIA device / NVRTC is available so non-NVIDIA CI stays
green.

## HIP/ROCm ray-tracing run test (verified working on AMD)

`--hip` is the AMD counterpart of `--cuda`: it traces the same scene BVH and runs
the **same trace kernel** (shared via `examples/tusdview/raytracer_kernel.inc`),
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

The **`tusdview-hip-render`** ctest exercises this end-to-end; it SKIPs
(return 77) when no AMD/ROCm device is available so non-AMD CI stays green.
Verified on AMD Radeon RX 9070 XT (gfx1201).

```sh
cd build && ctest -R tusdview-cuda-render --output-on-failure
```

**Verified working — NVIDIA GeForce RTX 5060 Ti (Linux, driver `610.43.02`,
NVRTC 12/13), 2026-06-28.** Renders the full scene (suzanne, 968 tris) non-blank
at 1469×1284. The debug AOVs were spot-checked too: `--mode normals` produces
smoothly varying RGB-encoded shading normals, and `--mode bvh-heatmap`
(traversal cost, rmode 27) shows the nested BLAS/TLAS node bounds with elevated
cost clustering on the silhouette. `--mode depth` gives a correct near→far ramp
(dark, since the range is spread by `depthScale`), `--mode uv` shows the R=u/G=v
UV gradient, and `--mode material-id` is a single uniform color (suzanne is one
material, so one id) — all correct.

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
- `--texture-compress off|bc|bc7` uses the `texcomp` encoders (`bc` picks
  BC1/BC3 by alpha; `bc7` needs BPTC support — GL 4.2 ext / VK BC feature).
- `--texture-mips on` builds content-aware CPU mip chains via `texpipe`
  (sRGB-correct filtering, alpha-coverage preservation for Mask materials,
  normal-map renormalization, variance-aware roughness-channel minification,
  wrap-mode-aware filter edges). GL uploads the precomputed levels instead of
  `glGenerateMipmap`; Vulkan gains real mip chains (it previously sampled only
  level 0).
- `--dome-ibl off|low|high` (default high) bakes DomeLight split-sum IBL at
  load via the `envmap` library from the HDR float envmap (EXR half/float and
  Radiance HDR decode correctly now): GGX-prefiltered specular cube chain,
  diffuse irradiance cube, and BRDF LUT. GL and Vulkan raster shade the
  ambient term with it; the VK ray-query and CUDA/HIP RT paths sample the dome
  environment on ray miss. The bake logs its wall time
  (`[tusdview] dome IBL bake ...`), typically ~0.3-0.5 s.

`tusdrender -ibl envmap` opts the offline renderer into the same envmap-library
precompute (default stays the built-in reference; measured parity on a dome
test scene: mean |diff| 0.16/255, 0.06% of channels >32/255).

## Headless screenshots (CI / no display)

```sh
# Vulkan, no X server at all (renders into an offscreen image + reads it back):
./build_ninja/tusdview --headless --frames 8 --screenshot out.png model.usdz
./build_ninja/tusdview --headless --rt --frames 8 --screenshot rt.png model.usdz

# Large-scene realtime presets: resolve Vulkan/LOD/budget flags and render a
# deferred-payload overview first. Payload roots with authored extents use them;
# otherwise compact marker boxes preserve the composed spatial distribution.
# The automatic profile also postpones DomeLight IBL precompute.
./build_ninja/tusdview --headless --large-scene-profile island \
  --frames 8 --screenshot island.ppm /path/to/island.usda

# Explicit complete/offline-quality load (higher latency and memory):
./build_ninja/tusdview --headless --large-scene-profile island \
  --load-payloads --dome-ibl high --frames 1 --screenshot island-full.ppm \
  /path/to/island.usda

# Optional local harness. Set any scene path env var that exists on the machine.
CALDERA=/path/to/caldera.usda ISLAND=/path/to/island.usda ALAB=/path/to/alab.usda \
  bash examples/tusdview/tests/run-large-scene-profiles.sh
# Use TUSDVIEW_SCENE_TIMEOUT=10m (default) to bound each large-scene run.

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

`.png` and `.ppm` outputs are both supported; `--frames N` loads synchronously so
screenshots are deterministic. Pixel-compare two screenshots (e.g. backend or
threaded-vs-single parity) with PIL/numpy:

The Vulkan RT shaded mode uses material base color, alpha, roughness, metallic,
and emissive constants plus interpolated `displayOpacity`. Material-image
sampling remains a separate follow-up: separate opacity images are supported by
GL/VK raster only (including UDIM masks), while RT receives the vertex alpha but
does not sample that image. This does not change texture resize or
compressed-texture behavior.

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
Wrap the viewer in Xvfb and pass the NVIDIA GLVND offload environment, then
select the NVIDIA Vulkan device explicitly:

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
`tusdview-usd-assets-render-smoke`, but skips unless `USD_ASSETS_ROOT` is set.
The tusdrender modes use `-autoframe` and the renderer's existing scene-light /
headlight fallback. This is basic load-and-render coverage, not a detailed
lighting match or perceptual golden-image test.

```sh
USD_ASSETS_ROOT=/path/to/usd-assets \
  TUSDVIEW=./build_ninja/tusdview \
  examples/tusdview/tests/run-usd-assets-render-smoke.sh
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

- The **Vulkan** backend already selects the discrete GPU on its own — these vars
  are not needed for `--backend vk` (confirm via `vulkaninfo --summary`, or that
  `maxBoundDescriptorSets` is 32 rather than llvmpipe/lavapipe's 8).
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
