# tusdview / tusdrender — open tasks & working notes

Single source of truth for in-flight work on the two renderers and the texture
program. Supersedes the old `doc/tusdview-audit.md` (full audit) and the
untracked `resume-tex.md` (KTX2 resume) — done history was dropped on merge; it
lives in git history if needed. Updated 2026-07-20.

The original two programs documented below -- (A) the **tusdview/tusdrender
audit** and (B) the **texture-compression / KTX2** work -- are complete.  The
active work is now the USD rendering-fidelity roadmap below.  This file is the
canonical task list; the repository-root `tasks.md` is historical planning
input and must not be used as evidence that an unchecked feature is missing.

---

## Active USD rendering-fidelity roadmap

Completion policy: shared schema/Tydra extraction is preferred over viewer-only
duplication.  Each feature lands in OpenGL and Vulkan raster first, then in
Vulkan ray query and the shared CUDA/HIP tracer.  Unsupported inputs must
produce structured diagnostics with their material/prim path rather than
silently becoming the default material.

| Area | Default `--next` | Legacy | GL/VK raster | VK/CUDA/HIP RT |
|---|---|---|---|---|
| PreviewSurface core semantics | supported | supported | supported | supported |
| OpenPBR/MaterialX advanced lobes | degraded | degraded | unsupported | partial constants |
| Ordinary/UDIM texture mips | supported | supported | supported | trilinear footprint LOD |
| Mesh/analytic/subdivision/instancing | supported | supported | supported | supported |
| Points and Basis/NURBS/Hermite curves | extracted | partial ribbons | GL raster | solid proxies |
| Authored camera | perspective + orthographic | perspective + orthographic | filmback/offset/exposure | filmback/offset/exposure |
| USD lights | full shared extraction | full extraction | linked multi-light + dome IBL | multi-light + dome IBL |

### Current implementation order — Linux-first

| Priority | Remaining deliverable | Why it comes here |
|---|---|---|
| **P0** | Tydra-owned real-time PBR record plus `--next`/legacy extraction-equivalence tests | **Implemented locally;** canonical packing and supported-material image parity now prevent the loaders from drifting. |
| **P1** | Independent semantic texture descriptors and a checked-in material grid | **In progress:** descriptors are self-contained after DrawScene texture deduplication; a complete semantic grid is still needed. |
| **P2** | Pixel parity for ordinary/UDIM material response in Vulkan RT and CUDA/HIP where available | **In progress:** RTX 3070 headless raster, Vulkan RT, and CUDA now cover the opacity/UDIM probes; expand this to the full semantic texture grid and retain HIP as a capability-gated check. |
| **P3** | Exact omnidirectional point-light raster shadows | **Implemented and Vulkan-verified:** six depth faces replace the one-sided finite approximation for point/zero-radius SphereLight emitters. |
| **P4** | Preserve evaluated graph inputs for advanced OpenPBR/MaterialX lobes | Keep unsupported lobes diagnosed while eliminating needless loss of supported inputs. |
| **P5** | Vulkan raster Points/Curves and native-carrier picking | Important viewport parity, but outside the material/lighting critical path. |
| **P6** | Area-light sampling, IES/portal/geometry/emissive lights, DOF/motion/stereo, and external-corpus goldens | Requires broader rendering scope or unavailable data; retain structured diagnostics and capability skips in the meantime. |

P0–P2 are one material-parity release gate. CUDA/HIP checks remain conditional
on a usable Linux GPU; lack of that hardware is a skip, not evidence of parity.

### P0--P2 -- Material and texture parity

- [x] Add one Tydra-owned backend-neutral real-time PBR material block,
  populated identically by the default `--next` and legacy loaders, then map it
  into `DrawMaterialCPU` and the raster/RT packing layouts. `RealtimePbrMaterial`
  is the canonical type; compatibility aliases preserve the older LightRT name.
- [x] Compare a supported OpenPBR material through default and legacy loaders.
  `tusdview-unsupported-realtime-lobes` now also requires its screenshot pixels
  to agree (mean absolute delta <= 2) before checking shared diagnostics.
- [ ] Cover base/diffuse, metalness, specular workflow/IOR, occlusion, coat,
  emission, opacity/cutout, normal, coat normal, and displacement for
  UsdPreviewSurface, OpenPBR, and MaterialX standard-surface graphs, with one
  independent semantic texture descriptor per input.
- [ ] Preserve successfully evaluated inputs when a graph degrades and report
  unsupported real-time lobes (transmission, subsurface, sheen, anisotropy,
  thin film, dispersion, volume) explicitly. Structured, path-qualified
  diagnostics now cover these advanced lobes in both loaders and the default
  next-core carrier retains the previously missing thin-film, dispersion, and
  extended anisotropy inputs; full degraded-graph preservation remains open.

- [x] Make every copied texture descriptor self-contained after DrawScene
  texture deduplication: its `tex` id now equals the mapped material slot, not
  the source RenderScene id. The focused material test forces two source
  UVTexture nodes to deduplicate and checks this invariant.
- [ ] Give every PBR texture input the same image/UDIM, channel, color-space,
  UV-set, transform, scale/bias, and wrap descriptor; add occlusion and coat
  semantic slots without re-aliasing independent metallic/roughness inputs.
- [x] Carry complete ordinary/compressed/UDIM mip chains into the RT texture
  table and use ray-footprint LOD plus trilinear filtering in Vulkan, CUDA, and
  HIP.
