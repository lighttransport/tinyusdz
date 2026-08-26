# Resume: MaterialX/OpenPBR GPU parity improvements

Work in this repository and follow `AGENTS.md`. Do not rewrite published
history. Do not touch or add the unrelated untracked `run.sh` or `usd-assets`.

## Repository state

- Branch: `dev`
- Public remote: `https://github.com/lighttransport/tinyusdz.git`
- Local `HEAD`: `0375387f1` (`Add MaterialX cell noise and random nodes`)
- `origin/dev`: `d8068d7fc` (`Expand headless MaterialX GPU parity`)
- Local `dev` is two commits ahead. The triangle-wave/checkerboard,
  compositing, circle, and line enhancements described in items 27-29 are
  and color-correction enhancements through item 30 are committed locally.
  The `fractal2d` work in item 32 is uncommitted.

## Implemented in the worktree

1. Fixed Vulkan LightRT material-path light uploads. The shader/API uses a
   16-float light record, but the host uploaded only eight floats and truncated
   environment radiance, producing black descriptor-backed texture renders.
2. Added `examples/tusdview/gpu_material_abi.h` plus the
   `tusdview-gpu-material-abi` CTest guard for host, CUDA/HIP, and Vulkan material,
   graph, light, and texture-record layouts.
3. Fixed CUDA/HIP graph interpretation using the obsolete 46-float header while
   the host packs 50 floats, and fixed Vulkan routing only 44 of 48 OpenPBR
   outputs.
4. Added live MaterialX `asin`, `acos`, `contrast`, and typed `swizzle` graph
   operators to the shared IR and both GPU interpreters. Swizzle supports rgba/
   xyzw selectors plus constant zero/one lanes and normalizes nodedef names such
   as `ND_swizzle_color4_color3`.
5. Added `scripts/run-tusdrender-materialx-hardware-matrix.sh`, a strict CI gate
   requiring Vulkan RT on configured NVIDIA/AMD devices, CUDA on NVIDIA, and HIP
   on AMD without software fallback.
6. Improved transmission/volume fidelity by preventing Vulkan from applying a
   depth-based transmission color both at the interface and as Beer absorption.
7. Added topology-aware displacement mip selection from incident UV-edge
   footprints. Dense meshes retain level zero; coarse meshes filter
   high-frequency height maps instead of aliasing them into vertex spikes. UDIM
   displacement uses the resident tile dimensions and per-tile mip chains.
8. Aligned Vulkan clamp-to-border fallback with CUDA/HIP and fixed connected
   MaterialX opacity bypassing MASK `alphaCutoff` on CUDA/HIP.
9. Tightened the headless parity fixture: Vulkan texture output must be nonblank,
   corrected live graph evaluation is compared across CUDA/HIP, and displacement
   now uses a generated high-frequency texture. A generated typed-swizzle scene
   also checks the actual `color4 -> color3` `bgr` result on every available RT
   backend, rather than accepting retained graph topology alone.
10. Versioned the persistent CUDA PTX cache schema after the graph-header and
    launch ABI changes. This prevents a loadable kernel compiled for an older
    packed-record contract from being reused merely because it survived in a
    copied or stale cache.
11. Added `TUSDR_PARITY_CUDA_CACHE_EXPECT=cold|warm` to the headless parity
    harness. With required CUDA and an isolated `XDG_CACHE_HOME`, it asserts the
    cold NVRTC compile/persist transition and the subsequent cache hit while
    still checking actual typed graph pixels. The strict hardware wrapper runs
    both passes when `TUSDR_CI_CUDA_CACHE_CYCLE=1` is set.
12. Fixed the actual CUDA/HIP executable-graph failure exposed by typed
    swizzle. Packed input and texture indices use `-1` for constant/absent, but
    `(int)(value + 0.5)` truncated `-0.5` to zero. The shared kernel now uses
    `floorf(value + 0.5)`, preserving sentinels; the ABI validator rejects the
    old decoding pattern.
