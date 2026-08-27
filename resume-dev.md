# Resume: complete MaterialX/OpenPBR support

Follow `AGENTS.md`. Continue the active objective: finish MaterialX/OpenPBR
support across tusdview CPU RT, Vulkan RT, CUDA RT, HIP RT, and headless
tusdrender, including all supported MaterialX node operations and a testable
CLI/headless path.

Do not touch the unrelated untracked `run.sh` or `usd-assets`. Do not rewrite
published history. Regenerate embedded SPIR-V headers from source shaders; do
not hand-edit generated headers.

## Current repository state

- Branch: `dev`
- HEAD: current branch tip (see `git log`; latest work records Vulkan parity)
- Upstream: `origin/dev`
- Worktree: no tracked modifications; the two unrelated untracked paths above
  remain untouched.
- Deterministic native gates after the mixed-graph fix: 29/29 selected
  unit/parser/roundtrip/feature tests passed, and the stable `next` suite
  passed 40/40 tests. The full viewer aggregate is not yet a clean pass: the
  headless GL/Vulkan parity test timed out on llvmpipe, and a later GL semantic
  test hung before its registered timeout.
- Recent completed commits:
  - `4c7e80c07` Complete MaterialX Vulkan RT graph evaluation
  - `63c5b9a29` Transport MaterialX time context to GPU RT
  - `60d4b638c` Preserve direct MaterialX GPU graph context
  - `9643985f8` Add MaterialX surface and volume closures
  - `4c5b073b0` Add MaterialX hair helper evaluation
  - `cab4b6aec` Add direct MaterialX closure bridge tests
  - `bab865016` Extract vector roughness in MaterialX closures
  - `c518d8fc3` Test nested MaterialX closure composition
  - `97d1cd317` Preserve conductor F0 in MaterialX closure lowering
  - `1ffade301` Honor generalized Schlick transmission mode
  - `f74f31384` Fix scalar multiplication of closure graphs
  - `5c6d425f5` Resolve MaterialX wrapper nodegraph outputs
  - `eaeafba1c` Use runtime view direction for latlong graphs
  - `3228d85cd` Preserve generic geomprops in next scene meshes
  - `abc287650` Evaluate arbitrary geomprops in CPU material graphs
  - `309fbee0e` Transport geomprops through CUDA and HIP RT
  - `7e9af3aee` Add compute BVH geomprop transport
  - `05b1c970b` Record SWBVH geomprop regression
- `f6d9a7af0` Support integer geomprops in next viewer
- `da8b1ac62` Honor MaterialX texture address modes
- `43c1cb068` Fix CUDA HIP geomprop path plumbing
  - `e882ce7c1` Preserve lanes in generalized EDF bridge
  - `0d29de89e` Transport matrix geomprops through RT graphs
  - `14cd07dd5` Preserve OpenPBR subsurface anisotropy
  - `6d58899ef` Handle reversed closure multiply operands
  - `ebcb8ca61` Record closure parity validation
  - `cfe8da671` Preserve surface unlit normals
  - `d355e2956` Fix matrix geomprop staging
  - `1477c7f6f` Test matrix geomprop stream safety
  - `c32152edb` Preserve vector subsurface radius scale

## Completed coverage

- Shared bounded graph ABI: 64 nodes, 48 OpenPBR output lanes, used by CPU,
  CUDA/HIP, Vulkan RT, and headless LightRT/tusdrender paths.
- Interactive Vulkan RT implements the current graph opcode set through op 121,
  including arithmetic, conditionals, compositing, channel operations,
  procedural noise/Worley/patterns, ramps, flake, textures, normal maps,
  matrices, blackbody, roughness helpers, artistic IOR, and shading context.
- Interactive Vulkan RT transports real hit normal, world position, view
  direction, MaterialX time, and MaterialX frame through the frame packet and
  push constants.
- CUDA/HIP graph routes transport normal, position, view direction, time, and
  frame through their camera ABI; headless Vulkan transports time/frame too.
- Standalone MaterialX evaluation supports surface, surface_unlit, volume,
  volumematerial, BSDF/VDF/EDF evaluation, physical helpers, transforms,
  procedural derivatives, and named geomprop callbacks.
- Texture paths cover image/tiled-image, UDIM, Ptex, latlong/triplanar,
  normal maps, mip selection, colorspace handling, and centered height
  derivatives; CPU RT now matches Vulkan's sRGB RGB decode while preserving
  alpha, and additional edge cases remain incomplete.

## Recent completed slice

