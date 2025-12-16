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
// Global State
// ============================================================================

let scene, camera, renderer, controls;
let pmremGenerator = null;
let envMap = null;
let loader = null;
let nativeLoader = null;
let gui = null;
let materialFolder = null;
let textureFolder = null;
let animationFolder = null;
let timeController = null; // GUI controller for timeline scrubbing
let envPresetController = null; // GUI controller for environment preset
let sceneRoot = null;
let currentMaterials = [];
let materialData = [];
let textureCache = new Map();
let currentFileUpAxis = 'Y'; // Store the current file's upAxis (Y or Z)
let currentSceneMetadata = null; // Store current USD scene metadata
let showingNormals = false; // Track if normal visualization is active
let originalMaterialsMap = new Map(); // Store original materials when showing normals
let usdDomeLightData = null; // Store DomeLight data from USD file

// Animation-related state
let mixer = null; // Three.js AnimationMixer
let animationClips = []; // USD animation clips converted to Three.js format
let animationAction = null; // Main animation action for playback control
let clock = new THREE.Clock(); // Clock for animation timing
let animationParams = {
    isPlaying: true,
    time: 0,
    beginTime: 0,
    endTime: 10,
    speed: 24.0, // FPS (frames per second)
    autoPlay: true
};

// Settings
const settings = {
    materialType: 'auto', // 'auto', 'openpbr', 'usdpreviewsurface'
    envMapPreset: 'goegap_1k', // 'goegap_1k', 'env_sunsky_sunset', 'studio', 'constant_color'
    envMapIntensity: 1.0,
    envConstantColor: '#ffffff', // Color for constant color environment
    envColorspace: 'sRGB', // 'sRGB' (convert to linear) or 'linear' (no conversion)
    showBackground: true,
    exposure: 1.0,
    toneMapping: 'aces',
    applyUpAxisConversion: false, // HACK. disabled for a while. Apply Z-up to Y-up conversion by default
    showNormals: false // Show normals visualization
};

// Environment map presets
const ENV_PRESETS = {
    'usd_dome': 'usd', // Special marker for USD DomeLight (if available)
    'goegap_1k': 'assets/textures/goegap_1k.hdr',
    'env_sunsky_sunset': 'assets/textures/env_sunsky_sunset.hdr',
    'studio': null, // Will use synthetic studio lighting
    'constant_color': 'constant' // Special marker for constant color environment
};

// Default embedded USDA scene with OpenPBR material
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
// Initialization
// ============================================================================

async function init() {
    // Create scene
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x1a1a1a);

    // Create camera
    camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    camera.position.set(3, 2, 5);

    // Create renderer
    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = settings.exposure;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    document.getElementById('canvas-container').appendChild(renderer.domElement);

    // Create controls
    controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.screenSpacePanning = true; // Enable pan controls
    controls.minDistance = 0.1;
    controls.maxDistance = 500; // Allow much more zoom out for large scenes
    controls.mouseButtons = {
        LEFT: THREE.MOUSE.ROTATE,
        MIDDLE: THREE.MOUSE.PAN,
        RIGHT: THREE.MOUSE.DOLLY  // Right mouse drag for zoom (dolly)
    };
    controls.keys = {
        LEFT: 'ArrowLeft',
        UP: 'ArrowUp',
        RIGHT: 'ArrowRight',
        BOTTOM: 'ArrowDown'
    };
    controls.enableKeys = true;
    controls.keyPanSpeed = 20.0;

    // Create PMREM generator for environment maps
    pmremGenerator = new THREE.PMREMGenerator(renderer);
    pmremGenerator.compileEquirectangularShader();

    // Initialize TinyUSDZ loader
    updateStatus('Initializing TinyUSDZ WASM...');
    loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });
    await loader.init({ useMemory64: false });
    updateStatus('TinyUSDZ initialized');

    // Setup GUI
    setupGUI();

    // Setup event listeners
    setupEventListeners();

    // Load default environment
    await loadEnvironment(settings.envMapPreset);

    // Load default scene - fancy teapot with MaterialX
    await loadDefaultUSDFile();

    // Start render loop
    animate();
}

function setupGUI() {
    gui = new GUI({ title: 'MaterialX Demo' });
    gui.domElement.style.position = 'absolute';
    gui.domElement.style.top = '10px';
    gui.domElement.style.right = '10px';

    // Scene settings
    const sceneFolder = gui.addFolder('Scene');
    envPresetController = sceneFolder.add(settings, 'envMapPreset', Object.keys(ENV_PRESETS))
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
        .onChange(v => { renderer.toneMappingExposure = v; });
    sceneFolder.add(settings, 'toneMapping', ['none', 'linear', 'reinhard', 'cineon', 'aces', 'agx', 'neutral'])
        .name('Tone Mapping')
        .onChange(updateToneMapping);
    sceneFolder.add(settings, 'applyUpAxisConversion')
        .name('Z-up to Y-up Fix')
        .onChange(toggleUpAxisConversion);
    sceneFolder.add(settings, 'showNormals')
        .name('Show Normals')
        .onChange(toggleNormalDisplay);

    // Animation controls (will be shown when animations are loaded)
    animationFolder = gui.addFolder('Animation');
    animationFolder.add(animationParams, 'isPlaying')
        .name('Play/Pause')
        .listen();
    timeController = animationFolder.add(animationParams, 'time', 0, 10, 0.01)
        .name('Time')
        .listen()
        .onChange(scrubToTime);
    animationFolder.add(animationParams, 'speed', 0.1, 120, 0.1)
        .name('Speed (FPS)')
        .listen();
    animationFolder.add(animationParams, 'beginTime')
        .name('Begin Time')
        .listen()
        .disable();
    animationFolder.add(animationParams, 'endTime')
        .name('End Time')
        .listen()
        .disable();
    animationFolder.close(); // Start closed, will open when animations are loaded

    // Material type selector
    const materialTypeFolder = gui.addFolder('Material Type');
    materialTypeFolder.add(settings, 'materialType', ['auto', 'openpbr', 'usdpreviewsurface'])
        .name('Preferred Type')
        .onChange(reloadMaterials);

    materialTypeFolder.open();

    // Material parameters folder (populated when material is loaded)
    materialFolder = gui.addFolder('Material Parameters');
    materialFolder.open();

    // Texture preview folder
    textureFolder = gui.addFolder('Textures');
    textureFolder.close();
}

function setupEventListeners() {
    // Window resize
    window.addEventListener('resize', onWindowResize);

    // File input
    const fileInput = document.getElementById('file-input');
    fileInput.addEventListener('change', onFileSelect);

    // Drag and drop
    const container = document.getElementById('canvas-container');
    container.addEventListener('dragover', onDragOver);
    container.addEventListener('drop', onFileDrop);
    container.addEventListener('dragleave', onDragLeave);
}

// ============================================================================
// Colorspace Conversion Utilities
// ============================================================================

/**
 * Convert sRGB component to linear
 * @param {number} c - sRGB component value [0, 1]
 * @returns {number} Linear component value [0, 1]
 */
function sRGBComponentToLinear(c) {
    if (c <= 0.04045) {
        return c / 12.92;
    } else {
        return Math.pow((c + 0.055) / 1.055, 2.4);
    }
}

/**
 * Parse hex color and convert to RGB [0, 1] with optional linear conversion
 * @param {string} hexColor - Hex color string (e.g., '#ff8800')
 * @param {boolean} toLinear - If true, convert from sRGB to linear
 * @returns {object} {r, g, b} values in [0, 1]
 */
