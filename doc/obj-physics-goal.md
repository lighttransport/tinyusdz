# Goal prompt: OBJ and physics support across native and Python

Continue improving LightUSD's OBJ, URDF and MJCF import, USD physics authoring,
and native C++/C/Python interoperability. Deliver working implementations and
tests in priority order. Use the shared native implementation for Python;
avoid implementing a second conversion or validation engine in Python.

## Constraints

- Preserve existing defaults, supported input behavior and stable C ABI.
- Keep the C++17 core dependency-free and exception-free. MuJoCo, OpenUSD,
  Newton and geometry-cooking integrations remain optional adapters or tests.
- Preserve authored data by default. Normalization, pruning, inferred physics,
  approximation and unit conversion must be explicit and reported.
- Respect another agent's edits. Use isolated Ninja trees under
  `build_ninja/obj-physics-*` or the existing private `obj-large-*` trees.
- Generate stress OBJ/MTL inputs in RAM; do not commit large assets or build
  artifacts. Leave changes uncommitted until the user requests a commit.
- Keep this prompt's progress and evidence current. Do not mark the overall
  goal complete while required roadmap work remains.

## Priority 1: shared import resources and physics API parity

1. **Deduplicate textures.** Resolve/cache texture requests within an import;
   retain one immutable byte payload per resolved identifier across material
   maps, imported layers, C/Python stages and USDZ packaging. Preserve source
   anchors, authoritative resolvers, error propagation and memory accounting.
   Deduplicate package entries without losing repeated references or aliases.
2. **Structured import reports and options.** Provide shared diagnostics with
   stable codes, severity, source/prim locations, imported/approximated/skipped
   features, dependencies, timings and memory estimates. Expose independent
   input/memory/depth limits and an unsupported-feature policy in Python while
   preserving existing calls and C options compatibility.
3. **Complete typed physics access and validation.** Expose authored UsdPhysics,
   `mjc:*` and `newton:*` data through a shared C++/C query layer. Use it for
   Python inspection and validation, including efficient owned array views.
   Generate Python schema names, property types and variability from native
   descriptors instead of maintaining independent hard-coded registries.

## Priority 2: fidelity and larger assets

4. **Mass/inertia policies.** Preserve authored values, optionally infer
   supported analytic properties, and support an optional MuJoCo compiled-value
   adapter. Report provenance and validate physical tensors and fallback values.
5. **Physics-aware unit conversion.** Convert into an existing stage's length,
   mass and axis conventions, including inertia, velocities, gravity, joint
   limits and drives. Expose the same helper through native and Python APIs.
6. **Compact OBJ topology.** Offer source-position preservation, indexed
   face-varying UVs and face-varying normals to avoid unnecessary position and
   skin-weight duplication. Retain the existing expanded representation.
7. **Supplied-skeleton binding.** Add native/Python helpers that validate `vw`
   indices against an existing skeleton, author relationships and bind
   transforms, and explicitly remap joints. Pruning/normalization is opt-in.
8. **Constraints and tendons.** Extend tendon equality preservation and graph
   validation; support evaluated sphere/cylinder wrap paths with clear
   approximation reporting and solver capability checks.
9. **Versioned Newton compatibility.** Audit current mass/joint/SDF/shell-mass
   schemas and add typed preservation, validation and optional downstream
   import verification. Distinguish schema preservation from solver execution.

## Priority 3: I/O and export

10. Add mapped/chunked large-file input, cancellation and progress callbacks;
    expose OBJ/MTL export with explicit transforms, material/topology policies
    and optional `vw` output. Treat deformables as a separately versioned,
    experimental extension after the rigid-body foundation is verified.

## Acceptance and verification

- Verify each delivered feature with meaningful generated-input regressions in
  native and Python paths, including failures, limits and ownership lifetimes.
- Run focused native CTest, ASan/UBSan and Python suites from isolated builds.
  Run the full native/standalone-next regression gates at integration milestones.
- Inventory the actual local Menagerie revision using `MENAGERIE_ROOT`; test
  tracked OBJ/MTL files and standalone MJCF entries, accounting for fragments.
- Extend engine comparisons beyond successful loading: compare body mass, COM,
  inertia tensors, joint frames/limits, filtering, actuators and initial state
  between direct imports and converted USD in the same engine. Keep optional
  short dynamics checks separate from ordinary dependency-free tests.
- Test USDA/USDC/USDZ round trips and downstream OpenUSD/Newton behavior where
  applicable. Record tool versions and unsupported features without claiming
  simulation equivalence from schema preservation alone.
- Generate a tested capability matrix and correct stale physics documentation.

## Current progress

- Completed before this goal: bounded large OBJ parsing, `vw` arrays, normal
  generation fixes, strict MTL numeric validation, bounded custom-property
  sorting, indexed material/library lookup and duplicate-name source tracking.
- Current verification baseline: 103 focused Python tests; native and sanitizer
  coverage; 100,000-name lookup stress; 1,184 Menagerie OBJ and 24 MTL files.
  These changes remain uncommitted.
