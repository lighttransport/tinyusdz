// progress-offscreenwebgl.worker.js -- Web Worker that owns the WebGL context
// via OffscreenCanvas, with progress reporting back to the main thread.
//
// Combines offscreengl.worker.js rendering with progress-demo.js progress stages.

// ---- document polyfill (must be at top) ----
if (typeof document === 'undefined') {
    globalThis.document = {
        createElement(tag) {
            if (tag === 'canvas') return new OffscreenCanvas(1, 1);
            return {
                style: {},
                appendChild() {},
                setAttribute() {},
                addEventListener() {},
                removeEventListener() {},
            };
        },
        createElementNS(_ns, tag) {
            return this.createElement(tag);
        },
    };
}

import * as THREE from 'three';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';
import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils, TextureLoadingManager } from './src/tinyusdz/TinyUSDZLoaderUtils.js';
import { setTinyUSDZ as setMaterialXTinyUSDZ } from './src/tinyusdz/TinyUSDZMaterialX.js';

// ---- Worker-compatible image loading patch ----
THREE.ImageLoader.prototype.load = function (url, onLoad, _onProgress, onError) {
    fetch(url)
        .then(r => {
            if (!r.ok) throw new Error(`HTTP ${r.status} loading ${url}`);
            return r.blob();
        })
        .then(blob => createImageBitmap(blob, {
            imageOrientation: 'flipY',
            colorSpaceConversion: 'none',
        }))
        .then(bitmap => { if (onLoad) onLoad(bitmap); })
        .catch(err => {
            console.warn('[Worker] ImageLoader error:', err);
            if (onError) onError(err);
        });
};

// ============================================================================
// Constants
// ============================================================================

const USE_MEMORY64    = false;
const CAMERA_FOV      = 45;
const CAMERA_NEAR     = 0.1;
const CAMERA_FAR      = 1000;
const CAMERA_PADDING  = 1.2;
const BG_COLOR        = 0x1a1a1a;

// ============================================================================
// State
// ============================================================================

const three = {
    renderer: null,
    scene: null,
    camera: null,
    pmremGenerator: null,
    envMap: null,
};

const loaderState = {
    loader: null,
    nativeLoader: null,
};

const sceneState = {
    root: null,
    materials: [],
    textureCache: new Map(),
    textureLoadingManager: null,
    textureCount: 0,
    meshCount: 0,
    upAxis: 'Y',
};

// Pre-allocated scratch vectors for hot-path (pointer move)
const _scratchRight = new THREE.Vector3();
const _scratchUp = new THREE.Vector3();
const _scratchDir = new THREE.Vector3();

// Manual orbit camera
const orbitState = {
    theta: 0.3,
    phi: 1.2,
    radius: 5,
    target: new THREE.Vector3(0, 0, 0),
    isDragging: false,
    isPanning: false,
    lastX: 0,
    lastY: 0,
};

// ============================================================================
// Main message dispatch
// ============================================================================

self.addEventListener('message', async (e) => {
    const msg = e.data;
    switch (msg.type) {
        case 'init':
            await handleInit(msg);
            break;
        case 'resize':
            handleResize(msg);
            break;
        case 'pointerdown':
            handlePointerDown(msg);
            break;
        case 'pointermove':
            handlePointerMove(msg);
            break;
        case 'pointerup':
            handlePointerUp(msg);
            break;
        case 'wheel':
            handleWheel(msg);
            break;
        case 'loadFile':
            await handleLoadFile(msg);
            break;
        case 'toggleUpAxis':
            handleToggleUpAxis(msg);
            break;
        case 'fitCamera':
            fitCameraToScene();
            break;
        default:
            console.warn('[Worker] Unknown message type:', msg.type);
    }
});

// ============================================================================
// Init
// ============================================================================

async function handleInit({ canvas, width, height, pixelRatio }) {
    sendStatus('Initializing renderer...');
    try {
        initThreeJS(canvas, width, height, pixelRatio);
    } catch (err) {
        sendError(`WebGL init failed: ${err.message}`);
        return;
    }

    sendStatus('Initializing TinyUSDZ WASM...');
    try {
        await initLoader();
    } catch (err) {
        sendError(`WASM init failed: ${err.message}`);
        return;
    }

    sendStatus('Loading environment...');
    await loadEnvironment();

    sendStatus('Ready - Load a USD file to begin');
    animate();
}

