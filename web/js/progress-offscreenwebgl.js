// progress-offscreenwebgl.js -- Main thread entry point for OffscreenCanvas progress demo.
//
// Responsibilities:
//   1. Check OffscreenCanvas support.
//   2. Transfer canvas control to a dedicated Web Worker.
//   3. Forward pointer / wheel / resize events to the worker.
//   4. Handle file-drop / file-picker -> send ArrayBuffer to worker.
//   5. Receive progress / status / loaded / error messages from worker -> update DOM.
//   6. Display progress bar, memory graph, and stage indicators.

// ============================================================================
// DOM references
// ============================================================================

const canvas = document.getElementById('gl');
const statusEl = document.getElementById('status');
const modelInfoEl = document.getElementById('model-info');
const meshCountEl = document.getElementById('mesh-count');
const materialCountEl = document.getElementById('material-count');
const textureCountEl = document.getElementById('texture-count');
const loadBtn = document.getElementById('load-btn');
const sampleBtn = document.getElementById('sample-btn');
const upAxisBtn = document.getElementById('upaxis-btn');
const fitBtn = document.getElementById('fit-btn');
const fileInput = document.getElementById('file-input');
const unsupportedOverlay = document.getElementById('unsupported-overlay');

// Sample models
const SAMPLE_MODELS = [
    'assets/WesternDesertTown2-mtlx.usdz'
];

// Scene state (tracked on main thread for UI)
const sceneState = {
    hasModel: false,
    upAxis: 'Y',
    applyUpAxisConversion: false
};

// ============================================================================
// Browser capability check
// ============================================================================

if (typeof canvas.transferControlToOffscreen !== 'function') {
    unsupportedOverlay.classList.add('visible');
    throw new Error('OffscreenCanvas not supported');
}

// ============================================================================
// Spawn the worker
// ============================================================================

const worker = new Worker(
    new URL('./progress-offscreenwebgl.worker.js', import.meta.url),
    { type: 'module' }
);

// ============================================================================
// Transfer canvas control
// ============================================================================

const offscreen = canvas.transferControlToOffscreen();

worker.postMessage(
    {
        type: 'init',
        canvas: offscreen,
        width: canvas.clientWidth,
        height: canvas.clientHeight,
        pixelRatio: window.devicePixelRatio || 1,
    },
    [offscreen]
);

// ============================================================================
// Progress UI Functions
// ============================================================================

let lastProgressUpdate = 0;
const PROGRESS_UPDATE_INTERVAL = 50;

function showProgress() {
    const container = document.getElementById('progress-container');
    container.classList.add('visible');
    resetProgressStages();
}

function hideProgress() {
    const container = document.getElementById('progress-container');
    container.classList.remove('visible');
}

function resetProgressStages() {
    const stageItems = document.querySelectorAll('.progress-stage-item');
    stageItems.forEach(item => {
        item.classList.remove('active', 'completed');
        const icon = item.querySelector('.stage-icon');
        icon.classList.remove('active', 'completed');
        icon.classList.add('pending');
    });

    const progressBar = document.getElementById('progress-bar');
    progressBar.style.transform = 'scaleX(0)';
    progressBar.classList.remove('complete');
    document.getElementById('progress-percentage').textContent = '0%';
    lastProgressUpdate = 0;
}

function updateProgressUI({ stage, percentage, message }) {
    const pct = Math.round(percentage);

    // Throttle updates
    const now = performance.now();
    if (now - lastProgressUpdate < PROGRESS_UPDATE_INTERVAL && pct < 100) {
        recordMemoryPoint(stage, pct);
        return;
    }
    lastProgressUpdate = now;

    // Update progress bar
    const progressBar = document.getElementById('progress-bar');
    progressBar.style.transform = `scaleX(${pct / 100})`;

    if (pct >= 100 || stage === 'complete') {
        progressBar.classList.add('complete');
    }

    document.getElementById('progress-percentage').textContent = `${pct}%`;

    const stageLabels = {
        'downloading': 'Downloading file...',
        'parsing': 'Parsing USD (Worker)...',
        'building': 'Building Three.js scene...',
        'textures': 'Processing textures...',
        'materials': 'Converting materials...',
        'complete': 'Complete!'
    };
    document.getElementById('progress-stage').textContent = stageLabels[stage] || stage;
    document.getElementById('progress-details').textContent = message || '';

    updateStageIcons(stage);
    recordMemoryPoint(stage, pct);

    console.log(`[Progress] ${stage}: ${pct}% - ${message || ''}`);
}

