# lusdview Vulkan Rendering Optimization Plan

## Summary

Build on the existing two-pass transparency path with weighted blended
order-independent transparency (OIT), identity-preserving material resource
deduplication, and cached Vulkan submission queues. Vulkan uses OIT when the
required features and formats are available; the existing sorted path remains
the fallback. The work covers transparent meshes, instances, points, curves,
Gaussians, and volumes without changing ray-tracing behavior.

## Implementation Status

- Completed: identity-preserving material canonicalization, canonical Vulkan
  raster parameter/graph tables, logical-ID-safe MaterialId output, live-edit
  remapping, canonical batching with logical submesh ranges, render-report
  counters, authored `doubleSided` behavior, cached sorted-fallback ordering,
  descriptor-bind deduplication, and transparent instance-group ordering.
- Completed: runtime capability validation for independent blending and the
  `RGBA16F` accumulation / `R16F` revealage formats. The capability is exposed
  through renderer diagnostics without changing the active fallback yet.
- Completed: optional OIT attachments/render pass, mesh/instance/native-carrier/
  volume fragment variants, independent-blend graphics pipelines, and the
  storage-image compute composite. Resource, format, shader, or pipeline
  failures disable OIT and retain the sorted fallback without failing startup.
- Completed: `--transparency auto|weighted|sorted` parsing and
  `render.transparency` startup configuration, including propagation across
  runtime backend recreation and an explicit warning when forced weighted OIT
  is unavailable.
- Completed: active opaque/OIT/compute-composite/overlay submission for meshes,
  instanced meshes, native point/curve/Gaussian carriers, and volumes. OIT uses
  stored opaque depth; selection and X-ray overlays are restored afterward.
- Completed: static mixed-opacity instance groups are partitioned into contiguous
  opaque and transparent ranges over shared geometry and instance buffers.
  Runtime-compacted visibility updates conservatively retain their legacy class
  until per-class cull counts are supplied.
- Completed: forced auto/weighted/sorted transparency regression coverage and
  a reversed-layer assertion proving equal-opacity weighted composition is
  independent of transparent submission/depth order.
- Completed: render-report observability for requested mode, active weighted
  state, hardware support, and exact OIT attachment bytes. Sorted mode skips
  OIT viewport allocation.
- Verified: weighted raster OIT on AMD Radeon RX 9070 XT (RADV GFX1201),
  including a 256x256 capture and render report, plain Khronos validation, and
  GPU-assisted descriptor/OOB validation using locally built ValidationLayers
  1.3.275.
- Completed: optional Linux ValidationLayers source setup and CTest wrapper.
  CI consumes a package/prebuilt layer when present and otherwise skips without
  downloading or compiling the layer.
- Completed: per-frame OIT draw-call and attachment-memory counters in render
  reports.
- Fixed and verified: Vulkan's unlit/ambient fallback now matches the OpenGL
  material shader (`baseColor * 0.12`) when no direct light contributes. This
  restores the multi-light backend-parity fixture without changing weighted
  transparency behavior. The GL/Vulkan parity, opacity-material,
  transparency, Vulkan render, stacked-glass, shadow-alpha-instance,
  double-sided, plain validation, and GPU-assisted validation checks pass.
- Hardened: the full backend-parity test timeout is 180 seconds so software or
  cold-start CI runs are not killed by the previous 60-second harness limit.
- Completed: exact per-present-frame Vulkan pipeline and descriptor-set bind
  counters cover raster, shadow, OIT, compute, native-carrier, volume, RT, and
  overlay command recording and are exposed in render reports. Transparency
  regression coverage requires both counters to be present and nonzero.
- Completed: a reproducible synthetic material/OIT performance matrix preserves
  32 authored material identities plus the fallback while collapsing them to
  three canonical raster payloads. The benchmark enforces deduplication, bind
  observability, weighted OIT draws, and zero sorted-mode OIT allocation/work.

### Synthetic Vulkan Performance Matrix

Run `examples/lusdview/tests/run-vk-material-oit-benchmark.sh`. The defaults use
32 authored materials, eight frames, and a 512x512 viewport; `MATERIALS`,
`FRAMES`, `SIZE`, and `LUSDVIEW` are overridable. On PRIME systems, use the
NVIDIA offload environment documented in `doc/lusdview.md`, or select another
available Vulkan device through the normal viewer environment/configuration.

The initial NVIDIA hardware run at 256x256 and four frames produced:

| Mode | Logical | Canonical | Deduplicated | Pipeline binds | Descriptor binds | OIT draws | OIT bytes | Elapsed seconds |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| weighted | 33 | 3 | 30 | 5 | 14 | 16 | 655360 | 17.9832 |
| sorted | 33 | 3 | 30 | 3 | 10 | 0 | 0 | 18.0715 |

