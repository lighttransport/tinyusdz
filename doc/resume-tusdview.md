# tusdview resume and current work state

Last reviewed against `8b922bd03`: 2026-07-20.

The original first-display goal in this document is complete: the progressive
OpenGL path reaches a useful frame in approximately 4.9-5.3 seconds under the
documented Xvfb/Mesa benchmark. The active local work has moved to shared
UsdPreviewSurface/material correctness across `tusdview`, its GL/Vulkan/CUDA/HIP
backends, and `tusdrender`. The active roadmap is now
[`doc/tusdview-tasks.md`](tusdview-tasks.md); this file retains the detailed
audit and performance history needed to resume that work.

## Current audit: material correctness

The reviewed commit had no tracked local edits. Preserve the current untracked
assets and local MCP manifests; they are not evidence of viewer regressions and
are outside the rendering-fidelity roadmap. The material work described below
is committed rather than an uncommitted local patch.

The current material patch spans the next UsdShade schema, Tydra conversion,
shared LightRT/OpenPBR evaluation, viewer mesh/submesh data, all rendering
backends, and `tusdrender`. Its main behaviors are:

- UsdShade binding resolution skips dangling/non-Material targets and continues
  through purpose fallback or ancestor inheritance. `bindMaterialAs` strength
  is taken from the relationship that actually wins.
- `GetInheritedBoundMaterialPathForPurpose()` resolves an exact additional
  purpose such as `back` without changing the ordinary preview/all/full chain.
- Unsupported surface shaders degrade per material instead of collapsing to a
  shared gray default. Conventional PBR constants/textures are recovered from
  the unsupported terminal or Material interface inputs; mesh `displayColor`
  remains visible when no base color can be recovered.
- Front/back purpose bindings are carried as `materialId` and
  `backfaceMaterialId` on each submesh. GL, Vulkan, CUDA, HIP, and CPU
  `tusdrender` select the appropriate material by face orientation.
- PreviewSurface opacity, opacity threshold, specular workflow, IOR, emissive,
  normal, displacement, occlusion, and related OpenPBR fields are being kept
  consistent through the shared evaluation path.
- The Vulkan ray-tracing shader and embedded SPIR-V header changed together;
  regenerate the header whenever `raytrace.comp` changes.

Relevant focused tests and harnesses:

```text
tusdview-material-binding-inheritance
tusdview-degraded-material
tusdview-backface-material
tusdview-double-sided
tusdview-specular-workflow
tusdview-opacity-material
tusdview_lightrt_bridge_test
next_test_schemas
next_test_schemas_ext
next_test_tydra
examples/tusdview/tests/run-degraded-material.sh
examples/tusdview/tests/run-backface-material.sh
tools/tusdrender/tests/run-degraded-material.sh
tools/tusdrender/tests/run-backface-material.sh
```

Post-merge verification on 2026-07-19 found and repaired two P0 regressions
that supersede the earlier parent-commit completion notes:

```text
next_test_schemas, next_test_schemas_ext, next_test_tydra: passed
tusdview_lightrt_bridge_test: passed
tusdview-opacity-material: passed (GL, Vulkan raster/RT, CUDA; HIP unavailable)
tusdview-rt-geomsubset-material: passed (including CUDA next/legacy)
12 focused tusdview/tusdrender material tests: passed
shell syntax and git diff --check: passed
full native CTest after persistent-window hardening: 183/183 passed; 4 optional tests skipped
tusdview-legacy-double-deform: passed, plus 15/15 focused deformation/RT tests
post-hardening diagnostics/CLI regressions: 3/3 passed
MCP persistent render batch + existing MCP/CLI focused tests: 3/3 passed
```

The CUDA/HIP launcher supplied a material-base-color array that the merged
kernel signature omitted, shifting every later argument and producing black
CUDA output. The fix restores the ABI and base-color modulation and applies
front/back material selection to cutout, shadow, and AO traversal. The viewer
now implements deterministic `--view-dir X,Y,Z`; invalid/zero values and
`--camera` ambiguity are rejected. The repaired opacity test also found a real
loader defect: `displayOpacity` had moved to a dedicated tydra-next channel but
the viewer still queried the old generic primvar bag.
`tasks.md` contains the larger usd-assets correctness backlog; it is planning
input, not evidence that every unchecked feature is broken.

