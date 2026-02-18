/**
 * OpenPBR NodeGraph Demo
 *
 * Standalone demo showing OpenPBR materials with MaterialX node graph visualization.
 * Uses Three.js for 3D rendering and LiteGraph.js for node graph display.
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
// import { RGBELoader } from 'three/examples/jsm/loaders/RGBELoader.js';  // Available for HDR env presets
// import { EXRLoader } from 'three/examples/jsm/loaders/EXRLoader.js';    // Available for EXR env presets
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import {
    MtlxNodeGraphProcessor,
    createOpenPBRMaterial,
    DEFAULT_OPENPBR_PARAMS
} from 'tinyusdz/TinyUSDZOpenPBR_WebGL.js';
import {
    optimizeNodeGraph,
    NodeGraphOptimizationLevel,
    removeInactiveNodes
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
// Environment & Tone Mapping Configuration
// ============================================================================

const ENV_PRESETS = {
    'studio': null,                     // Procedural gradient
    'usd_dome': 'usd',                 // From loaded USD DomeLight
    'constant_white': 'constant:#ffffff',
    'constant_warm': 'constant:#fff5e0',
    'constant_cool': 'constant:#e0f0ff',
};

const TONE_MAPPINGS = {
    'aces': THREE.ACESFilmicToneMapping,
    'agx': THREE.AgXToneMapping,
    'neutral': THREE.NeutralToneMapping,
    'reinhard': THREE.ReinhardToneMapping,
    'linear': THREE.LinearToneMapping,
    'none': THREE.NoToneMapping,
};

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
    bridgeConnected: false,

    // Node graph editing
    interactiveUpdate: false,
    graphDirty: false,
    updateDebounceTimer: null,

    // Node graph display
    showAllNodes: false,    // false = active only (DCE), true = show all including dead nodes

    // Picking
    selectedObject: null,
    selectionHelper: null,

    // Environment
    pmremGenerator: null,
    envMap: null,          // PMREM-processed (for IBL reflections via scene.environment)
    envMapSource: null,    // Original equirectangular texture (for scene.background)
    envPreset: 'studio',
    envIntensity: 1.0,
    showBackground: false,
    toneMapping: 'aces',
    exposure: 1.0,

    // USD lights
    domeLightData: null,
    usdLights: [],          // Three.js lights created from USD
    defaultLights: [],      // Default directional lights
    envEnabled: true,       // Whether envmap IBL is active
};

// ============================================================================
// LiteGraph Custom Node Types
// ============================================================================

function registerMtlxNodeTypes() {
    // Base class for MaterialX nodes
    function MtlxNode() {
        this.color = '#335544';
    }

    // Constant node (with editable value widget)
    function ConstantNode() {
        this.addOutput('out', 'color3');
        this.properties = { value: [0.8, 0.8, 0.8], valueType: 'color3' };
        this.color = '#445566';
        this.size = [180, 90];
        this._setupWidgets();
    }
    ConstantNode.title = 'Constant';
    ConstantNode.prototype._setupWidgets = function() {
        // Remove existing widgets
        this.widgets = [];
        const v = this.properties.value;
        if (Array.isArray(v) && v.length >= 3) {
            this.properties.valueType = 'color3';
            this.size = [200, 120];
            this.addWidget('slider', 'R', v[0], (val) => {
                this.properties.value[0] = val;
                this.setDirtyCanvas(true);
                if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            }, { min: 0, max: 1 });
            this.addWidget('slider', 'G', v[1], (val) => {
                this.properties.value[1] = val;
                this.setDirtyCanvas(true);
                if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            }, { min: 0, max: 1 });
            this.addWidget('slider', 'B', v[2], (val) => {
                this.properties.value[2] = val;
                this.setDirtyCanvas(true);
                if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            }, { min: 0, max: 1 });
        } else {
            this.properties.valueType = 'float';
            const numVal = (typeof v === 'number') ? v : 0;
            this.size = [200, 65];
            this.addWidget('slider', 'Value', numVal, (val) => {
                this.properties.value = val;
                this.setDirtyCanvas(true);
                if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            }, { min: 0, max: 1 });
        }
    };
    ConstantNode.prototype.onDrawForeground = function(ctx) {
        const v = this.properties.value;
        if (Array.isArray(v) && v.length >= 3) {
            ctx.fillStyle = `rgb(${Math.floor(v[0]*255)},${Math.floor(v[1]*255)},${Math.floor(v[2]*255)})`;
            ctx.fillRect(10, this.size[1] - 25, this.size[0] - 20, 18);
        }
    };
    ConstantNode.prototype.onPropertyChanged = function() {
        this._setupWidgets();
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

    // Mix node (with editable mix factor)
    function MixNode() {
        this.addInput('bg', 'color3');
        this.addInput('fg', 'color3');
        this.addInput('mix', 'float');
        this.addOutput('out', 'color3');
        this.properties = { mix: 0.5 };
        this.color = '#665555';
        this.size = [140, 90];

        this.addWidget('number', 'Mix', 0.5, (v) => {
            this.properties.mix = Math.max(0, Math.min(1, v));
            if (window._mtlxMarkDirty) window._mtlxMarkDirty();
        }, { min: 0, max: 1, step: 0.05 });
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

    // Invert node (with editable amount)
    function InvertNode() {
        this.addInput('in', 'color3');
        this.addInput('amount', 'float');
        this.addOutput('out', 'color3');
        this.properties = { amount: 1.0 };
        this.color = '#554455';
        this.size = [140, 75];

        this.addWidget('number', 'Amount', 1.0, (v) => {
            this.properties.amount = Math.max(0, Math.min(1, v));
            if (window._mtlxMarkDirty) window._mtlxMarkDirty();
        }, { min: 0, max: 1, step: 0.05 });
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

    // Convert node (type conversion pass-through, e.g. color4→color3, float→color3)
    function ConvertNode() {
        this.addInput('in', 'any');
        this.addOutput('out', 'any');
        this.color = '#555566';
        this.size = [100, 50];
    }
    ConvertNode.title = 'Convert';
    LiteGraph.registerNodeType('mtlx/convert', ConvertNode);

    // Tangent node
    function TangentNode() {
        this.addOutput('out', 'vector3');
        this.properties = { space: 'object' };
        this.color = '#445566';
        this.size = [110, 50];
    }
    TangentNode.title = 'Tangent';
    TangentNode.prototype.onDrawForeground = function(ctx) {
        ctx.fillStyle = '#888';
        ctx.font = '10px sans-serif';
        ctx.fillText(this.properties.space, 10, 35);
    };
    LiteGraph.registerNodeType('mtlx/tangent', TangentNode);

    // Crossproduct node
    function CrossProductNode() {
        this.addInput('in1', 'vector3');
        this.addInput('in2', 'vector3');
        this.addOutput('out', 'vector3');
        this.color = '#555566';
        this.size = [120, 60];
    }
    CrossProductNode.title = 'Cross';
    LiteGraph.registerNodeType('mtlx/crossproduct', CrossProductNode);

    // Dot product node
    function DotProductNode() {
        this.addInput('in1', 'vector3');
        this.addInput('in2', 'vector3');
        this.addOutput('out', 'float');
        this.color = '#555566';
        this.size = [120, 60];
    }
    DotProductNode.title = 'Dot';
    LiteGraph.registerNodeType('mtlx/dotproduct', DotProductNode);

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

    // Hook change events for editing
    hookGraphChangeEvents();
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
    // Suppress dirty notifications during graph building
    state._buildingGraph = true;
    state.graph.clear();

    if (!nodeGraphData || !nodeGraphData.nodegraph) {
        // Create a simple constant output for materials without node graphs
        const constNode = LiteGraph.createNode('mtlx/constant');
        constNode.pos = [100, 200];
        constNode.properties.value = [0.8, 0.8, 0.8];
        constNode._setupWidgets();
        state.graph.add(constNode);

        const surfaceNode = LiteGraph.createNode('mtlx/openpbr_surface');
        surfaceNode.pos = [400, 150];
        surfaceNode.title = materialName || 'OpenPBR Surface';
        state.graph.add(surfaceNode);

        constNode.connect(0, surfaceNode, 0);

        state._buildingGraph = false;
        state.graphDirty = false;
        document.getElementById('edit-indicator').classList.remove('visible');
        updateNodeStats();
        return;
    }

    const ng = nodeGraphData.nodegraph;
    const allNodes = ng.nodes || [];
    const outputs = ng.outputs || [];
    const connections = nodeGraphData.connections || [];

    // Separate active and inactive nodes
    const activeNodes = allNodes.filter(n => n._active !== false);
    const inactiveNodes = allNodes.filter(n => n._active === false);

    // Decide which nodes to display based on showAllNodes toggle
    const displayNodes = state.showAllNodes ? allNodes : activeNodes;

    // Map to store created LiteGraph nodes by name
    const nodeMap = new Map();

    // Layout parameters
    const startX = 50;
    const startY = 50;
    const xSpacing = 200;
    const yGap = 20; // gap between nodes in same column

    // Build dependency levels for layout
    const activeLevels = computeNodeLevels(activeNodes);
    const levels = state.showAllNodes ? computeNodeLevels(displayNodes) : activeLevels;

    // Pass 1: Create all LiteGraph nodes (no positioning yet)
    for (const node of displayNodes) {
        const isActive = node._active !== false;
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

        // Show shader node type as subtitle (e.g. "ND_combine3_color3")
        const nodeTypeStr = node.type || node.category || '';
        if (nodeTypeStr && nodeTypeStr !== lgNode.title) {
            lgNode.properties = lgNode.properties || {};
            lgNode.properties._nodeType = nodeTypeStr;
        }

        // Mark inactive nodes visually
        lgNode.properties = lgNode.properties || {};
        lgNode.properties._isActive = isActive;

        // Custom draw: show node type + dim inactive nodes
        const origDraw = lgNode.onDrawForeground;
        lgNode.onDrawForeground = function(ctx) {
            if (origDraw) origDraw.call(this, ctx);
            if (this.properties._nodeType && this.properties._nodeType !== this.title) {
                ctx.fillStyle = this.properties._isActive ? '#8899aa' : '#556666';
                ctx.font = '9px monospace';
                ctx.fillText(this.properties._nodeType, 6, this.size[1] - 4);
            }
            if (!this.properties._isActive) {
                ctx.fillStyle = 'rgba(0, 0, 0, 0.35)';
                ctx.fillRect(0, 0, this.size[0], this.size[1]);
                ctx.fillStyle = '#aa6666';
                ctx.font = 'bold 9px sans-serif';
                ctx.fillText('unused', 6, 12);
            }
        };
        lgNode.size[1] = Math.max(lgNode.size[1], 55) + 14;

        // Set properties (may affect node size via widgets)
        if (node.value !== undefined) {
            lgNode.properties = lgNode.properties || {};
            lgNode.properties.value = node.value;
            if (typeof lgNode._setupWidgets === 'function') {
                lgNode._setupWidgets();
            }
        }

        if (node.inputs) {
            for (const input of node.inputs) {
                if (input.value !== undefined && !input.nodename) {
                    lgNode.properties = lgNode.properties || {};
                    lgNode.properties[input.name] = input.value;
                }
            }
        }

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

        if (category === 'tangent' || category === 'normal' || category === 'position') {
            lgNode.properties = lgNode.properties || {};
            const spaceInput = node.inputs?.find(i => i.name === 'space');
            lgNode.properties.space = spaceInput?.value || 'object';
        }

        state.graph.add(lgNode);
        nodeMap.set(node.name, { node: lgNode, data: node });
    }

    // Pass 2: Position nodes by level, using actual node heights to avoid overlap
    // Group active and inactive nodes separately per level
    const activeByLevel = new Map();  // level → [node entries]
    const inactiveByLevel = new Map();
    const inactiveLevelsMap = (state.showAllNodes && inactiveNodes.length > 0)
        ? computeNodeLevels(inactiveNodes) : new Map();

    for (const node of displayNodes) {
        const isActive = node._active !== false;
        const entry = nodeMap.get(node.name);
        if (!entry) continue;

        let level;
        if (state.showAllNodes && !isActive) {
            level = inactiveLevelsMap.get(node.name) || 0;
            if (!inactiveByLevel.has(level)) inactiveByLevel.set(level, []);
            inactiveByLevel.get(level).push(entry);
        } else {
            level = levels.get(node.name) || 0;
            if (!activeByLevel.has(level)) activeByLevel.set(level, []);
            activeByLevel.get(level).push(entry);
        }
    }

    // Position active nodes (top section)
    let maxActiveBottom = startY;
    for (const [level, entries] of activeByLevel) {
        let y = startY;
        for (const entry of entries) {
            entry.node.pos = [startX + level * xSpacing, y];
            y += entry.node.size[1] + yGap;
        }
        maxActiveBottom = Math.max(maxActiveBottom, y);
    }

    // Position inactive nodes (below active, with separator gap)
    if (state.showAllNodes && inactiveByLevel.size > 0) {
        const inactiveTopY = maxActiveBottom + 40; // separator gap
        for (const [level, entries] of inactiveByLevel) {
            let y = inactiveTopY;
            for (const entry of entries) {
                entry.node.pos = [startX + level * xSpacing, y];
                y += entry.node.size[1] + yGap;
            }
        }
    }

    // Create output surface node
    const surfaceNode = LiteGraph.createNode('mtlx/openpbr_surface');
    const maxLevel = levels.size > 0 ? Math.max(...levels.values()) : 0;
    surfaceNode.pos = [startX + (maxLevel + 1) * xSpacing, startY];
    surfaceNode.title = materialName || 'OpenPBR Surface';
    state.graph.add(surfaceNode);

    // Create connections between nodes
    for (const node of displayNodes) {
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

    // Map shader parameter names to surface node input slots
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
        'opacity': 6,
    };
    const connectedSlots = new Set();

    // Build outputName→shaderParams map from connections array
    // Multiple shader params can connect to the same NodeGraph output
    const outputToShaderParams = new Map();
    for (const conn of connections) {
        if (conn.input && conn.output) {
            if (!outputToShaderParams.has(conn.output)) {
                outputToShaderParams.set(conn.output, []);
            }
            outputToShaderParams.get(conn.output).push(conn.input);
        }
    }

    // Connect outputs to surface node
    for (const output of outputs) {
        if (!output.nodename) continue;
        const sourceEntry = nodeMap.get(output.nodename);
        if (!sourceEntry) continue;

        // Get all shader parameters this output connects to
        const shaderParams = outputToShaderParams.get(output.name) || [output.name];
        for (const shaderParam of shaderParams) {
            let inputSlot = surfaceInputMap[shaderParam];
            if (inputSlot !== undefined && !connectedSlots.has(inputSlot)) {
                sourceEntry.node.connect(0, surfaceNode, inputSlot);
                connectedSlots.add(inputSlot);
            } else if (inputSlot === undefined) {
                // Unknown param — add a dynamic input on the surface node
                const newSlot = surfaceNode.addInput(shaderParam, 'any');
                sourceEntry.node.connect(0, surfaceNode, surfaceNode.inputs.length - 1);
            }
        }
    }

    // Clear building flag and reset dirty state
    state._buildingGraph = false;
    state.graphDirty = false;
    document.getElementById('edit-indicator').classList.remove('visible');

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

function toggleShowAllNodes(checked) {
    state.showAllNodes = checked;
    // Rebuild the graph view with the current material
    const matData = state.materialData[state.currentMaterialIndex];
    if (matData?.openPBR?.nodeGraph) {
        let nodeGraph = matData.openPBR.nodeGraph;
        nodeGraph = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
        const materialName = state.materials[state.currentMaterialIndex]?.name || 'Material';
        buildLiteGraphFromMtlx(nodeGraph, materialName);
    }
}

// Expose to HTML onclick
window.toggleShowAllNodes = toggleShowAllNodes;

// ============================================================================
// Node Graph Editing → Material Update
// ============================================================================

/**
 * Extract MaterialX-compatible node graph data from the current LiteGraph state.
 * Reads back node properties, connections, and outputs to build a nodegraph
 * structure that can be fed into MtlxNodeGraphProcessor.
 */
