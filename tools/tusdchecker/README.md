# tusdchecker

`tusdchecker` validates USDA, USDC, USDZ, and MaterialX `.mtlx` documents using
LightUSD's dependency-free `next` core. It runs AOUSD Core 1.0.1 semantic checks and the
available structural schema checks for UsdGeom/UsdSkel, UsdShade/MaterialX,
UsdLux, and UsdPhysics (including the supported physics extensions).

```sh
cmake -S . -B build_ninja -G Ninja -DLIGHTUSD_BUILD_TOOLS=ON
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
build_ninja/tusdchecker --dump-rules
build_ninja/tusdchecker --usdchecker-compat --composed scene.usda
build_ninja/tusdchecker --composed scene.usda                 # all variant combos
build_ninja/tusdchecker --composed --skip-variants scene.usda # authored selections
build_ninja/tusdchecker --variants shape:sphere,lod:high scene.usda
build_ninja/tusdchecker --variant-sets shape scene.usda
```

The default is equivalent to `--all`. `--arkit` adds the opt-in ARKit /
RealityKit delivery profile (`arkit` group + `core,geom,shade,package`), the
lightusd counterpart of `usdchecker --arkit`: Y-up stage metadata, the
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

## usdchecker parity

`tusdchecker` mirrors OpenUSD `usdchecker`'s CLI and rule coverage. The pxr flag
spellings are accepted as aliases (`--dumpRules`, `--includeKeywords`,
`--skipVariants`, `--rootPackageOnly`, `--noAssetChecks`,
`--disableVariantValidationLimit`), and pxr validator-keyword names map onto
rule groups in `-g`/`--include-keywords` (for example `UsdGeomValidators` →
`geom`, `UsdzValidators` → `package,crate`).

- `--dump-rules` prints every rule id with its group and a one-line doc
  (the counterpart of `usdchecker --dumpRules`).
- With `--composed`, every combination of authored variant selections is
  composed and validated (like usdchecker's default), deduplicating repeated
  findings; the sweep is capped at 1000 combinations unless
  `--disable-variant-validation-limit` is given, and truncation is always
  reported. `--skip-variants` validates only the authored selections;
  `--variants set:variant,...` pins explicit selections (implies `--composed`);
  `--variant-sets a,b` restricts the sweep to the named sets.
- `--no-asset-checks` disables the defaultPrim presence rule
  (`core.layer.defaultPrim.missing`), matching `usdchecker --noAssetChecks`;
  the upAxis/metersPerUnit presence rules stay on, also matching pxr.
- `--root-package-only` skips dependency-following checks (nested layer type
  audits and `core.dependency.unresolvable`).
- Severity defaults are tusdchecker's own; `--usdchecker-compat` raises the
  rules usdchecker reports as errors (missing stage metadata, MaterialBindingAPI,
  unresolvable dependencies, encapsulation, sdr type mismatches, ...) to error
  severity so exit codes line up with pxr's.

`tests/run-tusdchecker-vs-usdchecker.sh` is the differential parity harness: it
runs both checkers over `tests/usda` and fails if any pxr finding family has no
mapped tusdchecker finding on the same fixture. It runs in ctest as
`tusdchecker_vs_usdchecker` and self-skips when pxr is not installed.

## Current scope

Validation is performed on the authored `next::Layer` by default. `--composed`
resolves external arcs, validates the flattened result, and reports composition
errors and cross-arc attribute/property-kind type conflicts, and enumerates
every combination of authored variant selections (see "usdchecker parity"
above). Schema checks are structural and cover the schemas known to
LightUSD; they are not a plugin registry. The `package` group checks USDZ root
ordering, store mode, encryption/data-descriptor policy, 64-byte alignment,
CRC-32, central-directory consistency, safe/unique paths, portable extensions,
and authored dependency containment, including dictionary, clip, property
metadata, and time-sampled asset values.
The `crate` group performs a bounded decode plus token/path/field/fieldset/spec
cross-table checks on direct USDC files and USDC entries in USDZ packages.
Automatic fixers and OpenUSD's dynamically discovered validator plugins remain
out of scope. `--require-all-groups` turns a requested but inapplicable group
into an error; JSON reports requested, checked, and skipped groups separately.