- Delivered Priority 1.1: per-import request/resolved-identifier texture caching,
  shared immutable ownership through layers/C/Python stages, separate resolver
  open/read callbacks, borrowed USDZ asset views and one entry per retained
  texture. Fixed export path rewriting so packaged shader paths are self-contained
  without mutating the source stage. Separate composed foreign layers retain
  independent read caches.
- Priority 1.1 verification: 116 focused Python tests; all 68 registered standalone
  next tests passed (two optional AOUSD checks skipped); five focused ASan/UBSan
  tests passed. Native CLI build and legacy CLI syntax check passed.
- Delivered Priority 1.2: bounded versioned native reports, owned C report queries,
  Python stage/error dictionaries, independent input/memory/depth limits and
  explicit warn/error/preserve policies for known unsupported OBJ/MJCF data.
  Preserved records survive USDA/USDC/USDZ as non-executable layer metadata;
  reports remain runtime telemetry. Included/skipped XML subtrees and expanded
  documents enforce depth limits. Both native CLIs expose the new policies and
  independent limits. See [the report contract](import-report.md).
- Priority 1.2 verification: 135 focused Python tests (including OpenUSD 23.08
  preservation/skin oracles), full Python suite 269 passed / 10 optional skips,
  all 68 registered next tests passed (two optional AOUSD checks skipped), six
  focused ASan/UBSan tests passed. Both native CLIs passed preservation exports
  and policy/limit failures. At Menagerie revision
  `cda5236f98383f6fbaa4820c6b18dba892cd087f`, all 1,184 OBJ files passed and all
  223 standalone MJCF entries passed the existing MuJoCo 3.16.0 rest-frame,
  joint-anchor/limit and record-count comparisons. These checks do not establish
  dynamics, inferred-inertia or surface-wrap equivalence.
- Priority 1.3 delivered first slice: shared native C++/C typed snapshots now back
  Python inspection, including owned array views, source property types,
  variability, blocks, unknown engine attributes and collision groups. Existing
  Python tuple output remains the default. Native C++/C ownership/time/budget
  tests and 46 focused Python cases passed; the full Python suite passed 280
  tests with 10 optional skips. See [typed snapshots](physics-query.md).
- Snapshot sanitizer and core-only (Tydra/OBJ disabled) checks passed. All 223
  standalone Menagerie entries also passed through the native-backed inspector.
- Standard UsdPhysics authoring now reads types, variability, API names and
  multiple-apply namespaces from the generated native C11 descriptors. Python
  exposes immutable definitions and checks drive/limit instances. Versioned
  engine extension descriptors are still pending.
- Snapshot/descriptor milestone verification: full Python suite 284 passed /
  10 optional skips, plus
  150 final focused cases including explicit-auto dispatch; all 69 registered
  native next tests passed (two optional AOUSD checks skipped). Snapshot
  ASan/UBSan, core-only query and pure-C11 descriptor-link checks passed. The
  instance-group timing gate passed in the final full run after its earlier
  contention-related threshold failure during concurrent build/dataset work.
- Delivered structured native validation through owned C/C++ reports and
  immutable Python `physics.validate()` results. Reports retain native rule IDs,
  locations, sample times and coverage. Streaming issue/byte limits and bounded
  sample counts expose omissions and cannot turn incomplete reporting into a
  passing result. The validation component remains optional for ordinary native
  core/query/render consumers. See [validation](physics-validation.md).
- The new semantic Menagerie gate exposed and drove corrections for inherited
  partial MJCF solver arrays, canonical option tokens, zero solver tolerances,
  `jointactuatorfrc` and independent UsdPhysics inertia/orientation fallbacks.
  Solver completion and enum spelling changes have structured diagnostics.
  Nonuniform analytic ellipsoid approximations now respect warn/error/preserve
  policies while retaining the existing default representation.
- Current verification: full Python suite 308 passed / 10 optional skips;
  native next suite 68 passed / two optional AOUSD skips.
  Four focused ASan/UBSan tests, the core-only validation-report test and a C11
  header check passed. Both native CLIs passed solver/default-policy checks and
  USDA/USDC/USDZ mesh-collider validation round trips. Generated native C and
  Python cases cover inherited solver vectors and excess-entry failures.
- At Menagerie revision `cda5236f98383f6fbaa4820c6b18dba892cd087f`, all 223
  standalone entries passed import and existing MuJoCo 3.16.0 reference checks
  in both final profiles. With explicit mesh colliders, all 223 also passed
  native semantic validation. Default analytic colliders produced 50
  `physics.collider.nonUniformScale` findings across 10 entries with nonuniform
  ellipsoids. Meshes remain polygon approximations; these checks establish
  neither analytic collision nor simulation equivalence.
- Pending: remaining Priority 1.3 and the Priority 2–3 roadmap above.

Reference specifications: [UsdPhysics](https://openusd.org/dev/api/usd_physics_page_front.html),
[MJCF](https://mujoco.readthedocs.io/en/stable/XMLreference.html), and
[Newton USD import](https://newton-physics.github.io/newton/latest/concepts/usd_parsing.html).