function extractGraphFromLiteGraph() {
    if (!state.graph || !state.graph._nodes) return null;

    const lgNodes = state.graph._nodes;
    const mtlxNodes = [];
    const mtlxOutputs = [];
    let surfaceNode = null;

    // Map LiteGraph node IDs to our names
    const idToName = new Map();
    for (const lgNode of lgNodes) {
        const nodeType = lgNode.type || '';
        if (nodeType === 'mtlx/openpbr_surface') {
            surfaceNode = lgNode;
        } else {
            idToName.set(lgNode.id, lgNode.title || `node_${lgNode.id}`);
        }
    }

    // Build nodes (skip surface node)
    for (const lgNode of lgNodes) {
        if (lgNode === surfaceNode) continue;

        const nodeType = lgNode.type || '';
        const category = nodeType.replace('mtlx/', '');
        const nodeName = lgNode.title || `node_${lgNode.id}`;

        const mtlxNode = {
            name: nodeName,
            category: category,
            type: `ND_${category}`
        };

        // Extract value from properties
        if (lgNode.properties) {
            if (lgNode.properties.value !== undefined) {
                mtlxNode.value = lgNode.properties.value;
            }
            if (lgNode.properties.file !== undefined) {
                mtlxNode.file = lgNode.properties.file;
            }
            if (lgNode.properties.index !== undefined) {
                mtlxNode.index = lgNode.properties.index;
            }
            if (lgNode.properties.space !== undefined) {
                mtlxNode.space = lgNode.properties.space;
            }
        }

        // Extract inputs with connections
        const inputs = [];
        if (lgNode.inputs) {
            for (let i = 0; i < lgNode.inputs.length; i++) {
                const inputSlot = lgNode.inputs[i];
                const inputDef = { name: inputSlot.name };

                // Check if connected
                if (inputSlot.link != null) {
                    const link = state.graph.links[inputSlot.link];
                    if (link) {
                        const sourceId = link.origin_id;
                        const sourceName = idToName.get(sourceId);
                        if (sourceName) {
                            inputDef.nodename = sourceName;
                        }
                    }
                }

                // Check for property-based value
                if (!inputDef.nodename && lgNode.properties && lgNode.properties[inputSlot.name] !== undefined) {
                    inputDef.value = lgNode.properties[inputSlot.name];
                }

                inputs.push(inputDef);
            }
        }

        if (inputs.length > 0) {
            mtlxNode.inputs = inputs;
        }

        mtlxNodes.push(mtlxNode);
    }

    // Build outputs from surface node connections
    if (surfaceNode && surfaceNode.inputs) {
        const surfaceInputNames = [
            'base_color', 'base_metalness', 'specular_roughness',
            'specular_ior', 'coat_weight', 'emission_color', 'geometry_opacity'
        ];

        for (let i = 0; i < surfaceNode.inputs.length; i++) {
            const inputSlot = surfaceNode.inputs[i];
            if (inputSlot.link != null) {
                const link = state.graph.links[inputSlot.link];
                if (link) {
                    const sourceId = link.origin_id;
                    const sourceName = idToName.get(sourceId);
                    if (sourceName) {
                        mtlxOutputs.push({
                            name: surfaceInputNames[i] || inputSlot.name,
                            nodename: sourceName
                        });
                    }
                }
            }
        }
    }

    return {
        nodegraph: {
            name: 'NG_edited',
            nodes: mtlxNodes,
            outputs: mtlxOutputs
        }
    };
}

