# Resume prompt: reduce tusdview OpenGL first display to ~6 seconds

Continue optimizing `tusdview` OpenGL first-display performance and peak RSS for
the payload-heavy hotel scene. The practical target is OpenUSD `usdview`, which
the user measures at approximately 6 seconds to an interactive OpenGL display.
Do not stop at process-exit comparisons: measure time until the first useful
rendered image.

## Repository and constraints

- Repository: `/mnt/nvme02/work/tinyusdz-repo/tusdview`
- Read and follow the repository `AGENTS.md` before editing.
- Preserve all unrelated dirty-worktree changes and untracked files.
- Use `apply_patch` for edits.
- Prefer `build_ninja/` for new native configurations, but the existing
  Release viewer benchmark binary is currently `build/tusdview`.
- Do not use environment variables for performance controls or timing. Add and
  use explicit program arguments.
- Do not commit or push unless the user explicitly requests it.

## Additional local work retained across the pull

Status as of 2026-07-18. The `--next` primvars / texturing / texture-VRAM
workstream is **done and pushed**, including GPU skinning (raster, both backends,
instanced prototypes included) and the large-scene verification (numbers in
[large-scene.md §2.9](large-scene.md)).

Done locally on 2026-07-18: Vulkan/CUDA/HIP ray tracing now consumes the same
six semantic material slots as raster (base color, metallic, roughness, normal,
emissive, and opacity). The shared table carries UV-set selection and transforms,
sRGB decode, compressed-only sources, and sparse UDIMs; alpha-mask texels are
rejected during traversal. Vulkan normal mapping constructs its TBN from the
selected, transformed UVs. Per-face GeomSubset bindings resolve the committed
triangle's material rather than falling back to a mesh-wide first material.
The ray-query SPIR-V is regenerated with the final `RayQueryKHR` capability;
the shader generator now runs `spirv-val` when available so an old glslang
cannot silently emit provisional-capability/final-opcode output again.
The implementation is covered by bridge/unit tests and the headless opacity and
RT GeomSubset harnesses for both loaders and all available RT backends.

Also done locally on 2026-07-18: deferred payloads inside USDZ archives retain
their package backing across the asynchronous viewer recomposition handoff in
both legacy and next loaders. The legacy and next USDZ readers now enforce the
first physical entry as the root, recognize neutral `.usd` roots by payload
magic, reject unsafe or duplicate package paths, compressed entries, and
truncated trailing local headers, and clear reused output state after failure.
The legacy validator uses the same exact extension/content classifier. A real
two-entry stored ZIP test drives the viewer's MCP `load_payload` action through
defer, load, unload, and reload.

Sanitizer follow-up is also complete locally on 2026-07-18. The no-RTTI build
now disables UBSan's RTTI-dependent `vptr` check, so the sanitized unit binary
links while retaining the other undefined-behavior checks. The full 1,004-test
unit binary exposed and now pins two project-owned zero-length `memcpy` cases:
empty binary array time samples and an empty reconstructed normals array. The
time-sample insertion path additionally rejects null non-empty input, counts
that cannot fit its `uint32_t` bookkeeping, and byte-buffer overflow before it
adds partial sample state. Its public pointer overload also validates the
incoming element type, preventing a later sample from changing the element size
and reinterpreting an existing flat buffer. Vendored codec diagnostics are
scoped narrowly:
fpng selects its portable unaligned-I/O path, while only the stb resize/write
implementation TUs suppress the specific `alignment`/`shift` checks their
upstream code intentionally triggers. ASan and all other applicable UBSan
checks stay enabled. Verification:

```bash
cmake -S . -B build_asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=OFF \
  -DSANITIZE_ADDRESS=ON -DSANITIZE_UNDEFINED=ON
cmake --build build_asan --target unit-test-tinyusdz -j16
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ./build_asan/unit-test-tinyusdz
```

This passes all 1,004 tests with no sanitizer diagnostics. Leak detection is
disabled only because LeakSanitizer cannot run in the ptrace-restricted agent
sandbox; address checking remains active. The normal `build_ninja` unit CTest
also passes after the same changes.

