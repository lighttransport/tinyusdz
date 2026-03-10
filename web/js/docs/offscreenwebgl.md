# OffscreenCanvas + Web Worker + TinyUSDZ WASM

`offscreengl.*` and `progress-offscreenwebgl.*` — USD viewers that run all
WebGL rendering and WASM processing inside a dedicated Web Worker, leaving
the main thread entirely free for UI interactions.

`progress-offscreenwebgl.*` extends the base demo with a full progress UI
(progress bar, stage indicators, memory graph, texture loading progress).

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

### Base demo (`offscreengl.*`)

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

### Progress demo (`progress-offscreenwebgl.*`)

Extends the base architecture with real-time progress reporting:

```
┌───────────────────────────────────────────────────┐
│  Main Thread  (progress-offscreenwebgl.js)        │
│                                                   │
│  <canvas id="gl">                                 │
│   └─ transferControlToOffscreen() ──►──┐          │
│                                         │          │
│  DOM: info panel, toolbar, help text   │          │
│  Progress: bar, stage icons, %         │          │
│  Texture progress: bar, count          │          │
│  Memory graph: canvas 2D, stats        │          │
│  Events: pointer / wheel / resize ──►──┤          │
│  File drop/pick → ArrayBuffer    ──►──┤          │
│                                         │          │
│  Worker messages ◄──┐                  │          │
│    status           │                  │          │
│    progress ────► updateProgressUI()   │          │
│    texture_progress ► updateTextureUI()│          │
│    loaded           │                  │          │
│    error            │                  │          │
└───────────────────────────────────────────────────┘
                         │ postMessage (structured-clone / transferable)
                         ▼
┌───────────────────────────────────────────────────┐
│  Web Worker  (progress-offscreenwebgl.worker.js)  │
│                                                   │
│  THREE.WebGLRenderer({ canvas: offscreen })       │
│  PMREMGenerator  ──►  IBL env map                 │
│                                                   │
│  TinyUSDZLoader.init({                            │
│    onTydraProgress: ──► sendProgress()            │
│    onTydraComplete: ──► sendProgress()            │
│  })                                               │
│                                                   │
│  C++ WASM (Tydra conversion)                      │
│   └─ DetailedProgressCallback                     │
│       └─ EM_JS reportTydraProgress()              │
│           └─ Module.onTydraProgress()             │
│               └─ self.postMessage({progress})     │
│                                                   │
│  TextureLoadingManager (delayed, async)           │
│   └─ onProgress ──► sendTextureProgress()         │
│                                                   │
│  Manual orbit camera                              │
│  requestAnimationFrame render loop                │
└───────────────────────────────────────────────────┘
```

### Message protocol (base demo)

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

### Additional messages (progress demo)

| Direction       | `type`             | Key payload fields                          |
|-----------------|--------------------|---------------------------------------------|
| Main → Worker   | `toggleUpAxis`     | `apply: boolean`                            |
| Main → Worker   | `fitCamera`        | *(none)*                                    |
| Worker → Main   | `progress`         | `stage, percentage, message`                |
| Worker → Main   | `texture_progress` | `loaded, total, failed, percentage, isStart, isComplete` |
| Worker → Main   | `loaded`           | `meshCount, materialCount, textureCount, upAxis` |

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

### WASM progress callback: coroutine vs. WebWorker

The TinyUSDZ WASM build has two mechanisms for reporting progress during
Tydra scene conversion:

**1. Synchronous EM_JS callbacks** (`binding.cc`)

```
C++ ConvertToRenderScene()
  → DetailedProgressCallback (per-mesh, synchronous)
    → EM_JS reportTydraProgress()
      → Module.onTydraProgress() in JS
```

These fire synchronously inside the WASM call stack. In JS they call
`self.postMessage()` which **queues** the message — the main thread
receives it only when the worker's event loop ticks.

**2. C++20 coroutine yields** (`co_await yieldToEventLoop()`)

Optional (`-DTINYUSDZ_WASM_COROUTINE=ON`, default OFF). Suspends the C++
coroutine between phases (format detection → parsing → setup → conversion →
complete), returning a Promise that resolves on `requestAnimationFrame`.
This lets the worker's event loop flush queued `postMessage` calls and
continue its render loop.

**Why coroutines are not required in the WebWorker configuration:**

On the **main thread** (e.g. `progress-demo.js`), the synchronous WASM call
blocks the event loop — `postMessage` from `reportTydraProgress` never
reaches the UI because the same thread is blocked. Coroutine `co_await`
yields were the only way to get intermediate progress updates.

In the **WebWorker + OffscreenCanvas** configuration, this problem disappears:

```
Worker thread (blocked by WASM):
  C++ Tydra → reportTydraProgress → Module.onTydraProgress
    → self.postMessage({type:'progress',...})       ← queued

Main thread (free, not blocked):
  worker.onmessage → updateProgressUI()             ← processes immediately
  → DOM updates: progress bar, stage icons, memory graph
```

The main thread is **never blocked by WASM** — it processes progress
messages as fast as the browser delivers them. Progress updates appear
smooth and responsive even without coroutine yields.

**When coroutines are still useful in a worker:**

If `TINYUSDZ_WASM_COROUTINE=ON`, the worker's own `requestAnimationFrame`
render loop can continue running between `co_await` suspension points. This
means the 3D viewport keeps rendering (e.g. showing a spinning grid) while
USD parsing is in progress. Without coroutines, the viewport freezes during
the synchronous `loadFromBinary` call since the worker's event loop is
blocked.

| | Without coroutines | With coroutines |
|---|---|---|
| Progress to main thread UI | Smooth | Smooth |
| Worker render loop during parse | **Frozen** | Keeps running |
| Build requirement | Default build | `TINYUSDZ_WASM_COROUTINE=ON` |
| API used | `loadFromBinary` (sync) | `loadFromBinaryAsync` (Promise) |

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
├── offscreengl.html                    Base demo: HTML entry point
├── offscreengl.js                      Base demo: main thread (canvas transfer + events)
├── offscreengl.worker.js               Base demo: worker (Three.js + WASM + orbit camera)
├── progress-offscreenwebgl.html        Progress demo: HTML with progress UI + memory panel
├── progress-offscreenwebgl.js          Progress demo: main thread (progress bar + memory graph)
├── progress-offscreenwebgl.worker.js   Progress demo: worker (+ Tydra progress + texture loading)
├── src/tinyusdz/
│   ├── TinyUSDZLoader.js               WASM loader (sync + coroutine async)
│   ├── TinyUSDZLoaderUtils.js          Scene graph builder
│   ├── TinyUSDZMaterialX.js            MaterialX / OpenPBR material converter
│   ├── TinyUSDZWorker.js               Parse-only worker (for progress-demo.js worker mode)
│   └── TinyUSDZWorkerLoader.js         Worker loader bridge (main-thread API)
└── docs/
    └── offscreenwebgl.md               This document
```

## Related

- `materialx.html` / `materialx.js` — equivalent demo on the main thread
- `progress-demo.html` / `progress-demo.js` — main-thread progress demo (with worker toggle)
- `vite.config.ts` — COOP/COEP headers (required for other demos, not this one)
- `web/binding.cc` — C++ WASM bindings (EM_JS progress callbacks, coroutine yields)
- `src/tydra/render-data.hh` — `DetailedProgressCallback` type definitions
- `web/CMakeLists.txt` — `TINYUSDZ_WASM_COROUTINE` build option
