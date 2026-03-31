// offscreengl.js — Main thread entry point for the OffscreenCanvas demo.
// Responsibilities:
//   1. Check OffscreenCanvas support.
//   2. Transfer canvas control to a dedicated Web Worker.
//   3. Forward pointer / wheel / resize events to the worker.
//   4. Handle file-drop / file-picker → send ArrayBuffer to worker.
//   5. Receive status / loaded / error messages from worker → update DOM.

const canvas = document.getElementById('gl');
const statusEl = document.getElementById('status');
const modelInfoEl = document.getElementById('model-info');
const meshCountEl = document.getElementById('mesh-count');
const materialCountEl = document.getElementById('material-count');
const upAxisEl = document.getElementById('up-axis');
const loadBtn = document.getElementById('load-btn');
const fileInput = document.getElementById('file-input');
const unsupportedOverlay = document.getElementById('unsupported-overlay');

// ─── Browser capability check ──────────────────────────────────────────────

if (typeof canvas.transferControlToOffscreen !== 'function') {
    unsupportedOverlay.classList.add('visible');
    throw new Error('OffscreenCanvas not supported');
}

// ─── Spawn the worker ──────────────────────────────────────────────────────

const worker = new Worker(
    new URL('./offscreengl.worker.js', import.meta.url),
    { type: 'module' }
);

// ─── Transfer canvas control ───────────────────────────────────────────────

const offscreen = canvas.transferControlToOffscreen();

worker.postMessage(
    {
        type: 'init',
        canvas: offscreen,
        width: canvas.clientWidth,
        height: canvas.clientHeight,
        pixelRatio: window.devicePixelRatio || 1,
    },
    [offscreen]   // <-- transferable list (zero-copy)
);

// ─── Worker → DOM message handler ─────────────────────────────────────────

worker.addEventListener('message', (e) => {
    const { type, message, meshCount, materialCount, upAxis } = e.data;

    if (type === 'status') {
        statusEl.textContent = message;
        statusEl.className = '';
    } else if (type === 'loaded') {
        statusEl.textContent = `Loaded: ${meshCount} meshes, ${materialCount} materials`;
        statusEl.className = '';
        meshCountEl.textContent = meshCount;
        materialCountEl.textContent = materialCount;
        upAxisEl.textContent = upAxis || 'Y';
        modelInfoEl.style.display = 'block';
    } else if (type === 'error') {
        statusEl.textContent = `Error: ${message}`;
        statusEl.className = 'error';
        console.error('[Worker error]', message);
    }
});

worker.addEventListener('error', (e) => {
    statusEl.textContent = `Worker error: ${e.message}`;
    statusEl.className = 'error';
    console.error('[Worker uncaught error]', e);
});

// ─── Pointer events (forwarded verbatim to worker) ─────────────────────────

canvas.addEventListener('pointerdown', (e) => {
    canvas.setPointerCapture(e.pointerId);
    worker.postMessage({ type: 'pointerdown', x: e.clientX, y: e.clientY, button: e.button });
});

canvas.addEventListener('pointermove', (e) => {
    worker.postMessage({ type: 'pointermove', x: e.clientX, y: e.clientY, buttons: e.buttons });
});

canvas.addEventListener('pointerup', (e) => {
    worker.postMessage({ type: 'pointerup', x: e.clientX, y: e.clientY });
});

canvas.addEventListener('pointercancel', (e) => {
    worker.postMessage({ type: 'pointerup', x: e.clientX, y: e.clientY });
});

// ─── Wheel (zoom) ──────────────────────────────────────────────────────────

canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    worker.postMessage({ type: 'wheel', deltaY: e.deltaY });
}, { passive: false });

// ─── Resize ────────────────────────────────────────────────────────────────

const resizeObserver = new ResizeObserver((entries) => {
    for (const entry of entries) {
        const { width, height } = entry.contentRect;
        worker.postMessage({
            type: 'resize',
            width,
            height,
            pixelRatio: window.devicePixelRatio || 1,
        });
    }
});

resizeObserver.observe(canvas);

// ─── File picker ───────────────────────────────────────────────────────────

loadBtn.addEventListener('click', () => fileInput.click());

fileInput.addEventListener('change', async () => {
    const file = fileInput.files?.[0];
    if (!file) return;
    await sendFileToWorker(file);
    fileInput.value = '';     // reset so the same file can be re-dropped
});

// ─── Drag-and-drop ─────────────────────────────────────────────────────────
// The canvas has no visual after control is transferred, so we attach
// drag listeners to document.body instead.

document.body.addEventListener('dragover', (e) => {
    e.preventDefault();
    document.body.classList.add('drag-over');
});

document.body.addEventListener('dragleave', (e) => {
    if (!e.relatedTarget || !document.body.contains(e.relatedTarget)) {
        document.body.classList.remove('drag-over');
    }
});

document.body.addEventListener('drop', async (e) => {
    e.preventDefault();
    document.body.classList.remove('drag-over');
    const file = e.dataTransfer?.files?.[0];
    if (!file) return;
    await sendFileToWorker(file);
});

// ─── Helper: read file and send to worker (zero-copy ArrayBuffer) ──────────

async function sendFileToWorker(file) {
    try {
        statusEl.textContent = `Reading: ${file.name}…`;
        const arrayBuffer = await file.arrayBuffer();
        // Transfer the ArrayBuffer (zero-copy — it becomes detached on this side)
        worker.postMessage(
            { type: 'loadFile', data: arrayBuffer, filename: file.name },
            [arrayBuffer]
        );
    } catch (err) {
        statusEl.textContent = `Failed to read file: ${err.message}`;
        statusEl.className = 'error';
        console.error('[main] Failed to read file:', err);
    }
}
