# tusdchecker

`tusdchecker` validates USDA, USDC, USDZ, and MaterialX `.mtlx` documents using
TinyUSDZ's dependency-free `next` core. It runs AOUSD Core 1.0.1 semantic checks and the
available structural schema checks for UsdGeom/UsdSkel, UsdShade/MaterialX,
UsdLux, and UsdPhysics (including the supported physics extensions).

```sh
cmake -S . -B build_ninja -G Ninja -DTINYUSDZ_BUILD_TOOLS=ON
cmake --build build_ninja --target tusdchecker

build_ninja/tusdchecker scene.usdz
build_ninja/tusdchecker --core-only scene.usda
build_ninja/tusdchecker --groups core,geom,physics --strict scene.usdc
build_ninja/tusdchecker --json -o report.json scene.usdz
build_ninja/tusdchecker material.mtlx
```

The default is equivalent to `--all`. Reports use stable rule identifiers such
as `core.layer.defaultPrim`, `geom.mesh.topology.index`, and
`physics.joint.limit`. `--strict` makes warnings fail validation. Normal parsing
accepts safely-readable implementation extensions; `--strict-parse` additionally
rejects non-conforming or unsupported format data before semantic validation.
MaterialX checks cover XML parsing, material-to-shader references, terminal
connections, `MaterialXConfigAPI`, version/source metadata, and shader outputs.

Exit status is `0` for valid input, `1` for validation failure, and `2` for a
command-line, I/O, or parse error.

## Current scope

Validation is performed on the authored `next::Layer`, not every composed-stage
variant combination. Schema checks are structural and cover the schemas known
to TinyUSDZ; they are not a plugin registry. Next-core does not yet expose
Crate container tables, so low-level container validation is not an available
CLI group. Package layout/dependency portability checks and automatic fixers
from OpenUSD's `usdchecker` are also out of scope.