The native-suite follow-up fixed the last failure. Interactive asynchronous
legacy loads now apply the same Auto/GPU rest-pose rule and CPU fallback as the
synchronous screenshot path. The test itself now uses the new `--no-grid`
capture option: the raster grid is composited after the RT image without the RT
depth attachment, so it had overwritten the correctly posed mesh and corrupted
the silhouette comparison. The optional HIP, external MaterialX, external
alpha-sort, and usd-assets golden tests still skip when their dependencies/data
are unavailable.

The follow-up batch harness starts one long-lived tusdview MCP process, accepts
path-based or inline USDA cases, polls explicit loading/generation state, changes
resettable render mode/grid/camera-preset parameters, and captures each result
without renderer reinitialization. It runs both a persistent Vulkan/offscreen
batch and, under Xvfb, a persistent real GLFW/OpenGL-window batch. Window and
renderer lifecycle counters are asserted unchanged for every case. Startup-only
differences such as backend, loader, CUDA/HIP/Vulkan RT selection, environment
toggles, and validation-layer configuration intentionally remain separate
process launches.

The back-face material regression is the first existing multi-launch suite
migrated onto this path: its four camera/handedness cases share one process for
each of GL, Vulkan raster, and Vulkan RT. CUDA/HIP retain isolated launches due
to process-final screenshot ownership. On the current machine this reduced the
test from roughly 62 seconds to 40 seconds without weakening its color-class
assertions.

Persistent-batch follow-up (2026-07-19): the general MCP runner now supports
manifest-level repetition, multiple settings/camera/action captures per load,
image-change assertions, expected structured diagnostics, live post-capture
PID/lifecycle checks, and a Linux RSS-growth ceiling. The baseline batch performs
nine replacements while toggling grid overlays, AOVs, and front/back cameras.
A second material batch performs six replacements spanning degraded shader
recovery, GeomSubset bindings, blended opacity, and the opacity/material AOVs.
Both batches pass through one Vulkan/offscreen renderer and one real persistent
GLFW/OpenGL window under Xvfb without changing their lifecycle generations; RSS
settles after initial renderer allocation. This also pins the already-present
Vulkan RT helper overlay path, correcting the older README claim that helper
lines were unsupported. The subsequent rendering-fidelity work replaced the
level-zero RT texture table: ordinary, compressed, and sparse-UDIM mip chains
now use trilinear footprint LOD in Vulkan ray query and projected-triangle LOD
in the shared CUDA/HIP tracer.

Rendering-fidelity follow-up (2026-07-20): raster and RT material evaluation now
share Cook-Torrance GGX, coat, and indirect occlusion semantics; authored camera
filmback, aperture offsets, clipping, orthographic projection, conform policy,
and exposure reach every backend. The default-next loader retains full shared
light records and native Points/BasisCurves/HermiteCurves/NurbsCurves carriers.
OpenGL draws camera-facing point discs and curve ribbons, while Vulkan ray query
and CUDA/HIP trace width-aware solid proxies. Linked raster multi-light
evaluation and the deterministic raster shadow map are now implemented. The
canonical remaining work is in `doc/tusdview-tasks.md`, led by loader-neutral
material/texture parity, Linux visual parity, then omnidirectional raster
shadows for point lights.

The external usd-assets corpus was still unavailable at
`/mnt/disk1/work/usd-assets`, so no new color goldens were generated. The
checked-in manifest/expectation implementation remains ready for the next host
where that public corpus is mounted.

## Active priority — 2026-07-20

`doc/tusdview-tasks.md` is the single source of truth for active work. The
Linux-first order is:

1. **P0:** create one Tydra-owned real-time PBR material record and prove
   default `--next`/legacy extraction equivalence. **Implemented locally:** the
   canonical `RealtimePbrMaterial` packer and a supported-material pixel parity
   check now cover the common path.