/**
 * Re-evaluate the node graph from the current LiteGraph state
 * and apply the results to the 3D material.
 */
function evaluateAndApplyMaterial() {
    const graphData = extractGraphFromLiteGraph();
    if (!graphData) {
        showToast('No graph to evaluate');
        return;
    }

    const matIndex = state.currentMaterialIndex;
    if (matIndex < 0 || matIndex >= state.materials.length) {
        // No loaded material - create/update one on the sample sphere
        if (state.materials.length === 0) return;
    }

    const material = state.materials[matIndex];
    if (!material) return;

    // Process the node graph
    const processor = new MtlxNodeGraphProcessor();
    const outputs = processor.processGraph(graphData);

    // Build params from the current material data + overrides from graph
    const matData = state.materialData[matIndex];
    const openPBR = flattenOpenPBR(matData?.openPBR || {});
    const params = {
        base_color: extractOpenPBRValue(openPBR.base_color, DEFAULT_OPENPBR_PARAMS.base_color),
        base_metalness: extractOpenPBRValue(openPBR.base_metalness, DEFAULT_OPENPBR_PARAMS.base_metalness),
        specular_roughness: extractOpenPBRValue(openPBR.specular_roughness, DEFAULT_OPENPBR_PARAMS.specular_roughness),
        specular_ior: extractOpenPBRValue(openPBR.specular_ior, DEFAULT_OPENPBR_PARAMS.specular_ior),
        coat_weight: extractOpenPBRValue(openPBR.coat_weight, DEFAULT_OPENPBR_PARAMS.coat_weight),
        emission_color: extractOpenPBRValue(openPBR.emission_color, DEFAULT_OPENPBR_PARAMS.emission_color),
        geometry_opacity: extractOpenPBRValue(openPBR.geometry_opacity, DEFAULT_OPENPBR_PARAMS.geometry_opacity)
    };

    // Apply evaluated outputs
    for (const [name, value] of Object.entries(outputs)) {
        if (value === undefined) continue;
        if (processor.needsShader(value)) continue; // skip texture-dependent values

        const paramMap = {
            'base_color': 'base_color',
            'baseColor': 'base_color',
            'diffuseColor': 'base_color',
            'roughness': 'specular_roughness',
            'specular_roughness': 'specular_roughness',
            'metalness': 'base_metalness',
            'base_metalness': 'base_metalness',
            'specular_ior': 'specular_ior',
            'coat_weight': 'coat_weight',
            'emission_color': 'emission_color',
            'geometry_opacity': 'geometry_opacity',
            'opacity': 'geometry_opacity'
        };
        const paramName = paramMap[name] || name;
        if (params.hasOwnProperty(paramName)) {
            params[paramName] = value;
        }
    }

    // Apply to material (skip color/scalar overrides if a texture map is bound)
    if (!material.map) {
        const baseColor = params.base_color;
        if (Array.isArray(baseColor)) {
            material.color.setRGB(baseColor[0], baseColor[1], baseColor[2]);
        } else if (typeof baseColor === 'number') {
            material.color.setRGB(baseColor, baseColor, baseColor);
        }
    }

    if (!material.metalnessMap) material.metalness = params.base_metalness;
    if (!material.roughnessMap) material.roughness = params.specular_roughness;
    material.ior = params.specular_ior;
    material.clearcoat = params.coat_weight;

    if (!material.emissiveMap) {
        const emissionColor = params.emission_color;
        if (Array.isArray(emissionColor)) {
            material.emissive.setRGB(emissionColor[0], emissionColor[1], emissionColor[2]);
        }
    }

    if (params.geometry_opacity !== undefined) {
        material.opacity = params.geometry_opacity;
        material.transparent = params.geometry_opacity < 1.0;
    }

    material.needsUpdate = true;

    // Clear dirty flag
    state.graphDirty = false;
    document.getElementById('edit-indicator').classList.remove('visible');

    updateNodeStats();
}

/**
 * Mark the graph as dirty (modified since last update).
 */
function markGraphDirty() {
    if (state._buildingGraph) return; // Suppress during programmatic graph construction
    state.graphDirty = true;

    if (state.interactiveUpdate) {
        // Debounce: wait a short time before evaluating
        if (state.updateDebounceTimer) {
            clearTimeout(state.updateDebounceTimer);
        }
        state.updateDebounceTimer = setTimeout(() => {
            evaluateAndApplyMaterial();
        }, 150);
    } else {
        document.getElementById('edit-indicator').classList.add('visible');
    }
}