The closure bridge now explicitly extracts component zero when a scalar
OpenPBR lane receives a MaterialX vector2 input (notably vector roughness).
Direct Shader-to-Shader BSDF, EDF, and VDF graphs assert route indices and
packed graph-header parity; nested typed BSDF mixing is covered as well. The
vector extraction change is committed in `bab865016`, with the nested test in
`c518d8fc3`. Conductor closures now lower eta/extinction to the same bounded
F0 arithmetic used by the standalone evaluator (`97d1cd317`).
Generalized-Schlick R/T scatter mode now emits matching specular/transmission
lanes (`1ffade301`).
Typed closure `multiply` now distinguishes a closure operand from its scalar
factor instead of lowering the factor as a second closure (`f74f31384`).
The next MaterialX converter now resolves `surfacematerial` shader bindings
through `nodegraph` outputs, including graph-local surface shader nodes
(`5c6d425f5`).
Latlong image lowering now uses the runtime view direction when `viewdir` is
omitted instead of baking a +Z direction (`eaeafba1c`).
The next scene loader now preserves authored float/vector geomprops through
interpolation/index expansion, corner welding, Ptex expansion, and material
batching; validation and CPU resource accounting cover the new streams
(`3228d85cd`). Integer and unsigned scalar/vector primvars now use the same
packed float ABI through conversion at evaluation time; matrix primvars remain
intentionally excluded because they do not fit the four-component stream ABI.
Arbitrary geomprop nodes now survive JSON normalization and carry a stable
name hash through the fixed graph record; the CPU RT reference evaluates
float/vector streams from triangle-corner data with authored defaults for
missing streams (`abc287650`). The Vulkan hardware ray-query path now uploads
per-mesh descriptor/value streams and evaluates the opcode with barycentric
interpolation. The shared CUDA/HIP kernel source and host upload path now have
the matching lookup. Compute-BVH now has descriptor bindings, host uploads,
and barycentric interpolation in its full and path shader variants; arbitrary
typed variants remain open.

## Current implementation status

`examples/tusdview/lightrt_mtlx_bridge.cc` contains the validated recursive
closure-lowering pass. It discovers direct `bsdf`/`edf`/`vdf` terminal
connections and lowers closure leaves plus typed closure composition into
ordinary bounded graph nodes and OpenPBR lanes. The remaining closure work is
fidelity review for weighted composition and wrapper nesting; the direct
closure slice is committed and validated.
The next MaterialX converter resolves both direct surface bindings and
nodegraph-backed `surfacematerial` outputs. Host-side generic float/vector and
integer/unsigned scalar/vector geomprops are retained by the next scene loader;
CPU RT, Vulkan hardware ray-query, the shared CUDA/HIP kernel, and compute-BVH
evaluate the supported streams. Matrix-valued transport remains open.
The CPU LightRT texture sampler now honors MaterialX periodic, clamp, and
mirror address modes (and returns a neutral missing-texture value for constant
outside samples); the legacy periodic entry point remains unchanged.
Matrix33/Matrix44 geomprops are now retained by the next render converter,
expanded as column views in the bounded graph IR, and interpolated through the
CPU, Vulkan, CUDA, and HIP RT paths (`0d29de89e`). Object/world/view transform
and instance-transform fidelity still require separate validation.
The next converter now retains a separate volume-terminal graph. The tusdview
bridge converts its density/albedo outputs to absorption/scattering VDF inputs
and its emission output to EDF, preserving composed MaterialX volume networks
without conflating them with the surface graph; authored emission scale is
applied in the EDF bridge as well. VDF anisotropy is now retained in the
canonical real-time medium phase parameter and covered by the evaluator test.
The standalone LightRT texture cache now resolves `<UDIM>` filenames from the
integer UV tile and returns a clean miss for absent tiles; present/missing tile
behavior is covered by the C evaluator regression, including frozen-cache
preload behavior for the first ten UDIM rows.
Standalone EDF evaluation now recursively handles add/layer, mix, and
scalar-or-EDF multiply composition, so composed emission survives into both
surface and volume evaluation.
Measured EDF fallback behavior is covered in the standalone and LightRT bridge
paths: authored color is preserved while unsupported profile filenames remain
non-fatal.
Conical EDF bridge lowering preserves normal/view angular falloff through acos,
degree conversion, and clamped runtime graph arithmetic; bridge coverage
verifies the angular nodes are retained.
The standalone evaluator and bridge now also preserve standard
`subsurface_vdf` scattering and anisotropy inputs in the shared volume
representation, with focused evaluator and direct-VDF bridge coverage.
MaterialX `light` wrappers in the bridge now forward their EDF closure and
apply color intensity plus exposure gain to surface and volume emission lanes;
direct wrapper graph coverage is included.
Measured EDFs now have explicit regression coverage in the standalone and
LightRT bridge paths: authored color is preserved as the renderer-neutral
fallback while unsupported profile filenames remain non-fatal.
Standalone VDF evaluation now recursively handles add/layer, mix, and scalar
multiply composition, preserving composed absorption, scattering, and
anisotropy for volume evaluation.
CUDA/HIP path-tracing live reload now forwards the geomprop descriptor/value
buffers through `productionPath`; this keeps the shared kernel source's new
geomprop call signature valid under NVRTC and hipRTC.

## Verified tests after HEAD

Passed:

- Complete configured native CTest suite after CUDA/HIP geomprop path
  plumbing: 298/298 passed in 1112.57 seconds (24 documented
  capability/asset skips)
- Full configured native CTest after recursive EDF/VDF evaluation: 298/298
  passed in 1106.94 seconds (24 documented capability/asset skips)
- Stable `build-next` rebuild and regression: 40/40 passed
- Standalone EDF composition and headless MaterialX CPU graph tests after
  recursive EDF evaluation: 2/2 passed
- Standalone VDF composition and headless MaterialX CPU graph tests after
  recursive VDF evaluation: 2/2 passed