13. Applied the same sentinel correction to tusdview's CPU graph interpreter
    and added its missing `asin`, `acos`, `contrast`, and typed `swizzle`
    operators. The ABI/parity validator now guards the CPU decoder and operator
    table as well as CUDA/HIP and Vulkan. Removed its obsolete 64-pass
    fixed-point evaluation loop because compilation already sorts dependencies
    and rejects cycles; CPU graph shading is now one bounded pass. A reversed
    authoring-order unit fixture locks dependency-first packing.
14. Added `tusdview-cpu-rt-materialx-swizzle`, a ~1 s fail-fast headless pixel
    test and small USDA fixture that verifies `bgr` moves the strongly blue
    authored `color4` input into a strongly red CPU-rendered output. CPU-only RT
    CTests are now exempt from the NVIDIA GL warmup/offload fixture, so stale
    Xvfb or PRIME state cannot block their automatic Vulkan presentation shell.
    Updated the viewer/testing docs, which still incorrectly described CPU RT
    as flat-material-only and lacking headless screenshots.
15. Added executable MaterialX `atan`, `screen`, and `overlay` operators to the
    shared graph IR and CPU, CUDA/HIP, and Vulkan interpreters. The headless
    parity fixture chains all three and asserts the resulting channel ordering
    on every required backend.
16. Added a committed six-panel OpenPBR lobe fixture and deterministic 192x140,
    16-sample visual goldens. Vulkan RT is checked against its reference while
    CUDA and HIP share their byte-identical kernel reference; a 1% normalized
    RMSE gate allows small driver/compiler variation.
17. Added MaterialX `burn` and `dodge` across CPU, CUDA/HIP, and Vulkan, using
    the upstream standard-library edge cases and `fg`/`bg`/`mix` semantics.
    Auditing those definitions also corrected `screen` and `overlay`: both now
    honor the third `mix` input, and overlay selects multiply versus screen from
    the background channel as specified. The executable parity graph chains all
    five blend/atan operations.
18. Added spec-matched `ramplr` and `ramptb` nodes across all four interpreters.
    Connected texcoords use the third graph input; omitted texcoords correctly
    fall back to the hit UV. The headless fixture checks independent horizontal
    red/blue and vertical green gradients rather than relying on topology or a
    single center pixel.
19. Added `splitlr` and `splittb` across CPU, CUDA/HIP, and Vulkan. Their fourth
    logical texcoord dependency is retained in the otherwise-unused image slot
    of non-image graph records, preserving the fixed 21-float GPU ABI. A
    connected-texcoord quadrant fixture exposed and fixed a Vulkan regression
    where `ND_texcoord_vector2` incorrectly passed through image UV routing and
    collapsed to `(0, 0)` instead of returning the hit UV.
20. Added standalone, hermetic MaterialX graph-connection and graph-evaluation
    CTest suites. They cover forward references, nodegraph-output indirection,
    output selectors, surface binding, chained scalar/vector operations,
    conditionals, spatial UV nodes, and evaluator memo reset between shade
    points. Both suites run without a GPU or display.
21. Closed standalone evaluator parity for `ramptb`, `splittb`, `screen`,
    `overlay`, `burn`, and `dodge`. Their numerical tests share the same
    `fg`/`bg`/`mix`, vertical-coordinate, and split-threshold expectations as
    the CPU and GPU runtime graph interpreters.
22. Corrected runtime `saturate` semantics across CPU, CUDA/HIP, and Vulkan.
    It now interpolates each RGB channel from luminance using the authored
    `amount`, matching MaterialX and the standalone evaluator, instead of being
    miscompiled as a clamp. The hardware blend-chain fixture now executes it.
23. Corrected four-input `ifgreater`, `ifgreatereq`/`ifgreaterequal`, and
    `ifequal` runtime graphs. The fourth branch uses the non-image auxiliary
    lane as either a connected node or a packed literal. Cycles and graphs over
    the fixed 64-node execution bound are now rejected explicitly.