function parseHexColor(hexColor, toLinear = false) {
    // Remove # if present
    const hex = hexColor.replace('#', '');

    // Parse RGB components
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
 * Convert RGB [0, 1] to hex color string for canvas
 * @param {number} r - Red [0, 1]
 * @param {number} g - Green [0, 1]
 * @param {number} b - Blue [0, 1]
 * @returns {string} Hex color string
 */
function rgbToHex(r, g, b) {
    const toHex = (c) => {
        const clamped = Math.max(0, Math.min(1, c));
        const val = Math.round(clamped * 255);
        return val.toString(16).padStart(2, '0');
    };
    return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

// ============================================================================
// Animation Control Functions
// ============================================================================

/**
 * Scrub animation to a specific time
 * @param {number} time - Target time in seconds
 */
function scrubToTime(time) {
    if (!mixer || !animationAction) return;

    // Pause playback during scrubbing
    const wasPaused = !animationParams.isPlaying;

    // Reset mixer's internal time
    mixer.timeScale = 1.0;
    mixer.time = 0;

    // Set all actions to the target time
    animationClips.forEach((clip) => {
        const action = mixer.clipAction(clip);
        action.paused = false;
        action.enabled = true;
        action.time = time;
        action.weight = 1.0;
    });

    // Force mixer to evaluate
    const deltaForEval = 0.0001;
    mixer.update(deltaForEval);

    // Compensate for the small delta
    animationClips.forEach((clip) => {
        const action = mixer.clipAction(clip);
        action.time = time;
    });

    // Restore paused state if needed
    if (wasPaused) {
        animationClips.forEach((clip) => {
            const action = mixer.clipAction(clip);
            action.paused = true;
        });
    }

    console.log(`Scrubbed to time ${time.toFixed(3)}s`);
}

// ============================================================================
// USD Animation Extraction Functions
// ============================================================================

/**
 * Convert USD animation data to Three.js AnimationClip
 * Supports both channel/sampler and track-based animation structures
 * @param {Object} usdLoader - TinyUSDZ loader instance
 * @param {THREE.Object3D} sceneRoot - Three.js scene containing the loaded geometry
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips
 */
function convertUSDAnimationsToThreeJS(usdLoader, sceneRoot) {
    const animationClips = [];

    // Get number of animations
    const numAnimations = usdLoader.numAnimations();
    console.log(`Found ${numAnimations} animations in USD file`);

    if (numAnimations === 0) {
        return animationClips;
    }

    // Get summary of all animations
    const animationInfos = usdLoader.getAllAnimationInfos();
    console.log('Animation summaries:', animationInfos);

    // Build node index map for faster lookup
    const nodeIndexMap = new Map();
    let nodeIndex = 0;
    sceneRoot.traverse((obj) => {
        nodeIndexMap.set(nodeIndex, obj);
        nodeIndex++;
    });
    console.log(`Built node index map with ${nodeIndexMap.size} nodes`);

    // Convert each animation to Three.js format
    for (let i = 0; i < numAnimations; i++) {
        const usdAnimation = usdLoader.getAnimation(i);
        console.log(`Processing animation ${i}: ${usdAnimation.name}`);

        // Check if this is a track-based animation (legacy format)
        if (usdAnimation.tracks && usdAnimation.tracks.length > 0) {
            console.log(`Animation ${i} uses track-based format with ${usdAnimation.tracks.length} tracks`);

            // Process track-based animation
            const keyframeTracks = [];

            // Find the target object - for track animations, usually the first child after scene root
            let targetObject = sceneRoot;
            // Try to find the animated object by name from the animation
            if (usdAnimation.name) {
                let searchName = usdAnimation.name;
                // Remove common suffixes
                searchName = searchName.replace(/_xform$/, '');
                searchName = searchName.replace(/_anim$/, '');

                console.log(`Searching for target object with name: "${searchName}"`);

                // First try exact match
                let found = false;
                sceneRoot.traverse((obj) => {
                    if (obj.name === searchName) {
                        targetObject = obj;
                        found = true;
                        console.log(`  Found exact match: "${obj.name}"`);
                    }
                });

                // If no exact match, try matching without the mesh suffix
                if (!found) {
                    sceneRoot.traverse((obj) => {
                        if (obj.name && obj.name.startsWith(searchName)) {
                            targetObject = obj;
                            found = true;
                            console.log(`  Found prefix match: "${obj.name}"`);
                        }
                    });
                }
            }

            // If we can't find it by name, use the first mesh or group
            if (targetObject === sceneRoot) {
                console.warn(`Could not find target object for animation "${usdAnimation.name}", using first mesh/group`);
                sceneRoot.traverse((obj) => {
                    if ((obj.isMesh || obj.isGroup) && obj !== sceneRoot) {
                        targetObject = obj;
                        return;
                    }
                });
            }

            const targetName = targetObject.name || 'AnimatedObject';
            const targetUUID = targetObject.uuid;
            console.log(`Target object for track-based animation: "${targetName}" (UUID: ${targetUUID})`);

            // Process each track
            for (const track of usdAnimation.tracks) {
                if (!track.times || !track.values) {
                    console.warn('Track missing times or values');
                    continue;
                }

                // Convert times and values to arrays
                const times = Array.isArray(track.times) ? track.times : Array.from(track.times);
                const values = Array.isArray(track.values) ? track.values : Array.from(track.values);
                const interpolation = getUSDInterpolationMode(track.interpolation);

                console.log(`Processing track: ${track.path}, ${times.length} keyframes`);

                // Create appropriate Three.js KeyframeTrack based on path
                let keyframeTrack;

                // Use UUID-based targeting for reliability
                switch (track.path) {
                    case 'translation':
                    case 'Translation':
                        keyframeTrack = new THREE.VectorKeyframeTrack(
                            `${targetUUID}.position`,
                            times,
                            values,
                            interpolation
                        );
                        break;

                    case 'rotation':
                    case 'Rotation':
                        // Rotation is stored as quaternions (x, y, z, w)
                        keyframeTrack = new THREE.QuaternionKeyframeTrack(
                            `${targetUUID}.quaternion`,
                            times,
                            values,
                            interpolation
                        );
                        break;

                    case 'scale':
                    case 'Scale':
                        keyframeTrack = new THREE.VectorKeyframeTrack(
                            `${targetUUID}.scale`,
                            times,
                            values,
                            interpolation
                        );
                        break;

                    default:
                        console.warn(`Unknown track path: ${track.path}`);
                        continue;
                }

                if (keyframeTrack) {
                    keyframeTracks.push(keyframeTrack);
                }
            }

            // Create Three.js AnimationClip from tracks
            if (keyframeTracks.length > 0) {
                const clip = new THREE.AnimationClip(
                    usdAnimation.name || `Animation_${i}`,
                    usdAnimation.duration || -1,
                    keyframeTracks
                );

                animationClips.push(clip);
                console.log(`Created clip: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}`);
            }

            continue;
        }

        // Handle channel-based animation (newer format)
        if (!usdAnimation.channels || !usdAnimation.samplers) {
            console.warn(`Animation ${i} missing channels/samplers and tracks`);
            continue;
        }

        // Filter for node animations only (skip skeletal animations)
        const nodeChannels = usdAnimation.channels.filter(channel => {
            const targetType = channel.target_type || 'SceneNode';
            return targetType === 'SceneNode';
        });

        if (nodeChannels.length === 0) {
            console.log(`Animation ${i} has no SceneNode channels (skipping skeletal-only animation)`);
            continue;
        }

        console.log(`Animation ${i}: ${nodeChannels.length} node channels`);

        // Create Three.js KeyframeTracks from USD animation channels
        const keyframeTracks = [];

        for (const channel of nodeChannels) {
            // Get sampler data
            const sampler = usdAnimation.samplers[channel.sampler];
            if (!sampler || !sampler.times || !sampler.values) {
                console.warn(`Invalid sampler for channel`);
                continue;
            }

            // Find the Three.js object for this channel
            const targetObject = nodeIndexMap.get(channel.target_node);
            if (!targetObject) {
                console.warn(`Could not find object at node index: ${channel.target_node}`);
                continue;
            }

            // Convert times and values to arrays
            const times = Array.isArray(sampler.times) ? sampler.times : Array.from(sampler.times);
            const values = Array.isArray(sampler.values) ? sampler.values : Array.from(sampler.values);

            // Create appropriate Three.js KeyframeTrack based on path
            let keyframeTrack;
            const targetUUID = targetObject.uuid;
            const targetName = targetObject.name || `node_${channel.target_node}`;
            const interpolation = getUSDInterpolationMode(sampler.interpolation);

            console.log(`Channel: target_node=${channel.target_node}, path=${channel.path}, target_name="${targetName}"`);

            switch (channel.path) {
                case 'Translation':
                    keyframeTrack = new THREE.VectorKeyframeTrack(
                        `${targetUUID}.position`,
                        times,
                        values,
                        interpolation
                    );
                    break;

                case 'Rotation':
                    keyframeTrack = new THREE.QuaternionKeyframeTrack(
                        `${targetUUID}.quaternion`,
                        times,
                        values,
                        interpolation
                    );
                    break;

                case 'Scale':
                    keyframeTrack = new THREE.VectorKeyframeTrack(
                        `${targetUUID}.scale`,
                        times,
                        values,
                        interpolation
                    );
                    break;

                case 'Weights':
                    keyframeTrack = new THREE.NumberKeyframeTrack(
                        `.uuid[${targetUUID}].morphTargetInfluences`,
                        times,
                        values,
                        interpolation
                    );
                    break;

                default:
                    console.warn(`Unknown animation path: ${channel.path}`);
                    continue;
            }

            if (keyframeTrack) {
                keyframeTracks.push(keyframeTrack);
                console.log(`  Track "${keyframeTrack.name}": ${keyframeTrack.times.length} keyframes`);
            }
        }

        // Create Three.js AnimationClip
        if (keyframeTracks.length > 0) {
            const clip = new THREE.AnimationClip(
                usdAnimation.name || `Animation_${i}`,
                usdAnimation.duration || -1,
                keyframeTracks
            );

            animationClips.push(clip);
            console.log(`Created clip: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}`);
        }
    }

    return animationClips;
}

/**
 * Convert USD interpolation mode to Three.js InterpolateMode
 * @param {string} interpolation - USD interpolation mode (Linear, Step, CubicSpline)
 * @returns {number} Three.js InterpolateMode constant
 */
function getUSDInterpolationMode(interpolation) {
    switch (interpolation) {
        case 'Step':
        case 'STEP':
            return THREE.InterpolateDiscrete;
        case 'CubicSpline':
        case 'CUBICSPLINE':
            return THREE.InterpolateSmooth;
        case 'Linear':
        case 'LINEAR':
        default:
            return THREE.InterpolateLinear;
    }
}

// ============================================================================
// Environment Loading
// ============================================================================

async function loadEnvironment(preset) {
    settings.envMapPreset = preset;
    const path = ENV_PRESETS[preset];

    if (!path) {
        // Studio lighting - create synthetic environment
        envMap = createStudioEnvironment();
        applyEnvironment();
        return;
    }

    if (path === 'usd') {
        // USD DomeLight - use stored DomeLight data
        if (usdDomeLightData && usdDomeLightData.envMap) {
            console.log('Restoring USD DomeLight environment');
            // Restore the stored envMap and reapply
            envMap = usdDomeLightData.envMap;
            settings.envMapIntensity = usdDomeLightData.intensity || 1.0;
            applyEnvironment();
            updateStatus('Using USD DomeLight environment');
        } else {
            console.warn('USD DomeLight selected but no DomeLight data/envMap available');
            updateStatus('No USD DomeLight available - using studio lighting');
            envMap = createStudioEnvironment();
            applyEnvironment();
        }
        return;
    }

    if (path === 'constant') {
        // Constant color environment
        envMap = createConstantColorEnvironment(settings.envConstantColor, settings.envColorspace);
        applyEnvironment();
        return;
    }

    updateStatus(`Loading environment: ${preset}...`);
    try {
        const hdrLoader = new HDRLoader();
        const texture = await hdrLoader.loadAsync(path);
        envMap = pmremGenerator.fromEquirectangular(texture).texture;
        texture.dispose();
        applyEnvironment();
        updateStatus('Environment loaded');
    } catch (error) {
        console.error('Failed to load environment:', error);
        updateStatus('Failed to load environment');
        // Fall back to synthetic
        envMap = createStudioEnvironment();
        applyEnvironment();
    }
}

function createStudioEnvironment() {
    // Create a simple gradient environment
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 256;
    const ctx = canvas.getContext('2d');

    // Create gradient (light from top)
    const gradient = ctx.createLinearGradient(0, 0, 0, 256);
    gradient.addColorStop(0, '#ffffff');
    gradient.addColorStop(0.5, '#cccccc');
    gradient.addColorStop(1, '#666666');
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, 256, 256);

    const texture = new THREE.CanvasTexture(canvas);
    texture.mapping = THREE.EquirectangularReflectionMapping;
    return pmremGenerator.fromEquirectangular(texture).texture;
}

function createConstantColorEnvironment(color, colorspace = 'sRGB') {
    // Create a solid color environment
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 256;
    const ctx = canvas.getContext('2d');

    // Parse and potentially convert color based on colorspace
    let fillColor = color;
    if (colorspace === 'sRGB') {
        // Convert sRGB to linear for proper PBR workflow
        const rgb = parseHexColor(color, true); // true = convert to linear
        fillColor = rgbToHex(rgb.r, rgb.g, rgb.b);
    }
    // else: linear colorspace - use color as-is (no conversion)

    // Fill with solid color
    ctx.fillStyle = fillColor;
    ctx.fillRect(0, 0, 256, 256);

    const texture = new THREE.CanvasTexture(canvas);
    texture.mapping = THREE.EquirectangularReflectionMapping;

    // Set colorspace based on setting
    if (colorspace === 'sRGB') {
        texture.colorSpace = THREE.LinearSRGBColorSpace;
    } else {
        texture.colorSpace = THREE.LinearSRGBColorSpace; // Already linear
    }

    return pmremGenerator.fromEquirectangular(texture).texture;
}

function applyEnvironment() {
    console.log('applyEnvironment called:');
    console.log('  envMap:', envMap);
    console.log('  scene.environment before:', scene.environment);
    scene.environment = envMap;
    console.log('  scene.environment after:', scene.environment);
    updateBackground();
    updateEnvIntensity();

    // Update envMap reference on all existing materials
    console.log(`  Updating ${currentMaterials.length} materials with envMap`);
    currentMaterials.forEach((mat, idx) => {
        mat.envMap = envMap;
        mat.needsUpdate = true;  // Flag material for shader recompilation
        if (idx === 0) {
            console.log(`  Material[0] envMap: ${mat.envMap ? 'set' : 'null'}, envMapIntensity: ${mat.envMapIntensity}`);
        }
    });
}

function updateBackground() {
    if (settings.showBackground && envMap) {
        scene.background = envMap;
    } else {
        scene.background = new THREE.Color(0x1a1a1a);
    }
}

function updateConstantColorEnvironment() {
    // Only update if constant color environment is selected
    if (settings.envMapPreset === 'constant_color') {
        envMap = createConstantColorEnvironment(settings.envConstantColor, settings.envColorspace);
        applyEnvironment();
    }
}

function updateEnvIntensity() {
    currentMaterials.forEach(mat => {
        if (mat.envMapIntensity !== undefined) {
            mat.envMapIntensity = settings.envMapIntensity;
        }
    });
}

function updateToneMapping(value) {
    const mappings = {
        'none': THREE.NoToneMapping,
        'linear': THREE.LinearToneMapping,
        'reinhard': THREE.ReinhardToneMapping,
        'cineon': THREE.CineonToneMapping,
        'aces': THREE.ACESFilmicToneMapping,
        'agx': THREE.AgXToneMapping,
        'neutral': THREE.NeutralToneMapping
    };
    renderer.toneMapping = mappings[value] || THREE.ACESFilmicToneMapping;
}

// ============================================================================
// DomeLight Environment Map Loading
// ============================================================================

/**
 * Extract DomeLight from USD scene and load its environment map
 * @param {Object} usdLoader - Native USD loader instance
 * @returns {Promise<Object|null>} DomeLight data or null if not found
 */
async function loadDomeLightFromUSD(usdLoader) {
    try {
        const numLights = usdLoader.numLights ? usdLoader.numLights() : 0;
        console.log(`Checking USD for lights (found ${numLights})`);

        if (numLights === 0) {
            return null;
        }

        // Look for DomeLight
        for (let i = 0; i < numLights; i++) {
            const light = usdLoader.getLight(i);

            if (light.error) {
                console.warn(`Error getting light ${i}:`, light.error);
                continue;
            }

            console.log(`Light ${i}:`, light);

            // Check if this is a DomeLight
            if (light.type === 'dome' || light.type === 'Dome' || light.type === 'DomeLight') {
                console.log(`Found DomeLight: "${light.name || 'unnamed'}"`);

                // Check if envmapTextureId is available (texture loaded via Tydra)
                const envmapTextureId = light.envmapTextureId;
                const textureFile = light.textureFile || light.texture_file;

                if (envmapTextureId !== undefined && envmapTextureId >= 0) {
                    // Use envmapTextureId to get the image data from Tydra's images array
                    console.log(`DomeLight has envmapTextureId: ${envmapTextureId}`);

                    try {
                        const imageData = usdLoader.getImage(envmapTextureId);

                        if (imageData && imageData.data && imageData.data.length > 0) {
                            console.log(`Loaded DomeLight envmap image: ${imageData.uri || textureFile}`);
                            console.log(`  Size: ${imageData.width}x${imageData.height}, channels: ${imageData.channels}, decoded: ${imageData.decoded}`);

                            let texture = null;

                            if (imageData.decoded) {
                                // Image is already decoded - create texture from raw pixel data
                                texture = await createTextureFromDecodedData(
                                    imageData.data,
                                    imageData.width,
                                    imageData.height,
                                    imageData.channels,
                                    imageData.colorSpace
                                );
                            } else {
                                // Image is raw file data - decode using Three.js loaders
                                const uri = imageData.uri || textureFile || '';
                                texture = await decodeEnvmapFromBuffer(imageData.data, uri);
                            }

                            if (texture) {
                                // PMREM requires minimum 64x64 texture. For tiny textures, upscale to CanvasTexture
                                const MIN_PMREM_SIZE = 64;
                                const origWidth = texture.image?.width || 0;
                                const origHeight = texture.image?.height || 0;

                                if (origWidth < MIN_PMREM_SIZE || origHeight < MIN_PMREM_SIZE) {
                                    console.log(`Upscaling ${origWidth}x${origHeight} envmap to ${MIN_PMREM_SIZE}x${MIN_PMREM_SIZE} CanvasTexture`);

                                    // Extract average color from original texture
                                    let avgR = 1.0, avgG = 1.0, avgB = 1.0;
                                    const texData = texture.image?.data;
                                    if (texData && origWidth > 0 && origHeight > 0) {
                                        const isHalfFloat = texData instanceof Uint16Array;
                                        const pixelCount = origWidth * origHeight;
                                        let sumR = 0, sumG = 0, sumB = 0;

                                        // Helper to decode half-float
                                        const decodeHF = (h) => {
                                            const s = (h & 0x8000) >> 15;
                                            const e = (h & 0x7C00) >> 10;
                                            const f = h & 0x03FF;
                                            if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
                                            if (e === 0x1F) return f ? NaN : (s ? -Infinity : Infinity);
                                            return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
                                        };

                                        for (let i = 0; i < pixelCount; i++) {
                                            if (isHalfFloat) {
                                                sumR += decodeHF(texData[i * 4 + 0]);
                                                sumG += decodeHF(texData[i * 4 + 1]);
                                                sumB += decodeHF(texData[i * 4 + 2]);
                                            } else {
                                                sumR += texData[i * 4 + 0];
                                                sumG += texData[i * 4 + 1];
                                                sumB += texData[i * 4 + 2];
                                            }
                                        }
                                        avgR = sumR / pixelCount;
                                        avgG = sumG / pixelCount;
                                        avgB = sumB / pixelCount;
                                        console.log(`  Extracted avg color: R=${avgR.toFixed(4)}, G=${avgG.toFixed(4)}, B=${avgB.toFixed(4)}`);
                                    }

                                    texture.dispose();

                                    // Convert linear HDR values to sRGB for canvas (clamp to 0-1 range)
                                    const toSRGB = (v) => Math.pow(Math.max(0, Math.min(1, v)), 1/2.2);
                                    const r = Math.round(toSRGB(avgR) * 255);
                                    const g = Math.round(toSRGB(avgG) * 255);
                                    const b = Math.round(toSRGB(avgB) * 255);

                                    const canvas = document.createElement('canvas');
                                    canvas.width = MIN_PMREM_SIZE;
                                    canvas.height = MIN_PMREM_SIZE;
                                    const ctx = canvas.getContext('2d');
                                    ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
                                    ctx.fillRect(0, 0, MIN_PMREM_SIZE, MIN_PMREM_SIZE);

                                    texture = new THREE.CanvasTexture(canvas);
                                    texture.mapping = THREE.EquirectangularReflectionMapping;

                                    console.log(`  Created ${MIN_PMREM_SIZE}x${MIN_PMREM_SIZE} CanvasTexture with color rgb(${r}, ${g}, ${b})`);
                                }

                                // Generate environment map using PMREM
                                const pmremResult = pmremGenerator.fromEquirectangular(texture);
                                envMap = pmremResult.texture;
                                texture.dispose();

                                // Apply intensity and exposure
                                // Default exposure to 1.0 if not set or 0 (provides 2x intensity boost)
                                let intensity = light.intensity !== undefined ? light.intensity : 1.0;
                                const exposure = (light.exposure !== undefined && light.exposure !== 0) ? light.exposure : 1.0;
                                intensity *= Math.pow(2, exposure);

                                console.log(`DomeLight: intensity=${intensity.toFixed(2)} (base: ${light.intensity}, exposure: ${exposure})`);
                                settings.envMapIntensity = intensity;
                                settings.envMapPreset = 'usd_dome';

                                applyEnvironment();
                                updateStatus(`Loaded DomeLight environment from USD: ${imageData.uri || textureFile || 'embedded'}`);

                                // Store DomeLight data (including envMap for restoration)
                                usdDomeLightData = {
                                    name: light.name,
                                    textureFile: textureFile,
                                    envmapTextureId: envmapTextureId,
                                    intensity: intensity,
                                    color: light.color,
                                    exposure: light.exposure,
                                    envMap: envMap  // Store reference for restoration
                                };

                                return usdDomeLightData;
                            } else {
                                // Failed to decode - create fallback white CanvasTexture (PMREM needs 64x64 minimum)
                                console.warn('Failed to decode envmap, creating white fallback CanvasTexture');
                                const fallbackSize = 64;
                                const canvas = document.createElement('canvas');
                                canvas.width = fallbackSize;
                                canvas.height = fallbackSize;
                                const ctx = canvas.getContext('2d');
                                ctx.fillStyle = '#ffffff';
                                ctx.fillRect(0, 0, fallbackSize, fallbackSize);

                                let fallbackTexture = new THREE.CanvasTexture(canvas);
                                fallbackTexture.mapping = THREE.EquirectangularReflectionMapping;
                                console.log(`  Created white fallback CanvasTexture ${fallbackSize}x${fallbackSize}`);

                                // Generate environment map using PMREM
                                console.log('Generating PMREM from fallback texture...');
                                const pmremResult = pmremGenerator.fromEquirectangular(fallbackTexture);
                                envMap = pmremResult.texture;
                                fallbackTexture.dispose();

                                // Apply with default intensity
                                let intensity = light.intensity !== undefined ? light.intensity : 1.0;
                                const exposure = (light.exposure !== undefined && light.exposure !== 0) ? light.exposure : 1.0;
                                intensity *= Math.pow(2, exposure);

                                console.log(`DomeLight fallback intensity: ${intensity}`);
                                settings.envMapIntensity = intensity;
                                settings.envMapPreset = 'usd_dome';

                                applyEnvironment();

                                usdDomeLightData = {
                                    name: light.name,
                                    textureFile: textureFile,
                                    envmapTextureId: envmapTextureId,
                                    intensity: intensity,
                                    color: light.color,
                                    exposure: light.exposure,
                                    envMap: envMap
                                };

                                return usdDomeLightData;
                            }
                        } else {
                            console.warn(`Image data at index ${envmapTextureId} is empty or invalid`);
                        }
                    } catch (error) {
                        console.warn(`Failed to load envmap from image index ${envmapTextureId}:`, error);
                    }
                }

                // Fallback: Try loading from textureFile path directly (for external files)
                if (textureFile) {
                    console.log(`Falling back to direct file load: ${textureFile}`);

                    try {
                        let texture = null;

                        // Check file extension to determine loader
                        if (textureFile.toLowerCase().endsWith('.exr')) {
                            console.log(`Detected EXR file, using EXRLoader: ${textureFile}`);
                            const exrLoader = new EXRLoader();
                            texture = await exrLoader.loadAsync(textureFile);
                            texture.mapping = THREE.EquirectangularReflectionMapping;
                        } else if (textureFile.toLowerCase().endsWith('.hdr')) {
                            console.log(`Detected HDR file, using HDRLoader: ${textureFile}`);
                            const hdrLoader = new HDRLoader();
                            texture = await hdrLoader.loadAsync(textureFile);
                            texture.mapping = THREE.EquirectangularReflectionMapping;
                        } else {
                            console.warn(`Unknown texture format for file: ${textureFile}`);
                        }

                        if (texture) {
                            // Generate environment map
                            envMap = pmremGenerator.fromEquirectangular(texture).texture;
                            texture.dispose();

                            // Apply intensity and exposure (default exposure to 1.0)
                            let intensity = light.intensity !== undefined ? light.intensity : 1.0;
                            const exposure = (light.exposure !== undefined && light.exposure !== 0) ? light.exposure : 1.0;
                            intensity *= Math.pow(2, exposure);

                            settings.envMapIntensity = intensity;
                            settings.envMapPreset = 'usd_dome';

                            applyEnvironment();
                            updateStatus(`Loaded DomeLight environment directly: ${textureFile}`);

                            // Store DomeLight data (including envMap for restoration)
                            usdDomeLightData = {
                                name: light.name,
                                textureFile: textureFile,
                                intensity: intensity,
                                color: light.color,
                                exposure: light.exposure,
                                envMap: envMap
                            };

                            return usdDomeLightData;
                        }
                    } catch (fallbackError) {
                        console.warn(`Failed to load texture directly: ${fallbackError.message}`);
                    }
                }

                // Final fallback: Use DomeLight color as constant environment
                if (!textureFile || (envmapTextureId === undefined || envmapTextureId < 0)) {
                    console.log(`DomeLight has no texture, using constant color`);

                    if (light.color && light.color.length >= 3) {
                        const colorHex = rgbToHex(light.color[0], light.color[1], light.color[2]);
                        settings.envConstantColor = colorHex;
                        settings.envMapPreset = 'usd_dome';

                        // Apply intensity and exposure (default exposure to 1.0)
                        let intensity = light.intensity !== undefined ? light.intensity : 1.0;
                        const exposure = (light.exposure !== undefined && light.exposure !== 0) ? light.exposure : 1.0;
                        intensity *= Math.pow(2, exposure);
                        settings.envMapIntensity = intensity;

                        envMap = createConstantColorEnvironment(colorHex, 'linear');
                        applyEnvironment();

                        // Store DomeLight data (including envMap for restoration)
                        usdDomeLightData = {
                            name: light.name,
                            color: light.color,
                            intensity: intensity,
                            exposure: light.exposure,
                            envMap: envMap
                        };

                        return usdDomeLightData;
                    }
                }
            }
        }

        console.log('No DomeLight found in USD file');
        return null;
    } catch (error) {
        console.warn('Error loading DomeLight from USD:', error);
        return null;
    }
}

/**
 * Create Three.js texture from decoded image data (already decoded by Tydra)
 * @param {Uint8Array|Float32Array} data - Decoded pixel data
 * @param {number} width - Image width
 * @param {number} height - Image height
 * @param {number} channels - Number of channels (1, 2, 3, or 4)
 * @param {string} colorSpace - Color space (e.g., 'Lin_sRGB', 'sRGB', 'Raw')
 * @returns {Promise<THREE.Texture|null>}
 */
async function createTextureFromDecodedData(data, width, height, channels, colorSpace) {
    try {
        if (!data || !width || !height) {
            console.warn('Invalid image data for texture creation');
            return null;
        }

        // Determine if data is float or uint8
        const isFloat = data instanceof Float32Array ||
                        (data.buffer && data.BYTES_PER_ELEMENT === 4);

        let texture;

        if (isFloat) {
            // Float data - use DataTexture for HDR
            const floatData = data instanceof Float32Array ? data : new Float32Array(data.buffer);

            // Ensure we have RGBA data
            let rgbaData;
            if (channels === 4) {
                rgbaData = floatData;
            } else if (channels === 3) {
                rgbaData = new Float32Array(width * height * 4);
                for (let i = 0; i < width * height; i++) {
                    rgbaData[i * 4 + 0] = floatData[i * 3 + 0];
                    rgbaData[i * 4 + 1] = floatData[i * 3 + 1];
                    rgbaData[i * 4 + 2] = floatData[i * 3 + 2];
                    rgbaData[i * 4 + 3] = 1.0;
                }
            } else {
                console.warn(`Unsupported channel count for float data: ${channels}`);
                return null;
            }

            texture = new THREE.DataTexture(rgbaData, width, height, THREE.RGBAFormat, THREE.FloatType);
        } else {
            // Uint8 data - use canvas approach
            const canvas = document.createElement('canvas');
            canvas.width = width;
            canvas.height = height;
            const ctx = canvas.getContext('2d');
            const imageData = ctx.createImageData(width, height);

            // Convert to RGBA
            for (let i = 0; i < width * height; i++) {
                const srcIdx = i * channels;
                const dstIdx = i * 4;

                if (channels === 1) {
                    imageData.data[dstIdx + 0] = data[srcIdx];
                    imageData.data[dstIdx + 1] = data[srcIdx];
                    imageData.data[dstIdx + 2] = data[srcIdx];
                    imageData.data[dstIdx + 3] = 255;
                } else if (channels === 2) {
                    imageData.data[dstIdx + 0] = data[srcIdx];
                    imageData.data[dstIdx + 1] = data[srcIdx];
                    imageData.data[dstIdx + 2] = data[srcIdx];
                    imageData.data[dstIdx + 3] = data[srcIdx + 1];
                } else if (channels === 3) {
                    imageData.data[dstIdx + 0] = data[srcIdx + 0];
                    imageData.data[dstIdx + 1] = data[srcIdx + 1];
                    imageData.data[dstIdx + 2] = data[srcIdx + 2];
                    imageData.data[dstIdx + 3] = 255;
                } else if (channels === 4) {
                    imageData.data[dstIdx + 0] = data[srcIdx + 0];
                    imageData.data[dstIdx + 1] = data[srcIdx + 1];
                    imageData.data[dstIdx + 2] = data[srcIdx + 2];
                    imageData.data[dstIdx + 3] = data[srcIdx + 3];
                }
            }

            ctx.putImageData(imageData, 0, 0);
            texture = new THREE.CanvasTexture(canvas);
        }

        // Set mapping for environment map
        texture.mapping = THREE.EquirectangularReflectionMapping;

        // Set color space based on USD colorSpace
        if (colorSpace === 'sRGB' || colorSpace === 'sRGB_Texture') {
            texture.colorSpace = THREE.SRGBColorSpace;
        } else {
            // Linear (Lin_sRGB, Raw, etc.)
            texture.colorSpace = THREE.LinearSRGBColorSpace;
        }

        texture.needsUpdate = true;
        return texture;
    } catch (error) {
        console.error('Error creating texture from decoded data:', error);
        return null;
    }
}

/**
 * Decode environment map from raw buffer data using Three.js loaders
 * @param {Uint8Array} buffer - Raw file data
 * @param {string} uri - File URI/path for format detection
 * @returns {Promise<THREE.Texture|null>}
 */
async function decodeEnvmapFromBuffer(buffer, uri) {
    try {
        const lowerUri = uri.toLowerCase();

        // Create a Blob and Object URL for the buffer
        let mimeType = 'application/octet-stream';
        if (lowerUri.endsWith('.exr')) {
            mimeType = 'image/x-exr';
        } else if (lowerUri.endsWith('.hdr')) {
            mimeType = 'image/vnd.radiance';
        } else if (lowerUri.endsWith('.png')) {
            mimeType = 'image/png';
        } else if (lowerUri.endsWith('.jpg') || lowerUri.endsWith('.jpeg')) {
            mimeType = 'image/jpeg';
        }

        console.log(`decodeEnvmapFromBuffer: buffer size = ${buffer.length} bytes, uri = ${uri}`);
        // Show first few bytes to verify it's HDR format (should start with #?RADIANCE or similar)
        if (buffer.length > 20) {
            const header = new TextDecoder().decode(buffer.slice(0, Math.min(100, buffer.length)));
            console.log(`  Buffer header: "${header.substring(0, 50)}..."`);
        }

        const blob = new Blob([buffer], { type: mimeType });
        const objectUrl = URL.createObjectURL(blob);

        let texture = null;

        try {
            if (lowerUri.endsWith('.exr')) {
                console.log('Decoding EXR envmap from buffer');
                const exrLoader = new EXRLoader();
                texture = await exrLoader.loadAsync(objectUrl);
            } else if (lowerUri.endsWith('.hdr')) {
                console.log(`Decoding HDR envmap from buffer (${buffer.length} bytes)`);
                const hdrLoader = new HDRLoader();
                texture = await hdrLoader.loadAsync(objectUrl);
            } else {
                // Try as regular image
                console.log('Decoding envmap as regular image from buffer');
                const loader = new THREE.TextureLoader();
                texture = await loader.loadAsync(objectUrl);
            }

            if (texture) {
                texture.mapping = THREE.EquirectangularReflectionMapping;

                // Debug: analyze HDR texture data
                if (texture.image && texture.image.data) {
                    const data = texture.image.data;
                    const width = texture.image.width;
                    const height = texture.image.height;
                    let min = Infinity, max = -Infinity, sum = 0;
                    let validCount = 0;

                    // Helper to decode half-float (float16) to float32
                    const decodeHalfFloat = (h) => {
                        const s = (h & 0x8000) >> 15;
                        const e = (h & 0x7C00) >> 10;
                        const f = h & 0x03FF;
                        if (e === 0) {
                            return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
                        } else if (e === 0x1F) {
                            return f ? NaN : (s ? -Infinity : Infinity);
                        }
                        return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
                    };

                    const isHalfFloat = data instanceof Uint16Array;

                    for (let i = 0; i < data.length; i++) {
                        let val = data[i];
                        if (isHalfFloat) {
                            val = decodeHalfFloat(val);
                        }
                        if (isFinite(val)) {
                            if (val < min) min = val;
                            if (val > max) max = val;
                            sum += val;
                            validCount++;
                        }
                    }

                    const avg = validCount > 0 ? sum / validCount : 0;
                    console.log(`HDR texture loaded: ${width}x${height}`);
                    console.log(`  Data type: ${data.constructor.name}, length: ${data.length}${isHalfFloat ? ' (half-float)' : ''}`);
                    console.log(`  Values (decoded) - min: ${min.toFixed(6)}, max: ${max.toFixed(6)}, avg: ${avg.toFixed(6)}`);
                    console.log(`  Texture type: ${texture.type}, format: ${texture.format}`);
                    console.log(`  ColorSpace: ${texture.colorSpace}`);
                } else {
                    console.log('HDR texture loaded but no image.data available');
                    console.log('  Texture:', texture);
                }
            }
        } finally {
            // Clean up object URL
            URL.revokeObjectURL(objectUrl);
        }

        return texture;
    } catch (error) {
        console.error('Error decoding envmap from buffer:', error);
        return null;
    }
}

// ============================================================================
// UpAxis Conversion
// ============================================================================

function applyUpAxisConversion() {
    if (!sceneRoot) {
        console.warn('Cannot apply upAxis conversion: sceneRoot is null');
        return;
    }

    if (settings.applyUpAxisConversion && currentFileUpAxis === 'Z') {
        // Apply Z-up to Y-up conversion: rotate -90 degrees around X axis
        sceneRoot.rotation.x = -Math.PI / 2;
        console.log(`Applied Z-up to Y-up conversion (rotation.x = ${sceneRoot.rotation.x.toFixed(4)})`);
    } else {
        // Reset rotation
        sceneRoot.rotation.x = 0;
        if (currentFileUpAxis === 'Z') {
            console.log(`Z-up to Y-up conversion disabled (rotation.x = 0)`);
        } else {
            console.log(`No upAxis conversion needed (file upAxis: ${currentFileUpAxis})`);
        }
    }
}

function toggleUpAxisConversion() {
    console.log(`Toggle upAxis conversion: ${settings.applyUpAxisConversion}`);
    applyUpAxisConversion();
}

// ============================================================================
// Normal Display Mode
// ============================================================================

function toggleNormalDisplay() {
    if (!sceneRoot) {
        console.warn('Cannot toggle normal display: sceneRoot is null');
        return;
    }

    if (settings.showNormals) {
        // Switch to normal visualization
        showingNormals = true;
        sceneRoot.traverse(obj => {
            if (obj.isMesh && obj.material) {
                // Store original material
                originalMaterialsMap.set(obj, obj.material);
                // Replace with normal material
                obj.material = new THREE.MeshNormalMaterial({
                    flatShading: false
                });
            }
        });
        console.log('Normal visualization enabled');
    } else {
        // Restore original materials
        showingNormals = false;
        sceneRoot.traverse(obj => {
            if (obj.isMesh && originalMaterialsMap.has(obj)) {
                // Restore original material
                obj.material = originalMaterialsMap.get(obj);
            }
        });
        originalMaterialsMap.clear();
        console.log('Normal visualization disabled');
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
    updateStatus('Loading fancy teapot...');
    try {
        const response = await fetch('./assets/envmap-constant-test.usdz');
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.statusText}`);
        }
        const arrayBuffer = await response.arrayBuffer();
        const data = new Uint8Array(arrayBuffer);
        await loadUSDFromData(data, 'fancy-teapot-mtlx.usdz');
    } catch (error) {
        console.error('Failed to load default USD file:', error);
        updateStatus('Failed to load teapot, loading fallback scene...');
        // Fallback to embedded scene
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
    // Clear previous scene
    clearScene();

    // Create new native loader
    nativeLoader = new loader.native_.TinyUSDZLoaderNative();

    const success = nativeLoader.loadFromBinary(data, filename);
    if (!success) {
        updateStatus('Failed to parse USD file');
        return;
    }

    // Get scene metadata (including upAxis)
    const sceneMetadata = nativeLoader.getSceneMetadata ? nativeLoader.getSceneMetadata() : {};
    currentFileUpAxis = sceneMetadata.upAxis || 'Y';
    currentSceneMetadata = {
        upAxis: currentFileUpAxis,
        metersPerUnit: sceneMetadata.metersPerUnit || 1.0,
        framesPerSecond: sceneMetadata.framesPerSecond || 24.0,
        timeCodesPerSecond: sceneMetadata.timeCodesPerSecond || 24.0,
        startTimeCode: sceneMetadata.startTimeCode,
        endTimeCode: sceneMetadata.endTimeCode
    };

    console.log(`USD Scene Metadata:`, currentSceneMetadata);
    console.log(`File upAxis: "${currentFileUpAxis}"`);

    const numMeshes = nativeLoader.numMeshes();
    const numMaterials = nativeLoader.numMaterials();

    updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials (upAxis: ${currentFileUpAxis})`);

    // Load materials
    materialData = [];
    currentMaterials = [];
    textureCache.clear();

    for (let i = 0; i < numMaterials; i++) {
        try {
            const result = nativeLoader.getMaterialWithFormat(i, 'json');
            if (!result.error) {
                materialData.push(JSON.parse(result.data));
            }
        } catch (e) {
            console.warn(`Failed to get material ${i}:`, e);
        }
    }

    // Convert materials
    for (let i = 0; i < materialData.length; i++) {
        const mat = await convertMaterial(materialData[i], i);
        currentMaterials.push(mat);
    }

    // Create scene root
    sceneRoot = new THREE.Group();
    scene.add(sceneRoot);

    // Build meshes
    for (let i = 0; i < numMeshes; i++) {
        const meshData = nativeLoader.getMesh(i);
        if (!meshData) continue;

        const geometry = TinyUSDZLoaderUtils.convertUsdMeshToThreeMesh(meshData);
        if (!geometry) continue;

        // Check for submeshes (pre-computed in WASM)
        let mesh;
        if (geometry.userData['submeshes'] && geometry.userData['submeshes'].length > 0) {
            const submeshes = geometry.userData['submeshes'];
            console.log(`Mesh ${i} has ${submeshes.length} pre-computed submesh groups`);

            // Build materials array from unique material IDs
            const materials = [];
            const materialIdToIndex = new Map();

            // Collect unique material IDs
            for (const submesh of submeshes) {
                const matId = submesh.materialId;
                if (!materialIdToIndex.has(matId)) {
                    let material = new THREE.MeshPhysicalMaterial({ color: 0x888888 });
                    if (matId >= 0 && matId < currentMaterials.length) {
                        material = currentMaterials[matId];
                    }
                    materialIdToIndex.set(matId, materials.length);
                    materials.push(material);
                }
            }

            // Add geometry groups using pre-computed submesh data (already optimized in C++)
            for (const submesh of submeshes) {
                const matIndex = materialIdToIndex.get(submesh.materialId);
                geometry.addGroup(submesh.start, submesh.count, matIndex);
            }

            mesh = new THREE.Mesh(geometry, materials);
            console.log(`  Created multi-material mesh: ${materials.length} materials, ${submeshes.length} draw groups (pre-computed in WASM)`);
        } else {
            // Single material mesh
            let material = new THREE.MeshPhysicalMaterial({ color: 0x888888 });
            if (meshData.materialId !== undefined && meshData.materialId >= 0 && meshData.materialId < currentMaterials.length) {
                material = currentMaterials[meshData.materialId];
            }
            mesh = new THREE.Mesh(geometry, material);
        }

        mesh.name = meshData.name || `Mesh_${i}`;
        sceneRoot.add(mesh);
    }

    // Try to load DomeLight environment from USD (after materials are created)
    try {
        const domeLightData = await loadDomeLightFromUSD(nativeLoader);
        if (domeLightData) {
            console.log('Loaded DomeLight from USD:', domeLightData);
            // Update GUI to show USD DomeLight is selected
            if (envPresetController) {
                envPresetController.updateDisplay();
            }
        }
    } catch (error) {
        console.warn('Error checking for DomeLight:', error);
    }

    // Extract USD animations if available
    try {
        animationClips = convertUSDAnimationsToThreeJS(nativeLoader, sceneRoot);

        if (animationClips.length > 0) {
            console.log(`Extracted ${animationClips.length} animations from USD file`);

            // Create animation mixer on sceneRoot
            mixer = new THREE.AnimationMixer(sceneRoot);
            console.log('Created AnimationMixer on sceneRoot');

            // Update metadata with animation info
            if (currentSceneMetadata.startTimeCode !== undefined && currentSceneMetadata.endTimeCode !== undefined) {
                animationParams.beginTime = currentSceneMetadata.startTimeCode;
                animationParams.endTime = currentSceneMetadata.endTimeCode;
            } else if (animationClips.length > 0) {
                // Fallback to first clip duration
                animationParams.beginTime = 0;
                animationParams.endTime = animationClips[0].duration;
            }

            // Set FPS from metadata
            if (currentSceneMetadata.framesPerSecond) {
                animationParams.speed = currentSceneMetadata.framesPerSecond;
            } else if (currentSceneMetadata.timeCodesPerSecond) {
                animationParams.speed = currentSceneMetadata.timeCodesPerSecond;
            }

            // Set initial time to startTimeCode if available
            if (currentSceneMetadata.startTimeCode !== undefined) {
                animationParams.time = currentSceneMetadata.startTimeCode;
            } else {
                animationParams.time = 0;
            }

            // Play all animation clips
            animationClips.forEach((clip, index) => {
                const action = mixer.clipAction(clip);
                action.loop = THREE.LoopRepeat;

                // Set time to startTimeCode for initial evaluation
                if (currentSceneMetadata.startTimeCode !== undefined) {
                    action.time = currentSceneMetadata.startTimeCode;
                }

                action.play();
                console.log(`Playing animation ${index}: ${clip.name}, duration: ${clip.duration}s`);
            });

            // Store first action as main action
            if (animationClips.length > 0) {
                animationAction = mixer.clipAction(animationClips[0]);
            }

            // Reset clock to start fresh
            clock.start();

            // Update GUI time slider max value
            if (timeController) {
                timeController.max(animationParams.endTime);
                timeController.updateDisplay();
            }

            // Open animation folder to show controls
            if (animationFolder) {
                animationFolder.open();
            }

            updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials, ${animationClips.length} animations`);
        } else {
            console.log('No USD animations found in this USD file');
            updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials (upAxis: ${currentFileUpAxis})`);
        }
    } catch (error) {
        console.log('No animations found in USD file or animation extraction not supported:', error);
        updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials (upAxis: ${currentFileUpAxis})`);
    }

    // Apply upAxis conversion if needed
    applyUpAxisConversion();

    // Fit camera to scene
    fitCameraToScene();

    // Update material UI
    updateMaterialUI();

    document.getElementById('model-info').style.display = 'block';
    document.getElementById('mesh-count').textContent = numMeshes;
    document.getElementById('material-count').textContent = numMaterials;
}

async function convertMaterial(matData, index) {
    // Get material type info
    const typeInfo = TinyUSDZLoaderUtils.getMaterialType(matData);
    const typeString = TinyUSDZLoaderUtils.getMaterialTypeString(matData);

    console.log(`Material ${index}: ${typeString} (hasOpenPBR: ${typeInfo.hasOpenPBR}, hasUsdPreviewSurface: ${typeInfo.hasUsdPreviewSurface})`);

    try {
        const material = await TinyUSDZLoaderUtils.convertMaterial(
            matData,
            nativeLoader,
            {
                preferredMaterialType: settings.materialType,
                envMap: envMap,
                envMapIntensity: settings.envMapIntensity,
                textureCache: textureCache
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
    if (!materialData.length) return;

    updateStatus('Reloading materials...');

    // Re-convert materials with new preference
    const newMaterials = [];
    for (let i = 0; i < materialData.length; i++) {
        const mat = await convertMaterial(materialData[i], i);
        newMaterials.push(mat);
    }

    // Update meshes with new materials
    sceneRoot?.traverse(obj => {
        if (obj.isMesh && obj.material) {
            const matIndex = currentMaterials.indexOf(obj.material);
            if (matIndex >= 0 && matIndex < newMaterials.length) {
                obj.material = newMaterials[matIndex];
            }
        }
    });

    // Dispose old materials
    currentMaterials.forEach(mat => mat.dispose());
    currentMaterials = newMaterials;

    updateMaterialUI();
    updateStatus('Materials reloaded');
}

function clearScene() {
    // If showing normals, the mesh materials are MeshNormalMaterial instances
    // and the original materials are stored in originalMaterialsMap
    // Dispose the normal materials first before clearing sceneRoot
    if (showingNormals && sceneRoot) {
        sceneRoot.traverse(obj => {
            if (obj.isMesh && obj.material) {
                // This is a MeshNormalMaterial - dispose it
                if (obj.material.dispose) {
                    obj.material.dispose();
                }
            }
        });
    }
    originalMaterialsMap.clear();
    showingNormals = false;
    // Reset GUI checkbox state to match
    if (settings.showNormals) {
        settings.showNormals = false;
        // Update GUI if it exists
        if (gui) {
            gui.controllersRecursive().forEach(controller => {
                if (controller.property === 'showNormals') {
                    controller.updateDisplay();
                }
            });
        }
    }

    // Clear Three.js scene objects
    if (sceneRoot) {
        sceneRoot.traverse(obj => {
            if (obj.isMesh) {
                obj.geometry?.dispose();
                // Dispose material if not already disposed (when not in normal mode)
                if (obj.material && obj.material.dispose) {
                    if (Array.isArray(obj.material)) {
                        obj.material.forEach(m => m.dispose());
                    } else {
                        obj.material.dispose();
                    }
                }
            }
        });
        scene.remove(sceneRoot);
        sceneRoot = null;
    }

    // Clear material references (already disposed above)
    currentMaterials = [];
    materialData = [];

    // Dispose textures in cache
    textureCache.forEach((texture) => {
        if (texture && texture.dispose) {
            texture.dispose();
        }
    });
    textureCache.clear();

    // Clear animation state
    if (mixer) {
        mixer.stopAllAction();
        mixer = null;
    }
    animationClips = [];
    animationAction = null;
    animationParams.isPlaying = true;
    animationParams.time = 0;
    animationParams.beginTime = 0;
    animationParams.endTime = 10;
    clock.stop();

    // Reset upAxis to default
    currentFileUpAxis = 'Y';
    currentSceneMetadata = null;

    // Clear USD DomeLight data
    usdDomeLightData = null;

    // Clear WASM memory - reset the native loader to free render scene, assets, etc.
    if (nativeLoader) {
        // Log memory stats before clearing (for debugging)
        try {
            const stats = nativeLoader.getMemoryStats();
            console.log('Memory before reset:', stats);
        } catch (e) {
            // getMemoryStats may not exist in older builds
        }

        // Reset WASM state to free memory
        try {
            nativeLoader.reset();
            console.log('WASM memory reset complete');
        } catch (e) {
            // reset() may not exist in older builds, try clearAssets as fallback
            try {
                nativeLoader.clearAssets();
                console.log('WASM assets cleared (fallback)');
            } catch (e2) {
                console.warn('Could not clear WASM memory:', e2);
            }
        }

        // Set to null to allow GC - a new instance will be created on next load
        nativeLoader = null;
    }
}

function fitCameraToScene() {
    if (!sceneRoot) return;

    // Compute scene bounding box
    const box = new THREE.Box3().setFromObject(sceneRoot);
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());

    // Calculate the bounding sphere radius (diagonal distance from center)
    const boundingSphereRadius = size.length() * 0.5;

    // Calculate camera distance based on FOV and bounding sphere
    const fov = camera.fov * (Math.PI / 180);
    const aspectRatio = camera.aspect;

    // Calculate distance needed to fit sphere in both horizontal and vertical FOV
    const verticalFOV = fov;
    const horizontalFOV = 2 * Math.atan(Math.tan(verticalFOV / 2) * aspectRatio);

    // Use the smaller FOV to ensure object fits in both dimensions
    const effectiveFOV = Math.min(verticalFOV, horizontalFOV);

    // Distance from center to camera to fit bounding sphere
    let cameraDistance = boundingSphereRadius / Math.sin(effectiveFOV / 2);

    // Add padding (20% extra distance)
    cameraDistance *= 1.2;

    // Position camera at a nice 45-degree viewing angle
    const cameraOffset = new THREE.Vector3(
        cameraDistance * 0.5,    // X offset
        cameraDistance * 0.5,    // Y offset (elevated view)
        cameraDistance * 0.866   // Z offset (sqrt(3)/2 for 30-degree angle)
    );

    const cameraPosition = center.clone().add(cameraOffset);

    // Update camera position and orientation
    camera.position.copy(cameraPosition);
    camera.lookAt(center);

    // Update controls target to scene center
    controls.target.copy(center);

    // Update camera near/far planes based on scene size
    camera.near = Math.max(0.1, cameraDistance / 100);
    camera.far = Math.max(1000, cameraDistance * 10);
    camera.updateProjectionMatrix();

    controls.update();

    console.log(`Scene fitted: bounds=${size.x.toFixed(2)}×${size.y.toFixed(2)}×${size.z.toFixed(2)}, radius=${boundingSphereRadius.toFixed(2)}, distance=${cameraDistance.toFixed(2)}`);
}

// ============================================================================
// Color Space Utilities
// ============================================================================

// Convert sRGB to Linear (Rec.709)
// MaterialX assumes linear Rec.709 as working colorspace
function sRGBToLinear(color) {
    // sRGB to linear transformation
    const r = color.r <= 0.04045 ? color.r / 12.92 : Math.pow((color.r + 0.055) / 1.055, 2.4);
    const g = color.g <= 0.04045 ? color.g / 12.92 : Math.pow((color.g + 0.055) / 1.055, 2.4);
    const b = color.b <= 0.04045 ? color.b / 12.92 : Math.pow((color.b + 0.055) / 1.055, 2.4);
    return new THREE.Color(r, g, b);
}

// Convert Linear (Rec.709) to sRGB
function linearToSRGB(color) {
    // Linear to sRGB transformation
    const r = color.r <= 0.0031308 ? color.r * 12.92 : 1.055 * Math.pow(color.r, 1.0 / 2.4) - 0.055;
    const g = color.g <= 0.0031308 ? color.g * 12.92 : 1.055 * Math.pow(color.g, 1.0 / 2.4) - 0.055;
    const b = color.b <= 0.0031308 ? color.b * 12.92 : 1.055 * Math.pow(color.b, 1.0 / 2.4) - 0.055;
    return new THREE.Color(r, g, b);
}

// ============================================================================
// Material UI
// ============================================================================

function updateMaterialUI() {
    // Clear existing folders
    while (materialFolder.folders.length > 0) {
        materialFolder.folders[0].destroy();
    }
    while (textureFolder.folders.length > 0) {
        textureFolder.folders[0].destroy();
    }

    // Add controls for each material
    currentMaterials.forEach((mat, index) => {
        const matFolder = materialFolder.addFolder(`Material ${index}`);

        // Show material type
        const typeString = mat.userData.typeString || 'Unknown';
        matFolder.add({ type: typeString }, 'type').name('Type').disable();

        // Color
        if (mat.color) {
            // Convert from linear (material internal) to sRGB for display in color picker
            const displayColor = linearToSRGB(mat.color.clone());
            const colorObj = { color: '#' + displayColor.getHexString() };
            matFolder.addColor(colorObj, 'color').name('Base Color (sRGB)').onChange(v => {
                // Convert from sRGB (color picker) to linear (material internal)
                // MaterialX assumes linear Rec.709 as working colorspace
                const pickerColor = new THREE.Color(v);
                const linearColor = sRGBToLinear(pickerColor);
                mat.color.copy(linearColor);
            });
        }

        // Metalness
        if (mat.metalness !== undefined) {
            matFolder.add(mat, 'metalness', 0, 1, 0.01).name('Metalness');
        }

        // Roughness
        if (mat.roughness !== undefined) {
            matFolder.add(mat, 'roughness', 0, 1, 0.01).name('Roughness');
        }

        // IOR
        if (mat.ior !== undefined) {
            matFolder.add(mat, 'ior', 1, 3, 0.01).name('IOR');
        }

        // Clearcoat
        if (mat.clearcoat !== undefined) {
            matFolder.add(mat, 'clearcoat', 0, 1, 0.01).name('Clearcoat');
        }
        if (mat.clearcoatRoughness !== undefined) {
            matFolder.add(mat, 'clearcoatRoughness', 0, 1, 0.01).name('Clearcoat Roughness');
        }

        // Transmission
        if (mat.transmission !== undefined) {
            matFolder.add(mat, 'transmission', 0, 1, 0.01).name('Transmission');
        }

        // Fuzz (formerly Sheen)
        if (mat.sheen !== undefined) {
            matFolder.add(mat, 'sheen', 0, 1, 0.01).name('Fuzz');
        }

        // Diffuse Roughness (base_diffuse_roughness) - OpenPBR Oren-Nayar parameter
        // Check multiple possible locations for the value
        let diffuseRoughnessValue = undefined;
        if (mat.userData.rawData?.base_diffuse_roughness !== undefined) {
            diffuseRoughnessValue = mat.userData.rawData.base_diffuse_roughness;
        } else if (mat.userData.rawData?.openPBR?.base_diffuse_roughness !== undefined) {
            diffuseRoughnessValue = mat.userData.rawData.openPBR.base_diffuse_roughness;
        } else if (mat.userData.rawData?.openPBRShader?.base_diffuse_roughness !== undefined) {
            diffuseRoughnessValue = mat.userData.rawData.openPBRShader.base_diffuse_roughness;
        }

        if (diffuseRoughnessValue !== undefined) {
            // Store as custom property on material for easy access
            if (!mat.userData.customParams) {
                mat.userData.customParams = {};
            }
            mat.userData.customParams.baseDiffuseRoughness = diffuseRoughnessValue;

            matFolder.add(mat.userData.customParams, 'baseDiffuseRoughness', 0, 1, 0.01)
                .name('Diffuse Roughness')
                .onChange(v => {
                    // Update in all possible locations
                    if (mat.userData.rawData?.base_diffuse_roughness !== undefined) {
                        mat.userData.rawData.base_diffuse_roughness = v;
                    }
                    if (mat.userData.rawData?.openPBR?.base_diffuse_roughness !== undefined) {
                        mat.userData.rawData.openPBR.base_diffuse_roughness = v;
                    }
                    if (mat.userData.rawData?.openPBRShader?.base_diffuse_roughness !== undefined) {
                        mat.userData.rawData.openPBRShader.base_diffuse_roughness = v;
                    }
                    mat.needsUpdate = true;
                });
        }

        // Iridescence (disabled)
        // if (mat.iridescence !== undefined) {
        //     matFolder.add(mat, 'iridescence', 0, 1, 0.01).name('Iridescence');
        // }

        // Emissive
        if (mat.emissive) {
            // Convert from linear (material internal) to sRGB for display in color picker
            const displayEmissive = linearToSRGB(mat.emissive.clone());
            const emissiveObj = { emissive: '#' + displayEmissive.getHexString() };
            matFolder.addColor(emissiveObj, 'emissive').name('Emissive (sRGB)').onChange(v => {
                // Convert from sRGB (color picker) to linear (material internal)
                // MaterialX assumes linear Rec.709 as working colorspace
                const pickerColor = new THREE.Color(v);
                const linearColor = sRGBToLinear(pickerColor);
                mat.emissive.copy(linearColor);
            });
        }
        if (mat.emissiveIntensity !== undefined) {
            matFolder.add(mat, 'emissiveIntensity', 0, 10, 0.1).name('Emissive Intensity');
        }

        // Add texture info
        addTextureUI(mat, index);
    });
}

function addTextureUI(material, index) {
    const texFolder = textureFolder.addFolder(`Material ${index} Textures`);

    const textureMaps = [
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

    let hasTextures = false;

    textureMaps.forEach(({ prop, name }) => {
        const tex = material[prop];
        if (tex) {
            hasTextures = true;
            const texInfo = {
                enabled: true,
                preview: () => previewTexture(tex, name)
            };
            const folder = texFolder.addFolder(name);
            folder.add(texInfo, 'enabled').name('Enabled').onChange(v => {
                if (v) {
                    material[prop] = tex;
                } else {
                    material[prop] = null;
                }
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
    // Create preview modal
    const existing = document.getElementById('texture-preview-modal');
    if (existing) existing.remove();

    const modal = document.createElement('div');
    modal.id = 'texture-preview-modal';
    modal.style.cssText = `
        position: fixed;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        background: rgba(0, 0, 0, 0.8);
        display: flex;
        justify-content: center;
        align-items: center;
        z-index: 10000;
        cursor: pointer;
    `;
    modal.onclick = () => modal.remove();

    const container = document.createElement('div');
    container.style.cssText = `
        background: #2a2a2a;
        padding: 20px;
        border-radius: 10px;
        max-width: 80%;
        max-height: 80%;
    `;

    const title = document.createElement('h3');
    title.textContent = name;
    title.style.cssText = 'color: white; margin: 0 0 10px 0;';
    container.appendChild(title);

    // Render texture to canvas
    const canvas = document.createElement('canvas');
    canvas.width = 512;
    canvas.height = 512;
    canvas.style.cssText = 'max-width: 100%; border-radius: 5px;';

    const tempScene = new THREE.Scene();
    const tempCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    const geometry = new THREE.PlaneGeometry(2, 2);
    const material = new THREE.MeshBasicMaterial({ map: texture });
    tempScene.add(new THREE.Mesh(geometry, material));

    const tempRenderer = new THREE.WebGLRenderer({ canvas: canvas });
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
// Event Handlers
// ============================================================================

function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
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
    console.log(message);
}

// Make functions available globally for HTML buttons
window.loadFile = () => document.getElementById('file-input').click();

// ============================================================================
// Animation Loop
// ============================================================================

function animate() {
    requestAnimationFrame(animate);
    controls.update();

    // Update animation mixer if animations are loaded
    if (mixer && animationParams.isPlaying) {
        const delta = clock.getDelta();
        // Scale delta by speed (FPS)
        const scaledDelta = delta * (animationParams.speed / 24.0);
        mixer.update(scaledDelta);

        // Update time parameter for GUI
        if (animationAction) {
            animationParams.time = animationAction.time;
        }
    }

    renderer.render(scene, camera);
}

// ============================================================================
// Start
// ============================================================================

init().catch(err => {
    console.error('Initialization failed:', err);
    updateStatus('Initialization failed: ' + err.message);
});
