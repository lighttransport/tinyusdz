# Headless Hardware-Accelerated Browser Checks

This note records the browser execution setup used for WebGL/WASM demo
regression checks when hardware acceleration is required.

## Prerequisites

- Vite dev server running from `web/js`, usually on port `5174`.
- Google Chrome or Chromium installed.
- NVIDIA driver visible from the test session.
- `xvfb-run` available when no interactive X display is attached.

Verify the GPU first:

```bash
nvidia-smi
```

## Launch Environment

Use the NVIDIA GLVND environment and force Chrome through ANGLE/Vulkan:

```bash
env \
  __NV_PRIME_RENDER_OFFLOAD=1 \
  __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  xvfb-run -a node path/to/browser-test.mjs
```

Recommended Chrome flags for Puppeteer or Playwright:

```js
[
  '--no-sandbox',
  '--disable-setuid-sandbox',
  '--disable-dev-shm-usage',
  '--ignore-gpu-blocklist',
  '--use-gl=angle',
  '--use-angle=vulkan',
  '--enable-features=Vulkan',
  '--enable-webgl',
  '--enable-gpu-rasterization',
  '--enable-zero-copy',
  '--window-size=1280,960',
  '--js-flags=--expose-gc'
]
```

Do not use `--disable-gpu`. Avoid SwiftShader unless the test explicitly wants
software rendering coverage.

## Runtime Verification

Inside the page, verify the renderer before trusting performance numbers:

```js
const canvas = document.querySelector('canvas');
const gl = canvas && (canvas.getContext('webgl2') || canvas.getContext('webgl'));
const ext = gl && gl.getExtension('WEBGL_debug_renderer_info');
console.log({
  vendor: gl && ext ? gl.getParameter(ext.UNMASKED_VENDOR_WEBGL) : '',
  renderer: gl && ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : ''
});
```

Expected renderer text should include NVIDIA and Vulkan/ANGLE. Also check
`nvidia-smi` while the run is active; Chrome should appear as a GPU client.

## Smoke Test Pattern

For demo pages that support `?uri=` and `?backend=`, use a fresh page per case:

1. Navigate to `http://localhost:5174/<demo>.html?uri=<model>&backend=<backend>`.
2. Wait for `window.renderComplete === true` or a visible error.
3. If the page reports texture progress, wait until the load panel no longer
   contains `Textures: ... loading`.
4. Capture:
   - visible file/status text,
   - load timing and memory panel,
   - FPS value when present,
   - console warnings/errors,
   - WebGL renderer info,
   - screenshot or canvas nonblank check.
5. Treat uncaught page errors and console errors as failures. Whitelist only
   expected unsupported-feature warnings with clear text.

Use generic fixture names in committed docs and scripts. Keep private asset
filenames in ignored local notes under `priv/`.