function updateStageIcons(currentStage) {
    const stageOrder = ['downloading', 'parsing', 'building', 'textures', 'materials', 'complete'];
    const currentIndex = stageOrder.indexOf(currentStage);

    stageOrder.forEach((stage, index) => {
        const item = document.querySelector(`.progress-stage-item[data-stage="${stage}"]`);
        if (!item) return;

        const icon = item.querySelector('.stage-icon');
        item.classList.remove('active', 'completed');
        icon.classList.remove('active', 'completed', 'pending');

        if (index < currentIndex) {
            item.classList.add('completed');
            icon.classList.add('completed');
            icon.textContent = '\u2713';
        } else if (index === currentIndex) {
            item.classList.add('active');
            icon.classList.add('active');
            icon.textContent = String(index + 1);
        } else {
            icon.classList.add('pending');
            icon.textContent = String(index + 1);
        }
    });
}

// ============================================================================
// Texture Progress UI
// ============================================================================

function showTextureProgress() {
    const container = document.getElementById('texture-progress-container');
    if (container) {
        container.classList.add('visible');
        const bar = document.getElementById('texture-progress-bar');
        if (bar) bar.style.transform = 'scaleX(0)';
    }
}

function hideTextureProgress() {
    const container = document.getElementById('texture-progress-container');
    if (container) container.classList.remove('visible');
}

function updateTextureProgressUI(info) {
    const { loaded, total, failed, percentage, currentTexture, isComplete } = info;

    const countEl = document.getElementById('texture-progress-count');
    if (countEl) {
        const failedText = failed > 0 ? ` (${failed} failed)` : '';
        countEl.textContent = `${loaded}/${total}${failedText}`;
    }

    const bar = document.getElementById('texture-progress-bar');
    if (bar) {
        bar.style.transform = `scaleX(${percentage / 100})`;
    }

    const detailsEl = document.getElementById('texture-progress-details');
    if (detailsEl) {
        if (isComplete) {
            detailsEl.textContent = failed > 0
                ? `Complete with ${failed} failures`
                : 'All textures loaded';
        } else if (currentTexture) {
            detailsEl.textContent = `Loading: ${currentTexture}`;
        } else {
            detailsEl.textContent = 'Starting texture loading...';
        }
    }

    if (isComplete) {
        setTimeout(() => hideTextureProgress(), 2000);
    }
}

// ============================================================================
// Memory Tracking
// ============================================================================

const memoryState = {
    available: false,
    dataPoints: [],
    maxDataPoints: 100,
    startTime: 0,
    canvas: null,
    ctx: null,
    colors: {
        used: '#4CAF50',
        total: '#2196F3',
        limit: 'rgba(244, 67, 54, 0.3)',
        grid: 'rgba(255, 255, 255, 0.1)',
        text: '#888',
        stageLine: 'rgba(255, 152, 0, 0.5)'
    }
};

function initMemoryGraph() {
    memoryState.canvas = document.getElementById('memory-graph');
    if (!memoryState.canvas) return;

    const rect = memoryState.canvas.parentElement.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    memoryState.canvas.width = rect.width * dpr;
    memoryState.canvas.height = 120 * dpr;
    memoryState.canvas.style.width = rect.width + 'px';
    memoryState.canvas.style.height = '120px';

    memoryState.ctx = memoryState.canvas.getContext('2d');
    memoryState.ctx.scale(dpr, dpr);

    memoryState.available = !!(performance && performance.memory);
    if (!memoryState.available) {
        document.getElementById('memory-warning').classList.add('visible');
    }
}

