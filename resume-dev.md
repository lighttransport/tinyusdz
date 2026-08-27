# Resume: MaterialX/OpenPBR Vulkan work

## Current state

- Branch: `dev`
- Current HEAD: `5a78a0696 Preserve typed OpenPBR constants across graph lowering`
- Preserve the unrelated untracked paths `run.sh` and `usd-assets`.
- Do not push without the repository's exact-range pre-push audit and fresh
  user authorization.

The MaterialX/OpenPBR implementation and registered regression matrix are in a
good state. The most recent work fixed default/legacy loader parity, avoided
unnecessary Vulkan full-shader promotion, and bounded runtime shader generation.

Recent commits:

- `5a78a0696` Preserve typed OpenPBR constants across graph lowering
- `7ebf28848` Bound MaterialX Vulkan shader compilation
- `e0768baf0` Avoid unnecessary full shader promotion for constant lanes
- `12286c5a4` Fix Vulkan semantic AOV GPU preflight
- `ccc0f984a` Record NVIDIA hardware validation results
- `f9c4c836c` Record final full regression aggregate

## Implemented behavior

### Typed OpenPBR and graph lowering

- Unconnected typed surface inputs are no longer serialized as executable
  MaterialX Constant nodes by the next converter. Only authored connections
  enter the runtime graph.
- Typed OpenPBR values survive evaluation of unrelated graph-connected lanes.
  Graph routes still take precedence where they genuinely drive an input.
- Constant and Convert-of-Constant lanes remain in the compact Vulkan path and
  do not unnecessarily promote a material to the full interpreter.
- Constant-only OpenPBR materials retain the converter's authoritative shader
  type instead of selecting a synthesized PreviewSurface fallback on ties.
- Default and legacy loaders now match for OpenPBR IOR/F0 and all other tested
  semantic lanes.

### Vulkan shader safeguards

- Default MaterialX Vulkan source limit: 129 KiB.
- Default live `glslc` deadline: 30 seconds.
- CLI overrides:
  - `--materialx-vk-shader-max-kib N`
  - `--materialx-vk-compile-timeout N`
- JSON configuration under `render`:
  - `materialx_vulkan_shader_max_kib`
  - `materialx_vulkan_compile_timeout_sec`
- Oversized automatic full-shader promotion retains the compact pipeline.
- A timed-out live compiler process is terminated while the last-good pipeline
  remains active.

## Latest verification evidence

- Full `build_ninja` build passed.
- Focused MaterialX/OpenPBR regression passed 10/10:
  - tusdrender CPU graph
  - flake parity
  - projection parity
  - LightRT bridge/evaluator/connection/evaluation
  - OpenPBR material
  - geometry primvar
  - texture pipeline
- Standalone Tydra Next test executable passed all tests.
- Complete OpenPBR semantic-AOV matrix on NVIDIA Vulkan hardware RT passed for
  both default and legacy loaders. Every loader comparison reported image MAD 0,
  including IOR/F0, specular F0, normals, coat, occlusion, core factors,
  emission, and opacity.
- NVIDIA Vulkan ray-query and CUDA MaterialX flake parity passed 2/2. The former
  CUDA failure was a harness diagnostic-name mismatch: CUDA reports
  `graphs`/`graph_nodes`, while Vulkan reports `graphMaterials`/`graphNodes`.
- Displacement UDIM test passed with loader and package parity.
- OpenChess MaterialX smoke passed CPU RT, forced NVIDIA Vulkan RT, and Vulkan
  raster rendering. The production path-trace benchmark remains opt-in.
- Runtime compiler watchdog was verified with a deliberately stalled compiler:
  it was terminated at the configured one-second limit and retained the
  last-good Vulkan pipeline.
- The last complete registered aggregate passed 298/298 with documented
  capability/asset skips.

## Known limitation

The embedded full MaterialX Vulkan interpreter is approximately 195 KiB of GLSL
and 549 KiB of SPIR-V. With an explicit 256 KiB source-limit override, NVIDIA
hardware reaches driver-side `vkCreateComputePipelines` validation but can still
take longer than the bounded 120-second diagnostic run on a cold cache.

The default 129 KiB guard safely avoids this path and keeps the compact,
ABI-compatible pipeline active. The 30-second process watchdog applies to live
`glslc` compilation; an in-process Vulkan driver JIT cannot currently be killed
safely after entering `vkCreateComputePipelines`.

## Remaining tasks

1. Reduce or split the full Vulkan MaterialX interpreter enough to compile
   predictably on a cold NVIDIA driver cache. Prefer per-feature shader variants
   or generated dead-lane elimination over raising the default source limit.
2. If full cold compilation must remain available, investigate a safe
   driver-JIT strategy such as pipeline-creation cache-control fail-fast probes,
   a helper-process precompiler, or another design that cannot strand the main
   viewer process inside `vkCreateComputePipelines`.
3. Add a focused converter regression asserting that a direct constant-only
   OpenPBR material produces no executable graph outputs while preserving its
   typed IOR and lobe values.
4. Expand hardware render validation for nested closure wrappers and less common
   schema-specific volume materials using real authored assets when available.
5. Run the production OpenChess path-trace benchmark only when its long-run
   opt-in gate is desired; the ordinary smoke coverage is already green.
6. Before any push, audit every outgoing commit for credentials, personal paths,
   build artifacts, and unintended binaries/assets, then ask for fresh push
   permission.

## Suggested next command

Start with the direct-constant converter regression, then profile shader feature
usage for the full-interpreter split:

```sh
cmake --build build_ninja -j16
ctest --test-dir build_ninja \
  -R 'tusdview_lightrt|tusdview_openpbr|materialx-(cpu-graph|flake|projection)' \
  --output-on-failure
```
