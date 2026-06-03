# C++ Testing Guide

How to build, run, and extend the TinyUSDZ C++ test suite.

## Overview

The C++ test infrastructure is split into four layers:

1. `ctest`-registered tests for parser coverage, roundtrip coverage, feature tests, and the main Acutest unit suite.
2. A large Acutest executable, `unit-test-tinyusdz`, which aggregates the unit coverage from `tests/unit/unit-main.cc`.
3. Standalone/manual runners under `tests/` for broader corpus checks, especially parser and Tydra conversion sweeps.
4. Fuzzer targets under `tests/fuzzer/` built separately with Meson/libFuzzer.

Core functionality is tested by the parser, reader, writer, composition, and crate-writer coverage. Tydra is covered in both the Acutest suite and the manual `tydra_to_renderscene` corpus runner.

## Build

Configure the native build with tests enabled:

```bash
mkdir build
cd build
cmake .. -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
make -j16
```

Relevant options in the current build configuration:

- `TINYUSDZ_BUILD_TESTS=ON`
- `TINYUSDZ_BUILD_EXAMPLES=ON`
- `TINYUSDZ_WITH_JSON=ON`
- `TINYUSDZ_WITH_MODULE_USDA_READER=ON`
- `TINYUSDZ_WITH_MODULE_USDC_READER=ON`
- `TINYUSDZ_WITH_MODULE_USDC_WRITER=ON`
- `TINYUSDZ_WITH_TYDRA=ON`
- `TINYUSDZ_WITH_PXR_COMPAT_API=ON`

## Full Regression Tests

Run the full regression suite before changes that affect parsing, composition,
USDA/USDC writing, USDZ packaging, schema reconstruction, or tool output.