// ============================================================================
// Three.js setup
// ============================================================================

function initThreeJS(offscreenCanvas, width, height, pixelRatio) {
    three.scene = new THREE.Scene();
    three.scene.background = new THREE.Color(BG_COLOR);

    three.camera = new THREE.PerspectiveCamera(CAMERA_FOV, width / height, CAMERA_NEAR, CAMERA_FAR);
    three.camera.position.set(3, 2, 5);

    three.renderer = new THREE.WebGLRenderer({
        canvas: offscreenCanvas,
        antialias: true,
    });
    three.renderer.setSize(width, height, false);
    three.renderer.setPixelRatio(pixelRatio);
    three.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    three.renderer.toneMappingExposure = 1.0;
    three.renderer.outputColorSpace = THREE.SRGBColorSpace;

    // Lights
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.5);
    three.scene.add(ambientLight);
    const directionalLight = new THREE.DirectionalLight(0xffffff, 1.0);
    directionalLight.position.set(5, 10, 7.5);
    three.scene.add(directionalLight);

    // Grid
    const grid = new THREE.GridHelper(10, 10, 0x444444, 0x333333);
    three.scene.add(grid);

    three.pmremGenerator = new THREE.PMREMGenerator(three.renderer);
    three.pmremGenerator.compileEquirectangularShader();
}

// ============================================================================
// TinyUSDZ WASM loader
// ============================================================================

async function initLoader() {
    loaderState.loader = new TinyUSDZLoader(null, {
        maxMemoryLimitMB: 512,
        onTydraProgress: (info) => {
            const meshProgress = info.meshTotal > 0
                ? `${info.meshCurrent}/${info.meshTotal}`
                : '';
            const meshName = info.meshName ? info.meshName.split('/').pop() : '';

            sendProgress('parsing', 30 + (info.progress * 50),
                meshProgress
                    ? `Converting: ${meshProgress} ${meshName}`
                    : `Converting: ${info.stage}`
            );
        },
        onTydraComplete: (info) => {
            console.log(`[Worker Tydra] Complete: ${info.meshCount} meshes, ${info.materialCount} materials, ${info.textureCount} textures`);
            sendProgress('building', 80, `Building ${info.meshCount} meshes...`);
        }
    });
    await loaderState.loader.init({ useMemory64: USE_MEMORY64 });

    const wasmModule = loaderState.loader.native_;
    TinyUSDZLoaderUtils.setTinyUSDZ(wasmModule);
    setMaterialXTinyUSDZ(wasmModule);
}

// ============================================================================
// Environment (HDR)
// ============================================================================

async function loadEnvironment() {
    try {
        const hdrLoader = new HDRLoader();
        const texture = await hdrLoader.loadAsync('./assets/textures/goegap_1k.hdr');
        three.envMap = three.pmremGenerator.fromEquirectangular(texture).texture;
        texture.dispose();
    } catch {
        three.envMap = createGradientEnv();
    }
    applyEnv();
}

function createGradientEnv() {
    const oc = new OffscreenCanvas(256, 256);
    const ctx = oc.getContext('2d');
    const grad = ctx.createLinearGradient(0, 0, 0, 256);
    grad.addColorStop(0, '#ffffff');
    grad.addColorStop(0.5, '#cccccc');
    grad.addColorStop(1, '#666666');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 256, 256);

    const texture = new THREE.CanvasTexture(oc);
    texture.mapping = THREE.EquirectangularReflectionMapping;
    const envTexture = three.pmremGenerator.fromEquirectangular(texture).texture;
    texture.dispose();
    return envTexture;
}

function applyEnv() {
    three.scene.environment = three.envMap;
    three.scene.background = three.envMap;
    sceneState.materials.forEach(mat => {
        mat.envMap = three.envMap;
        mat.needsUpdate = true;
    });
}

// ============================================================================
// USD Loading with progress
// ============================================================================

