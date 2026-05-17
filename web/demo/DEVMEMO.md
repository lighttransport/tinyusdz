# Development Memo: TinyUSDZ npm package with Vite

## Overview

The demos resolve TinyUSDZ from the npm package in `node_modules/tinyusdz`.
They should not symlink to the repository's `web/js` tree.

Current package target:

```bash
tinyusdz@0.9.9-rc1
```

## Setup

```bash
cd web/demo
npm install
npm run dev
```

The GitHub Pages workflow uses `bun install` followed by `bash web/site/build-pages.sh`.
That build path also resolves `tinyusdz` from `node_modules`.

## Vite Resolution

`vite.config.js` aliases `tinyusdz` to `node_modules/tinyusdz` so imports like:

```js
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
```

always come from the installed package. Vite emits the WASM files referenced by
the package's `tinyusdz.js` / `tinyusdz_64.js` modules into the static bundle.

## Updating TinyUSDZ

```bash
cd web/demo
npm install tinyusdz@<version> --save-exact
npm run build
```

Do not run a local symlink setup unless the demos are intentionally being moved
back to in-repo package development.