> **Scope:** The experimental `next` module (`src/next/`, `tinyusdz_next`) and
> its tests under `tests/next/` are **not** part of this regression suite. They
> are a standalone CMake project, are not built by the main `build/` (so they do
> not appear in `ctest`), and are not run by the Pixar comparison runner. Do not
> treat `next` results as part of the regression gate. See
> [Experimental `next` library tests](#experimental-next-library-tests) for how
> to build and run them on demand.

The full regression pass has two parts:

1. All CMake/CTest-registered tests, including parser corpus tests, roundtrip
   corpus tests, registered feature tests, benchmarks in quick mode, MCP tests,
   and the main Acutest unit suite.
2. The Node.js `tusdcat` vs OpenUSD v26.05 `usdcat` comparison runner, which
   checks TinyUSDZ output against `usdcat` over the USDA and USDC fixture
   corpora.

Recommended command sequence from the repository root:

```bash
# Build all configured tests and examples, including tusdcat.
cmake --build build

# 1. Run all CTest-registered tests.
cd build
ctest --output-on-failure
cd ..

# 2. Run the Node.js roundtrip/comparison suite against OpenUSD v26.05.
TUSDCAT_PATH=./build/tusdcat \
USDCAT_PATH=../OpenUSD/dist/bin/usdcat \
  bash tests/run-usdcat-compare.sh
```

`tests/run-usdcat-compare.sh` requires Node.js and a working OpenUSD v26.05
`usdcat`. Prefer a sibling OpenUSD v26.05 install at
`../OpenUSD/dist/bin/usdcat`. The script falls back to
`~/local/USD/dist/bin/usdcat` when the sibling install is not present, but full
regression results should use v26.05 unless a test intentionally targets
another OpenUSD release. Set `USDCAT_PATH` explicitly when needed.

The comparison runner writes detailed logs to `tests/comparison-results/` and
prints a failure/warning summary. It continues across individual files so one
failure does not hide later failures; inspect the summary and log even when the
shell command itself completes.

Useful variants:

```bash
# Quieter comparison logs.
SHOW_DETAILED_DIFF=false \
TUSDCAT_PATH=./build/tusdcat \
USDCAT_PATH=../OpenUSD/dist/bin/usdcat \
  bash tests/run-usdcat-compare.sh

# Longer per-file timeout for slow debug/ASan builds.
TIMEOUT_MS=120000 \
TUSDCAT_PATH=./build/tusdcat \
USDCAT_PATH=../OpenUSD/dist/bin/usdcat \
  bash tests/run-usdcat-compare.sh

# Single-file comparison through the Node.js runner.
node tests/compare-usda.js \
  --tusdcat ./build/tusdcat \
  --usdcat ../OpenUSD/dist/bin/usdcat \
  --detailed-diff \
  tests/usda/somefile.usda
```

## ctest Suite

CMake registers these tests when the corresponding targets are built (most in the top-level `CMakeLists.txt`; `unit-test-tinyusdz` in `tests/unit/CMakeLists.txt` and `mcp-test` in `tests/mcp/CMakeLists.txt`):

| ctest name | Kind | Backing executable/script |
| --- | --- | --- |
| `usda-parser-unit-test` | Parser corpus runner | `python3 tests/usda/unit-runner.py --app build/test_tinyusdz` |
| `usda-roundtrip-test` | USDA roundtrip corpus runner | `python3 tests/usda/roundtrip-runner.py --app build/usda_roundtrip` |
| `usdc-roundtrip-test` | USDA -> USDC -> reparse corpus runner | `python3 tests/usda/usdc-roundtrip-runner.py --app build/usdc_roundtrip` |
| `usdc-parser-unit-test` | Parser corpus runner | `python3 tests/usdc/unit-runner.py --app build/test_tinyusdz` |
| `usdc-writer-diff-test` | USDC writer roundtrip diff (informational) | `python3 tests/usdc-writer/usdc-writer-runner.py ... --report-only` |
| `feat-value-clip` | Feature test | `build/feat-value-clip` |
| `feat-mtlx-parse`, `feat-mtlx-import`, `feat-mtlx-export` | Feature tests | `build/feat-mtlx-*` |
| `feat-mtlx-grouped-params` | Feature test (needs `TINYUSDZ_WITH_JSON`) | `build/feat-mtlx-grouped-params` |
| `feat-variant-converter`, `feat-variant-applier` | Feature tests | `build/feat-variant-*` |
| `feat-subdiv-compare` | Feature test (only when `TINYUSDZ_WITH_OPENSUBDIV`) | `build/feat-subdiv-compare` |
| `bench-parse-opt` | Benchmark target (label `benchmark`) | `build/bench-parse-opt --quick` |
| `unit-test-tinyusdz` | Acutest unit suite | `build/unit-test-tinyusdz` |
| `mcp-test` | MCP server unit test (only when `TINYUSDZ_WITH_MCP_SERVER`) | `build/mcp-test` |

`usdc-parser-unit-test` is set to run after `unit-test-tinyusdz` (it globs `*-runtime.usdc` fixtures the unit suite generates).

Only `bench-parse-opt` has a `ctest` label today:

```bash
ctest --print-labels
# benchmark
```

Useful commands:

```bash
cd build

# List configured tests
ctest -N

# Run everything registered with ctest
ctest --output-on-failure

# Run just the Acutest suite
ctest -R unit-test-tinyusdz --output-on-failure

# Run parser corpus tests only
ctest -R 'parser-unit-test' --output-on-failure

# Run roundtrip corpus tests only
ctest -R roundtrip --output-on-failure

# Exclude benchmark-labeled tests
ctest --output-on-failure -LE benchmark
```

## Regression Test Procedure

Use this procedure for merge-level validation, release checks, and refactor hardening:

### 1) CTest matrix (native)

```bash
cd build

# Native regression gate for parser/roundtrip/unit/feature tests
ctest --output-on-failure

# Optional focused subsets
ctest -R unit --output-on-failure
ctest -R roundtrip --output-on-failure
ctest -R feat --output-on-failure
```

### 2) Feature binaries (target-level)

Run feature targets directly when an individual behavior needs isolation:

```bash
./build/feat-mtlx-parse
./build/feat-mtlx-import
./build/feat-mtlx-export
./build/feat-variant-converter
./build/feat-variant-applier
./build/feat-mtlx-grouped-params
```

### 3) WASM regression checks

For web changes, build and validate the WebAssembly tree:

```bash
cd web
emcmake cmake -S . -B build
cmake --build web/build -j16
ctest --test-dir web/build --output-on-failure
```

If your local web build does not register ctest targets, use the web package checks documented by the web frontend pipeline as the functional equivalent.

### 4) Roundtrip comparison against Pixar USD

These checks catch cross-version serialization or compatibility drift:

```bash
# Batch script for broad coverage
USDCAT_PATH=~/local/USD/dist/bin/usdcat TUSDCAT_PATH=./build/tusdcat \
  bash tests/run-usdcat-compare.sh

# Per-file diff for regression investigation
node tests/compare-usda.js --detailed-diff \
  --tusdcat ./build/tusdcat --usdcat ~/local/USD/dist/bin/usdcat \
  tests/usda/somefile.usda
```

`bench-parse-opt` is intentionally split into two profiles:

- `ctest` runs `bench-parse-opt --quick` to keep suite wall time short.
- Manual benchmark runs can still use the default full profile via `./build/bench-parse-opt`.

## Fixture Coverage

Primary fixture locations in the repository:

- `tests/usda/*.usda`
- `tests/usda/fail-case/*.usda`
- `tests/usdc/*.usdc`
- `models/`

The `ctest` parser and roundtrip runners only operate on top-level `*.usda` or `*.usdc` files in their configured fixture directories. They do not recurse.

## Acutest Unit Suite

The main unit executable is built from `tests/unit/CMakeLists.txt` and registered through `tests/unit/unit-main.cc`.

The suite currently contains 600+ registered test cases. Coverage spans Core parser/value/stage/composition/writer functionality plus Tydra scene-access, RenderScene conversion, shader queries, physics, and IK/rigid-body solvers.

Major source groups in `tests/unit/` (see `tests/unit/CMakeLists.txt` for the full source list):

- Core parsing and value handling: `unit-ascii-parse`, `unit-value-types`, `unit-customdata`, `unit-primvar`, `unit-timesamples`, `unit-fp-parse-print`, `unit-minijson`, `unit-strutil`, `unit-math`, `unit-xform`, `unit-half-roundtrip`
- Scene graph and composition: `unit-stage`, `unit-composition`, `unit-composition-arcs`, `unit-composition-graph`, `unit-layer`, `unit-primspec`, `unit-prim-api`, `unit-prim-reconstruct`
- Reader/writer coverage: `unit-usda-reader`, `unit-usdc-reader`, `unit-usdc-reconstruct`, `unit-usda-writer`, `unit-usda-roundtrip`, `unit-usdz-writer`, `unit-usdc-writer`, `unit-crate-writer`, `unit-usd-validation`
- Tydra coverage: `unit-tydra`, `unit-tydra-renderscene`, `unit-tydra-shader`, `unit-tydra-subdivision`, `unit-materialx`
- Subdivision: `unit-subdiv-tinyusdz`
- Physics / simulation: `unit-physics`, `unit-ik`, `unit-rb-collision`, `unit-rb-dynamics`
- Security and utility coverage: `unit-security`, `unit-task-queue`, `unit-tiny-container`, `unit-tiny-hashmap`, `unit-handle-allocator`, `unit-ioutil`, `unit-pathutil`, `unit-pprint`
- PXR compat API: `unit-pxr-compat-api` (conditionally compiled with `TINYUSDZ_WITH_PXR_COMPAT_API`)

`unit-dedup.cc` is present but temporarily disabled in `CMakeLists.txt` (needs API updates).

Run it directly:

```bash
./build/unit-test-tinyusdz
```

List individual Acutest cases:

```bash
./build/unit-test-tinyusdz --list
```

Run one case:

```bash
./build/unit-test-tinyusdz crate_writer_cone_test
```

Representative Tydra-related cases:

- `tydra_connection_validation_test`
- `tydra_scene_access_helper_test`
- `tydra_shader_scene_access_test`
- `tydra_skel_scene_access_test`
- `tydra_renderscene_single_mesh_test`
- `tydra_renderscene_material_binding_test`
- `tydra_shader_get_bound_material_test`

## Parser Corpus Runners

These are the Python scripts used by `ctest`.

### USDA parser runner

`tests/usda/unit-runner.py` runs:

- every top-level `*.usda` file under `--basedir` as a success case
- every `fail-case/*.usda` file under `--basedir` as an expected failure

It exits nonzero when any success case fails to parse.

```bash
python3 tests/usda/unit-runner.py \
  --app ./build/test_tinyusdz \
  --basedir tests/usda
```

### USDC parser runner

`tests/usdc/unit-runner.py` runs:

- every top-level `*.usdc` file under `--basedir` as a success case
- every `failure-case/*.usdc` file under `--basedir` as an expected failure

It exits nonzero when any success case fails, or when an expected-failure file parses successfully.

```bash
python3 tests/usdc/unit-runner.py \
  --app ./build/test_tinyusdz \
  --basedir tests/usdc
```

### Standalone loader binary

The parser runners above use `test_tinyusdz`, built from `tests/test-main.cc`.

`test_tinyusdz`:

- dispatches by extension to `LoadUSDAFromFile`, `LoadUSDCFromFile`, `LoadUSDZFromFile`, or `LoadUSDFromFile`
- returns success/failure based on the parser result
- supports an optional `--verbose`
- is marked in source as a candidate for future deprecation in favor of `tusdcat`

## Roundtrip Runners

### USDA roundtrip

`tests/usda/roundtrip-runner.py` runs `usda_roundtrip` over top-level `*.usda` fixtures and supports:

- `--verbose`
- `--dump-on-fail`
- `--skip-file`
- `--no-skip-known`

The runner supports a `KNOWN_FAILURES` skip list.

```bash
python3 tests/usda/roundtrip-runner.py \
  --app ./build/usda_roundtrip \
  --basedir tests/usda \
  --verbose
```

### USDC roundtrip

`tests/usda/usdc-roundtrip-runner.py` runs `usdc_roundtrip` over top-level `*.usda` fixtures and reports pass/fail per file.

```bash
python3 tests/usda/usdc-roundtrip-runner.py \
  --app ./build/usdc_roundtrip \
  --basedir tests/usda \
  --verbose
```

## USDC Writer Diff Test

`usdc-writer-diff-test` exercises the full USDC write/read cycle with Layer-level diff comparison.

Pipeline per file:

1. `tusdcat input.usda -o temp.usdc` — TinyUSDZ writes USDC
2. `tusdcat temp.usdc -o temp_rt.usda` — TinyUSDZ reads USDC back as USDA
3. `tusddiff input.usda temp_rt.usda` — Layer-level diff of original vs roundtripped USDA

The `ctest` target runs in `--report-only` mode (informational — always exits 0) because the USDC writer does not yet preserve all property and metadata details.

For strict validation:

```bash
python3 tests/usdc-writer/usdc-writer-runner.py \
  --tusdcat ./build/tusdcat \
  --tusddiff ./build/tusddiff \
  --basedir tests/usda \
  --verbose
```

### tusddiff

`tusddiff` is the Layer-level diff tool built from `examples/usddiff/`. It loads both files via `LoadLayerFromFile` (PrimSpec tree) and reports added, deleted, and modified prims and properties.

```bash
# Text diff
./build/tusddiff file1.usda file2.usda

# JSON diff
./build/tusddiff --json file1.usda file2.usda

# Quiet mode (exit code only: 0 = identical, 1 = different, 2 = error)
./build/tusddiff --quiet file1.usda file2.usda
```

## Feature Tests

The feature-test layer is now partially integrated into `ctest`.

Registered feature executables include:

- MaterialX: `feat-mtlx-parse`, `feat-mtlx-import`, `feat-mtlx-export`, `feat-mtlx-grouped-params`
- Variant support: `feat-variant-converter`, `feat-variant-applier`

Some older feature work still exists outside `ctest` under `tests/feat/` as standalone programs, benchmarks, or notes. Not everything under `tests/feat/` is automatically built or registered.

For `bench-parse-opt` specifically:

- `ctest -R bench-parse-opt` exercises the reduced `--quick` profile.
- `./build/bench-parse-opt` runs the full synthetic workload for ad hoc performance work.
- `./build/bench-parse-opt --quick` runs the same reduced profile directly.

## Tydra Testing

Tydra is covered in two different ways.

### Unit coverage inside `unit-test-tinyusdz`

The Tydra unit sources are:

- `tests/unit/unit-tydra.cc`
- `tests/unit/unit-tydra-renderscene.cc`
- `tests/unit/unit-tydra-shader.cc`

These cover:

- scene-access helpers
- material binding validation
- texture and envmap loader policy
- skeletal animation and skin binding validation
- RenderScene conversion for empty stages, mesh stages, transforms, materials, lights, cameras, and memory estimation
- shader listing and material query behavior

### Manual corpus conversion runner

The `tydra_to_renderscene` example target is built from `examples/tydra_to_renderscene/CMakeLists.txt` and lands in `build/tydra_to_renderscene`.

`tests/tydra_to_renderscene/runner.py`:

- is not registered with `ctest`
- assumes it is launched from `build/`
- executes `./tydra_to_renderscene`
- recursively scans a USD tree with case-insensitive matching for `.usd`, `.usda`, `.usdc`, and `.usdz`
- prints success and failure lists, but does not currently `sys.exit(1)` on failures

Typical usage:

```bash
cd build
python3 ../tests/tydra_to_renderscene/runner.py ../models
```

## Standalone and Manual Targets

### Experimental `next` library tests

`next` (`src/next/`, library `tinyusdz_next`) is an **experimental, under-construction**
rewrite of the core with a new modular architecture. It is intentionally kept
out of the main regression suite:

- It is a **standalone CMake project** (`src/next/CMakeLists.txt` with its own
  `project()`), *not* added by the top-level `CMakeLists.txt`. The default
  `build/` therefore does not compile it, and none of its tests appear in the
  `build/` `ctest` run or the Pixar comparison runner.
- Its tests are gated behind `TINYUSDZ_NEXT_BUILD_TESTS` (**OFF by default**).
- Treat its results as informational only — **not** a merge/regression gate.
  Do not wire `next` into the full regression gate until the suite is hardened.

Build and run them on demand in a separate build directory:

```bash
# Configure the standalone next project with its tests enabled.
cmake -S src/next -B build-next -DTINYUSDZ_NEXT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-next -j16
ctest --test-dir build-next --output-on-failure
```

> **Caveat — use a Debug build.** The `tests/next/` programs validate via bare
> `assert()`. Under `NDEBUG` (i.e. `Release`/`RelWithDebInfo`) `assert(expr)`
> does not evaluate `expr`, so these tests check nothing — and any side effect
> placed inside an `assert()` is silently dropped (this previously caused a
> null-dereference SIGSEGV in `test_usdc_roundtrip` when the `toc`-populating
> `ParseUSDCBinary()` call was compiled out). Build with `-DCMAKE_BUILD_TYPE=Debug`
> for meaningful validation, and keep side-effecting calls out of `assert()`.

### CMake targets not in ctest

These targets exist in the CMake test infrastructure but are not currently registered with `ctest`:

- `test-decompress-int` — defined in `tests/decompress-int/CMakeLists.txt` but its `add_test(...)` block is still commented out.
- `pprint_benchmark` — defined in `tests/pprint/CMakeLists.txt` as a standalone benchmark executable.

### Feature-directory standalone tools

Several directories under `tests/feat/` contain standalone programs built via local Makefiles, not through CMake or `ctest`. These are developer tools for ad hoc benchmarking and manual testing:

| Directory | Contents | Notes |
| --- | --- | --- |
| `hash/` | `hash_bench.cc` | Hash function microbenchmark |
| `tangent/` | `bench_tangent.cc` | Tangent computation benchmark |
| `tydra-mesh-build/` | `bench_mesh_build.cc` | RenderScene mesh-build benchmark |
| `zstdusd/` | `test_zstd_usd.cc`, `compress_usda.cc` | Zstd-compressed USD read/write tools |
| `nestedVariantSet/` | `test_variant_api.cpp` | Standalone variant API exerciser (the ctest-registered tests are `feat-variant-converter` and `feat-variant-applier`) |

Some earlier standalone tests have been folded into the Acutest unit suite (`typed-array-view`, `typed-array-timesamples`, `value-view`). Their `tests/feat/` Makefiles still exist but the canonical coverage now lives in `unit-test-tinyusdz`.

### Feature fixture directories

These directories hold `.usda` fixtures used by feature tests or as reference material. They are not scanned by any `ctest` runner:

| Directory | Contents |
| --- | --- |
| `tests/feat/lux/` | Light shader and mesh-light test scenes (7 USDA files + MaterialX reference) |
| `tests/feat/node-mtlx/` | Blender-style MaterialX node USDA files (CombineColor, MapRange, Math, etc.) |
| `tests/feat/skinning/` | Skeletal animation fixtures (static, timesampled, mixed) |

### MaterialX standalone tests

Besides the four ctest-registered targets (`feat-mtlx-parse`, `-import`,
`-export`, `-grouped-params`), `tests/feat/mtlx/` holds extra source files built
only via its local Makefile, e.g. `test_nodegraph_export.cc`,
`test_mtlx_include_traversal.cc`, `test_parser_debug.cc`, and
`threejs_mtlx_export_example.cc`.

## Additional Corpus Runner

`tests/parse_usd/runner.py` is a separate broad parser sweep tool. It is not used by `ctest`.

Important current behavior:

- it looks for `build_release/tusdcat`, not `build/test_tinyusdz`
- it recursively scans a target tree for `.usd`, `.usda`, `.usdc`, and `.usdz`
- it invokes `tusdcat -l <file>`
- it supports `--timeout`
- it prints failures but does not currently return a failing process exit code based on the failure list

Typical usage:

```bash
python3 tests/parse_usd/runner.py models --timeout 180
```

## Roundtrip Comparison Against OpenUSD

For the full batch comparison against OpenUSD v26.05 `usdcat`, use the second half of
the [Full Regression Tests](#full-regression-tests) sequence above. The runner
compares all top-level USDA fixtures under `tests/usda/` and all top-level USDC
fixtures under `tests/usdc/`.

Short form:

```bash
TUSDCAT_PATH=./build/tusdcat \
USDCAT_PATH=../OpenUSD/dist/bin/usdcat \
  bash tests/run-usdcat-compare.sh
```

Single-file mode through the Node.js comparator:

```bash
node tests/compare-usda.js \
  --tusdcat ./build/tusdcat \
  --usdcat ../OpenUSD/dist/bin/usdcat \
  --detailed-diff \
  tests/usda/somefile.usda
```

## Python Bindings Test

`tests/python/test_basic.py` contains pytest-based tests for the Python bindings module (`tinyusdz`). This is not integrated into `ctest` and requires the Python bindings to be built and installed separately.

```bash
cd tests/python
pytest test_basic.py
```

## Fuzzing

Fuzzer targets live under `tests/fuzzer/`. They are built separately from the CMake test suite using Meson and libFuzzer.

Documented setup in `tests/fuzzer/README.md`:

```bash
CXX=clang++ CC=clang meson build -Db_sanitize=address
cd build
ninja
```

Examples from the current README:

```bash
./fuzz_tinyusdz -max_len=128m
./fuzz_intcoding_decompress -rss_limit_mb=8192 -jobs 4
```

Relevant fuzz entry points include:

- `tinyusdz_fuzzmain.cc`
- `usdaparser_fuzzmain.cc`
- `usdcparser_fuzzmain.cc`
- `lz4_decompress_fuzzmain.cc`
- `intCoding_decompress_fuzzmain.cc`

## Adding or Updating Tests

### Add a new Acutest unit

1. Declare the test function in the corresponding `tests/unit/unit-*.h` header.
2. Implement it in the matching `tests/unit/unit-*.cc` file with `TEST_CHECK` or `TEST_ASSERT`.
3. Register it in `tests/unit/unit-main.cc`.
4. Rebuild and run `ctest -R unit-test-tinyusdz --output-on-failure`.

### Add a new `ctest` target

1. Add the executable in the top-level `CMakeLists.txt` under `if(TINYUSDZ_BUILD_TESTS)`.
2. Register it with `add_test(...)`.
3. If it is a benchmark, label it consistently, as `bench-parse-opt` already does with `LABELS "benchmark"`.

### Add parser or roundtrip fixtures

1. Place the fixture in the relevant top-level directory:
   `tests/usda/`, `tests/usda/fail-case/`, `tests/usdc/`, or `tests/usdc/failure-case/`.
2. Keep in mind the current runners do not recurse for the `ctest` corpus tests.
3. Re-run the matching parser or roundtrip runner.

## Disabled Tests (TODO/FIXME)

The USDC memory-budget and variant PrimSpec/roundtrip tests that were disabled
after the `spec-2026-mar` merge have since been **re-enabled** — they are all
active registrations in `tests/unit/unit-main.cc`, and their static fixtures
(`variantSet-collision-001.usdc`, `variantSet-prim-001.usdc` in `tests/usdc/`)
load successfully.

The only Acutest tests still disabled are the array-dedup tests. They live in
`tests/unit/unit-dedup.cc`, which is commented out of `TEST_SOURCES` in
`tests/unit/CMakeLists.txt`; the six matching registrations are commented out in
`tests/unit/unit-main.cc`:

- `dedup_float_array_test`
- `dedup_double_array_test`
- `dedup_int_array_test`
- `dedup_unique_arrays_test`
- `dedup_string_array_test`
- `dedup_matrix4d_test`

To re-enable them: update `unit-dedup.cc` to the current CrateWriter dedup API,
re-add `unit-dedup.cc` to `TEST_SOURCES`, and uncomment the registrations.

(Note: `crate_writer_validation_disabled_test` and `column_wrap_disabled_test`
are *active* tests despite "disabled" in their names — each verifies behavior
when a feature is turned off.)

## Large Test Fixtures

Test fixture files should stay under 50KB to avoid hitting `CrateReaderConfig.maxTokenLength` (default 64K). When the crate format stores string values as tokens, large strings can exceed this limit.

Files that were reduced to stay within bounds:

- `tests/usda/memory-budget-attr-customdata-001.usda` — blob reduced from 1.2MB to 32K
- `tests/usda/memory-budget-customdata-001.usda` — blob reduced from 1.2MB to 32K

Large fixture files outside `tests/usda/` and `tests/usdc/` (not affected by the token limit, listed for reference):

- `tests/feat/node-mtlx/RealisticScene.usda` (244K)
- `tests/usda/suzanne.usda` (148K) — geometry data, tokens are short
- `tests/feat/node-mtlx/ChainTest.usda` (84K)
- `tests/feat/node-mtlx/ExtractPatternTest.usda` (72K)

## Known Gaps

The current infrastructure has a few operational gaps worth keeping in mind:

- `ctest` can be fully configured even when the test executables are not yet built.
- `tests/tydra_to_renderscene/runner.py` and `tests/parse_usd/runner.py` are manual tools and do not currently fail the process based on collected failures.
- `tests/decompress-int` and `tests/pprint` are built outside the main `ctest` suite.
- Several standalone feature benchmarks and tools under `tests/feat/` (hash, tangent, tydra-mesh-build, zstdusd) use local Makefiles and are not part of CMake or `ctest`.
- The Python bindings test (`tests/python/test_basic.py`) is not integrated into `ctest`.
- Feature fixture directories (`lux/`, `node-mtlx/`, `skinning/`) provide test data but are not exercised by any automated runner.
- The experimental `next` module (`src/next/`, `tests/next/`) is a standalone CMake project excluded from `build/` `ctest` and the regression gate by design (`TINYUSDZ_NEXT_BUILD_TESTS=OFF`); its `assert()`-based tests are only meaningful in Debug builds. See [Experimental `next` library tests](#experimental-next-library-tests).