24. Added renderer parity for `rgbtohsv`, `hsvtorgb`, `rotate2d`, and the
    `oneminus` alias, plus evaluator parity for common category aliases,
    `fract`/`fraction`, and `step`. Graph tests now cover graph-local duplicate
    names, unresolved producers, conditional boundaries, HSV round trips, and
    memoized UV reevaluation.
25. Lowered high-arity `ramp4` and ten-input `switch` nodes into bounded graph
    primitives, retaining connected values and texcoords without changing the
    21-float node ABI. Added vector geometry, premultiply, component reduction,
    mask, logical, geomcolor, and bitangent operations across CPU, CUDA/HIP,
    Vulkan, and the standalone evaluator.
26. Raised default CPU tusdrender's guarded MaterialX graph evaluation depth
    from 10 to the renderer's 64-node bound. A new headless CTest renders the
    authored deep-16 ChainTest and rejects the former fallback warnings.
27. Added exact stdlib graph lowerings for `trianglewave` and `checkerboard`,
    with connected hit-UV execution and standalone numerical evaluation. The
    strict hardware fixture now checks their rendered spatial pattern.
28. Added `difference`, `in`, `mask`, `matte`, `out`, `over`, and
    `disjointover` to CPU, CUDA/HIP, Vulkan, and the standalone evaluator,
    including upstream alpha and `mix` semantics.
29. Added exact stdlib graph lowerings for `circle` and `line` using existing
    bounded runtime primitives, plus direct standalone evaluation and numerical
    tests.
30. Added exact `colorcorrect` lowering through the upstream hue, saturation,
    signed reciprocal-gamma, lift, gain, contrast, and exposure graph. Color4
    preserves authored alpha through a small `SetAlpha` runtime primitive.
    Fixed scalar-to-vector promotion in runtime graph packing and replaced the
    CUDA/HIP `hsvadjust` pass-through with live evaluation. A dedicated
    headless fixture checks hue rotation rather than topology alone.
31. Added upstream-exact Jenkins integer hashing for `cellnoise2d` and
    `cellnoise3d` across CPU, CUDA/HIP, Vulkan, and the standalone evaluator.
    Added bounded stdlib lowerings for `randomfloat` and `randomcolor`, including
    float-input scaling, seeded channel offsets, authored ranges, and HSV output.
    Numerical tests lock same-cell stability and boundary changes, while the
    rendered pattern fixture executes connected cell noise at hit UVs.
32. Added upstream-compatible `fractal2d` evaluation across CPU, CUDA/HIP,
    Vulkan, and the standalone evaluator. The lowering preserves connected or
    vector amplitude outside the bounded four-input core and carries output
    channel count through the packed ABI. Scalar and color numerical tests
    cover the exact Perlin/hash octave result, and the rendered pattern fixture
    now executes the fractal at interpolated hit UVs.
33. Added upstream-exact `worleynoise2d` and `worleynoise3d` evaluation across
    CPU, CUDA/HIP, Vulkan, and the standalone evaluator. Distance and solid
    styles preserve MaterialX's nearest-feature search, scalar/vector channel
    behavior, and Jenkins-derived feature positions. Numerical tests cover 2D
    F1/F2/F3, solid cell color, and 3D F1/F2; the rendered spatial fixture now
    includes live Worley modulation.
34. Replaced the placeholder hash implementations of `noise2d` and `noise3d`
    with MaterialX Jenkins/Perlin evaluation for scalar and vector outputs on
    every renderer backend. Added an exact `fractal3d` bounded-core lowering,
    including connected diminish and vector amplitude. Added full stdlib
    lowerings for `unifiednoise2d` and `unifiednoise3d`: frequency/offset,
    jitter rotation, Perlin/cell/Worley/fractal selection, output fitting, and
    conditional clamping. Standalone numerical coverage locks every 2D noise
    selection and 3D fractal selection, while the rendered fixture executes a
    connected unified Worley graph.
35. Added exact bounded `cloverleaf` and signed-distance `hexagon` shape
    operators across CPU, CUDA/HIP, Vulkan, and the standalone evaluator.
    Default hit-UV synthesis and connected center/radius inputs are preserved;
    numerical inside/outside tests and bridge opcode tests cover both shapes.