function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

function getMemoryUsage() {
    if (!memoryState.available) return { used: 0, total: 0, limit: 0 };
    const mem = performance.memory;
    return { used: mem.usedJSHeapSize, total: mem.totalJSHeapSize, limit: mem.jsHeapSizeLimit };
}

function recordMemoryPoint(stage, percentage) {
    const mem = getMemoryUsage();
    const time = performance.now() - memoryState.startTime;

    memoryState.dataPoints.push({ time, used: mem.used, total: mem.total, limit: mem.limit, stage, percentage });

    if (memoryState.dataPoints.length > memoryState.maxDataPoints) {
        memoryState.dataPoints.shift();
    }

    updateMemoryStats(mem);
    drawMemoryGraph();
}

function updateMemoryStats(mem) {
    const usedEl = document.getElementById('mem-used');
    const totalEl = document.getElementById('mem-total');
    const limitEl = document.getElementById('mem-limit');
    const pointsEl = document.getElementById('mem-points');

    if (usedEl) usedEl.textContent = formatBytes(mem.used);
    if (totalEl) totalEl.textContent = formatBytes(mem.total);
    if (limitEl) limitEl.textContent = formatBytes(mem.limit);
    if (pointsEl) pointsEl.textContent = memoryState.dataPoints.length;

    if (usedEl) {
        const usageRatio = mem.used / mem.limit;
        usedEl.classList.remove('warning', 'critical');
        if (usageRatio > 0.8) usedEl.classList.add('critical');
        else if (usageRatio > 0.6) usedEl.classList.add('warning');
    }
}

function drawMemoryGraph() {
    const ctx = memoryState.ctx;
    const cvs = memoryState.canvas;
    if (!ctx || !cvs) return;

    const width = cvs.width / (window.devicePixelRatio || 1);
    const height = cvs.height / (window.devicePixelRatio || 1);
    const padding = { top: 10, right: 10, bottom: 20, left: 50 };
    const graphWidth = width - padding.left - padding.right;
    const graphHeight = height - padding.top - padding.bottom;

    ctx.fillStyle = '#111';
    ctx.fillRect(0, 0, width, height);

    if (memoryState.dataPoints.length < 2) return;

    const points = memoryState.dataPoints;
    const maxMem = Math.max(...points.map(p => Math.max(p.used, p.total)));
    const limitMem = points[0].limit || maxMem * 1.2;
    const yMax = Math.max(maxMem * 1.1, limitMem);
    const timeRange = points[points.length - 1].time - points[0].time;

    // Grid
    ctx.strokeStyle = memoryState.colors.grid;
    ctx.lineWidth = 0.5;
    for (let i = 0; i <= 4; i++) {
        const y = padding.top + (graphHeight / 4) * i;
        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(width - padding.right, y);
        ctx.stroke();
    }

    // Limit line
    if (limitMem > 0) {
        const limitY = padding.top + graphHeight * (1 - limitMem / yMax);
        ctx.strokeStyle = memoryState.colors.limit;
        ctx.lineWidth = 1;
        ctx.setLineDash([5, 5]);
        ctx.beginPath();
        ctx.moveTo(padding.left, limitY);
        ctx.lineTo(width - padding.right, limitY);
        ctx.stroke();
        ctx.setLineDash([]);
    }

    const drawLine = (dataKey, color) => {
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        points.forEach((point, i) => {
            const x = padding.left + (graphWidth * (point.time - points[0].time) / (timeRange || 1));
            const y = padding.top + graphHeight * (1 - point[dataKey] / yMax);
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        });
        ctx.stroke();
    };

    drawLine('total', memoryState.colors.total);
    drawLine('used', memoryState.colors.used);

    // Stage change markers
    let lastStage = null;
    ctx.strokeStyle = memoryState.colors.stageLine;
    ctx.lineWidth = 1;
    ctx.setLineDash([2, 2]);
    points.forEach((point) => {
        if (point.stage !== lastStage) {
            const x = padding.left + (graphWidth * (point.time - points[0].time) / (timeRange || 1));
            ctx.beginPath();
            ctx.moveTo(x, padding.top);
            ctx.lineTo(x, height - padding.bottom);
            ctx.stroke();
            lastStage = point.stage;
        }
    });
    ctx.setLineDash([]);

    // Y axis labels
    ctx.fillStyle = memoryState.colors.text;
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let i = 0; i <= 4; i++) {
        const y = padding.top + (graphHeight / 4) * i;
        const value = yMax * (1 - i / 4);
        ctx.fillText(formatBytes(value), padding.left - 5, y);
    }

    // Current stage label
    if (points.length > 0) {
        const lastPoint = points[points.length - 1];
        ctx.fillStyle = '#FF9800';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText(lastPoint.stage || '', width / 2, height - 5);
    }
}