/**
 * Hook LiteGraph events for change detection.
 * Called after graph is initialized.
 */
function hookGraphChangeEvents() {
    if (!state.graph) return;

    // LGraph fires 'change' on structural changes (connections, node add/remove)
    state.graph.onNodeConnectionChange = function() {
        markGraphDirty();
    };

    state.graph.onNodeAdded = function(node) {
        // Attach widget change listener to new nodes
        if (node) {
            node.onWidgetChanged = function() { markGraphDirty(); };
        }
        markGraphDirty();
    };

    state.graph.onNodeRemoved = function() {
        markGraphDirty();
    };

    // Monitor widget interactions via canvas
    if (state.graphCanvas) {
        const origProcessNodeWidgets = state.graphCanvas.processNodeWidgets;
        if (origProcessNodeWidgets) {
            state.graphCanvas.processNodeWidgets = function(node, pos, event, active_widget) {
                const result = origProcessNodeWidgets.call(this, node, pos, event, active_widget);
                if (result) {
                    markGraphDirty();
                }
                return result;
            };
        }
    }

    // Also hook into the graph's afterChange callback
    state.graph.onAfterChange = function() {
        markGraphDirty();
    };
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

    // Object picking
    canvas.addEventListener('click', onCanvasClick);

    // Initialize PMREMGenerator for environment maps
    state.pmremGenerator = new THREE.PMREMGenerator(state.renderer);
    state.pmremGenerator.compileEquirectangularShader();

    // Add default lights (can be toggled when USD lights are loaded)
    setupDefaultLights();

    // Add grid
    const gridHelper = new THREE.GridHelper(10, 10, 0x333355, 0x222244);
    state.scene.add(gridHelper);

    // Handle resize
    window.addEventListener('resize', onWindowResize);
}

function setupDefaultLights() {
    state.defaultLights = [];

    const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
    ambientLight.name = 'Ambient';
    state.scene.add(ambientLight);
    state.defaultLights.push(ambientLight);

    const mainLight = new THREE.DirectionalLight(0xffffff, 1.5);
    mainLight.name = 'Key';
    mainLight.position.set(5, 10, 7);
    state.scene.add(mainLight);
    state.defaultLights.push(mainLight);

    const fillLight = new THREE.DirectionalLight(0x8888ff, 0.4);
    fillLight.name = 'Fill';
    fillLight.position.set(-5, 3, -5);
    state.scene.add(fillLight);
    state.defaultLights.push(fillLight);

    const rimLight = new THREE.DirectionalLight(0xffaa88, 0.3);
    rimLight.name = 'Rim';
    rimLight.position.set(0, -3, -5);
    state.scene.add(rimLight);
    state.defaultLights.push(rimLight);
}

// ============================================================================
// Environment Map Functions
// ============================================================================

/**
 * Create a procedural studio environment (gradient: white top → gray middle → dark bottom).
 * Returns { envMap, source } where envMap is PMREM-processed and source is equirectangular.
 */
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

    const source = new THREE.CanvasTexture(canvas);
    source.mapping = THREE.EquirectangularReflectionMapping;
    const envMap = state.pmremGenerator.fromEquirectangular(source).texture;
    return { envMap, source };
}

/**
 * Create a solid-color constant environment map.
 * Returns { envMap, source } where envMap is PMREM-processed and source is equirectangular.
 * @param {string} hexColor - CSS hex color (e.g. '#ffffff')
 */
function createConstantColorEnvironment(hexColor) {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 256;
    const ctx = canvas.getContext('2d');

    ctx.fillStyle = hexColor;
    ctx.fillRect(0, 0, 256, 256);

    const source = new THREE.CanvasTexture(canvas);
    source.mapping = THREE.EquirectangularReflectionMapping;
    source.colorSpace = THREE.LinearSRGBColorSpace;

    const envMap = state.pmremGenerator.fromEquirectangular(source).texture;
    return { envMap, source };
}

/**
 * Load an environment map from a preset key.
 * Handles 'studio', 'usd_dome', and 'constant:*' presets.
 */
/**
 * Set both envMap (PMREM) and envMapSource (equirectangular) from a result object.
 */
function setEnvFromResult(result) {
    state.envMap = result.envMap;
    state.envMapSource = result.source;
}

async function loadEnvironment(preset) {
    state.envPreset = preset;
    const path = ENV_PRESETS[preset];

    if (!path) {
        // Studio (procedural gradient)
        setEnvFromResult(createStudioEnvironment());
        applyEnvironment();
        return;
    }

    if (path === 'usd') {
        // Use stored dome light env map
        if (state.domeLightData && state.domeLightData.texture) {
            state.envMap = state.domeLightData.texture;
            // DomeLight source may not have a separate equirect; use PMREM for both
            state.envMapSource = state.domeLightData.sourceTexture || state.domeLightData.texture;
            state.envIntensity = state.domeLightData.intensity || 1.0;
            applyEnvironment();
        } else {
            showToast('No USD DomeLight available, using studio');
            setEnvFromResult(createStudioEnvironment());
            applyEnvironment();
        }
        return;
    }

    if (path.startsWith('constant:')) {
        const hexColor = path.substring('constant:'.length);
        setEnvFromResult(createConstantColorEnvironment(hexColor));
        applyEnvironment();
        return;
    }
}

/**
 * Apply the current envMap to the scene and all materials.
 */
function applyEnvironment() {
    // scene.environment drives IBL for all MeshStandardMaterial/MeshPhysicalMaterial
    state.scene.environment = state.envEnabled ? state.envMap : null;

    // Use the original equirectangular source for background (renders correctly)
    // Fall back to PMREM texture if no source is available
    const bgTexture = state.envMapSource || state.envMap;
    state.scene.background = state.showBackground ? bgTexture : new THREE.Color(0x1a1a2e);

    // Set envMapIntensity on all materials (don't set mat.envMap — let scene.environment handle it)
    const intensity = state.envEnabled ? state.envIntensity : 0;
    state.materials.forEach(mat => {
        mat.envMapIntensity = intensity;
        mat.needsUpdate = true;
    });

    // Update renderer tone mapping
    const tmValue = TONE_MAPPINGS[state.toneMapping];
    if (tmValue !== undefined) {
        state.renderer.toneMapping = tmValue;
    }
    state.renderer.toneMappingExposure = state.exposure;
}

// ============================================================================
// USD Light Loading
// ============================================================================

/**
 * Remove all USD-created lights from the scene.
 */
function clearUSDLights() {
    for (const light of state.usdLights) {
        state.scene.remove(light);
        if (light.dispose) light.dispose();
    }
    state.usdLights = [];
    state.domeLightData = null;
}

/**
 * Load non-dome USD lights (point, distant, rect, spot) from the USD scene.
 * Converts USD light types to Three.js light objects.
 */
