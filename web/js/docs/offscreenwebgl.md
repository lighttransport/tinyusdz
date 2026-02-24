# OffscreenCanvas + Web Worker + TinyUSDZ WASM

`offscreengl.*` — a USD viewer that runs all WebGL rendering and WASM
processing inside a dedicated Web Worker, leaving the main thread entirely
free for UI interactions.

---

## Motivation

The original `materialx.js` demo runs Three.js and the TinyUSDZ WASM module
on the **main thread**. This causes observable jank:

- The browser tab is unresponsive while Emscripten initialises the WASM heap
  (~200–400 ms on a fast desktop; 1–3 s on mobile).
- Parsing and converting a large USDZ file to a Three.js scene graph can
  block for hundreds of milliseconds.
- Even the steady-state `requestAnimationFrame` render loop competes with
  scroll/touch event processing.

`OffscreenCanvas` transfers the rendering surface to a Worker so that none of
this work touches the main-thread task queue.

---

## Architecture

```
┌──────────────────────────────────────────┐
│  Main Thread  (offscreengl.js)           │
│                                          │
│  <canvas id="gl">                        │
│   └─ transferControlToOffscreen() ──►──┐ │
│                                         │ │
│  DOM: info panel, toolbar, help text    │ │
│  Events: pointer / wheel / resize ──►──┤ │
│  File drop/pick → ArrayBuffer    ──►──┤ │
│  Worker messages ◄── status/loaded/err  │ │
└──────────────────────────────────────────┘
                         │ postMessage (structured-clone / transferable)
                         ▼
┌──────────────────────────────────────────┐
│  Web Worker  (offscreengl.worker.js)     │
│                                          │
│  THREE.WebGLRenderer({ canvas: offscreen })
│  PMREMGenerator  ──►  IBL env map        │
│  HDRLoader (fetch + createImageBitmap)   │
│                                          │
│  TinyUSDZLoader.init()  (WASM 32-bit)   │
│   └─ TinyUSDZLoaderNative.loadFromBinary │
│   └─ TinyUSDZLoaderUtils.buildThreeNode  │
│        (MaterialX / OpenPBR → THREE mat) │
│   └─ loadDomeLightFromUSD                │
│                                          │
│  Manual orbit camera                     │
│  requestAnimationFrame render loop       │
└──────────────────────────────────────────┘
```

### Message protocol

| Direction       | `type`        | Key payload fields                          |
|-----------------|---------------|---------------------------------------------|
| Main → Worker   | `init`        | `canvas: OffscreenCanvas, width, height, pixelRatio` |
| Main → Worker   | `resize`      | `width, height, pixelRatio`                 |
| Main → Worker   | `pointerdown` | `x, y, button`                              |
| Main → Worker   | `pointermove` | `x, y, buttons`                             |
| Main → Worker   | `pointerup`   | `x, y`                                      |
| Main → Worker   | `wheel`       | `deltaY`                                    |
| Main → Worker   | `loadFile`    | `data: ArrayBuffer *(transferred)*, filename` |
| Worker → Main   | `status`      | `message`                                   |
| Worker → Main   | `loaded`      | `meshCount, materialCount, upAxis`          |
| Worker → Main   | `error`       | `message`                                   |

The `OffscreenCanvas` and file `ArrayBuffer` are sent as **transferables**
(zero-copy), not cloned.

### Manual orbit camera

`OrbitControls` requires DOM event listeners and is not usable inside a
Worker. A small spherical-coordinate orbit is implemented directly:

```
position = target + (radius · sin(φ)sin(θ),  radius · cos(φ),  radius · sin(φ)cos(θ))
```

- Left-drag: update θ (azimuth) and φ (polar, clamped to [0.01, π−0.01])
- Right-drag: pan `target` along camera right/up vectors (speed ∝ `radius`)
- Scroll wheel: scale `radius` by ×1.1 or ×(1/1.1)

### Worker-side polyfills and patches

Two issues arise from moving Three.js into a Worker:

1. **`document` is undefined.** `TinyUSDZLoaderUtils` calls
   `document.createElement('canvas')` when building environment textures.
   A minimal `globalThis.document` stub is injected at the very top of the
   worker module (before any imports take effect at runtime).