The full normal Ninja build and all 174 registered CTests are accounted for and
green (passes or declared capability skips). `web-validation-parity-test`
passes when run outside the restricted agent sandbox; inside it, Node receives
`EPERM` when it tries to spawn the local `tusdcat`, before any parity assertion.
The Vulkan-RT Python gates now share a `vulkaninfo` preflight that recognizes an
all-CPU Vulkan installation (llvmpipe here) even when a hardware kernel module
is loaded. BLAS compaction, RT skinning/refit, blendshape RT, deform RT, and
legacy deform RT therefore capability-skip promptly instead of paying 6-15
minute subprocess timeouts. Raster deform/blendshape gates use a test-local
320x320 config instead of inheriting the interactive saved window size; the
five shared deform comparisons dropped from roughly two minutes to 45 seconds
while retaining their image assertions. The affected subprocesses and CTests
also have explicit upper bounds, so a wedged non-CPU device cannot stall the
suite indefinitely.

The earlier raster-opacity work remains intact: separate opacity textures,
including UDIM masks, are sampled by both raster backends after consolidating
UDIM lookup maps into one scene-wide atlas. Varying `primvars:displayOpacity` is
preserved through batching and multiplied in raster and RT. The GL and Vulkan
opacity AOVs report the same composed raster opacity (material/base alpha,
separate mask, and per-vertex or per-instance `displayOpacity`). Point-instanced
prototypes retain their per-vertex opacity, and both raster backends classify
those batches as translucent before drawing.

## Benchmark asset and commands

Asset:

```text
/mnt/disk1/data/caldera/map_source/prefabs/br/wz_vg/mp_wz_island/commercial/hotel_01.usd
```

Current reproducible tusdview benchmark:

```bash
xvfb-run -a sh -c '/usr/bin/time -v build/tusdview \
  --backend gl \
  --large-scene-profile island \
  --load-payloads \
  --compose-threads 8 \
  --convert-threads 8 \
  --timing \
  --frames 1 \
  --size 1280x720 \
  /mnt/disk1/data/caldera/map_source/prefabs/br/wz_vg/mp_wz_island/commercial/hotel_01.usd'
```

OpenUSD comparison binary:

```text
../OpenUSD/dist/bin/usdview
```

An automated OpenUSD run may require:

```bash
xvfb-run -a sh -c 'PYTHONPATH=../OpenUSD/dist/lib/python \
  LD_LIBRARY_PATH=../OpenUSD/dist/lib \
  ../OpenUSD/dist/bin/usdview --renderer Storm --timing --quitAfterStartup \
  /mnt/disk1/data/caldera/map_source/prefabs/br/wz_vg/mp_wz_island/commercial/hotel_01.usd'
```

This automated OpenUSD invocation previously reported about 11.4 seconds to
first image and 28.7 seconds to exit under Xvfb/Mesa llvmpipe, but the user's
real OpenGL display measurement is about 6 seconds and is the target. Establish
a directly comparable first-useful-frame metric for tusdview rather than using
shutdown time.

## Current measured tusdview state

Latest representative result:

```text
compose                              3.34 s
point-instancer extraction           0.06 s
native-instance/render extraction    1.27 s
mesh conversion/flatten/batching     3.27 s
finalize                             0.12 s
loader total                         8.10 s
end-to-end --frames 1               12.60 s
peak RSS                             3,319,652 KiB (~3.32 GB)
```

Scene result:

```text
35,375 source meshes
596 draw meshes
12,111 instances
6,332,592 unique triangles
14,264,594 effective triangles
6,136,519 flattened vertices
```

The earlier baseline was approximately 13.1 seconds end-to-end and 3.46 GB
peak RSS. Before fixing quadratic batching, it was approximately 38.9 seconds.

## Changes already implemented

Inspect the dirty diff before modifying anything. Relevant existing work
includes:

- Fixed an O(N^2) scan of all accumulated `sourceFaceId` values during static
  batching by adding a per-batch `nextFaceId` counter.
- Corrected source-face ID uniqueness in multi-material batches.
- Added bounded parallel mesh conversion and moved flattening/world transforms
  into conversion workers.
- Added bulk movement of plain static vertex buffers into batches.
- Skipped skin/blendshape stage queries for meshes without those features.
- Cached inherited world transform, purpose, and material path in
  `RenderPrimRecord`.
- Activated `ParallelWarmSources`; it existed in
  `src/next/pcp/cache-parallel-warm.inc` but was not called by `BuildStage`.
- Batched payload load-rule invalidation and added payload progress reporting.
- Added explicit CLI controls:
  - `--compose-threads N`
  - `--convert-threads N`
  - `--upload-budget-ms N`
  - `--timing`
