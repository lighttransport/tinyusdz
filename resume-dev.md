# Resume: MaterialX/OpenPBR GPU parity improvements

Work in this repository and follow `AGENTS.md`. Do not rewrite published
history. Do not touch or add the unrelated untracked `run.sh` or `usd-assets`.

## Repository state

- Branch: `dev`
- Public remote: `https://github.com/lighttransport/tinyusdz.git`
- `HEAD` and `origin/dev`: `ac3cc417f` (`Update renderer development handoff`)
- The renderer improvements below are uncommitted.

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

## Remaining work

1. Commit only after the user requests a commit. Keep `run.sh` and `usd-assets`
   untracked.
2. If a push is requested, run the mandatory exact-range pre-push audit and ask
   for fresh push authorization after reporting its results.
