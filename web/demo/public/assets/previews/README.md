# Demo Portal Preview Images

Each demo page needs a preview image shown in the portal grid. Images are
referenced from `src/demo-configs.js` via the `image` property.

## Format

- **JPG** — actual browser screenshots at 640×360 (produced by `capture-previews.mjs`)
- **SVG** — hand-authored vector illustrations for newer demos (lighter, no browser needed)

## Naming Convention

`{demo-id}.{jpg|svg}` — where `demo-id` matches the `id` field in `demo-configs.js`.

## Creating a New Preview

For a browser-captured screenshot:
```bash
node scripts/capture-previews.mjs
```

For an SVG illustration, create `{demo-id}.svg` matching the dark theme:
- Background: `#18181b`
- Panel surfaces: `#101013` / `#202025` / `#1c1c20`
- Accent: `#b794f6`
- 640×360 viewBox