async function handleLoadFile({ data, filename }) {
    sendStatus(`Loading: ${filename}...`);

    try {
        await loadUSDFromData(new Uint8Array(data), filename);
    } catch (err) {
        sendError(`Failed to load ${filename}: ${err.message}`);
        console.error('[Worker] loadFile error:', err);
    }
}

async function loadUSDFromData(data, filename) {
    clearScene();

    sendProgress('parsing', 30, `Parsing: ${filename}...`);

    // Parse USD — wrap in try/catch to detect WASM OOM
    let usd;
    try {
        usd = await new Promise((resolve, reject) => {
            loaderState.loader.parse(
                data,
                filename,
                resolve,
                reject
            );
        });
    } catch (err) {
        const msg = err?.message || String(err);
        if (msg.includes('resize_heap') || msg.includes('enlarge memory') ||
            msg.includes('Cannot enlarge') || msg.includes('OOM') ||
            msg.includes('out of memory') || msg.includes('the limit is')) {
            const sizeMB = Math.round(data.byteLength / (1024 * 1024));
            throw new Error(
                `Out of WASM memory loading ${filename} (${sizeMB} MB). ` +
                `The 32-bit WASM build is limited to ~2 GB heap. ` +
                `Try a smaller file, or rebuild with USE_MEMORY64=true for 64-bit WASM.`
            );
        }
        throw err;
    }

    loaderState.nativeLoader = usd;

    // Read metadata
    const metadata = usd.getSceneMetadata ? usd.getSceneMetadata() : {};
    sceneState.upAxis = metadata.upAxis || 'Y';

    // Build scene with progress
    sendProgress('building', 50, 'Building Three.js scene...');
    await buildSceneWithProgress(usd);

    // Try to load DomeLight environment
    try {
        const result = await TinyUSDZLoaderUtils.loadDomeLightFromUSD(
            usd,
            three.pmremGenerator
        );
        if (result) {
            three.envMap = result.texture;
            applyEnv();
        }
    } catch (err) {
        console.warn('[Worker] DomeLight load error (non-fatal):', err);
    }

    // Apply Z-up -> Y-up
    if (sceneState.root && sceneState.upAxis === 'Z') {
        sceneState.root.rotation.x = -Math.PI / 2;
    }

    fitCameraToScene();

    // Start delayed texture loading
    if (sceneState.textureLoadingManager && sceneState.textureLoadingManager.total > 0) {
        const texManager = sceneState.textureLoadingManager;
        console.log(`[Worker] Starting delayed texture loading: ${texManager.total} textures queued`);

        sendTextureProgress({ loaded: 0, total: texManager.total, percentage: 0, isStart: true });

        texManager.startLoading({
            onProgress: (info) => {
                sendTextureProgress(info);
            },
            onTextureLoaded: (material, _mapProperty, _texture) => {
                material.needsUpdate = true;
            },
            concurrency: 2,
            yieldInterval: 16
        }).then(status => {
            console.log(`[Worker] Texture loading complete: ${status.loaded}/${status.total}`);
            sceneState.textureCount = status.loaded;
            sendTextureProgress({
                loaded: status.loaded,
                total: status.total,
                failed: status.failed,
                percentage: 100,
                isComplete: true
            });
        }).catch(err => {
            console.error('[Worker] Texture loading error:', err);
        });
    }

    // Send loaded
    sendLoaded(
        sceneState.meshCount,
        sceneState.materials.length,
        sceneState.textureCount,
        sceneState.upAxis
    );
}

// ============================================================================
// Scene building with progress
// ============================================================================

