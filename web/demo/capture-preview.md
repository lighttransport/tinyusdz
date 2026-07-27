# Capturing Demo Preview Images

Use the preview capture script when demo screenshots need to be refreshed for
`index.html`.

## Prerequisites

- Install the web demo dependencies in `web/demo`.
- Have Google Chrome or Chromium available in `PATH`.
- On Linux, install `xvfb-run` for the most reliable WebGL captures. The script
  uses it automatically when available.

## Capture

From the repository root:

```bash
npm --prefix web/demo run capture-previews
```

Or from `web/demo`:

```bash
npm run capture-previews
```

The script starts a temporary Vite server, opens each demo in Chrome through the
Chrome DevTools Protocol, waits for the scene to load, captures the viewport, and
then shuts the server and browser down.

## Outputs

Generated JPEG previews are written to:

```text
web/demo/public/assets/previews/<demo-id>.jpg
```

The script also updates `web/demo/src/demo-configs.js` so every demo card uses
its generated preview image. `web/demo/index.html` reads that config directly.

## Useful Overrides

```bash
TINYUSDZ_PREVIEW_WIDTH=1280 npm --prefix web/demo run capture-previews
TINYUSDZ_PREVIEW_HEIGHT=820 npm --prefix web/demo run capture-previews
TINYUSDZ_PREVIEW_WAIT_MS=120000 npm --prefix web/demo run capture-previews
CHROME_BIN=/path/to/chrome npm --prefix web/demo run capture-previews
TINYUSDZ_PREVIEW_NO_XVFB=1 npm --prefix web/demo run capture-previews
```

## Verify

```bash
npm --prefix web/demo run build
git diff -- web/demo/src/demo-configs.js web/demo/public/assets/previews
```

Open `web/demo/public/assets/previews/` or the demo index page to visually check
the generated images before committing.