- [ ] Pin packed-map, sparse-UDIM, scalar-color-space, and external/USDZ parity
  for every new slot.
- [x] Run the material-binding texture-sampling regression on Linux hardware:
  default and `--legacy-load` both produce non-flat ordinary texture sampling
  and their RTX 3070 Vulkan-raster PPMs agree exactly (mean absolute delta
  0.000). The script still skips this assertion on llvmpipe/lavapipe.

### P3 -- Shading

- [x] Replace the raster preview's half-Lambert/Phong approximation with
  linear-light Cook-Torrance GGX (Smith masking + Schlick Fresnel),
  energy-conserving diffuse/specular separation, and a second coat lobe.
- [x] Apply occlusion only to indirect light and evaluate the same lobes for
  DomeLight IBL; keep AOV and alpha behavior unchanged.
- [ ] Match the evaluated material response in Vulkan ray query and the shared
  CUDA/HIP tracer after Linux GL/Vulkan raster parity is pinned. Occlusion and the
  coat weight/color/roughness, specular-workflow color, and dedicated coat-normal
  maps now reach every raster and RT backend with independent UV routing and
  scale/bias (plus channel selection for scalar slots). Full ordinary/UDIM
  visual parity coverage remains open.

### P4 -- Geom/Prim support

- [x] Feed Tydra `RenderPoints` and `RenderCurves` into the default `--next`
  DrawScene instead of limiting visible geometry to meshes.
- [x] Render Points in OpenGL as world-sized camera-facing instanced quads with
  widths, displayColor/displayOpacity, materials, animation-time evaluation,
  purpose filtering, and material/prim/mesh/purpose AOV ids.
- [x] Render Basis/NURBS/Hermite curves in OpenGL as camera-facing ribbons using
  Tydra's tessellated centerlines and interpolated widths/colors/opacity.
- [ ] Add the matching Vulkan raster point/ribbon pipeline and GL/Vulkan image
  parity coverage. Current Vulkan raster deliberately uses the shared solid
  RT-proxy fallback, which is visible but not camera-facing: the RTX 3070
  checked fixture reports 75,435 colored pixels versus 6,142 for GL ribbons.
  Implement the dedicated pipeline before adding a pixel tolerance.
- [ ] Extend viewport click/region picking, framing, visibility, and selection
  highlights from mesh-only indices to native Points/Curves carriers.
- [x] Use width-aware octahedron/tube proxy geometry for Points/Curves in
  Vulkan ray query and the shared CUDA/HIP tracer, preserving color and
  opacity.

### P5 -- Camera

- [x] Preserve authored perspective/orthographic projection, full filmback,
  aperture offsets, clipping range, and exposure in both loaders.
- [x] Add `--camera-conform fit|crop|horizontal|vertical|none` (default `fit`),
  with matching config and GUI controls, and use the same projection in all
  backends and headless rendering.
- [ ] Keep depth of field, shutter/motion blur, stereo, and arbitrary clipping
  planes as documented future work.

### P6 -- Lighting

- [x] Make default `--next` light extraction retain the complete shared
  DrawLightCPU data used by legacy conversion, including normalize, shaping,
  diffuse/specular multipliers, shadows, and light links.
- [x] Replace the single derived preview light with linked multi-light direct
  evaluation for distant, point/sphere, rect, disk, and cylinder lights while
  preserving DomeLight IBL. Raster evaluation is bounded to the first 16
  supported lights in stage order, diagnoses truncation, applies per-mesh
  collection masks, and currently evaluates finite area lights at their
  representative point with authored shaping.
- [x] Add one deterministic 2048-square raster shadow map (first enabled
  DistantLight, otherwise the first finite light in stage order)
  with 3x3 PCF. Unshaped finite emitters use a stable scene-fitted
  representative-direction orthographic approximation; authored shaping cones
  retain perspective projection.
  Landed in GL and Vulkan raster and pinned by
  `tusdview-raster-shadow-map`, which requires a *bounded* dark band (lit floor
  on both sides) so a collapsed light-space transform that darkens the whole
  floor cannot pass. Verified discriminating: the same fixture with
  `shadow:enable = 0` scores 0 shadow rows on both backends versus 27 (GL) and
  33 (VK) enabled. Direct and shadow collections now use independent per-mesh
  masks in GL and Vulkan. Ordinary and UDIM base-alpha/separate-opacity masks
  cut holes in the GL/Vulkan shadow pass, and Vulkan instanced prototypes now
  cast through a dedicated deformation-aware shadow pipeline. The checked-in
  regression now requires an opaque bounded band, zero rows for constant and
  sparse-UDIM rejected cutouts, and the same bounded band from a PointInstancer
  prototype.
