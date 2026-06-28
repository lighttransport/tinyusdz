# tusdrender — GPU backend testing (Vulkan / Direct3D 11)

`tusdrender` (CPU preview ray-tracer, see
[`tools/tusdrender/README.md`](../tools/tusdrender/README.md)) has two optional
**GPU compute backends** that traverse the same LightRT BVH:

- **Vulkan** (`-vk` / `-vkr`) — `src/external/lightrt/lightrt_c_vk.c` +
  `lightrt_vkew.c`, driven by `tusdr_vulkan.cc`. Runtime-loaded via vkew (no
  Vulkan SDK to build), on by default (`-DTUSDRENDER_WITH_VULKAN=ON`). `-vkr`
  uses hardware ray tracing when present, else falls back to compute trace.
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
# Direct3D 11 compute trace (Windows):
tusdrender models/suzanne-pbr.usda d3d.png -d3d -w 320 -height 240 -autoframe
```

A correct GPU image must match the `-rtPreview` reference (same framed mesh).
The startup log prints the chosen device + path, e.g. `backend: LightRT VK
(compute trace)` / `LightRT D3D11 (compute trace, N rays in 1 dispatch)`.

## ⚠️ Known-broken configuration (please retest elsewhere)

On the dev machine **both** GPU backends mis-render, while the CPU path renders
the scene perfectly — so the BVH/geometry is fine and the fault is GPU-side, in
the compute trace itself, not the scene conversion.

| | |
|---|---|
| **GPU** | AMD **Radeon RX 570** (Polaris / GCN 4, 2017) |
| **Driver** | AMD Windows driver — Vulkan driverID `AMD_PROPRIETARY` (amdvlk64.dll) |
| **`-vk` / `-vkr`** | run (`rc=0`) but render only a sparse Suzanne silhouette (most interior rays miss); **hang** at larger resolutions (320×240 timed out >2 min — the Vulkan helper does one GPU round-trip *per pixel*) |
| **`-d3d`** | runs (`rc=0`, one batched dispatch, no hang) but produces the **same** sparse silhouette as Vulkan |
| **CPU `-rtPreview`** | ✅ correct, full render |

**Key finding:** the D3D11 path was added specifically to test whether the mature
D3D driver fixes this — it does **not**. Vulkan and D3D11 produce the *same*
wrong image (the HLSL is decompiled from the same SPIR-V), so the bug is in the
LightRT GPU **trace compute shader on GCN/Polaris**, not in amdvlk. (The
silhouette-only pattern — edges hit, interior misses — looks like a
traversal-depth / stack issue specific to this GPU; the shader works on the
reference RTX 3070.) The D3D11 backend itself is correct and is the better one to
develop against (single batched dispatch, no per-pixel round-trip, no hang). It
should render correctly on a GPU where the compute shader is sound.

## Retesting on another AMD GPU / driver

To confirm whether the Vulkan backend is sound (and just blocked by this old
card's driver), retest on a machine with a **proper AMD Vulkan driver**:

- **Newer AMD card — RDNA2 / RDNA3** (RX 6000 / RX 7000): has hardware ray
  tracing, so `-vkr` exercises the real ray-query path, and the Adrenalin /
  amdvlk stack on these cards is far better maintained than on Polaris. This is
  the most useful retest.
- **Linux + Mesa RADV** (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json`):
  RADV is an independent, very robust open-source AMD Vulkan driver — a good way
  to tell a tusdrender bug apart from an amdvlk bug. (Pick the GPU/ICD with
  `vulkaninfo --summary`.)
- Avoid relying on **amdvlk on GCN/Polaris**; prefer the vendor driver on RDNA,
  or RADV.

What to check on the new device:

```sh
tusdrender scene.usda cpu.png -rtPreview -w 320 -height 240 -autoframe
tusdrender scene.usda vk.png  -vk        -w 320 -height 240 -autoframe
tusdrender scene.usda vkr.png -vkr       -w 320 -height 240 -autoframe
# vk.png / vkr.png should look like cpu.png. Also try a larger size (1920x1080)
# to check the resolution-dependent hang seen on the RX 570 is gone.
```

If it still renders wrong, capture a definitive reason with the **Khronos
validation layers** — build/run them with the helper added for tusdview
(`scripts/setup-vulkan-validation.ps1 -FromSource`, see
[`doc/tusdview.md`](tusdview.md)); the layer DLL works for any Vulkan app:

```powershell
. .\activate-vulkan-validation.ps1
.\build\tools\tusdrender\tusdrender.exe scene.usda vk.png -vk -w 320 -height 240 -autoframe 2>&1 |
  Select-String "VUID|Validation"
```

Note the layer output (and tusdrender's own diagnostics) goes to stdout; capture
both streams. Record the working GPU + driver version here once verified.
