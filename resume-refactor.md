# LightUSD refactor checkpoint

## Current continuation (2026-10-16)

The requested seven-area maintainability refactor is implemented: the load /
session facade, layer diff, crate reconstruction, shader extraction, URDF/MJCF
authoring, RenderStream bindings, and layer-document bindings have focused
compiled boundaries. Loader result handling, session memory accounting,
streaming asset normalization and matching bounded string copies are shared.
Public APIs, product defaults and optimization flags are unchanged.

This continuation started from clean commit `7659fa4fc`.
New private headers remain implementation-only and uninstalled. Session Impl
and its synchronization stay together. Crate scheduling, URDF authoring order,
JSON budgets, ownership and distinct copy protocols retain their behavior.

The accepted earlier WASM-size stopping point still applies. The new request
explicitly authorized these TU/binding splits; it did not reopen size tuning,
raw-value-buffer migrations, portable POD bindings, or product-default work.
The completed PCP split described below remains historical context.

See [the continuation report](doc/refactor-c-core.md#continued-maintainability-refactor-2026-10-16)
for boundaries, measurements and limitations. Key results:

- Native affected-TU slowest median: 3.15 → 2.03 s; aggregate isolated compile
  time: 12.20 → 21.34 s. wasm32: 2.47 → 1.99 s and 11.60 → 25.39 s.
  Splitting increases aggregate compilation work; no clean-build win is claimed.
- Current next-only raw WASM: 1,954,631 bytes (wasm32), 2,174,316 bytes
  (memory64), measured with Emscripten 5.0.1. The older PCP table used 4.0.14
  and is not directly comparable. Compressed size grows slightly against the
  matching archived baseline. This is not a renewed size target.
- Root native CTest: 113 passed, two optional skips. Standalone Debug,
  threaded Debug, and optimized threaded ASan/UBSan without PCH: 57 passed,
  one optional skip each. Sanitizers disable leak detection in the sandbox
  and GCC vptr checks for RTTI-disabled libraries.
- Next and legacy Debug builds also pass with examples and tools enabled
  and the GUI viewer disabled. Additional legacy checks report 154 passed,
  four skipped and two failures; the unit suite was stopped after ten minutes.
  The full legacy gate remains incomplete; details are in the continuation report.
- Sixteen standalone header syntax checks pass; 705 fixture rewrites retain
  identical exit status, stdout and stderr. No OpenUSD usdcat is available.
- Next-only and combined wasm32/memory64 builds pass. The new raw C-boundary
  copy/name test passes on both baseline and refactored modules at both widths.
- Node profiles pass 49 suites with seven known baseline failures per width.
  The full web gate remains incomplete because these failures and missing
  Menagerie data remain. Both validation-app browser checks and eight UDIM
  main-thread/Worker cases pass after providing Chrome.
- Alternating generated-mesh and 1,000-link robot runtime probes preserve
  geometry hashes and byte-identical URDF USDA output. They do not establish
  comprehensive runtime parity.

Reports and the original source snapshot are under ignored
`build_ninja/continued-refactor/`. The no-PCH check also repaired missing
includes in the earlier USDA-printer and schema-predicate headers; do not
remove those explicit dependencies. Generated validator/EXR JS glue is ignored.

## Historical accepted stopping point

The user accepted approximately **1.6 MB raw WASM** and requested that TU and
WASM-size optimization stop after finishing the PCP TU refactor. That refactor
is finished. The earlier <512 KiB goal remains superseded. The measurements
and verification below describe that older checkpoint, not the current build.

## Finished PCP refactor

Private implementation types live in `src/next/pcp/cache-internal.hh`.
Non-template compiled boundaries separate:

- `cache.cc`: public forwarding API and remaining coordination.
- `cache-arcs.cc`: arc and variant expansion.
- `cache-relocates.cc`: relocation and ancestral source resolution.
- `cache-layers.cc`: layer-stack construction and expression fingerprints.
- `cache-opinions.cc`: strong-to-weak opinion composition.
- `cache-storage.cc`: container construction/destruction, including worker caches.

The existing composition algorithms, ownership and threading behavior are
preserved. The private implementation header must not become a consumer header.
No source-local optimization workaround was added.

## Historical PCP measurements

Emscripten 4.0.14, MinSizeRel, SIMD, wasm32, without LTO:

| Item | Result |
| --- | ---: |
| Raw next + Tydra-next WASM | 1,626,756 bytes |
| Comparable pre-refactor WASM | 1,689,884 bytes |
| Raw size reduction | 63,128 bytes (3.7%) |
| Memory64 WASM | 1,827,671 bytes |
| Original PCP cache TU, isolated pass | 9.00 s |
| Final cache.cc, isolated pass | 6.25 s |
| Final cache-arcs.cc | 5.43 s |
| Final cache-relocates.cc | 4.55 s |
| Final cache-layers.cc | 4.38 s |
| Final cache-opinions.cc | 4.21 s |
| Final cache-storage.cc | 4.13 s |

Final PCP timing is one cache-disabled serial pass, not a multi-run median.
Earlier three-run measurements and the broader production sweep are described
in `doc/refactor-c-core.md`. Splitting reduces the slowest TU but increases
aggregate compilation work and aggregate pre-link object text; do not claim a
whole-product clean-build speedup. The pre-refactor product retained its old
Tydra coordinator's source-local optimization override; the current build does
not. A separate LTO trial at the preceding checkpoint produced 1,667,514 bytes,
so LTO was not adopted.

## Historical PCP verification

After the final PCP lifecycle extraction:

- Native next CTest: 37 passed; one optional conformance case skipped.
- Threaded ASan/UBSan CTest: 37 passed; the same optional case skipped.
- wasm32 and memory64 products built successfully.
- wasm32 USDA composition suite passed.
- Memory64 next-only composition/variant/diff/validation cases passed;
  dispatch lifetime/JSON and Tydra/flatten smoke suites passed.
- The mixed memory64 composition suite's legacy case fails because the
  existing legacy module lacks `nextFlattenAsyncBegin`; it is outside the
  next-only product and was not changed.

Before the final lifecycle-only extraction: the rebuilt Python extension passed
133 tests with seven skips; legacy MiniJSON unit tests passed; the full Node
profile using the rebuilt next module passed 18 suites, with the helper suite's
three previously observed EXR failures remaining. The standalone checker built,
dumped the immutable rule registry and validated the cube fixture.

These results do not declare the full browser/legacy parity gate green.

## Other changes retained from this work

See `doc/refactor-c-core.md` for the API changes: C-dispatched next WASM classes
(still using emval internally), split render bindings, compiled POD chunk
storage with explicit mutation, Dragonbox-backed MiniJSON number serialization,
and split validation with an immutable pointer/count rule registry.

The larger raw-value-buffer, portable POD binding ABI and default-product
migrations remain unimplemented roadmap items, not follow-up instructions.
A mixed-function GCC buffer microbenchmark showed a register-spill slowdown,
while an isolated read kernel did not and a generated 30,000-triangle WASM
conversion measured about 47 ms versus 49 ms with equal heap usage. Comprehensive
runtime parity is not established; this caveat is retained rather than treating
the size acceptance as proof of runtime parity.
