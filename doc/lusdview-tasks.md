# lusdview / lusdrender active tasks

This file tracks unfinished renderer work only. Completed implementation history,
old audit notes, machine-specific run logs, and superseded plans belong in Git
history and are intentionally omitted here.

Updated 2026-08-31.

## Build performance

### Reduce cold MaterialX graph compilation time

The large MaterialX graph compiler was separated from
`lightrt_mtlx_bridge.cc` into `lightrt_mtlx_graph_compile.cc`. Release and
RelWithDebInfo builds currently compile that source at `-O1` on GCC and Clang to
avoid pathological GCC IPA/PTA optimization time.

Measured Linux Release baseline:

- Original combined translation unit: about 181 seconds.
- Split graph translation unit at `-O2`: about 148 seconds.
- Split graph translation unit at source-local `-O1`: about 71 seconds.
- Incremental bridge edit after the split: about 8 seconds.
- Incremental flat GPU packing edit after the split: about 1 second.
- Final executable link: below one second with both GNU BFD and LLD; linking is
  not the remaining bottleneck.

Future work:

- [ ] Reduce a cold graph-compiler build substantially below the current
  approximately 71-second baseline without changing graph behavior.
- [ ] Break `CompileMaterialXGraphRuntime` into independently compiled,
  testable phases such as JSON indexing, closure lowering, node lowering, and
  output packing. Merely moving the existing giant function to another file
  does not reduce its internal optimizer cost.
- [ ] Replace allocation-heavy C++ machinery in flat numeric or ABI-oriented
  phases with narrow C11-compatible data/functions where profiling shows a
  compile-time or runtime benefit. Keep JSON parsing and C++ scene ownership at
  the C++ boundary; do not introduce a C object solely for trivial copy helpers.
- [ ] Re-measure GCC and Clang Release and RelWithDebInfo builds with ccache
  disabled. Record per-object wall time and peak memory from Ninja's log and
  `/usr/bin/time -v`.
- [ ] Re-evaluate the source-local `-O1` override after decomposition. Restore
  the normal target optimization level if cold compilation remains bounded.

Acceptance criteria:

- MaterialX bridge, graph-connection, graph-evaluation, and lusdrender CPU graph
  tests remain green.
- Generated graph records and rendered reference pixels remain unchanged.
- Ordinary edits to Vulkan rendering, bridge orchestration, or GPU packing do
  not rebuild the graph compiler.
- Documentation reports compilation and linking separately; abbreviated Ninja
  progress output is not treated as timing evidence.

## Incremental scene updates

- [ ] Replace lusdview's post-edit full `DrawScene` rebuild with a DrawScene/GPU
  `SceneUpdateSink`. Retain the streaming converter for initial loads and very
  large geometry.
- [ ] Preserve stable renderer resource IDs across payload, variant, and
  dependency-layer edits, with transactional rollback on failed updates.
- [ ] Add focused tests proving unchanged geometry, materials, and textures are
  neither reconverted nor re-uploaded.

## Camera completeness

- [ ] Add thin-lens depth of field to GL and Vulkan raster. Vulkan ray query and
  the shared CUDA/HIP tracer already support it.
- [ ] Implement shutter/motion blur consistently across raster and RT backends.
- [ ] Implement stereo rendering and arbitrary authored clipping planes.
- [ ] Keep default and legacy loader camera records equivalent and extend image
  regressions for every newly supported path.

## Lighting completeness

- [ ] Implement finite Sphere, Disk, Rect, and Cylinder area-light sampling in
  GL and Vulkan raster. RT backends already sample these emitters.
- [ ] Implement dedicated GeometryLight, PortalLight, IES, and emissive-mesh
  paths. Until implemented, retain structured path-qualified diagnostics rather
  than silently approximating them.
- [ ] Extend deterministic cross-backend image tests for new lighting paths,
  including light and shadow collection membership.

## Verification

Prefer the Ninja build tree and run the focused renderer gates before the full
native suite:

```sh
cmake -S . -B build_ninja -G Ninja \
  -DLIGHTUSD_BUILD_TESTS=ON -DLIGHTUSD_BUILD_GUI_VIEWER=ON
cmake --build build_ninja -j16
ctest --test-dir build_ninja -R 'lusdview|tool-lusdrender' --output-on-failure
```

GPU-dependent tests require a usable hardware device and the documented
environment in `doc/lusdview.md`. A missing backend or external corpus is an
explicit skip, not proof of rendering parity.
