// TinyUSDZ MaterialX/OpenPBR Simple Demo with Three.js
// Simple viewer for USD files with MaterialX/OpenPBR and UsdPreviewSurface material support

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';
import { EXRLoader } from 'three/examples/jsm/loaders/EXRLoader.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { OpenPBRMaterial } from './OpenPBRMaterial.js';
import { OpenPBRValidator, OpenPBRGroundTruth } from './OpenPBRValidation.js';

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
    'aces_1.3': THREE.ACESFilmicToneMapping,
    'aces_2.0': 'custom_aces2',  // Custom implementation
    'agx': THREE.AgXToneMapping,
    'neutral': THREE.NeutralToneMapping
};

// ACES 2.0 Tonemapping Shader
// Simplified real-time approximation based on ACES 2.0 Output Transform
// Key features: better hue preservation, highlight desaturation, improved S-curve
// Store original shader chunk for restoration
let originalTonemappingParsFragment = null;

const ACES2_TONEMAP_SHADER = `
// Attempt a simplified ACES 2.0 approximation for real-time rendering
// Based on the key principles: luminance-based tonemapping with hue preservation

// sRGB luminance coefficients
const vec3 ACES2_LUMA = vec3(0.2126, 0.7152, 0.0722);

// Attempt a simplified tone curve inspired by Daniele Evo curve
// This attempt uses a modified Reinhard-style curve with better highlight rolloff
float aces2_tonecurve(float x) {
    // attempt parameters tuned for SDR output
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    // attempt ACES-style filmic curve
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// attempt per-channel with luminance preservation for hue stability
vec3 ACES2ToneMapping(vec3 color) {
    // attempt clamp negative values
    color = max(color, vec3(0.0));

    // attempt exposure adjustment (attempt ACES-like pre-scaling)
    color *= 0.6;

    // attempt compute luminance before tonemapping
    float Lin = dot(color, ACES2_LUMA);

    // attempt apply tone curve to luminance
    float Lout = aces2_tonecurve(Lin);

    // attempt per-channel tonemapping
    vec3 Cout = vec3(
        aces2_tonecurve(color.r),
        aces2_tonecurve(color.g),
        aces2_tonecurve(color.b)
    );

    // attempt blend between per-channel and luminance-based for hue preservation
    // attempt more luminance-based in highlights to preserve hue
    float t = smoothstep(0.0, 1.0, Lout);

    // attempt luminance-based result (perfect hue preservation)
    vec3 LumBased = Lin > 0.0 ? color * (Lout / Lin) : vec3(0.0);
    LumBased = clamp(LumBased, 0.0, 1.0);

    // attempt blend: use more luminance-based in highlights
    vec3 result = mix(Cout, LumBased, t * 0.5);

    // attempt highlight desaturation (attempt ACES 2.0 style)
    float saturation = 1.0 - smoothstep(0.5, 1.0, Lout) * 0.3;
    float finalLum = dot(result, ACES2_LUMA);
    result = mix(vec3(finalLum), result, saturation);

    return clamp(result, 0.0, 1.0);
}
`;

// Helper function to apply ACES 2.0 custom tonemapping
function applyACES2Tonemapping() {
    // Save original shader chunk if not already saved
    if (originalTonemappingParsFragment === null) {
        originalTonemappingParsFragment = THREE.ShaderChunk.tonemapping_pars_fragment;
    }

    // Remove existing CustomToneMapping function from original chunk to avoid duplicate definition
    const originalWithoutCustom = originalTonemappingParsFragment.replace(
        /vec3 CustomToneMapping\s*\(\s*vec3 color\s*\)\s*\{[^}]*\}/g,
        '// CustomToneMapping replaced by ACES 2.0'
    );

    // Create custom tonemapping shader with ACES2ToneMapping as CustomToneMapping
    const customTonemappingShader = `
${ACES2_TONEMAP_SHADER}

// Map CustomToneMapping to our ACES 2.0 implementation
vec3 CustomToneMapping( vec3 color ) {
    return ACES2ToneMapping( color );
}
${originalWithoutCustom}
`;

    // Patch Three.js shader chunk
    THREE.ShaderChunk.tonemapping_pars_fragment = customTonemappingShader;

    // Set renderer to use custom tonemapping
    if (threeState.renderer) {
        threeState.renderer.toneMapping = THREE.CustomToneMapping;

        // Force all materials to recompile
        if (sceneState.root) {
            sceneState.root.traverse((object) => {
                if (object.isMesh && object.material) {
                    object.material.needsUpdate = true;
                }
            });
        }
    }
}

// Helper function to restore standard tonemapping
function restoreStandardTonemapping(tonemappingType) {
    // Restore original shader chunk if we modified it
    if (originalTonemappingParsFragment !== null) {
        THREE.ShaderChunk.tonemapping_pars_fragment = originalTonemappingParsFragment;
    }

    // Set the requested tonemapping
    if (threeState.renderer) {
        threeState.renderer.toneMapping = tonemappingType;

        // Force all materials to recompile
        if (sceneState.root) {
            sceneState.root.traverse((object) => {
                if (object.isMesh && object.material) {
                    object.material.needsUpdate = true;
                }
            });
        }
    }
}

// Set tonemapping mode (handles both standard and custom modes)
function setTonemapping(mode) {
    const mapping = TONE_MAPPINGS[mode];

    if (mapping === 'custom_aces2') {
        applyACES2Tonemapping();
        console.log('Applied ACES 2.0 tonemapping (Hellwig 2022 CAM-based)');
    } else {
        restoreStandardTonemapping(mapping || THREE.ACESFilmicToneMapping);
    }
}

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
        isPlaying: false,  // Do not auto-play by default
        time: 0,
        beginTime: 0,
        endTime: 10,
        speed: 24.0
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
    materialImplementation: 'physical', // 'physical' | 'openpbr' | 'auto'
    envMapPreset: 'goegap_1k',
    envMapIntensity: 1.0,
    envConstantColor: '#ffffff',
    envColorspace: 'sRGB',
    showBackground: true,
    exposure: 1.0,
    toneMapping: 'aces_1.3',
    applyUpAxisConversion: false,
    showNormals: false,
    normalAbsMode: false, // true: map [-1,1] to [0,1], false: standard MeshNormalMaterial
    showGrid: false,
    showAxes: false,
    gridSize: 10,
    gridDivisions: 10
};

// Normal vector visualization state (per-selected object)
const normalVectorState = {
    enabled: false,
    type: 'vertex', // 'vertex' | 'face'
    length: 0.1,
    helper: null
};

// Validator instance (created after renderer init)
let validator = null;

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
    // Initialize tonemapping from settings (default: aces_1.3)
    setTonemapping(settings.toneMapping);
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

    // Animation disabled for now - may revisit later
    // setupAnimationFolder();

    // OpenPBR validation folder
    setupValidationFolder(guiState.gui);
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

    sceneFolder.add(settings, 'envMapIntensity', 0, 100, 0.1)
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
        .onChange(v => { setTonemapping(v); });

    sceneFolder.add(settings, 'applyUpAxisConversion')
        .name('Z-up to Y-up Fix')
        .onChange(applyUpAxisConversion);

    sceneFolder.add(settings, 'showNormals')
        .name('Show Normals')
        .onChange(toggleNormalDisplay);

    sceneFolder.add(settings, 'normalAbsMode')
        .name('Normal Abs Color')
        .onChange(() => {
            if (settings.showNormals) {
                toggleNormalDisplay(); // Re-apply with new mode
            }
        });

    sceneFolder.add(settings, 'showGrid')
        .name('Show Grid')
        .onChange(toggleGrid);

    sceneFolder.add(settings, 'showAxes')
        .name('Show Axes')
        .onChange(toggleAxes);
}

/*
 * Animation UI - disabled for now, may revisit later
 *
function setupAnimationFolder() {
    guiState.animationFolder = guiState.gui.addFolder('Animation');

    // Play/Pause button
    const playPauseBtn = {
        toggle: toggleAnimationPlayback
    };
    guiState.animationFolder.add(playPauseBtn, 'toggle').name('Play / Pause');

    // Reset button
    const resetBtn = {
        reset: resetAnimation
    };
    guiState.animationFolder.add(resetBtn, 'reset').name('Reset');

    // Playing state indicator (read-only checkbox)
    guiState.animationFolder.add(animationState.params, 'isPlaying')
        .name('Playing')
        .listen()
        .onChange((value) => {
            // Sync the playing state with actions
            if (animationState.mixer) {
                animationState.clips.forEach((clip) => {
                    const action = animationState.mixer.clipAction(clip);
                    action.paused = !value;
                });
                // Reset clock delta to avoid jump
                if (value) {
                    threeState.clock.getDelta();
                }
            }
        });

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
*/

function setupMaterialTypeFolder() {
    const materialTypeFolder = guiState.gui.addFolder('Material Type');
    materialTypeFolder.add(settings, 'materialType', ['auto', 'openpbr', 'usdpreviewsurface'])
        .name('Preferred Type')
        .onChange(reloadMaterials);

    materialTypeFolder.add(settings, 'materialImplementation', ['physical', 'openpbr', 'auto'])
        .name('Implementation')
        .onChange(reloadMaterials);

    // Show discrepancy report
    materialTypeFolder.add({
        showDiscrepancies: showMaterialDiscrepancyReport
    }, 'showDiscrepancies').name('Show Limitations');

    materialTypeFolder.open();
}

/**
 * Show MeshPhysicalMaterial vs OpenPBRMaterial discrepancy report
 */