- Removed the next loader's timing/composition-thread environment controls.
- Reduced conversion staging from the original 256 mesh/~256 MiB wave to a
  128 mesh/~128 MiB wave. A 64 MiB wave reduced peak RSS to about 3.20 GB but
  added roughly 0.8 seconds due to repeated thread creation.
- Unified render-mesh/native-instance extraction into one
  `CollectRenderPrims` pass and removed a redundant per-instance prototype
  subtree walk.

Relevant files include:

```text
examples/tusdview/main.cc
examples/tusdview/app.cc
examples/tusdview/app.hh
examples/tusdview/scene_loader.hh
examples/tusdview/next_scene_loader.cc
examples/tusdview/gl/gl_renderer.cc
examples/tusdview/gpu_scene.hh
src/next/pcp/cache-parallel-warm.inc
src/next/tinyusdz-next.cc
src/tydra/next/render-extract.hh
src/tydra/next/render-extract.cc
```

## Primary conclusion

The current pipeline blocks the first frame until it has fully composed,
converted, flattened, batched, and uploaded the complete six-million-triangle
scene. Micro-optimizations alone are unlikely to reach ~6 seconds. The main
attack should be first-useful-frame latency through pipelining/progressive
display, while continuing background conversion and upload.

## Recommended attack plan

1. Add an explicit first-useful-frame metric.

   - Record timestamps for process start, stage ready, first geometry submitted,
     first non-empty frame presented, full conversion complete, and full upload
     complete.
   - Print them under `--timing`.
   - Define “first useful” consistently, for example: camera framed and at least
     the initial visible geometry batch rendered.
   - Avoid treating process exit or full-scene completion as first display.

2. Stream conversion output directly to the render/upload queue.

   - Replace the blocking `LoadUSDViaNext -> complete DrawScene -> uploadScene`
     sequence for interactive OpenGL with a producer/consumer pipeline.
   - Keep composition on the load worker.
   - Convert bounded chunks using persistent workers rather than creating and
     joining threads for every wave.
   - Flush completed static batches incrementally to a thread-safe queue.
   - Upload ready batches on the GL/context thread and present as soon as the
     first useful set is available.
   - Continue conversion and upload in the background while keeping the UI
     responsive and progress percentages accurate.
   - Preserve the synchronous path for deterministic `--frames` screenshots,
     or add an explicit wait-for-complete mode so tests remain deterministic.

3. Prioritize visible/large geometry.

   - Once preliminary bounds and camera are available, prioritize meshes or
     prototype batches by projected size and frustum visibility.
   - Emit PointInstancer/native-instance prototype geometry early because a
     small number of prototype uploads can reveal much of the scene.
   - Consider a coarse initial representation only when it is authored USD
     proxy-purpose geometry. Do not reintroduce synthetic cube placeholders.

4. Remove repeated whole-stage work.

   - Current combined native/render extraction still costs about 1.27 seconds.
   - Profile `CollectRenderPrims`, `GatherMeshPrims`, prototype emission, and
     material binding resolution separately.
   - Avoid traversing shared prototype subtrees per instance.
   - Reuse `RenderExtractResult.native_instances`, cached world matrices, and
     prototype mesh lists everywhere possible.
   - Investigate folding PointInstancer discovery into the same inherited-state
     extraction pass.

5. Lower peak RSS without serializing conversion.

   - Implement persistent conversion workers with a bounded task/result queue;
     this should retain the ~64 MiB staging memory result without paying for
     hundreds of thread start/join waves.
   - Release each `RenderMesh`, flattened local mesh, and vertex-to-point remap
     immediately after its data enters a batch.
   - For static meshes, retain only metadata needed after flattening rather than
     the full `RenderMesh` topology and attribute arrays.
   - Measure PCP layer/stage ownership after `CacheRetention::LayersOnly` and
     verify `TrimTransientCaches()` actually releases allocator memory.
   - Distinguish CPU RSS from Mesa llvmpipe's software “GPU” allocations.

6. Defer nonessential OpenGL auxiliary uploads.

   - The initial surface frame does not need wireframe EBOs, source-face-ID
     texture buffers, selection buffers, or other visualization-only data.
   - Upload VBO/EBO data needed for surface rendering first.
   - Lazily upload wireframe/source-face buffers when their mode/AOV is first
     requested, or upload them in low-priority background slices after the first
     frame.
   - Preserve authored pre-triangulation wireframe behavior and always-on-top
     depth bias when wireframe mode is enabled.
   - Ensure CPU source data remains available until deferred auxiliary upload is
     complete, then free it.

