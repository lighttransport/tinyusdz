# usd-wg/assets batch render — tusdview & tusdrender

Batch smoke-render results for the [usd-wg/assets](https://github.com/usd-wg/assets)
corpus rendered through both viewers. This is the "disk1" batch, wired as the
opt-in ctest `tusdview-usd-assets-disk1-smoke` and the wrapper script
`examples/tusdview/tests/run-usd-assets-disk1.sh`.

## Latest run — 2026-07-08

- **Corpus:** `/mnt/disk1/work/usd-assets` (a local usd-wg/assets checkout), 280
  USD-family files (`*.usd|*.usda|*.usdc|*.usdz`).
- **Modes (4):** tusdview `vk-raster`, tusdview `vk-rt`, tusdrender `tusdr-cpu`,
  tusdrender `tusdr-vk`.
- **Total renders:** 1120 (280 × 4).
- **Settings:** `--allow-parent-paths` on (usd-wg/assets uses `../` references),
  headless via Xvfb, 45s per-render timeout, 256×256.
- **Result:** **PASS** (exit 0) — 0 `timeout`, 0 `backend_error`, 0
  `degraded_material`.

### Status × mode

| Status                   | Total | vk-raster | vk-rt | tusdr-cpu | tusdr-vk |
|--------------------------|------:|----------:|------:|----------:|---------:|
| `rendered`               |   762 |       221 |   221 |       157 |      163 |
| `rendered_with_warnings` |   204 |        30 |    30 |        30 |      114 |
| `no_renderable`          |   146 |        28 |    28 |        90 |        — |
| `load_error`             |     8 |         1 |     1 |         3 |        3 |

Only `timeout`, `backend_error` fail the batch (crashes / hangs / backend
breakage). `load_error` / `no_renderable` are reported but not failed, because a
broad external corpus legitimately contains intentionally-invalid assets and
geometry-less layers.

### `load_error` (8) — all expected

Every failure is on a file literally named `*_invalid.usda` from the
`test_assets/foundation/stage_composition/` suite — **intentionally malformed
composition** whose purpose is to fail to compose:

- `payload/payload_invalid.usda`
- `references/reference_invalid.usda`
- `references_prim/reference_prim_in_other_file.usda`
- `subLayer/sublayer_invalid.usda`

tusdview and tusdrender split on *which* of these hard-error vs. load-empty
(their loaders classify invalid composition differently), but failing here is
the correct outcome — no action needed.

### `no_renderable` (146) — benign

Mostly geometry-less files: composition-doc `.usda`s (`CompositionPuzzles/…`),
materials-only sublayers (`example_materials.usda`), and camera layers
(`camera.usda`). tusdr-cpu reports more (90) because its next-loader treats some
sublayer-only files as empty where the raster path still finds fallback
geometry.

### Fixes validated by this run

Two tusdview/tydra fixes landed while standing up this batch, both confirmed
clean across all 1120 renders:

1. **Parent-relative asset paths** — usd-wg/assets uses `../` references.
   tusdview rejected them by default (security policy); the harness now passes
   `--allow-parent-paths`, and the tydra texture/light asset resolver honors the
   same policy (previously it hard-stripped `..` regardless). Result:
   `MaterialXTest/basicTextured.usda` `load_error → rendered`;
   `StandardShaderBall` textures resolve (`missing_textures 8 → 0`).
2. **NodeGraph-wrapped surface shaders** — MaterialX materials wrap the surface
   Shader inside a NodeGraph; the legacy `ConvertMaterial` required a direct
   Shader connection. It now follows NodeGraph passthroughs (authored output or
   child surface Shader) and demotes unresolved MaterialX terminals / fileless
   `<image>` nodes to warnings. Result: `StandardShaderBall`
   `degraded_material → rendered_with_warnings`; **0 degraded_material** in the
   whole corpus.

## Reproducing

Full batch (both tools, all modes), via the wrapper:

```bash
# builds under build/ ; usd-wg/assets checkout at /mnt/disk1/work/usd-assets
examples/tusdview/tests/run-usd-assets-disk1.sh
# narrow / redirect:
examples/tusdview/tests/run-usd-assets-disk1.sh --limit 20 --out /tmp/batch
examples/tusdview/tests/run-usd-assets-disk1.sh --modes vk-raster,tusdr-cpu
```

As an opt-in ctest (SKIPs unless the gate is set and the corpus exists):

```bash
TUSDVIEW_USD_ASSETS_DISK1=1 ctest -R tusdview-usd-assets-disk1 --output-on-failure
```

Both funnel into `examples/tusdview/tests/run-usd-assets-render-smoke.sh`, which
writes `results.tsv`, `results.json`, and per-render `.ppm`/`.png` + `.log` into
the output dir. Point `USD_ASSETS_ROOT` elsewhere to run against another corpus.
