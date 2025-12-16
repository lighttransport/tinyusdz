// TinyUSDZ MaterialX/OpenPBR Simple Demo with Three.js
// Simple viewer for USD files with MaterialX/OpenPBR and UsdPreviewSurface material support

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';
import { EXRLoader } from 'three/examples/jsm/loaders/EXRLoader.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_BACKGROUND_COLOR = 0x1a1a1a;
const CAMERA_PADDING = 1.2;

const ENV_PRESETS = {
    'usd_dome': 'usd',
    'goegap_1k': 'assets/textures/goegap_1k.hdr',
    'env_sunsky_sunset': 'assets/textures/env_sunsky_sunset.hdr',
    'studio': null,
    'constant_color': 'constant'
};

const TONE_MAPPINGS = {
    'none': THREE.NoToneMapping,
    'linear': THREE.LinearToneMapping,
    'reinhard': THREE.ReinhardToneMapping,
    'cineon': THREE.CineonToneMapping,
    'aces': THREE.ACESFilmicToneMapping,
    'agx': THREE.AgXToneMapping,
    'neutral': THREE.NeutralToneMapping
};

const TEXTURE_MAPS = [
    { prop: 'map', name: 'Base Color' },
    { prop: 'normalMap', name: 'Normal' },
    { prop: 'roughnessMap', name: 'Roughness' },
    { prop: 'metalnessMap', name: 'Metalness' },
    { prop: 'emissiveMap', name: 'Emissive' },
    { prop: 'aoMap', name: 'AO' },
    { prop: 'alphaMap', name: 'Alpha' },
    { prop: 'clearcoatMap', name: 'Clearcoat' },
    { prop: 'clearcoatRoughnessMap', name: 'Clearcoat Roughness' },
    { prop: 'sheenColorMap', name: 'Fuzz Color' },
    { prop: 'iridescenceMap', name: 'Iridescence' }
];

const DEFAULT_USDA_SCENE = `#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Sphere "Sphere"
    {
        double radius = 1.0
        rel material:binding = </World/_materials/DefaultMaterial>
    }

    def Scope "_materials"
    {
        def Material "DefaultMaterial"
        {
            token outputs:surface.connect = </World/_materials/DefaultMaterial/OpenPBRSurface.outputs:surface>

            def Shader "OpenPBRSurface"
            {
                uniform token info:id = "OpenPBRSurface"

                # Base layer - metallic gold
                color3f inputs:base_color = (0.9, 0.7, 0.3)
                float inputs:base_metalness = 0.8
                float inputs:base_weight = 1.0

                # Specular layer
                float inputs:specular_roughness = 0.3
                float inputs:specular_ior = 1.5
                float inputs:specular_weight = 1.0

                token outputs:surface
            }
        }
    }
}
`;

// ============================================================================
// Global State
// ============================================================================

// Three.js core objects
const threeState = {
    scene: null,
    camera: null,
    renderer: null,
    controls: null,
    pmremGenerator: null,
    envMap: null,
    clock: new THREE.Clock(),
    gridHelper: null,
    axesHelper: null
};

// USD loader state
const loaderState = {
    loader: null,
    nativeLoader: null
};

// Scene state
const sceneState = {
    root: null,
    materials: [],
    materialData: [],
    textureCache: new Map(),
    upAxis: 'Y',
    metadata: null,
    showingNormals: false,
    originalMaterialsMap: new Map(),
    domeLightData: null
};

// Picking state
const pickState = {
    raycaster: new THREE.Raycaster(),
    mouse: new THREE.Vector2(),
    selectedObject: null,
    selectionHelper: null
};

// Animation state
const animationState = {
    mixer: null,
    clips: [],
    action: null,
    params: {
        isPlaying: true,
        time: 0,
        beginTime: 0,
        endTime: 10,
        speed: 24.0,
        autoPlay: true
    }
};

// GUI state
const guiState = {
    gui: null,
    materialFolder: null,
    textureFolder: null,
    animationFolder: null,
    timeController: null,
    envPresetController: null
};

// User settings
const settings = {
    materialType: 'auto',
    envMapPreset: 'goegap_1k',
    envMapIntensity: 1.0,
    envConstantColor: '#ffffff',
    envColorspace: 'sRGB',
    showBackground: true,
    exposure: 1.0,
    toneMapping: 'aces',
    applyUpAxisConversion: false,
    showNormals: false,
    showGrid: false,
    showAxes: false,
    gridSize: 10,
    gridDivisions: 10
};

// ============================================================================
// Colorspace Utilities
// ============================================================================

/**
 * Convert sRGB component to linear
 */
function sRGBComponentToLinear(c) {
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

/**
 * Convert linear component to sRGB
 */
function linearComponentToSRGB(c) {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1.0 / 2.4) - 0.055;
}

/**
 * Parse hex color and convert to RGB [0, 1] with optional linear conversion
 */
function parseHexColor(hexColor, toLinear = false) {
    const hex = hexColor.replace('#', '');
    const r = parseInt(hex.substring(0, 2), 16) / 255;
    const g = parseInt(hex.substring(2, 4), 16) / 255;
    const b = parseInt(hex.substring(4, 6), 16) / 255;

    if (toLinear) {
        return {
            r: sRGBComponentToLinear(r),
            g: sRGBComponentToLinear(g),
            b: sRGBComponentToLinear(b)
        };
    }
    return { r, g, b };
}

/**
 * Convert RGB [0, 1] to hex color string
 */
function rgbToHex(r, g, b) {
    const toHex = (c) => {
        const clamped = Math.max(0, Math.min(1, c));
        return Math.round(clamped * 255).toString(16).padStart(2, '0');
    };
    return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

/**
 * Convert sRGB THREE.Color to linear
 */
function sRGBToLinear(color) {
    return new THREE.Color(
        sRGBComponentToLinear(color.r),
        sRGBComponentToLinear(color.g),
        sRGBComponentToLinear(color.b)
    );
}

/**
 * Convert linear THREE.Color to sRGB
 */
function linearToSRGB(color) {
    return new THREE.Color(
        linearComponentToSRGB(color.r),
        linearComponentToSRGB(color.g),
        linearComponentToSRGB(color.b)
    );
}

// ============================================================================
// Initialization
// ============================================================================

async function init() {
    initThreeJS();
    initControls();
    await initLoader();
    setupGUI();
    setupEventListeners();
    await loadEnvironment(settings.envMapPreset);
    await loadDefaultUSDFile();
    animate();
}

function initThreeJS() {
    threeState.scene = new THREE.Scene();
    threeState.scene.background = new THREE.Color(DEFAULT_BACKGROUND_COLOR);

    threeState.camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    threeState.camera.position.set(3, 2, 5);

    threeState.renderer = new THREE.WebGLRenderer({ antialias: true });
    threeState.renderer.setSize(window.innerWidth, window.innerHeight);
    threeState.renderer.setPixelRatio(window.devicePixelRatio);
    threeState.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    threeState.renderer.toneMappingExposure = settings.exposure;
    threeState.renderer.outputColorSpace = THREE.SRGBColorSpace;
    document.getElementById('canvas-container').appendChild(threeState.renderer.domElement);

    threeState.pmremGenerator = new THREE.PMREMGenerator(threeState.renderer);
    threeState.pmremGenerator.compileEquirectangularShader();
}

function initControls() {
    threeState.controls = new OrbitControls(threeState.camera, threeState.renderer.domElement);
    threeState.controls.enableDamping = true;
    threeState.controls.dampingFactor = 0.05;
    threeState.controls.screenSpacePanning = true;
    threeState.controls.minDistance = 0.1;
    threeState.controls.maxDistance = 500;
    threeState.controls.mouseButtons = {
        LEFT: THREE.MOUSE.ROTATE,
        MIDDLE: THREE.MOUSE.PAN,
        RIGHT: THREE.MOUSE.DOLLY
    };
    threeState.controls.keys = {
        LEFT: 'ArrowLeft',
        UP: 'ArrowUp',
        RIGHT: 'ArrowRight',
        BOTTOM: 'ArrowDown'
    };
    threeState.controls.enableKeys = true;
    threeState.controls.keyPanSpeed = 20.0;
}

async function initLoader() {
    updateStatus('Initializing TinyUSDZ WASM...');
    loaderState.loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });
    await loaderState.loader.init({ useMemory64: false });
    updateStatus('TinyUSDZ initialized');
}