7. Optimize composition only after validating the pipeline gain.

   - Current PCP breakdown with eight threads is approximately:

     ```text
     ParallelWarmSources workers ~1.33-1.38 s
     warm merge                  ~0.17 s
     BuildStage source hits      ~0.04 s
     opinion composition         ~0.58-0.63 s
     instance registration       ~0.04 s
     total compose               ~3.25-3.34 s
     ```

   - Eight composition workers performed better than sixteen on this machine.
   - Profile root layer parsing/opening and worker cache merge/duplication.
   - Do not sacrifice correctness or payload/variant recomposition support for a
     benchmark-only cache drop.

## Correctness requirements

Do not regress the fixes already made:

- Payload loading and `M of N` percentage progress.
- Correct USD purpose inheritance and display controls.
- No synthetic cubes for unloaded/rejected payload geometry.
- Concave n-gon earcut triangulation.
- Wireframe from authored polygon perimeters, not triangulation diagonals.
- Dark-green wireframe independent of light color.
- Wireframe rendered on top without surface penetration.
- Material binding inheritance and multi-material source-face mapping.
- Native and PointInstancer prototype placement/counts.

## Validation

After each meaningful performance change:

```bash
cmake --build build -j16 --target tusdview
ctest --test-dir build-next \
  -R 'next_test_pcp|next_test_load_rules|next_test_tydra' \
  --output-on-failure
git diff --check
```

Also run relevant tusdview render/purpose/wireframe tests if those paths change.
Record at minimum:

```text
first useful frame
full scene ready
end-to-end wall time
peak RSS
user CPU time
system CPU time
mesh/draw/triangle counts
```

Run benchmarks more than once and report representative or median values.
Always include the exact command and whether rendering used real GPU OpenGL or
Mesa llvmpipe under Xvfb.

## Desired outcome

Deliver an interactive OpenGL path that presents useful scene geometry near the
user's ~6 second `usdview` target, then finishes the full-resolution scene in the
background. Reduce peak RSS materially below the current ~3.32 GB, keep the
synchronous test/screenshot path deterministic, and provide evidence from
repeatable CLI-only benchmarks.

## 2026-07-18 implementation result

The interactive, non-threaded OpenGL next-loader now uses a bounded progressive
producer/consumer stream. Conversion workers publish material/texture snapshots
and completed meshes while conversion continues; the context-owning main thread
performs incremental GPU uploads. Prototype groups with the greatest instance
coverage are converted first. Surface buffers are sufficient for the first
frame, while source-face and wireframe buffers are uploaded afterward (or
eagerly when one of those display modes is requested). The conversion pool is
persistent across bounded chunks, avoiding repeated thread creation.

The synchronous `--frames` and non-OpenGL paths retain the all-at-once behavior.
Two explicit diagnostic controls were added:

```text
--stream-buffer-mb N       bounded progressive CPU geometry queue (default 64)
--quit-after-full-upload   exit once progressive conversion/upload is complete
```

The completion flag is intentionally restricted to interactive, non-threaded
OpenGL with the next loader and cannot be combined with `--frames`.

Representative benchmark, using Mesa llvmpipe under Xvfb:

```bash
xvfb-run -a sh -c '/usr/bin/time -v build/tusdview \
  --backend gl --large-scene-profile island --load-payloads \
  --compose-threads 8 --convert-threads 8 --timing \
  --quit-after-full-upload --size 1280x720 \
  /mnt/disk1/data/caldera/map_source/prefabs/br/wz_vg/mp_wz_island/commercial/hotel_01.usd'
```

```text
first useful frame             5.274 s
full CPU conversion            9.863 s
full GPU upload/presentation  12.238 s
end-to-end wall time          13.30 s
peak RSS                       3,306,412 KiB
draws / instances              596 / 12,111
unique / effective triangles   6,332,592 / 14,264,594
```

Repeated first-useful measurements were approximately 4.9-5.3 seconds. This
meets the requested near-six-second display target. Peak process RSS is only
modestly below the previous ~3.31-3.32 GiB measurement because this benchmark
uses software rendering, so Mesa's CPU-backed OpenGL allocations are included
in RSS; the progressive queue itself is bounded to 64 MiB by default.