- Exact outgoing-range audit over `origin/dev..HEAD`: heuristic credential,
  personal-path, artifact, and flagged-asset scans clean; gitleaks clean;
  trufflehog 3.97.1 clean with auto-update disabled
- `tusdview_lightrt_bridge_test`, OpenPBR, evaluator, and graph tests after
  vector2 roughness extraction
- `tusdview-vk-render`, CUDA/HIP semantic RT parity, and headless
  `tusdrender` OpenPBR parity after vector2 roughness extraction
- `tusdview-gpu-material-abi`
- `tusdview-vk-render` on NVIDIA GeForce RTX 5060 Ti with Vulkan RT
- `tusdview-texture-semantic-rt-loader-parity-vk-rt` on NVIDIA Vulkan RT
- both interactive Vulkan shader variants compile via
  `examples/tusdview/vk/shaders/build-shaders.sh`
- `tusdview` rebuild after Vulkan shader/context changes
- Vulkan hardware geomprop shader regeneration, viewer rebuild, and focused
  CPU/bridge/safety checks: 3/3 passed
- Texture/material regression batch after CPU sRGB parity change: 45/45
  passed, including 15 documented unavailable-hardware/asset skips
- Reconfigured native build including the shared CUDA/HIP RT host target and
  `tusdview`: passed
- Compute-BVH shader regeneration, viewer rebuild, ABI, and safety checks:
  passed
- SWBVH stacked-glass regression: passed; general SWBVH render profile:
  environment-skipped
- SWBVH stacked-glass regression rerun after barycentric geomprop update and
  GPU material ABI check: 2/2 passed
- `tusdview` rebuild plus focused GPU material ABI and SWBVH checks after
  integer/unsigned scalar-vector geomprop ingestion: build passed, ABI passed,
  SWBVH profile environment-skipped
- `tusdview` rebuild plus texture pipeline, LightRT evaluator, and graph
  evaluation tests after address-mode sampling update: 3/3 passed
- `tusdview` rebuild plus NVIDIA GL warmup and CUDA/HIP MCP live-reload tests
  after geomprop path-tracer plumbing fix: 3/3 passed
- Full configured native CTest rerun after conductor and generalized-Schlick
  lowering changes: 298/298 passed, with 24 documented skips
- Focused bridge/evaluator/graph/ABI and CPU/GPU parity run after closure
  scalar-multiply fix: 12/12 passed
- Stable `test_tydra_next` Release executable now completes all tests; its
  side-effecting `assert` setup in the COW-budget and external-MaterialX tests
  was made explicit for `NDEBUG` builds.
- `tusdview_lightrt_bridge_test` after latlong default-view fix: passed
- Generalized-Schlick EDF bridge lowering now preserves inherited closure
  lanes and applies the runtime angle-dependent emission tint; bridge,
  evaluator, and CPU graph focused tests pass 3/3.
- Hardware/material gate after the bridge slice: 15/15 passed across NVIDIA
  GL warmup, Vulkan RT, CUDA, HIP, semantic texture parity, CPU RT, and the
  GPU material ABI.
- Complete registered MaterialX graph/parity group: 7/7 passed, including
  CPU graph, flake/projection/OpenPBR parity, evaluator, connection, and graph
  evaluation tests.
- Optimized Release build plus complete registered native/viewer/tusdrender
  regression: 298/298 passed in 1112.26 seconds. The 24 skips were documented
  unavailable external-asset, corpus, or validation-capability profiles.
- Matrix geomprop bridge/packing, Release rebuild, Vulkan render, geometry
  primvar, and GPU ABI checks: 5/5 passed; Vulkan shaders were regenerated
  from source.
- Stable `build-next` rebuild and full regression after matrix format support:
  40/40 passed.
- Final current-HEAD optimized Release full native/viewer/tusdrender
  regression: 298/298 passed in 1109.64 seconds; the 24 skips remain the
  documented unavailable external-asset, corpus, or validation-capability
  profiles.
- Stable `next` regression after wrapper changes: 40/40 passed
- Viewer rebuild plus focused geomprop/bridge/ABI/nonmesh/safety checks: 6/6
  passed after generic geomprop stream preservation (`3228d85cd`)
- Focused CPU RT, bridge, and safety checks after live arbitrary geomprop
  evaluation: 6/6 passed (`abc287650`)
- Measured-EDF bridge and standalone evaluator regressions: 2/2 passed;
  authored color fallback is covered for a profile filename that cannot be
  decoded by the standalone backend.
- Broader post-change validation: 14/14 passed, including MaterialX CPU,
  flake/projection/OpenPBR parity, opacity texture alpha, NVIDIA GL warmup,
  next nonmesh extraction, displacement UDIM, texture pipeline, bridge,
  evaluator, graph connection/evaluation, and `unit-test-next`.
- Final current-HEAD optimized native/viewer/tusdrender regression: 298/298
  passed in 1109.38 seconds. The 24 unavailable asset/capability profiles
  remained documented skips; no test failed.
- Post-conical bridge MaterialX parity/graph/ABI validation: 11/11 passed,
  including CPU graph, flake/projection/OpenPBR parity, Vulkan render, NVIDIA
  warmup, bridge/evaluator/graph tests, and GPU material ABI.