function showMaterialDiscrepancyReport() {
    const report = `
=== MeshPhysicalMaterial vs OpenPBRMaterial ===

Features MISSING in MeshPhysicalMaterial:
- base_diffuse_roughness (Oren-Nayar diffuse)
- coat_color (always white)
- coat_ior (fixed at 1.5)
- specular_weight (different from specularIntensity)
- base_weight

Features with DIFFERENT models:
- Sheen: Three.js uses Charlie, OpenPBR uses different formulation
- Thin Film: Different parameterization

OUT OF SCOPE (require raytracing):
- Subsurface scattering (SSS)
- Transmission scatter
- Dispersion

Use 'openpbr' implementation to get accurate OpenPBR rendering.
`.trim();

    console.log(report);
    alert(report);
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

/*
 * ============================================================================
 * Animation Control - disabled for now, may revisit later
 * ============================================================================

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

End of Animation Control block
*/

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

/**
 * Create a shader material that displays normals as absolute colors
 * Maps normal components from [-1, 1] to [0, 1]
 * This shows world-space normal direction regardless of view angle
 * If normalMap is provided, it will show perturbed normals
 */
function createAbsNormalMaterial(normalMap = null, normalScale = new THREE.Vector2(1, 1)) {
    const material = new THREE.ShaderMaterial({
        uniforms: {
            normalMap: { value: normalMap },
            normalScale: { value: normalScale },
            useNormalMap: { value: normalMap !== null }
        },
        vertexShader: `
            varying vec3 vNormal;
            varying vec3 vWorldNormal;
            varying vec2 vUv;
            varying vec3 vTangent;
            varying vec3 vBitangent;

            #ifdef USE_TANGENT
                attribute vec4 tangent;
            #endif

            void main() {
                // Transform normal to world space using inverse transpose of model matrix
                // For uniform scale, mat3(modelMatrix) works; for non-uniform, need proper normal matrix
                mat3 worldNormalMatrix = mat3(modelMatrix);
                vWorldNormal = normalize(worldNormalMatrix * normal);
                vNormal = normalize(normalMatrix * normal);
                vUv = uv;

                #ifdef USE_TANGENT
                    vTangent = normalize(worldNormalMatrix * tangent.xyz);
                    vBitangent = normalize(cross(vWorldNormal, vTangent) * tangent.w);
                #else
                    // Compute tangent - align with world X axis for horizontal planes
                    vTangent = normalize(worldNormalMatrix * vec3(1.0, 0.0, 0.0));
                    vBitangent = normalize(cross(vWorldNormal, vTangent));
                #endif

                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            }
        `,
        fragmentShader: `
            uniform sampler2D normalMap;
            uniform vec2 normalScale;
            uniform bool useNormalMap;

            varying vec3 vNormal;
            varying vec3 vWorldNormal;
            varying vec2 vUv;
            varying vec3 vTangent;
            varying vec3 vBitangent;

            void main() {
                // Use world-space normal directly - don't flip based on facing
                // We want to show the actual geometric normal direction
                vec3 n = normalize(vWorldNormal);

                if (useNormalMap) {
                    // Sample normal map and transform to world space
                    vec3 mapN = texture2D(normalMap, vUv).xyz * 2.0 - 1.0;
                    mapN.xy *= normalScale;
                    mapN = normalize(mapN);

                    // Build TBN matrix - use geometric normal for the N component
                    vec3 T = normalize(vTangent);
                    vec3 B = normalize(vBitangent);
                    vec3 N = n;
                    mat3 TBN = mat3(T, B, N);
                    n = normalize(TBN * mapN);
                }

                // Map from [-1, 1] to [0, 1]
                vec3 color = n * 0.5 + 0.5;
                gl_FragColor = vec4(color, 1.0);
            }
        `,
        side: THREE.DoubleSide
    });

    return material;
}

function toggleNormalDisplay() {
    if (!sceneState.root) return;

    if (settings.showNormals) {
        sceneState.showingNormals = true;
        sceneState.root.traverse(obj => {
            if (obj.isMesh && obj.material) {
                // Store original material if not already stored
                if (!sceneState.originalMaterialsMap.has(obj)) {
                    sceneState.originalMaterialsMap.set(obj, obj.material);
                }

                const origMat = sceneState.originalMaterialsMap.get(obj);
                const normalMap = origMat?.normalMap || null;
                const normalScale = origMat?.normalScale || new THREE.Vector2(1, 1);

                if (settings.normalAbsMode) {
                    // Absolute color mode: [-1,1] -> [0,1]
                    // Pass normal map to show perturbed normals
                    obj.material = createAbsNormalMaterial(normalMap, normalScale);
                } else {
                    // Standard MeshNormalMaterial (view-dependent coloring)
                    // Note: MeshNormalMaterial doesn't support normal maps
                    obj.material = new THREE.MeshNormalMaterial({
                        flatShading: false,
                        side: THREE.DoubleSide
                    });
                }
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
    //const defaultFile = './assets/fancy-teapot-mtlx.usdz';
    const defaultFile = './assets/mtlx-normalmap-plane.usdz';
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
    // Animation disabled for now - may revisit later
    // loadAnimations();
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
        const numAnimations = loaderState.nativeLoader.numAnimations();
        console.log(`USD file contains ${numAnimations} animations`);

        if (numAnimations === 0) {
            console.log('No animations in USD file');
            return;
        }

        animationState.clips = convertUSDAnimationsToThreeJS(loaderState.nativeLoader, sceneState.root);
        console.log(`Converted ${animationState.clips.length} animation clips`);

        if (animationState.clips.length > 0) {
            // Create mixer on the scene root
            animationState.mixer = new THREE.AnimationMixer(sceneState.root);
            console.log('Created AnimationMixer on sceneState.root:', sceneState.root.name, 'uuid:', sceneState.root.uuid);

            // Debug: List objects in the scene that could be animation targets
            console.log('=== Scene objects available for animation ===');
            sceneState.root.traverse((obj) => {
                console.log(`  "${obj.name}" (${obj.type}) uuid: ${obj.uuid.slice(0, 8)}`);
            });
            console.log('============================================');

            updateAnimationParams();
            prepareAnimationClips();  // Prepare but don't auto-play

            if (guiState.timeController) {
                guiState.timeController.max(animationState.params.endTime);
                guiState.timeController.updateDisplay();
            }

            if (guiState.animationFolder) {
                guiState.animationFolder.open();
            }

            const numMeshes = loaderState.nativeLoader.numMeshes();
            const numMaterials = loaderState.nativeLoader.numMaterials();
            updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials, ${animationState.clips.length} animations (paused)`);
        }
    } catch (error) {
        console.error('Error loading animations:', error);
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

/**
 * Prepare animation clips for playback (but don't auto-play)
 * Sets up all clips with proper loop settings and initial time
 */
function prepareAnimationClips() {
    const startTime = sceneState.metadata?.startTimeCode ?? 0;

    animationState.clips.forEach((clip, clipIndex) => {
        const action = animationState.mixer.clipAction(clip);
        action.loop = THREE.LoopRepeat;
        action.clampWhenFinished = false;
        action.enabled = true;
        action.setEffectiveWeight(1.0);
        action.time = startTime;
        action.paused = true;  // Start paused
        action.play();  // Register the action (but it's paused)

        console.log(`Prepared animation clip ${clipIndex}: "${clip.name}", ${clip.tracks.length} tracks, duration: ${clip.duration}s`);

        // Debug: log track targets
        clip.tracks.forEach(track => {
            console.log(`  Track: ${track.name}`);
        });
    });

    if (animationState.clips.length > 0) {
        animationState.action = animationState.mixer.clipAction(animationState.clips[0]);
    }

    // Force initial pose evaluation
    // This is critical - without this, the paused animation won't show initial state
    animationState.mixer.update(0);

    // Start the clock but don't start playing
    threeState.clock.start();
    threeState.clock.getDelta(); // Reset delta to avoid large jump when playing starts

    console.log(`Animation prepared: ${animationState.clips.length} clips, starting paused at time ${startTime}`);
}

/**
 * Toggle animation playback
 */
function toggleAnimationPlayback() {
    if (!animationState.mixer) {
        console.warn('No animation mixer - cannot toggle playback');
        return;
    }

    animationState.params.isPlaying = !animationState.params.isPlaying;
    console.log(`Animation playback: ${animationState.params.isPlaying ? 'PLAYING' : 'PAUSED'}`);

    animationState.clips.forEach((clip) => {
        const action = animationState.mixer.clipAction(clip);
        action.paused = !animationState.params.isPlaying;
        console.log(`  Action "${clip.name}": paused=${action.paused}, time=${action.time.toFixed(3)}`);
    });

    // Reset clock delta to avoid large jump when resuming
    if (animationState.params.isPlaying) {
        threeState.clock.getDelta();
    }
}

/**
 * Reset animation to beginning
 */
function resetAnimation() {
    if (!animationState.mixer) return;

    animationState.params.time = animationState.params.beginTime;

    animationState.clips.forEach((clip) => {
        const action = animationState.mixer.clipAction(clip);
        action.time = animationState.params.beginTime;
    });

    // Force update to show the reset position
    animationState.mixer.update(0.0001);

    // Reset to exact time after evaluation
    animationState.clips.forEach((clip) => {
        const action = animationState.mixer.clipAction(clip);
        action.time = animationState.params.beginTime;
    });
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
        // Check if we should use OpenPBRMaterial
        const useOpenPBRMaterial = shouldUseOpenPBRMaterial(typeInfo, settings.materialImplementation);

        let material;

        if (useOpenPBRMaterial) {
            // Create OpenPBRMaterial directly (async for texture loading)
            material = await convertToOpenPBRMaterial(matData, loaderState.nativeLoader);
            material.envMap = threeState.envMap;
            material.envMapIntensity = settings.envMapIntensity;
        } else {
            // Use TinyUSDZLoaderUtils for MeshPhysicalMaterial
            material = await TinyUSDZLoaderUtils.convertMaterial(
                matData,
                loaderState.nativeLoader,
                {
                    preferredMaterialType: settings.materialType,
                    envMap: threeState.envMap,
                    envMapIntensity: settings.envMapIntensity,
                    textureCache: sceneState.textureCache
                }
            );

            // Load normal map from OpenPBR geometry.normal if not already loaded
            // TinyUSDZLoaderUtils expects normalTextureId at top level, but MaterialX has it nested
            if (!material.normalMap) {
                const openPBR = matData.openPBR || matData.openPBRShader || {};
                const geometrySection = openPBR.geometry || {};
                const normalParam = geometrySection.normal || openPBR.normal || openPBR.geometry_normal;
                const normalTextureId = extractTextureId(normalParam);

                if (normalTextureId >= 0 && loaderState.nativeLoader) {
                    try {
                        // Find embedded texture (bufferId >= 0) for the same filename
                        let actualTextureId = normalTextureId;
                        const tex = loaderState.nativeLoader.getTexture(normalTextureId);
                        const texImage = loaderState.nativeLoader.getImage(tex.textureImageId);

                        if (texImage.bufferId === -1 && texImage.uri) {
                            const filename = texImage.uri.replace(/^\.\//, '');
                            const numImages = loaderState.nativeLoader.numImages();
                            for (let i = 0; i < numImages; i++) {
                                const altImage = loaderState.nativeLoader.getImage(i);
                                if (altImage.bufferId >= 0 && altImage.uri === filename) {
                                    const numTextures = loaderState.nativeLoader.numTextures();
                                    for (let t = 0; t < numTextures; t++) {
                                        const altTex = loaderState.nativeLoader.getTexture(t);
                                        if (altTex.textureImageId === i) {
                                            actualTextureId = t;
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                        }

                        const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(loaderState.nativeLoader, actualTextureId);
                        if (texture) {
                            material.normalMap = texture;
                            // Extract normal map scale from OpenPBR
                            const normalMapScale = geometrySection.normal_map_scale !== undefined
                                ? (typeof geometrySection.normal_map_scale === 'object' ? geometrySection.normal_map_scale.value : geometrySection.normal_map_scale)
                                : 1.0;
                            material.normalScale = new THREE.Vector2(normalMapScale, normalMapScale);
                            material.needsUpdate = true;
                        }
                    } catch (err) {
                        console.warn('Failed to load normal map texture for MeshPhysicalMaterial:', err);
                    }
                }
            }
        }

        material.userData.typeInfo = typeInfo;
        material.userData.typeString = typeString + (useOpenPBRMaterial ? ' [OpenPBRMaterial]' : ' [MeshPhysicalMaterial]');
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

/**
 * Determine if we should use OpenPBRMaterial based on settings and material type
 */
function shouldUseOpenPBRMaterial(typeInfo, implementation) {
    if (implementation === 'physical') {
        return false;
    }
    if (implementation === 'openpbr') {
        return typeInfo.hasOpenPBR;
    }
    // 'auto' - use OpenPBRMaterial for OpenPBR materials
    return typeInfo.hasOpenPBR;
}

/**
 * Extract texture ID from a property value
 * Handles formats like "texture_id[N]" or objects with texture_id
 */
function extractTextureId(param) {
    if (param === undefined || param === null) return -1;

    // Handle string format "texture_id[N]"
    if (typeof param === 'string') {
        const match = param.match(/texture_id\[(\d+)\]/);
        if (match) return parseInt(match[1], 10);
    }

    // Handle object with texture_id property
    if (typeof param === 'object') {
        if (param.texture_id !== undefined) return param.texture_id;
        if (param.textureId !== undefined) return param.textureId;
    }

    return -1;
}

/**
 * Convert material data to OpenPBRMaterial
 * @param {Object} matData - Material data from USD
 * @param {Object} nativeLoader - TinyUSDZ native loader for texture access
 * @returns {Promise<OpenPBRMaterial>} The created material
 */
async function convertToOpenPBRMaterial(matData, nativeLoader = null) {
    const openPBR = matData.openPBR || matData.openPBRShader || matData;

    // Extract values with fallbacks
    const extractValue = (param, defaultVal) => {
        if (param === undefined || param === null) return defaultVal;
        if (typeof param === 'object' && param.value !== undefined) return param.value;
        if (typeof param === 'number') return param;
        return defaultVal;
    };

    const extractColor = (param, defaultVal) => {
        if (!param) return new THREE.Color(...defaultVal);
        if (param.r !== undefined) return new THREE.Color(param.r, param.g, param.b);
        if (Array.isArray(param)) return new THREE.Color(...param);
        if (typeof param === 'object' && param.value) {
            if (Array.isArray(param.value)) return new THREE.Color(...param.value);
            if (param.value.r !== undefined) return new THREE.Color(param.value.r, param.value.g, param.value.b);
        }
        return new THREE.Color(...defaultVal);
    };

    const material = new OpenPBRMaterial({
        // Base layer
        base_weight: extractValue(openPBR.base_weight, 1.0),
        base_color: extractColor(openPBR.base_color, [0.8, 0.8, 0.8]),
        base_metalness: extractValue(openPBR.base_metalness, 0.0),
        base_diffuse_roughness: extractValue(openPBR.base_diffuse_roughness, 0.0),

        // Specular layer
        specular_weight: extractValue(openPBR.specular_weight, 1.0),
        specular_color: extractColor(openPBR.specular_color, [1.0, 1.0, 1.0]),
        specular_roughness: extractValue(openPBR.specular_roughness, 0.3),
        specular_ior: extractValue(openPBR.specular_ior, 1.5),
        specular_anisotropy: extractValue(openPBR.specular_anisotropy, 0.0),

        // Coat layer
        coat_weight: extractValue(openPBR.coat_weight, 0.0),
        coat_color: extractColor(openPBR.coat_color, [1.0, 1.0, 1.0]),
        coat_roughness: extractValue(openPBR.coat_roughness, 0.0),
        coat_ior: extractValue(openPBR.coat_ior, 1.5),

        // Fuzz layer
        fuzz_weight: extractValue(openPBR.fuzz_weight || openPBR.sheen_weight, 0.0),
        fuzz_color: extractColor(openPBR.fuzz_color || openPBR.sheen_color, [1.0, 1.0, 1.0]),
        fuzz_roughness: extractValue(openPBR.fuzz_roughness || openPBR.sheen_roughness, 0.5),

        // Thin film
        thin_film_weight: extractValue(openPBR.thin_film_weight, 0.0),
        thin_film_thickness: extractValue(openPBR.thin_film_thickness, 500.0),
        thin_film_ior: extractValue(openPBR.thin_film_ior, 1.5),

        // Emission
        emission_luminance: extractValue(openPBR.emission_luminance, 0.0),
        emission_color: extractColor(openPBR.emission_color, [1.0, 1.0, 1.0]),

        // Geometry
        geometry_opacity: extractValue(openPBR.geometry_opacity || openPBR.opacity, 1.0)
    });

    // Set color alias for compatibility
    material.uniforms.base_color.value.copy(material.uniforms.base_color.value);

    // Load normal map texture if available
    // The normal map is in openPBR.geometry.normal (not openPBR.normal)
    const geometrySection = openPBR.geometry || {};
    const normalParam = geometrySection.normal || openPBR.normal || openPBR.geometry_normal;
    const normalTextureId = extractTextureId(normalParam);
    if (normalTextureId >= 0 && nativeLoader) {
        try {
            // nativeLoader itself has getTexture, getImage methods - use it directly as usdScene
            // First, try to find an image with valid bufferId for the same filename
            // This works around the issue where TinyUSDZ creates duplicate images with different paths
            let actualTextureId = normalTextureId;
            const tex = nativeLoader.getTexture(normalTextureId);
            const texImage = nativeLoader.getImage(tex.textureImageId);

            if (texImage.bufferId === -1 && texImage.uri) {
                // Try to find an alternative image with the same filename but valid bufferId
                const filename = texImage.uri.replace(/^\.\//, ''); // Remove leading ./
                const numImages = nativeLoader.numImages();
                for (let i = 0; i < numImages; i++) {
                    const altImage = nativeLoader.getImage(i);
                    if (altImage.bufferId >= 0 && altImage.uri === filename) {
                        // Find a texture that uses this image
                        const numTextures = nativeLoader.numTextures();
                        for (let t = 0; t < numTextures; t++) {
                            const altTex = nativeLoader.getTexture(t);
                            if (altTex.textureImageId === i) {
                                actualTextureId = t;
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(nativeLoader, actualTextureId);
            if (texture) {
                material.normalMap = texture;

                // Apply normal map scale if available
                const normalMapScale = extractValue(openPBR.normal_map_scale, 1.0);
                if (material.uniforms.normalScale) {
                    material.uniforms.normalScale.value.set(normalMapScale, normalMapScale);
                }

                // Normal map loaded successfully
            }
        } catch (err) {
            console.warn('Failed to load normal map texture:', err);
        }
    }

    // Store geometry_normal info in userData for UI display
    material.userData.geometry_normal = {
        hasTexture: normalTextureId >= 0,
        textureId: normalTextureId,
        scale: extractValue(openPBR.normal_map_scale, 1.0)
    };

    return material;
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
    animationState.params.isPlaying = false;  // Reset to not playing
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
        // Multiple objects - show nothing initially, require selection
        materialsToShow = [];
        headerText = 'Click object to select';
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

    // Add normal vector visualization controls (only when an object is selected)
    if (pickState.selectedObject && pickState.selectedObject.isMesh) {
        const normalVectorFolder = guiState.materialFolder.addFolder('Normal Vectors');

        normalVectorFolder.add(normalVectorState, 'enabled')
            .name('Show Vectors')
            .onChange(updateNormalVectorVisualization);

        normalVectorFolder.add(normalVectorState, 'type', ['vertex', 'face'])
            .name('Type')
            .onChange(updateNormalVectorVisualization);

        normalVectorFolder.add(normalVectorState, 'length', 0.01, 1.0, 0.01)
            .name('Length')
            .onChange(updateNormalVectorVisualization);

        normalVectorFolder.open();
    }
}

function addMaterialControls(folder, mat) {
    // Get material name from raw data
    const rawData = mat.userData?.rawData;
    const materialName = rawData?.name || rawData?.materialName || mat.name || 'Unnamed';
    folder.add({ name: materialName }, 'name').name('Name').disable();

    const typeString = mat.userData.typeString || 'Unknown';
    folder.add({ type: typeString }, 'type').name('Type').disable();

    // Double-sided toggle
    if (mat.side !== undefined) {
        const sideControl = { doubleSided: mat.side === THREE.DoubleSide };
        folder.add(sideControl, 'doubleSided').name('Double Sided').onChange(v => {
            mat.side = v ? THREE.DoubleSide : THREE.FrontSide;
            mat.needsUpdate = true;
        });
    }

    if (mat.color) {
        const displayColor = linearToSRGB(mat.color.clone());
        const colorObj = { color: '#' + displayColor.getHexString() };
        folder.addColor(colorObj, 'color').name('Base Color (sRGB)').onChange(v => {
            mat.color.copy(sRGBToLinear(new THREE.Color(v)));
        });
    }

    // --- Base Layer ---
    if (mat.metalness !== undefined) {
        folder.add(mat, 'metalness', 0, 1, 0.01).name('Metalness');
    }

    addDiffuseRoughnessControl(folder, mat);

    // --- Specular Layer ---
    if (mat.roughness !== undefined) {
        folder.add(mat, 'roughness', 0, 1, 0.01).name('Specular Roughness');
    }

    if (mat.ior !== undefined) {
        folder.add(mat, 'ior', 1, 3, 0.01).name('Specular IOR');
    }

    addSpecularWeightControl(folder, mat);
    addSpecularColorControl(folder, mat);

    // --- Coat Layer ---
    if (mat.clearcoat !== undefined) {
        folder.add(mat, 'clearcoat', 0, 1, 0.01).name('Coat Weight');
    }

    if (mat.clearcoatRoughness !== undefined) {
        folder.add(mat, 'clearcoatRoughness', 0, 1, 0.01).name('Coat Roughness');
    }

    addCoatIORControl(folder, mat);
    addCoatColorControl(folder, mat);

    // --- Fuzz/Sheen Layer ---
    if (mat.sheen !== undefined) {
        folder.add(mat, 'sheen', 0, 1, 0.01).name('Fuzz Weight');
    }

    if (mat.sheenRoughness !== undefined) {
        folder.add(mat, 'sheenRoughness', 0, 1, 0.01).name('Fuzz Roughness');
    }

    addFuzzColorControl(folder, mat);

    // --- Other Layers ---
    if (mat.transmission !== undefined) {
        folder.add(mat, 'transmission', 0, 1, 0.01).name('Transmission');
    }

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

    // --- Geometry Layer (Normal Map) ---
    addGeometryNormalControl(folder, mat);
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

function addSpecularWeightControl(folder, mat) {
    let specularWeightValue;
    const rawData = mat.userData?.rawData;

    if (rawData?.specular_weight !== undefined) {
        specularWeightValue = rawData.specular_weight;
    } else if (rawData?.openPBR?.specular_weight !== undefined) {
        specularWeightValue = rawData.openPBR.specular_weight;
    } else if (rawData?.openPBRShader?.specular_weight !== undefined) {
        specularWeightValue = rawData.openPBRShader.specular_weight;
    }

    // Default to 1.0 if OpenPBR material but no specular_weight specified
    if (specularWeightValue === undefined) {
        if (rawData?.openPBR || rawData?.openPBRShader) {
            specularWeightValue = 1.0;
        } else {
            return; // Not an OpenPBR material
        }
    }

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }
    mat.userData.customParams.specularWeight = specularWeightValue;

    folder.add(mat.userData.customParams, 'specularWeight', 0, 1, 0.01)
        .name('Specular Weight')
        .onChange(v => {
            if (rawData?.specular_weight !== undefined) {
                rawData.specular_weight = v;
            }
            if (rawData?.openPBR) {
                rawData.openPBR.specular_weight = v;
            }
            if (rawData?.openPBRShader) {
                rawData.openPBRShader.specular_weight = v;
            }
            // Update Three.js specularIntensity if available
            if (mat.specularIntensity !== undefined) {
                mat.specularIntensity = v;
            }
            mat.needsUpdate = true;
        });
}

function addSpecularColorControl(folder, mat) {
    let specularColorValue;
    const rawData = mat.userData?.rawData;

    if (rawData?.specular_color !== undefined) {
        specularColorValue = rawData.specular_color;
    } else if (rawData?.openPBR?.specular_color !== undefined) {
        specularColorValue = rawData.openPBR.specular_color;
    } else if (rawData?.openPBRShader?.specular_color !== undefined) {
        specularColorValue = rawData.openPBRShader.specular_color;
    }

    // Default to white if OpenPBR material but no specular_color specified
    if (specularColorValue === undefined) {
        if (rawData?.openPBR || rawData?.openPBRShader) {
            specularColorValue = [1.0, 1.0, 1.0];
        } else {
            return; // Not an OpenPBR material
        }
    }

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }

    // Convert to THREE.Color for display
    const color = Array.isArray(specularColorValue)
        ? new THREE.Color(specularColorValue[0], specularColorValue[1], specularColorValue[2])
        : new THREE.Color(specularColorValue);

    const displayColor = linearToSRGB(color.clone());
    mat.userData.customParams.specularColorHex = '#' + displayColor.getHexString();

    folder.addColor(mat.userData.customParams, 'specularColorHex')
        .name('Specular Color')
        .onChange(v => {
            const newColor = sRGBToLinear(new THREE.Color(v));
            const colorArray = [newColor.r, newColor.g, newColor.b];

            if (rawData?.specular_color !== undefined) {
                rawData.specular_color = colorArray;
            }
            if (rawData?.openPBR) {
                rawData.openPBR.specular_color = colorArray;
            }
            if (rawData?.openPBRShader) {
                rawData.openPBRShader.specular_color = colorArray;
            }
            // Update Three.js specularColor if available
            if (mat.specularColor) {
                mat.specularColor.copy(newColor);
            }
            mat.needsUpdate = true;
        });
}

function addCoatIORControl(folder, mat) {
    let coatIORValue;
    const rawData = mat.userData?.rawData;

    if (rawData?.coat_ior !== undefined) {
        coatIORValue = rawData.coat_ior;
    } else if (rawData?.openPBR?.coat_ior !== undefined) {
        coatIORValue = rawData.openPBR.coat_ior;
    } else if (rawData?.openPBRShader?.coat_ior !== undefined) {
        coatIORValue = rawData.openPBRShader.coat_ior;
    }

    // Default to 1.5 if OpenPBR material (show regardless of coat weight)
    if (coatIORValue === undefined) {
        if (rawData?.openPBR || rawData?.openPBRShader) {
            coatIORValue = 1.5;
        } else {
            return; // Not an OpenPBR material
        }
    }

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }
    mat.userData.customParams.coatIOR = coatIORValue;

    folder.add(mat.userData.customParams, 'coatIOR', 1, 3, 0.01)
        .name('Coat IOR')
        .onChange(v => {
            if (rawData?.coat_ior !== undefined) {
                rawData.coat_ior = v;
            }
            if (rawData?.openPBR) {
                rawData.openPBR.coat_ior = v;
            }
            if (rawData?.openPBRShader) {
                rawData.openPBRShader.coat_ior = v;
            }
            // Note: Three.js MeshPhysicalMaterial doesn't support coat IOR (fixed at 1.5)
            mat.needsUpdate = true;
        });
}

function addCoatColorControl(folder, mat) {
    let coatColorValue;
    const rawData = mat.userData?.rawData;

    if (rawData?.coat_color !== undefined) {
        coatColorValue = rawData.coat_color;
    } else if (rawData?.openPBR?.coat_color !== undefined) {
        coatColorValue = rawData.openPBR.coat_color;
    } else if (rawData?.openPBRShader?.coat_color !== undefined) {
        coatColorValue = rawData.openPBRShader.coat_color;
    }

    // Default to white if OpenPBR material (show regardless of coat weight)
    if (coatColorValue === undefined) {
        if (rawData?.openPBR || rawData?.openPBRShader) {
            coatColorValue = [1.0, 1.0, 1.0];
        } else {
            return; // Not an OpenPBR material
        }
    }

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }

    const color = Array.isArray(coatColorValue)
        ? new THREE.Color(coatColorValue[0], coatColorValue[1], coatColorValue[2])
        : new THREE.Color(coatColorValue);

    const displayColor = linearToSRGB(color.clone());
    mat.userData.customParams.coatColorHex = '#' + displayColor.getHexString();

    folder.addColor(mat.userData.customParams, 'coatColorHex')
        .name('Coat Color')
        .onChange(v => {
            const newColor = sRGBToLinear(new THREE.Color(v));
            const colorArray = [newColor.r, newColor.g, newColor.b];

            if (rawData?.coat_color !== undefined) {
                rawData.coat_color = colorArray;
            }
            if (rawData?.openPBR) {
                rawData.openPBR.coat_color = colorArray;
            }
            if (rawData?.openPBRShader) {
                rawData.openPBRShader.coat_color = colorArray;
            }
            // Note: Three.js MeshPhysicalMaterial doesn't support coat color (always white)
            mat.needsUpdate = true;
        });
}

function addFuzzColorControl(folder, mat) {
    let fuzzColorValue;
    const rawData = mat.userData?.rawData;

    if (rawData?.fuzz_color !== undefined) {
        fuzzColorValue = rawData.fuzz_color;
    } else if (rawData?.openPBR?.fuzz_color !== undefined) {
        fuzzColorValue = rawData.openPBR.fuzz_color;
    } else if (rawData?.openPBRShader?.fuzz_color !== undefined) {
        fuzzColorValue = rawData.openPBRShader.fuzz_color;
    }

    // Use Three.js sheenColor if available and no OpenPBR value
    if (fuzzColorValue === undefined && mat.sheenColor) {
        fuzzColorValue = [mat.sheenColor.r, mat.sheenColor.g, mat.sheenColor.b];
    }

    // Default to white if has sheen/fuzz but no color specified
    if (fuzzColorValue === undefined) {
        const hasFuzz = mat.sheen !== undefined && mat.sheen > 0;
        if (hasFuzz || rawData?.openPBR || rawData?.openPBRShader) {
            fuzzColorValue = [1.0, 1.0, 1.0];
        } else {
            return; // Not applicable
        }
    }

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }

    const color = Array.isArray(fuzzColorValue)
        ? new THREE.Color(fuzzColorValue[0], fuzzColorValue[1], fuzzColorValue[2])
        : new THREE.Color(fuzzColorValue);

    const displayColor = linearToSRGB(color.clone());
    mat.userData.customParams.fuzzColorHex = '#' + displayColor.getHexString();

    folder.addColor(mat.userData.customParams, 'fuzzColorHex')
        .name('Fuzz Color')
        .onChange(v => {
            const newColor = sRGBToLinear(new THREE.Color(v));
            const colorArray = [newColor.r, newColor.g, newColor.b];

            if (rawData?.fuzz_color !== undefined) {
                rawData.fuzz_color = colorArray;
            }
            if (rawData?.openPBR) {
                rawData.openPBR.fuzz_color = colorArray;
            }
            if (rawData?.openPBRShader) {
                rawData.openPBRShader.fuzz_color = colorArray;
            }
            // Update Three.js sheenColor if available
            if (mat.sheenColor) {
                mat.sheenColor.copy(newColor);
            }
            mat.needsUpdate = true;
        });
}

/**
 * Add geometry_normal (normal map) control to the material UI
 * Shows normal map status, texture info, and scale control
 */
function addGeometryNormalControl(folder, mat) {
    const rawData = mat.userData?.rawData;
    const openPBR = rawData?.openPBR || rawData?.openPBRShader;
    const geometryNormal = mat.userData?.geometry_normal;

    // Check if normal map is available
    const hasNormalMap = mat.normalMap !== null && mat.normalMap !== undefined;

    // Get normal map scale
    let normalScale = 1.0;
    if (openPBR?.normal_map_scale !== undefined) {
        normalScale = openPBR.normal_map_scale;
    } else if (mat.uniforms?.normalScale?.value) {
        normalScale = mat.uniforms.normalScale.value.x;
    } else if (mat.normalScale) {
        normalScale = mat.normalScale.x;
    }

    // Only show control if this is an OpenPBR material or has normal map
    if (!openPBR && !hasNormalMap) return;

    if (!mat.userData.customParams) {
        mat.userData.customParams = {};
    }

    // Create a subfolder for geometry/normal map settings
    const geoFolder = folder.addFolder('Geometry Normal');

    // Show normal map status
    const normalStatus = hasNormalMap ? 'Enabled' : 'None';
    geoFolder.add({ status: normalStatus }, 'status').name('Normal Map').disable();

    // Show texture ID if available
    if (geometryNormal?.textureId >= 0) {
        geoFolder.add({ textureId: `texture_id[${geometryNormal.textureId}]` }, 'textureId')
            .name('Texture ID').disable();
    }

    // Normal map scale control (only if normal map exists)
    if (hasNormalMap) {
        mat.userData.customParams.normalScale = normalScale;

        geoFolder.add(mat.userData.customParams, 'normalScale', 0, 3, 0.01)
            .name('Normal Scale')
            .onChange(v => {
                // Update material's normalScale uniform
                if (mat.uniforms?.normalScale?.value) {
                    mat.uniforms.normalScale.value.set(v, v);
                } else if (mat.normalScale) {
                    mat.normalScale.set(v, v);
                }

                // Update raw data
                if (openPBR) {
                    openPBR.normal_map_scale = v;
                }

                mat.needsUpdate = true;
            });

        // Toggle normal map
        mat.userData.customParams.normalMapEnabled = true;
        const cachedNormalMap = mat.normalMap;

        geoFolder.add(mat.userData.customParams, 'normalMapEnabled')
            .name('Enabled')
            .onChange(v => {
                mat.normalMap = v ? cachedNormalMap : null;
                mat.needsUpdate = true;
            });

        // Preview button
        geoFolder.add({
            preview: () => previewTexture(mat.normalMap, 'Normal Map')
        }, 'preview').name('Preview');
    }

    geoFolder.close();
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
 * Clear normal vector helper
 */
function clearNormalVectorHelper() {
    if (normalVectorState.helper) {
        threeState.scene.remove(normalVectorState.helper);
        if (normalVectorState.helper.geometry) {
            normalVectorState.helper.geometry.dispose();
        }
        if (normalVectorState.helper.material) {
            normalVectorState.helper.material.dispose();
        }
        normalVectorState.helper = null;
    }
}

/**
 * Create vertex normal vectors for a mesh
 */
function createVertexNormalHelper(mesh, length) {
    const geometry = mesh.geometry;
    if (!geometry) return null;

    const posAttr = geometry.attributes.position;
    const normalAttr = geometry.attributes.normal;
    if (!posAttr || !normalAttr) return null;

    const positions = [];
    const colors = [];
    const tempPos = new THREE.Vector3();
    const tempNormal = new THREE.Vector3();

    // Get world matrix
    mesh.updateMatrixWorld(true);
    const normalMatrix = new THREE.Matrix3().getNormalMatrix(mesh.matrixWorld);

    for (let i = 0; i < posAttr.count; i++) {
        tempPos.fromBufferAttribute(posAttr, i);
        tempNormal.fromBufferAttribute(normalAttr, i);

        // Transform to world space
        tempPos.applyMatrix4(mesh.matrixWorld);
        tempNormal.applyMatrix3(normalMatrix).normalize();

        // Start point
        positions.push(tempPos.x, tempPos.y, tempPos.z);
        // End point
        positions.push(
            tempPos.x + tempNormal.x * length,
            tempPos.y + tempNormal.y * length,
            tempPos.z + tempNormal.z * length
        );

        // Color based on normal direction (start: cyan, end: yellow)
        colors.push(0, 1, 1); // Cyan at base
        colors.push(1, 1, 0); // Yellow at tip
    }

    const lineGeom = new THREE.BufferGeometry();
    lineGeom.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    lineGeom.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));

    const lineMat = new THREE.LineBasicMaterial({
        vertexColors: true,
        depthTest: true,
        depthWrite: false,
        transparent: true,
        opacity: 0.8
    });

    return new THREE.LineSegments(lineGeom, lineMat);
}

/**
 * Create face normal vectors for a mesh
 */
function createFaceNormalHelper(mesh, length) {
    const geometry = mesh.geometry;
    if (!geometry) return null;

    const posAttr = geometry.attributes.position;
    const normalAttr = geometry.attributes.normal;
    const indexAttr = geometry.index;
    if (!posAttr || !normalAttr) return null;

    const positions = [];
    const colors = [];
    const tempPos = new THREE.Vector3();
    const tempNormal = new THREE.Vector3();
    const v0 = new THREE.Vector3();
    const v1 = new THREE.Vector3();
    const v2 = new THREE.Vector3();
    const n0 = new THREE.Vector3();
    const n1 = new THREE.Vector3();
    const n2 = new THREE.Vector3();
    const center = new THREE.Vector3();
    const avgNormal = new THREE.Vector3();

    // Get world matrix
    mesh.updateMatrixWorld(true);
    const normalMatrix = new THREE.Matrix3().getNormalMatrix(mesh.matrixWorld);

    const processedFaces = new Set();

    const addFace = (i0, i1, i2) => {
        const faceKey = [i0, i1, i2].sort().join(',');
        if (processedFaces.has(faceKey)) return;
        processedFaces.add(faceKey);

        v0.fromBufferAttribute(posAttr, i0);
        v1.fromBufferAttribute(posAttr, i1);
        v2.fromBufferAttribute(posAttr, i2);

        n0.fromBufferAttribute(normalAttr, i0);
        n1.fromBufferAttribute(normalAttr, i1);
        n2.fromBufferAttribute(normalAttr, i2);

        // Calculate face center
        center.copy(v0).add(v1).add(v2).divideScalar(3);

        // Average face normal
        avgNormal.copy(n0).add(n1).add(n2).normalize();

        // Transform to world space
        center.applyMatrix4(mesh.matrixWorld);
        avgNormal.applyMatrix3(normalMatrix).normalize();

        // Start point (center of face)
        positions.push(center.x, center.y, center.z);
        // End point
        positions.push(
            center.x + avgNormal.x * length,
            center.y + avgNormal.y * length,
            center.z + avgNormal.z * length
        );

        // Color: magenta at base, green at tip
        colors.push(1, 0, 1); // Magenta at base
        colors.push(0, 1, 0); // Green at tip
    };

    if (indexAttr) {
        for (let i = 0; i < indexAttr.count; i += 3) {
            addFace(indexAttr.getX(i), indexAttr.getX(i + 1), indexAttr.getX(i + 2));
        }
    } else {
        for (let i = 0; i < posAttr.count; i += 3) {
            addFace(i, i + 1, i + 2);
        }
    }

    const lineGeom = new THREE.BufferGeometry();
    lineGeom.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    lineGeom.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));

    const lineMat = new THREE.LineBasicMaterial({
        vertexColors: true,
        depthTest: true,
        depthWrite: false,
        transparent: true,
        opacity: 0.8
    });

    return new THREE.LineSegments(lineGeom, lineMat);
}

/**
 * Update normal vector visualization for selected object
 */
function updateNormalVectorVisualization() {
    clearNormalVectorHelper();

    if (!normalVectorState.enabled || !pickState.selectedObject) {
        return;
    }

    const mesh = pickState.selectedObject;
    if (!mesh.isMesh) return;

    if (normalVectorState.type === 'vertex') {
        normalVectorState.helper = createVertexNormalHelper(mesh, normalVectorState.length);
    } else {
        normalVectorState.helper = createFaceNormalHelper(mesh, normalVectorState.length);
    }

    if (normalVectorState.helper) {
        normalVectorState.helper.name = '__normalVectorHelper__';
        threeState.scene.add(normalVectorState.helper);
    }
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
    // Also clear normal vector helper when selection is cleared
    clearNormalVectorHelper();
    normalVectorState.enabled = false;
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
// Render Loop
// ============================================================================

function animate() {
    requestAnimationFrame(animate);
    threeState.controls.update();

    // Animation disabled for now - may revisit later
    // if (animationState.mixer && animationState.params.isPlaying) {
    //     const delta = threeState.clock.getDelta();
    //     const scaledDelta = delta * (animationState.params.speed / 24.0);
    //     animationState.mixer.update(scaledDelta);
    //
    //     if (animationState.action) {
    //         animationState.params.time = animationState.action.time;
    //     }
    // }

    threeState.renderer.render(threeState.scene, threeState.camera);
}

// ============================================================================
// OpenPBR Validation Framework
// ============================================================================

/**
 * OpenPBR Ground Truth BRDF Implementation
 * Based on https://academysoftwarefoundation.github.io/OpenPBR/
 */
const OpenPBRValidation = {
    // Current material parameters for validation
    params: {
        base_color: [0.9, 0.7, 0.3],
        base_metalness: 0.8,
        base_weight: 1.0,
        specular_roughness: 0.3,
        specular_ior: 1.5,
        specular_weight: 1.0
    },

    // Validation results
    results: {
        sphereCenter: null,
        sphereEdge: null,
        sphereMidpoint: null,
        groundTruth: null,
        differences: null
    },

    /**
     * Fresnel Schlick approximation
     * F(μ) = F₀ + (1 - F₀)(1 - μ)⁵
     * @param {number} cosTheta - cos of angle between view and half-vector
     * @param {number[]} f0 - Fresnel reflectance at normal incidence
     * @returns {number[]} Fresnel term RGB
     */
    fresnelSchlick(cosTheta, f0) {
        const t = Math.pow(1.0 - Math.max(0, cosTheta), 5);
        return f0.map(f => f + (1.0 - f) * t);
    },

    /**
     * Calculate F0 from IOR using Schlick's approximation
     * F₀ = ((n₁ - n₂) / (n₁ + n₂))²
     * For air (n=1) to material (n=ior)
     * @param {number} ior - Index of refraction
     * @returns {number} F0 value
     */
    iorToF0(ior) {
        const r = (ior - 1.0) / (ior + 1.0);
        return r * r;
    },

    /**
     * GGX Normal Distribution Function
     * D(m) = α² / (π * (cos²θ * (α² - 1) + 1)²)
     * @param {number} NdotH - cos of angle between normal and half-vector
     * @param {number} roughness - Surface roughness [0, 1]
     * @returns {number} NDF value
     */
    ggxNDF(NdotH, roughness) {
        const a = roughness * roughness;
        const a2 = a * a;
        const NdotH2 = NdotH * NdotH;
        const denom = NdotH2 * (a2 - 1.0) + 1.0;
        return a2 / (Math.PI * denom * denom);
    },

    /**
     * GGX Smith Geometry Function (single direction)
     * G₁(v) = 2 * NdotV / (NdotV + sqrt(α² + (1 - α²) * NdotV²))
     * @param {number} NdotV - cos of angle between normal and direction
     * @param {number} roughness - Surface roughness
     * @returns {number} Geometry term
     */
    ggxGeometry1(NdotV, roughness) {
        const a = roughness * roughness;
        const a2 = a * a;
        const NdotV2 = NdotV * NdotV;
        return 2.0 * NdotV / (NdotV + Math.sqrt(a2 + (1.0 - a2) * NdotV2));
    },

    /**
     * GGX Smith Geometry Function (full)
     * G(l, v) = G₁(l) * G₁(v)
     * @param {number} NdotV - cos of angle between normal and view
     * @param {number} NdotL - cos of angle between normal and light
     * @param {number} roughness - Surface roughness
     * @returns {number} Geometry term
     */
    ggxGeometry(NdotV, NdotL, roughness) {
        return this.ggxGeometry1(NdotV, roughness) * this.ggxGeometry1(NdotL, roughness);
    },

    /**
     * Cook-Torrance specular BRDF
     * f_spec = D * F * G / (4 * NdotL * NdotV)
     * @param {number} NdotL - cos of angle between normal and light
     * @param {number} NdotV - cos of angle between normal and view
     * @param {number} NdotH - cos of angle between normal and half-vector
     * @param {number} VdotH - cos of angle between view and half-vector
     * @param {number[]} f0 - Fresnel F0
     * @param {number} roughness - Surface roughness
     * @returns {number[]} Specular BRDF RGB
     */
    specularBRDF(NdotL, NdotV, NdotH, VdotH, f0, roughness) {
        if (NdotL <= 0 || NdotV <= 0) return [0, 0, 0];

        const D = this.ggxNDF(NdotH, roughness);
        const F = this.fresnelSchlick(VdotH, f0);
        const G = this.ggxGeometry(NdotV, NdotL, roughness);

        const denom = 4.0 * NdotL * NdotV;
        return F.map(f => (D * f * G) / Math.max(denom, 0.001));
    },

    /**
     * Lambertian diffuse BRDF
     * f_diff = base_color / π
     * @param {number[]} baseColor - Base color RGB
     * @returns {number[]} Diffuse BRDF RGB
     */
    diffuseBRDF(baseColor) {
        return baseColor.map(c => c / Math.PI);
    },

    /**
     * Full OpenPBR BRDF evaluation
     * Combines metal and dielectric responses based on metalness
     * @param {Object} params - Material parameters
     * @param {number} NdotL - cos of angle between normal and light
     * @param {number} NdotV - cos of angle between normal and view
     * @param {number} NdotH - cos of angle between normal and half-vector
     * @param {number} VdotH - cos of angle between view and half-vector
     * @returns {number[]} BRDF RGB
     */
    evaluateBRDF(params, NdotL, NdotV, NdotH, VdotH) {
        const metalness = params.base_metalness;
        const roughness = params.specular_roughness;
        const baseColor = params.base_color;
        const ior = params.specular_ior;

        // Dielectric F0 from IOR
        const dielectricF0 = this.iorToF0(ior);

        // Metal F0 is the base color (conductor Fresnel)
        const metalF0 = baseColor;

        // Blend F0 between dielectric and metal
        const f0 = baseColor.map((c, i) => {
            const dielectric = dielectricF0;
            const metal = metalF0[i];
            return dielectric * (1.0 - metalness) + metal * metalness;
        });

        // Specular contribution
        const specular = this.specularBRDF(NdotL, NdotV, NdotH, VdotH, f0, roughness);

        // Diffuse contribution (only for dielectrics)
        const fresnel = this.fresnelSchlick(VdotH, f0);
        const diffuse = this.diffuseBRDF(baseColor);

        // Metals have no diffuse, dielectrics have diffuse weighted by (1 - F)
        const result = specular.map((s, i) => {
            const diffuseContrib = diffuse[i] * (1.0 - fresnel[i]) * (1.0 - metalness);
            return s + diffuseContrib;
        });

        return result;
    },

    /**
     * Calculate expected radiance for a sphere under furnace test
     * Furnace test uses constant environment illumination
     * @param {Object} params - Material parameters
     * @param {number[]} envColor - Environment color (linear RGB)
     * @param {number} theta - Angle from sphere center (0 = center, PI/2 = edge)
     * @returns {number[]} Expected radiance RGB
     */
    furnaceTestRadiance(params, envColor, theta) {
        // View direction is always towards camera (0, 0, 1) in view space
        // Normal at theta from center: N = (sin(theta), 0, cos(theta))
        const sinTheta = Math.sin(theta);
        const cosTheta = Math.cos(theta);

        const N = [sinTheta, 0, cosTheta];
        const V = [0, 0, 1]; // View direction

        // NdotV
        const NdotV = Math.max(0, N[2]); // N.z since V = (0,0,1)

        // For furnace test, we integrate over hemisphere
        // With constant illumination, this simplifies to:
        // L_out = f_r * L_in * cos_weighted_hemisphere_integral

        // For a perfect mirror: reflect all light back
        // For diffuse: L_out = albedo * L_in (due to 1/π in BRDF and π from hemisphere integral)

        // Simplified furnace test: at each point, compute BRDF * cosine * 2π
        // (hemisphere integral of constant lighting)

        // Use multiple sample directions for more accurate integration
        const numSamples = 64;
        let accum = [0, 0, 0];

        for (let i = 0; i < numSamples; i++) {
            for (let j = 0; j < numSamples; j++) {
                // Uniform hemisphere sampling
                const u1 = (i + 0.5) / numSamples;
                const u2 = (j + 0.5) / numSamples;

                // Cosine-weighted hemisphere sampling
                const phi = 2 * Math.PI * u1;
                const r = Math.sqrt(u2);
                const x = r * Math.cos(phi);
                const y = r * Math.sin(phi);
                const z = Math.sqrt(1 - u2);

                // Transform to world space aligned with N
                // Create tangent frame
                const up = Math.abs(N[1]) < 0.999 ? [0, 1, 0] : [1, 0, 0];
                const T = this.normalize(this.cross(up, N));
                const B = this.cross(N, T);

                // Light direction in world space
                const L = [
                    T[0] * x + B[0] * y + N[0] * z,
                    T[1] * x + B[1] * y + N[1] * z,
                    T[2] * x + B[2] * y + N[2] * z
                ];

                // Half vector
                const H = this.normalize([V[0] + L[0], V[1] + L[1], V[2] + L[2]]);

                // Dot products
                const NdotL = Math.max(0, N[0] * L[0] + N[1] * L[1] + N[2] * L[2]);
                const NdotH = Math.max(0, N[0] * H[0] + N[1] * H[1] + N[2] * H[2]);
                const VdotH = Math.max(0, V[0] * H[0] + V[1] * H[1] + V[2] * H[2]);

                if (NdotL > 0) {
                    const brdf = this.evaluateBRDF(params, NdotL, NdotV, NdotH, VdotH);

                    // For cosine-weighted sampling, PDF = cos(theta) / π
                    // Contribution = BRDF * L_in * NdotL / PDF = BRDF * L_in * π
                    // But we already have cosine in sampling, so just BRDF * L_in
                    accum[0] += brdf[0] * envColor[0] * Math.PI;
                    accum[1] += brdf[1] * envColor[1] * Math.PI;
                    accum[2] += brdf[2] * envColor[2] * Math.PI;
                }
            }
        }

        const numTotalSamples = numSamples * numSamples;
        return accum.map(v => v / numTotalSamples);
    },

    // Vector math helpers
    normalize(v) {
        const len = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return len > 0 ? [v[0] / len, v[1] / len, v[2] / len] : [0, 0, 0];
    },

    cross(a, b) {
        return [
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]
        ];
    },

    dot(a, b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    },

    /**
     * Capture pixel from renderer at screen coordinates
     * @param {THREE.WebGLRenderer} renderer
     * @param {number} x - Screen X coordinate
     * @param {number} y - Screen Y coordinate
     * @returns {number[]} RGB values [0, 1]
     */
    capturePixel(renderer, x, y) {
        const renderTarget = renderer.getRenderTarget();
        const pixelBuffer = new Float32Array(4);

        // Create a small render target to read from
        const readTarget = new THREE.WebGLRenderTarget(1, 1, {
            format: THREE.RGBAFormat,
            type: THREE.FloatType
        });

        // We need to read from the main framebuffer
        // Three.js doesn't directly support reading pixels, so we use WebGL
        const gl = renderer.getContext();
        const pixels = new Uint8Array(4);
        gl.readPixels(Math.floor(x), Math.floor(renderer.domElement.height - y), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);

        readTarget.dispose();

        // Convert from 0-255 sRGB to 0-1 linear
        return [
            sRGBComponentToLinear(pixels[0] / 255),
            sRGBComponentToLinear(pixels[1] / 255),
            sRGBComponentToLinear(pixels[2] / 255)
        ];
    },

    /**
     * Get sphere screen positions for validation points
     * @param {THREE.Camera} camera
     * @param {THREE.Mesh} sphereMesh
     * @param {THREE.WebGLRenderer} renderer
     * @returns {Object} Positions for center, edge, and midpoint
     */
    getSphereScreenPositions(camera, sphereMesh, renderer) {
        const sphere = new THREE.Sphere();
        sphereMesh.geometry.computeBoundingSphere();
        sphere.copy(sphereMesh.geometry.boundingSphere);
        sphere.applyMatrix4(sphereMesh.matrixWorld);

        const center = sphere.center.clone();
        const radius = sphere.radius;

        // Get camera right vector
        const camRight = new THREE.Vector3();
        camera.getWorldDirection(camRight);
        const camUp = new THREE.Vector3(0, 1, 0);
        camRight.crossVectors(camUp, camRight).normalize();

        // Center position
        const centerScreen = this.worldToScreen(center, camera, renderer);

        // Edge position (right edge of sphere)
        const edgeWorld = center.clone().add(camRight.clone().multiplyScalar(radius * 0.95));
        const edgeScreen = this.worldToScreen(edgeWorld, camera, renderer);

        // Midpoint position (halfway between center and edge)
        const midWorld = center.clone().add(camRight.clone().multiplyScalar(radius * 0.5));
        const midScreen = this.worldToScreen(midWorld, camera, renderer);

        return {
            center: centerScreen,
            edge: edgeScreen,
            midpoint: midScreen,
            // Angular positions for ground truth calculation
            centerTheta: 0,
            edgeTheta: Math.asin(0.95), // ~72 degrees
            midpointTheta: Math.asin(0.5) // 30 degrees
        };
    },

    /**
     * Convert world position to screen coordinates
     */
    worldToScreen(worldPos, camera, renderer) {
        const pos = worldPos.clone().project(camera);
        return {
            x: (pos.x + 1) / 2 * renderer.domElement.width,
            y: (1 - pos.y) / 2 * renderer.domElement.height
        };
    },

    /**
     * Run furnace test validation
     * @param {THREE.WebGLRenderer} renderer
     * @param {THREE.Scene} scene
     * @param {THREE.Camera} camera
     * @param {THREE.Mesh} sphereMesh
     * @param {Object} materialParams - OpenPBR material parameters
     * @param {number[]} envColor - Environment color (linear RGB)
     */
    runFurnaceTest(renderer, scene, camera, sphereMesh, materialParams, envColor) {
        console.log('=== OpenPBR Furnace Test Validation ===');
        console.log('Material parameters:', materialParams);
        console.log('Environment color (linear):', envColor);

        // Update internal params
        this.params = { ...this.params, ...materialParams };

        // Get screen positions
        const positions = this.getSphereScreenPositions(camera, sphereMesh, renderer);
        console.log('Screen positions:', positions);

        // Render the scene
        renderer.render(scene, camera);

        // Capture pixels
        this.results.sphereCenter = this.capturePixel(renderer, positions.center.x, positions.center.y);
        this.results.sphereEdge = this.capturePixel(renderer, positions.edge.x, positions.edge.y);
        this.results.sphereMidpoint = this.capturePixel(renderer, positions.midpoint.x, positions.midpoint.y);

        console.log('Captured pixels (linear RGB):');
        console.log('  Center:', this.results.sphereCenter);
        console.log('  Edge:', this.results.sphereEdge);
        console.log('  Midpoint:', this.results.sphereMidpoint);

        // Calculate ground truth
        this.results.groundTruth = {
            center: this.furnaceTestRadiance(this.params, envColor, positions.centerTheta),
            edge: this.furnaceTestRadiance(this.params, envColor, positions.edgeTheta),
            midpoint: this.furnaceTestRadiance(this.params, envColor, positions.midpointTheta)
        };

        console.log('Ground truth (linear RGB):');
        console.log('  Center:', this.results.groundTruth.center);
        console.log('  Edge:', this.results.groundTruth.edge);
        console.log('  Midpoint:', this.results.groundTruth.midpoint);

        // Calculate differences
        this.results.differences = {
            center: this.calculateDifference(this.results.sphereCenter, this.results.groundTruth.center),
            edge: this.calculateDifference(this.results.sphereEdge, this.results.groundTruth.edge),
            midpoint: this.calculateDifference(this.results.sphereMidpoint, this.results.groundTruth.midpoint)
        };

        console.log('Differences (absolute):');
        console.log('  Center:', this.results.differences.center);
        console.log('  Edge:', this.results.differences.edge);
        console.log('  Midpoint:', this.results.differences.midpoint);

        // Summary
        const avgDiff = (
            this.results.differences.center.magnitude +
            this.results.differences.edge.magnitude +
            this.results.differences.midpoint.magnitude
        ) / 3;

        console.log(`Average difference magnitude: ${avgDiff.toFixed(4)}`);
        console.log('=== End Furnace Test ===');

        return this.results;
    },

    /**
     * Calculate difference between captured and expected values
     */
    calculateDifference(captured, expected) {
        const diff = captured.map((c, i) => c - expected[i]);
        const magnitude = Math.sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
        return { rgb: diff, magnitude };
    },

    /**
     * Create a simple validation report
     */
    generateReport() {
        if (!this.results.differences) {
            return 'No validation results available. Run furnace test first.';
        }

        const lines = [
            '=== OpenPBR Validation Report ===',
            '',
            'Material Parameters:',
            `  Base Color: [${this.params.base_color.map(v => v.toFixed(3)).join(', ')}]`,
            `  Metalness: ${this.params.base_metalness.toFixed(3)}`,
            `  Roughness: ${this.params.specular_roughness.toFixed(3)}`,
            `  IOR: ${this.params.specular_ior.toFixed(3)}`,
            '',
            'Results:',
            '',
            'Sphere Center:',
            `  Captured: [${this.results.sphereCenter.map(v => v.toFixed(4)).join(', ')}]`,
            `  Expected: [${this.results.groundTruth.center.map(v => v.toFixed(4)).join(', ')}]`,
            `  Diff Mag: ${this.results.differences.center.magnitude.toFixed(4)}`,
            '',
            'Sphere Edge:',
            `  Captured: [${this.results.sphereEdge.map(v => v.toFixed(4)).join(', ')}]`,
            `  Expected: [${this.results.groundTruth.edge.map(v => v.toFixed(4)).join(', ')}]`,
            `  Diff Mag: ${this.results.differences.edge.magnitude.toFixed(4)}`,
            '',
            'Sphere Midpoint:',
            `  Captured: [${this.results.sphereMidpoint.map(v => v.toFixed(4)).join(', ')}]`,
            `  Expected: [${this.results.groundTruth.midpoint.map(v => v.toFixed(4)).join(', ')}]`,
            `  Diff Mag: ${this.results.differences.midpoint.magnitude.toFixed(4)}`,
            '',
            '=== End Report ==='
        ];

        return lines.join('\n');
    }
};

/**
 * Validation state and UI
 */
const validationState = {
    enabled: false,
    useConstantEnv: true,
    envColor: '#ffffff'
};

/**
 * Setup validation UI in the GUI
 */
function setupValidationFolder(gui) {
    const validationFolder = gui.addFolder('OpenPBR Validation');

    validationFolder.add(validationState, 'enabled')
        .name('Enable Validation')
        .onChange(toggleValidationMode);

    validationFolder.add({
        runTest: () => runValidationTest()
    }, 'runTest').name('Run Furnace Test');

    validationFolder.add({
        runBRDFTests: () => runBRDFValidationTests()
    }, 'runBRDFTests').name('Run BRDF Tests');

    validationFolder.add({
        runMultiRes: () => runMultiResolutionBRDFTests()
    }, 'runMultiRes').name('Multi-Res Tests (256-1024)');

    validationFolder.add({
        runLayerTests: () => runLayerValidationTests()
    }, 'runLayerTests').name('Run Layer Tests');

    validationFolder.add({
        showReport: () => {
            const report = OpenPBRValidation.generateReport();
            console.log(report);
            alert(report);
        }
    }, 'showReport').name('Show Report');

    validationFolder.close();
}

/**
 * Run BRDF validation tests using OpenPBRValidator
 */
function runBRDFValidationTests(resolution = 256) {
    // Create validator with specified resolution
    const testValidator = new OpenPBRValidator(threeState.renderer, resolution);

    // Get material params from current sphere if available
    let testOptions = {};
    if (sceneState.root) {
        let sphereMesh = null;
        sceneState.root.traverse(obj => {
            if (obj.isMesh && !sphereMesh) {
                sphereMesh = obj;
            }
        });

        if (sphereMesh && sphereMesh.material) {
            const mat = sphereMesh.material;
            testOptions = {
                baseColor: mat.color ? [mat.color.r, mat.color.g, mat.color.b] : [0.8, 0.8, 0.8],
                metalness: mat.metalness !== undefined ? mat.metalness : 0.0,
                ior: mat.ior || 1.5
            };
        }
    }

    console.log(`\n>>> Running BRDF tests at ${resolution}x${resolution} resolution <<<\n`);
    const results = testValidator.runAllTests(testOptions);

    // Cleanup
    testValidator.dispose();

    // Show summary using the allPassed field from results
    updateStatus(`BRDF Validation (${resolution}x${resolution}): ${results.allPassed ? 'ALL PASSED' : 'SOME FAILED'}`);

    return results;
}

/**
 * Run BRDF validation at multiple resolutions to compare accuracy
 */
function runMultiResolutionBRDFTests() {
    const resolutions = [256, 512, 1024];
    const allResults = {};

    console.log('========================================');
    console.log('Multi-Resolution BRDF Validation');
    console.log('========================================');

    for (const res of resolutions) {
        allResults[res] = runBRDFValidationTests(res);
    }

    // Print comparison table
    console.log('\n========================================');
    console.log('Resolution Comparison Summary');
    console.log('========================================');
    console.log('Resolution | Fresnel Avg | GGX Avg    | Smith G Avg | Full BRDF Avg');
    console.log('-----------|-------------|------------|-------------|---------------');

    for (const res of resolutions) {
        const r = allResults[res];
        console.log(`${res.toString().padStart(10)} | ${r.fresnel.avgError.toFixed(6).padStart(11)} | ${r.ggxNDF.avgError.toFixed(6).padStart(10)} | ${r.smithG.avgError.toFixed(6).padStart(11)} | ${r.brdfFull.avgError.toFixed(6)}`);
    }
    console.log('========================================');

    updateStatus('Multi-resolution validation complete - see console');
}

/**
 * Run layer mixing and energy conservation validation tests
 */
function runLayerValidationTests(resolution = 256) {
    // Create validator with specified resolution
    const testValidator = new OpenPBRValidator(threeState.renderer, resolution);

    console.log('\n========================================');
    console.log(`Layer Validation Tests (${resolution}x${resolution})`);
    console.log('========================================\n');

    // Run layer tests
    const results = testValidator.runLayerTests();

    // Cleanup
    testValidator.dispose();

    // Show summary - results has { allPassed, configResults, energyConservation }
    updateStatus(`Layer Validation: ${results.allPassed ? 'ALL PASSED' : 'SOME FAILED'}`);

    return results;
}

/**
 * Toggle validation mode
 */
function toggleValidationMode(enabled) {
    if (enabled) {
        // Switch to constant color environment for furnace test
        settings.envMapPreset = 'constant_color';
        settings.envConstantColor = '#ffffff';
        loadEnvironment('constant_color');
        updateStatus('Validation mode: Furnace test environment enabled');
    }
}

/**
 * Run the validation test
 */
function runValidationTest() {
    if (!sceneState.root) {
        updateStatus('No scene loaded for validation');
        return;
    }

    // Find the sphere mesh
    let sphereMesh = null;
    sceneState.root.traverse(obj => {
        if (obj.isMesh && !sphereMesh) {
            sphereMesh = obj;
        }
    });

    if (!sphereMesh) {
        updateStatus('No mesh found for validation');
        return;
    }

    // Get material parameters from the sphere
    const mat = sphereMesh.material;
    const rawData = mat.userData?.rawData;

    let materialParams = { ...OpenPBRValidation.params };

    if (rawData?.openPBR || rawData?.openPBRShader) {
        const openPBR = rawData.openPBR || rawData.openPBRShader;
        if (openPBR.base_color) {
            materialParams.base_color = Array.isArray(openPBR.base_color)
                ? openPBR.base_color
                : [openPBR.base_color.r || 0.8, openPBR.base_color.g || 0.8, openPBR.base_color.b || 0.8];
        }
        if (openPBR.base_metalness !== undefined) {
            materialParams.base_metalness = openPBR.base_metalness;
        }
        if (openPBR.specular_roughness !== undefined) {
            materialParams.specular_roughness = openPBR.specular_roughness;
        }
        if (openPBR.specular_ior !== undefined) {
            materialParams.specular_ior = openPBR.specular_ior;
        }
    } else if (mat.color && mat.metalness !== undefined && mat.roughness !== undefined) {
        // Use Three.js material properties
        materialParams.base_color = [mat.color.r, mat.color.g, mat.color.b];
        materialParams.base_metalness = mat.metalness;
        materialParams.specular_roughness = mat.roughness;
        materialParams.specular_ior = mat.ior || 1.5;
    }

    // Get environment color (convert from hex to linear RGB)
    const envColor = parseHexColor(settings.envConstantColor, true);
    const envColorArray = [envColor.r, envColor.g, envColor.b];

    // Run the furnace test
    const results = OpenPBRValidation.runFurnaceTest(
        threeState.renderer,
        threeState.scene,
        threeState.camera,
        sphereMesh,
        materialParams,
        envColorArray
    );

    // Update status with summary
    const avgDiff = (
        results.differences.center.magnitude +
        results.differences.edge.magnitude +
        results.differences.midpoint.magnitude
    ) / 3;

    updateStatus(`Validation complete. Avg difference: ${avgDiff.toFixed(4)}`);
}

// Export for external access
window.OpenPBRValidation = OpenPBRValidation;
window.runValidationTest = runValidationTest;
// Debug exports
window._debugMaterialX = {
    get sceneState() { return sceneState; },
    get threeState() { return threeState; },
    get loaderState() { return loaderState; },
    get settings() { return settings; },
    selectMesh(index) {
        const meshes = [];
        if (sceneState.root) {
            sceneState.root.traverse(obj => {
                if (obj.isMesh) meshes.push(obj);
            });
        }
        if (index >= 0 && index < meshes.length) {
            selectObject(meshes[index]);
            return `Selected mesh ${index}: ${meshes[index].name || 'Unnamed'}`;
        }
        return `Invalid index. Available: 0-${meshes.length - 1}`;
    },
    listMeshes() {
        const meshes = [];
        if (sceneState.root) {
            sceneState.root.traverse(obj => {
                if (obj.isMesh) meshes.push(obj.name || 'Unnamed');
            });
        }
        return meshes;
    },
    get normalVectorState() { return normalVectorState; },
    updateNormalVectors() { updateNormalVectorVisualization(); }
};

// ============================================================================
// Start
// ============================================================================

init().catch(err => {
    console.error('Initialization failed:', err);
    updateStatus('Initialization failed: ' + err.message);
});
