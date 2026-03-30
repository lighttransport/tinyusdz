# CLAUDE.md

See [AGENTS.md](AGENTS.md) for full project guidance (structure, build, test, conventions).

## Quick Reference

- build folder @build make with -j16
- wasm build folder @web/build
- native tests: `cd build && ctest --output-on-failure`
- roundtrip tests: `bash tests/run-usdcat-compare.sh`
