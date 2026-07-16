# TinyUSDZ web demos

## Requirements

* Node.js/npm or Bun
* Vite (installed by the package manager)
* CMake, Ninja, and Emscripten (`emcmake`) for local development

## Setup

`npm install` or `bun install` to install tinyusdz npm package to `node_modules` folder.

## Run locally with the next backend

```bash
npm install
npm run dev
```

`npm run dev` incrementally builds the local legacy and next WASM modules and
serves JavaScript modules from `../js/src/tinyusdz`. The default backend is
`next`; use `?backend=legacy` or the Backend control to compare with the legacy
path. Generated builds live in `../build_ninja` and
`../build_next_ninja`.

The production `npm run build` path intentionally continues to use the pinned
`tinyusdz` npm dependency and the legacy backend until a next-capable package is
selected for GitHub Pages.

## Production build with the TinyUSDZ npm package

For some reason, vite cannot find tinyusdz.wasm file for caching(optimzieDeps).
Please `exclude` tinyusdz package to `vite.config.ts`(or `vite.config.js`) file as a work around.

```ts
import { defineConfig } from 'vite';

// https://vitejs.dev/config/
export default defineConfig({
  server: {
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
  optimizeDeps: {
    exclude: ['tinyusdz'],
  },
});
```

## Deploy

```
$ bun run build
(or vite build)
```

Content will be installed to ../dist

## Demo asset info

UsdCookie.usdz : Each asset has a license declared in the readme, typically CC0 or something highly permissive

image is resized to 1024x1024.
