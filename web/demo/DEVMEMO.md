# Development Memo: Local TinyUSDZ Module Integration with Vite

## Overview

For local development, `node_modules/tinyusdz` is replaced with a symlink pointing to the local source at `../js/src/tinyusdz/`. Combined with `preserveSymlinks: true` in `vite.config.js`, this lets Vite serve files from the local source while resolving bare imports (`three`, `fzstd`) from the demo's `node_modules`.

## How It Works

1. **Symlink** replaces the npm-installed package with local source:
   ```
   node_modules/tinyusdz -> ../../js/src/tinyusdz
   ```
   The `setup:local` script first removes the installed `node_modules/tinyusdz` directory, then recreates it as a symlink.

2. **`preserveSymlinks: true`** in `vite.config.js` tells Vite to keep the `node_modules/tinyusdz/` path instead of following the symlink to the real path. This means bare imports like `three` and `fzstd` in `TinyUSDZLoader.js` resolve from the demo's `node_modules/`, not from the real file location.

3. **`resolve.alias`** maps `tinyusdz` → `node_modules/tinyusdz` so that `from 'tinyusdz/TinyUSDZLoader.js'` resolves correctly (the symlinked dir has no `package.json` with exports).

4. **WASM loading** works because `tinyusdz.js` uses `new URL('tinyusdz.wasm', import.meta.url)`, and the `.wasm` file is co-located with the `.js` file in the symlinked directory.

## Setup

```bash
cd web/demo
npm install          # install all deps (puts npm tinyusdz in node_modules)
npm run setup:local  # replace with symlink to local source
npm run dev          # start Vite dev server
```

Or manually:
```bash
ln -sfn ../../js/src/tinyusdz node_modules/tinyusdz
```

**Note:** `npm install` will overwrite the symlink with the npm package. Run `npm run setup:local` again after any `npm install`.

## Development Workflow

1. **Build WASM** (if needed):
   ```bash
   cd web && ./bootstrap-linux.sh && cd build && make
   ```
   Outputs `tinyusdz.js` and `tinyusdz.wasm` to `web/js/src/tinyusdz/`.

2. **Run demo dev server:**
   ```bash
   cd web/demo && npm run dev
   ```

3. **Edit JS helpers** in `web/js/src/tinyusdz/` — Vite detects changes and reloads.

4. **Rebuild WASM** after C++ changes — `cd web/build && make`, then refresh browser.

## Switching Back to npm Package

```bash
cd web/demo
npm install   # restores npm tinyusdz package
```

## WASM64 (Optional)

`tinyusdz_64.js` is the 64-bit WASM build. If it's not present, `TinyUSDZLoader.js` will warn and fall back to the 32-bit module automatically.

---
*Last updated: 2026-03-31*