Elapsed time includes process, scene-conversion, pipeline-startup, and frame
time, so it is informational rather than a pass threshold. Structural metrics
are asserted exactly and are suitable for regression testing across machines.

## Transparency

- Add opaque/Mask, OIT accumulation, and composite passes. Use `RGBA16F`
  accumulation with additive blending and `R16F` revealage with multiplicative
  blending. Reuse stored opaque depth with depth writes disabled during OIT.
- Composite in linear space and encode sRGB only once at final output. Use the
  standard depth-weighted formula:
  `clamp(pow(min(1, alpha * 10) + 0.01, 3) * 1e8 *
  pow(1 - depth * 0.9, 3), 1e-2, 3e3)`.
- Add OIT variants for mesh, instanced mesh, native carrier, and volume
  fragments. Normalize premultiplied volume output before common accumulation.
- Enable and validate Vulkan `independentBlend` and attachment capabilities.
  Fall back on feature, format, allocation, or pipeline failure.
- Add `--transparency auto|weighted|sorted` and matching
  `render.transparency` configuration. `auto` is the default. OpenGL and
  unsupported Vulkan devices use sorted transparency.
- Honor authored `doubleSided` consistently. Mask remains alpha-tested and
  depth-writing. Blend shadows remain omitted. Picking and diagnostic/AOV
  passes remain alpha-aware, deterministic depth-writing passes. Selection and
  X-ray overlays render after OIT composition.

## Material Identity and Deduplication

- Replace destructive scene material removal with a `DrawMaterialTable` that
  maps logical authored IDs to canonical raster payload representatives.
- Preserve material records, names, paths, IDs, and independent live editing.
  Share only Vulkan raster constants, descriptors, graph data, and state keys.
- Serialize render-affecting fields explicitly for hashing/equality. Include
  raster constants, textures and sampling, alpha heuristics, shader-selection
  flags, volume data, and packed MaterialX graphs. Normalize signed zero, keep
  non-finite materials unique, and verify hash collisions with exact equality.
- Keep Vulkan RT arrays logically indexed. Rebuild the raster canonical map
  after a material edit so the edited material can split without affecting its
  former peers.
- Batch next-loader geometry by canonical identity while retaining logical
  submesh ranges. Coalesce equivalent ranges only in shaded submission;
  MaterialId and picking modes preserve authored distinctions.
- Keep canonical and logical material IDs separate in Vulkan draws without
  increasing the 128-byte push constant: `ids.x` addresses the canonical
  raster table, while the logical ID and displacement-enable bit are encoded in
  the existing `emissive.w` payload.

## Submission and Instance Optimization

- Build cached opaque/Mask, OIT, and diagnostic draw queues whenever scene
  geometry or canonical mappings change. Cache the resolved pipeline variant,
  material binding, cull state, mesh/range, and logical IDs.
- Coalesce adjacent shaded ranges with equivalent state and group
  order-independent work for pipeline/descriptor locality. Remove per-frame
  alpha vectors, centroid sorting, and repeated material-property lookups from
  the OIT path.
- Partition mixed-opacity instance groups into opaque and transparent indirect
  command ranges over shared instance buffers. Retain depth-writing MDI for the
  opaque subset and OIT MDI for the transparent subset. Existing morph/skinning
  restrictions remain.
- Support full queue rebuilds and append-safe updates during progressive scene
  loading.

## Interfaces and Observability

- No LightUSD library API changes. The viewer gains the transparency CLI/config
  option and internal canonical material/submission structures.
- Define optimization statistics as source conversion attempts, preserved
  logical materials, canonical raster payloads, and logical records sharing a
  payload.
- Report opaque/OIT draw items, coalesced ranges, descriptor and pipeline binds,
  opaque/transparent MDI commands, OIT memory, selected mode, and fallback
  reason through existing viewer diagnostics and render reports.

## Test and Acceptance Plan

- Unit-test canonicalization, hash collision equality, signed-zero/non-finite
  handling, MaterialX and alpha-heuristic differences, live edit splitting,
  progressive append, and identity-preserving batching.
- Add headless Vulkan coverage for intersecting/reversed transparent layers,
  opaque occlusion, Mask holes, authored sidedness, mixed-alpha instances,
  transparent native carriers and volumes, picking, and forced fallback.
- Rebuild and validate embedded SPIR-V, then run existing transparency,
  opacity-material, displacement-isolation, scene-safety, stacked-glass,
  shadow-alpha-instance, double-sided, Vulkan render, and backend-parity tests.
- Run the documented NVIDIA PRIME/Xvfb and AMD/RADV Vulkan regressions with
  validation enabled.
- Benchmark a fixed synthetic duplicate-material/transparent-instance scene.
  Acceptance requires no per-frame alpha sort/allocation under OIT, fewer
  canonical GPU materials and state binds, retained mixed-alpha MDI paths, and
  reported CPU/GPU timing with regressions explicitly identified.
