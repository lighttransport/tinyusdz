# tusdrender — Vulkan backend testing

`tusdrender` (CPU preview ray-tracer, see
[`tools/tusdrender/README.md`](../tools/tusdrender/README.md)) also has an
optional **GPU backend** built on the vendored LightRT Vulkan path
(`src/external/lightrt/lightrt_c_vk.c` + `lightrt_vkew.c`, driven by
`tools/tusdrender/tusdr_vulkan.cc`). It is **runtime-loaded via vkew** — no
Vulkan SDK is needed to build, and it is compiled in whenever
`-DTUSDRENDER_WITH_VULKAN=ON` (the default). At runtime it picks a Vulkan device,
uploads the LightRT BVH, and traces in a compute shader (buffer-device-address);
`-vkr` additionally uses hardware ray tracing when the device exposes it,
otherwise it falls back to the same compute trace.

```sh
# CPU reference (always correct):
tusdrender models/suzanne-pbr.usda cpu.png -rtPreview -w 320 -height 240 -autoframe
# Vulkan compute trace:
tusdrender models/suzanne-pbr.usda vk.png  -vk      -w 320 -height 240 -autoframe
# Vulkan hardware ray query (RDNA2+; else falls back to compute trace):
tusdrender models/suzanne-pbr.usda vkr.png -vkr     -w 320 -height 240 -autoframe
```

A correct `-vk`/`-vkr` image must match the `-rtPreview` reference (same framed
mesh). The startup log prints the chosen device and path, e.g.:

```
Vulkan device: <GPU name>
Vulkan caps: compute=1 buf_addr
backend: LightRT VK (compute trace)   # or "(ray query)" with -vkr on RT hardware
```

## ⚠️ Known-broken configuration (please retest elsewhere)

On the dev machine the Vulkan backend is **broken**, but the CPU path renders the
same scene perfectly — so the BVH/geometry is fine and the fault is GPU-side
(driver or the LightRT VK compute path), not in the scene conversion.

| | |
|---|---|
| **GPU** | AMD **Radeon RX 570** (Polaris / GCN 4, 2017) |
| **Driver** | AMD Windows driver — reports driverID `AMD_PROPRIETARY` (amdvlk64.dll) |
| **Symptom** | `-vk`/`-vkr` run (`rc=0`) but render only a sparse scatter of fragments instead of the mesh; **hangs** at larger resolutions (320×240 timed out >2 min) |
| **CPU `-rtPreview`** | ✅ correct, full render |

This is the same GPU/driver that exposed an unrelated Vulkan bug in `tusdview`
(amdvlk's low `maxPushConstantsSize`); amdvlk on this Polaris card is suspect.

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
