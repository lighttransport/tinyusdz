# Repository Guidelines

## Project Structure & Module Organization
Core C++14 sources sit in `src/`, grouped by USD domains (`crate-*`, `usda-*`, `usdGeom`, `tydra`, etc.). Tests mirror production code: lightweight Acutest units live in `tests/unit/`, scenario runners in `tests/parse_usd/` and `tests/tydra_to_renderscene/`. Reference assets stay in `models/`, docs in `doc/`, and agent bootstrapping scripts under `scripts/`. Keep new tooling inside `sandbox/` or `container/` to avoid polluting release artifacts.

## Build, Test, and Development Commands
Configure with CMake presets for repeatable builds: `cmake --preset default_debug` then `cmake --build --preset default_debug`. Swift iteration uses Ninja multi-config via `cmake --preset ninja-multi`. Run the regression suite with `ctest --preset default_debug` or the helper `./run-timesamples-test.sh` when focusing on crate time-samples. Platform helpers (`scripts/bootstrap-cmake-linux.sh`, `vcsetup.bat`) prepare toolchains before invoking CMake.

## Coding Style & Naming Conventions
Formatting follows `.clang-format` (Google base, 2-space indent, attached braces, no tabs). Prefer `.cc`/`.hh` extensions, PascalCase for public types (`PODTimeSamples`), and camelCase for functions. Keep headers self-contained, avoid exceptions, and favor `nonstd::expected` for error propagation. Run `clang-format -i <files>` before committing.

## Testing Guidelines
Enable tests with `-DTINYUSDZ_BUILD_TESTS=ON` during configure; new units belong beside peers as `unit-*.cc` with matching `unit-*.h`. Exercise higher-level flows via the Python runners in `tests/parse_usd` and `tests/tydra_to_renderscene`. Include representative fixtures in `tests/data/` or reuse `models/`. Target full `ctest` runs prior to PRs and add focused scripts when touching performance-sensitive parsers.

## Commit & Pull Request Guidelines
Commits in this repo use concise, imperative subjects (e.g., "Optimize TimeSamples parsing") with optional detail in the body. Reference GitHub issues using `#123` when relevant and batch related changes together. Pull requests should summarize scope, list validation steps or command output, and attach screenshots for viewer/UI updates. Link CI results when available and request reviews from domain owners listed in CODEOWNERS (default to `dev` branch).

## Security & Configuration Tips
Parsing code defends against untrusted USD via memory budgets (`USDLoadOptions::max_memory_limit_in_mb`) and fuzz targets. Preserve these guards when modifying loaders, and surface new knobs through `scripts/` or `doc/`. Never commit private assets; place external samples under `sandbox/` with clear licensing notes.
