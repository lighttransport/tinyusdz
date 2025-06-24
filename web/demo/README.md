## Setup

### Build TinyUSDZ wasm module

See ../README.md for details.

## Run locally

Copy tinyusdz.js and tinyusdz.wasm from ../dist

Edit tinyusdz.js path to local file path in `main.js`

Run the server(We use bun + vite).

```
$ bun run dev
```

## Run with tinyusdz npm package with vite.

For some reason, vite cannot find tinyusdz.wasm file for caching(optimzieDeps).
Please `exclude` tinyusdz package in `vite.config.ts` or `vite.config.js` file as a work around.

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

## Demo info

UsdCookie.usdz : Each asset has a license declared in the readme, typically CC0 or something highly permissive

image is resized to 1024x1024.