function setupGUI() {
    guiState.gui = new GUI({ title: 'MaterialX Demo' });
    guiState.gui.domElement.style.position = 'absolute';
    guiState.gui.domElement.style.top = '10px';
    guiState.gui.domElement.style.right = '10px';
    guiState.gui.domElement.style.maxHeight = 'calc(100vh - 20px)';
    guiState.gui.domElement.style.overflowY = 'auto';

    setupSceneFolder();
    setupMaterialTypeFolder();

    guiState.materialFolder = guiState.gui.addFolder('Material Parameters');
    guiState.materialFolder.open();

    guiState.textureFolder = guiState.gui.addFolder('Textures');
    guiState.textureFolder.close();

    setupAnimationFolder();
}

function setupSceneFolder() {
    const sceneFolder = guiState.gui.addFolder('Scene');

    // Action buttons
    const actions = {
        fitToScene: fitCameraToScene,
        clearSelection: clearSelection
    };
    sceneFolder.add(actions, 'fitToScene').name('Fit to Scene');
    sceneFolder.add(actions, 'clearSelection').name('Clear Selection');

    guiState.envPresetController = sceneFolder.add(settings, 'envMapPreset', Object.keys(ENV_PRESETS))
        .name('Environment')
        .onChange(loadEnvironment);

    sceneFolder.addColor(settings, 'envConstantColor')
        .name('Env Color')
        .onChange(updateConstantColorEnvironment);

    sceneFolder.add(settings, 'envColorspace', ['sRGB', 'linear'])
        .name('Env Colorspace')
        .onChange(updateConstantColorEnvironment);

    sceneFolder.add(settings, 'envMapIntensity', 0, 1000, 0.1)
        .name('Env Intensity')
        .onChange(updateEnvIntensity);

    sceneFolder.add(settings, 'showBackground')
        .name('Show Background')
        .onChange(updateBackground);

    sceneFolder.add(settings, 'exposure', 0, 100, 0.1)
        .name('Exposure')
        .onChange(v => { threeState.renderer.toneMappingExposure = v; });

    sceneFolder.add(settings, 'toneMapping', Object.keys(TONE_MAPPINGS))
        .name('Tone Mapping')
        .onChange(v => { threeState.renderer.toneMapping = TONE_MAPPINGS[v] || THREE.ACESFilmicToneMapping; });

    sceneFolder.add(settings, 'applyUpAxisConversion')
        .name('Z-up to Y-up Fix')
        .onChange(applyUpAxisConversion);

    sceneFolder.add(settings, 'showNormals')
        .name('Show Normals')
        .onChange(toggleNormalDisplay);

    sceneFolder.add(settings, 'showGrid')
        .name('Show Grid')
        .onChange(toggleGrid);

    sceneFolder.add(settings, 'showAxes')
        .name('Show Axes')
        .onChange(toggleAxes);
}

function setupAnimationFolder() {
    guiState.animationFolder = guiState.gui.addFolder('Animation');

    guiState.animationFolder.add(animationState.params, 'isPlaying')
        .name('Play/Pause')
        .listen();

    guiState.timeController = guiState.animationFolder.add(animationState.params, 'time', 0, 10, 0.01)
        .name('Time')
        .listen()
        .onChange(scrubToTime);

    guiState.animationFolder.add(animationState.params, 'speed', 0.1, 120, 0.1)
        .name('Speed (FPS)')
        .listen();

    guiState.animationFolder.add(animationState.params, 'beginTime')
        .name('Begin Time')
        .listen()
        .disable();

    guiState.animationFolder.add(animationState.params, 'endTime')
        .name('End Time')
        .listen()
        .disable();

    guiState.animationFolder.close();
}

function setupMaterialTypeFolder() {
    const materialTypeFolder = guiState.gui.addFolder('Material Type');
    materialTypeFolder.add(settings, 'materialType', ['auto', 'openpbr', 'usdpreviewsurface'])
        .name('Preferred Type')
        .onChange(reloadMaterials);
    materialTypeFolder.open();
}

function setupEventListeners() {
    window.addEventListener('resize', onWindowResize);

    const fileInput = document.getElementById('file-input');
    fileInput.addEventListener('change', onFileSelect);

    const container = document.getElementById('canvas-container');
    container.addEventListener('dragover', onDragOver);
    container.addEventListener('drop', onFileDrop);
    container.addEventListener('dragleave', onDragLeave);

    // Object picking - use click event
    container.addEventListener('click', onCanvasClick);
}

// ============================================================================
// Animation Control
// ============================================================================

function scrubToTime(time) {
    if (!animationState.mixer || !animationState.action) return;

    const wasPaused = !animationState.params.isPlaying;

    animationState.mixer.timeScale = 1.0;
    animationState.mixer.time = 0;

    animationState.clips.forEach((clip) => {
        const action = animationState.mixer.clipAction(clip);
        action.paused = false;
        action.enabled = true;
        action.time = time;
        action.weight = 1.0;
    });

    animationState.mixer.update(0.0001);

    animationState.clips.forEach((clip) => {
        const action = animationState.mixer.clipAction(clip);
        action.time = time;
    });

    if (wasPaused) {
        animationState.clips.forEach((clip) => {
            const action = animationState.mixer.clipAction(clip);
            action.paused = true;
        });
    }
}

// ============================================================================
// USD Animation Extraction
// ============================================================================

function convertUSDAnimationsToThreeJS(usdLoader, root) {
    const clips = [];
    const numAnimations = usdLoader.numAnimations();

    if (numAnimations === 0) return clips;

    const nodeIndexMap = buildNodeIndexMap(root);

    for (let i = 0; i < numAnimations; i++) {
        const usdAnimation = usdLoader.getAnimation(i);
        const clip = convertSingleAnimation(usdAnimation, i, root, nodeIndexMap);
        if (clip) clips.push(clip);
    }

    return clips;
}

function buildNodeIndexMap(root) {
    const map = new Map();
    let index = 0;
    root.traverse((obj) => {
        map.set(index++, obj);
    });
    return map;
}

