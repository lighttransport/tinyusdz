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
- HEAD: `b8dd11c88` (`Match conical EDF angle semantics`)
- Upstream: `origin/dev`
- Worktree: no tracked modifications; the two unrelated untracked paths above
  remain untouched.
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
- A fresh full configured regression at this HEAD is not clean: the aggregate
  GL/Vulkan parity test reports the unsupported-real-time-lobes comparison
  failing (19.6% of pixels), while its standalone test passes; the subsequent
  Vulkan RT texture-semantic-AOV test produced no output for roughly six
  minutes and was terminated. The older 298/298 result below predates this
  transform change and remains historical evidence only.

The first cold NVIDIA compilation of the full MaterialX RT pipeline measured
about 155 seconds; cached startup is a few seconds. The Vulkan smoke-test
timeout is 180 seconds so a healthy cold device is not incorrectly skipped.

The configured full native regression is green after the CUDA/HIP kernel
plumbing fix: 298/298 passed in 1112.57 seconds. Dedicated AMD Vulkan,
real-texture/validation profiles, and external-asset profiles remain
capability-skipped in this environment.

## Remaining work, in priority order

1. Finish and validate remaining bridge closure fidelity, especially any
   schema-specific closure parameter mappings; generalized-Schlick and
   measured-EDF fallback behavior are now covered.
2. Preserve weighted colors/roughness where the OpenPBR lane representation
   permits it; add vector2 roughness extraction and exact scatter-mode handling.
3. Complete remaining shader constructors/wrappers: `surface`,
   `surfacematerial`, `light`, displacement, and unlit behavior in legacy and
next converters; volume/volumematerial graph transport is now implemented,
but texture-driven and schema-specific volume validation remains. Separate
displacement terminals now resolve authored scalar `displacement`, `height`,
`dispScalar`, or `value` inputs into the next loader's geometry lane.
4. Validate matrix-valued/interpolated/uniform geomprops in real scene assets;
   matrix-valued transport is implemented across the RT graph paths, while
   object/world/view transform and Vulkan instance-transform fidelity remain.
5. Close texture/projection gaps: camera-aware latlong defaults, filtering and
   derivatives, colorspace/alpha, UDIM missing-tile behavior, and any true
   measured-EDF profile backend beyond the documented color fallback.
6. Add graph connection/evaluation/render tests for closure composition, wrapper
   nesting, multi-output selectors, cycles, missing inputs, type conversion,
   direct Shader-to-Shader graphs, and the 64-node limit.
7. Completed for this environment: focused CUDA/NVIDIA, HIP, and Vulkan RT
   checks pass; AMD hardware remains unavailable and is capability-skipped
   with evidence.
8. Completed for this slice: optimized full native/viewer/tusdrender regression
   passed 298/298 in 1107.08 seconds.
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
