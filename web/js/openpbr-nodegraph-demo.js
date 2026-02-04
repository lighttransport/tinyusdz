/**
 * OpenPBR NodeGraph Demo
 *
 * Standalone demo showing OpenPBR materials with MaterialX node graph visualization.
 * Uses Three.js for 3D rendering and LiteGraph.js for node graph display.
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import {
    MtlxNodeGraphProcessor,
    createOpenPBRMaterial,
    DEFAULT_OPENPBR_PARAMS
} from 'tinyusdz/TinyUSDZOpenPBR_WebGL.js';
import {
    optimizeNodeGraph,
    NodeGraphOptimizationLevel
} from 'tinyusdz/TinyUSDZMaterialX.js';

// Import bridge client (uses relative path to blender-bridge)
let BridgeClient = null;
try {
    const bridgeModule = await import('../blender-bridge/client/bridge-client.js');
    BridgeClient = bridgeModule.BridgeClient;
    console.log('BridgeClient loaded');
} catch (e) {
    console.warn('BridgeClient not available:', e.message);
}

// ============================================================================
// Global State
// ============================================================================

const state = {
    // Three.js
    renderer: null,
    scene: null,
    camera: null,
    controls: null,

    // USD
    loader: null,
    usdData: null,
    currentModel: null,

    // Materials
    materials: [],
    materialData: [],
    currentMaterialIndex: 0,

    // LiteGraph
    graph: null,
    graphCanvas: null,

    // Blender Bridge
    bridgeClient: null,
    bridgeConnected: false
};

// ============================================================================
// LiteGraph Custom Node Types
// ============================================================================

function registerMtlxNodeTypes() {
    // Base class for MaterialX nodes
    function MtlxNode() {
        this.color = '#335544';
    }

    // Constant node
    function ConstantNode() {
        this.addOutput('out', 'color3');
        this.properties = { value: [0.8, 0.8, 0.8] };
        this.color = '#445566';
        this.size = [160, 60];
    }
    ConstantNode.title = 'Constant';
    ConstantNode.prototype.onDrawForeground = function(ctx) {
        const v = this.properties.value;
        if (Array.isArray(v)) {
            ctx.fillStyle = `rgb(${Math.floor(v[0]*255)},${Math.floor(v[1]*255)},${Math.floor(v[2]*255)})`;
            ctx.fillRect(10, 30, this.size[0] - 20, 20);
        }
    };
    LiteGraph.registerNodeType('mtlx/constant', ConstantNode);

    // Image/Texture node
    function ImageNode() {
        this.addInput('texcoord', 'vector2');
        this.addOutput('out', 'color4');
        this.properties = { file: '' };
        this.color = '#664433';
        this.size = [180, 70];
    }
    ImageNode.title = 'Image';
    ImageNode.prototype.onDrawForeground = function(ctx) {
        ctx.fillStyle = '#888';
        ctx.font = '10px monospace';
        const file = this.properties.file || 'no file';
        const shortFile = file.length > 20 ? '...' + file.slice(-18) : file;
        ctx.fillText(shortFile, 10, 50);
    };
    LiteGraph.registerNodeType('mtlx/image', ImageNode);

    // Math operation nodes
    const mathOps = [
        { name: 'add', title: 'Add', color: '#446655' },
        { name: 'subtract', title: 'Subtract', color: '#554466' },
        { name: 'multiply', title: 'Multiply', color: '#556644' },
        { name: 'divide', title: 'Divide', color: '#665544' },
        { name: 'power', title: 'Power', color: '#445566' },
        { name: 'min', title: 'Min', color: '#446666' },
        { name: 'max', title: 'Max', color: '#664466' }
    ];

    for (const op of mathOps) {
        const NodeClass = function() {
            this.addInput('in1', 'any');
            this.addInput('in2', 'any');
            this.addOutput('out', 'any');
            this.color = op.color;
            this.size = [120, 60];
        };
        NodeClass.title = op.title;
        LiteGraph.registerNodeType(`mtlx/${op.name}`, NodeClass);
    }

    // Unary operations
    const unaryOps = [
        { name: 'absval', title: 'Abs' },
        { name: 'sqrt', title: 'Sqrt' },
        { name: 'floor', title: 'Floor' },
        { name: 'ceil', title: 'Ceil' },
        { name: 'negate', title: 'Negate' },
        { name: 'normalize', title: 'Normalize' },
        { name: 'luminance', title: 'Luminance' }
    ];

    for (const op of unaryOps) {
        const NodeClass = function() {
            this.addInput('in', 'any');
            this.addOutput('out', 'any');
            this.color = '#555566';
            this.size = [100, 50];
        };
        NodeClass.title = op.title;
        LiteGraph.registerNodeType(`mtlx/${op.name}`, NodeClass);
    }

    // Mix node
    function MixNode() {
        this.addInput('bg', 'color3');
        this.addInput('fg', 'color3');
        this.addInput('mix', 'float');
        this.addOutput('out', 'color3');
        this.color = '#665555';
        this.size = [120, 80];
    }
    MixNode.title = 'Mix';
    LiteGraph.registerNodeType('mtlx/mix', MixNode);

    // Clamp node
    function ClampNode() {
        this.addInput('in', 'any');
        this.addInput('low', 'any');
        this.addInput('high', 'any');
        this.addOutput('out', 'any');
        this.color = '#556655';
        this.size = [120, 80];
    }
    ClampNode.title = 'Clamp';
    LiteGraph.registerNodeType('mtlx/clamp', ClampNode);

    // Extract (separate) node
    function ExtractNode() {
        this.addInput('in', 'color3');
        this.addOutput('out', 'float');
        this.properties = { index: 0 };
        this.color = '#556666';
        this.size = [120, 50];
    }
    ExtractNode.title = 'Extract';
    ExtractNode.prototype.onDrawForeground = function(ctx) {
        const channels = ['R', 'G', 'B', 'A'];
        ctx.fillStyle = '#aaa';
        ctx.font = '12px sans-serif';
        ctx.fillText(`Channel: ${channels[this.properties.index] || this.properties.index}`, 10, 35);
    };
    LiteGraph.registerNodeType('mtlx/extract', ExtractNode);

    // Combine node
    function CombineNode() {
        this.addInput('in1', 'float');
        this.addInput('in2', 'float');
        this.addInput('in3', 'float');
        this.addOutput('out', 'color3');
        this.color = '#665566';
        this.size = [120, 80];
    }
    CombineNode.title = 'Combine3';
    LiteGraph.registerNodeType('mtlx/combine3', CombineNode);

    // HSV Adjust node
    function HsvAdjustNode() {
        this.addInput('in', 'color3');
        this.addInput('amount', 'vector3');
        this.addOutput('out', 'color3');
        this.color = '#664455';
        this.size = [130, 60];
    }
    HsvAdjustNode.title = 'HSV Adjust';
    LiteGraph.registerNodeType('mtlx/hsvadjust', HsvAdjustNode);

    // Invert node
    function InvertNode() {
        this.addInput('in', 'color3');
        this.addInput('amount', 'float');
        this.addOutput('out', 'color3');
        this.color = '#554455';
        this.size = [120, 60];
    }
    InvertNode.title = 'Invert';
    LiteGraph.registerNodeType('mtlx/invert', InvertNode);

    // Remap node
    function RemapNode() {
        this.addInput('in', 'any');
        this.addInput('inlow', 'any');
        this.addInput('inhigh', 'any');
        this.addInput('outlow', 'any');
        this.addInput('outhigh', 'any');
        this.addOutput('out', 'any');
        this.color = '#556655';
        this.size = [130, 110];
    }
    RemapNode.title = 'Remap';
    LiteGraph.registerNodeType('mtlx/remap', RemapNode);

    // TexCoord node
    function TexCoordNode() {
        this.addOutput('out', 'vector2');
        this.color = '#446655';
        this.size = [100, 40];
    }
    TexCoordNode.title = 'TexCoord';
    LiteGraph.registerNodeType('mtlx/texcoord', TexCoordNode);

    // Position node
    function PositionNode() {
        this.addOutput('out', 'vector3');
        this.properties = { space: 'object' };
        this.color = '#445566';
        this.size = [110, 50];
    }
    PositionNode.title = 'Position';
    PositionNode.prototype.onDrawForeground = function(ctx) {
        ctx.fillStyle = '#888';
        ctx.font = '10px sans-serif';
        ctx.fillText(this.properties.space, 10, 35);
    };
    LiteGraph.registerNodeType('mtlx/position', PositionNode);

    // Normal node
    function NormalNode() {
        this.addOutput('out', 'vector3');
        this.properties = { space: 'object' };
        this.color = '#445566';
        this.size = [110, 50];
    }
    NormalNode.title = 'Normal';
    NormalNode.prototype.onDrawForeground = function(ctx) {
        ctx.fillStyle = '#888';
        ctx.font = '10px sans-serif';
        ctx.fillText(this.properties.space, 10, 35);
    };
    LiteGraph.registerNodeType('mtlx/normal', NormalNode);

    // OpenPBR Surface output node
    function OpenPBRSurfaceNode() {
        this.addInput('base_color', 'color3');
        this.addInput('base_metalness', 'float');
        this.addInput('specular_roughness', 'float');
        this.addInput('specular_ior', 'float');
        this.addInput('coat_weight', 'float');
        this.addInput('emission_color', 'color3');
        this.addInput('geometry_opacity', 'float');
        this.color = '#4CAF50';
        this.size = [180, 150];
    }
    OpenPBRSurfaceNode.title = 'OpenPBR Surface';
    LiteGraph.registerNodeType('mtlx/openpbr_surface', OpenPBRSurfaceNode);

    // Generic node for unknown types
    function GenericNode() {
        this.addInput('in', 'any');
        this.addOutput('out', 'any');
        this.color = '#555555';
        this.size = [100, 50];
    }
    GenericNode.title = 'Node';
    LiteGraph.registerNodeType('mtlx/generic', GenericNode);
}

// ============================================================================
// LiteGraph Setup
// ============================================================================

function initLiteGraph() {
    // Configure LiteGraph appearance
    LiteGraph.NODE_DEFAULT_COLOR = '#333344';
    LiteGraph.NODE_DEFAULT_BGCOLOR = '#222233';
    LiteGraph.NODE_DEFAULT_BOXCOLOR = '#666677';
    LiteGraph.NODE_TITLE_COLOR = '#eee';
    LiteGraph.LINK_COLOR = '#9ecae1';
    LiteGraph.DEFAULT_SHADOW_COLOR = 'rgba(0,0,0,0.5)';

    // Register custom node types
    registerMtlxNodeTypes();

    // Create graph
    state.graph = new LiteGraph.LGraph();

    // Create canvas
    const canvas = document.getElementById('nodegraph-canvas');
    state.graphCanvas = new LiteGraph.LGraphCanvas(canvas, state.graph);

    // Configure canvas
    state.graphCanvas.background_image = null;
    state.graphCanvas.clear_background_color = '#1a1a2e';
    state.graphCanvas.render_shadows = false;
    state.graphCanvas.render_connection_arrows = true;
    state.graphCanvas.connections_width = 2;

    // Handle resize
    resizeGraphCanvas();
    window.addEventListener('resize', resizeGraphCanvas);
}

function resizeGraphCanvas() {
    const canvas = document.getElementById('nodegraph-canvas');
    const panel = document.getElementById('nodegraph-panel');
    const header = panel.querySelector('.nodegraph-header');

    canvas.width = panel.clientWidth;
    canvas.height = panel.clientHeight - header.clientHeight;

    if (state.graphCanvas) {
        state.graphCanvas.resize(canvas.width, canvas.height);
    }
}

// ============================================================================
// Convert MaterialX NodeGraph to LiteGraph
// ============================================================================

function buildLiteGraphFromMtlx(nodeGraphData, materialName) {
    state.graph.clear();

    if (!nodeGraphData || !nodeGraphData.nodegraph) {
        // Create a simple constant output for materials without node graphs
        const constNode = LiteGraph.createNode('mtlx/constant');
        constNode.pos = [100, 200];
        constNode.properties.value = [0.8, 0.8, 0.8];
        state.graph.add(constNode);

        const surfaceNode = LiteGraph.createNode('mtlx/openpbr_surface');
        surfaceNode.pos = [400, 150];
        surfaceNode.title = materialName || 'OpenPBR Surface';
        state.graph.add(surfaceNode);

        constNode.connect(0, surfaceNode, 0);

        updateNodeStats();
        return;
    }

    const ng = nodeGraphData.nodegraph;
    const nodes = ng.nodes || [];
    const outputs = ng.outputs || [];
    const connections = nodeGraphData.connections || [];

    // Map to store created LiteGraph nodes by name
    const nodeMap = new Map();

    // Layout parameters
    const startX = 50;
    const startY = 50;
    const nodeWidth = 140;
    const nodeHeight = 70;
    const xSpacing = 200;
    const ySpacing = 100;

    // Build dependency levels for layout
    const levels = computeNodeLevels(nodes);

    // Create nodes
    for (const node of nodes) {
        const category = getBaseCategory(node.category || node.type);
        const nodeType = `mtlx/${category}`;

        let lgNode;
        if (LiteGraph.registered_node_types[nodeType]) {
            lgNode = LiteGraph.createNode(nodeType);
        } else {
            lgNode = LiteGraph.createNode('mtlx/generic');
            lgNode.title = category;
        }

        lgNode.title = node.name || category;

        // Set position based on level
        const level = levels.get(node.name) || 0;
        const nodesAtLevel = [...levels.entries()].filter(([_, l]) => l === level);
        const indexAtLevel = nodesAtLevel.findIndex(([n, _]) => n === node.name);

        lgNode.pos = [
            startX + level * xSpacing,
            startY + indexAtLevel * ySpacing
        ];

        // Set properties
        if (node.value !== undefined) {
            lgNode.properties = lgNode.properties || {};
            lgNode.properties.value = node.value;
        }

        if (node.inputs) {
            for (const input of node.inputs) {
                if (input.value !== undefined && !input.nodename) {
                    lgNode.properties = lgNode.properties || {};
                    lgNode.properties[input.name] = input.value;
                }
            }
        }

        // Handle specific node properties
        if (category === 'extract' || category === 'separate' || category === 'separate3') {
            lgNode.properties = lgNode.properties || {};
            const indexInput = node.inputs?.find(i => i.name === 'index');
            lgNode.properties.index = indexInput?.value ?? 0;
        }

        if (category === 'image' || category === 'tiledimage') {
            lgNode.properties = lgNode.properties || {};
            const fileInput = node.inputs?.find(i => i.name === 'file');
            lgNode.properties.file = fileInput?.value || '';
        }

        state.graph.add(lgNode);
        nodeMap.set(node.name, { node: lgNode, data: node });
    }

    // Create output surface node
    const surfaceNode = LiteGraph.createNode('mtlx/openpbr_surface');
    surfaceNode.pos = [startX + (Math.max(...levels.values()) + 1) * xSpacing, startY + 100];
    surfaceNode.title = materialName || 'OpenPBR Surface';
    state.graph.add(surfaceNode);

    // Create connections between nodes
    for (const node of nodes) {
        const lgNodeEntry = nodeMap.get(node.name);
        if (!lgNodeEntry) continue;

        const lgNode = lgNodeEntry.node;

        if (node.inputs) {
            for (let i = 0; i < node.inputs.length; i++) {
                const input = node.inputs[i];
                if (input.nodename) {
                    const sourceEntry = nodeMap.get(input.nodename);
                    if (sourceEntry) {
                        // Find output slot (default to 0)
                        const outputSlot = 0;
                        // Find input slot by name
                        let inputSlot = lgNode.findInputSlot(input.name);
                        if (inputSlot === -1) inputSlot = i;
                        if (inputSlot === -1) inputSlot = 0;

                        sourceEntry.node.connect(outputSlot, lgNode, inputSlot);
                    }
                }
            }
        }
    }

    // Connect outputs to surface node
    for (const output of outputs) {
        if (output.nodename) {
            const sourceEntry = nodeMap.get(output.nodename);
            if (sourceEntry) {
                // Map output name to surface input
                const surfaceInputMap = {
                    'base_color': 0,
                    'baseColor': 0,
                    'diffuseColor': 0,
                    'base_metalness': 1,
                    'metalness': 1,
                    'specular_roughness': 2,
                    'roughness': 2,
                    'specular_ior': 3,
                    'coat_weight': 4,
                    'emission_color': 5,
                    'geometry_opacity': 6,
                    'opacity': 6
                };

                const inputSlot = surfaceInputMap[output.name] ?? 0;
                sourceEntry.node.connect(0, surfaceNode, inputSlot);
            }
        }
    }

    // Also handle connections from materialData
    for (const conn of connections) {
        // These are shader-level connections
        // Could add visual indicators for these
    }

    updateNodeStats();
}

function computeNodeLevels(nodes) {
    const levels = new Map();
    const nodeInputs = new Map();

    // Build dependency map
    for (const node of nodes) {
        const deps = [];
        if (node.inputs) {
            for (const input of node.inputs) {
                if (input.nodename) {
                    deps.push(input.nodename);
                }
            }
        }
        nodeInputs.set(node.name, deps);
    }

    // Compute levels (nodes with no deps are level 0)
    let changed = true;
    let iteration = 0;
    const maxIterations = 100;

    // Initialize all to level 0
    for (const node of nodes) {
        levels.set(node.name, 0);
    }

    while (changed && iteration < maxIterations) {
        changed = false;
        iteration++;

        for (const node of nodes) {
            const deps = nodeInputs.get(node.name) || [];
            if (deps.length === 0) continue;

            const maxDepLevel = Math.max(...deps.map(d => levels.get(d) ?? 0));
            const newLevel = maxDepLevel + 1;

            if (newLevel > levels.get(node.name)) {
                levels.set(node.name, newLevel);
                changed = true;
            }
        }
    }

    return levels;
}

function getBaseCategory(category) {
    if (!category) return 'generic';
    // Strip type suffixes
    return category.replace(/_(color3|color4|float|vector2|vector3|vector4|integer|boolean|string)$/, '');
}

function updateNodeStats() {
    const nodeCount = state.graph._nodes ? state.graph._nodes.length : 0;
    let connectionCount = 0;

    if (state.graph._nodes) {
        for (const node of state.graph._nodes) {
            if (node.outputs) {
                for (const output of node.outputs) {
                    if (output.links) {
                        connectionCount += output.links.length;
                    }
                }
            }
        }
    }

    document.getElementById('node-count').textContent = nodeCount;
    document.getElementById('connection-count').textContent = connectionCount;
}

// ============================================================================
// Three.js Setup
// ============================================================================

function initThreeJS() {
    const canvas = document.getElementById('canvas-3d');

    // Create renderer
    state.renderer = new THREE.WebGLRenderer({
        canvas,
        antialias: true,
        alpha: false
    });

    state.renderer.setSize(canvas.clientWidth, canvas.clientHeight);
    state.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    state.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    state.renderer.toneMappingExposure = 1.0;
    state.renderer.outputColorSpace = THREE.SRGBColorSpace;

    // Create scene
    state.scene = new THREE.Scene();
    state.scene.background = new THREE.Color(0x1a1a2e);

    // Create camera
    state.camera = new THREE.PerspectiveCamera(
        45,
        canvas.clientWidth / canvas.clientHeight,
        0.01,
        1000
    );
    state.camera.position.set(3, 2, 3);

    // Create controls
    state.controls = new OrbitControls(state.camera, canvas);
    state.controls.enableDamping = true;
    state.controls.dampingFactor = 0.05;

    // Add lights
    setupLights();

    // Add grid
    const gridHelper = new THREE.GridHelper(10, 10, 0x333355, 0x222244);
    state.scene.add(gridHelper);

    // Handle resize
    window.addEventListener('resize', onWindowResize);
}

function setupLights() {
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
    state.scene.add(ambientLight);

    const mainLight = new THREE.DirectionalLight(0xffffff, 1.5);
    mainLight.position.set(5, 10, 7);
    state.scene.add(mainLight);

    const fillLight = new THREE.DirectionalLight(0x8888ff, 0.4);
    fillLight.position.set(-5, 3, -5);
    state.scene.add(fillLight);

    const rimLight = new THREE.DirectionalLight(0xffaa88, 0.3);
    rimLight.position.set(0, -3, -5);
    state.scene.add(rimLight);
}

function onWindowResize() {
    const panel = document.getElementById('viewer-panel');
    const canvas = document.getElementById('canvas-3d');

    canvas.width = panel.clientWidth;
    canvas.height = panel.clientHeight;

    state.camera.aspect = canvas.width / canvas.height;
    state.camera.updateProjectionMatrix();
    state.renderer.setSize(canvas.width, canvas.height);

    resizeGraphCanvas();
}

// ============================================================================
// USD Loading
// ============================================================================

async function initLoader() {
    updateStatus('Initializing USD loader...');

    state.loader = new TinyUSDZLoader();
    await state.loader.init({ useMemory64: false });
    state.loader.setMaxMemoryLimitMB(500);

    updateStatus('Ready');
}

async function loadUSDFile(file) {
    showLoading(true);
    updateStatus(`Loading ${file.name}...`);

    try {
        const arrayBuffer = await file.arrayBuffer();

        state.usdData = await new Promise((resolve, reject) => {
            state.loader.parse(arrayBuffer, file.name, resolve, reject);
        });

        if (!state.usdData) {
            throw new Error('Failed to parse USD file');
        }

        // Clear existing
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        state.materials = [];
        state.materialData = [];

        // Load materials
        const numMaterials = state.usdData.numMaterials();
        for (let i = 0; i < numMaterials; i++) {
            const result = state.usdData.getMaterialWithFormat(i, 'json');
            if (!result.error) {
                const matData = JSON.parse(result.data);
                state.materialData.push(matData);
            }
        }

        // Build scene
        await buildScene();

        // Update material selector
        updateMaterialSelector();

        // Load first material's node graph
        if (state.materialData.length > 0) {
            selectMaterial(0);
        }

        showToast(`Loaded ${file.name}`);

    } catch (error) {
        console.error('Load error:', error);
        updateStatus(`Error: ${error.message}`);
        showToast(`Failed: ${error.message}`);
    } finally {
        showLoading(false);
    }
}

async function buildScene() {
    const usd = state.usdData;
    const numMeshes = usd.numMeshes();

    // Create materials
    for (const matData of state.materialData) {
        const openPBR = matData.openPBR || {};
        let nodeGraph = openPBR.nodeGraph;

        // Optionally optimize
        if (nodeGraph) {
            nodeGraph = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
        }

        const params = {
            base_color: openPBR.base_color || DEFAULT_OPENPBR_PARAMS.base_color,
            base_metalness: openPBR.base_metalness ?? DEFAULT_OPENPBR_PARAMS.base_metalness,
            specular_roughness: openPBR.specular_roughness ?? DEFAULT_OPENPBR_PARAMS.specular_roughness,
            specular_ior: openPBR.specular_ior ?? DEFAULT_OPENPBR_PARAMS.specular_ior,
            coat_weight: openPBR.coat_weight ?? DEFAULT_OPENPBR_PARAMS.coat_weight,
            emission_color: openPBR.emission_color ?? DEFAULT_OPENPBR_PARAMS.emission_color
        };

        // Process node graph for constant values
        if (nodeGraph) {
            const processor = new MtlxNodeGraphProcessor();
            const outputs = processor.processGraph(nodeGraph);

            for (const [name, value] of Object.entries(outputs)) {
                if (value !== undefined && !processor.needsShader(value)) {
                    const paramMap = {
                        'base_color': 'base_color',
                        'baseColor': 'base_color',
                        'roughness': 'specular_roughness',
                        'metalness': 'base_metalness'
                    };
                    const paramName = paramMap[name] || name;
                    if (params.hasOwnProperty(paramName)) {
                        params[paramName] = value;
                    }
                }
            }
        }

        const material = createOpenPBRMaterial(params);
        material.name = matData.name || `Material_${state.materials.length}`;
        state.materials.push(material);
    }

    // Build meshes
    const root = new THREE.Group();

    for (let i = 0; i < numMeshes; i++) {
        const meshData = usd.getMesh(i);
        if (!meshData || !meshData.vertices) continue;

        const geometry = createGeometry(meshData);
        const materialId = meshData.materialId ?? 0;
        const material = state.materials[materialId] || new THREE.MeshStandardMaterial({ color: 0x888888 });

        const mesh = new THREE.Mesh(geometry, material);
        mesh.name = meshData.name || `Mesh_${i}`;
        root.add(mesh);
    }

    state.currentModel = root;
    state.scene.add(root);

    fitCameraToObject(root);
    updateStatus(`Loaded: ${numMeshes} meshes, ${state.materials.length} materials`);

    document.getElementById('mesh-count').textContent = numMeshes;
    document.getElementById('material-count').textContent = state.materials.length;
}

function createGeometry(meshData) {
    const geometry = new THREE.BufferGeometry();

    const positions = new Float32Array(meshData.vertices);
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));

    if (meshData.indices) {
        const indices = new Uint32Array(meshData.indices);
        geometry.setIndex(new THREE.BufferAttribute(indices, 1));
    }

    if (meshData.normals && meshData.normals.length > 0) {
        const normals = new Float32Array(meshData.normals);
        geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3));
    } else {
        geometry.computeVertexNormals();
    }

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
    let cameraZ = Math.abs(maxDim / 2 / Math.tan(fov / 2)) * 1.5;

    state.camera.position.set(center.x + cameraZ * 0.5, center.y + cameraZ * 0.3, center.z + cameraZ);
    state.camera.lookAt(center);

    state.controls.target.copy(center);
    state.controls.update();
}

function disposeObject(obj) {
    obj.traverse((child) => {
        if (child.geometry) child.geometry.dispose();
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
// Material Selection
// ============================================================================

function updateMaterialSelector() {
    const selector = document.getElementById('material-selector');
    const select = document.getElementById('material-select');

    select.innerHTML = '';

    for (let i = 0; i < state.materialData.length; i++) {
        const mat = state.materialData[i];
        const option = document.createElement('option');
        option.value = i;
        option.textContent = mat.name || `Material ${i}`;
        select.appendChild(option);
    }

    select.onchange = (e) => selectMaterial(parseInt(e.target.value));

    selector.style.display = state.materialData.length > 0 ? 'block' : 'none';
}

function selectMaterial(index) {
    if (index < 0 || index >= state.materialData.length) return;

    state.currentMaterialIndex = index;
    const matData = state.materialData[index];

    // Get node graph
    let nodeGraph = matData.openPBR?.nodeGraph;
    if (nodeGraph) {
        nodeGraph = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
    }

    // Build LiteGraph visualization
    buildLiteGraphFromMtlx(nodeGraph, matData.name);

    // Auto-fit
    setTimeout(() => fitGraph(), 100);
}

// ============================================================================
// Sample Scene
// ============================================================================

function createSampleScene() {
    // Clear existing
    if (state.currentModel) {
        state.scene.remove(state.currentModel);
        disposeObject(state.currentModel);
    }

    state.materials = [];
    state.materialData = [];

    // Create sample material data with node graph
    const sampleMaterialData = {
        name: 'Sample_Material',
        openPBR: {
            base_color: [0.8, 0.2, 0.1],
            base_metalness: 0.0,
            specular_roughness: 0.3,
            nodeGraph: {
                version: '1.39',
                nodegraph: {
                    name: 'NG_sample',
                    nodes: [
                        {
                            name: 'input_color',
                            category: 'constant_color3',
                            type: 'ND_constant_color3',
                            value: [0.8, 0.2, 0.1]
                        },
                        {
                            name: 'invert1',
                            category: 'invert_color3',
                            type: 'ND_invert_color3',
                            inputs: [
                                { name: 'in', nodename: 'input_color' },
                                { name: 'amount', value: 1.0 }
                            ]
                        },
                        {
                            name: 'invert2',
                            category: 'invert_color3',
                            type: 'ND_invert_color3',
                            inputs: [
                                { name: 'in', nodename: 'invert1' },
                                { name: 'amount', value: 1.0 }
                            ]
                        },
                        {
                            name: 'roughness_const',
                            category: 'constant_float',
                            type: 'ND_constant_float',
                            value: 0.3
                        }
                    ],
                    outputs: [
                        { name: 'base_color', nodename: 'invert2' },
                        { name: 'specular_roughness', nodename: 'roughness_const' }
                    ]
                }
            }
        }
    };

    state.materialData = [sampleMaterialData];

    // Create material
    const material = createOpenPBRMaterial({
        base_color: [0.8, 0.2, 0.1], // Double invert = original
        specular_roughness: 0.3
    });
    material.name = 'Sample_Material';
    state.materials = [material];

    // Create geometry
    const geometry = new THREE.SphereGeometry(1, 64, 64);
    const mesh = new THREE.Mesh(geometry, material);

    state.currentModel = new THREE.Group();
    state.currentModel.add(mesh);
    state.scene.add(state.currentModel);

    fitCameraToObject(state.currentModel);

    // Update UI
    updateMaterialSelector();
    selectMaterial(0);

    updateStatus('Sample loaded');
    document.getElementById('mesh-count').textContent = '1';
    document.getElementById('material-count').textContent = '1';

    showToast('Sample scene with double-invert node graph');
}

// ============================================================================
// UI Helpers
// ============================================================================

function updateStatus(text) {
    document.getElementById('status').textContent = text;
}

function showLoading(visible) {
    document.getElementById('loading').classList.toggle('visible', visible);
}

function showToast(message, duration = 3000) {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.classList.add('visible');
    setTimeout(() => toast.classList.remove('visible'), duration);
}

// ============================================================================
// Resizer
// ============================================================================

function initResizer() {
    const resizer = document.getElementById('resizer');
    const viewerPanel = document.getElementById('viewer-panel');
    const nodegraphPanel = document.getElementById('nodegraph-panel');
    let isResizing = false;

    resizer.addEventListener('mousedown', (e) => {
        isResizing = true;
        document.body.style.cursor = 'col-resize';
        document.body.style.userSelect = 'none';
    });

    document.addEventListener('mousemove', (e) => {
        if (!isResizing) return;

        const containerWidth = window.innerWidth;
        const newViewerWidth = e.clientX;

        if (newViewerWidth > 300 && newViewerWidth < containerWidth - 400) {
            viewerPanel.style.flex = 'none';
            viewerPanel.style.width = `${newViewerWidth}px`;
            nodegraphPanel.style.flex = '1';

            onWindowResize();
        }
    });

    document.addEventListener('mouseup', () => {
        isResizing = false;
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
    });
}

// ============================================================================
// Global Functions
// ============================================================================

window.loadFile = function() {
    document.getElementById('file-input').click();
};

window.loadSample = function() {
    createSampleScene();
};

window.loadBlenderSample = async function() {
    showLoading(true);
    updateStatus('Loading Blender sample...');

    try {
        const response = await fetch('assets/texture_channel_blender.usdz');
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.status}`);
        }

        const arrayBuffer = await response.arrayBuffer();

        state.usdData = await new Promise((resolve, reject) => {
            state.loader.parse(arrayBuffer, 'texture_channel_blender.usdz', resolve, reject);
        });

        if (!state.usdData) {
            throw new Error('Failed to parse USD file');
        }

        // Clear existing
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        state.materials = [];
        state.materialData = [];

        // Load materials
        const numMaterials = state.usdData.numMaterials();
        console.log(`Found ${numMaterials} materials`);

        for (let i = 0; i < numMaterials; i++) {
            const result = state.usdData.getMaterialWithFormat(i, 'json');
            if (!result.error) {
                const matData = JSON.parse(result.data);
                console.log(`Material ${i}:`, matData.name);
                state.materialData.push(matData);
            }
        }

        // Build scene
        await buildScene();

        // Update material selector
        updateMaterialSelector();

        // Load first material's node graph
        if (state.materialData.length > 0) {
            selectMaterial(0);
        }

        showToast(`Loaded Blender sample with ${numMaterials} materials`);

    } catch (error) {
        console.error('Load error:', error);
        updateStatus(`Error: ${error.message}`);
        showToast(`Failed: ${error.message}`);
    } finally {
        showLoading(false);
    }
};

window.fitGraph = function() {
    if (state.graphCanvas) {
        state.graphCanvas.ds.reset();
        state.graph.arrange();
    }
};

// ============================================================================
// Blender Bridge Functions
// ============================================================================

window.toggleBridge = function() {
    const panel = document.getElementById('bridge-panel');
    panel.classList.toggle('visible');
};

window.bridgeConnect = async function() {
    if (!BridgeClient) {
        showToast('Bridge client not available');
        return;
    }

    const sessionInput = document.getElementById('bridge-session');
    const sessionId = sessionInput.value.trim();

    if (!sessionId) {
        showToast('Enter session ID from Blender');
        return;
    }

    const statusEl = document.getElementById('bridge-status');
    const btnBridge = document.getElementById('btn-bridge');

    try {
        statusEl.textContent = 'Connecting...';

        // Create bridge client
        state.bridgeClient = new BridgeClient({
            serverUrl: 'ws://localhost:8090',
            sessionId: sessionId
        });

        // Set up event handlers
        state.bridgeClient.addEventListener('connected', () => {
            state.bridgeConnected = true;
            statusEl.textContent = `Connected: ${sessionId}`;
            statusEl.classList.add('connected');
            btnBridge.textContent = 'Connected';
            btnBridge.style.background = '#4CAF50';
            showToast('Connected to Blender Bridge');
        });

        state.bridgeClient.addEventListener('disconnected', () => {
            state.bridgeConnected = false;
            statusEl.textContent = 'Disconnected';
            statusEl.classList.remove('connected');
            btnBridge.textContent = 'Connect';
            btnBridge.style.background = '#E91E63';
        });

        state.bridgeClient.addEventListener('scene-upload', async (event) => {
            console.log('Scene received from Blender');
            await handleBridgeScene(event.detail);
        });

        state.bridgeClient.addEventListener('server-error', (event) => {
            showToast(`Bridge error: ${event.detail.message}`);
        });

        // Connect
        await state.bridgeClient.connect(sessionId);

    } catch (error) {
        console.error('Bridge connect error:', error);
        statusEl.textContent = 'Connection failed';
        showToast(`Failed: ${error.message}`);
    }
};

window.bridgePullScene = async function() {
    if (!state.bridgeClient || !state.bridgeConnected) {
        showToast('Not connected to Blender');
        return;
    }

    showLoading(true);
    updateStatus('Requesting scene from Blender...');

    try {
        // Request export with MaterialX
        const sceneData = await state.bridgeClient.requestExport({
            materialx: true,
            animation: false
        });

        console.log('Scene pulled successfully');
        await handleBridgeScene(sceneData);

    } catch (error) {
        console.error('Pull scene error:', error);
        showToast(`Failed: ${error.message}`);
        updateStatus('Error');
    } finally {
        showLoading(false);
    }
};

async function handleBridgeScene(sceneData) {
    showLoading(true);
    updateStatus('Loading scene from Blender...');

    try {
        const { binaryData, scene, metadata } = sceneData;

        // Parse with TinyUSDZ
        state.usdData = await new Promise((resolve, reject) => {
            state.loader.parse(binaryData.buffer, scene?.name || 'BlenderScene.usdz', resolve, reject);
        });

        if (!state.usdData) {
            throw new Error('Failed to parse USD');
        }

        // Clear existing
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        state.materials = [];
        state.materialData = [];

        // Load materials
        const numMaterials = state.usdData.numMaterials();
        console.log(`Found ${numMaterials} materials from Blender`);

        for (let i = 0; i < numMaterials; i++) {
            const result = state.usdData.getMaterialWithFormat(i, 'json');
            if (!result.error) {
                const matData = JSON.parse(result.data);
                console.log(`Material ${i}:`, matData.name);
                state.materialData.push(matData);
            }
        }

        // Build scene
        await buildScene();

        // Update material selector
        updateMaterialSelector();

        // Load first material's node graph
        if (state.materialData.length > 0) {
            selectMaterial(0);
        }

        const sceneName = scene?.name || 'BlenderScene';
        const blenderVer = metadata?.blenderVersion || 'unknown';
        showToast(`Loaded ${sceneName} from Blender ${blenderVer}`);
        updateStatus(`Loaded: ${sceneName}`);

    } catch (error) {
        console.error('Handle bridge scene error:', error);
        showToast(`Failed: ${error.message}`);
        updateStatus('Error');
    } finally {
        showLoading(false);
    }
}

// ============================================================================
// Animation Loop
// ============================================================================

function animate() {
    requestAnimationFrame(animate);

    state.controls.update();
    state.renderer.render(state.scene, state.camera);

    // LiteGraph update
    if (state.graphCanvas) {
        state.graphCanvas.draw(true);
    }
}

// ============================================================================
// Initialization
// ============================================================================

async function init() {
    updateStatus('Initializing...');

    // Init Three.js
    initThreeJS();

    // Init LiteGraph
    initLiteGraph();

    // Init resizer
    initResizer();

    // Init USD loader
    await initLoader();

    // File input handler
    document.getElementById('file-input').addEventListener('change', (e) => {
        const file = e.target.files[0];
        if (file) loadUSDFile(file);
    });

    // Start render loop
    animate();

    // Initial resize
    onWindowResize();
}

init().catch(console.error);
