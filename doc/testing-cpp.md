# C++ Testing Guide

How to build, run, and extend the TinyUSDZ C++ test suite.

## Quick Start

```bash
# Build with tests enabled
mkdir build && cd build
cmake .. -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
make -j16

# Run all ctest-registered tests
ctest --output-on-failure
```

## Test Executables

| Executable | Source | Purpose |
|-----------|--------|---------|
| `unit-test-tinyusdz` | `tests/unit/unit-main.cc` + 20+ unit files | Acutest unit tests (140+ cases) |
| `test_tinyusdz` | `tests/test-main.cc` | Standalone USD file loader (used by Python runners) |
| `usda_roundtrip` | `tests/usda-roundtrip.cc` | USDA parse -> export -> reparse -> compare |
| `usdc_roundtrip` | `tests/usdc-roundtrip.cc` | USDA -> USDC -> reparse -> compare |
| `test-decompress-int` | `tests/decompress-int/` | Integer decompression tests |
| `pprint_benchmark` | `tests/pprint/` | Pretty-printer benchmark |
| `tydra_to_renderscene` | `examples/tydra_to_renderscene/` | Tydra Stage-to-RenderScene conversion (used by integration runner) |

## ctest-Registered Tests

These run when you invoke `ctest`:

| ctest Name | What It Runs |
|-----------|--------------|
| `unit-test-tinyusdz` | All Acutest unit tests (single executable) |
| `usda-parser-unit-test` | `python3 tests/usda/unit-runner.py` — runs `test_tinyusdz` on all `tests/usda/*.usda` files and `tests/usda/fail-case/*.usda` expected-failure files |
| `usdc-parser-unit-test` | `python3 tests/usdc/unit-runner.py` — runs `test_tinyusdz` on all `tests/usdc/*.usdc` files |
| `usda-roundtrip-test` | `python3 tests/usda/roundtrip-runner.py` — USDA export-reparse roundtrip on all `tests/usda/*.usda` |
| `usdc-roundtrip-test` | `python3 tests/usda/usdc-roundtrip-runner.py` — USDC write-reparse roundtrip on all `tests/usda/*.usda` |

```bash
# Run a specific ctest by name
ctest -R unit-test-tinyusdz --output-on-failure

# Run only roundtrip tests
ctest -R roundtrip --output-on-failure

# Parallel execution
ctest -j16 --output-on-failure

# Verbose output
ctest -V
```

## Unit Tests (Acutest)

