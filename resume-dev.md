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
- HEAD: `abc287650` (`Evaluate arbitrary geomprops in CPU material graphs`)
- Upstream: `origin/dev`
- Worktree: tracked follow-up shader/renderer/test/evidence edits are pending;
  the two unrelated untracked paths above remain untouched.
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
(`3228d85cd`). Backend shader lookup and typed GPU ABI consumption remain open.
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
nodegraph-backed `surfacematerial` outputs. Host-side generic float/vector
geomprops are retained by the next scene loader; CPU RT, Vulkan hardware
ray-query, the shared CUDA/HIP kernel, and compute-BVH now evaluate the
supported streams; arbitrary typed variants remain open.

## Verified tests after HEAD

Passed:

- Complete configured native CTest suite: 298/298 passed (24 documented
  capability/asset skips)
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
- Full configured native CTest rerun after conductor and generalized-Schlick
  lowering changes: 298/298 passed, with 24 documented skips
- Focused bridge/evaluator/graph/ABI and CPU/GPU parity run after closure
  scalar-multiply fix: 12/12 passed
- Isolated `TestMaterialXUtilities` validation for nodegraph-backed wrapper
  conversion passed. The aggregate `test_tydra_next` executable still crashes
  in an earlier unrelated `TestChunkedArrayShareCowBudgetFailure` test.
- `tusdview_lightrt_bridge_test` after latlong default-view fix: passed
- Stable `next` regression after wrapper changes: 40/40 passed
- Viewer rebuild plus focused geomprop/bridge/ABI/nonmesh/safety checks: 6/6
  passed after generic geomprop stream preservation (`3228d85cd`)
- Focused CPU RT, bridge, and safety checks after live arbitrary geomprop
  evaluation: 6/6 passed (`abc287650`)

The first cold NVIDIA compilation of the full MaterialX RT pipeline measured
about 155 seconds; cached startup is a few seconds. The Vulkan smoke-test
timeout is 180 seconds so a healthy cold device is not incorrectly skipped.

The configured full native regression is now green. Dedicated AMD Vulkan,
real-texture/validation profiles, and external-asset profiles remain
capability-skipped in this environment.

## Remaining work, in priority order

1. Finish and validate bridge closure lowering: diffuse, translucent,
   dielectric, conductor, generalized-Schlick, subsurface, sheen, Chiang hair,
   EDF, VDF, and add/mix/multiply/layer.
2. Preserve weighted colors/roughness where the OpenPBR lane representation
   permits it; add vector2 roughness extraction and exact scatter-mode handling.
3. Complete shader constructors/wrappers: `surface`, `surfacematerial`,
   `volume`, `volumematerial`, `light`, displacement, and unlit behavior in
   legacy and next converters.
4. Complete arbitrary typed/interpolated/uniform GPU geomprop transport and
   object/world/view transform matrices, including Vulkan instance transforms.
5. Close texture/projection gaps: camera-aware latlong defaults, filtering and
   derivatives, colorspace/alpha, UDIM missing-tile behavior, and measured EDF
   profile handling.
6. Add graph connection/evaluation/render tests for closure composition, wrapper
   nesting, multi-output selectors, cycles, missing inputs, type conversion,
   direct Shader-to-Shader graphs, and the 64-node limit.
7. Run focused CUDA/NVIDIA and HIP/AMD tests, then Vulkan RT on NVIDIA and AMD;
   capability-skip only when documented hardware is genuinely unavailable.
8. Run optimized full native/viewer/tusdrender regression and keep the suite
   near the established 20-minute budget.
9. Update this file with the new HEAD and evidence after each coherent slice.
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
