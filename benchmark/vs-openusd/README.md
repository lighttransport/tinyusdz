# lightusd vs OpenUSD benchmark harness

Compares parse, write (USDA/USDC), and composition performance between
lightusd and Pixar OpenUSD on the same input files.

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

If `pxr_DIR` is omitted (or OpenUSD is not found), only `bench_lightusd`
is built and the runner reports lightusd numbers alone.

## Run

```bash
python3 runner.py                  # default model set from ../../models
python3 runner.py path/to/scene.usdc --iters 20
```

Output is a per-file table of median times plus the OpenUSD/lightusd ratio
(>1 means lightusd is faster).

## What is measured

| op         | lightusd                                            | OpenUSD                              |
|------------|-----------------------------------------------------|--------------------------------------|
| parse      | `LoadUSDFromFile` → `Stage`                         | `SdfLayer::FindOrOpen` / `Reload(force)` (single layer, no composition) |
| write_usda | `usda::SaveAsUSDA`                                  | root layer `Export(*.usda)`          |
| write_usdc | `usdc::SaveAsUSDCToMemory`                          | root layer `Export(*.usdc)`          |
| composite  | `LoadLayerFromFile` + `CompositeSublayers/References/Payload` + `LayerToStage` | `UsdStage::Open(LoadAll)` + `Flatten` |

Caveats:

- The two libraries do different amounts of work per op (e.g. OpenUSD
  USDC "parse" is mostly mmap + lazy decode; lightusd fully decodes).
  Treat numbers as end-to-end workload comparisons, not parser microbenchmarks.
- lightusd writes USDC to memory (no file I/O); OpenUSD `Export` writes to disk.
- Requires the `pcp-2026` branch (or newer) of lightusd for crate write and
  the rvalue `LayerToStage` API; the harness builds lightusd as C++17.
- OpenUSD `Flatten` composes all arcs (variants, inherits, ...); the
  lightusd path composes sublayers/references/payload only.