function convertSingleAnimation(usdAnimation, index, root, nodeIndexMap) {
    // Handle track-based animation (legacy format)
    if (usdAnimation.tracks && usdAnimation.tracks.length > 0) {
        return convertTrackBasedAnimation(usdAnimation, index, root);
    }

    // Handle channel-based animation
    if (usdAnimation.channels && usdAnimation.samplers) {
        return convertChannelBasedAnimation(usdAnimation, index, nodeIndexMap);
    }

    return null;
}

function convertTrackBasedAnimation(usdAnimation, index, root) {
    const keyframeTracks = [];
    const targetObject = findAnimationTarget(usdAnimation.name, root);
    const targetUUID = targetObject.uuid;

    for (const track of usdAnimation.tracks) {
        if (!track.times || !track.values) continue;

        const times = Array.isArray(track.times) ? track.times : Array.from(track.times);
        const values = Array.isArray(track.values) ? track.values : Array.from(track.values);
        const interpolation = getUSDInterpolationMode(track.interpolation);

        const keyframeTrack = createKeyframeTrack(track.path, targetUUID, times, values, interpolation);
        if (keyframeTrack) keyframeTracks.push(keyframeTrack);
    }

    if (keyframeTracks.length === 0) return null;

    return new THREE.AnimationClip(
        usdAnimation.name || `Animation_${index}`,
        usdAnimation.duration || -1,
        keyframeTracks
    );
}

function convertChannelBasedAnimation(usdAnimation, index, nodeIndexMap) {
    const nodeChannels = usdAnimation.channels.filter(channel => {
        const targetType = channel.target_type || 'SceneNode';
        return targetType === 'SceneNode';
    });

    if (nodeChannels.length === 0) return null;

    const keyframeTracks = [];

    for (const channel of nodeChannels) {
        const sampler = usdAnimation.samplers[channel.sampler];
        if (!sampler || !sampler.times || !sampler.values) continue;

        const targetObject = nodeIndexMap.get(channel.target_node);
        if (!targetObject) continue;

        const times = Array.isArray(sampler.times) ? sampler.times : Array.from(sampler.times);
        const values = Array.isArray(sampler.values) ? sampler.values : Array.from(sampler.values);
        const interpolation = getUSDInterpolationMode(sampler.interpolation);

        const keyframeTrack = createKeyframeTrack(channel.path, targetObject.uuid, times, values, interpolation);
        if (keyframeTrack) keyframeTracks.push(keyframeTrack);
    }

    if (keyframeTracks.length === 0) return null;

    return new THREE.AnimationClip(
        usdAnimation.name || `Animation_${index}`,
        usdAnimation.duration || -1,
        keyframeTracks
    );
}

function findAnimationTarget(animationName, root) {
    let targetObject = root;

    if (animationName) {
        let searchName = animationName.replace(/_xform$/, '').replace(/_anim$/, '');

        // Try exact match
        root.traverse((obj) => {
            if (obj.name === searchName) targetObject = obj;
        });

        // Try prefix match
        if (targetObject === root) {
            root.traverse((obj) => {
                if (obj.name && obj.name.startsWith(searchName)) targetObject = obj;
            });
        }
    }

    // Fallback to first mesh or group
    if (targetObject === root) {
        root.traverse((obj) => {
            if ((obj.isMesh || obj.isGroup) && obj !== root) {
                targetObject = obj;
                return;
            }
        });
    }

    return targetObject;
}

function createKeyframeTrack(path, targetUUID, times, values, interpolation) {
    const normalizedPath = path.toLowerCase();

    if (normalizedPath === 'translation') {
        return new THREE.VectorKeyframeTrack(`${targetUUID}.position`, times, values, interpolation);
    }
    if (normalizedPath === 'rotation') {
        return new THREE.QuaternionKeyframeTrack(`${targetUUID}.quaternion`, times, values, interpolation);
    }
    if (normalizedPath === 'scale') {
        return new THREE.VectorKeyframeTrack(`${targetUUID}.scale`, times, values, interpolation);
    }
    if (normalizedPath === 'weights') {
        return new THREE.NumberKeyframeTrack(`.uuid[${targetUUID}].morphTargetInfluences`, times, values, interpolation);
    }

    return null;
}

function getUSDInterpolationMode(interpolation) {
    const mode = (interpolation || '').toUpperCase();
    if (mode === 'STEP') return THREE.InterpolateDiscrete;
    if (mode === 'CUBICSPLINE') return THREE.InterpolateSmooth;
    return THREE.InterpolateLinear;
}

// ============================================================================
// Environment Loading
// ============================================================================

async function loadEnvironment(preset) {
    settings.envMapPreset = preset;
    const path = ENV_PRESETS[preset];

    if (!path) {
        threeState.envMap = createStudioEnvironment();
        applyEnvironment();
        return;
    }

    if (path === 'usd') {
        loadUSDDomeEnvironment();
        return;
    }

    if (path === 'constant') {
        threeState.envMap = createConstantColorEnvironment(settings.envConstantColor, settings.envColorspace);
        applyEnvironment();
        return;
    }

    updateStatus(`Loading environment: ${preset}...`);
    try {
        const hdrLoader = new HDRLoader();
        const texture = await hdrLoader.loadAsync(path);
        threeState.envMap = threeState.pmremGenerator.fromEquirectangular(texture).texture;
        texture.dispose();
        applyEnvironment();
        updateStatus('Environment loaded');
    } catch (error) {
        console.error('Failed to load environment:', error);
        updateStatus('Failed to load environment');
        threeState.envMap = createStudioEnvironment();
        applyEnvironment();
    }
}

function loadUSDDomeEnvironment() {
    if (sceneState.domeLightData && sceneState.domeLightData.envMap) {
        threeState.envMap = sceneState.domeLightData.envMap;
        settings.envMapIntensity = sceneState.domeLightData.intensity || 1.0;
        applyEnvironment();
        updateStatus('Using USD DomeLight environment');
    } else {
        updateStatus('No USD DomeLight available - using studio lighting');
        threeState.envMap = createStudioEnvironment();
        applyEnvironment();
    }
}

function createStudioEnvironment() {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 256;
    const ctx = canvas.getContext('2d');

    const gradient = ctx.createLinearGradient(0, 0, 0, 256);
    gradient.addColorStop(0, '#ffffff');
    gradient.addColorStop(0.5, '#cccccc');
    gradient.addColorStop(1, '#666666');
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, 256, 256);

    const texture = new THREE.CanvasTexture(canvas);
    texture.mapping = THREE.EquirectangularReflectionMapping;
    return threeState.pmremGenerator.fromEquirectangular(texture).texture;
}

function createConstantColorEnvironment(color, colorspace = 'sRGB') {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 256;
    const ctx = canvas.getContext('2d');

    let fillColor = color;
    if (colorspace === 'sRGB') {
        const rgb = parseHexColor(color, true);
        fillColor = rgbToHex(rgb.r, rgb.g, rgb.b);
    }

    ctx.fillStyle = fillColor;
    ctx.fillRect(0, 0, 256, 256);

    const texture = new THREE.CanvasTexture(canvas);
    texture.mapping = THREE.EquirectangularReflectionMapping;
    texture.colorSpace = THREE.LinearSRGBColorSpace;

    return threeState.pmremGenerator.fromEquirectangular(texture).texture;
}