2. **P1–P2:** finish the semantic texture grid and prove ordinary/UDIM image
   response through Xvfb OpenGL, Vulkan headless raster, Vulkan RT, and CUDA/HIP
   where a Linux GPU is available. **Hardware evidence now exists:** an RTX 3070
   gives exact default/legacy ordinary-texture Vulkan-raster parity and passes
   isolated UDIM-opacity probes in Vulkan raster, Vulkan RT, and CUDA. Complete
   the remaining semantic-grid and capability-gated HIP coverage before closing
   the release gate. The corrected specular-workflow regression now makes a
   genuine Xvfb GL versus headless Vulkan comparison (rather than selecting
   Vulkan twice); it reports identical RTX 3070 green-highlight dominance.
   The semantic harness now includes a vector normal AOV probe, independent
   OpenPBR coat-normal response, texture-driven occlusion/coat coverage, and
   external-versus-generated-USDZ normal-map parity. It now also covers base,
   metalness, roughness, emission, and opacity ramps for PreviewSurface,
   OpenPBR, and MaterialX standard surface. The default loader passes the
   tested Vulkan-raster scalar routes, including the newly repaired MaterialX
   `specular_roughness` route. After making the generated USDA strict-parser
   compliant and repairing mixed MaterialX/UsdUVTexture plus coat-normal
   conversion, PreviewSurface, OpenPBR, standard-surface scalar AOVs, and the
   coat-normal, coat-weight, coat-color, and coat-roughness AOVs agree exactly
   between default and legacy loaders (MAD 0.0). The dedicated coat channels
   also pass Xvfb OpenGL, Vulkan ray query, and CUDA; HIP remains
   capability-gated on this NVIDIA-only host.
   The same matrix now includes evaluated `specular-f0` and `ior-f0` AOVs. It found that
   the legacy adapter omitted PreviewSurface's `useSpecularWorkflow` flag;
   after copying that field into the draw material, GL, Vulkan raster/ray
   query, and CUDA all give exact default/legacy specular-texture pixels. IOR
   fixtures now cover PreviewSurface, OpenPBR, and MaterialX Standard Surface
   at two authored values. OpenPBR/Standard specular-color fixtures also pin
   their distinct model: the color textures tint dielectric F0 derived from
   `specular_ior`/`specular_IOR`, while PreviewSurface's explicit specular
   workflow treats `specularColor` as F0 directly. All four available backends
   pass both loader paths with exact default/legacy image parity (MAD 0.0).
   Follow-up two-tile UDIM fixtures found that Vulkan raster still treated the
   newer specular-color slot as ordinary-2D-only. A dedicated descriptor-array
   binding and atlas-row route now preserve OpenPBR/Standard specular F0 across
   tiles 1001 and 1002. Vulkan raster, Vulkan ray query, and CUDA pass the UDIM
   fixtures through both loaders; HIP remains capability-gated.
3. **P3:** complete for point/zero-radius SphereLight emitters: GL and Vulkan
   raster render/sample all six projected-depth cube faces, and the Vulkan
   regression proves the bounded shadow. Keep finite non-zero area sampling as
   representative-point behavior until a later area-light task.
4. **P4–P6:** retain evaluated graph inputs for advanced lobes, then address
   Vulkan non-mesh/picking and broader area-light, camera, and external-corpus
   work.

Advanced OpenPBR/MaterialX lobes remain path-qualified degraded behavior until
the core material record and its cross-backend image parity are complete.

## Prioritized tusdview backlog — historical 2026-07-19 snapshot

This section preserves the earlier completion record. It is not the current
backlog; use `doc/tusdview-tasks.md` for prioritization.

- **P0**: finish and prove the current material patch before starting another
  feature area.
- **P1**: establish visual-regression evidence and diagnostics needed to judge
  later work.
- **P2**: highest-impact usd-assets fidelity gaps.
- **P3**: broader scene and renderer parity after the material baseline is
  trustworthy.

### P0 — close the current UsdPreviewSurface/binding patch

