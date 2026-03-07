/**
 * TinyUSDZ MaterialX WebGL2 Demo
 *
 * Demonstrates MaterialX node graph support for WebGL2 using Three.js.
 * Features:
 * - Load USD files with Blender MaterialX exports
 * - Node graph optimization (invert chains, separate/combine, etc.)
 * - Real-time material preview with WebGL2
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import {
    MtlxNodeEval,
    MtlxNodeGraphProcessor,
    MtlxMaterialConverter,
    createOpenPBRMaterial,
    DEFAULT_OPENPBR_PARAMS
} from 'tinyusdz/TinyUSDZOpenPBR_WebGL.js';
import {
    optimizeNodeGraph,
    analyzeNodeGraph,
    getOptimizationSummary,
    NodeGraphOptimizationLevel
} from 'tinyusdz/TinyUSDZMaterialX.js';

// ============================================================================
// Global State
// ============================================================================

const state = {
    renderer: null,
    scene: null,
    camera: null,
    controls: null,
    loader: null,
    currentModel: null,
    usdData: null,
    optimizationEnabled: true,
    materialData: []
};

// ============================================================================
// Initialization
// ============================================================================

async function init() {
    updateStatus('Initializing WebGL2...');

    // Create renderer with WebGL2
    const canvas = document.createElement('canvas');
    const context = canvas.getContext('webgl2');

    if (!context) {
        updateStatus('WebGL2 not supported!');
        showToast('WebGL2 is not supported in this browser');
        return;
    }

    state.renderer = new THREE.WebGLRenderer({
        canvas,
        context,
        antialias: true,
        alpha: false
    });

    state.renderer.setSize(window.innerWidth, window.innerHeight);
    state.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    state.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    state.renderer.toneMappingExposure = 1.0;
    state.renderer.outputColorSpace = THREE.SRGBColorSpace;

    document.getElementById('canvas-container').appendChild(state.renderer.domElement);

    // Create scene
    state.scene = new THREE.Scene();
    state.scene.background = new THREE.Color(0x1a1a1a);

    // Create camera
    state.camera = new THREE.PerspectiveCamera(
        45,
        window.innerWidth / window.innerHeight,
        0.01,
        1000
    );
    state.camera.position.set(3, 2, 3);

    // Create controls
    state.controls = new OrbitControls(state.camera, state.renderer.domElement);
    state.controls.enableDamping = true;
    state.controls.dampingFactor = 0.05;

    // Add lights
    setupLights();

    // Add grid helper
    const gridHelper = new THREE.GridHelper(10, 10, 0x444444, 0x222222);
    state.scene.add(gridHelper);

    // Initialize TinyUSDZ loader
    updateStatus('Initializing USD loader...');
    state.loader = new TinyUSDZLoader();
    await state.loader.init({ useMemory64: false });
    state.loader.setMaxMemoryLimitMB(500);

    // Setup event handlers
    setupEventHandlers();

    // Start render loop
    animate();

    updateStatus('Ready - Load a USD file to begin');
}

function setupLights() {
    // Ambient light
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.3);
    state.scene.add(ambientLight);

    // Main directional light
    const mainLight = new THREE.DirectionalLight(0xffffff, 1.5);
    mainLight.position.set(5, 10, 7);
    mainLight.castShadow = true;
    state.scene.add(mainLight);

    // Fill light
    const fillLight = new THREE.DirectionalLight(0x8888ff, 0.5);
    fillLight.position.set(-5, 3, -5);
    state.scene.add(fillLight);

    // Rim light
    const rimLight = new THREE.DirectionalLight(0xffaa88, 0.3);
    rimLight.position.set(0, -5, -5);
    state.scene.add(rimLight);
}

function setupEventHandlers() {
    // Window resize
    window.addEventListener('resize', onWindowResize);

    // File input
    document.getElementById('file-input').addEventListener('change', onFileSelected);

    // Drag and drop
    const container = document.getElementById('canvas-container');
    container.addEventListener('dragover', onDragOver);
    container.addEventListener('dragleave', onDragLeave);
    container.addEventListener('drop', onDrop);
}

// ============================================================================
// Event Handlers
// ============================================================================

function onWindowResize() {
    state.camera.aspect = window.innerWidth / window.innerHeight;
    state.camera.updateProjectionMatrix();
    state.renderer.setSize(window.innerWidth, window.innerHeight);
}

function onDragOver(e) {
    e.preventDefault();
    e.currentTarget.classList.add('drag-over');
}

function onDragLeave(e) {
    e.currentTarget.classList.remove('drag-over');
}

function onDrop(e) {
    e.preventDefault();
    e.currentTarget.classList.remove('drag-over');

    const files = e.dataTransfer.files;
    if (files.length > 0) {
        loadUSDFile(files[0]);
    }
}

function onFileSelected(e) {
    const file = e.target.files[0];
    if (file) {
        loadUSDFile(file);
    }
}

// ============================================================================
// File Loading
// ============================================================================

async function loadUSDFile(file) {
    showLoading(true);
    updateStatus(`Loading ${file.name}...`);

    try {
        const arrayBuffer = await file.arrayBuffer();

        // Parse USD file
        state.usdData = await new Promise((resolve, reject) => {
            state.loader.parse(arrayBuffer, file.name, resolve, reject);
        });

        if (!state.usdData) {
            throw new Error('Failed to parse USD file');
        }

        // Clear existing model
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        // Process materials and build scene
        await buildScene();

        showToast(`Loaded ${file.name}`);

    } catch (error) {
        console.error('Load error:', error);
        updateStatus(`Error: ${error.message}`);
        showToast(`Failed to load: ${error.message}`);
    } finally {
        showLoading(false);
    }
}

async function buildScene() {
    const usd = state.usdData;
    const numMeshes = usd.numMeshes();
    const numMaterials = usd.numMaterials();

    updateStatus(`Processing ${numMeshes} meshes, ${numMaterials} materials...`);

    state.materialData = [];
    const materials = new Map();

    // Process materials
    for (let i = 0; i < numMaterials; i++) {
        const matResult = usd.getMaterialWithFormat(i, 'json');
        if (matResult.error) {
            console.warn(`Material ${i} error:`, matResult.error);
            continue;
        }

        const matData = JSON.parse(matResult.data);
        let nodeGraph = matData.openPBR?.nodeGraph;
        let originalNodeCount = 0;
        let optimizedNodeCount = 0;

        if (nodeGraph) {
            originalNodeCount = nodeGraph.nodegraph?.nodes?.length || 0;

            // Apply optimization if enabled
            if (state.optimizationEnabled) {
                const optimized = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
                nodeGraph = optimized;
                optimizedNodeCount = optimized.nodegraph?.nodes?.length || 0;
            } else {
                optimizedNodeCount = originalNodeCount;
            }

            state.materialData.push({
                name: matData.name || `Material_${i}`,
                originalNodes: originalNodeCount,
                optimizedNodes: optimizedNodeCount,
                nodes: nodeGraph.nodegraph?.nodes || []
            });
        }

        // Create Three.js material from processed data
        const material = createMaterialFromData(matData, nodeGraph);
        materials.set(i, material);
    }

    // Build meshes
    const root = new THREE.Group();

    for (let i = 0; i < numMeshes; i++) {
        const meshData = usd.getMesh(i);
        if (!meshData || !meshData.vertices) continue;

        const geometry = createGeometry(meshData);
        const materialId = meshData.materialId ?? 0;
        const material = materials.get(materialId) || new THREE.MeshStandardMaterial({ color: 0x888888 });

        const mesh = new THREE.Mesh(geometry, material);
        mesh.name = meshData.name || `Mesh_${i}`;
        root.add(mesh);
    }

    state.currentModel = root;
    state.scene.add(root);

    // Fit camera to model
    fitCameraToObject(root);

    // Update UI
    updateModelInfo(numMeshes, numMaterials);
    updateNodeGraphInfo();

    updateStatus('Model loaded');
}

function createMaterialFromData(matData, nodeGraph) {
    const openPBR = matData.openPBR || {};

    // Create material with OpenPBR parameters
    const params = {
        base_color: openPBR.base_color || DEFAULT_OPENPBR_PARAMS.base_color,
        base_metalness: openPBR.base_metalness ?? DEFAULT_OPENPBR_PARAMS.base_metalness,
        specular_roughness: openPBR.specular_roughness ?? DEFAULT_OPENPBR_PARAMS.specular_roughness,
        specular_ior: openPBR.specular_ior ?? DEFAULT_OPENPBR_PARAMS.specular_ior,
        coat_weight: openPBR.coat_weight ?? DEFAULT_OPENPBR_PARAMS.coat_weight,
        coat_roughness: openPBR.coat_roughness ?? DEFAULT_OPENPBR_PARAMS.coat_roughness,
        emission_color: openPBR.emission_color ?? DEFAULT_OPENPBR_PARAMS.emission_color,
        emission_luminance: openPBR.emission_luminance ?? DEFAULT_OPENPBR_PARAMS.emission_luminance
    };

    // Process node graph if present
    if (nodeGraph && nodeGraph.nodegraph) {
        const processor = new MtlxNodeGraphProcessor();
        const outputs = processor.processGraph(nodeGraph);

        // Apply node graph outputs to parameters
        for (const [name, value] of Object.entries(outputs)) {
            if (value !== undefined && !processor.needsShader(value)) {
                // Map common output names
                const paramMap = {
                    'base_color': 'base_color',
                    'baseColor': 'base_color',
                    'roughness': 'specular_roughness',
                    'specular_roughness': 'specular_roughness',
                    'metalness': 'base_metalness',
                    'base_metalness': 'base_metalness'
                };

                const paramName = paramMap[name] || name;
                if (params.hasOwnProperty(paramName)) {
                    params[paramName] = value;
                }
            }
        }
    }

    return createOpenPBRMaterial(params);
}

function createGeometry(meshData) {
    const geometry = new THREE.BufferGeometry();

    // Positions
    const positions = new Float32Array(meshData.vertices);
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));

    // Indices
    if (meshData.indices) {
        const indices = new Uint32Array(meshData.indices);
        geometry.setIndex(new THREE.BufferAttribute(indices, 1));
    }

    // Normals
    if (meshData.normals && meshData.normals.length > 0) {
        const normals = new Float32Array(meshData.normals);
        geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3));
    } else {
        geometry.computeVertexNormals();
    }

    // UVs
    if (meshData.uvs && meshData.uvs.length > 0) {
        const uvs = new Float32Array(meshData.uvs);
        geometry.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    }

    return geometry;
}

function fitCameraToObject(object) {
    const box = new THREE.Box3().setFromObject(object);
    const center = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());

    const maxDim = Math.max(size.x, size.y, size.z);
    const fov = state.camera.fov * (Math.PI / 180);
    let cameraZ = Math.abs(maxDim / 2 / Math.tan(fov / 2));
    cameraZ *= 1.5;

    state.camera.position.set(center.x + cameraZ * 0.5, center.y + cameraZ * 0.3, center.z + cameraZ);
    state.camera.lookAt(center);

    state.controls.target.copy(center);
    state.controls.update();
}

function disposeObject(obj) {
    obj.traverse((child) => {
        if (child.geometry) {
            child.geometry.dispose();
        }
        if (child.material) {
            if (Array.isArray(child.material)) {
                child.material.forEach(m => m.dispose());
            } else {
                child.material.dispose();
            }
        }
    });
}

// ============================================================================
// UI Updates
// ============================================================================

function updateStatus(text) {
    document.getElementById('status').textContent = text;
}

function updateModelInfo(meshCount, materialCount) {
    document.getElementById('mesh-count').textContent = meshCount;
    document.getElementById('material-count').textContent = materialCount;
    document.getElementById('model-info').style.display = 'block';
}

function updateNodeGraphInfo() {
    const infoDiv = document.getElementById('nodegraph-info');

    if (state.materialData.length === 0) {
        infoDiv.style.display = 'none';
        return;
    }

    const totalOriginal = state.materialData.reduce((sum, m) => sum + m.originalNodes, 0);
    const totalOptimized = state.materialData.reduce((sum, m) => sum + m.optimizedNodes, 0);
    const reduction = totalOriginal > 0
        ? Math.round((1 - totalOptimized / totalOriginal) * 100)
        : 0;

    document.getElementById('original-nodes').textContent = totalOriginal;
    document.getElementById('optimized-nodes').textContent = totalOptimized;
    document.getElementById('node-reduction').textContent = `${reduction}%`;

    // Build node list
    const nodeList = document.getElementById('node-list');
    nodeList.innerHTML = '';

    for (const mat of state.materialData) {
        if (mat.nodes.length > 0) {
            const header = document.createElement('div');
            header.style.cssText = 'color: #888; margin-top: 5px; margin-bottom: 3px;';
            header.textContent = `${mat.name} (${mat.nodes.length} nodes)`;
            nodeList.appendChild(header);

            for (const node of mat.nodes.slice(0, 10)) {
                const item = document.createElement('div');
                item.className = 'node-item';
                item.innerHTML = `
                    <span class="node-name">${node.name || 'unnamed'}</span>
                    <span class="node-type">${node.category || node.type || 'unknown'}</span>
                `;
                nodeList.appendChild(item);
            }

            if (mat.nodes.length > 10) {
                const more = document.createElement('div');
                more.style.cssText = 'color: #666; font-style: italic;';
                more.textContent = `... and ${mat.nodes.length - 10} more`;
                nodeList.appendChild(more);
            }
        }
    }

    infoDiv.style.display = 'block';
}

function showLoading(visible) {
    const overlay = document.getElementById('loading-overlay');
    overlay.classList.toggle('visible', visible);
}

function showToast(message, duration = 3000) {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.classList.add('visible');

    setTimeout(() => {
        toast.classList.remove('visible');
    }, duration);
}

// ============================================================================
// Global Functions (called from HTML)
// ============================================================================

window.loadFile = function() {
    document.getElementById('file-input').click();
};

window.loadSample = async function() {
    // Create a sample cube with a material that has a simple node graph
    showLoading(true);

    try {
        // Clear existing
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        // Create sample geometry
        const geometry = new THREE.BoxGeometry(1, 1, 1);

        // Create sample node graph evaluation
        const processor = new MtlxNodeGraphProcessor();

        // Simulate a Blender invert chain: invert -> invert = original
        const originalColor = [0.8, 0.2, 0.1];
        const inverted1 = MtlxNodeEval.invert(originalColor, 1.0);
        const inverted2 = MtlxNodeEval.invert(inverted1, 1.0);

        console.log('Original color:', originalColor);
        console.log('After double invert:', inverted2);

        // Create material with evaluated color
        const material = createOpenPBRMaterial({
            base_color: inverted2, // Should be same as original
            specular_roughness: 0.3,
            base_metalness: 0.0
        });

        const mesh = new THREE.Mesh(geometry, material);
        mesh.name = 'SampleCube';

        state.currentModel = mesh;
        state.scene.add(mesh);

        // Set material data for UI
        state.materialData = [{
            name: 'SampleMaterial',
            originalNodes: 2,
            optimizedNodes: 0,
            nodes: [
                { name: 'invert1', category: 'invert' },
                { name: 'invert2', category: 'invert' }
            ]
        }];

        fitCameraToObject(mesh);
        updateModelInfo(1, 1);
        updateNodeGraphInfo();
        updateStatus('Sample loaded - double invert demo');
        showToast('Sample cube loaded with node evaluation demo');

    } catch (error) {
        console.error('Sample error:', error);
        showToast(`Error: ${error.message}`);
    } finally {
        showLoading(false);
    }
};

window.toggleOptimization = async function() {
    state.optimizationEnabled = !state.optimizationEnabled;

    const label = document.getElementById('opt-label');
    label.textContent = state.optimizationEnabled ? 'Optimized' : 'Raw';

    showToast(`Optimization ${state.optimizationEnabled ? 'enabled' : 'disabled'}`);

    // Rebuild scene if we have USD data
    if (state.usdData) {
        showLoading(true);
        await buildScene();
        showLoading(false);
    }
};

// ============================================================================
// Render Loop
// ============================================================================

function animate() {
    requestAnimationFrame(animate);

    state.controls.update();
    state.renderer.render(state.scene, state.camera);
}

// ============================================================================
// Start
// ============================================================================

init().catch(console.error);