- [x] Add exact omnidirectional point shadows in GL and Vulkan raster. Both
  backends render six shared 90-degree depth faces and select the matching
  projected-depth cubemap face during direct-light evaluation. The Vulkan
  regression uses a zero-radius `SphereLight` (UsdLux's point-emitter form) and
  proves a bounded shadow band. Finite non-zero area Sphere/Cylinder sampling
  remains representative-point shading, not an area-light implementation.
- [x] Use per-light visibility rays for shadows in Vulkan ray query and the
  shared CUDA/HIP tracer. RT instances now carry independent direct-light and
  shadow collection masks, so linked meshes receive only the authored lights
  and only authored shadow casters can occlude them. Selective membership is
  exact for the first 32 stage-order light records; later records retain
  collection-all behavior.
- [x] Diagnose rather than silently approximate GeometryLight, PortalLight,
  IES, and emissive-mesh light sampling until dedicated implementations land.

### Acceptance gates

- [ ] Shared extraction tests prove `--next`/legacy equivalence for material,
  texture, camera, and light records. Material/texture and light records are now
  pinned; camera remains to be consolidated into the same acceptance gate.
  `tusdview-light-record-equivalence` compares eight authored lights across both
  loaders, including type, transforms, shape, normalize, diffuse/specular,
  shadow parameters, derived intensity, and collection-all state. It exposed
  and fixed the next carrier dropping generic shaping cone angles on non-spot
  lights. The same gate now covers Rect, Sphere, Distant, Disk, Cylinder, and
  Dome records. Adding Dome exposed the next loader leaving its direction at
  the default despite an authored rotation; position/direction now derive from
  its computed world transform and match legacy numerically. The dumped record
  also carries the Dome projection enum, with latlong, mirrored-ball, and
  angular tokens all pinned. A generated latlong texture now verifies asset
  retention and matching baked IBL carriers (specular 64, irradiance 32, BRDF
  LUT 64, environment cube 256). The legacy-only decoded 2D texture index is
  deliberately excluded because next consumes the shared HDR cube directly.
- `tusdview-dome-orientation` now renders a sphere under an asymmetric latlong
  environment at 0 and 90 degrees. Rotation produces a 4.55 image MAD, proving
  the transform affects IBL sampling, while both orientations have exact
  default/legacy Vulkan pixels (MAD 0.0).
  Both orientations are now also stored as USDZ with the latlong environment
  map resolved from the archive. Default and legacy packaged renders match
  their external USDA/PPM counterparts exactly (package MAD 0.0).
- [ ] Checked-in fixtures cover a PBR material grid, packed/UDIM minification,
  Points and all curve families, perspective/orthographic lens shift,
  multi-light linking, and raster shadows. The linked red/blue/magenta raster
  fixture is checked in and compares GL/Vulkan output. The generated raster
  shadow regression covers the current baseline; a checked-in alpha-cutout and
  instancing fixture is still outstanding.
- [ ] GL/Vulkan raster images agree within the focused-test tolerances before
  the corresponding Vulkan/CUDA/HIP RT task is closed.
- [ ] Run the curated external usd-assets golden sweep when the corpus is
  mounted; missing external data remains a skip, not a normal-test failure.
- [ ] Run focused tusdview tests, full native CTest, and the large-scene
  first-display/VRAM comparison. Regenerate embedded SPIR-V with the documented
  SDK glslang whenever Vulkan shader sources change.

---

## Active-roadmap verification evidence

Latest focused verification on 2026-07-20:

- Offline RTX 3070 verification used the documented `--headless` Vulkan path.
  Vulkan raster and Vulkan ray query both passed the UDIM opacity-cutout probe
  (raster: red=2415, green=2585; RT: red=2591, green=2620), and RT preserved
  varying `displayOpacity` (4502 green samples, intensity spread 71). The
  shared CUDA/HIP kernel had an obsolete `mul` call in its directional-light
  branch; replacing it with the existing `scale` helper restored NVRTC
  compilation. CUDA then passed the same RTX 3070 display-opacity (15382 green
  samples, spread 44) and UDIM cutout (red=10082, green=10368) checks. HIP was
  not available on this NVIDIA host. The aggregate opacity script's Vulkan
  subrun exceeded its harness timing envelope, so these are retained isolated
  hardware invocations rather than a falsely reported aggregate pass.
- `tusdview-specular-workflow` now invokes a real Xvfb OpenGL render (it no
  longer accidentally supplies `--headless`, which selects Vulkan). On the RTX
  3070 its green specular-workflow highlight is identical in GL and Vulkan:
  G=217, dominance=153. The harness also fixes the window size through a local
  config so this visual threshold is stable in headless Linux runs.
- `tusdview-texture-semantic-aov` is registered as a capability-gated release
  regression. It drives raw image ramps through UsdPreviewSurface, OpenPBR,
  and MaterialX standard-surface base color, metallic, roughness, emission,
  and opacity inputs, then checks the corresponding raster/RT/CUDA AOVs.
  While adding it, the default next-core loader was found to rebuild its
  canonical PBR record with texture-unaware USD fallback constants; Vulkan RT
  therefore made a textured metallic AOV flat. The handoff now neutralizes all
  live base/metal/roughness/emission/opacity/specular/coat slots before packing;
  the RTX 3070 Vulkan-RT metallic ramp is restored (delta 102.3). Isolated
  hardware slices also pass Vulkan-RT emission (delta 97.9) and CUDA metallic
  / emission (75.1 / 74.2). `TUSDVIEW_SEMANTIC_BACKENDS` and
  `TUSDVIEW_SEMANTIC_MODES` select one slice when a driver needs serial process
  launches; `TUSDVIEW_SEMANTIC_LOADERS=default legacy` additionally requests
  pixel comparison between the two loader paths. The generator emits strict
  one-property-per-line USDA so both parsers consume the same fixture; retained
  load errors are hard failures rather than screenshots of the prior scene.
  PreviewSurface, native OpenPBR, and standard-surface scalar AOVs now agree
  exactly between loaders on Vulkan raster (MAD 0.0). Dedicated `coat-weight`
  `coat-color`, and `coat-roughness` AOVs replace the former lighting-sensitive
  exception and likewise agree exactly between loaders on Vulkan raster and
  Xvfb OpenGL. Vulkan ray query and CUDA also pass all three evaluated coat
  channels, including exact default/legacy comparisons. The default
  CTest still runs the complete available matrix. A
  focused bridge unit test independently locks the equivalent legacy/DrawScene
  path.
- The semantic grid now uses a packed ORM-style source for metallic/roughness:
  R rises while G falls across the same image. Vulkan raster proves both
  independent channel routes (metallic +80.1, roughness -73.6), while its
  separate grayscale emission input rises +76.7. Vulkan RT/CUDA slices use the
  same fixture and capability-gated assertions.
- The semantic harness now also creates raw two-tile normal maps and requires
  the normals AOV to show the authored red/blue vector directions. It exercises
  OpenPBR `geometry_coat_normal`, PreviewSurface occlusion, and texture-driven
  coat weight/color descriptors in the same backend matrix, then packages the
  normal fixture as a stored USDZ and requires its output to match the external
  texture form. GL runs through Xvfb; Vulkan RT, CUDA, and HIP remain
  capability-gated.
- MaterialX standard-surface `specular_roughness` textures now fall back into
  the single real-time roughness lane in both scene-loader adapters. A dedicated
  `coat-normal` AOV replaces the former lighting-sensitive coat-normal oracle;
  `coat-weight`, `coat-color`, and `coat-roughness` expose the remaining coat
  descriptors directly. Vulkan raster material descriptors now select cached
  samplers from each
  slot's independent S/T wrap modes instead of forcing global repeat.
- The legacy MaterialX converter now recognizes direct UsdUVTexture connections
  before attempting NodeGraph traversal and preserves
  `geometry_coat_normal`, which was previously dropped by its compatibility
  OpenPBR intermediate. Proper ND_image coat-normal graphs now produce exact
  next/legacy Vulkan-raster pixels (MAD 0.0).
- A dedicated `specular-f0` AOV evaluates PreviewSurface specular-workflow
  color after texture sampling. It exposed and fixed a legacy DrawScene adapter
  omission: `useSpecularWorkflow` was never copied into `DrawMaterialCPU`, so
  every legacy backend ignored an otherwise valid specular-color map. Xvfb GL,
  Vulkan raster/ray query, and CUDA now pass the red/blue F0 grid with exact
  default/legacy image parity (MAD 0.0).
- The same AOV now covers OpenPBR and MaterialX Standard Surface without
  conflating their semantics with PreviewSurface: `specular_color` tints the
  IOR-derived dielectric F0 before the metalness blend. A separate `ior-f0`
  diagnostic covers authored IOR 1.5 versus 2.5 for all three shader families.
  GL, Vulkan raster/ray query, and CUDA pass both diagnostics through the
  default and legacy loaders with exact cross-loader pixels (MAD 0.0); HIP
  remains capability-gated on the NVIDIA test host.
- Preview, OpenPBR, and Standard Surface specular-F0 fixtures now cross a
  two-tile UDIM. Preview's specular-workflow addition is pixel-exact between
  loaders in raster and CUDA; Vulkan RT passes with maximum loader MAD 0.314.
  This exposed Vulkan raster's former 2D-only treatment of the newer specular
  slot; it now has a dedicated array binding and atlas-row lookup. Vulkan
  raster, ray query, and CUDA pass ordinary and UDIM F0 through both loaders;
  raster and RT/CUDA loader comparisons are exact apart from the existing
  subpixel Preview RT tolerance.
  Their ordinary specular textures are now also stored in USDZ. All 18
  external/package comparisons across both loaders and Vulkan raster/RT plus
  CUDA are pixel-exact, covering both Preview direct-F0 and IOR-tinted
  OpenPBR/Standard semantics.
- Coat weight, color, and roughness AOVs now also cross two UDIM tiles. Vulkan
  raster gained array descriptors and atlas-row routing for those three slots.
  The paired loader test exposed legacy scalar UDIMs inheriting an sRGB decoder
  default; post-material usage classification now forces scalar, normal, and
  displacement semantics to raw before mip generation/upload. Vulkan raster
  gives exact default/legacy coat pixels (MAD 0.0), and Vulkan ray query plus
  CUDA pass the ordinary/UDIM response fixtures.
  PreviewSurface clearcoat weight and roughness now have their own ordinary and
  UDIM cases as well; Vulkan raster is exact between loaders, and Vulkan RT/CUDA
  pass with loader MAD at or below 0.474.
  Standard Surface now independently covers `coat`, `coat_color`, and
  `coat_roughness` through ordinary and UDIM sources. Vulkan raster is exact
  between loaders; Vulkan RT/CUDA pass with maximum loader MAD 0.474.
  Ordinary Preview, OpenPBR, and Standard coat fixtures are now packaged with
  their independent weight/color/roughness images. All 48 external/package
  comparisons across both loaders and Vulkan raster/RT plus CUDA are exact.
- Coat normal now uses the same model: a per-sample UDIM classification bit is
  resolved after texture deduplication, Vulkan raster binds a dedicated normal
  array, and the coat-normal AOV crosses tiles carrying distinct tangent-space
  directions. Vulkan raster default/legacy images are exact (MAD 0.0), with
  Vulkan ray query and CUDA passing the same ordinary/UDIM fixture.
  MaterialX Standard Surface now runs that fixture too. Its first legacy pass
  exposed `inputs:coat_normal` missing from reconstruction and conversion; the
  schema and both material-terminal paths now preserve authored values and
  texture connections. Raster loader parity is exact, while Vulkan RT/CUDA
  pass with maximum loader MAD 0.476.
  Ordinary OpenPBR and Standard Surface coat-normal fixtures are now repacked
  into USDZ and compared against their external USDA/PPM renders as part of the
  same gate. Vulkan raster, Vulkan RT, and CUDA are pixel-exact for package
  parity through both loaders.
- Raster displacement now has a vertex/tessellation-visible UDIM array and LUT
  route instead of silently binding black for UDIM height maps. Semantic raw/
  UDIM classification is packed after texture deduplication and the focused
  bridge test pins its material ABI. The checked-in Vulkan image regression
  compares flat tiles with a raised second tile at an oblique view and measures
  a 2.37 mean absolute pixel response, closing displaced-silhouette coverage.
  The same fixture now covers PreviewSurface through both loaders: its response
  is also 2.37 MAD and flat/raised loader comparisons are exact (MAD 0.0).
- The three-family core semantic grid now drives base color, packed metallic/
  roughness channels, and emission through ordinary and two-tile UDIM sources.
  Vulkan raster passes all 24 AOV responses; the focused default/legacy
  emission comparison is pixel-exact (MAD 0.0) for Preview, OpenPBR, and
  Standard Surface.
  Each ordinary core fixture is now also stored as a self-contained USDZ with
  its base, packed ORM, and emission images. All 72 external/package AOV
  comparisons (three families, four semantics, two loaders, three GPU
  backends) pass; package images are exact except the established Vulkan RT
  Preview sampling variance (maximum MAD 0.314).
- Opacity now has ordinary and two-tile UDIM response cases for all three
  shader families. The first legacy run exposed Standard Surface conversion
  collapsing a connected color opacity to constant luminance; connections are
  now preserved, the Tydra unit pins the texture descriptor, and Vulkan
  default/legacy images agree within MAD 0.018.
  The ordinary fixtures are now also stored with their alpha images in USDZ.
  All 18 external/package comparisons across the three families, two loaders,
  and Vulkan raster/RT plus CUDA are pixel-exact.
- PreviewSurface occlusion now also crosses a two-tile raw scalar UDIM. Its
  controlled indirect-light response passes and Vulkan default/legacy pixels
  are exact (MAD 0.0).
  Its ordinary map is now packaged as USDZ as well; package parity is exact in
  Vulkan raster/RT and CUDA through both loaders.
- Primary surface-normal coverage now spans ordinary and two-tile UDIM maps for
  Preview, native OpenPBR, and Standard Surface, with exact Vulkan loader parity.
  It exposed native OpenPBR reconstruction ignoring canonical
  `geometry_normal`/`geometry_tangent` aliases and Standard conversion dropping
  connected normal/tangent inputs; focused parser and Tydra units pin both fixes.
  Ordinary normal fixtures for all three families now have stored USDZ forms.
  Together with occlusion, all 24 external/package comparisons are exact across
  both loaders and Vulkan raster/RT plus CUDA.
- Vulkan ray query now runs the complete 36-case core semantic matrix per
  loader: base, packed metallic/roughness, emission, opacity, and primary normal
  across ordinary/UDIM Preview, OpenPBR, and Standard Surface inputs. This found
  RT texture detection stopping at an untextured `base_roughness` alias before
  the textured Standard `specular_roughness`; all aliases are now scanned, the
  bridge unit pins the mixed case, and loader images agree within MAD 0.082.
- CUDA now covers the same ordinary/UDIM core grid. Its first packed scalar run
  exposed a test-oracle collision: the low texel value 32 was indistinguishable
  from CUDA's clear value 31, although the image contained the correct texels.
  Raising the controlled low signal to 64 makes the response discriminating;
  CUDA, Vulkan raster, and Vulkan RT packed metallic/roughness cases now pass
  every family and loader with exact cross-loader pixels.
- `tusdview-texture-semantic-rt-loader-parity` now makes the complete RT matrix
  a registered gate rather than a manual spot check. It runs both loaders over
  Vulkan ray query, CUDA, and HIP when available; the NVIDIA host passes the
  Vulkan/CUDA matrix in 221 seconds with HIP capability-skipped.
- `tusdview_lightrt_bridge_test` now pins sparse-UDIM RT table addressing: tile
  1001 maps to the first complete mip chain, tile 1002 remains missing, and
  tile 1003 maps to a later independent complete chain. This ABI is shared by
  Vulkan RT, CUDA, and HIP.
- `tusdview_lightrt_bridge_test`, `tusdview_openpbr_material_test`, and
  `tusdview_lighting_test` pass after the canonical PBR record, descriptor-id
  repair, and shared point-shadow cube-camera tests. Native `tusdview` builds.
  The OpenPBR extraction test now gives every real-time texture slot an
  independent source (base, metalness, roughness, normal/coat-normal, emission,
  opacity, specular color, coat weight/color/roughness) and checks that its
  post-dedup descriptor id, channel, scale/bias, UV transform, and neutral
  fallback factor survive canonical PBR baking. It now also asserts every one
  of those descriptors preserves its Repeat/Mirror wrap intent and its authored
  Raw-versus-sRGB color-space intent independently of the shared image table.
  `tusdview_texture_pipeline_test` separately covers the image pipeline itself:
  an sRGB base map and Raw normal map retain their distinct per-material
  descriptor color spaces and Repeat wrap intent after decode, mip generation,
  and usage classification.
  `tusdview-unsupported-realtime-lobes` reports exact default/legacy supported
  image parity (mean absolute delta 0.000) and validates the shared unsupported-
  lobe diagnostic. `run-material-binding-inheritance.sh` is syntax-checked and
  skips as expected on software Vulkan; isolated RTX 3070 headless renders now
  provide its hardware ordinary-texture assertion (mean absolute delta 0.000).
- Point-light cube-camera construction is shared and unit-tested (six 90-degree
  faces, consistent GL/Vulkan depth convention). GL and Vulkan both render the
  six depth faces and sample the matching projected-depth cubemap face. The
  extended `tusdview-raster-shadow-map` Vulkan run reports 29 bounded point-cube
  shadow rows with 142 contrast, while the directional, RectLight, alpha-cutout,
  sparse-UDIM, and instanced baseline checks remain green.
- The resumed material/shadow implementation builds after regenerating Vulkan
  shaders. `tusdview_lightrt_bridge_test`, `tusdview_openpbr_material_test`,
  `tusdview_lighting_test`, and `tusdview_texture_pipeline_test` pass. The
  bridge test pins the expanded raster and RT coat scale/bias ABI.
- `tusdview-raster-shadow-map` now renders both an opaque blocker and the same
  blocker rejected by `opacityThreshold`; GL and Vulkan must show a bounded
  shadow only for the opaque case. `tusdview-vk-render` also passes with the
  material-aware shadow fragment shader and expanded RT descriptor ABI.
- Vulkan shader regeneration and `tusdview-vk-render` cover the dedicated
  instanced shadow pipeline, 25-binding material set, and coat-normal raster/RT
  layouts. `tusdview_lightrt_bridge_test` pins the 54-vec4 raster and 155-float
  RT coat-normal descriptor offsets.
- The expanded `tusdview-raster-shadow-map` oracle reports 27 bounded shadow
  rows for both ordinary and PointInstancer casters and zero rows for constant
  and sparse-UDIM rejected masks on Vulkan. Backend-unavailable cases remain
  skips, but an instanced/UDIM subcase may not silently skip after its baseline
  backend has rendered.

- Native `tusdview` and `test_tydra_next` build successfully.
- The twelve focused backend/material/camera/non-mesh CTests pass, including
  Vulkan raster, Vulkan ray query, CUDA deformation, GL carrier image output,
  opacity, back-face materials, transparency, and camera CLI behavior.
- All seven registered headless tusdview unit executables pass; the lighting
  test includes point/curve RT proxy topology and opacity assertions.
- All eleven tests carrying the `tusdview` CTest label pass. This label now
  includes the seven headless unit executables plus the checked non-mesh
  extraction and OpenGL carrier-render regressions, so `ctest -L tusdview`
  exercises both CPU records and visible Points/Curves output.
- The complete configured native CTest run covers 190 tests: 185 pass and five
  external/backend-dependent tests skip, with no failures. The new
  unsupported-lobe regression passes in the focused, labeled, and complete
  gates. An earlier complete run's sole reported failure,
  `tusdview-rt-skinning`, was a comparator false positive: decoded CPU/GPU
  images differ at one gray pixel by one 8-bit level while the animated poses
  otherwise agree.  The regression now compares decoded pixels and permits at
  most that single-pixel/one-LSB floating-point roundoff.  The saved failing
  pair passes the corrected comparator, and the corrected test passes on the
  NVIDIA Vulkan RT device. Together with the fresh complete run, this satisfies
  the full native CTest portion of the acceptance gate.
- `test_tydra_next` directly covers Points/Curves opacity interpolation, all
  curve families, light-link collections, and unauthored light shape defaults.
- The checked `tests/usda/tusdview-nonmesh-points-curves.usda` fixture retains
  one Points prim and Basis/Hermite/NURBS curves and renders through OpenGL;
  Vulkan ray query builds four corresponding BLAS carriers.
- The checked `tests/usda/tusdview-raster-multilight-links.usda` fixture proves
  additive red/blue/magenta direct-light accumulation, per-mesh light-link
  masks, focused GL/Vulkan mean-color agreement, and default/legacy Vulkan
  agreement. The shared packer unit also pins the 16-light stage-order bound
  and DistantLight direction convention. Legacy streaming now resolves
  CollectionAPI after its final mesh table exists and dispatches every USD
  light schema; a DistantLight path-expression unit pins that behavior. Scenes
  with authored light-link collections retain source-prim batch identity so
  collection membership cannot be lost to static batching.
- `tusdview-unsupported-realtime-lobes` loads the same OpenPBR material through
  default and legacy conversion and requires one structured diagnostic naming
  transmission, subsurface, sheen/fuzz, thin-film, anisotropy, and dispersion.
  The neutral next-core record retains these authored inputs instead of
  discarding them before a future evaluator can consume them.
- Shell syntax and `git diff --check` pass.

These results do not yet satisfy the unchecked large-scene performance,
external usd-assets, or GL/Vulkan image-parity gates.

---

## Task status

### Audit (A)

- ~~**T12 [medium] — tusdview `--next` material gaps.**~~ **DONE 2026-07-15.**
  The loader reads
  `use_specular_workflow` / `specular_color` / `ior` (UsdPreviewSurface) and
  `specular_ior` (OpenPBR) into `DrawMaterialCPU`; both raster backends compute
  F0 = specularColor (spec workflow) or dielectric-from-ior (metallic), unified
  GL↔VK (`computeF0`). VK routes it through a new `specParams` vec4 in the set-6
  SSBO (workflow flag folded into ior's sign, so no push-constant lane); GL uses
  three uniforms. Pinned by `tusdview-specular-workflow`. Separate scalar
  opacity textures now work in GL/VK raster for ordinary images and UDIMs,
  including channel selection, UV transforms/UV-set routing, and scale/bias.
  The base-rgb/same-texture-alpha graph is sampled once rather than squared.
  Both rasterizers use one scene-wide 100-column UDIM lookup atlas, replacing
  the per-slot LUT samplers that previously crossed radeonsi's shader-complexity
  cliff. Missing opacity tiles are opaque. Varying `displayOpacity` is packed
  with displayColor as RGBA and consumed by GL/VK raster, Vulkan ray query, and
  the shared CUDA/HIP tracer; constant values still fold into per-mesh material
  variants, multiplicatively with authored material opacity. The opacity AOV is
  GL/VK-identical and includes the separate mask plus varying vertex and
  PointInstancer opacity. Prototype vertex alpha also participates in instanced
  batch transparency classification in both raster backends, pinned by the
  point-instanced opacity-ramp case in `tusdview-opacity-material`.

- ~~Cross-tool parity oracle~~ **DONE (2026-07-14)** — `models/parity-material-uv-subset.usda`
  + `tools/tusdrender/tests/run-cross-tool-parity.sh`, registered as
  `tusdview-tusdrender-parity`. Four coplanar quads, each bound through a
  `materialBind` GeomSubset: red | green | blue | (texture cropped via `uvSet1`
  onto a yellow quadrant). Renders `tusdview --headless` (Vulkan) and
  `tusdrender -rtPreview` and asserts both give `r g b y` AND agree region-by-region.
  It found **two real tusdview `--next` bugs on its first run** (both fixed, both
  now pinned by it — verified by reverting each and watching it fail):
  1. **Constant-color materials grayed out.** `BakeLightRtOpenPBR` →
     `BakeUsdPreviewSurface` read via a PARAM MAP (`mat.params`) that only the
     LEGACY loader populates, then copied its own defaults back over the material —
     so on the (default) `--next` path every untextured constant-color material lost
     its color. Fixed by seeding from `DrawMaterialCPU`'s direct fields first.
  2. **The secondary UV set never reached the GPU.** `uv0` rides inside `DrawVertex`,
     but `uv1` is a PARALLEL array, and the batch builder appended vertices without
     it — so every batched draw carried an EMPTY `uv1` and any texture routed to
     `uvSet1` sampled one fixed coordinate (GL: the corner texel; VK: the averaged
     mip). Fixed by carrying `uv1` through both batch-append paths (`anyUv1`,
     mirroring `anyColor`). Affected GL *and* VK.
     **`tusdview-uv-set-routing` was pinning this bug**: it asserted the `uvSet1`
     render is "3x flatter than st", which is only true when uv1 is broken (a 4x
     zoom of a checkerboard is still a checkerboard — same contrast, bigger cells).
     Its second assertion is now a contrast FLOOR (the crop must not be flat).
     Do not re-add the flatness ratio.

### Texture / KTX2 (B)

- ~~**Web scene-texture routing.**~~ **DONE locally 2026-07-15.** The main
  tinyusdz WASM module links textools and exposes RGBA8 -> `uni` plus
  `uni` -> BC7/ASTC/ETC2/RGBA8 bindings. `getTextureFromUSD` sends both
  native-decoded and ordinary browser-decoded scene images through the best
  supported GPU block format, with linear/sRGB variants, pre-encode Y flip,
  and the original uncompressed fallback. Three.js receives its recognized base
  compressed-format constant; `texture.colorSpace` selects the sRGB upload.
- ~~**Basis Universal / KHR_texture_basisu** transcoder.~~ **DONE locally
  2026-07-15.** Undecoded external and embedded `.ktx2` scene textures route
  through a lazy Three.js `KTX2Loader` for ETC1S/UASTC. Applications may supply
  a renderer-configured loader or use the automatic WebGL capability probe;
  decoded tinyexr `uni` textures retain the Basis-free fast path. Embedded
  headers distinguish private `uni` from real Basis/standard ASTC before
  dispatch, and a Chrome render/readback regression pins decoded pixels.
- ~~**Land tinyexr PR #259** (`texpipe-ktx2-zstd-writer`, the KTX2 Zstd
  writer).~~ **DONE 2026-07-15.** Merged into TinyEXR `release` as `1b10661` and
  re-synced into `src/external/textools`; upstream `make tools-test`, upstream's
  standalone Three.js KTX2Loader test, native textools/tusdview tests, and the
  four tusdview WASM/browser texture gates all pass.

---

## Status snapshot

- **Audit (A): backlog EMPTY** (T12 and the parity oracle are DONE). All other
  findings fixed or refuted, pushed to `tusdview` (latest `79d0c8dcd`). Highlights
  of the last pass: R10 light `normalize` + dome `texture:format`; T8 doubleSided
  on `--next` + VK back-face cull; R12 doubleSided cull in the tracer; **T11 +
  GL↔VK unification** — linear sRGB workflow (sRGB textures upload `_SRGB`,
  `linearToSrgb` OETF on the shaded output; GL adopted VK's derivative-TBN normal
  map, VK adopted GL's soft-headlight diffuse — GL==VK byte-identical now; RT got
  the encode too). T7 (Facing AOV) refuted. The standalone `src/next` gate also
  found a post-merge UV-slot edge locally: a material-referenced set already in
  `texcoords_1` was not normalized to primary. Promotion now swaps it into slot 0
  while preserving the displaced set in slot 1, pinned by
  `TestMaterialParityFixes`.
- **Texture (B): all 5 phases + kept-compressed / Zstd / HDR-BC6H follow-ups DONE,
  plus web scene-texture routing and Basis interop done locally.** tinyusdz
  `tusdview` through `fe30d477c`; TinyEXR's reader and Zstd writer work are both
  merged (`release` `1b10661`, PRs #258/#259) and the vendored textools snapshot
  is synchronized. textools/tusdview ctests are re-registered (`textools-*`,
  `tusdview_texture_pipeline_test`).

## By-design residuals (NOT todo — documented so they aren't re-opened)

- tusdrender TLAS/instanced path faceforwards (no per-prim filter in
  `lrt_tlas_intersect1`); doubleSided cull is flat-path only.
- VK MDI instanced batch draws many prototypes per indirect call, so it can't
  vary cull mode — stays no-cull (the per-mesh instanced fallback does cull).
- RT (ray-query) keeps its own real-shadow lighting; it only shares the sRGB
  output encode with the raster backends, not the diffuse model.

---

## Gotchas (learned the hard way — still live)

- **`--next` is the DEFAULT tusdview loader** and builds its DrawScene textures
  itself (`next_scene_loader.cc` + tydra-next cache), bypassing `mesh_build`'s
  `BuildDrawTextures` / `ApplyTextureRuntimeOptions`. A feature wired only into
  the legacy path is silently inert by default.
- **Anything PARALLEL to `DrawMeshCPU::vertices` must be appended by hand in the
  batch builder** (`next_scene_loader.cc`, both the single-material fast path AND
  the multi-material GeomSubset path). Position/normal/uv0 ride inside `DrawVertex`
  and travel for free; `uv1`, `vertexColors`, `vertexAlpha`, `tangents`, skin and
  morph attributes do NOT. Forget one and the batched draw silently carries an
  EMPTY buffer — the shader then reads a default (uv1 = one fixed coordinate) and
  the failure looks like a *shader* bug, not a loader one. Follow the `anyColor` /
  `anyUv1` pattern: back-fill the vertices already in the batch, then push per
  vertex (zeros for meshes that lack the attribute).
- **`raytrace_comp.spv.h` must be regenerated with the SDK glslang 14.0.0**
  (`build-vulkan-sdk-1.3.275.0/src/Vulkan-ValidationLayers/external/glslang/build/install/bin/glslang`),
  NOT the PATH `/usr/bin/glslang` 15.1.0 — the latter emits ray-query SPIR-V that
  **segfaults radv at pipeline creation**. Run
  `bash examples/tusdview/vk/shaders/build-shaders.sh <that-glslang>`, keep only
  the `.spv.h` you actually edited, `git checkout --` the rest (they're full-file
  glslang-version churn).
- **Vendored textools stays pristine:** fix upstream at `~/work/tinyexr`, then
  re-copy into `src/external/`. `TINYUSDZ_WITH_ZSTD_COMPRESSION` is defined
  PRIVATEly on the core lib — textools consumers (tusdview, usd-texcomp) must
  re-declare it.
- **`uni` bitstream CHANGED** (tinyexr `release`). Old `.ktx2` are rejected
  (TC_ERROR_CORRUPT); regenerate authored uni `.ktx2` with `usd-texcomp`.
- `mesh_build.cc` wraps helpers in an anonymous namespace (~L33-2574); anything
  declared in `mesh_build.hh` must be defined OUTSIDE it or the call is ambiguous.
- **Never push without the AGENTS.md pre-push audit + explicit permission** every
  time (credential grep + gitleaks + trufflehog over the exact range, personal
  paths in messages AND diffs, private asset names, artifacts/binaries).

## Build & verify

- Native: `@build`, `make -j16`; `cd build && ctest --output-on-failure`. GPU
  tests need a real device — prefer `DISPLAY=:0 ctest`; software GL (Xvfb/llvmpipe)
  skips them (see `tests/tusdview/gpu_backend.py`).
- Roundtrip: `bash tests/run-usdcat-compare.sh`.
- Texture: `build/textools_*_test`,
  `build/examples/tusdview/tusdview_texture_pipeline_test`; textools upstream
  `cd ~/work/tinyexr && make tools-test`.
- The render goldens (`examples/tusdview/tests/*-goldens.tsv`) are
  coverage/silhouette fingerprints (robust to shading nudges), per-machine,
  regenerated with `--update-golden`.

## Resuming prompt

> Read `doc/tusdview-tasks.md`. Two programs: the tusdview/tusdrender audit
> (backlog empty; T12 and the cross-tool parity oracle are done) and the KTX2
> texture program (all local and upstream work complete; TinyEXR PR #259 merged
> and vendored at `release` `1b10661`). Native build `@build -j16`, gate with `ctest`
> (`DISPLAY=:0` for GPU). Mind the gotchas above — especially the SDK-glslang
> requirement for `raytrace_comp.spv.h`. Pre-push audit + permission every time.
