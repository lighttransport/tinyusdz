# USD Assets Screenshots

Visual verification for the `usd-wg/assets` corpus renders `test_assets` through
the browser TinyUSDZ/tydra WebGL path and captures per-scene PNGs plus a preview
catalog.

```bash
cd web/js

# Default local checkout: /mnt/nvme02/work/usd/assets/test_assets
xvfb-run -a node tests/screenshot-usd-assets-batch.mjs --hw
npm run screenshot:usd-assets

# Full-asset roots (separate output recommended)
xvfb-run -a node tests/screenshot-usd-assets-batch.mjs \
  --hw --set full_assets --out tests/screenshots/usd-full-assets

# Software fallback, useful on hosts without xvfb or an NVIDIA Vulkan stack
node tests/screenshot-usd-assets-batch.mjs --sw --limit 4
npm run screenshot:usd-assets:sw -- --limit 4

# Explicit local files or HTTP URLs
node tests/screenshot-usd-assets-batch.mjs /path/to/Scene.usda
node tests/screenshot-usd-assets-batch.mjs \
  https://raw.githubusercontent.com/usd-wg/assets/main/test_assets/AlphaBlendModeTest/AlphaBlendModeTest.usd

# Discover local filenames but fetch them from raw GitHub
node tests/screenshot-usd-assets-batch.mjs \
  --remote-base https://raw.githubusercontent.com/usd-wg/assets/main/test_assets/
```

Outputs go to `tests/screenshots/usd-assets/` by default:

- one PNG per scene,
- `*.FAIL.png` for failed scenes,
- `summary.json`,
- `summary.tsv`,
- `catalog.png`.

`summary.tsv` and `summary.json` include a `kind` field on failures. Current
kinds are `parse-load`, `composition`, `convert-skel-animation`,
`convert-material-binding`, `convert-render-scene`, `blank-render`, and
`unknown`.

Catalog defaults are 4 columns and 2500 px wide. Override them with
`--catalog-columns <n>` and `--catalog-width <px>`, or disable catalog generation
with `--no-catalog`.

The default camera is a front-oblique, bbox-framed view matching the
`AlphaBlendModeTest/screenshots/20220603-usdview.png` style. Per-scene camera
overrides can be supplied with `--config`; the default config path is
`tests/usd-assets-camera.json` if present:

```json
{
  "defaults": {
    "camera": { "az": 3.14159, "el": 0.26, "fov": 50, "padding": 1.04 }
  },
  "scenes": {
    "AlphaBlendModeTest/AlphaBlendModeTest.usd": {
      "camera": { "az": 3.14159, "el": 0.22, "padding": 1.02 }
    }
  }
}
```

`--hw` uses the same Linux headless-GPU setup as
`tests/screenshot-urdf-batch.mjs`: Chrome runs `headless:false` inside
`xvfb-run -a` so ANGLE can use Vulkan/NVIDIA off-screen. If there is no
`DISPLAY` or NVIDIA Vulkan/EGL driver, the runner falls back to true-headless
SwiftShader.
