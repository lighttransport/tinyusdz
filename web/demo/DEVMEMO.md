# Development Memo: TinyUSDZ npm package with Vite

## Overview

Production builds resolve TinyUSDZ from the package in
`node_modules/tinyusdz`. The Vite development server resolves the local
`web/js/src/tinyusdz` modules instead so next-backend changes can be tested
before npm publication.

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

The `prepare:local-tinyusdz` script configures and incrementally builds:

* `web/build_ninja` for the local legacy module
* `web/build_next_ninja` for `tinyusdz_next`

Both targets emit their glue and WASM files into `web/js/src/tinyusdz`. Ninja
does no work when the outputs are current.

The GitHub Pages workflow uses `bun install` followed by
`bash web/site/build-pages.sh`. That production build still resolves
`tinyusdz` from `node_modules` and defaults to the legacy backend.

## Vite Resolution

`vite.config.js` selects the alias from the Vite command and mode. Development
uses `../js/src/tinyusdz`; production uses `node_modules/tinyusdz`, so imports
like:

```js
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
```

come from the selected source. Vite emits the WASM files referenced by the
selected module into the static bundle.

## Updating TinyUSDZ

```bash
cd web/demo
npm install tinyusdz@<version> --save-exact
npm run build
```

Do not symlink `node_modules/tinyusdz`; development source selection is handled
by Vite.
