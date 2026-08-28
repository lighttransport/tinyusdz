# UE 5.8 MetaHuman to tusdview

This reproducible bridge assembles the installed offline `Ada` MetaHuman
Character preset, exports its LOD0 head and body through Unreal's native USD
skeletal-mesh exporter, and authors a portable hero layer with MaterialX skin,
strand hair (`BasisCurves`), and hair cards (`Mesh`). Generated assets remain in
the gitignored `output/` and `Content/Generated/` directories.

```sh
tools/ue-metahuman-usd/run-export.sh

env __NV_PRIME_RENDER_OFFLOAD=1 \
  __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  build_ninja/tusdview --headless --backend vk --vk-device nvidia \
  --mode shaded --no-grid --view-dir 0,1,0 --frames 3 \
  --screenshot /tmp/metahuman-vk.ppm \
  tools/ue-metahuman-usd/output/MetaHuman_Hero.usda
```

The UE commandlet runs with `-NullRHI`. UE 5.8's material baker requires a live
RHI and crashes in `FExportMaterialProxy` in this mode, while its public groom
USD conversion is import-only. Consequently the commandlet disables material
baking and the postprocessor authors renderer-independent MaterialX lookdev.
The offline preset also lacks the optional high-resolution texture sources, so
the default look uses calibrated constant parameters rather than silently
requiring an authenticated texture download.

The Vulkan skin path consumes OpenPBR subsurface weight, color, radius, and RGB
radius scale using a bounded Burley-style diffusion approximation. Native
curves now carry their strand tangent into the fragment stage and evaluate
three shifted longitudinal R/TT/TRT lobes inspired by UE's `HairBsdf.ush`, with
authored display color acting as the MaterialX absorption result. This is a
raster implementation only; `--rt` is intentionally not part of this workflow.

Run the dependency-free structural smoke test with:

```sh
tools/ue-metahuman-usd/test-export.sh
```
