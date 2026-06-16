# tinyusdz vs OpenUSD benchmark harness

Compares parse, write (USDA/USDC), and composition performance between
tinyusdz and Pixar OpenUSD on the same input files.

## Prerequisites

A minimal (no Python, no imaging/MaterialX/OpenSubdiv) OpenUSD build:

```bash
git clone --depth 1 https://github.com/PixarAnimationStudios/OpenUSD.git ../../../OpenUSD
cd ../../../OpenUSD
python3 build_scripts/build_usd.py \
    --no-python --no-imaging --no-materialx \
    --no-examples --no-tutorials --no-tests --no-tools \
    ./install
```

## Build

```bash
cd benchmark/vs-openusd
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -Dpxr_DIR=$(cd ../../../OpenUSD/install && pwd)
cmake --build build -j
```

If `pxr_DIR` is omitted (or OpenUSD is not found), only `bench_tinyusdz`
is built and the runner reports tinyusdz numbers alone.

## Run

```bash
python3 runner.py                  # default model set from ../../models
python3 runner.py path/to/scene.usdc --iters 20
```

Output is a per-file table of median times plus the OpenUSD/tinyusdz ratio
(>1 means tinyusdz is faster).

## What is measured

| op         | tinyusdz                                            | OpenUSD                              |
|------------|-----------------------------------------------------|--------------------------------------|
| parse      | `LoadUSDFromFile` → `Stage`                         | `SdfLayer::FindOrOpen` / `Reload(force)` (single layer, no composition) |
| write_usda | `usda::SaveAsUSDA`                                  | root layer `Export(*.usda)`          |
| write_usdc | `usdc::SaveAsUSDCToMemory`                          | root layer `Export(*.usdc)`          |
| composite  | `LoadLayerFromFile` + `CompositeSublayers/References/Payload` + `LayerToStage` | `UsdStage::Open(LoadAll)` + `Flatten` |

Caveats:

- The two libraries do different amounts of work per op (e.g. OpenUSD
  USDC "parse" is mostly mmap + lazy decode; tinyusdz fully decodes).
  Treat numbers as end-to-end workload comparisons, not parser microbenchmarks.
- tinyusdz writes USDC to memory (no file I/O); OpenUSD `Export` writes to disk.
- Requires the `pcp-2026` branch (or newer) of tinyusdz for crate write and
  the rvalue `LayerToStage` API; the harness builds tinyusdz as C++17.
- OpenUSD `Flatten` composes all arcs (variants, inherits, ...); the
  tinyusdz path composes sublayers/references/payload only.
