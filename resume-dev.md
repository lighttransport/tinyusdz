# Resume: full MaterialX/OpenPBR renderer parity

Work in this repository and follow `AGENTS.md`. Do not rewrite published
history. Do not touch or add the unrelated untracked `run.sh` or `usd-assets`.

## Repository state

- Branch: `dev`
- Local HEAD: `8d21ba688` (`Add MaterialX glossiness anisotropy evaluation`)
- Recorded remote tip: `origin/dev` at `d8068d7fc`
- The local branch contains the continuing MaterialX renderer work and has not
  been pushed in this session.
- Worktree is clean except for this resume update and the unrelated untracked
  `run.sh` and `usd-assets`.

## Current renderer coverage

- The bounded 64-node MaterialX graph ABI is shared by tusdview CPU RT,
  CUDA/HIP RT, Vulkan RT, and headless tusdrender.
- Arithmetic, conditionals, compositing, channel operations, matrices,
  procedural noise/patterns, ramps, flake multi-outputs, image/tiled-image,
  latlong and triplanar projection, normal maps, and spatial bump derivatives
  are executable rather than topology-only fallbacks.
- `heighttonormal(image)` uses centered texel differences on CPU, CUDA/HIP,
  and Vulkan. The standalone evaluator also differentiates connected
  procedural graphs by reevaluating at neighboring UVs.
- Runtime shading-context nodes now include position, normal, view direction,
  time, and frame opcodes. CPU and Vulkan receive real view direction in their
  primary paths. CUDA/HIP and time/frame still need complete call/API plumbing.
- The standalone evaluator has object/world/view transform matrices and exact
  point/vector/inverse-transpose-normal transformations.
- `geompropvalue` supports arbitrary named lookup through a standalone
  renderer callback. GPU graph lowering supports standard bounded primvars:
  st/UV, display color, position, normal, tangent, and bitangent; arbitrary GPU
  primvar storage is still pending.
- Upstream-exact physical helpers now include blackbody,
  roughness_anisotropy, roughness_dual, glossiness_anisotropy, and the two
  artistic_ior outputs.
- Standalone `surface_unlit` parameter evaluation exists, but the USD/Tydra
  legacy and next converters do not yet recognize the unlit shader schema.
- OpenPBR graph routes already include spatial volume density, albedo, and
  emission lanes, but closure-based VDF/EDF/volume/volumematerial composition
  is not implemented.

## Latest commits

- `85c2ce717` Add MaterialX spatial bump evaluation
- `044c6905c` Add MaterialX shading context evaluation
- `d36bb43d9` Add MaterialX geometry property evaluation
- `18034da33` Add MaterialX physical helper nodes
- `4e235e944` Add MaterialX artistic IOR evaluation
- `8d21ba688` Add MaterialX glossiness anisotropy evaluation

## Verification in the current session

- Rebuilt tusdview, tusdrender, the bridge test, standalone evaluator test, and
  standalone graph-evaluation test after the ABI/operator changes.
- Focused bridge, evaluator, graph-evaluation, and GPU-material-ABI CTests pass.
- The real Vulkan rendered bump delta passed when NVIDIA Vulkan was visible.
- llvmpipe produces a flat path-traced image for that hardware semantic case;
  the fixture now capability-skips it while hermetic derivative tests remain
  active.
- The current boot no longer exposes a usable NVIDIA driver or `/dev/kfd`, so
  strict CUDA/HIP and NVIDIA/AMD hardware reruns remain pending.
- Generated `trace_materialx_path.spv.h` was rebuilt from
  `trace_materialx_path.comp`; never edit it by hand.

## Authoritative upstream inventory

The MaterialX 1.39 `stdlib_defs.mtlx` and `pbrlib_defs.mtlx` currently expose
175 distinct node categories. The remaining material gaps are concentrated in
these groups:

1. Complete GPU context transport for view direction on every CUDA/HIP route,
   scene time/frame, and named object/world/view transformations. The Vulkan
   headless path currently flattens geometry and does not preserve object-space
   transforms, so this needs explicit instance transform data rather than an
   identity placeholder.
2. Arbitrary typed/interpolated GPU `geompropvalue` primvars beyond the standard
   built-ins, including uniform primvars.
3. Hair helpers: `deon_hair_absorption_from_melanin`,
   `chiang_hair_absorption_from_color`, and multi-output
   `chiang_hair_roughness`. Upstream's current OSL roughness implementation is
   itself a TODO returning zero, which must be handled and documented.
4. Closure nodes: diffuse/translucent/dielectric/conductor/generalized-Schlick,
   subsurface, sheen, and Chiang hair BSDFs; uniform/conical/measured EDFs;
   absorption/anisotropic VDFs; typed closure add/mix/multiply/layer.
5. Shader constructors and wrappers: `surface`, renderer-wide `surface_unlit`,
   `surfacematerial`, `volume`, `volumematerial`, `light`, and displacement
   constructors.
6. Closure-based spatial volume evaluation and mapping into the renderer's
   density/albedo/emission representation.
7. Exact camera-aware latlong default view direction, filtering/derivative,
   colorspace/alpha, and UDIM-projection edge cases.
8. Expanded connection/evaluation/render suites for all remaining nodes,
   multi-output selectors, wrapper nesting, cycles, missing inputs, types, and
   the 64-node bound.
9. Strict hardware matrix: Vulkan RT on NVIDIA and AMD, CUDA RT on NVIDIA, HIP
   RT on AMD, followed by focused and full native regression within the
   established approximately 20-minute whole-suite budget.
10. Re-run an exact-range pre-push audit and request fresh authorization before
    any push.

The active objective remains full MaterialX/OpenPBR support across Vulkan RT,
CUDA RT, HIP RT, and headless tusdrender. Do not mark it complete from focused
tests alone.