async function loadUSDLights(usdLoader) {
    if (!usdLoader || !usdLoader.numLights) return;

    const numLights = usdLoader.numLights();
    if (numLights === 0) return;

    let loadedCount = 0;

    for (let i = 0; i < numLights; i++) {
        let lightData;
        try {
            lightData = usdLoader.getLight(i);
        } catch (e) {
            continue;
        }
        if (!lightData || lightData.error) continue;

        const type = (lightData.type || '').toLowerCase();

        // Skip dome lights (handled separately)
        if (type === 'dome' || type === 'domelight') continue;

        // Calculate intensity: intensity * 2^exposure
        let intensity = lightData.intensity !== undefined ? lightData.intensity : 1.0;
        if (lightData.exposure && lightData.exposure !== 0) {
            intensity *= Math.pow(2, lightData.exposure);
        }

        const color = new THREE.Color(
            lightData.color?.[0] || 1,
            lightData.color?.[1] || 1,
            lightData.color?.[2] || 1
        );

        // Extract transform
        const position = new THREE.Vector3(
            lightData.position?.[0] || 0,
            lightData.position?.[1] || 0,
            lightData.position?.[2] || 0
        );
        const quaternion = new THREE.Quaternion();

        if (lightData.transform && lightData.transform.length === 16) {
            const matrix = new THREE.Matrix4();
            matrix.fromArray(lightData.transform);
            const scale = new THREE.Vector3();
            matrix.decompose(position, quaternion, scale);
        }

        let light = null;
        let lightGroup = null;

        switch (type) {
            case 'point':
            case 'sphere': {
                if (lightData.shapingConeAngle && lightData.shapingConeAngle < 90) {
                    light = new THREE.SpotLight(color, intensity);
                    light.angle = THREE.MathUtils.degToRad(lightData.shapingConeAngle);
                    light.penumbra = lightData.shapingConeSoftness || 0;
                    light.decay = 2;
                    lightGroup = new THREE.Group();
                    lightGroup.add(light);
                    light.target.position.set(0, 0, -5);
                    lightGroup.add(light.target);
                } else {
                    light = new THREE.PointLight(color, intensity);
                    light.decay = 2;
                    light.position.copy(position);
                }
                break;
            }
            case 'distant': {
                light = new THREE.DirectionalLight(color, intensity);
                lightGroup = new THREE.Group();
                light.position.set(0, 0, 0);
                light.target.position.set(0, 0, -1);
                lightGroup.add(light);
                lightGroup.add(light.target);
                lightGroup.quaternion.copy(quaternion);
                const direction = new THREE.Vector3(0, 0, -1).applyQuaternion(quaternion);
                lightGroup.position.copy(direction.multiplyScalar(-50));
                break;
            }
            case 'rect': {
                const width = lightData.width || 1;
                const height = lightData.height || 1;
                light = new THREE.RectAreaLight(color, intensity, width, height);
                light.position.copy(position);
                light.quaternion.copy(quaternion);
                break;
            }
            default:
                continue;
        }

        if (!light) continue;

        const obj = lightGroup || light;
        // For distant lights, position/quaternion are already set inside the case
        if (lightGroup && type !== 'distant') {
            lightGroup.position.copy(position);
            lightGroup.quaternion.copy(quaternion);
        } else if (!lightGroup && type !== 'point' && type !== 'sphere') {
            // Non-group lights that haven't had position set yet
            light.position.copy(position);
        }

        obj.name = lightData.name || `USDLight_${i}`;
        state.scene.add(obj);
        state.usdLights.push(obj);
        loadedCount++;
    }

    if (loadedCount > 0) {
        console.log(`Loaded ${loadedCount} USD lights`);
    }
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
        clearSelection();
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        // Build scene (material loading consolidated inside buildScene)
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

// ============================================================================
// Texture Loading Helpers
// ============================================================================

function hasOpenPBRTexture(param) {
    return param && typeof param === 'object' && param.textureId !== undefined && param.textureId >= 0;
}

function getOpenPBRTextureId(param) {
    if (!param || typeof param !== 'object') return -1;
    return param.textureId !== undefined ? param.textureId : -1;
}

function extractOpenPBRValue(param, defaultVal) {
    if (param === undefined || param === null) return defaultVal;
    if (typeof param === 'object' && param.value !== undefined) return param.value;
    if (typeof param === 'number' || Array.isArray(param)) return param;
    return defaultVal;
}

/**
 * Flatten nested openPBR structure into flat param keys.
 * The C++ serializer groups params by category (e.g. openPBR.base.base_color),
 * but our code expects flat access (e.g. openPBR.base_color).
 * This function returns a flat object with all params + nodeGraph.
 */
function flattenOpenPBR(openPBR) {
    if (!openPBR) return {};
    const flat = {};
    const categoryKeys = ['base', 'specular', 'transmission', 'subsurface', 'sheen',
                          'fuzz', 'thin_film', 'coat', 'emission', 'geometry'];
    for (const cat of categoryKeys) {
        const section = openPBR[cat];
        if (section && typeof section === 'object') {
            Object.assign(flat, section);
        }
    }
    // Preserve nodeGraph at top level
    if (openPBR.nodeGraph) flat.nodeGraph = openPBR.nodeGraph;
    if (openPBR.type) flat.type = openPBR.type;
    return flat;
}

function resolveTextureId(nativeLoader, textureId) {
    if (textureId < 0) return textureId;
    try {
        const tex = nativeLoader.getTexture(textureId);
        const texImage = nativeLoader.getImage(tex.textureImageId);
        if (texImage.bufferId === -1 && texImage.uri) {
            const filename = texImage.uri.replace(/^\.\//, '');
            const numImages = nativeLoader.numImages();
            for (let i = 0; i < numImages; i++) {
                const altImage = nativeLoader.getImage(i);
                if (altImage.bufferId >= 0 && altImage.uri === filename) {
                    const numTextures = nativeLoader.numTextures();
                    for (let t = 0; t < numTextures; t++) {
                        const altTex = nativeLoader.getTexture(t);
                        if (altTex.textureImageId === i) return t;
                    }
                    break;
                }
            }
        }
    } catch (e) { /* ignore */ }
    return textureId;
}

/**
 * Find a texture ID in the nativeLoader by matching a filename from a node graph image node.
 * Returns the texture index, or -1 if not found.
 */
function findTextureIdByFilename(nativeLoader, filename) {
    if (!filename || !nativeLoader) return -1;
    const cleanName = filename.replace(/^[@.]\//, '').replace(/@$/, '');

    const numImages = nativeLoader.numImages();
    for (let i = 0; i < numImages; i++) {
        const img = nativeLoader.getImage(i);
        if (!img.uri) continue;
        const imgName = img.uri.replace(/^\.\//, '');
        if (imgName === cleanName || imgName.endsWith(cleanName) || cleanName.endsWith(imgName)) {
            // Found matching image - find a texture that references it
            const numTextures = nativeLoader.numTextures();
            for (let t = 0; t < numTextures; t++) {
                const tex = nativeLoader.getTexture(t);
                if (tex.textureImageId === i) return t;
            }
            // Image found but no texture references it - try loading via any texture with data
            if (img.bufferId >= 0) {
                // Look for any texture pointing to this image
                for (let t = 0; t < numTextures; t++) {
                    const tex = nativeLoader.getTexture(t);
                    const altImg = nativeLoader.getImage(tex.textureImageId);
                    if (altImg.bufferId >= 0 && altImg.uri) {
                        const altName = altImg.uri.replace(/^\.\//, '');
                        if (altName === cleanName) return t;
                    }
                }
            }
        }
    }
    return -1;
}

/**
 * Scan a node graph for image/tiledimage nodes and determine which shader
 * parameters they connect to via the connections array.
 * Returns a map: { shaderParamName: filename }
 */
function findNodeGraphTextures(nodeGraphData) {
    const result = {};
    if (!nodeGraphData) return result;

    const ng = nodeGraphData.nodegraph || nodeGraphData;
    const nodes = ng.nodes || [];
    const outputs = ng.outputs || [];
    const connections = nodeGraphData.connections || [];

    // Find image/tiledimage nodes
    const imageNodes = new Map(); // nodeName -> filename
    for (const node of nodes) {
        const cat = (node.category || '').replace(/_(color3|color4|float|vector2|vector3|vector4)$/, '');
        if (cat === 'image' || cat === 'tiledimage') {
            const fileInput = (node.inputs || []).find(i => i.name === 'file');
            if (fileInput && fileInput.value) {
                imageNodes.set(node.name, fileInput.value);
            }
        }
    }

    if (imageNodes.size === 0) return result;

    // Build a reverse-dependency map: nodeName -> set of nodes that depend on it
    // Then trace from image nodes to outputs
    const dependsOn = new Map(); // nodeName -> [nodenames it takes input from]
    for (const node of nodes) {
        for (const input of (node.inputs || [])) {
            if (input.nodename) {
                if (!dependsOn.has(node.name)) dependsOn.set(node.name, []);
                dependsOn.get(node.name).push(input.nodename);
            }
        }
    }

    // For each output, check if it transitively depends on an image node
    function tracesToImageNode(nodeName, visited = new Set()) {
        if (visited.has(nodeName)) return null;
        visited.add(nodeName);
        if (imageNodes.has(nodeName)) return nodeName;
        const deps = dependsOn.get(nodeName) || [];
        for (const dep of deps) {
            const found = tracesToImageNode(dep, visited);
            if (found) return found;
        }
        return null;
    }

    // Map outputs to shader params via connections
    const outputToParam = new Map();
    for (const conn of connections) {
        if (conn.input && conn.output) {
            outputToParam.set(conn.output, conn.input);
        }
    }

    // For each output, trace to image node
    for (const output of outputs) {
        if (!output.nodename) continue;
        const imageNodeName = tracesToImageNode(output.nodename);
        if (!imageNodeName) continue;

        const paramName = outputToParam.get(output.name);
        if (paramName) {
            result[paramName] = imageNodes.get(imageNodeName);
        }
    }

    return result;
}

async function loadMaterialTextures(openPBR, nativeLoader) {
    const textures = {};
    if (!nativeLoader) return textures;

    const textureParams = [
        ['base_color', 'base_color'],
        ['specular_roughness', 'specular_roughness'],
        ['base_metalness', 'base_metalness'],
        ['emission_color', 'emission_color'],
        ['coat_weight', 'coat_weight'],
    ];

    // First: check direct texture references on shader params
    for (const [openPBRKey, texKey] of textureParams) {
        const param = openPBR[openPBRKey];
        if (hasOpenPBRTexture(param)) {
            try {
                const texId = resolveTextureId(nativeLoader, getOpenPBRTextureId(param));
                const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(nativeLoader, texId);
                if (texture) {
                    texture.colorSpace = THREE.SRGBColorSpace;
                    textures[texKey] = texture;
                }
            } catch (err) {
                console.warn(`Failed to load ${openPBRKey} texture:`, err);
            }
        }
    }

    // Normal map (nested under geometry section)
    const geometrySection = openPBR.geometry || {};
    const normalParam = geometrySection.normal || openPBR.normal || openPBR.geometry_normal;
    if (hasOpenPBRTexture(normalParam)) {
        try {
            const texId = resolveTextureId(nativeLoader, getOpenPBRTextureId(normalParam));
            const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(nativeLoader, texId);
            if (texture) {
                texture.colorSpace = THREE.LinearSRGBColorSpace;
                textures['normal'] = texture;
            }
        } catch (err) {
            console.warn('Failed to load normal map texture:', err);
        }
    }

    // Second: scan node graph for image nodes (handles base_color connected via node graph)
    if (openPBR.nodeGraph) {
        const ngTextures = findNodeGraphTextures(openPBR.nodeGraph);
        for (const [paramName, filename] of Object.entries(ngTextures)) {
            // Map shader param names to texture keys
            const paramToTexKey = {
                'base_color': 'base_color',
                'specular_roughness': 'specular_roughness',
                'base_metalness': 'base_metalness',
                'emission_color': 'emission_color',
                'coat_weight': 'coat_weight',
            };
            const texKey = paramToTexKey[paramName];
            if (!texKey || textures[texKey]) continue; // skip if already loaded

            try {
                const texId = findTextureIdByFilename(nativeLoader, filename);
                if (texId >= 0) {
                    const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(nativeLoader, texId);
                    if (texture) {
                        texture.colorSpace = (paramName === 'normal' || paramName === 'geometry_normal')
                            ? THREE.LinearSRGBColorSpace : THREE.SRGBColorSpace;
                        textures[texKey] = texture;
                    }
                }
            } catch (err) {
                console.warn(`Failed to load node graph texture for ${paramName}:`, err);
            }
        }
    }

    return textures;
}

async function buildScene() {
    const usd = state.usdData;

    // Clear and load material data (consolidated from callers)
    state.materials = [];
    state.materialData = [];

    const numMaterials = usd.numMaterials();
    for (let i = 0; i < numMaterials; i++) {
        const result = usd.getMaterialWithFormat(i, 'json');
        if (!result.error) {
            state.materialData.push(JSON.parse(result.data));
        }
    }

    // Create OpenPBR materials from material data
    for (const matData of state.materialData) {
        const openPBR = flattenOpenPBR(matData.openPBR || {});
        let nodeGraph = openPBR.nodeGraph;

        // Optionally optimize
        if (nodeGraph) {
            nodeGraph = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
        }

        const params = {
            base_color: extractOpenPBRValue(openPBR.base_color, DEFAULT_OPENPBR_PARAMS.base_color),
            base_metalness: extractOpenPBRValue(openPBR.base_metalness, DEFAULT_OPENPBR_PARAMS.base_metalness),
            specular_roughness: extractOpenPBRValue(openPBR.specular_roughness, DEFAULT_OPENPBR_PARAMS.specular_roughness),
            specular_ior: extractOpenPBRValue(openPBR.specular_ior, DEFAULT_OPENPBR_PARAMS.specular_ior),
            coat_weight: extractOpenPBRValue(openPBR.coat_weight, DEFAULT_OPENPBR_PARAMS.coat_weight),
            emission_color: extractOpenPBRValue(openPBR.emission_color, DEFAULT_OPENPBR_PARAMS.emission_color)
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

        // Load textures from USDZ
        const textures = await loadMaterialTextures(openPBR, state.usdData);
        const material = createOpenPBRMaterial(params, textures);
        material.name = matData.name || `Material_${state.materials.length}`;
        state.materials.push(material);
    }

    // Build scene hierarchy using TinyUSDZLoaderUtils
    const rootNode = usd.getDefaultRootNode();
    let root;

    if (rootNode) {
        // Build with proper transforms and submeshes
        const defaultMtl = new THREE.MeshStandardMaterial({ color: 0x888888 });
        root = await TinyUSDZLoaderUtils.buildThreeNode(rootNode, defaultMtl, usd, {});

        // Replace materials with our OpenPBR materials
        // Build name→index map for fast lookup
        const matNameToIndex = new Map();
        for (let i = 0; i < state.materialData.length; i++) {
            const name = state.materialData[i].name;
            if (name) matNameToIndex.set(name, i);
        }

        root.traverse((obj) => {
            if (!obj.isMesh) return;

            // Get doubleSided from geometry userData (set by convertUsdMeshToThreeMesh)
            const doubleSided = obj.geometry?.userData?.doubleSided;

            if (Array.isArray(obj.material)) {
                // Multi-material (GeomSubset) - replace each
                obj.material = obj.material.map(mat => {
                    const rawData = mat.userData?.rawData;
                    const name = rawData?.name;
                    const idx = name !== undefined ? matNameToIndex.get(name) : undefined;
                    if (idx !== undefined) {
                        const openPBRMat = state.materials[idx];
                        if (doubleSided) openPBRMat.side = THREE.DoubleSide;
                        return openPBRMat;
                    }
                    return mat;
                });
            } else {
                const rawData = obj.material.userData?.rawData;
                const name = rawData?.name;
                const idx = name !== undefined ? matNameToIndex.get(name) : undefined;
                if (idx !== undefined) {
                    obj.material = state.materials[idx];
                    if (doubleSided) obj.material.side = THREE.DoubleSide;
                }
            }
        });
    } else {
        // Fallback: flat mesh loop using TinyUSDZLoaderUtils for geometry
        root = new THREE.Group();
        const numMeshes = usd.numMeshes();

        for (let i = 0; i < numMeshes; i++) {
            const meshData = usd.getMesh(i);
            if (!meshData || !meshData.points || meshData.points.length === 0) continue;

            const geometry = TinyUSDZLoaderUtils.convertUsdMeshToThreeMesh(meshData);
            const materialId = meshData.materialId ?? 0;

            // Handle submeshes (GeomSubset multi-material)
            if (geometry.userData['submeshes'] && geometry.userData['submeshes'].length > 0) {
                const submeshes = geometry.userData['submeshes'];
                const materials = [];
                const matIdToIdx = new Map();

                for (const sub of submeshes) {
                    if (!matIdToIdx.has(sub.materialId)) {
                        matIdToIdx.set(sub.materialId, materials.length);
                        const mat = state.materials[sub.materialId] || new THREE.MeshStandardMaterial({ color: 0x888888 });
                        materials.push(mat);
                    }
                    geometry.addGroup(sub.start, sub.count, matIdToIdx.get(sub.materialId));
                }

                const mesh = new THREE.Mesh(geometry, materials);
                mesh.name = meshData.primName || `Mesh_${i}`;
                root.add(mesh);
            } else {
                const material = state.materials[materialId] || new THREE.MeshStandardMaterial({ color: 0x888888 });
                const mesh = new THREE.Mesh(geometry, material);
                mesh.name = meshData.primName || `Mesh_${i}`;
                if (meshData.doubleSided) material.side = THREE.DoubleSide;
                root.add(mesh);
            }
        }
    }

    state.currentModel = root;

    // Apply Z-up to Y-up conversion if needed
    const metadata = usd.getSceneMetadata ? usd.getSceneMetadata() : {};
    if ((metadata.upAxis || 'Y') === 'Z') {
        root.rotation.x = -Math.PI / 2;
    }

    state.scene.add(root);
    fitCameraToObject(root);

    // Load USD lights (DomeLight for environment, others for direct lighting)
    clearUSDLights();

    try {
        const domeLightData = await TinyUSDZLoaderUtils.loadDomeLightFromUSD(usd, state.pmremGenerator);
        if (domeLightData) {
            state.domeLightData = domeLightData;
            state.envMap = domeLightData.texture;
            state.envMapSource = domeLightData.sourceTexture || domeLightData.texture;
            state.envIntensity = domeLightData.intensity || 1.0;
            state.envPreset = 'usd_dome';
            applyEnvironment();
            updateEnvUI();
            console.log('DomeLight loaded from USD');
        }
    } catch (e) {
        console.warn('Failed to load DomeLight:', e);
    }

    // Load non-dome USD lights
    await loadUSDLights(usd);

    // If no DomeLight found, use studio environment
    if (!state.domeLightData) {
        state.envPreset = 'studio';
        setEnvFromResult(createStudioEnvironment());
        applyEnvironment();
        updateEnvUI();
    }

    const numMeshes = usd.numMeshes();
    updateStatus(`Loaded: ${numMeshes} meshes, ${state.materials.length} materials`);
    document.getElementById('mesh-count').textContent = numMeshes;
    document.getElementById('material-count').textContent = state.materials.length;
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
    clearSelection();
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

    // Apply studio environment to sample scene
    clearUSDLights();
    state.envPreset = 'studio';
    setEnvFromResult(createStudioEnvironment());
    applyEnvironment();
    updateEnvUI();

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
        clearSelection();
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        // Build scene (material loading consolidated inside buildScene)
        await buildScene();

        // Update material selector
        updateMaterialSelector();

        // Load first material's node graph
        if (state.materialData.length > 0) {
            selectMaterial(0);
        }

        const numMaterials = state.usdData.numMaterials();
        console.log(`Found ${numMaterials} materials`);
        showToast(`Loaded Blender sample with ${numMaterials} materials`);

    } catch (error) {
        console.error('Load error:', error);
        updateStatus(`Error: ${error.message}`);
        showToast(`Failed: ${error.message}`);
    } finally {
        showLoading(false);
    }
};

/**
 * Compute the bounding box of all nodes in the graph.
 * Returns { minX, minY, maxX, maxY } or null if no nodes.
 */
function getNodesBoundingBox() {
    if (!state.graph || !state.graph._nodes || state.graph._nodes.length === 0) return null;
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const n of state.graph._nodes) {
        const w = n.size[0] || 100;
        const h = n.size[1] || 60;
        minX = Math.min(minX, n.pos[0]);
        minY = Math.min(minY, n.pos[1]);
        maxX = Math.max(maxX, n.pos[0] + w);
        maxY = Math.max(maxY, n.pos[1] + h);
    }
    return { minX, minY, maxX, maxY };
}

window.fitGraph = function() {
    if (state.graphCanvas) {
        state.graphCanvas.ds.reset();
        state.graph.arrange();
    }
};

window.fitGraphWidth = function() {
    if (!state.graphCanvas) return;
    const bb = getNodesBoundingBox();
    if (!bb) return;
    const padding = 40;
    const canvasW = state.graphCanvas.canvas.width;
    const graphW = bb.maxX - bb.minX + padding * 2;
    const scale = Math.min(canvasW / graphW, 2.0);
    const centerY = (bb.minY + bb.maxY) / 2;
    state.graphCanvas.ds.scale = scale;
    state.graphCanvas.ds.offset[0] = -bb.minX * scale + padding * scale;
    state.graphCanvas.ds.offset[1] = -centerY * scale + state.graphCanvas.canvas.height / 2;
    state.graphCanvas.setDirty(true, true);
};

window.fitGraphHeight = function() {
    if (!state.graphCanvas) return;
    const bb = getNodesBoundingBox();
    if (!bb) return;
    const padding = 40;
    const canvasH = state.graphCanvas.canvas.height;
    const graphH = bb.maxY - bb.minY + padding * 2;
    const scale = Math.min(canvasH / graphH, 2.0);
    const centerX = (bb.minX + bb.maxX) / 2;
    state.graphCanvas.ds.scale = scale;
    state.graphCanvas.ds.offset[0] = -centerX * scale + state.graphCanvas.canvas.width / 2;
    state.graphCanvas.ds.offset[1] = -bb.minY * scale + padding * scale;
    state.graphCanvas.setDirty(true, true);
};

window.fitGraphAll = function() {
    if (!state.graphCanvas) return;
    const bb = getNodesBoundingBox();
    if (!bb) return;
    const padding = 40;
    const canvasW = state.graphCanvas.canvas.width;
    const canvasH = state.graphCanvas.canvas.height;
    const graphW = bb.maxX - bb.minX + padding * 2;
    const graphH = bb.maxY - bb.minY + padding * 2;
    const scale = Math.min(canvasW / graphW, canvasH / graphH, 2.0);
    const centerX = (bb.minX + bb.maxX) / 2;
    const centerY = (bb.minY + bb.maxY) / 2;
    state.graphCanvas.ds.scale = scale;
    state.graphCanvas.ds.offset[0] = -centerX * scale + canvasW / 2;
    state.graphCanvas.ds.offset[1] = -centerY * scale + canvasH / 2;
    state.graphCanvas.setDirty(true, true);
};

window.updateMaterialFromGraph = function() {
    evaluateAndApplyMaterial();
    showToast('Material updated from graph');
};

window.toggleInteractiveUpdate = function(enabled) {
    state.interactiveUpdate = enabled;
    if (enabled && state.graphDirty) {
        evaluateAndApplyMaterial();
    }
    showToast(enabled ? 'Live update enabled' : 'Live update disabled');
};

// Expose markGraphDirty for node widget callbacks
window._mtlxMarkDirty = function() {
    markGraphDirty();
};

// ============================================================================
// Lighting Control Functions
// ============================================================================

/**
 * Update the lighting UI controls to reflect current state.
 */
function updateEnvUI() {
    const envSelect = document.getElementById('env-select');
    if (envSelect) envSelect.value = state.envPreset;

    const intensitySlider = document.getElementById('env-intensity');
    const intensityVal = document.getElementById('env-intensity-val');
    if (intensitySlider) intensitySlider.value = state.envIntensity;
    if (intensityVal) intensityVal.textContent = state.envIntensity.toFixed(1);

    const exposureSlider = document.getElementById('exposure');
    const exposureVal = document.getElementById('exposure-val');
    if (exposureSlider) exposureSlider.value = state.exposure;
    if (exposureVal) exposureVal.textContent = state.exposure.toFixed(1);

    const tonemapSelect = document.getElementById('tonemap-select');
    if (tonemapSelect) tonemapSelect.value = state.toneMapping;

    const showBg = document.getElementById('show-bg');
    if (showBg) showBg.checked = state.showBackground;

    buildLightListUI();
}

window.changeEnvironment = function(preset) {
    loadEnvironment(preset);
    showToast(`Environment: ${preset}`);
};

window.changeEnvIntensity = function(val) {
    state.envIntensity = val;
    document.getElementById('env-intensity-val').textContent = val.toFixed(1);
    applyEnvironment();
};

window.changeExposure = function(val) {
    state.exposure = val;
    document.getElementById('exposure-val').textContent = val.toFixed(1);
    state.renderer.toneMappingExposure = val;
};

window.changeToneMapping = function(mode) {
    state.toneMapping = mode;
    const tmValue = TONE_MAPPINGS[mode];
    if (tmValue !== undefined) {
        state.renderer.toneMapping = tmValue;
    }
};

window.toggleBackground = function(show) {
    state.showBackground = show;
    const bgTexture = state.envMapSource || state.envMap;
    state.scene.background = show ? bgTexture : new THREE.Color(0x1a1a2e);
};

window.toggleLightingPanel = function() {
    const panel = document.getElementById('lighting-controls');
    panel.classList.toggle('collapsed');
};

// ============================================================================
// Light List UI
// ============================================================================

/**
 * Get a short type label for a Three.js light.
 */
function getLightTypeLabel(light) {
    if (light.isAmbientLight) return 'Ambient';
    if (light.isDirectionalLight) return 'Dir';
    if (light.isPointLight) return 'Point';
    if (light.isSpotLight) return 'Spot';
    if (light.isRectAreaLight) return 'Rect';
    if (light.isHemisphereLight) return 'Hemi';
    // Groups (e.g. for distant/spot with target)
    if (light.isGroup) {
        const child = light.children.find(c => c.isLight);
        if (child) return getLightTypeLabel(child);
        return 'Group';
    }
    return 'Light';
}

/**
 * Get a CSS color string from a Three.js light's color.
 */
function getLightColorCSS(light) {
    let color = light.color;
    if (!color && light.isGroup) {
        const child = light.children.find(c => c.isLight);
        if (child) color = child.color;
    }
    if (!color) return '#ffffff';
    return `#${color.getHexString()}`;
}

/**
 * Build the light list UI from current state.
 * Called after scene loads or lights change.
 */
function buildLightListUI() {
    const container = document.getElementById('light-list');
    if (!container) return;
    container.innerHTML = '';

    // Envmap row
    const envRow = document.createElement('div');
    envRow.className = 'light-item';
    envRow.innerHTML = `
        <input type="checkbox" ${state.envEnabled ? 'checked' : ''}
               onchange="toggleEnvMap(this.checked)" title="Toggle environment map">
        <span class="light-swatch" style="background: linear-gradient(135deg, #fff, #999, #666)"></span>
        <span class="light-label">Envmap</span>
        <span class="light-type">IBL</span>
    `;
    container.appendChild(envRow);

    // Default lights
    for (let i = 0; i < state.defaultLights.length; i++) {
        const light = state.defaultLights[i];
        const row = createLightRow(light, 'default', i);
        container.appendChild(row);
    }

    // USD lights
    for (let i = 0; i < state.usdLights.length; i++) {
        const light = state.usdLights[i];
        const row = createLightRow(light, 'usd', i);
        container.appendChild(row);
    }
}

function createLightRow(light, source, index) {
    const row = document.createElement('div');
    row.className = 'light-item';

    const name = light.name || `${source}_${index}`;
    const typeLabel = getLightTypeLabel(light);
    const colorCSS = getLightColorCSS(light);
    const isOn = light.visible !== false;

    row.innerHTML = `
        <input type="checkbox" ${isOn ? 'checked' : ''}
               onchange="toggleLight('${source}', ${index}, this.checked)" title="Toggle light">
        <span class="light-swatch" style="background: ${colorCSS}"></span>
        <span class="light-label">${name}</span>
        <span class="light-type">${typeLabel}</span>
    `;
    return row;
}

window.toggleLight = function(source, index, enabled) {
    let light;
    if (source === 'default') {
        light = state.defaultLights[index];
    } else if (source === 'usd') {
        light = state.usdLights[index];
    }
    if (!light) return;
    light.visible = enabled;
};

window.toggleEnvMap = function(enabled) {
    state.envEnabled = enabled;
    applyEnvironment();
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
        clearSelection();
        if (state.currentModel) {
            state.scene.remove(state.currentModel);
            disposeObject(state.currentModel);
        }

        // Build scene (material loading consolidated inside buildScene)
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
// Object Picking
// ============================================================================

function onCanvasClick(event) {
    if (event.target !== state.renderer.domElement) return;

    const rect = state.renderer.domElement.getBoundingClientRect();
    const mouse = new THREE.Vector2(
        ((event.clientX - rect.left) / rect.width) * 2 - 1,
        -((event.clientY - rect.top) / rect.height) * 2 + 1
    );

    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(mouse, state.camera);

    if (!state.currentModel) return;
    const intersects = raycaster.intersectObjects(state.currentModel.children, true);

    if (intersects.length > 0) {
        const hit = intersects.find(i => i.object.isMesh);
        if (hit) {
            pickObject(hit);
        } else {
            clearSelection();
        }
    } else {
        clearSelection();
    }
}

function pickObject(hit) {
    const mesh = hit.object;

    // Determine which material was clicked
    let clickedMaterial;
    if (Array.isArray(mesh.material)) {
        clickedMaterial = mesh.material[hit.materialIndex ?? 0];
    } else {
        clickedMaterial = mesh.material;
    }

    // Find matching index in state.materials
    let matIndex = state.materials.indexOf(clickedMaterial);
    if (matIndex < 0) {
        matIndex = state.materials.findIndex(m => m.name === clickedMaterial?.name);
    }

    // Highlight the selected object
    clearSelectionHighlight();
    state.selectedObject = mesh;
    const box = new THREE.Box3().setFromObject(mesh);
    const helper = new THREE.Box3Helper(box, 0x00ff00);
    helper.name = '__selectionHelper__';
    state.scene.add(helper);
    state.selectionHelper = helper;

    // Select material and sync UI
    if (matIndex >= 0) {
        selectMaterial(matIndex);
        const select = document.getElementById('material-select');
        if (select) select.value = matIndex;
        showToast(`Selected: ${mesh.name || 'Mesh'} → ${state.materialData[matIndex]?.name || 'Material'}`);
    } else {
        showToast(`Selected: ${mesh.name || 'Mesh'} (no editable material)`);
    }

    const absPath = mesh.userData?.['primMeta.absPath'] || '';
    updateStatus(`Selected: ${mesh.name || 'Mesh'}${absPath ? ' (' + absPath + ')' : ''}`);
}

function clearSelectionHighlight() {
    if (state.selectionHelper) {
        state.scene.remove(state.selectionHelper);
        state.selectionHelper.dispose();
        state.selectionHelper = null;
    }
}

function clearSelection() {
    clearSelectionHighlight();
    state.selectedObject = null;
    updateStatus('Ready');
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

    // Load default studio environment (provides IBL reflections from the start)
    setEnvFromResult(createStudioEnvironment());
    applyEnvironment();
    buildLightListUI();

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

    // Keyboard shortcuts for node graph fitting
    document.addEventListener('keydown', (e) => {
        // Ignore if focus is in an input/select/textarea
        const tag = e.target.tagName;
        if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;

        if (e.key === 'f' || e.key === 'F') {
            e.preventDefault();
            window.fitGraphWidth();
        } else if (e.key === 'a' || e.key === 'A') {
            e.preventDefault();
            window.fitGraphHeight();
        }
    });

    // Start render loop
    animate();

    // Initial resize
    onWindowResize();

    // Load default asset
    loadBlenderSample();
}

init().catch(console.error);
