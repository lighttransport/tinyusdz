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

- **Vulkan** is enabled automatically when `find_package(Vulkan)` succeeds (needs
  a build-time `glslangValidator` to compile the shaders to SPIR-V). Force a
  GL-only build with `-DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON`.
- **Vulkan ray tracing** (`--rt`) additionally needs a `glslangValidator` that
  supports `GL_EXT_ray_query` (≈ glslang ≥ 11 / a Vulkan SDK build) and a GPU
  exposing `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`. CMake probes the
  compiler at configure time; if it can't compile `vk/shaders/raytrace.comp` the
  Vulkan backend silently uses rasterization. Build a local glslang once if the
  system one is too old: `examples/common/build-glslang.sh`.
- **Experimental threaded render path** (a dedicated render thread owns the GL
  context / Vulkan queue so the UI never blocks on the GPU; opt-in `--threaded`)
  is gated behind a default-OFF option:

  ```sh
  cmake -S . -B build_ninja -DTUSDVIEW_ENABLE_GL_THREAD=ON   # + the flags above
  ```

  See [`threading-stage2.md`](../examples/tusdview/doc/threading-stage2.md). When
  OFF, `--threaded` is a no-op and the single-threaded path is unchanged.

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
