// TinyUSDZ MaterialX/OpenPBR Simple Demo with Three.js
// Simple viewer for USD files with MaterialX/OpenPBR and UsdPreviewSurface material support

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RGBELoader } from 'three/examples/jsm/loaders/RGBELoader.js';
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
let sceneRoot = null;
let currentMaterials = [];
let materialData = [];
let textureCache = new Map();
let currentFileUpAxis = 'Y'; // Store the current file's upAxis (Y or Z)
let currentSceneMetadata = null; // Store current USD scene metadata
let showingNormals = false; // Track if normal visualization is active
let originalMaterialsMap = new Map(); // Store original materials when showing normals

// Settings
const settings = {
    materialType: 'auto', // 'auto', 'openpbr', 'usdpreviewsurface'
    envMapPreset: 'goegap_1k', // 'goegap_1k', 'env_sunsky_sunset', 'studio'
    envMapIntensity: 1.0,
    showBackground: true,
    exposure: 1.0,
    toneMapping: 'aces',
    applyUpAxisConversion: false, // HACK. disabled for a while. Apply Z-up to Y-up conversion by default
    showNormals: false // Show normals visualization
};

// Environment map presets
const ENV_PRESETS = {
    'goegap_1k': 'assets/textures/goegap_1k.hdr',
    'env_sunsky_sunset': 'assets/textures/env_sunsky_sunset.hdr',
    'studio': null // Will use synthetic studio lighting
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
    sceneFolder.add(settings, 'envMapPreset', Object.keys(ENV_PRESETS))
        .name('Environment')
        .onChange(loadEnvironment);
    sceneFolder.add(settings, 'envMapIntensity', 0, 3, 0.1)
        .name('Env Intensity')
        .onChange(updateEnvIntensity);
    sceneFolder.add(settings, 'showBackground')
        .name('Show Background')
        .onChange(updateBackground);
    sceneFolder.add(settings, 'exposure', 0, 3, 0.1)
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

    updateStatus(`Loading environment: ${preset}...`);
    try {
        const rgbeLoader = new RGBELoader();
        const texture = await rgbeLoader.loadAsync(path);
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

function applyEnvironment() {
    scene.environment = envMap;
    updateBackground();
    updateEnvIntensity();
}

function updateBackground() {
    if (settings.showBackground && envMap) {
        scene.background = envMap;
    } else {
        scene.background = new THREE.Color(0x1a1a1a);
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
        const response = await fetch('./assets/fancy-teapot-mtlx.usdz');
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
        timeCodesPerSecond: sceneMetadata.timeCodesPerSecond || 24.0
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
    if (sceneRoot) {
        sceneRoot.traverse(obj => {
            if (obj.isMesh) {
                obj.geometry?.dispose();
            }
        });
        scene.remove(sceneRoot);
        sceneRoot = null;
    }

    currentMaterials.forEach(mat => mat.dispose());
    currentMaterials = [];
    materialData = [];
    textureCache.clear();

    // Reset upAxis to default
    currentFileUpAxis = 'Y';
    currentSceneMetadata = null;
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
    renderer.render(scene, camera);
}

// ============================================================================
// Start
// ============================================================================

init().catch(err => {
    console.error('Initialization failed:', err);
    updateStatus('Initialization failed: ' + err.message);
});