- Final current-HEAD optimized native/viewer/tusdrender regression after
  conical EDF lowering: 298/298 passed in 1110.36 seconds; the 24 documented
  unavailable asset/capability profiles remained skipped.
- Final current-HEAD optimized native/viewer/tusdrender regression after
  `subsurface_vdf` support: 298/298 passed in 1108.96 seconds; the same 24
  documented unavailable asset/capability profiles remained skipped.
- Final current-HEAD optimized native/viewer/tusdrender regression after the
  MaterialX light-wrapper change: 298/298 passed in 1109.01 seconds; the same
  24 documented unavailable asset/capability profiles remained skipped.
- Measured-EDF fixture inputs now use the schema-correct `file`/`filename`
  field (`f686776d2`); the focused bridge/evaluator/graph test group passed
  4/4 afterward.
- Separate displacement terminals remain present even when their resolved
  scalar is exactly zero (`94b22e060`); bridge, displacement-UDIM, and viewer
  rebuild checks passed.
- Final current-HEAD optimized native/viewer/tusdrender regression after the
  zero-valued displacement fix: 298/298 passed in 1107.08 seconds; only the
  documented unavailable external-asset, real-texture/validation, corpus, and
  OpenChess profiles were skipped.
- Conical EDF authored normal inputs now reach the runtime graph instead of
  always using the geometric normal (`81f90a574`); the focused bridge test and
  the complete final regression both passed.
- Final current-HEAD optimized native/viewer/tusdrender regression after the
  conical-direction fix: 298/298 passed in 1107.55 seconds; the same
  documented unavailable profiles were skipped.
- Conical EDF lowering now follows the full-angle cosine-boundary and
  hard-cutoff/smoothstep semantics, with authored direction preserved
  (`b8dd11c88`). Numeric evaluator and runtime-graph regressions pass.
- Final current-HEAD optimized native/viewer/tusdrender regression after the
  conical angle-semantics fix: 298/298 passed in 1105.89 seconds; the same
  documented unavailable profiles were skipped.
- MaterialX `scatter_mode` handling now accepts only the schema-defined `R`,
  `T`, and `RT` values in both the standalone evaluator and runtime bridge;
  transmission-only, invalid-mode, and vector2-roughness extraction tests
  pass (`56a86af56`).
- Generalized-Schlick transmission no longer misuses reflection `color90` as
  a transmission tint; the standalone evaluator and bridge use neutral
  transmission color while preserving `color0` for reflection. Focused
  bridge/evaluator tests pass after this correction.
- Full configured native/viewer/tusdrender regression after the generalized
  Schlick correction: 298/298 passed in 1107.81 seconds; the same 24
  documented capability/asset profiles remained skipped.
- Volume MaterialX graph baking now roots texture evaluation at the owning
  asset directory through a base-directory-aware evaluator; a relative
  density texture regression passes in the bridge test.
- Next volume graph export now retains unconnected authored shader values as
  synthetic constant nodes, including direct `emission` mapped to
  `volume_emission_scale`; the next converter regression passes.
- CPU/RT host-scene normal baking now applies the inverse-transpose for
  non-uniform object-to-world transforms, matching Vulkan; a scaled-triangle
  regression passes in the bridge test.
- Frozen LightRT texture caches now preload `latlongimage` and all three
  `triplanarprojection` file inputs; the evaluator regression verifies these
  projection textures remain available after preload/freeze.
- Frozen UDIM caches now probe the complete bounded `1001`–`1999` range rather
  than stopping at row 10; the evaluator regression covers a high-row `1101`
  tile alongside present and missing lower-row tiles.
- LightRT image evaluation now honors `filtertype="nearest"` for regular and
  UDIM-capable image paths while retaining bilinear as the default; a two-texel
  evaluator regression distinguishes nearest selection from bilinear blending.
- Composed closure roughness is now weight-averaged in both the standalone
  evaluator and C++ bridge lowering for dielectric, generalized-Schlick,
  conductor, sheen, and hair lanes; scalar closure multiplication leaves
  roughness unchanged while scaling the lobe weight. Evaluator and bridge
  graph regressions pass.
- Bridge closure color lanes are now weight-averaged for base, specular,
  transmission, subsurface, and sheen compositions; emission remains additive,
  and scalar closure multiplication leaves these colors unchanged. The
  rebuilt focused nine-test matrix remains green.
- Projection image nodes now honor `filtertype="nearest"` for both
  `latlongimage` and `triplanarprojection`, with a two-texel evaluator fixture
  covering both paths.
- Texture evaluator coverage now verifies sRGB RGB linearization and
  independent RGBA alpha preservation using a minimal 32-bit TGA fixture.
- The public bridge now has an end-to-end `surfacematerial` to
  `surface_unlit` regression covering emission, transmission, and opacity.
- Standalone EDF evaluation now handles the MaterialX `light` wrapper,
  applying its color intensity and EV exposure to the nested EDF; a focused
  wrapper regression passes and matches the bridge lowering.
- Transform evaluation now has non-identity view→world point, vector, and
  normal coverage in addition to the existing non-uniform object→world case.
- The texture semantic harness now preflights Vulkan device selection before
  its RT sweep, using `vulkaninfo` when available and a bounded viewer probe as
  fallback; this skips llvmpipe RT before expensive shader initialization while
  retaining raster probing.