function resetMemoryTracking() {
    memoryState.dataPoints = [];
    memoryState.startTime = performance.now();

    const usedEl = document.getElementById('mem-used');
    const totalEl = document.getElementById('mem-total');
    const limitEl = document.getElementById('mem-limit');
    const pointsEl = document.getElementById('mem-points');
    if (usedEl) usedEl.textContent = '--';
    if (totalEl) totalEl.textContent = '--';
    if (limitEl) limitEl.textContent = '--';
    if (pointsEl) pointsEl.textContent = '0';

    if (memoryState.ctx && memoryState.canvas) {
        const width = memoryState.canvas.width / (window.devicePixelRatio || 1);
        const height = memoryState.canvas.height / (window.devicePixelRatio || 1);
        memoryState.ctx.fillStyle = '#111';
        memoryState.ctx.fillRect(0, 0, width, height);
    }
}

// ============================================================================
// Toast
// ============================================================================

function showToast(message, duration = 3000) {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.classList.add('visible');
    setTimeout(() => toast.classList.remove('visible'), duration);
}

// ============================================================================
// UpAxis / Fit buttons
// ============================================================================

function updateUpAxisButton() {
    if (sceneState.upAxis === 'Z') {
        upAxisBtn.style.display = 'flex';
        if (sceneState.applyUpAxisConversion) {
            upAxisBtn.classList.add('active');
            upAxisBtn.title = 'Z-up to Y-up: ON (click to disable)';
        } else {
            upAxisBtn.classList.remove('active');
            upAxisBtn.title = 'Z-up to Y-up: OFF (click to enable)';
        }
    } else {
        upAxisBtn.style.display = 'none';
    }
}

function updateFitButton() {
    fitBtn.style.display = sceneState.hasModel ? 'flex' : 'none';
}

// ============================================================================
// Worker -> DOM message handler
// ============================================================================

worker.addEventListener('message', (e) => {
    const msg = e.data;

    switch (msg.type) {
        case 'status':
            statusEl.textContent = msg.message;
            statusEl.className = '';
            break;

        case 'progress':
            updateProgressUI({
                stage: msg.stage,
                percentage: msg.percentage,
                message: msg.message
            });
            break;

        case 'loaded':
            statusEl.textContent = `Loaded: ${msg.meshCount} meshes, ${msg.materialCount} materials`;
            statusEl.className = '';
            meshCountEl.textContent = msg.meshCount;
            materialCountEl.textContent = msg.materialCount;
            textureCountEl.textContent = msg.textureCount || 0;
            modelInfoEl.style.display = 'block';

            sceneState.hasModel = true;
            sceneState.upAxis = msg.upAxis || 'Y';
            sceneState.applyUpAxisConversion = (sceneState.upAxis === 'Z');
            updateUpAxisButton();
            updateFitButton();

            hideProgress();
            break;

        case 'texture_progress':
            if (msg.isStart) {
                showTextureProgress();
            }
            updateTextureProgressUI(msg);
            if (msg.isComplete) {
                textureCountEl.textContent = msg.loaded;
            }
            break;

        case 'error':
            statusEl.textContent = `Error: ${msg.message}`;
            statusEl.className = 'error';
            hideProgress();
            showToast(`Failed to load: ${msg.message}`);
            console.error('[Worker error]', msg.message);
            break;
    }
});

