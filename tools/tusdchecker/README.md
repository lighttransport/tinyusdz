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
build_ninja/tusdchecker --composed scene.usda
build_ninja/tusdchecker --groups package,crate scene.usdz
build_ninja/tusdchecker --arkit scene.usdz
build_ninja/tusdchecker --require-all-groups --all scene.usdz
build_ninja/tusdchecker --list-groups
build_ninja/tusdchecker --json -o report.json scene.usdz
build_ninja/tusdchecker material.mtlx
```

The default is equivalent to `--all`. `--arkit` adds the opt-in ARKit /
RealityKit delivery profile (`arkit` group + `core,geom,shade,package`), the
tinyusdz counterpart of `usdchecker --arkit`: Y-up stage metadata, the
ARKit prim-type whitelist, `id`-based UsdPreviewSurface / UsdUVTexture shading,
portable texture formats, normal-map scale/bias, resolvable material bindings,
and a `.usdc`-rooted package containing only layers and exr/jpg/jpeg/png
textures. See `doc/openusd-usdz.md` for the full rule mapping. `arkit` is a
delivery profile rather than a defect class, so it is *not* part of `--all`.
Reports use stable rule identifiers such
as `core.layer.defaultPrim`, `geom.mesh.topology.index`, and
`physics.joint.limit`. `--strict` makes warnings fail validation. Normal parsing
accepts safely-readable implementation extensions; `--strict-parse` additionally
rejects non-conforming or unsupported format data before semantic validation.
MaterialX checks cover XML parsing, material-to-shader references, terminal
connections, `MaterialXConfigAPI`, version/source metadata, and shader outputs.

Exit status is `0` for valid input, `1` for validation failure, and `2` for a
command-line, I/O, or parse error.

## Current scope

Validation is performed on the authored `next::Layer` by default. `--composed`
resolves external arcs, validates the flattened result, and reports composition
errors and cross-arc attribute/property-kind type conflicts; it still uses
authored variant selections rather than enumerating every possible variant
combination. Schema checks are structural and cover the schemas known to
TinyUSDZ; they are not a plugin registry. The `package` group checks USDZ root
ordering, store mode, encryption/data-descriptor policy, 64-byte alignment,
CRC-32, central-directory consistency, safe/unique paths, portable extensions,
and authored dependency containment, including dictionary, clip, property
metadata, and time-sampled asset values.
The `crate` group performs a bounded decode plus token/path/field/fieldset/spec
cross-table checks on direct USDC files and USDC entries in USDZ packages.
Automatic fixers and OpenUSD's dynamically discovered validator plugins remain
out of scope. `--require-all-groups` turns a requested but inapplicable group
into an error; JSON reports requested, checked, and skipped groups separately.