- GL/Vulkan image parity now limits itself to the bounded fixtures supported by
  both raster lanes; the advanced OpenPBR-lobe fixture remains covered by its
  dedicated next/legacy loader and material test. Both targeted checks pass.
- After the projection preload and parity-harness fixes, the full rebuild and
  focused nine-test MaterialX/bridge matrix pass. The Vulkan RT
  texture-semantic-AOV case still needs a valid hardware run; its CTest
  warm-up dependency failed here because the inherited X display was absent.
- A direct `coat-normal` RT attempt with the documented NVIDIA selector
  confirms this environment exposes only llvmpipe; tusdview reports no Vulkan
  physical device matching `nvidia`. The RT semantic case is therefore
  capability-skipped here pending a machine with the configured NVIDIA Vulkan
  ICD.
- The texture semantic harness now detects software Vulkan during both its
  mode-sweep and per-case paths, preventing llvmpipe RT runs from entering the
  full expensive matrix. A direct Vulkan-RT albedo profile completes; the
  cold coat-normal variant remains hardware-dependent.
- Fresh full CTest after the harness guard passed tests 1–132, including all
  smoke, renderer, parity, deformation, MCP, and raster semantic groups. Test
  133 (`tusdview-texture-semantic-aov-vk-rt`) then stalled in its RT/JIT sweep
  without output for several minutes and the aggregate run was terminated;
  therefore no clean 298-test result is claimed for this HEAD. The subsequent
  preflight fix (`eed469668`) avoids this specific RT entry path on CPU-only
  Vulkan hosts; hardware Vulkan validation remains required.
- The rebuilt focused MaterialX/bridge matrix at this HEAD passes 9/9,
  including GL/Vulkan parity and the unsupported-real-time-lobes standalone
  coverage. The full configured run remains incomplete because the Vulkan RT
  texture-semantic-AOV test stalls in software Vulkan initialization; the
  older 298/298 result below predates the latest texture changes and remains
  historical evidence only.
- The graph evaluator regression now includes a cyclic two-node graph and
  verifies that the memo sentinel terminates it safely; the focused test passes.
- Standalone `latlongimage` evaluation now defaults to the normalized shading
  view direction, matching the bridge lowering, with a +Z fallback for empty
  contexts; the projection evaluator test covers the runtime direction.
- Volume evaluation now has a texture-backed regression covering an image-fed
  absorption VDF, in addition to the existing constant and composed VDF tests.
- The focused nine-test MaterialX/bridge/parity matrix passes 9/9 after the
  latlong and volume changes, including the GL warm-up dependency.
- A broad CTest run excluding the known Vulkan RT semantic case again reached
  the early OpenPBR parity test but did not record a completed result before
  the runner stopped; no aggregate pass is claimed.
- The standard `diffuse_bsdf` closure category is now accepted alongside
  Oren–Nayar and Burley diffuse closures in both evaluator and bridge paths;
  direct bridge and evaluator regressions pass.
- Standalone OpenPBR evaluation now preserves all three components of
  `subsurface_radius`, matching the bridge's vector transport; the evaluator
  regression covers a non-uniform radius.
- OpenPBR `subsurface_radius_scale` now multiplies the radius vector
  component-wise, while `subsurface_scale` is read independently as its
  scalar parameter; the evaluator regression covers both values.
- OpenPBR thin-film thickness now stays in the documented nanometer unit in
  standalone evaluation, matching the bridge; the evaluator regression uses
  a 450 nm film.
- `chiang_hair_roughness` now implements the MaterialX empirical conversion
  for longitudinal variance and azimuthal logistic scale, including squared
  TT/TRT lobe scales in multi-output connections. Standalone graph and bridge
  regressions pass, including explicit TT/TRT selector coverage.
- The registered NVIDIA RT semantic test was rerun with the documented
  offload environment. Device selection and hardware ray-query initialization
  succeeded on an NVIDIA GeForce RTX 5060 Ti, but cold SPIR-V validation
  stalled and produced no completed CTest result. The bounded explicit-device
  preflight remains in place; a clean hardware run is still required.
- The focused CUDA and HIP semantic-AOV checks both pass on the current build;
  the CUDA run completed in 26.75 seconds and the HIP run in 14.70 seconds,
  including texture-backed OpenPBR AOVs and USDZ parity.
- Standard `subsurface_bsdf` lowering now preserves its authored `scale` input
  in both standalone evaluation and the bounded closure graph; direct bridge
  and evaluator regressions pass 2/2.
- Next volume graph conversion now preserves VDF anisotropy as
  `volume_anisotropy` and reconnects it to the generated anisotropic VDF;
  the next-volume bridge regression passes with a nonzero authored value.
- Standard `subsurface_bsdf` now preserves authored anisotropy in standalone
  evaluation and copies it into the LightRT OpenPBR block; the focused
  evaluator and bridge checks pass after a clean rebuild.
- A clean full native rebuild completed after the subsurface transport changes;
  the five core MaterialX/bridge tests pass 5/5. The non-RT aggregate again
  reaches `tool-tusdrender-materialx-openpbr-parity` and stalls before CTest
  reports the child timeout, so no new aggregate pass is claimed.