- [x] Reconcile the shared CUDA/HIP kernel ABI with both launchers, restore
  base-color modulation, and use the selected front/back material consistently
  for primary, cutout, shadow, AO, and material-ID paths.
- [x] Implement and document deterministic `--view-dir X,Y,Z` camera control;
  reject invalid vectors and ambiguity with `--camera`.
- [x] Repair opacity/cutout assertions so raster blend, single-hit RT blend,
  and masked any-hit behavior each test the behavior they actually implement.
- [x] Review front/back material selection in GL, Vulkan, CUDA, HIP, and CPU
  LightRT for a single geometric-front-face convention, including mirrored or
  negative-scale transforms.
- [x] Verify opaque, masked, and blended back materials enter the correct pass;
  test a front/back binding combined with GeomSubset material assignment.
- [x] Visually inspect the degraded-material and backface fixtures, not only
  their script exit status. Confirm an unsupported material never drops its
  mesh and recovered constants/interface inputs remain distinguishable.
- [x] Run the full focused material suite, schema/Tydra tests, and the relevant
  GL/Vulkan/RT screenshot paths after the diff settles. Regenerate and verify
  embedded Vulkan SPIR-V if its source shader changes.
- [x] Remove any accidental type-alias/formatting debris and ensure the shared
  LightRT/OpenPBR data boundary remains owned by Tydra rather than duplicated by
  viewer backends.

Already present and therefore validation work rather than open implementation:
per-material degraded PreviewSurface fallback; valid-target binding fallback;
exact-purpose `back` binding resolution; explicit backface material IDs;
UsdPreviewSurface constant/texture opacity and opacity threshold; specular
workflow tests; double-sided tests; and degraded/front-back test harnesses.

Pre-merge parent result (superseded by the post-merge audit above): GL, Vulkan
raster, and Vulkan ray query passed
whole-mesh and GeomSubset front/back bindings in normal and mirrored transforms;
the back material uses degraded recovery plus blended opacity. CUDA/HIP side
selection was source-reviewed and builds, but runtime execution was unavailable
on that run. The parent audit also fixed two uncovered Vulkan raster regressions:
stale embedded shaders, and displayColor loss/black geometric-normal meshes.
Raster displayColor now uses a normal vertex binding while RT retains its device
address, and shader headers were regenerated from their GLSL sources.

### P1 — make usd-assets results measurable

- [x] Audit and extend `tests/tusdview/run-usd-assets-batch.sh` and
  `examples/tusdview/tests/usd-assets-goldens.tsv`. The checked-in harness and
  `tusdview-usd-assets-golden` target already exist, so do not create a second
  runner. Ensure it distinguishes load errors, timeouts, backend errors,
  no-renderable files, rendered-with-warnings, and golden mismatches.
- [x] Curate visual coverage for MaterialXTest,
  TextureCoordinateTestMaterialX, StandardShaderBall, Teapot,
  AlphaBlendModeTest, AlphaBlendSortTest, representative USDZ skinning, and
  primitive/schema fixtures. Record per-asset tolerance and expected warnings.
- [x] Add a concise structured warning summary for unsupported MaterialX nodes,
  missing textures, degraded materials, and skipped/unresolved instancers.
  Unsupported node diagnostics must include actionable prim/node paths.
- [x] Make a previously supported material degrading to fallback a visual test
  failure while allowing explicitly catalogued unsupported materials to render
  with expected warnings.

P1 completion notes (2026-07-19): the single shared runner now accepts a
per-asset expectations TSV with accepted statuses, required warning regexes, and
per-asset golden tolerances. Results retain their compatible TSV columns, add a
compact structured-diagnostics column, and mirror it into JSON. An asset with an
existing golden that unexpectedly degrades becomes `unexpected_degradation`;
status/warning disagreement becomes `expectation_mismatch`. Both are hard
failures in the tusdview wrapper/golden CTest. The curated manifest covers the
priority MaterialX, transparency, StandardShaderBall, Teapot, skinning, and
primvar assets. `tusdview-usd-assets-classification` proves the classification
and expectation-failure behavior without external data. The external corpus was
not mounted at its documented path during this run, so the opt-in full golden
sweep remains the next machine/data-dependent validation rather than an open P1
implementation item.