worker.addEventListener('error', (e) => {
    statusEl.textContent = `Worker error: ${e.message}`;
    statusEl.className = 'error';
    hideProgress();
    console.error('[Worker uncaught error]', e);
});

// ============================================================================
// Pointer events (forwarded to worker)
// ============================================================================

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

// ============================================================================
// Wheel (zoom)
// ============================================================================

canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    worker.postMessage({ type: 'wheel', deltaY: e.deltaY });
}, { passive: false });

// ============================================================================
// Resize
// ============================================================================

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

// ============================================================================
// File loading helpers
// ============================================================================

async function sendFileToWorker(file) {
    try {
        showProgress();
        resetMemoryTracking();
        updateProgressUI({ stage: 'downloading', percentage: 0, message: `Reading: ${file.name}...` });

        const arrayBuffer = await file.arrayBuffer();

        updateProgressUI({ stage: 'downloading', percentage: 30, message: `Sending to worker...` });

        worker.postMessage(
            { type: 'loadFile', data: arrayBuffer, filename: file.name },
            [arrayBuffer]
        );
    } catch (err) {
        statusEl.textContent = `Failed to read file: ${err.message}`;
        statusEl.className = 'error';
        hideProgress();
        console.error('[main] Failed to read file:', err);
    }
}

async function loadSampleModel() {
    const url = SAMPLE_MODELS[Math.floor(Math.random() * SAMPLE_MODELS.length)];

    showProgress();
    resetMemoryTracking();
    updateProgressUI({ stage: 'downloading', percentage: 0, message: `Downloading ${url}...` });

    try {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const contentLength = response.headers.get('content-length');
        const total = contentLength ? parseInt(contentLength, 10) : 0;

        let loaded = 0;
        const reader = response.body.getReader();
        const chunks = [];

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            loaded += value.length;

            const pct = total > 0 ? Math.round((loaded / total) * 100) : 0;
            updateProgressUI({
                stage: 'downloading',
                percentage: pct * 0.3,
                message: `Downloading... ${pct}%`
            });
        }

        const totalLength = chunks.reduce((acc, c) => acc + c.length, 0);
        const binary = new Uint8Array(totalLength);
        let offset = 0;
        for (const chunk of chunks) {
            binary.set(chunk, offset);
            offset += chunk.length;
        }

        updateProgressUI({ stage: 'downloading', percentage: 30, message: 'Sending to worker...' });

        worker.postMessage(
            { type: 'loadFile', data: binary.buffer, filename: url },
            [binary.buffer]
        );
    } catch (err) {
        statusEl.textContent = `Failed to download: ${err.message}`;
        statusEl.className = 'error';
        hideProgress();
        showToast(`Failed to load: ${err.message}`);
    }
}

// ============================================================================
// File picker
// ============================================================================

loadBtn.addEventListener('click', () => fileInput.click());

fileInput.addEventListener('change', async () => {
    const file = fileInput.files?.[0];
    if (!file) return;
    await sendFileToWorker(file);
    fileInput.value = '';
});

sampleBtn.addEventListener('click', () => loadSampleModel());

upAxisBtn.addEventListener('click', () => {
    sceneState.applyUpAxisConversion = !sceneState.applyUpAxisConversion;
    updateUpAxisButton();
    worker.postMessage({
        type: 'toggleUpAxis',
        apply: sceneState.applyUpAxisConversion
    });
});

fitBtn.addEventListener('click', () => {
    worker.postMessage({ type: 'fitCamera' });
    showToast('Camera fitted to scene');
});

// ============================================================================
// Drag-and-drop
// ============================================================================

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

    if (/\.(usd|usda|usdc|usdz)$/i.test(file.name)) {
        await sendFileToWorker(file);
    } else {
        showToast('Please drop a USD file (.usd, .usda, .usdc, .usdz)');
    }
});

// ============================================================================
// Initialization
// ============================================================================

initMemoryGraph();