2. **`HTMLImageElement` is undefined.** `THREE.ImageLoader` creates an `<img>`
   element via `document.createElementNS` and sets its `src`. In a Worker
   there is no HTML element API. `THREE.ImageLoader.prototype.load` is
   monkey-patched to use `fetch` + `createImageBitmap` instead.

   A subtle additional fix is required: Chrome's WebGL implementation
   **ignores `UNPACK_FLIP_Y_WEBGL`** when the pixel source is an
   `ImageBitmap` (it only honours the hint for `HTMLImageElement`).
   Three.js r183 sets the hint but does not have an ImageBitmap-specific
   code path. Without correction the texture appears vertically flipped.
   The fix is to pass `{ imageOrientation: 'flipY' }` to `createImageBitmap`
   so the pre-flip compensates for the ignored WebGL hint:

   ```
   HTMLImageElement path (main thread):
     pixels top→bottom  +  UNPACK_FLIP_Y=true  →  GPU bottom→top  ✓

   ImageBitmap path (worker, unfixed):
     pixels top→bottom  +  UNPACK_FLIP_Y=true (ignored)  →  GPU top→bottom  ✗

   ImageBitmap path (worker, fixed):
     createImageBitmap(blob, { imageOrientation:'flipY' })  →  pixels bottom→top
     +  UNPACK_FLIP_Y=true (ignored)  →  GPU bottom→top  ✓
   ```

### WASM loading

`USE_MEMORY64 = false` selects `tinyusdz.js` (32-bit WASM, ≤ 2 GB heap).
Set to `true` to use `tinyusdz_64.js` (64-bit WASM, ≤ 8 GB heap) when
loading very large scenes on capable browsers.

---

## OffscreenCanvas vs. SharedArrayBuffer

Both approaches can unblock the main thread for USD loading, but they have
very different requirements and trade-offs.

### OffscreenCanvas (this demo)

**This demo does NOT require `SharedArrayBuffer` or `crossOriginIsolated`.**

OffscreenCanvas transfers the `<canvas>` drawing surface to a Worker.
The Worker owns the WebGL context and drives its own `requestAnimationFrame`
loop entirely off the main thread. Communication is via `postMessage` with
structured-clone or transferable objects — no shared memory.

The `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy` headers
present in `vite.config.ts` are there for *other* demos in this repo that
use WASM threads or SharedArrayBuffer. The `offscreengl` demo itself does
not need them.

| Aspect | Detail |
|--------|--------|
| **SharedArrayBuffer required** | **No** |
| **crossOriginIsolated required** | **No** |
| **COOP/COEP headers required** | No (present in vite.config.ts for other demos) |
| **Rendering thread** | Worker — GPU commands issued from the Worker |
| **Main thread CPU during render** | Near-zero |
| **Main thread CPU during parse** | Near-zero |
| **Input latency** | +1 frame (~16 ms) — pointer events forwarded via postMessage |
| **DOM helpers (OrbitControls, GUI)** | Must be reimplemented or forwarded |

**Pros:**
- No shared-memory coordination — simpler concurrency model
- The entire rendering pipeline (parsing → scene build → render loop) is
  off the main thread; the page stays responsive even during heavy WASM work
- File `ArrayBuffer` transfer is zero-copy (transferable)
- `requestAnimationFrame` in the Worker is not subject to main-thread
  throttling by the browser's task scheduler

**Cons:**
- Input-to-render latency increases by up to one frame because pointer
  events travel through `postMessage`
- DOM-dependent Three.js helpers (`OrbitControls`, `lil-gui`, drag-drop
  handlers on the canvas) cannot run in the Worker and must be replaced
- Debugging is split across the page console and the Worker console
- Once `transferControlToOffscreen()` is called the canvas has no `.style`;
  resize must be explicitly forwarded via `postMessage`

---

### SharedArrayBuffer (alternative)

SharedArrayBuffer lets two threads share the same memory region directly.
The typical pattern:

1. A Worker writes parsed scene data (vertex buffers, transforms) into a
   `SharedArrayBuffer`.
2. The main thread reads from the same buffer and issues WebGL calls.
3. `Atomics.wait` / `Atomics.notify` synchronise access.

**SharedArrayBuffer requires `crossOriginIsolated = true`**, which in turn
requires both COOP and COEP response headers on every page that uses it.

| Aspect | Detail |
|--------|--------|
| **SharedArrayBuffer required** | **Yes** |
| **crossOriginIsolated required** | **Yes** (both COOP + COEP headers mandatory) |
| **Rendering thread** | Main thread retains the WebGL context |
| **Main thread CPU during render** | Reduced but not zero (GL calls remain on main thread) |
| **Main thread CPU during parse** | Near-zero (Worker does the parsing) |
| **Input latency** | Zero — events handled directly on main thread |
| **DOM helpers (OrbitControls, GUI)** | Work unmodified |