Post-merge follow-up: the shared material carrier now exposes typed diagnostics
for unsupported shaders, unsupported MaterialX nodes, and degraded materials,
including material/node paths and shader IDs. `tusdview` and `tusdrender` feed
those diagnostics into their structured load summaries and bounded examples;
their degraded-material regressions require the shader path and ID so this
cannot silently collapse back to a generic warning. A renderer-independent
`tusdview-cli-camera-options` regression covers malformed, zero, non-finite,
and authored-camera-conflicting `--view-dir` values, plus help exposure for
`--view-dir` and `--no-grid`.
The external runner now accepts `gl-raster` and `hip-rt` in addition to its
existing modes. No new color baseline was generated because the external
usd-assets corpus was not mounted; do not treat the checked-in coverage
fingerprints as newly validated color parity.

### P2 — material, texture, and transparency fidelity

- [x] Complete `ND_standard_surface_surfaceshader` evaluation for base color,
  specular/color, metalness, roughness, coat/roughness, emission/color,
  opacity, normal, and displacement. Do not count name-based degraded recovery
  as full node implementation.
- [x] Complete MaterialX NodeGraph traversal for image, texcoord, place2d/tiling,
  arithmetic, extract/combine/convert, channel selection, and typed-output
  conversion. Resolve interface inputs on Material, Shader, and NodeGraph prims
  without `TODO: ShaderNode` noise.
- [x] Unify MaterialX filename, geompropvalue, UDIM/tile, and asset anchoring for
  filesystem and USDZ-contained assets.
- [x] Honor MaterialX color-management metadata, including source color spaces,
  Raw versus sRGB scalar maps, and ACEScg content used by StandardShaderBall.
- [x] Finish texture wrap modes (repeat/periodic, clamp, mirror,
  black/constant), UV transforms (scale, bias, rotation, translation,
  real-world tile size, MaterialX uvtiling), and packed-channel sampling for
  ORM, opacity, roughness/metallic, and normals.
- [x] Verify identical external/USDZ texture behavior and cover CMYK JPEG,
  grayscale JPEG/PNG, 16/32-bit PNG, and EXR when enabled.
- [x] Implement deterministic translucent ordering for tusdview raster
  (`AlphaBlendModeTest` and `AlphaBlendSortTest`). Constant/texture opacity and
  alpha cutout exist, but general blend ordering remains a separate problem.
- [x] Continue converging tusdview and tusdrender on the shared material
  evaluator; compare tusdview GL/Vulkan/RT images with tusdrender `-rtPreview`
  and Vulkan modes on the curated subset.

P2 completion notes (2026-07-19): the audit confirmed that most of this list
described code already present in the dirty worktree: node-aware constant
folding and texture traversal, interface and non-local NodeGraph forwarding,
geomprop-driven UV selection, filesystem/USDZ/UDIM resolution, color-space and
wrap metadata, packed-channel sampling, and deterministic GL/Vulkan translucent
sorting. Focused MaterialX, USDZ equivalence, UDIM, CMYK, opacity, transparency,
and LightRT bridge tests pass. The remaining standard_surface gap was real:
displacement was discarded by the next OpenPBR carrier. It is now retained and
fed into tusdview's existing displacement path. The same change fixed the
shared LightRT conversion mistakenly taking specular roughness from base
roughness. `TestStandardSurfaceNoRecursion` now verifies the requested base,
specular, metalness, roughness, coat, emission, opacity, normal, and displacement
fields. The checked-in usd-assets goldens cover grayscale and 16/32-bit PNG
variants; the external corpus itself was not mounted, so those pre-existing
goldens could not be regenerated in this session.

### P3 — broader usd-assets scene fidelity

- [x] Validate subdivision rendering and refinement controls for Catmull-Clark,
  Loop, bilinear, and creases. A `subdivision-schemes` regression exists; extend
  it to visual parity instead of assuming the original task is wholly open.
