# UE 5.8 MetaHuman to tusdview

This reproducible bridge assembles the installed offline `Ada` MetaHuman
Character preset, exports its LOD0 head and body through Unreal's native USD
skeletal-mesh exporter, exports the native head/body MetaHuman DNA rigs, and
authors a portable hero layer with MaterialX skin, authored strand hair
(`BasisCurves`), and hair cards (`Mesh`). Generated assets remain in
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

The UE commandlet runs with `-NullRHI`. On first run it compiles the included
editor-only `TinyUSDZGroomExport` module, because `UnrealEditor-Cmd` will not
compile a project plugin by itself. UE 5.8's material baker requires a live RHI
and crashes in `FExportMaterialProxy` in this mode, while the stock USD groom
conversion is import-only. The bridge instead reads `UGroomAsset`'s public
editable `FHairDescription` and writes real linear USD `BasisCurves`, including
vertex positions, widths and display color. It selects the installed
`Hair_S_Casual` groom and uniformly samples its 101,871 source strands to at
most 15,000 strands for interactive Vulkan raster rendering; the exported USD
records both counts and the source asset path. The postprocessor references
those curves and retains cards only as a fallback representation. Consequently
the commandlet disables material baking and the postprocessor authors
renderer-independent MaterialX lookdev.
The offline preset also lacks the optional high-resolution texture sources, so
the default look uses calibrated constant parameters rather than silently
requiring an authenticated texture download.

The head and body USDC files contain `UsdSkelSkeleton`, bind/rest transforms,
joint order, and per-vertex joint indices and weights. MetaHuman facial
deformation does not use ordinary `UMorphTarget` assets: its blend-shape target
deltas, GUI/raw controls, PSD mappings, joint groups and animated maps live in
the companion `*_Head.dna` and `*_Body.dna` RigLogic files. The offline Ada
preset currently supplies body DNA only: producing fitted head DNA requires
UE's authenticated `RequestAutoRigging` cloud operation. The exporter records
that condition without substituting the generic archetype DNA, which would not
match this character. A small
`MetaHuman_Rig.usda` manifest records that relationship without pretending the
opaque DNA payload is a standard USD schema. Converting every DNA delta into
`UsdSkelBlendShape` prims is the next interoperability step.

The exporter also writes `MetaHuman_Deformers.usda` and
`MetaHuman_Physics.usda`. `UsdSkel` remains authoritative for skeletons and
skinning. DNA/RigLogic deformation metadata (DNA files, GUI controls, PSD
controls, joint groups and animated maps) is preserved with namespaced
`unreal:deformer*` custom properties because it has no lossless stock USD
schema. UE `PhysicsAsset` body/constraint counts and source-asset identity are
also emitted as `unreal:*` custom metadata: a bone-bound ragdoll PhysicsAsset
is not automatically equivalent to a `UsdPhysics` scene.

When Epic authentication and network access are configured, request the full
fitted face rig (joints and blend shapes) before export with:

```sh
TINYUSDZ_UE_AUTORIG=1 tools/ue-metahuman-usd/run-export.sh
```

This uses UE's blocking `RequestAutoRigging` API and then exports the resulting
head DNA. It is intentionally opt-in because it calls Epic's remote service.

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