**Pros:**
- Zero extra input latency; pointer/scroll events go straight to the camera
- Existing rendering code changes minimally (main thread keeps WebGL context)
- OrbitControls, lil-gui, file drag-drop all work without changes
- Easier incremental migration from a single-threaded codebase

**Cons:**
- WebGL calls still run on the main thread — very heavy draw calls can still
  cause frame drops
- COOP/COEP headers break OAuth pop-ups (Google Sign-In, etc.),
  cross-origin `<iframe>` embeds, and CDN-hosted assets without a CORP
  header — can be difficult to deploy in production
- Manual synchronisation with `Atomics` is error-prone (deadlock, torn reads)
- WASM threads require a pthreads-enabled WASM build; the standard
  `tinyusdz.wasm` is single-threaded

---

### Comparison summary

| Feature | OffscreenCanvas (this demo) | SharedArrayBuffer |
|---------|-----------------------------|-------------------|
| Main thread free during render | ✓ | ✗ (GL calls remain) |
| Main thread free during parse/load | ✓ | ✓ |
| SharedArrayBuffer needed | **No** | **Yes** |
| crossOriginIsolated needed | **No** | **Yes** |
| COOP/COEP headers needed | No | Yes |
| Zero input latency | ✗ (+1 frame) | ✓ |
| DOM helpers work unmodified | ✗ (must reimplement) | ✓ |
| Zero-copy file transfer | ✓ (transferable) | ✓ (shared buffer) |
| Synchronisation complexity | Low (postMessage) | High (Atomics) |
| Works with single-threaded WASM | ✓ | ✓ (parsing only) |

For a USD viewer where the bottleneck is WASM parsing and Three.js scene
construction, **OffscreenCanvas is the better fit** — the entire pipeline
is off the main thread without any shared-memory complexity or cross-origin
isolation requirements.

---

## Browser support

### OffscreenCanvas

| Browser | Minimum version | Notes |
|---------|----------------|-------|
| Chrome / Edge (desktop) | 69 | Full support including Worker RAF |
| Firefox (desktop) | 105 | Full support |
| Safari (desktop) | 16.4 | macOS 13.3+ |
| Chrome for Android | 69 | Full support |
| **Safari on iOS** | **18.0** | iOS 18 (Sep 2024); **not available on iOS 17 or earlier** |
| Firefox for Android | 105 | Full support |
| Samsung Internet | 23 | Full support; partial in earlier versions |

> **iOS note:** Safari on iOS 17 and below does **not** support
> OffscreenCanvas in Workers. Users on iOS 17 will see the unsupported-browser
> overlay and a link to the main-thread `materialx.html` demo.

### `requestAnimationFrame` in a Worker with OffscreenCanvas

Required for the render loop. Available wherever OffscreenCanvas in Workers
is supported (same browser versions as above). Fallback to `setTimeout(fn, 16)`
is included in the worker for older environments.

### `createImageBitmap` with `imageOrientation`

Required for correct texture orientation in the Worker (see patch above).

| Browser | Minimum version |
|---------|----------------|
| Chrome / Edge | 68 |
| Firefox | 93 |
| Safari / iOS Safari | 15.4 |

All browsers that support OffscreenCanvas in Workers also support
`createImageBitmap` with `imageOrientation`, so no separate check is needed.

### Feature detection

`offscreengl.js` guards against unsupported browsers at startup:

```js
if (typeof canvas.transferControlToOffscreen !== 'function') {
    // Shows #unsupported-overlay with a link to materialx.html
}
```

---

## File structure

```
web/js/
├── offscreengl.html          HTML entry point (no importmap needed)
├── offscreengl.js            Main thread: canvas transfer + event forwarding
├── offscreengl.worker.js     Worker: Three.js + TinyUSDZ WASM + orbit camera
└── docs/
    └── offscreenwebgl.md     This document
```

## Related

- `materialx.html` / `materialx.js` — equivalent demo on the main thread
- `vite.config.ts` — COOP/COEP headers (required for other demos, not this one)
- `src/tinyusdz/TinyUSDZLoader.js` — WASM loader (`USE_MEMORY64` flag)
- `src/tinyusdz/TinyUSDZLoaderUtils.js` — scene graph builder
- `src/tinyusdz/TinyUSDZMaterialX.js` — MaterialX / OpenPBR material converter