- [x] Verify constant, uniform, varying, vertex, and faceVarying primvars and all
  numeric point/normal types used by the schema fixtures.
- [x] Validate orientation, doubleSided, and extent fallbacks without noisy
  missing-attribute warnings, plus analytic sphere/cube/cone/cylinder/capsule
  parity against converted meshes.
- [x] Fix any remaining composed-scene PointInstancer prototype failures; add
  payload/reference/sublayer visual cases and preserve per-instance primvars
  and material overrides. Catalogue intentional no-renderable fixtures without
  misclassifying composed parents with hidden payloads.
- [x] Verify DomeLight/environment color space, intensity, and orientation;
  authored/default camera framing; autoframe/headless parity; emissive geometry,
  environment contribution, shadows, and purpose filtering.
- [x] Add representative-time visual checks for animated transforms, points,
  normals, meshes, blendshapes, and skinning, including AnimatedCube,
  AnimatedTriangle, BoxAnimated, InterpolationTest, CesiumMan, RiggedFigure,
  and RiggedSimple across raster and ray-traced paths.

P3 completion notes (2026-07-19): existing converter tests cover all mesh
interpolation modes, face-varying seams/tangents, half/numeric widening,
orientation and analytic primitive axes, extent generation, instancer validation
and nested visibility, light linking, dome format, and camera extraction. The
visual/runtime matrix passed subdivision schemes, double-sided culling,
PointInstancer visibility/orientation, purpose filtering, camera modes,
DomeLight format and textureless fallback, transform/point/normal/blendshape
time codes, morphing, skinning, and raster/RT deformable instancing. The first
skinning screenshot run failed because sandboxed Xvfb could not open its chosen
display; the identical test passed outside the sandbox. The opt-in named
usd-assets golden sweep remains data-dependent (the corpus was not mounted),
but its curated P1 manifest contains the requested representative assets and
times, so this is an external validation limitation rather than remaining P3
implementation work.

## Completed first-display work (historical context)

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

## Archived `resume.md`: Moana Island tusdrender track

These notes are retained for context after merging the repository-root
`resume.md` into this canonical handoff. They predate the current material work;
their old branch/commit-count and clean-worktree statements are obsolete.

Build and smoke-test commands:

```bash
cmake --build build_ninja --target tusdrender -j16
BIN=build_ninja/tools/tusdrender/tusdrender
python3 tools/tusdrender/check_tusdrender_smoke.py "$BIN" . /tmp/tusd_smoke
python3 tools/tusdrender/bench_island.py --out /tmp/island_bench
```

Island data is under `/mnt/disk1/data/island/usd/`. The full-scene reference
command was:

```bash
$BIN /mnt/disk1/data/island/usd/island.usda /tmp/i.png -rtPreview -stats \
  -w 320 -height 180 -camera shotCam -maxMem 44
```

Completed work included PointInstancer expansion; curve ray tracing and curve
prototype instancing; the compact isCoral triangle/material representation;
compact 3x4 instance transforms; material-resolution caching; constant and
per-vertex display color/opacity; authored smooth normals; finite lights; the
isBeach TLAS serial-loop parallelization; and removal of the isIronwoodA1
prototype-holder double count.

Recorded 320x180 results were approximately:

```text
isCoral       87.5M triangles, 12.8 s, 4.85 GB
isBeach       4.09B visible / 63K unique triangles, ~25 s, 7.5 GB
full island   5.69B triangles, 1m33s, 25.9 GB
```

Remaining Island-specific work:

1. The isCoral gap to Embree is architectural: crate decode and LightRT's BVH
   builder/cache layout remain slower and larger. The easy 4-byte-per-triangle
   footprint lever has already been used.
2. Instanced curve prototypes are always built as round hair. Authored
   normal-driven flat/ribbon curves are not separated per prototype.
3. Refresh the per-element table in `doc/island-benchmark.md` with
   `bench_island.py`; headline isCoral/isBeach/full-island numbers were current,
   but some small-element rows predated the final footprint and display-color
   changes.

Keep this Island work secondary to the active material-correctness patch unless
the user explicitly switches focus back to renderer performance.