Framework: [Acutest](https://github.com/mity/acutest) — single-header C/C++ unit test library (`tests/unit/acutest.h`).

### Structure

```
tests/unit/
  acutest.h              Framework header
  unit-common.hh         Shared helpers (temp files, comparison utils)
  unit-main.cc           Test registration (TEST_LIST array)
  unit-ascii-parse.cc    ASCII parser tests
  unit-crate-writer.cc   Crate writer tests (61 tests)
  unit-math.cc           Math/vector tests
  unit-xform.cc          Transform tests
  unit-stage.cc          Stage operations
  unit-materialx.cc      MaterialX tests
  unit-usda-roundtrip.cc USDA roundtrip tests
  ...                    (20+ files total)
```

### How test registration works

Each `unit-*.cc` file defines `#define TEST_NO_MAIN` before including `acutest.h`, then exports test functions declared in a corresponding `.h` header:

```cpp
// unit-foo.h
void foo_basic_test(void);
void foo_edge_case_test(void);

// unit-foo.cc
#define TEST_NO_MAIN
#include "acutest.h"
void foo_basic_test(void) {
  TEST_CHECK(some_condition);
  TEST_MSG("diagnostic info: %d", value);
}
```

`unit-main.cc` collects all tests into a single `TEST_LIST`:

```cpp
TEST_LIST = {
  {"foo_basic_test", foo_basic_test},
  {"foo_edge_case_test", foo_edge_case_test},
  // ...
  {nullptr, nullptr}  // sentinel
};
```

### Key assertion macros

| Macro | Behavior |
|-------|----------|
| `TEST_CHECK(cond)` | Check condition, continue on failure |
| `TEST_ASSERT(cond)` | Check condition, abort test on failure |
| `TEST_CHECK_(cond, fmt, ...)` | Check with custom message |
| `TEST_MSG(fmt, ...)` | Print diagnostic message |

### Running individual unit tests

The Acutest executable supports running individual tests by name:

```bash
# List all available tests
./build/unit-test-tinyusdz --list

# Run a single test
./build/unit-test-tinyusdz crate_writer_cone_test

# Run tests matching a pattern
./build/unit-test-tinyusdz crate_writer_*
```

### Adding a new unit test

1. Add the test function declaration to the relevant `.h` file (e.g., `unit-crate-writer.h`)
2. Implement the function in the corresponding `.cc` file
3. Register it in `unit-main.cc` by adding `{"test_name", test_function}` to `TEST_LIST`
4. Rebuild and run: `make -j16 && ctest -R unit-test-tinyusdz --output-on-failure`

## Roundtrip Comparison (tusdcat vs pxrUSD usdcat)

Compares TinyUSDZ `tusdcat` output against Pixar's `usdcat` for correctness. Requires both tools to be built/installed.

### Batch run

```bash
TUSDCAT_PATH=./build/tusdcat \
USDCAT_PATH=~/local/USD/dist/bin/usdcat \
  bash tests/run-usdcat-compare.sh
```

Options:
- `--no-detailed-diff` — summary only (faster)
- `--no-failure-summary` — skip the end-of-run failure list
- `--timeout MS` — per-file timeout (default 60000)

Results are saved to `tests/comparison-results/results_<timestamp>.log`.

### Single-file comparison

```bash
node tests/compare-usda.js \
  --tusdcat ./build/tusdcat \
  --usdcat ~/local/USD/dist/bin/usdcat \
  --detailed-diff \
  tests/usda/cube.usda
```

The comparison script (`tests/compare-usda.js`) does structural comparison at the Prim/Attribute level, ignoring ordering differences.

## Python Test Runners

These are called by ctest but can also be invoked directly:

```bash
# USDA parse tests (success + expected failure cases)
python3 tests/usda/unit-runner.py \
  --app ./build/test_tinyusdz \
  --basedir tests/usda

# USDA roundtrip
python3 tests/usda/roundtrip-runner.py \
  --app ./build/usda_roundtrip \
  --basedir tests/usda

# USDC roundtrip
python3 tests/usda/usdc-roundtrip-runner.py \
  --app ./build/usdc_roundtrip \
  --basedir tests/usda
```

## Integration Test Runners

These standalone Python runners exercise parsing and Tydra conversion on real USD files. They are **not** registered with ctest — run them manually.

### Parse test (`tests/parse_usd/runner.py`)

Runs `test_tinyusdz` on all USD/USDA/USDC/USDZ files under a directory. Requires the `test_tinyusdz` binary (or symlink) in the same directory as the runner.

```bash
cd tests/parse_usd
ln -sf ../../build/test_tinyusdz .
python3 runner.py ../../models
```

Reports success/failure per file. Exit code 0 even if some files fail — check the "Failure cases" section in output.

### Tydra conversion test (`tests/tydra_to_renderscene/runner.py`)

Runs `tydra_to_renderscene` on all USD files under `../models/`. Verifies that Stage-to-RenderScene conversion succeeds. Requires the `tydra_to_renderscene` binary (or symlink) in the same directory.

```bash
cd tests/tydra_to_renderscene
ln -sf ../../build/tydra_to_renderscene .
python3 runner.py
```

Uses case-insensitive glob matching for file extensions. Test data is in `tests/models/` (symlink or copy of `models/`).

## Test Data

| Directory | Contents |
|-----------|----------|
| `tests/usda/` | USDA test fixtures (success cases) |
| `tests/usda/fail-case/` | USDA expected-failure cases |
| `tests/usdc/` | USDC binary test fixtures |
| `tests/feat/` | Feature-specific test data and standalone test programs |
| `models/` | Larger USD test files for development |

## Fuzzing

Fuzzer targets are in `tests/fuzzer/` and use the Meson build system with libFuzzer:

- `tinyusdz_fuzzmain.cc` — main USD parser
- `usdaparser_fuzzmain.cc` — USDA parser
- `usdcparser_fuzzmain.cc` — USDC parser
- `lz4_decompress_fuzzmain.cc` — LZ4 decompression
- `intCoding_decompress_fuzzmain.cc` — integer decompression

## CMake Options

| Option | Default | Effect |
|--------|---------|--------|
| `TINYUSDZ_BUILD_TESTS` | ON (top-level) | Build test executables and register ctest targets |
| `TINYUSDZ_BUILD_EXAMPLES` | ON (top-level) | Build `tusdcat` and other example apps |
| `TINYUSDZ_WITH_MODULE_USDA_READER` | ON | Enable USDA parser tests |
| `TINYUSDZ_WITH_MODULE_USDC_READER` | ON | Enable USDC parser tests |
| `TINYUSDZ_WITH_MODULE_USDC_WRITER` | ON | Enable USDC roundtrip test |
| `TINYUSDZ_WITH_PXR_COMPAT_API` | OFF | Build pxrUSD compatibility API tests |