function applyEnvironment() {
    threeState.scene.environment = threeState.envMap;
    updateBackground();
    updateEnvIntensity();

    sceneState.materials.forEach((mat) => {
        mat.envMap = threeState.envMap;
        mat.needsUpdate = true;
    });
}

function updateBackground() {
    threeState.scene.background = (settings.showBackground && threeState.envMap)
        ? threeState.envMap
        : new THREE.Color(DEFAULT_BACKGROUND_COLOR);
}

function updateConstantColorEnvironment() {
    if (settings.envMapPreset === 'constant_color') {
        threeState.envMap = createConstantColorEnvironment(settings.envConstantColor, settings.envColorspace);
        applyEnvironment();
    }
}

function updateEnvIntensity() {
    sceneState.materials.forEach(mat => {
        if (mat.envMapIntensity !== undefined) {
            mat.envMapIntensity = settings.envMapIntensity;
        }
    });
}

// ============================================================================
// DomeLight Loading (uses TinyUSDZLoaderUtils)
// ============================================================================

/**
 * Load DomeLight from USD and apply to scene
 * Uses TinyUSDZLoaderUtils.loadDomeLightFromUSD for the heavy lifting
 */
async function loadDomeLightFromUSD(usdLoader) {
    const result = await TinyUSDZLoaderUtils.loadDomeLightFromUSD(usdLoader, threeState.pmremGenerator);

    if (result) {
        // Apply result to app state
        threeState.envMap = result.texture;
        settings.envMapIntensity = result.intensity;
        settings.envMapPreset = 'usd_dome';

        if (result.colorHex) {
            settings.envConstantColor = result.colorHex;
        }

        applyEnvironment();

        // Store DomeLight data for reference
        sceneState.domeLightData = {
            name: result.name,
            textureFile: result.textureFile,
            envmapTextureId: result.envmapTextureId,
            intensity: result.intensity,
            color: result.color,
            exposure: result.exposure,
            envMap: threeState.envMap
        };

        return sceneState.domeLightData;
    }

    return null;
}

// ============================================================================
// UpAxis Conversion
// ============================================================================

/**
 * Initialize upAxis conversion setting based on USD file's upAxis
 * Called once when a new file is loaded
 */
function initUpAxisConversion() {
    // Automatically enable Z-up to Y-up conversion when USD file has upAxis = 'Z'
    if (sceneState.upAxis === 'Z') {
        settings.applyUpAxisConversion = true;
    } else {
        settings.applyUpAxisConversion = false;
    }

    // Update GUI checkbox if available
    if (guiState.gui) {
        guiState.gui.controllersRecursive().forEach(controller => {
            if (controller.property === 'applyUpAxisConversion') {
                controller.updateDisplay();
            }
        });
    }
}

function applyUpAxisConversion() {
    if (!sceneState.root) return;

    if (settings.applyUpAxisConversion && sceneState.upAxis === 'Z') {
        sceneState.root.rotation.x = -Math.PI / 2;
    } else {
        sceneState.root.rotation.x = 0;
    }
}

// ============================================================================
// Normal Display Mode
// ============================================================================

function toggleNormalDisplay() {
    if (!sceneState.root) return;

    if (settings.showNormals) {
        sceneState.showingNormals = true;
        sceneState.root.traverse(obj => {
            if (obj.isMesh && obj.material) {
                sceneState.originalMaterialsMap.set(obj, obj.material);
                obj.material = new THREE.MeshNormalMaterial({ flatShading: false });
            }
        });
    } else {
        sceneState.showingNormals = false;
        sceneState.root.traverse(obj => {
            if (obj.isMesh && sceneState.originalMaterialsMap.has(obj)) {
                obj.material = sceneState.originalMaterialsMap.get(obj);
            }
        });
        sceneState.originalMaterialsMap.clear();
    }
}

// ============================================================================
// Grid and Axes Helpers
// ============================================================================

function toggleGrid() {
    if (settings.showGrid) {
        if (!threeState.gridHelper) {
            createGridHelper();
        }
        threeState.gridHelper.visible = true;
    } else {
        if (threeState.gridHelper) {
            threeState.gridHelper.visible = false;
        }
    }
}

function toggleAxes() {
    if (settings.showAxes) {
        if (!threeState.axesHelper) {
            createAxesHelper();
        }
        threeState.axesHelper.visible = true;
    } else {
        if (threeState.axesHelper) {
            threeState.axesHelper.visible = false;
        }
    }
}

function createGridHelper() {
    const size = settings.gridSize;
    const divisions = settings.gridDivisions;

    threeState.gridHelper = new THREE.GridHelper(size, divisions, 0x444444, 0x222222);
    threeState.gridHelper.visible = settings.showGrid;
    threeState.scene.add(threeState.gridHelper);
}

function createAxesHelper() {
    const size = settings.gridSize / 2;

    threeState.axesHelper = new THREE.AxesHelper(size);
    threeState.axesHelper.visible = settings.showAxes;
    threeState.scene.add(threeState.axesHelper);
}

function updateHelpersSize() {
    // Update grid size based on scene bounds
    if (sceneState.root) {
        const box = new THREE.Box3().setFromObject(sceneState.root);
        const size = box.getSize(new THREE.Vector3());
        const maxDim = Math.max(size.x, size.y, size.z);

        // Set grid size to be larger than the scene
        settings.gridSize = Math.ceil(maxDim * 2);
        settings.gridDivisions = Math.min(20, Math.max(10, Math.ceil(settings.gridSize)));

        // Recreate helpers with new size
        if (threeState.gridHelper) {
            threeState.scene.remove(threeState.gridHelper);
            threeState.gridHelper.dispose();
            threeState.gridHelper = null;
            if (settings.showGrid) {
                createGridHelper();
            }
        }

        if (threeState.axesHelper) {
            threeState.scene.remove(threeState.axesHelper);
            threeState.axesHelper.dispose();
            threeState.axesHelper = null;
            if (settings.showAxes) {
                createAxesHelper();
            }
        }
    }
}

// ============================================================================
// USD Loading
// ============================================================================

async function loadDefaultScene() {
    updateStatus('Loading default scene...');
    const encoder = new TextEncoder();
    const data = encoder.encode(DEFAULT_USDA_SCENE);
    await loadUSDFromData(data, 'default.usda');
}

async function loadDefaultUSDFile() {
    const defaultFile = './assets/fancy-teapot-mtlx.usdz';
    updateStatus(`Loading ${defaultFile}...`);
    try {
        const response = await fetch(defaultFile);
        if (!response.ok) {
            throw new Error(`Failed to fetch ${defaultFile}: ${response.statusText}`);
        }
        const arrayBuffer = await response.arrayBuffer();
        const data = new Uint8Array(arrayBuffer);
        await loadUSDFromData(data, defaultFile);
    } catch (error) {
        console.error(`Failed to load default USD file (${defaultFile}):`, error);
        updateStatus(`Failed to load ${defaultFile}, loading fallback scene...`);
        await loadDefaultScene();
    }
}

