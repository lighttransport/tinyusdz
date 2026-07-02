# CLAUDE.md

See [AGENTS.md](AGENTS.md) for full project guidance (structure, build, test, conventions).

## Quick Reference

- build folder @build make with -j16
- wasm build folder @web/build
- native tests: `cd build && ctest --output-on-failure`
- roundtrip tests: `bash tests/run-usdcat-compare.sh`
- next-core (lean USD core): gate into main build with `-DTINYUSDZ_WITH_NEXT_CORE=ON` (links `next_core`, defines `TINYUSDZ_WITH_NEXT_CORE`; default OFF, legacy core untouched). Standalone build/tests live in `src/next` (`cmake -S src/next -B build-next -DTINYUSDZ_NEXT_BUILD_TESTS=ON`); see `scripts/run-next-checks.sh`.