The Vulkan compute shader was recompiled into
`trace_materialx_path.spv.h`. Do not edit the generated header by hand.

## Verification

- Strict CUDA hardware parity passed on an NVIDIA GeForce RTX 5060 Ti: cold PTX
  cache 112.26 s and warm cache 26.94 s. Both runs verified the requested cache
  transition and typed-swizzle pixels.
- Strict HIP hardware parity passed in 74.10 s on an AMD Radeon RX 9070 XT
  (`gfx1201`).
- Explicit Vulkan hardware RT parity passed in 69.59 s on both physical devices:
  GPU 0 NVIDIA RTX 5060 Ti and GPU 1 AMD RX 9070 XT (RADV).
- The complete 144-test tusdview/Xvfb suite finished in 169.29 s. Two tests
  timed out under eight-way GPU/MCP contention; both immediately passed serially
  (`tusdview-gl-nonmesh-render` 0.83 s and `tusdview-backface-material` 35.73 s).
- Full native CTest passed 293/293 in 271.82 s at four-way concurrency, including
  the NVIDIA GLVND warmup, CUDA, HIP, Vulkan RT, MaterialX/OpenPBR parity, and all
  available tusdview tests. Optional external-asset/corpus tests capability-
  skipped as configured.
- Stable-next rebuilt successfully and passed 40/40 in 8.71 s.
- Focused CPU/MaterialX regression, ABI, bridge, parser, and all four CPU RT
  tests passed before the full gate.
- Regenerated `trace_materialx_path.spv.h` was decoded and compared byte-for-
  byte with a fresh Vulkan 1.2 `glslc` compile: exact 150,128-byte match
  (`sha256 2e29c1249fcdb54d5ad3bc534f43f0e1a0df060f8ba74dc7a85ebff069c30139`).
- `git diff --check`, Python compilation, shell syntax, generated ABI validation,
  and personal-path scan: pass.
- Expanded strict hardware parity (Vulkan RT on NVIDIA/AMD, CUDA, and HIP),
  including the new operators and both golden families: passed in 69.19 s.
- Strict hardware parity after the spec-correct blend update passed in 158.35 s,
  including the expected cold CUDA/HIP kernel recompilation.
- Warm strict hardware parity with the spatial ramp fixture passed in 75.81 s.
- Strict hardware parity with connected-texcoord split coverage passed in
  80.39 s across Vulkan RT on NVIDIA and AMD, CUDA on NVIDIA, and HIP on AMD.
- Strict hardware parity after the MaterialX saturation fix passed in 161.76 s
  across the same four hardware paths; the shared CUDA/HIP kernel rebuilt cold.
- Strict hardware parity with RGB/HSV round-trip and a connected four-input
  conditional passed in 158.29 s across Vulkan RT on NVIDIA/AMD, CUDA, and HIP.
- Strict hardware parity with connected `ramp4` lowering and a conditional-
  driven lowered `switch` passed in 156.46 s across the same four paths.
- The current focused CPU/bridge/evaluator/ABI suite passes 6/6 in 0.64 s.
- The current boot exposes neither `/dev/nvidia0` nor `/dev/kfd`; the strict
  hardware gate therefore stopped after Vulkan with required CUDA/HIP absent.
  Re-run it when the NVIDIA and AMD device nodes are restored.

## Remaining work

1. Continue incremental MaterialX coverage using the bounded graph ABI and the
   non-image auxiliary-input convention where a standard node needs a fourth
   dependency.
2. Continue the upstream stdlib inventory with procedural patterns, noise,
   matrix/transform, texture, geometry-property, and surface wrapper nodes.
3. Extend spatial MaterialX evaluation into volume shader inputs; current next
   volume resolution handles connected constants but not texture/noise graphs.
4. Keep `run.sh` and `usd-assets` untracked. If a later push is requested, run
   the mandatory exact-range pre-push audit and request fresh authorization.