async function buildSceneWithProgress(usd) {
    const rootNode = usd.getDefaultRootNode();
    sceneState.meshCount = 0;
    sceneState.textureCount = 0;
    sceneState.materials = [];
    sceneState.textureCache.clear();

    // Create texture loading manager for delayed texture loading
    sceneState.textureLoadingManager = new TextureLoadingManager();

    const totalMeshes = usd.numMeshes ? usd.numMeshes() : 0;

    sendProgress('building', 50, `Building Three.js meshes (0/${totalMeshes})...`);

    const defaultMtl = new THREE.MeshPhysicalMaterial({
        color: 0x888888,
        roughness: 0.5,
        metalness: 0.0,
        envMap: three.envMap,
    });

    if (rootNode) {
        sceneState.root = await TinyUSDZLoaderUtils.buildThreeNode(
            rootNode,
            defaultMtl,
            usd,
            {
                overrideMaterial: false,
                envMap: three.envMap,
                envMapIntensity: 1.0,
                preferredMaterialType: 'auto',
                textureCache: sceneState.textureCache,
                textureLoadingManager: sceneState.textureLoadingManager,
                onProgress: (info) => {
                    const mappedPercentage = 50 + (info.percentage * 0.3);
                    sendProgress('building', Math.min(80, mappedPercentage), info.message);
                }
            }
        );
    } else {
        sceneState.root = new THREE.Group();
    }

    three.scene.add(sceneState.root);

    // Count meshes & collect materials
    sendProgress('materials', 80, 'Counting materials...');
    const matSet = new Set();
    sceneState.root.traverse(obj => {
        if (obj.isMesh) {
            sceneState.meshCount++;
            if (obj.material) {
                const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
                mats.forEach(m => matSet.add(m));
            }
        }
    });
    sceneState.materials = Array.from(matSet);

    // Count textures
    sendProgress('textures', 85, `Counting textures from ${sceneState.materials.length} materials...`);
    const textureProps = ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'emissiveMap', 'aoMap', 'alphaMap'];
    for (const mat of sceneState.materials) {
        textureProps.forEach(prop => {
            if (mat[prop]) sceneState.textureCount++;
        });
    }

    sendProgress('complete', 100,
        `Complete! ${sceneState.meshCount} meshes, ${sceneState.materials.length} materials, ${sceneState.textureCount} textures`
    );
}

// ============================================================================
// Scene cleanup
// ============================================================================

function clearScene() {
    if (sceneState.root) {
        sceneState.root.traverse(obj => {
            if (obj.isMesh) {
                obj.geometry?.dispose();
                const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
                mats.forEach(m => m?.dispose());
            }
        });
        three.scene.remove(sceneState.root);
        sceneState.root = null;
    }

    sceneState.materials = [];
    sceneState.meshCount = 0;
    sceneState.textureCount = 0;
    sceneState.textureCache.forEach(t => t?.dispose());
    sceneState.textureCache.clear();

    if (sceneState.textureLoadingManager) {
        sceneState.textureLoadingManager.abort();
        sceneState.textureLoadingManager.reset();
        sceneState.textureLoadingManager = null;
    }

    if (loaderState.nativeLoader) {
        try { loaderState.nativeLoader.reset(); } catch (_) {
            try { loaderState.nativeLoader.clearAssets(); } catch (_2) { /* ignore */ }
        }
        loaderState.nativeLoader = null;
    }
}

// ============================================================================
// Camera
// ============================================================================

function fitCameraToScene() {
    if (!sceneState.root) return;

    sceneState.root.updateMatrixWorld(true);

    const box = new THREE.Box3().setFromObject(sceneState.root);
    if (box.isEmpty()) return;

    const size   = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());

    const sphereR = size.length() * 0.5;
    const fovRad  = CAMERA_FOV * (Math.PI / 180);
    const aspect  = three.camera.aspect;
    const hFov    = 2 * Math.atan(Math.tan(fovRad / 2) * aspect);
    const distV   = sphereR / Math.sin(fovRad / 2);
    const distH   = sphereR / Math.sin(hFov / 2);
    const dist    = Math.max(distV, distH) * CAMERA_PADDING;

    orbitState.target.copy(center);
    orbitState.radius = dist;
    orbitState.theta  = 0.3;
    orbitState.phi    = 1.2;

    three.camera.near = Math.max(0.01, dist / 100);
    three.camera.far  = Math.max(1000, dist * 10);
    three.camera.updateProjectionMatrix();

    updateCamera();
}

function updateCamera() {
    const { theta, phi, radius, target } = orbitState;
    const sinPhi = Math.sin(phi);
    const cosPhi = Math.cos(phi);
    const sinTheta = Math.sin(theta);
    const cosTheta = Math.cos(theta);

    three.camera.position.set(
        target.x + radius * sinPhi * sinTheta,
        target.y + radius * cosPhi,
        target.z + radius * sinPhi * cosTheta,
    );
    three.camera.lookAt(target);
}