async function loadUSDFromFile(file) {
    updateStatus(`Loading: ${file.name}...`);
    try {
        const arrayBuffer = await file.arrayBuffer();
        const data = new Uint8Array(arrayBuffer);
        await loadUSDFromData(data, file.name);
    } catch (error) {
        console.error('Failed to load USD file:', error);
        updateStatus(`Error: ${error.message}`);
    }
}

async function loadUSDFromData(data, filename) {
    clearScene();

    loaderState.nativeLoader = new loaderState.loader.native_.TinyUSDZLoaderNative();

    const success = loaderState.nativeLoader.loadFromBinary(data, filename);
    if (!success) {
        updateStatus(`Failed to parse USD file: ${filename}`);
        return;
    }

    loadSceneMetadata();
    await buildSceneGraph();
    await loadDomeLight();
    loadAnimations();
    initUpAxisConversion();
    applyUpAxisConversion();
    fitCameraToScene();
    updateHelpersSize();
    updateMaterialUI();
    updateModelInfo();
}

function loadSceneMetadata() {
    const metadata = loaderState.nativeLoader.getSceneMetadata ? loaderState.nativeLoader.getSceneMetadata() : {};
    sceneState.upAxis = metadata.upAxis || 'Y';
    sceneState.metadata = {
        upAxis: sceneState.upAxis,
        metersPerUnit: metadata.metersPerUnit || 1.0,
        framesPerSecond: metadata.framesPerSecond || 24.0,
        timeCodesPerSecond: metadata.timeCodesPerSecond || 24.0,
        startTimeCode: metadata.startTimeCode,
        endTimeCode: metadata.endTimeCode
    };
}

/**
 * Build scene graph using TinyUSDZLoaderUtils.buildThreeNode
 * This properly reflects USD Prim xformOps hierarchy
 */
async function buildSceneGraph() {
    // Clear state
    sceneState.materialData = [];
    sceneState.materials = [];
    sceneState.textureCache.clear();

    // Get the USD root node for hierarchy traversal
    const usdRootNode = loaderState.nativeLoader.getDefaultRootNode();
    if (!usdRootNode) {
        console.warn('No default root node found, falling back to flat mesh loading');
        await buildMeshesFallback();
        return;
    }

    // Create default material with environment map
    const defaultMtl = new THREE.MeshPhysicalMaterial({
        color: 0x888888,
        roughness: 0.5,
        metalness: 0.0,
        envMap: threeState.envMap,
        envMapIntensity: settings.envMapIntensity
    });

    // Build options for material conversion
    const options = {
        overrideMaterial: false,
        envMap: threeState.envMap,
        envMapIntensity: settings.envMapIntensity,
        preferredMaterialType: settings.materialType,
        textureCache: sceneState.textureCache
    };

    // Build Three.js scene graph from USD hierarchy
    sceneState.root = await TinyUSDZLoaderUtils.buildThreeNode(
        usdRootNode,
        defaultMtl,
        loaderState.nativeLoader,
        options
    );

    // Add to scene
    threeState.scene.add(sceneState.root);

    // Collect materials and material data from the scene graph
    collectMaterialsFromScene();

    // Store USD scene reference on meshes for material reloading
    sceneState.root.traverse((child) => {
        if (child.isMesh) {
            child.userData.usdScene = loaderState.nativeLoader;
        }
    });

    const numMeshes = loaderState.nativeLoader.numMeshes();
    const numMaterials = sceneState.materials.length;
    updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials (upAxis: ${sceneState.upAxis})`);
}

/**
 * Collect materials from the scene graph after buildThreeNode
 * This extracts materials from meshes for UI and management
 */
function collectMaterialsFromScene() {
    const materialSet = new Set();

    sceneState.root.traverse((obj) => {
        if (obj.isMesh && obj.material) {
            if (Array.isArray(obj.material)) {
                obj.material.forEach(mat => materialSet.add(mat));
            } else {
                materialSet.add(obj.material);
            }
        }
    });

    sceneState.materials = Array.from(materialSet);

    // Extract material data from userData if available
    sceneState.materialData = sceneState.materials.map(mat => {
        return mat.userData?.rawData || null;
    });
}

/**
 * Fallback to flat mesh loading if getDefaultRootNode is not available
 */
async function buildMeshesFallback() {
    const numMeshes = loaderState.nativeLoader.numMeshes();
    const numMaterials = loaderState.nativeLoader.numMaterials();

    // Pre-load materials
    for (let i = 0; i < numMaterials; i++) {
        try {
            const result = loaderState.nativeLoader.getMaterialWithFormat(i, 'json');
            if (!result.error) {
                sceneState.materialData.push(JSON.parse(result.data));
            }
        } catch (e) {
            console.warn(`Failed to get material ${i}:`, e);
        }
    }

    for (let i = 0; i < sceneState.materialData.length; i++) {
        const mat = await convertMaterial(sceneState.materialData[i], i);
        sceneState.materials.push(mat);
    }

    sceneState.root = new THREE.Group();
    threeState.scene.add(sceneState.root);

    for (let i = 0; i < numMeshes; i++) {
        const meshData = loaderState.nativeLoader.getMesh(i);
        if (!meshData) continue;

        const geometry = TinyUSDZLoaderUtils.convertUsdMeshToThreeMesh(meshData);
        if (!geometry) continue;

        const mesh = createMeshWithMaterialsFallback(geometry, meshData, i);
        mesh.name = meshData.name || `Mesh_${i}`;
        sceneState.root.add(mesh);
    }

    updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials (upAxis: ${sceneState.upAxis}) [fallback mode]`);
}

/**
 * Create mesh with materials for fallback mode
 */
function createMeshWithMaterialsFallback(geometry, meshData, index) {
    const submeshes = geometry.userData['submeshes'];

    if (submeshes && submeshes.length > 0) {
        const materials = [];
        const materialIdToIndex = new Map();

        for (const submesh of submeshes) {
            const matId = submesh.materialId;
            if (!materialIdToIndex.has(matId)) {
                const material = (matId >= 0 && matId < sceneState.materials.length)
                    ? sceneState.materials[matId]
                    : new THREE.MeshPhysicalMaterial({ color: 0x888888 });
                materialIdToIndex.set(matId, materials.length);
                materials.push(material);
            }
        }

        for (const submesh of submeshes) {
            const matIndex = materialIdToIndex.get(submesh.materialId);
            geometry.addGroup(submesh.start, submesh.count, matIndex);
        }

        return new THREE.Mesh(geometry, materials);
    }

    const material = (meshData.materialId !== undefined && meshData.materialId >= 0 && meshData.materialId < sceneState.materials.length)
        ? sceneState.materials[meshData.materialId]
        : new THREE.MeshPhysicalMaterial({ color: 0x888888 });

    return new THREE.Mesh(geometry, material);
}

async function loadDomeLight() {
    try {
        const domeLightData = await loadDomeLightFromUSD(loaderState.nativeLoader);
        if (domeLightData && guiState.envPresetController) {
            guiState.envPresetController.updateDisplay();
        }
    } catch (error) {
        console.warn('Error checking for DomeLight:', error);
    }
}

