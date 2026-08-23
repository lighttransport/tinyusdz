# Reproducible Verification

The repository-wide verification harness is driven by
[`scripts/verify.sh`](../scripts/verify.sh). It separates preparation from
testing: preparation checks out the pinned external repositories and builds
ignored artifacts, while test actions consume that prepared cache with no
network updates.

## Clean full run

Use a new cache for an independent reproduction. The cache may be placed
elsewhere with `TINYUSDZ_VERIFY_CACHE`.

```bash
export TINYUSDZ_VERIFY_CACHE=/tmp/tinyusdz-verification-clean
scripts/verify.sh prepare --profile full
scripts/verify.sh test --profile full --offline --software
```

The preparation manifest is
[`tests/verification/manifest.json`](../tests/verification/manifest.json).
It pins MuJoCo, MuJoCo Menagerie, USD-WG assets, and the OpenUSD v26.05 oracle.
The web dependencies are locked by `web/js/package-lock.json` and installed
with `npm ci`.

The full profile covers:

- native CMake/CTest tests and the USD-WG corpus parser;
- the Debug `src/next` CMake/CTest suite;
- web Node/WASM tests, MuJoCo physics, Menagerie MJCF/USD/MJCF closure,
  five-model URDF browser rendering, and the full OffscreenCanvas worker sweep;
- USD-WG asset parsing; and
- TinyUSDZ/OpenUSD `usdcat` comparison and checker parity.

Browser phases need loopback access for Vite and Puppeteer. In restricted
sandboxes, run the test command with the environment's approved loopback
permission. Software mode avoids requiring a physical GPU.

## Focused preparation and tests

The same entrypoint supports smaller slices:

```bash
# MuJoCo checkout/build and direct physics smoke test.
scripts/verify.sh prepare --target mujoco-wasm
scripts/verify.sh test --target mujoco-wasm --offline

# Physics smoke plus all pinned Menagerie closure cases.
scripts/verify.sh test --target web-physics --offline --software

# Individual dependency preparation.
scripts/verify.sh prepare --target menagerie
scripts/verify.sh prepare --target assets
scripts/verify.sh prepare --target openusd
scripts/verify.sh prepare --target wasm --offline
```

Preparation scripts also expose checkout/build boundaries:

```bash
scripts/prepare-mujoco-wasm.sh --checkout-only
scripts/prepare-mujoco-wasm.sh --build-only --offline
scripts/prepare-usd-assets.sh --checkout-only
```

`scripts/prepare-mujoco-wasm.sh` checks out and builds the pinned manifest
ref into the verification cache. The pinned MuJoCo ref carries one local
patch, merged on the fork's `tinyusdz` branch: emscripten's `--emit-tsd` is
gated behind `MUJOCO_WASM_EMIT_TSD` (default OFF) because the typings codegen
shells out to `tsc --outFile`, which TypeScript 5.x removed. Point the
physics tests at the built artifacts with
`MUJOCO_WASM_DIR=<cache>/mujoco/wasm/dist`.

Use `scripts/verify.sh doctor --profile full` to check required host tools and
print the manifest digest and cache location. Each action writes a JSON report
under `<cache>/reports/`.

## Recorded clean-run result

On 2026-08-07, the full offline profile passed from a fresh cache:

- native CTest: 37/37 passed;
- next CTest: 34 passed, 1 intentional skip;
- Menagerie closure: 67/67 passed;
- URDF browser sweep: 5/5 passed;
- OffscreenCanvas worker sweep: 67/67 passed;
- USD-WG asset parser: 0 hard failures; and
- OpenUSD comparison: 446 compared, 0 failed, 2 documented skips.

Generated builds, downloaded repositories, browser output, reports, and corpus
results remain outside Git or are covered by repository ignore rules.