// ============================================================================
// Pointer / orbit / pan handlers
// ============================================================================

function handlePointerDown({ x, y, button }) {
    orbitState.isDragging = button === 0;
    orbitState.isPanning  = button === 2;
    orbitState.lastX = x;
    orbitState.lastY = y;
}

function handlePointerMove({ x, y, buttons }) {
    if (buttons === 0) {
        orbitState.isDragging = false;
        orbitState.isPanning  = false;
        return;
    }

    const dx = x - orbitState.lastX;
    const dy = y - orbitState.lastY;
    orbitState.lastX = x;
    orbitState.lastY = y;

    if (orbitState.isDragging && buttons & 1) {
        orbitState.theta -= dx * 0.01;
        orbitState.phi    = Math.max(0.01, Math.min(Math.PI - 0.01, orbitState.phi + dy * 0.01));
    } else if (orbitState.isPanning && buttons & 2) {
        const panSpeed = orbitState.radius * 0.001;
        three.camera.getWorldDirection(_scratchDir);
        _scratchRight.crossVectors(_scratchDir.negate(), three.camera.up).normalize();
        _scratchUp.copy(three.camera.up).normalize();

        orbitState.target.addScaledVector(_scratchRight, -dx * panSpeed);
        orbitState.target.addScaledVector(_scratchUp,     dy * panSpeed);
    }

    updateCamera();
}

function handlePointerUp(_msg) {
    orbitState.isDragging = false;
    orbitState.isPanning  = false;
}

// ============================================================================
// Wheel (zoom)
// ============================================================================

function handleWheel({ deltaY }) {
    const zoomFactor = deltaY > 0 ? 1.1 : (1 / 1.1);
    orbitState.radius = Math.max(0.01, orbitState.radius * zoomFactor);
    updateCamera();
}

// ============================================================================
// Resize
// ============================================================================

function handleResize({ width, height, pixelRatio }) {
    three.camera.aspect = width / height;
    three.camera.updateProjectionMatrix();
    three.renderer.setSize(width, height, false);
    three.renderer.setPixelRatio(pixelRatio);
}

// ============================================================================
// UpAxis toggle
// ============================================================================

function handleToggleUpAxis({ apply }) {
    if (!sceneState.root) return;
    if (apply && sceneState.upAxis === 'Z') {
        sceneState.root.rotation.x = -Math.PI / 2;
    } else {
        sceneState.root.rotation.x = 0;
    }
}

// ============================================================================
// Render loop
// ============================================================================

function animate() {
    if (typeof requestAnimationFrame === 'function') {
        requestAnimationFrame(animate);
    } else {
        setTimeout(animate, 16);
    }
    three.renderer.render(three.scene, three.camera);
}

// ============================================================================
// Helpers: postMessage shortcuts
// ============================================================================

function sendStatus(message) {
    console.log('[Worker]', message);
    self.postMessage({ type: 'status', message });
}

function sendError(message) {
    console.error('[Worker] ERROR:', message);
    self.postMessage({ type: 'error', message });
}

function sendProgress(stage, percentage, message) {
    self.postMessage({ type: 'progress', stage, percentage, message });
}

function sendTextureProgress(info) {
    self.postMessage({ type: 'texture_progress', ...info });
}

function sendLoaded(meshCount, materialCount, textureCount, upAxis) {
    console.log(`[Worker] Loaded: ${meshCount} meshes, ${materialCount} materials, ${textureCount} textures, upAxis=${upAxis}`);
    self.postMessage({ type: 'loaded', meshCount, materialCount, textureCount, upAxis });
}

// ============================================================================
// Global error handlers (safety net for uncaught errors, e.g. Emscripten abort)
// ============================================================================

self.addEventListener('error', (e) => {
    const msg = e.message || 'Unknown worker error';
    console.error('[Worker] Uncaught error:', msg);
    self.postMessage({ type: 'error', message: msg });
});

self.addEventListener('unhandledrejection', (e) => {
    const msg = e.reason?.message || String(e.reason) || 'Unhandled rejection in worker';
    console.error('[Worker] Unhandled rejection:', msg);
    self.postMessage({ type: 'error', message: msg });
});