- Direct absorption, anisotropic, and subsurface VDF closure lowering now
  emits anisotropy into the shared transmission-scatter medium lane; the
  direct VDF bridge regression passes after rebuilding the bridge target.
- The rebuilt headless `tusdrender` MaterialX/OpenPBR parity checker completes
  on Vulkan: semantic grid, executable graph, swizzle, extended operators,
  color correction, ramps, splits, patterns, lobe golden (RMSE 0), texture/
  UDIM cases, and displacement all pass. CUDA/HIP are unavailable for this
  `tusdrender` binary and are skipped by the checker.
- A complete configured native CTest run excluding only the dedicated Vulkan
  RT semantic sweep reached the displacement-UDIM test and exposed one
  remaining loader-parity failure: `tusdview-displacement-udim` reported
  loader MAD 28.4950 (package MAD 22.3343). Reproduction on the current
  software-Vulkan environment shows the next loader creates two materials
  and three textures for the standard-surface fixture while legacy creates
  one material and one texture; PreviewSurface parity is unchanged. A direct
  NVIDIA-selector rerun cannot reproduce here because only llvmpipe is
  visible, so hardware Vulkan validation remains pending after fixing the
  standard-surface loader mismatch.
- The mixed-graph lane fix (`8840d598f`) now requires an image dependency on
  the individual graph output before compatibility baking can neutralize a
  typed lane. The affected bridge object and viewer relink compile cleanly,
  and the next loader reports one texture for the standard displacement
  fixture; software-Vulkan screenshot completion remains unreliable in this
  environment, so the displacement MAD has not yet been re-certified.
- The focused LightRT bridge regression executable exits successfully after
  the mixed-graph fix. The source object and viewer relink both compile; the
  full displacement-UDIM image comparison still needs a capable, stable
  Vulkan runtime to certify the pixel MAD and packaged/legacy parity.
- The mixed-graph fix is now applied after runtime graph compilation, removing
  constant surface lanes that were retained only because an unrelated
  displacement image kept the graph alive. After refreshing the static bridge
  archive, the complete 16-image displacement-UDIM matrix passes on llvmpipe:
  Standard MAD 2.6539, Preview MAD 2.6539, loader parity 0.0000, and USDZ
  package parity 0.0000. Hardware Vulkan validation remains pending.
- Authored MaterialX graph routes now take precedence over direct fallback
  values during the compatibility bake, while direct values are restored only
  for lanes without an active graph route. This fixes nodegraph interface
  inputs and mixed constant/image graphs; the rebuilt bridge, evaluator, and
  graph-connection checks pass 4/4 (`3d3a85a7d`).
- Vulkan hardware-ray-query shading now uses the inverse-transpose of an
  instance's object-to-world linear transform for authored normals, including
  the point-surface path. The ray-query and fast-path shaders were regenerated
  from source and compile successfully; the GPU material ABI check passes
  (`48d14e834`). Hardware pixel validation remains pending.
- MaterialX `surface` closure wrappers now forward nested BSDF and EDF lanes
  into the bounded runtime graph, with direct diffuse-plus-emission wrapper
  coverage in the rebuilt bridge regression (`c4b3083f6`).
- JSON `surface_unlit` terminal outputs now lower emission, transmission,
  opacity, and normal inputs into bounded OpenPBR lanes instead of reaching
  the unsupported-category fallback. Direct output mapping is covered by the
  rebuilt bridge regression; the corrected surface-wrapper route test now
  expects the actual diffuse/EDF lanes.
- Closure-layer regression now covers a diffuse BSDF layered with an EDF,
  including composed lane routing when one lobe does not provide the other's
  parameters.
- Under the documented Xvfb/PRIME offload environment, direct Vulkan RT
  startup and a renderable sphere smoke pass complete on the NVIDIA GeForce
  RTX 5060 Ti with hardware ray query. The registered texture semantic-AOV
  Vulkan RT harness still stalls at its 180-second bound, even on a warm
  rerun, so no full semantic sweep is claimed.
- An isolated NVIDIA `vk-rt` OpenPBR albedo semantic case passes in 4.5 s;
  the full mode sweep remains unproven. Splitting it into fresh per-mode
  processes was rejected because subsequent modes incur repeated long driver
  compilation, so the existing persistent-sweep design remains unchanged.
- The isolated NVIDIA `vk-rt` albedo loader/package parity shard passes with
  default and legacy loaders, UDIM, and USDZ coverage: all image MAD checks
  are 0.0. Coat-normal remains blocked at full SPIR-V pipeline validation.
- Registered CUDA and HIP semantic-AOV shards pass on the current build:
  CUDA 26.03 s and HIP 16.97 s.
- Direct OpenPBR baking now retains authored `subsurface_anisotropy` and
  `subsurface_scatter_anisotropy` in the canonical realtime block
  (`14cd07dd5`); the bridge regression passes.
- Typed closure multiplication now accepts the scalar factor on either input
  side instead of assuming the closure is `in1` (`6d58899ef`); the reversed
  operand bridge regression passes.
- Post-change MaterialX validation passes 9/9: headless CPU graph, flake and
  projection parity, OpenPBR parity, CPU swizzle, bridge, evaluator, graph
  connection, and graph evaluation. CUDA and HIP semantic-AOV shards also
  pass (26.16 s and 16.97 s).
