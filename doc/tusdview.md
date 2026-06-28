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

```sh
cd build && ctest -R tusdview-cuda-render --output-on-failure
```

**Verified working — NVIDIA GeForce RTX 5060 Ti (Linux, driver `610.43.02`,
NVRTC 12/13), 2026-06-28.** Renders the full scene (suzanne, 968 tris) non-blank
at 1469×1284. The debug AOVs were spot-checked too: `--mode normals` produces
smoothly varying RGB-encoded shading normals, and `--mode bvh-heatmap`
(traversal cost, rmode 27) shows the nested BLAS/TLAS node bounds with elevated
cost clustering on the silhouette — both correct.

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

## Headless screenshots (CI / no display)

```sh
# Vulkan, no X server at all (renders into an offscreen image + reads it back):
./build_ninja/tusdview --headless --frames 8 --screenshot out.png model.usdz
./build_ninja/tusdview --headless --rt --frames 8 --screenshot rt.png model.usdz

# OpenGL (needs a window/context) on a headless host — wrap in a virtual X server
# with a 24-bit visual:
xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build_ninja/tusdview --backend gl --frames 8 --screenshot out.png model.usdz
```

`.png` and `.ppm` outputs are both supported; `--frames N` loads synchronously so
screenshots are deterministic. Pixel-compare two screenshots (e.g. backend or
threaded-vs-single parity) with PIL/numpy:

```sh
python3 -c "from PIL import Image; import numpy as np; \
a=np.array(Image.open('a.png').convert('RGB')).astype(int); \
b=np.array(Image.open('b.png').convert('RGB')).astype(int); \
print('maxdiff', int(np.abs(a-b).max()))"
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