function loadAnimations() {
    try {
        animationState.clips = convertUSDAnimationsToThreeJS(loaderState.nativeLoader, sceneState.root);

        if (animationState.clips.length > 0) {
            animationState.mixer = new THREE.AnimationMixer(sceneState.root);

            updateAnimationParams();
            playAnimationClips();

            if (guiState.timeController) {
                guiState.timeController.max(animationState.params.endTime);
                guiState.timeController.updateDisplay();
            }

            if (guiState.animationFolder) {
                guiState.animationFolder.open();
            }

            const numMeshes = loaderState.nativeLoader.numMeshes();
            const numMaterials = loaderState.nativeLoader.numMaterials();
            updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials, ${animationState.clips.length} animations`);
        }
    } catch (error) {
        // Animation extraction not supported or no animations
    }
}

function updateAnimationParams() {
    const metadata = sceneState.metadata;

    if (metadata.startTimeCode !== undefined && metadata.endTimeCode !== undefined) {
        animationState.params.beginTime = metadata.startTimeCode;
        animationState.params.endTime = metadata.endTimeCode;
    } else if (animationState.clips.length > 0) {
        animationState.params.beginTime = 0;
        animationState.params.endTime = animationState.clips[0].duration;
    }

    animationState.params.speed = metadata.framesPerSecond || metadata.timeCodesPerSecond || 24.0;
    animationState.params.time = metadata.startTimeCode !== undefined ? metadata.startTimeCode : 0;
}

function playAnimationClips() {
    animationState.clips.forEach((clip) => {
        const action = animationState.mixer.clipAction(clip);
        action.loop = THREE.LoopRepeat;
        if (sceneState.metadata.startTimeCode !== undefined) {
            action.time = sceneState.metadata.startTimeCode;
        }
        action.play();
    });

    if (animationState.clips.length > 0) {
        animationState.action = animationState.mixer.clipAction(animationState.clips[0]);
    }

    threeState.clock.start();
}

function updateModelInfo() {
    const numMeshes = loaderState.nativeLoader.numMeshes();
    const numMaterials = loaderState.nativeLoader.numMaterials();

    document.getElementById('model-info').style.display = 'block';
    document.getElementById('mesh-count').textContent = numMeshes;
    document.getElementById('material-count').textContent = numMaterials;
}

async function convertMaterial(matData, index) {
    const typeInfo = TinyUSDZLoaderUtils.getMaterialType(matData);
    const typeString = TinyUSDZLoaderUtils.getMaterialTypeString(matData);

    try {
        const material = await TinyUSDZLoaderUtils.convertMaterial(
            matData,
            loaderState.nativeLoader,
            {
                preferredMaterialType: settings.materialType,
                envMap: threeState.envMap,
                envMapIntensity: settings.envMapIntensity,
                textureCache: sceneState.textureCache
            }
        );
        material.userData.typeInfo = typeInfo;
        material.userData.typeString = typeString;
        material.userData.rawData = matData;
        return material;
    } catch (e) {
        console.warn(`Failed to convert material ${index}:`, e);
        return new THREE.MeshPhysicalMaterial({
            color: 0x888888,
            roughness: 0.5,
            metalness: 0.0
        });
    }
}

async function reloadMaterials() {
    if (!sceneState.root) return;

    updateStatus('Reloading materials...');

    // Build a map from old material to new material for replacement
    const oldToNewMaterialMap = new Map();
    const newMaterialSet = new Set();

    // Traverse scene and reload each unique material
    for (const mat of sceneState.materials) {
        if (oldToNewMaterialMap.has(mat)) continue;

        // Get raw material data from userData (stored by buildThreeNode)
        const rawData = mat.userData?.rawData;
        if (rawData) {
            const newMat = await convertMaterial(rawData, sceneState.materials.indexOf(mat));
            oldToNewMaterialMap.set(mat, newMat);
            newMaterialSet.add(newMat);
        } else {
            // No raw data available, keep original material
            oldToNewMaterialMap.set(mat, mat);
            newMaterialSet.add(mat);
        }
    }

    // Replace materials on meshes
    sceneState.root.traverse(obj => {
        if (obj.isMesh && obj.material) {
            if (Array.isArray(obj.material)) {
                obj.material = obj.material.map(mat => oldToNewMaterialMap.get(mat) || mat);
            } else {
                const newMat = oldToNewMaterialMap.get(obj.material);
                if (newMat && newMat !== obj.material) {
                    obj.material = newMat;
                }
            }
        }
    });

    // Dispose old materials that were replaced
    for (const [oldMat, newMat] of oldToNewMaterialMap) {
        if (oldMat !== newMat && oldMat.dispose) {
            oldMat.dispose();
        }
    }

    // Update state
    sceneState.materials = Array.from(newMaterialSet);
    sceneState.materialData = sceneState.materials.map(mat => mat.userData?.rawData || null);

    updateMaterialUI();
    updateStatus('Materials reloaded');
}

function clearScene() {
    // Dispose normal visualization materials
    if (sceneState.showingNormals && sceneState.root) {
        sceneState.root.traverse(obj => {
            if (obj.isMesh && obj.material && obj.material.dispose) {
                obj.material.dispose();
            }
        });
    }
    sceneState.originalMaterialsMap.clear();
    sceneState.showingNormals = false;

    // Reset GUI checkbox
    if (settings.showNormals) {
        settings.showNormals = false;
        if (guiState.gui) {
            guiState.gui.controllersRecursive().forEach(controller => {
                if (controller.property === 'showNormals') {
                    controller.updateDisplay();
                }
            });
        }
    }

    // Clear scene objects
    if (sceneState.root) {
        sceneState.root.traverse(obj => {
            if (obj.isMesh) {
                obj.geometry?.dispose();
                if (obj.material) {
                    if (Array.isArray(obj.material)) {
                        obj.material.forEach(m => m.dispose());
                    } else {
                        obj.material.dispose();
                    }
                }
            }
        });
        threeState.scene.remove(sceneState.root);
        sceneState.root = null;
    }

    sceneState.materials = [];
    sceneState.materialData = [];

    // Dispose texture cache
    sceneState.textureCache.forEach(texture => {
        if (texture && texture.dispose) texture.dispose();
    });
    sceneState.textureCache.clear();

    // Clear animation state
    if (animationState.mixer) {
        animationState.mixer.stopAllAction();
        animationState.mixer = null;
    }
    animationState.clips = [];
    animationState.action = null;
    animationState.params.isPlaying = true;
    animationState.params.time = 0;
    animationState.params.beginTime = 0;
    animationState.params.endTime = 10;
    threeState.clock.stop();

    // Clear pick state
    clearSelectionHighlight();
    pickState.selectedObject = null;

    // Reset scene state
    sceneState.upAxis = 'Y';
    sceneState.metadata = null;
    sceneState.domeLightData = null;

    // Clear WASM memory
    if (loaderState.nativeLoader) {
        try {
            loaderState.nativeLoader.reset();
        } catch (e) {
            try {
                loaderState.nativeLoader.clearAssets();
            } catch (e2) {
                // Ignore
            }
        }
        loaderState.nativeLoader = null;
    }
}

function fitCameraToScene() {
    if (!sceneState.root) return;

    const box = new THREE.Box3().setFromObject(sceneState.root);
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());

    const boundingSphereRadius = size.length() * 0.5;
    const fov = threeState.camera.fov * (Math.PI / 180);
    const aspectRatio = threeState.camera.aspect;

    const verticalFOV = fov;
    const horizontalFOV = 2 * Math.atan(Math.tan(verticalFOV / 2) * aspectRatio);
    const effectiveFOV = Math.min(verticalFOV, horizontalFOV);

    let cameraDistance = boundingSphereRadius / Math.sin(effectiveFOV / 2);
    cameraDistance *= CAMERA_PADDING;

    const cameraOffset = new THREE.Vector3(
        cameraDistance * 0.5,
        cameraDistance * 0.5,
        cameraDistance * 0.866
    );

    threeState.camera.position.copy(center.clone().add(cameraOffset));
    threeState.camera.lookAt(center);
    threeState.controls.target.copy(center);

    threeState.camera.near = Math.max(0.1, cameraDistance / 100);
    threeState.camera.far = Math.max(1000, cameraDistance * 10);
    threeState.camera.updateProjectionMatrix();

    threeState.controls.update();
}

// ============================================================================
// Material UI
// ============================================================================

function updateMaterialUI() {
    // Clear existing controllers and folders
    while (guiState.materialFolder.controllers.length > 0) {
        guiState.materialFolder.controllers[0].destroy();
    }
    while (guiState.materialFolder.folders.length > 0) {
        guiState.materialFolder.folders[0].destroy();
    }
    while (guiState.textureFolder.controllers.length > 0) {
        guiState.textureFolder.controllers[0].destroy();
    }
    while (guiState.textureFolder.folders.length > 0) {
        guiState.textureFolder.folders[0].destroy();
    }

    // Determine which materials to show
    let materialsToShow = [];
    let headerText = '';
    const meshCount = countMeshes();

    if (pickState.selectedObject) {
        // Show only materials from selected object
        materialsToShow = getMaterialsFromObject(pickState.selectedObject);
        const objName = pickState.selectedObject.name || 'Unnamed';
        headerText = `Selected: ${objName}`;
    } else if (meshCount === 1) {
        // Single mesh in scene - auto-select its materials
        sceneState.root?.traverse(obj => {
            if (obj.isMesh && materialsToShow.length === 0) {
                materialsToShow = getMaterialsFromObject(obj);
            }
        });
        headerText = 'Single Mesh';
    } else {
        // Show all materials
        materialsToShow = sceneState.materials;
        headerText = 'All Materials';
    }

    // Add header info
    if (headerText) {
        guiState.materialFolder.add({ info: headerText }, 'info').name('Showing').disable();
    }

    // Add material controls
    materialsToShow.forEach((mat, index) => {
        const globalIndex = sceneState.materials.indexOf(mat);
        const matName = globalIndex >= 0 ? `Material ${globalIndex}` : `Material ${index}`;
        const matFolder = guiState.materialFolder.addFolder(matName);
        addMaterialControls(matFolder, mat);
        addTextureUI(mat, globalIndex >= 0 ? globalIndex : index);
    });

    // If no materials, show message
    if (materialsToShow.length === 0) {
        guiState.materialFolder.add({ info: 'No materials' }, 'info').name('Status').disable();
    }
}

function addMaterialControls(folder, mat) {
    // Get material name from raw data
    const rawData = mat.userData?.rawData;
    const materialName = rawData?.name || rawData?.materialName || mat.name || 'Unnamed';
    folder.add({ name: materialName }, 'name').name('Name').disable();

    const typeString = mat.userData.typeString || 'Unknown';
    folder.add({ type: typeString }, 'type').name('Type').disable();

    if (mat.color) {
        const displayColor = linearToSRGB(mat.color.clone());
        const colorObj = { color: '#' + displayColor.getHexString() };
        folder.addColor(colorObj, 'color').name('Base Color (sRGB)').onChange(v => {
            mat.color.copy(sRGBToLinear(new THREE.Color(v)));
        });
    }

    if (mat.metalness !== undefined) {
        folder.add(mat, 'metalness', 0, 1, 0.01).name('Metalness');
    }

    if (mat.roughness !== undefined) {
        folder.add(mat, 'roughness', 0, 1, 0.01).name('Roughness');
    }

    if (mat.ior !== undefined) {
        folder.add(mat, 'ior', 1, 3, 0.01).name('IOR');
    }

    if (mat.clearcoat !== undefined) {
        folder.add(mat, 'clearcoat', 0, 1, 0.01).name('Clearcoat');
    }

    if (mat.clearcoatRoughness !== undefined) {
        folder.add(mat, 'clearcoatRoughness', 0, 1, 0.01).name('Clearcoat Roughness');
    }

    if (mat.transmission !== undefined) {
        folder.add(mat, 'transmission', 0, 1, 0.01).name('Transmission');
    }

    if (mat.sheen !== undefined) {
        folder.add(mat, 'sheen', 0, 1, 0.01).name('Fuzz');
    }

    addDiffuseRoughnessControl(folder, mat);

    if (mat.emissive) {
        const displayEmissive = linearToSRGB(mat.emissive.clone());
        const emissiveObj = { emissive: '#' + displayEmissive.getHexString() };
        folder.addColor(emissiveObj, 'emissive').name('Emissive (sRGB)').onChange(v => {
            mat.emissive.copy(sRGBToLinear(new THREE.Color(v)));
        });
    }

    if (mat.emissiveIntensity !== undefined) {
        folder.add(mat, 'emissiveIntensity', 0, 10, 0.1).name('Emissive Intensity');
    }
}

function addDiffuseRoughnessControl(folder, mat) {
    let diffuseRoughnessValue;
    const rawData = mat.userData.rawData;

    if (rawData?.base_diffuse_roughness !== undefined) {
        diffuseRoughnessValue = rawData.base_diffuse_roughness;
    } else if (rawData?.openPBR?.base_diffuse_roughness !== undefined) {
        diffuseRoughnessValue = rawData.openPBR.base_diffuse_roughness;
    } else if (rawData?.openPBRShader?.base_diffuse_roughness !== undefined) {
        diffuseRoughnessValue = rawData.openPBRShader.base_diffuse_roughness;
    }

    if (diffuseRoughnessValue === undefined) return;

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }
    mat.userData.customParams.baseDiffuseRoughness = diffuseRoughnessValue;

    folder.add(mat.userData.customParams, 'baseDiffuseRoughness', 0, 1, 0.01)
        .name('Diffuse Roughness')
        .onChange(v => {
            if (rawData?.base_diffuse_roughness !== undefined) {
                rawData.base_diffuse_roughness = v;
            }
            if (rawData?.openPBR?.base_diffuse_roughness !== undefined) {
                rawData.openPBR.base_diffuse_roughness = v;
            }
            if (rawData?.openPBRShader?.base_diffuse_roughness !== undefined) {
                rawData.openPBRShader.base_diffuse_roughness = v;
            }
            mat.needsUpdate = true;
        });
}

function addTextureUI(material, index) {
    const texFolder = guiState.textureFolder.addFolder(`Material ${index} Textures`);
    let hasTextures = false;

    TEXTURE_MAPS.forEach(({ prop, name }) => {
        const tex = material[prop];
        if (tex) {
            hasTextures = true;
            const texInfo = {
                enabled: true,
                preview: () => previewTexture(tex, name)
            };
            const folder = texFolder.addFolder(name);
            folder.add(texInfo, 'enabled').name('Enabled').onChange(v => {
                material[prop] = v ? tex : null;
                material.needsUpdate = true;
            });
            folder.add(texInfo, 'preview').name('Preview');
            folder.close();
        }
    });

    if (!hasTextures) {
        texFolder.add({ info: 'No textures' }, 'info').name('Status').disable();
    }
}

function previewTexture(texture, name) {
    const existing = document.getElementById('texture-preview-modal');
    if (existing) existing.remove();

    const modal = document.createElement('div');
    modal.id = 'texture-preview-modal';
    modal.style.cssText = `
        position: fixed; top: 0; left: 0; width: 100%; height: 100%;
        background: rgba(0, 0, 0, 0.8); display: flex;
        justify-content: center; align-items: center; z-index: 10000; cursor: pointer;
    `;
    modal.onclick = () => modal.remove();

    const container = document.createElement('div');
    container.style.cssText = `
        background: #2a2a2a; padding: 20px; border-radius: 10px;
        max-width: 80%; max-height: 80%;
    `;

    const title = document.createElement('h3');
    title.textContent = name;
    title.style.cssText = 'color: white; margin: 0 0 10px 0;';
    container.appendChild(title);

    const canvas = document.createElement('canvas');
    canvas.width = 512;
    canvas.height = 512;
    canvas.style.cssText = 'max-width: 100%; border-radius: 5px;';

    const tempScene = new THREE.Scene();
    const tempCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    const geometry = new THREE.PlaneGeometry(2, 2);
    const material = new THREE.MeshBasicMaterial({ map: texture });
    tempScene.add(new THREE.Mesh(geometry, material));

    const tempRenderer = new THREE.WebGLRenderer({ canvas });
    tempRenderer.setSize(512, 512);
    tempRenderer.render(tempScene, tempCamera);
    tempRenderer.dispose();
    geometry.dispose();
    material.dispose();

    container.appendChild(canvas);
    modal.appendChild(container);
    document.body.appendChild(modal);
}

// ============================================================================
// Object Picking
// ============================================================================

/**
 * Handle canvas click for object picking
 */
function onCanvasClick(event) {
    // Ignore if clicking on GUI
    if (event.target !== threeState.renderer.domElement) return;

    // Calculate normalized device coordinates
    const rect = threeState.renderer.domElement.getBoundingClientRect();
    pickState.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pickState.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

    // Perform raycasting
    pickState.raycaster.setFromCamera(pickState.mouse, threeState.camera);

    if (!sceneState.root) return;

    const intersects = pickState.raycaster.intersectObjects(sceneState.root.children, true);

    if (intersects.length > 0) {
        // Find the first mesh
        const hit = intersects.find(i => i.object.isMesh);
        if (hit) {
            selectObject(hit.object);
        } else {
            // Clicked on non-mesh object, clear selection
            clearSelection();
        }
    } else {
        // Clicked on background, clear selection
        clearSelection();
    }
}

/**
 * Select an object and update UI
 */
function selectObject(object) {
    // Clear previous selection
    clearSelectionHighlight();

    pickState.selectedObject = object;

    // Create selection highlight (wireframe box)
    const box = new THREE.Box3().setFromObject(object);
    const helper = new THREE.Box3Helper(box, 0x00ff00);
    helper.name = '__selectionHelper__';
    threeState.scene.add(helper);
    pickState.selectionHelper = helper;

    // Update material UI for selected object
    updateMaterialUI();

    // Update status
    const objName = object.name || 'Unnamed';
    const absPath = object.userData['primMeta.absPath'] || '';
    updateStatus(`Selected: ${objName}${absPath ? ' (' + absPath + ')' : ''}`);
}

/**
 * Clear selection highlight
 */
function clearSelectionHighlight() {
    if (pickState.selectionHelper) {
        threeState.scene.remove(pickState.selectionHelper);
        pickState.selectionHelper.dispose();
        pickState.selectionHelper = null;
    }
}

/**
 * Clear selection and show all materials
 */
function clearSelection() {
    clearSelectionHighlight();
    pickState.selectedObject = null;
    updateMaterialUI();
    updateStatus('Selection cleared - showing all materials');
}

/**
 * Get materials from a specific object
 */
function getMaterialsFromObject(object) {
    const materials = [];
    if (!object) return materials;

    if (object.material) {
        if (Array.isArray(object.material)) {
            object.material.forEach(mat => {
                if (!materials.includes(mat)) materials.push(mat);
            });
        } else {
            if (!materials.includes(object.material)) {
                materials.push(object.material);
            }
        }
    }

    return materials;
}

/**
 * Count meshes in the scene
 */
function countMeshes() {
    let count = 0;
    if (sceneState.root) {
        sceneState.root.traverse(obj => {
            if (obj.isMesh) count++;
        });
    }
    return count;
}

// ============================================================================
// Event Handlers
// ============================================================================

function onWindowResize() {
    threeState.camera.aspect = window.innerWidth / window.innerHeight;
    threeState.camera.updateProjectionMatrix();
    threeState.renderer.setSize(window.innerWidth, window.innerHeight);
}

function onFileSelect(event) {
    const files = event.target.files;
    if (files.length > 0) {
        loadUSDFromFile(files[0]);
    }
    event.target.value = '';
}

function onDragOver(event) {
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
    document.getElementById('canvas-container').classList.add('drag-over');
}

function onDragLeave(event) {
    event.preventDefault();
    document.getElementById('canvas-container').classList.remove('drag-over');
}

function onFileDrop(event) {
    event.preventDefault();
    document.getElementById('canvas-container').classList.remove('drag-over');

    const files = event.dataTransfer.files;
    if (files.length > 0) {
        const file = files[0];
        const ext = file.name.toLowerCase().split('.').pop();
        if (['usd', 'usda', 'usdc', 'usdz'].includes(ext)) {
            loadUSDFromFile(file);
        } else {
            updateStatus('Please drop a USD file (.usd, .usda, .usdc, .usdz)');
        }
    }
}

// ============================================================================
// UI Helpers
// ============================================================================

function updateStatus(message) {
    const statusEl = document.getElementById('status');
    if (statusEl) {
        statusEl.textContent = message;
    }
}

window.loadFile = () => document.getElementById('file-input').click();

// ============================================================================
// Animation Loop
// ============================================================================

function animate() {
    requestAnimationFrame(animate);
    threeState.controls.update();

    if (animationState.mixer && animationState.params.isPlaying) {
        const delta = threeState.clock.getDelta();
        const scaledDelta = delta * (animationState.params.speed / 24.0);
        animationState.mixer.update(scaledDelta);

        if (animationState.action) {
            animationState.params.time = animationState.action.time;
        }
    }

    threeState.renderer.render(threeState.scene, threeState.camera);
}

// ============================================================================
// Start
// ============================================================================

init().catch(err => {
    console.error('Initialization failed:', err);
    updateStatus('Initialization failed: ' + err.message);
});