- A broad CTest run excluding the known Vulkan RT semantic-AOV sweep reached
  test 75/296 and reproduced the existing `tusdview-gl-vk-parity` timeout at
  120 seconds after tests 1–74 passed or skipped. No aggregate pass is claimed
  from this partial run; focused MaterialX and CUDA/HIP results remain green.
- The current `build-next` stable suite passes 40/40 after these changes.
- The next MaterialX `surface_unlit` converter now transports its authored
  normal into the renderer-neutral OpenPBR block (`cfe8da671`); the focused
  `next_test_tydra` conversion regression and native `tusdview` rebuild pass.
- Matrix-valued next geomprops are now staged into component-sized temporary
  storage before interpolation/welding (`d355e2956`); this removes the
  9/16-component read overflow while preserving the existing matrix streams.
  Native `tusdview` rebuild, bridge, scene-safety, and GPU-material ABI checks
  pass.
- Scene-safety coverage now validates a correctly sized 16-component matrix
  stream (`1477c7f6f`); the focused safety test passes.
- Graph `subsurface_radius_scale` is now composed into the vector radius route
  rather than truncated through scalar `subsurface_scale`; scalar scale remains
  independent (`c32152edb`). The focused bridge regression passes.
- The complete bridge/evaluator/graph focused group passes 4/4 after the
  radius-scale change.
- Mixed authored scalar radius plus graph-driven vector radius scale is now
  restored as a component-wise product during the bake; the JSON-to-MaterialX
  fallback maps the runtime alias onto the evaluator's vector radius lane.
  The bridge regression and complete four-test focused group pass again.
- The native parser, roundtrip, unit, and feature regression subset passes
  29/29 on the committed tree.
- Post-commit renderer coverage passes 8/8: CUDA and HIP semantic-AOV,
  CPU-RT headless/OpenPBR/MaterialX, Vulkan render/software-BVH, and Vulkan
  raster semantic-AOV tests.
- The standalone `build-next` suite passes 40/40, including Tydra,
  composition, writer/reader, validation, and corpus checks.
- Current hardware probes remain environment-limited: `nvidia-smi` cannot
  communicate with the NVIDIA driver, and `vulkaninfo --summary` cannot open
  the configured X server/ NVIDIA ICD. Hardware Vulkan RT validation therefore
  remains pending for a capable machine.
- The full registered native pre-parity slice passes 74/74. Three tests are
  capability-skipped (island production asset, Vulkan texture validation, and
  shadow-alpha instancing); no test in the slice fails.
- The post-parity viewer interval reached test 130 with 55/55 completed cases
  passing; HIP BVH refit and the GL-window MCP material case were skipped.
  The following GL semantic-AOV child became unobservable in the restricted
  session and was stopped; the hardware/display gate remains unresolved.
- The remaining registered interval 132 and 134–165 completes with all 32
  executed tests passing. The unavailable GL-degenerate, OpenChess path-trace,
  and external-asset profiles are capability-skipped; Vulkan RT test 133 is
  the remaining hardware-only CTest boundary, while its direct preflight
  reports a clean skip.
- The final registered interval 166–296 passes 131/131 executed tests,
  including bridge, checker, texture synthetic, CUDA/HIP format, and diff
  coverage. Corpus and real-asset cases are capability-skipped where their
  external inputs are absent.
- A direct Vulkan-RT semantic-AOV run confirms the unresolved hardware gate:
  it reports divergent reference vectors and fails at `vk-rt coat-normal`
  because no screenshot is produced. This is consistent with the unavailable
  NVIDIA driver/ICD probe and is not counted as a product pass.
- The semantic-AOV preflight now recognizes the viewer's explicit `rt=off` /
  `rt_available=0` capability line and skips only Vulkan RT cases, preventing
  false `coat-normal` failures on raster-only Vulkan devices.
- After the preflight fix, registered Vulkan RT semantic-AOV test 133 skips
  cleanly in 1.88s. The GL semantic-AOV wrapper still cannot complete under
  this restricted X11 setup and remains an environment-only boundary.
- The same semantic-AOV harness now performs a bounded Xvfb/`xdpyinfo`
  preflight; direct GL invocation cleanly skips when the X11 socket cannot be
  opened instead of entering the render loop.
- Preflight-only capability skips no longer mark the whole semantic shard as
  degraded, so unrelated CUDA/HIP coverage remains executable. The focused
  GL/Vulkan-raster/RT/CUDA/HIP registration is now 6/6 (one intentional RT
  skip).
- After regenerating `build_ninja` from this worktree, the complete registered
  native aggregate passes 298/298 with no failures (1085.58s). Thirty-one
  capability skips cover unavailable RT/GL semantic backends, external assets,
  corpus inputs, and hardware-only texture validation; test 75 GL/Vulkan
  parity passes.

The first cold NVIDIA compilation of the full MaterialX RT pipeline measured
about 155 seconds; cached startup is a few seconds. The Vulkan smoke-test
timeout is 180 seconds so a healthy cold device is not incorrectly skipped.

The configured full native regression is green after the CUDA/HIP kernel
plumbing fix: 298/298 passed in 1112.57 seconds. Dedicated AMD Vulkan,
real-texture/validation profiles, and external-asset profiles remain
capability-skipped in this environment.

The focused GPU texture/backend slice also passes 13/13: synthetic GPU,
mip/BC6H, Vulkan formats, HIP formats/mips, CUDA formats/mips, and CUDA/HIP
semantic loader-parity checks. The remaining texture implementation gap is
derivative-aware filtering; hardware Vulkan RT and external-asset profiles
remain environment-bound.

Direct schema-surface graph lowering now covers `standard_surface` and
`open_pbr_surface` Shader-to-Shader outputs. The bridge regression verifies
authored base color, metalness, IOR, emission, opacity, and weight routes;
the OpenPBR-specific lane names, coat-affect/darkening fields, and thin-film
fields are covered as well, and the complete focused graph group passes 4/4.
Standard Surface thin-film presence is also derived from positive thickness
into the runtime weight lane, matching the evaluator's schema behavior.
Standard Surface roughness/IOR and OpenPBR base-roughness fallback aliases
are now normalized during direct graph lowering; the focused graph group
passes 4/4 after this coverage.
Direct `volume`/`volumeshader` Shader-to-Shader wrappers now forward nested
VDF and EDF closures into runtime volume lanes; the focused graph group still
passes 4/4.
Direct OpenPBR lowering now accepts the evaluator-compatible `fuzz_*` aliases
for sheen weight, color, and roughness in addition to canonical `sheen_*`
fields; the focused graph group remains 4/4.
Generalized-Schlick BSDF lowering now preserves `color90` and exponent through
generated view-angle power/mix nodes instead of retaining only `color0`; the
bridge regression passes with the RT scatter-mode case.
Direct `standard_surface` lowering now accepts both canonical `thin_film_*` and
legacy `thinfilm_*` thickness/IOR spellings, preferring canonical inputs; the
updated bridge regression and complete focused graph group pass 4/4.
Closure `mix` weighted color and roughness lanes now include the authored mix
factor in both numerator and denominator, preserving the expected background /
foreground contribution rather than averaging raw child weights.

## Remaining work, in priority order

1. Finish and validate remaining bridge closure fidelity, especially any
   schema-specific closure parameter mappings; standard diffuse, Chiang,
   generalized-Schlick, direct standard/OpenPBR surface lowering, and
   measured-EDF fallback behavior are now covered.
2. Preserve weighted colors/roughness where the OpenPBR lane representation
   permits it; vector2 roughness extraction, exact scatter-mode handling,
   composed roughness weighting, and weighted color lowering are covered by
   focused tests. Wrapper/layer combinations remain to be audited.
3. Complete remaining shader constructors/wrappers: displacement and any
   remaining unlit behavior in legacy and next converters; public
   `surfacematerial`/`surface_unlit` resolution and JSON runtime
   `surface_unlit` lowering are now covered,
   and the `light` EDF wrapper is now covered in both evaluator and bridge,
   volume/volumematerial graph transport and texture-fed absorption validation
   are now covered, including direct `volumematerial` Shader-to-Shader
   forwarding; schema-specific volume validation remains. Separate
displacement terminals now resolve authored scalar `displacement`, `height`,
`dispScalar`, or `value` inputs into the next loader's geometry lane.
4. Validate matrix-valued/interpolated/uniform geomprops in real scene assets;
   matrix-valued transport and object/world/view evaluator transforms are
   covered, while Vulkan instance-transform fidelity remains.
5. Close texture/projection gaps: derivatives,
   and any true measured-EDF
   profile backend beyond the documented color fallback. Nearest filtering is
   now covered for regular, latlong, triplanar, hextiled, and UDIM-capable image paths;
   sRGB RGB conversion and RGBA alpha preservation are covered as well.
6. The focused graph connection/evaluation/bridge suites now cover closure
   composition, wrapper nesting, multi-output selectors, missing inputs, type
   conversion, direct Shader-to-Shader graphs, dependency ordering, cycles,
   and the 64-node limit (4/4 focused tests pass). Remaining work is render
   path validation for these graph shapes, especially nested closures and
   multi-output routing on hardware backends.
7. Run focused CUDA/NVIDIA, HIP, and hardware Vulkan RT checks on capable
   machines; CUDA and HIP semantic-AOV checks now pass here, while the
   registered hardware Vulkan RT semantic sweep still requires a clean cold
   NVIDIA validation run and AMD hardware remains unavailable.
8. The complete registered native/viewer/tusdrender aggregate now passes
   298/298 with documented capability skips; repeat the hardware-only RT and
   external-asset profiles on capable machines when available.
9. Keep updating this file with the new HEAD and evidence after each coherent
   slice.
10. Before pushing, audit the exact outgoing range for credentials, personal
    paths, build artifacts, large binaries/assets, and stale generated output;
    request fresh authorization immediately before `git push`.

## Resuming prompt

Resume the active MaterialX/OpenPBR implementation in this worktree. Read
`AGENTS.md` and this file first. The direct closure bridge slice and scalar
closure multiplication fix are committed and tested. Continue with closure
fidelity, wrappers, geomprops/transforms, texture edge cases, and the
hardware/regression matrix in the numbered order above. Preserve the two
unrelated untracked paths, do not push without the exact-range
pre-push audit and fresh authorization, and do not claim completion from
focused tests alone.
