/**
 * OpenPBR NodeGraph Demo
 *
 * Standalone demo showing OpenPBR materials with MaterialX node graph visualization.
 * Uses Three.js for 3D rendering and LiteGraph.js for node graph display.
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';
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
    'goegap': 'hdr:assets/textures/goegap_1k.hdr',
    'pisa': 'cube:assets/textures/cube/pisa/',
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
    interactiveUpdate: true,
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
    showBackground: true,
    toneMapping: 'aces',
    exposure: 1.0,

    // USD lights
    domeLightData: null,
    usdLights: [],          // Three.js lights created from USD
    defaultLights: [],      // Default directional lights
    envEnabled: true,       // Whether envmap IBL is active

    // Shading debug
    shadingMode: 'solid',
    originalMaterialsMap: new Map(),

    // Demand rendering: only call renderer.render() when this is true
    // or when OrbitControls is still damping (controls.update() returns true).
    renderNeeded: true,

    // Undo/redo history for node graph edits
    undoStack: [],              // array of JSON-serialized graph snapshots (pre-change)
    redoStack: [],
    _pendingHistorySnapshot: null,  // snapshot taken on pointerdown, committed if graph changed
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
            // Color swatch: drawn as a custom widget (type unknown → default case calls w.draw).
            // This runs inside drawNodeWidgets after the sliders, so it is never overwritten by them.
            // Reading node.properties.value at draw-time means it always reflects the current value.
            const swatch = this.addWidget('color_swatch', 'color', null, () => {}, {});
            swatch.draw = (ctx, node, widget_width, y, H) => {
                const vv = node.properties.value;
                if (Array.isArray(vv) && vv.length >= 3) {
                    ctx.fillStyle = `rgb(${Math.round(vv[0]*255)},${Math.round(vv[1]*255)},${Math.round(vv[2]*255)})`;
                    ctx.fillRect(15, y, widget_width - 30, H);
                }
            };
        } else {
            this.properties.valueType = 'float';
            const numVal = (typeof v === 'number') ? v : 0;
            this.addWidget('slider', 'Value', numVal, (val) => {
                this.properties.value = val;
                this.setDirtyCanvas(true);
                if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            }, { min: 0, max: 1 });
        }
    };
    // Color preview for color3 constant nodes is handled by the 'color_swatch' custom widget
    // added in _setupWidgets(). No onDrawForeground needed here for that purpose.
    ConstantNode.prototype.onPropertyChanged = function() {
        this._setupWidgets();
    };
    // Re-setup widgets after graph.configure() restores a snapshot so slider
    // values reflect the restored properties.value (not the constructor default).
    ConstantNode.prototype.onConfigure = function() {
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

    // Helper: convert [r,g,b] (0-1) to hex string
    function rgbToHex(c) {
        const r = Math.round(Math.max(0, Math.min(1, c[0])) * 255);
        const g = Math.round(Math.max(0, Math.min(1, c[1])) * 255);
        const b = Math.round(Math.max(0, Math.min(1, c[2])) * 255);
        return '#' + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
    }
    // Helper: convert hex string to [r,g,b] (0-1)
    function hexToRGB(hex) {
        const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
        if (!result) return [0, 0, 0];
        return [parseInt(result[1], 16) / 255, parseInt(result[2], 16) / 255, parseInt(result[3], 16) / 255];
    }
    // Helper: check if a value matches its default
    function isDefaultValue(val, def) {
        if (Array.isArray(val) && Array.isArray(def)) {
            return val.length === def.length && val.every((v, i) => Math.abs(v - def[i]) < 0.001);
        }
        if (typeof val === 'number' && typeof def === 'number') {
            return Math.abs(val - def) < 0.001;
        }
        return val === def;
    }

    // Group boundaries for input slot labels
    const OPENPBR_GROUPS = [
        { name: 'BASE', startIdx: 0 },
        { name: 'SPECULAR', startIdx: 3 },
        { name: 'TRANSMISSION', startIdx: 9 },
        { name: 'COAT', startIdx: 15 },
        { name: 'FUZZ', startIdx: 23 },
        { name: 'EMISSION', startIdx: 26 },
        { name: 'GEOMETRY', startIdx: 28 },
    ];

    // Parameter definitions: { type, default, min, max, step }
    const OPENPBR_PARAM_DEFS = {
        // Base layer
        'base_color':                    { type: 'color3', default: [0.8, 0.8, 0.8] },
        'base_metalness':                { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'base_diffuse_roughness':        { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        // Specular layer
        'specular_weight':               { type: 'float', default: 1.0, min: 0, max: 1, step: 0.01 },
        'specular_color':                { type: 'color3', default: [1, 1, 1] },
        'specular_roughness':            { type: 'float', default: 0.3, min: 0, max: 1, step: 0.01 },
        'specular_ior':                  { type: 'float', default: 1.5, min: 1.0, max: 3.0, step: 0.01 },
        'specular_anisotropy':           { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'specular_rotation':             { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        // Transmission layer
        'transmission_weight':           { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'transmission_color':            { type: 'color3', default: [1, 1, 1] },
        'transmission_depth':            { type: 'float', default: 0.0, min: 0, max: 100, step: 0.1 },
        'transmission_scatter':          { type: 'color3', default: [0, 0, 0] },
        'transmission_scatter_anisotropy': { type: 'float', default: 0.0, min: -1, max: 1, step: 0.01 },
        'transmission_dispersion':       { type: 'float', default: 0.0, min: 0, max: 100, step: 0.1 },
        // Coat layer
        'coat_weight':                   { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'coat_color':                    { type: 'color3', default: [1, 1, 1] },
        'coat_roughness':                { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'coat_ior':                      { type: 'float', default: 1.5, min: 1.0, max: 3.0, step: 0.01 },
        'coat_anisotropy':               { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'coat_rotation':                 { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'coat_affect_color':             { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'coat_affect_roughness':         { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        // Fuzz layer
        'fuzz_weight':                   { type: 'float', default: 0.0, min: 0, max: 1, step: 0.01 },
        'fuzz_color':                    { type: 'color3', default: [1, 1, 1] },
        'fuzz_roughness':                { type: 'float', default: 0.5, min: 0, max: 1, step: 0.01 },
        // Emission
        'emission_color':                { type: 'color3', default: [0, 0, 0] },
        'emission_luminance':            { type: 'float', default: 0.0, min: 0, max: 10000, step: 1.0 },
        // Geometry
        'geometry_opacity':              { type: 'float', default: 1.0, min: 0, max: 1, step: 0.01 },
    };

    function OpenPBRSurfaceNode() {
        // Base layer (0-2)
        this.addInput('base_color', 'color3');
        this.addInput('base_metalness', 'float');
        this.addInput('base_diffuse_roughness', 'float');
        // Specular layer (3-8)
        this.addInput('specular_weight', 'float');
        this.addInput('specular_color', 'color3');
        this.addInput('specular_roughness', 'float');
        this.addInput('specular_ior', 'float');
        this.addInput('specular_anisotropy', 'float');
        this.addInput('specular_rotation', 'float');
        // Transmission layer (9-14)
        this.addInput('transmission_weight', 'float');
        this.addInput('transmission_color', 'color3');
        this.addInput('transmission_depth', 'float');
        this.addInput('transmission_scatter', 'color3');
        this.addInput('transmission_scatter_anisotropy', 'float');
        this.addInput('transmission_dispersion', 'float');
        // Coat layer (15-22)
        this.addInput('coat_weight', 'float');
        this.addInput('coat_color', 'color3');
        this.addInput('coat_roughness', 'float');
        this.addInput('coat_ior', 'float');
        this.addInput('coat_anisotropy', 'float');
        this.addInput('coat_rotation', 'float');
        this.addInput('coat_affect_color', 'float');
        this.addInput('coat_affect_roughness', 'float');
        // Fuzz layer (23-25)
        this.addInput('fuzz_weight', 'float');
        this.addInput('fuzz_color', 'color3');
        this.addInput('fuzz_roughness', 'float');
        // Emission (26-27)
        this.addInput('emission_color', 'color3');
        this.addInput('emission_luminance', 'float');
        // Geometry (28-32)
        this.addInput('geometry_opacity', 'float');
        this.addInput('geometry_normal', 'vector3');
        this.addInput('geometry_tangent', 'vector3');
        this.addInput('geometry_coat_normal', 'vector3');
        this.addInput('geometry_coat_tangent', 'vector3');
        this.color = '#4CAF50';
        this.size = [240, 150];
        this.properties = {};
        // Track which input indices have inline widgets
        this._inlineSlots = new Set();
    }
    OpenPBRSurfaceNode.title = 'OpenPBR Surface';

    /**
     * Set up inline value widgets for unconnected inputs.
     * @param {Set} connectedSlots - Set of input slot indices that have node connections
     * @param {Object} materialValues - Current material values { base_color, base_metalness, ... }
     */
    // All 33 input names in order matching addInput calls
    const OPENPBR_INPUT_NAMES = [
        // Base (0-2)
        'base_color', 'base_metalness', 'base_diffuse_roughness',
        // Specular (3-8)
        'specular_weight', 'specular_color', 'specular_roughness',
        'specular_ior', 'specular_anisotropy', 'specular_rotation',
        // Transmission (9-14)
        'transmission_weight', 'transmission_color', 'transmission_depth',
        'transmission_scatter', 'transmission_scatter_anisotropy', 'transmission_dispersion',
        // Coat (15-22)
        'coat_weight', 'coat_color', 'coat_roughness', 'coat_ior',
        'coat_anisotropy', 'coat_rotation', 'coat_affect_color', 'coat_affect_roughness',
        // Fuzz (23-25)
        'fuzz_weight', 'fuzz_color', 'fuzz_roughness',
        // Emission (26-27)
        'emission_color', 'emission_luminance',
        // Geometry (28-32)
        'geometry_opacity', 'geometry_normal', 'geometry_tangent',
        'geometry_coat_normal', 'geometry_coat_tangent'
    ];

    // Short display names for widget labels
    const OPENPBR_SHORT_NAMES = {
        'base_color': 'color', 'base_metalness': 'metalness', 'base_diffuse_roughness': 'diff rough',
        'specular_weight': 'spec wt', 'specular_color': 'spec color', 'specular_roughness': 'spec rough',
        'specular_ior': 'spec ior', 'specular_anisotropy': 'spec aniso', 'specular_rotation': 'spec rot',
        'transmission_weight': 'trans wt', 'transmission_color': 'trans color',
        'transmission_depth': 'trans depth', 'transmission_scatter': 'trans scat',
        'transmission_scatter_anisotropy': 'scat aniso', 'transmission_dispersion': 'dispersion',
        'coat_weight': 'coat wt', 'coat_color': 'coat color', 'coat_roughness': 'coat rough',
        'coat_ior': 'coat ior', 'coat_anisotropy': 'coat aniso', 'coat_rotation': 'coat rot',
        'coat_affect_color': 'coat aff clr', 'coat_affect_roughness': 'coat aff rgh',
        'fuzz_weight': 'fuzz wt', 'fuzz_color': 'fuzz color', 'fuzz_roughness': 'fuzz rough',
        'emission_color': 'emit color', 'emission_luminance': 'emit lum',
        'geometry_opacity': 'opacity',
    };

    // Inline control layout constants
    // Empirical slot spacing that aligns with LiteGraph 0.7.18 rendering
    const SLOT_H = 18;
    const CTRL_W = 70;  // inline control width
    const CTRL_H = 14;  // inline control height
    const CTRL_PAD = 8; // right padding from node edge

    OpenPBRSurfaceNode.prototype._setupInlineWidgets = function(connectedSlots, materialValues) {
        // No LiteGraph widgets — we render custom inline controls via onDrawForeground
        this.widgets = [];
        this._inlineSlots = new Set();
        this._editableParams = new Map();
        this._dragState = null;

        for (let i = 0; i < OPENPBR_INPUT_NAMES.length; i++) {
            const name = OPENPBR_INPUT_NAMES[i];
            const def = OPENPBR_PARAM_DEFS[name];
            if (!def) continue; // Skip vector3 geometry inputs

            const currentVal = materialValues[name] !== undefined ? materialValues[name] : def.default;

            if (def.type === 'color3') {
                const c = Array.isArray(currentVal) ? currentVal : def.default;
                this.properties[name] = [c[0], c[1], c[2]];
            } else {
                this.properties[name] = typeof currentVal === 'number' ? currentVal : def.default;
            }

            // All unconnected params get inline controls
            if (!connectedSlots.has(i)) {
                this._inlineSlots.add(i);
                this._editableParams.set(i, { name, def });
            }
        }

        // Size based on input slots only (no widget area)
        const inputHeight = OPENPBR_INPUT_NAMES.length * SLOT_H + 30;
        this.size[0] = 240;
        this.size[1] = Math.max(inputHeight + 10, 150);
    };

    /** Return the Y center of input slot idx in node-local coords. */
    function _slotY(idx) {
        // Empirical: matches LiteGraph 0.7.18 slot rendering in onDrawForeground context
        return (idx + 1) * SLOT_H;
    }

    /** Return the bounding rect {x, y, w, h} for the inline control of slot idx. */
    function _ctrlRect(nodeW, idx) {
        const cy = _slotY(idx);
        return {
            x: nodeW - CTRL_W - CTRL_PAD,
            y: cy - CTRL_H / 2,
            w: CTRL_W,
            h: CTRL_H
        };
    }

    /**
     * Draw inline value controls next to each unconnected input slot.
     * - color3: small color swatch
     * - float: mini progress bar with value text
     * Also draws group separator lines.
     */
    OpenPBRSurfaceNode.prototype.onDrawForeground = function(ctx) {
        if (!this._editableParams) return;

        // Group separator lines
        ctx.strokeStyle = 'rgba(100,130,160,0.25)';
        ctx.lineWidth = 1;
        for (let g = 1; g < OPENPBR_GROUPS.length; g++) {
            const y = _slotY(OPENPBR_GROUPS[g].startIdx) - SLOT_H * 0.5;
            ctx.beginPath();
            ctx.moveTo(10, y);
            ctx.lineTo(this.size[0] - 10, y);
            ctx.stroke();
        }

        // Inline controls
        for (const [idx, param] of this._editableParams) {
            const r = _ctrlRect(this.size[0], idx);
            const val = this.properties[param.name];
            const isDef = isDefaultValue(val, param.def.default);

            if (param.def.type === 'color3') {
                const c = Array.isArray(val) ? val : [0, 0, 0];
                const cr = Math.floor(Math.max(0, Math.min(1, c[0])) * 255);
                const cg = Math.floor(Math.max(0, Math.min(1, c[1])) * 255);
                const cb = Math.floor(Math.max(0, Math.min(1, c[2])) * 255);
                ctx.fillStyle = `rgb(${cr},${cg},${cb})`;
                ctx.fillRect(r.x, r.y, r.w, r.h);
                ctx.strokeStyle = isDef ? '#556' : '#8af';
                ctx.lineWidth = isDef ? 0.5 : 1;
                ctx.strokeRect(r.x, r.y, r.w, r.h);
            } else {
                // Float: mini slider bar
                const range = param.def.max - param.def.min;
                const t = (range > 0 && typeof val === 'number')
                    ? (val - param.def.min) / range : 0;
                // Background
                ctx.fillStyle = isDef ? 'rgba(30,30,50,0.4)' : 'rgba(40,40,65,0.7)';
                ctx.fillRect(r.x, r.y, r.w, r.h);
                // Fill bar
                ctx.fillStyle = isDef ? 'rgba(80,100,130,0.3)' : 'rgba(90,140,200,0.55)';
                ctx.fillRect(r.x, r.y, r.w * Math.max(0, Math.min(1, t)), r.h);
                // Border
                ctx.strokeStyle = isDef ? '#445' : '#68a';
                ctx.lineWidth = isDef ? 0.5 : 1;
                ctx.strokeRect(r.x, r.y, r.w, r.h);
                // Value text
                ctx.fillStyle = isDef ? '#889' : '#cde';
                ctx.font = '9px monospace';
                ctx.textAlign = 'center';
                ctx.fillText(typeof val === 'number' ? val.toFixed(3) : '?',
                             r.x + r.w / 2, r.y + r.h - 3);
                ctx.textAlign = 'left';
            }
        }
    };

    /**
     * Handle mouse clicks on inline controls.
     * - color3 swatch: open native color picker
     * - float bar: set value based on click position (drag to scrub)
     */
    OpenPBRSurfaceNode.prototype.onMouseDown = function(event, localPos, graphCanvas) {
        if (!this._editableParams) return false;
        const x = localPos[0], y = localPos[1];

        for (const [idx, param] of this._editableParams) {
            const r = _ctrlRect(this.size[0], idx);
            if (x < r.x || x > r.x + r.w || y < r.y || y > r.y + r.h) continue;

            if (param.def.type === 'color3') {
                this._openColorPicker(param.name, event);
                return true;
            } else {
                // Float: set value from click position
                const t = Math.max(0, Math.min(1, (x - r.x) / r.w));
                const raw = param.def.min + t * (param.def.max - param.def.min);
                const step = param.def.step || 0.01;
                this.properties[param.name] = Math.round(raw / step) * step;
                this._dragState = { name: param.name, def: param.def, rx: r.x, rw: r.w };
                this.setDirtyCanvas(true);
                return true;
            }
        }
        return false;
    };

    OpenPBRSurfaceNode.prototype.onMouseMove = function(event, localPos, graphCanvas) {
        if (!this._dragState) return false;
        const { name, def, rx, rw } = this._dragState;
        const t = Math.max(0, Math.min(1, (localPos[0] - rx) / rw));
        const raw = def.min + t * (def.max - def.min);
        const step = def.step || 0.01;
        this.properties[name] = Math.round(raw / step) * step;
        this.setDirtyCanvas(true);
        return true;
    };

    OpenPBRSurfaceNode.prototype.onMouseUp = function(event, localPos, graphCanvas) {
        if (this._dragState) {
            this._dragState = null;
            if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            return true;
        }
        return false;
    };

    /** Double-click on a float bar opens a prompt for precise value entry. */
    OpenPBRSurfaceNode.prototype.onDblClick = function(event, localPos, graphCanvas) {
        if (!this._editableParams) return false;
        const x = localPos[0], y = localPos[1];

        for (const [idx, param] of this._editableParams) {
            const r = _ctrlRect(this.size[0], idx);
            if (x < r.x || x > r.x + r.w || y < r.y || y > r.y + r.h) continue;

            if (param.def.type === 'color3') {
                this._openColorPicker(param.name, event);
                return true;
            } else {
                const current = this.properties[param.name];
                const input = prompt(param.name + ' (' + param.def.min + ' - ' + param.def.max + '):',
                                     typeof current === 'number' ? current.toFixed(4) : '0');
                if (input !== null) {
                    const parsed = parseFloat(input);
                    if (!isNaN(parsed)) {
                        this.properties[param.name] = Math.max(param.def.min, Math.min(param.def.max, parsed));
                        this.setDirtyCanvas(true);
                        if (window._mtlxMarkDirty) window._mtlxMarkDirty();
                    }
                }
                return true;
            }
        }
        return false;
    };

    /** Open native color picker for a color3 property. */
    OpenPBRSurfaceNode.prototype._openColorPicker = function(paramName, event) {
        const self = this;
        const currentColor = this.properties[paramName] || [0, 0, 0];
        const input = document.createElement('input');
        input.type = 'color';
        input.value = rgbToHex(currentColor);
        input.style.position = 'fixed';
        input.style.left = ((event && event.clientX) || 200) + 'px';
        input.style.top = ((event && event.clientY) || 200) + 'px';
        input.style.opacity = '0';
        input.style.width = '1px';
        input.style.height = '1px';
        input.style.pointerEvents = 'none';
        document.body.appendChild(input);

        input.addEventListener('input', function() {
            self.properties[paramName] = hexToRGB(this.value);
            self.setDirtyCanvas(true);
        });
        input.addEventListener('change', function() {
            self.properties[paramName] = hexToRGB(this.value);
            self.setDirtyCanvas(true);
            if (window._mtlxMarkDirty) window._mtlxMarkDirty();
            setTimeout(function() { if (input.parentNode) input.parentNode.removeChild(input); }, 100);
        });
        input.addEventListener('blur', function() {
            setTimeout(function() { if (input.parentNode) input.parentNode.removeChild(input); }, 200);
        });
        setTimeout(function() { input.click(); }, 50);
    };

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

    // Normal map node (ND_normalmap) — converts tangent-space sample to perturbed normal
    function NormalMapNode() {
        this.addInput('in', 'vector3');
        this.addInput('scale', 'float');
        this.addInput('space', 'string');
        this.addOutput('out', 'vector3');
        this.color = '#445577';
        this.size = [130, 80];
    }
    NormalMapNode.title = 'NormalMap';
    LiteGraph.registerNodeType('mtlx/normalmap', NormalMapNode);

    // Height-to-normal node (ND_heighttonormal) — converts height/displacement to a normal vector
    function HeightToNormalNode() {
        this.addInput('in', 'float');
        this.addInput('scale', 'float');
        this.addOutput('out', 'vector3');
        this.color = '#445577';
        this.size = [130, 70];
    }
    HeightToNormalNode.title = 'HeightToNormal';
    LiteGraph.registerNodeType('mtlx/heighttonormal', HeightToNormalNode);

    // 3D fractal noise node (ND_fractal3d) — procedural noise in 3D space
    function Fractal3DNode() {
        this.addInput('amplitude', 'any');
        this.addInput('octaves', 'any');
        this.addInput('lacunarity', 'any');
        this.addInput('diminish', 'any');
        this.addInput('position', 'vector3');
        this.addOutput('out', 'any');
        this.color = '#446655';
        this.size = [130, 110];
    }
    Fractal3DNode.title = 'Fractal3D';
    LiteGraph.registerNodeType('mtlx/fractal3d', Fractal3DNode);

    // Rotate3D node (ND_rotate3d) — rotates a vector3 around an axis
    function Rotate3DNode() {
        this.addInput('in', 'vector3');
        this.addInput('amount', 'float');
        this.addInput('axis', 'vector3');
        this.addOutput('out', 'vector3');
        this.color = '#556644';
        this.size = [120, 80];
    }
    Rotate3DNode.title = 'Rotate3D';
    LiteGraph.registerNodeType('mtlx/rotate3d', Rotate3DNode);

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

    // History: capture pre-action snapshot on every pointerdown over the canvas
    canvas.addEventListener('pointerdown', historyCapturePre);

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

    // Clear undo/redo history when a new graph is loaded
    state.undoStack = [];
    state.redoStack = [];
    state._pendingHistorySnapshot = null;
    updateHistoryUI();

    if (!nodeGraphData || !nodeGraphData.nodegraph) {
        // Create a surface node with inline widgets for all parameters
        const surfaceNode = LiteGraph.createNode('mtlx/openpbr_surface');
        surfaceNode.pos = [200, 50];
        surfaceNode.title = materialName || 'OpenPBR Surface';
        state.graph.add(surfaceNode);

        // No connections — all inputs are inline
        const matData = state.materialData[state.currentMaterialIndex];
        const openPBR = matData ? flattenOpenPBR(matData.openPBR || {}) : {};
        const materialValues = buildMaterialValues(openPBR);
        surfaceNode._setupInlineWidgets(new Set(), materialValues);

        state._buildingGraph = false;
        state._hiddenNodeCount = 0;
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
    state._hiddenNodeCount = state.showAllNodes ? 0 : inactiveNodes.length;

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

        // Set all input slot types to wildcard (0) for programmatic graph construction.
        // MaterialX allows implicit type conversions (e.g. color4→color3) that
        // LiteGraph's strict type checking would reject.
        if (lgNode.inputs) {
            for (const input of lgNode.inputs) input.type = 0;
        }

        // Adapt output type from the MaterialX type suffix (e.g. image_color4 → color4)
        const rawCategory = node.category || node.type || '';
        const typeSuffixMatch = rawCategory.match(/_(color[34]|float|vector[234]|integer|boolean|string)$/);
        if (typeSuffixMatch && lgNode.outputs) {
            for (const output of lgNode.outputs) {
                output.type = typeSuffixMatch[1];
            }
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
        // Set properties (may affect node size via widgets)
        if (node.value !== undefined) {
            lgNode.properties = lgNode.properties || {};
            lgNode.properties.value = node.value;
            if (typeof lgNode._setupWidgets === 'function') {
                lgNode._setupWidgets();
            }
        }
        // Apply minimum height AFTER _setupWidgets() so it isn't overridden by computeSize().
        lgNode.size[1] = Math.max(lgNode.size[1], 55) + 14;

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
    // Set surface input types to wildcard for accepting any MaterialX type connection
    if (surfaceNode.inputs) {
        for (const input of surfaceNode.inputs) input.type = 0;
    }
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

    // Map shader parameter names to surface node input slots (matches addInput order)
    const surfaceInputMap = {
        // Base (0-2)
        'base_color': 0, 'baseColor': 0, 'diffuseColor': 0,
        'base_metalness': 1, 'metalness': 1,
        'base_diffuse_roughness': 2,
        // Specular (3-8)
        'specular_weight': 3,
        'specular_color': 4,
        'specular_roughness': 5, 'roughness': 5,
        'specular_ior': 6,
        'specular_anisotropy': 7,
        'specular_rotation': 8,
        // Transmission (9-14)
        'transmission_weight': 9,
        'transmission_color': 10,
        'transmission_depth': 11,
        'transmission_scatter': 12,
        'transmission_scatter_anisotropy': 13,
        'transmission_dispersion': 14,
        // Coat (15-22)
        'coat_weight': 15,
        'coat_color': 16,
        'coat_roughness': 17,
        'coat_ior': 18,
        'coat_anisotropy': 19,
        'coat_rotation': 20,
        'coat_affect_color': 21,
        'coat_affect_roughness': 22,
        // Fuzz (23-25)
        'fuzz_weight': 23,
        'fuzz_color': 24,
        'fuzz_roughness': 25,
        // Emission (26-27)
        'emission_color': 26,
        'emission_luminance': 27,
        // Geometry (28-32)
        'geometry_opacity': 28, 'opacity': 28,
        'geometry_normal': 29,
        'geometry_tangent': 30,
        'geometry_coat_normal': 31,
        'geometry_coat_tangent': 32,
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

    // Set up inline value widgets for unconnected surface inputs
    if (typeof surfaceNode._setupInlineWidgets === 'function') {
        // Get current material values from state
        const matData = state.materialData[state.currentMaterialIndex];
        const openPBR = matData ? flattenOpenPBR(matData.openPBR || {}) : {};
        const materialValues = buildMaterialValues(openPBR);
        surfaceNode._setupInlineWidgets(connectedSlots, materialValues);
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
    // Strip type suffixes including MaterialX variant tags (e.g. color3FA, vector3FA)
    return category.replace(/_(color[34]\w*|float|vector[234]\w*|integer|boolean|string)$/, '');
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

    const hiddenEl = document.getElementById('hidden-nodes-info');
    if (hiddenEl) {
        const hidden = state._hiddenNodeCount || 0;
        hiddenEl.textContent = hidden > 0 ? '| Hidden: ' + hidden : '';
    }
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
        for (let i = 0; i < surfaceNode.inputs.length; i++) {
            const inputSlot = surfaceNode.inputs[i];
            if (inputSlot.link != null) {
                const link = state.graph.links[inputSlot.link];
                if (link) {
                    const sourceId = link.origin_id;
                    const sourceName = idToName.get(sourceId);
                    if (sourceName) {
                        mtlxOutputs.push({
                            name: inputSlot.name,
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
    const params = buildMaterialValues(openPBR);

    // Override params with inline widget values from the surface node (for unconnected inputs)
    const surfaceNode = state.graph._nodes?.find(n => n.type === 'mtlx/openpbr_surface');
    if (surfaceNode && surfaceNode._inlineSlots) {
        // Use input slot names directly from the node
        for (const idx of surfaceNode._inlineSlots) {
            const slot = surfaceNode.inputs[idx];
            if (!slot) continue;
            const name = slot.name;
            if (name && surfaceNode.properties[name] !== undefined) {
                params[name] = surfaceNode.properties[name];
            }
        }
    }

    // Apply evaluated outputs
    const paramMap = {
        'base_color': 'base_color', 'baseColor': 'base_color', 'diffuseColor': 'base_color',
        'roughness': 'specular_roughness', 'specular_roughness': 'specular_roughness',
        'metalness': 'base_metalness', 'base_metalness': 'base_metalness',
        'specular_ior': 'specular_ior', 'specular_weight': 'specular_weight',
        'specular_color': 'specular_color', 'specular_anisotropy': 'specular_anisotropy',
        'specular_rotation': 'specular_rotation',
        'transmission_weight': 'transmission_weight', 'transmission_color': 'transmission_color',
        'transmission_depth': 'transmission_depth', 'transmission_scatter': 'transmission_scatter',
        'transmission_scatter_anisotropy': 'transmission_scatter_anisotropy',
        'transmission_dispersion': 'transmission_dispersion',
        'coat_weight': 'coat_weight', 'coat_color': 'coat_color', 'coat_roughness': 'coat_roughness',
        'coat_ior': 'coat_ior', 'coat_anisotropy': 'coat_anisotropy', 'coat_rotation': 'coat_rotation',
        'coat_affect_color': 'coat_affect_color', 'coat_affect_roughness': 'coat_affect_roughness',
        'fuzz_weight': 'fuzz_weight', 'fuzz_color': 'fuzz_color', 'fuzz_roughness': 'fuzz_roughness',
        'emission_color': 'emission_color', 'emission_luminance': 'emission_luminance',
        'geometry_opacity': 'geometry_opacity', 'opacity': 'geometry_opacity',
    };
    for (const [name, value] of Object.entries(outputs)) {
        if (value === undefined) continue;
        if (processor.needsShader(value)) continue; // skip texture-dependent values

        const paramName = paramMap[name] || name;
        if (params.hasOwnProperty(paramName)) {
            params[paramName] = value;
        }
    }

    // Helper to apply a color3 param to a Three.js Color property
    const applyColor3 = (threeColor, paramVal) => {
        if (Array.isArray(paramVal)) {
            threeColor.setRGB(paramVal[0], paramVal[1], paramVal[2]);
        } else if (typeof paramVal === 'number') {
            threeColor.setRGB(paramVal, paramVal, paramVal);
        }
    };

    // Apply to material (skip color/scalar overrides if a texture map is bound)
    // --- Base ---
    if (!material.map) applyColor3(material.color, params.base_color);
    if (!material.metalnessMap) material.metalness = params.base_metalness;
    if (!material.roughnessMap) material.roughness = params.specular_roughness;

    // --- Specular ---
    material.specularIntensity = params.specular_weight;
    applyColor3(material.specularColor, params.specular_color);
    material.ior = params.specular_ior;
    material.anisotropy = params.specular_anisotropy;
    material.anisotropyRotation = params.specular_rotation * Math.PI * 2;

    // --- Transmission ---
    material.transmission = params.transmission_weight;
    applyColor3(material.attenuationColor, params.transmission_color);
    material.attenuationDistance = params.transmission_depth > 0 ? params.transmission_depth : Infinity;
    material.dispersion = params.transmission_dispersion;

    // --- Coat ---
    material.clearcoat = params.coat_weight;
    material.clearcoatRoughness = params.coat_roughness;

    // --- Emission ---
    if (!material.emissiveMap) applyColor3(material.emissive, params.emission_color);
    material.emissiveIntensity = params.emission_luminance;

    // --- Geometry opacity ---
    if (params.geometry_opacity !== undefined) {
        material.opacity = params.geometry_opacity;
        // When transmission is active, Three.js handles transparency internally
        if (params.transmission_weight > 0) {
            material.transparent = false;
        } else {
            material.transparent = params.geometry_opacity < 1.0;
        }
    }

    material.needsUpdate = true;
    requestRender();

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
        historyCommit();
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
                    historyCommit();
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
// Undo / Redo History
// ============================================================================

const HISTORY_MAX = 20;

/**
 * Take a pre-action snapshot. Called on pointerdown over the graph canvas so
 * we always have the state just before the user's next change.
 */
function historyCapturePre() {
    if (state._buildingGraph) return;
    state._pendingHistorySnapshot = JSON.stringify(state.graph.serialize());
}

/**
 * Commit the pending pre-action snapshot to the undo stack if the graph has
 * actually changed since we captured it. Called from every change hook.
 */
function historyCommit() {
    if (state._buildingGraph) return;
    if (state._pendingHistorySnapshot === null) return;
    const current = JSON.stringify(state.graph.serialize());
    if (current !== state._pendingHistorySnapshot) {
        state.undoStack.push(state._pendingHistorySnapshot);
        if (state.undoStack.length > HISTORY_MAX) state.undoStack.shift();
        state.redoStack = [];
        state._pendingHistorySnapshot = null;
        updateHistoryUI();
    }
}

function historyUndo() {
    if (!state.undoStack.length) return;
    state.redoStack.push(JSON.stringify(state.graph.serialize()));
    restoreHistorySnapshot(JSON.parse(state.undoStack.pop()));
    updateHistoryUI();
}

function historyRedo() {
    if (!state.redoStack.length) return;
    state.undoStack.push(JSON.stringify(state.graph.serialize()));
    restoreHistorySnapshot(JSON.parse(state.redoStack.pop()));
    updateHistoryUI();
}

function restoreHistorySnapshot(snapshot) {
    state._buildingGraph = true;
    state.graph.configure(snapshot);
    state._buildingGraph = false;
    state._pendingHistorySnapshot = null;
    state.graphDirty = false;
    document.getElementById('edit-indicator').classList.remove('visible');
    updateNodeStats();
    updateHistoryUI();
    if (state.interactiveUpdate) {
        evaluateAndApplyMaterial();
    }
}

function updateHistoryUI() {
    const btnUndo = document.getElementById('btn-undo');
    const btnRedo = document.getElementById('btn-redo');
    const u = state.undoStack.length;
    const r = state.redoStack.length;
    if (btnUndo) {
        btnUndo.disabled = u === 0;
        btnUndo.title = u > 0 ? `Undo (${u}) – Ctrl+Z` : 'Nothing to undo';
    }
    if (btnRedo) {
        btnRedo.disabled = r === 0;
        btnRedo.title = r > 0 ? `Redo (${r}) – Ctrl+Y` : 'Nothing to redo';
    }
}

window.historyUndo = historyUndo;
window.historyRedo = historyRedo;

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
 * Set both envMap (PMREM) and envMapSource (equirectangular) from a result object.
 */
function setEnvFromResult(result) {
    state.envMap = result.envMap;
    state.envMapSource = result.source;
}

/**
 * Generate a high-resolution PMREM from a texture using fromScene().
 * The default fromEquirectangular() derives cube size from the input texture
 * (e.g. 1024px HDR → cube 256), which causes visible LOD discontinuities
 * at low roughness values due to the mip clamp at CUBEUV_MAX_MIP.
 * Using fromScene() with a larger size (512) adds an extra LOD level,
 * pushing the discontinuity from roughness ~0.054 to ~0.038.
 *
 * @param {THREE.Texture} texture - Source texture (equirectangular or cubemap)
 * @param {number} [size=512] - PMREM cube face resolution
 * @returns {THREE.Texture} PMREM-processed environment map texture
 */
function generateHighResPMREM(texture, minCubeSize = 512) {
    // Override PMREMGenerator's internal _setSize to enforce a minimum cube size.
    // By default, fromEquirectangular() sets cubeSize = texture.width / 4
    // (e.g. 1024px HDR → cube 256). With cube 256, CUBEUV_MAX_MIP = 8,
    // and the mip clamp at the max creates a visible LOD discontinuity
    // at roughness ~0.054. Forcing cube 512 gives CUBEUV_MAX_MIP = 9,
    // pushing the discontinuity down to roughness ~0.038.
    const pmrem = state.pmremGenerator;

    // Force recreation of internal GGX shader material. PMREMGenerator caches
    // _ggxMaterial with CUBEUV_MAX_MIP baked into shader defines. When cube size
    // changes, the stale material has wrong defines (Three.js _dispose() disposes
    // but doesn't null the reference, so the null-check in _applyGGXFilter skips
    // recreation). We must null it to force a fresh shader with correct defines.
    if (pmrem._ggxMaterial) {
        pmrem._ggxMaterial.dispose();
        pmrem._ggxMaterial = null;
    }

    const origSetSize = pmrem._setSize.bind(pmrem);
    pmrem._setSize = function(cubeSize) {
        origSetSize(Math.max(cubeSize, minCubeSize));
    };
    const isCube = texture.isCubeTexture;
    const envMap = isCube
        ? pmrem.fromCubemap(texture).texture
        : pmrem.fromEquirectangular(texture).texture;
    pmrem._setSize = origSetSize;
    return envMap;
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

    if (path.startsWith('hdr:')) {
        const url = path.substring('hdr:'.length);
        try {
            const loader = new HDRLoader();
            const texture = await loader.loadAsync(url);
            texture.mapping = THREE.EquirectangularReflectionMapping;
            // Use high-res PMREM (cube 512) to avoid LOD discontinuities at low roughness
            const envMap = generateHighResPMREM(texture, 512);
            state.envMap = envMap;
            state.envMapSource = texture;
            applyEnvironment();
        } catch (e) {
            console.error('Failed to load HDR:', url, e);
            showToast('Failed to load HDR environment');
            setEnvFromResult(createStudioEnvironment());
            applyEnvironment();
        }
        return;
    }

    if (path.startsWith('cube:')) {
        const dir = path.substring('cube:'.length);
        try {
            const loader = new THREE.CubeTextureLoader();
            const faces = ['px.png', 'nx.png', 'py.png', 'ny.png', 'pz.png', 'nz.png'];
            const cubeTexture = await loader.setPath(dir).loadAsync(faces);
            // Use high-res PMREM (cube 512) to avoid LOD discontinuities at low roughness
            const envMap = generateHighResPMREM(cubeTexture, 512);
            state.envMap = envMap;
            // CubeTexture can serve as background directly
            state.envMapSource = cubeTexture;
            applyEnvironment();
        } catch (e) {
            console.error('Failed to load cubemap:', dir, e);
            showToast('Failed to load cubemap environment');
            setEnvFromResult(createStudioEnvironment());
            applyEnvironment();
        }
        return;
    }
}

/**
 * Apply the current envMap to the scene and all materials.
 * Uses PMREM texture for scene.environment (IBL reflections) and
 * equirectangular source for scene.background (visible panorama).
 */
function applyEnvironment() {
    const activeEnvMap = state.envEnabled ? state.envMap : null;
    const activeSource = state.envEnabled ? state.envMapSource : null;

    // scene.environment drives IBL for all MeshStandardMaterial/MeshPhysicalMaterial
    state.scene.environment = activeEnvMap;

    // Use equirectangular source texture for background (shows gradient/color clearly)
    // PMREM (cubeUV) textures look flattened as backgrounds; source textures are better
    if (state.showBackground && activeSource) {
        state.scene.background = activeSource;
    } else {
        state.scene.background = new THREE.Color(0x1a1a2e);
    }

    // Set envMap explicitly on ALL materials (critical for reflections to update)
    const intensity = state.envEnabled ? state.envIntensity : 0;
    state.materials.forEach(mat => {
        mat.envMap = activeEnvMap;
        mat.envMapIntensity = intensity;
        mat.needsUpdate = true;
    });

    // Also traverse scene for any materials not tracked in state.materials
    state.scene.traverse(obj => {
        if (obj.isMesh && obj.material && !state.materials.includes(obj.material)) {
            obj.material.envMap = activeEnvMap;
            obj.material.envMapIntensity = intensity;
            obj.material.needsUpdate = true;
        }
    });

    // Update renderer tone mapping
    const tmValue = TONE_MAPPINGS[state.toneMapping];
    if (tmValue !== undefined) {
        state.renderer.toneMapping = tmValue;
    }
    state.renderer.toneMappingExposure = state.exposure;
    requestRender();
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
    requestRender();
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
 * Build a complete materialValues object from flattened openPBR data.
 * Extracts all editable OpenPBR parameters with fallback to DEFAULT_OPENPBR_PARAMS.
 * @param {Object} openPBR - Flattened openPBR object
 * @returns {Object} materialValues keyed by parameter name
 */
function buildMaterialValues(openPBR) {
    const vals = {};
    for (const key of Object.keys(DEFAULT_OPENPBR_PARAMS)) {
        const def = DEFAULT_OPENPBR_PARAMS[key];
        if (typeof def === 'boolean') continue; // skip geometry_thin_walled
        vals[key] = extractOpenPBRValue(openPBR[key], def);
    }
    return vals;
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

    // Build node lookup
    const nodeMap = new Map();
    for (const node of nodes) nodeMap.set(node.name, node);

    // Find image/tiledimage nodes
    const imageNodes = new Map(); // nodeName -> { filename, colorspace }
    for (const node of nodes) {
        const cat = (node.category || '').replace(/_(color3|color4|float|vector2|vector3|vector4)$/, '');
        if (cat === 'image' || cat === 'tiledimage') {
            const fileInput = (node.inputs || []).find(i => i.name === 'file');
            if (fileInput && fileInput.value) {
                imageNodes.set(node.name, {
                    filename: fileInput.value,
                    colorspace: fileInput.colorspace || '',
                });
            }
        }
    }

    if (imageNodes.size === 0) return result;

    // Build a reverse-dependency map: nodeName -> [nodenames it takes input from]
    const dependsOn = new Map();
    for (const node of nodes) {
        for (const input of (node.inputs || [])) {
            if (input.nodename) {
                if (!dependsOn.has(node.name)) dependsOn.set(node.name, []);
                dependsOn.get(node.name).push(input.nodename);
            }
        }
    }

    // Trace from a node back to the image node, collecting the chain of operations
    function traceToImageNode(nodeName, chain = [], visited = new Set()) {
        if (visited.has(nodeName)) return null;
        visited.add(nodeName);
        if (imageNodes.has(nodeName)) return { imageNode: nodeName, ops: chain };
        const deps = dependsOn.get(nodeName) || [];
        for (const dep of deps) {
            const node = nodeMap.get(nodeName);
            const cat = (node?.category || '').replace(/_(color3|color4|float|vector2|vector3|vector4)$/, '');
            // Skip pass-through nodes in the ops chain.
            // normalmap/heighttonormal transform the channel but the underlying image file is still the one to load.
            const isPassthrough = (cat === 'convert' || cat === 'texcoord' || cat === 'normalmap' || cat === 'heighttonormal');
            const newChain = isPassthrough ? chain : [...chain, { name: nodeName, category: cat, node }];
            const found = traceToImageNode(dep, newChain, visited);
            if (found) return found;
        }
        return null;
    }

    // Map outputs to shader params via connections (one output can connect to multiple params)
    const outputToParams = new Map(); // outputName -> [paramName, ...]
    for (const conn of connections) {
        if (conn.input && conn.output) {
            if (!outputToParams.has(conn.output)) outputToParams.set(conn.output, []);
            outputToParams.get(conn.output).push(conn.input);
        }
    }

    // For each output, trace to image node and collect operations
    for (const output of outputs) {
        if (!output.nodename) continue;
        const trace = traceToImageNode(output.nodename);
        if (!trace) continue;

        const paramNames = outputToParams.get(output.name) || [];
        const imgInfo = imageNodes.get(trace.imageNode);
        for (const paramName of paramNames) {
            result[paramName] = {
                filename: imgInfo.filename,
                colorspace: imgInfo.colorspace,
                ops: [...trace.ops],  // clone - chain of operations from output to image (reverse order)
            };
        }
    }

    return result;
}

/**
 * Colorspace classification helpers.
 * Determines transfer function (linear vs sRGB gamma) and gamut (primaries).
 */
const LINEAR_COLORSPACES = new Set([
    'lin_rec709', 'lin_displayp3', 'lin_rec2020', 'lin_ap1', 'lin_ap0',
    'acescg',   // ACEScg = linear AP1
    'aces2065-1', 'lin_adobergb',
    'scene_linear', 'raw',
]);

function isLinearColorspace(cs) {
    if (!cs) return false;
    const lower = cs.toLowerCase();
    return lower.startsWith('lin_') || LINEAR_COLORSPACES.has(lower);
}


/**
 * Apply node graph operations to texture pixel data (CPU-side baking).
 * Modifies the texture's image data in-place.
 */
function bakeTextureOps(texture, ops) {
    if (!ops || ops.length === 0) return;

    // Get image data from the texture
    const image = texture.image;
    if (!image) return;

    const canvas = document.createElement('canvas');
    canvas.width = image.width;
    canvas.height = image.height;
    const ctx = canvas.getContext('2d');
    ctx.drawImage(image, 0, 0);
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const data = imageData.data; // Uint8ClampedArray RGBA

    // Process each operation in reverse order (ops are collected output→image,
    // so reversed = image→output order)
    for (const op of ops.reverse()) {
        const cat = op.category;
        if (cat === 'power') {
            // Get the power exponent from in2 input
            const in2Input = (op.node?.inputs || []).find(i => i.name === 'in2');
            const exponent = in2Input?.value;
            if (!exponent) continue;

            const expR = Array.isArray(exponent) ? exponent[0] : exponent;
            const expG = Array.isArray(exponent) ? exponent[1] : exponent;
            const expB = Array.isArray(exponent) ? exponent[2] : exponent;

            for (let i = 0; i < data.length; i += 4) {
                // Convert to 0-1, apply power, convert back
                data[i]     = Math.round(Math.pow(data[i] / 255, expR) * 255);
                data[i + 1] = Math.round(Math.pow(data[i + 1] / 255, expG) * 255);
                data[i + 2] = Math.round(Math.pow(data[i + 2] / 255, expB) * 255);
                // Alpha unchanged
            }
        } else if (cat === 'multiply') {
            const in2Input = (op.node?.inputs || []).find(i => i.name === 'in2');
            const factor = in2Input?.value;
            if (!factor) continue;

            const fR = Array.isArray(factor) ? factor[0] : factor;
            const fG = Array.isArray(factor) ? factor[1] : factor;
            const fB = Array.isArray(factor) ? factor[2] : factor;

            for (let i = 0; i < data.length; i += 4) {
                data[i]     = Math.min(255, Math.round((data[i] / 255) * fR * 255));
                data[i + 1] = Math.min(255, Math.round((data[i + 1] / 255) * fG * 255));
                data[i + 2] = Math.min(255, Math.round((data[i + 2] / 255) * fB * 255));
            }
        } else if (cat === 'add') {
            const in2Input = (op.node?.inputs || []).find(i => i.name === 'in2');
            const offset = in2Input?.value;
            if (!offset) continue;

            const oR = Array.isArray(offset) ? offset[0] : offset;
            const oG = Array.isArray(offset) ? offset[1] : offset;
            const oB = Array.isArray(offset) ? offset[2] : offset;

            for (let i = 0; i < data.length; i += 4) {
                data[i]     = Math.min(255, Math.max(0, Math.round(data[i] + oR * 255)));
                data[i + 1] = Math.min(255, Math.max(0, Math.round(data[i + 1] + oG * 255)));
                data[i + 2] = Math.min(255, Math.max(0, Math.round(data[i + 2] + oB * 255)));
            }
        } else if (cat === 'invert') {
            for (let i = 0; i < data.length; i += 4) {
                data[i]     = 255 - data[i];
                data[i + 1] = 255 - data[i + 1];
                data[i + 2] = 255 - data[i + 2];
            }
        }
        // Other ops: convert, texcoord etc. are pass-through, already filtered
    }

    ctx.putImageData(imageData, 0, 0);

    // Replace texture image with the baked canvas
    texture.image = canvas;
    texture.needsUpdate = true;
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
        const bakedTexKeys = new Set(); // track which textures already had ops baked
        const colorspaceApplied = new Set(); // track colorspace corrections
        for (const [paramName, texInfo] of Object.entries(ngTextures)) {
            const filename = typeof texInfo === 'string' ? texInfo : texInfo.filename;
            const ops = typeof texInfo === 'object' ? texInfo.ops : [];
            const colorspace = typeof texInfo === 'object' ? (texInfo.colorspace || '') : '';

            // Determine Three.js colorSpace from MaterialX colorspace metadata
            const isLinear = isLinearColorspace(colorspace)
                || paramName === 'normal' || paramName === 'geometry_normal';
            const threeColorSpace = isLinear ? THREE.LinearSRGBColorSpace : THREE.SRGBColorSpace;

            // Map shader param names to texture keys
            const paramToTexKey = {
                'base_color': 'base_color',
                'specular_roughness': 'specular_roughness',
                'base_metalness': 'base_metalness',
                'emission_color': 'emission_color',
                'coat_weight': 'coat_weight',
                'transmission_color': 'base_color',    // transmission color often shares base texture
                'subsurface_color': 'base_color',      // subsurface color often shares base texture
                'geometry_normal': 'normal',            // normal map connected via node graph
                'normal': 'normal',
            };
            const texKey = paramToTexKey[paramName];
            if (!texKey) continue;

            // If texture already loaded, apply colorspace correction and ops
            if (textures[texKey]) {
                // Apply colorspace from node graph (overrides default sRGB assumption)
                if (colorspace && !colorspaceApplied.has(texKey)) {
                    textures[texKey].colorSpace = threeColorSpace;
                    textures[texKey].needsUpdate = true;
                    colorspaceApplied.add(texKey);
                }
                // Bake ops once per texKey
                if (ops && ops.length > 0 && !bakedTexKeys.has(texKey)) {
                    bakeTextureOps(textures[texKey], ops);
                    bakedTexKeys.add(texKey);
                }
                continue;
            }

            try {
                const texId = findTextureIdByFilename(nativeLoader, filename);
                if (texId >= 0) {
                    const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(nativeLoader, texId);
                    if (texture) {
                        texture.colorSpace = threeColorSpace;
                        // Apply node graph operations (power, multiply, etc.) to texture
                        if (ops && ops.length > 0) {
                            bakeTextureOps(texture, ops);
                        }
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

    // Reset shading mode when loading a new scene (old materials are being replaced)
    state.originalMaterialsMap.clear();
    state.shadingMode = 'solid';
    const shadingSelect = document.getElementById('shading-mode-select');
    if (shadingSelect) shadingSelect.value = 'solid';

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

        const params = buildMaterialValues(openPBR);

        // Process node graph for constant values
        if (nodeGraph) {
            const processor = new MtlxNodeGraphProcessor();
            const outputs = processor.processGraph(nodeGraph);

            for (const [name, value] of Object.entries(outputs)) {
                if (value !== undefined && !processor.needsShader(value)) {
                    const paramMap = {
                        'base_color': 'base_color', 'baseColor': 'base_color', 'diffuseColor': 'base_color',
                        'roughness': 'specular_roughness', 'metalness': 'base_metalness',
                        'opacity': 'geometry_opacity',
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

    // Pre-compute bounding spheres so Three.js doesn't do it lazily on the
    // first render frame (which would cause a visible spike).
    state.scene.traverse(obj => {
        if (obj.isMesh && obj.geometry && !obj.geometry.boundingSphere) {
            obj.geometry.computeBoundingSphere();
        }
    });

    // Pre-compile all WebGL shader programs upfront so the first rendered
    // frame doesn't stall on shader compilation.
    state.renderer.compile(state.scene, state.camera);

    requestRender();
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
    const ngBar = document.getElementById('ng-material-bar');
    const ngSelect = document.getElementById('ng-material-select');
    const ngCount = document.getElementById('ng-material-count');

    // Build options for both selects
    const fragment = () => {
        const frag = document.createDocumentFragment();
        for (let i = 0; i < state.materialData.length; i++) {
            const mat = state.materialData[i];
            const option = document.createElement('option');
            option.value = i;
            option.textContent = mat.name || `Material ${i}`;
            frag.appendChild(option);
        }
        return frag;
    };

    select.innerHTML = '';
    select.appendChild(fragment());
    select.onchange = (e) => selectMaterial(parseInt(e.target.value));

    ngSelect.innerHTML = '';
    ngSelect.appendChild(fragment());
    // onchange is handled inline via HTML attribute

    const count = state.materialData.length;
    if (ngCount) ngCount.textContent = count > 0 ? `(${count})` : '';

    selector.style.display = count > 0 ? 'block' : 'none';
    if (ngBar) ngBar.classList.toggle('visible', count > 0);
}

function selectMaterial(index) {
    if (index < 0 || index >= state.materialData.length) return;

    state.currentMaterialIndex = index;

    // Sync both selector UIs
    const viewerSelect = document.getElementById('material-select');
    if (viewerSelect && parseInt(viewerSelect.value) !== index) viewerSelect.value = index;
    const ngSelect = document.getElementById('ng-material-select');
    if (ngSelect && parseInt(ngSelect.value) !== index) ngSelect.value = index;

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
    const select = document.getElementById('blender-sample-select');
    const filename = select ? select.value : 'texture_channel_blender.usdz';
    showLoading(true);
    updateStatus('Loading ' + filename + '...');

    try {
        const response = await fetch('assets/' + filename);
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.status}`);
        }

        const arrayBuffer = await response.arrayBuffer();

        state.usdData = await new Promise((resolve, reject) => {
            state.loader.parse(arrayBuffer, filename, resolve, reject);
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
        showToast(`Loaded ${filename} (${numMaterials} materials)`);

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
    historyCommit();
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
    applyEnvironment();
};

window.changeToneMapping = function(mode) {
    state.toneMapping = mode;
    applyEnvironment();
};

window.toggleBackground = function(show) {
    state.showBackground = show;
    applyEnvironment();
};

window.toggleLightingPanel = function() {
    const panel = document.getElementById('lighting-controls');
    panel.classList.toggle('collapsed');
};

// ============================================================================
// Shading Panel
// ============================================================================

/**
 * Create a shader material that visualizes world-space normals as RGB colors.
 * If normalMap is provided, the normal map is applied via TBN before coloring.
 */
function createAbsNormalMaterial(normalMap = null, normalScale = new THREE.Vector2(1, 1)) {
    return new THREE.ShaderMaterial({
        uniforms: {
            normalMap: { value: normalMap },
            normalScale: { value: normalScale },
            useNormalMap: { value: normalMap !== null }
        },
        vertexShader: `
            varying vec3 vWorldNormal;
            varying vec2 vUv;
            varying vec3 vTangent;
            varying vec3 vBitangent;

            #ifdef USE_TANGENT
                attribute vec4 tangent;
            #endif

            void main() {
                mat3 worldNormalMatrix = mat3(modelMatrix);
                vWorldNormal = normalize(worldNormalMatrix * normal);
                vUv = uv;

                #ifdef USE_TANGENT
                    vTangent = normalize(worldNormalMatrix * tangent.xyz);
                    vBitangent = normalize(cross(vWorldNormal, vTangent) * tangent.w);
                #else
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

            varying vec3 vWorldNormal;
            varying vec2 vUv;
            varying vec3 vTangent;
            varying vec3 vBitangent;

            void main() {
                vec3 n = normalize(vWorldNormal);

                if (useNormalMap) {
                    vec3 mapN = texture2D(normalMap, vUv).xyz * 2.0 - 1.0;
                    mapN.xy *= normalScale;
                    mapN = normalize(mapN);
                    vec3 T = normalize(vTangent);
                    vec3 B = normalize(vBitangent);
                    mat3 TBN = mat3(T, B, n);
                    n = normalize(TBN * mapN);
                }

                gl_FragColor = vec4(n * 0.5 + 0.5, 1.0);
            }
        `,
        side: THREE.DoubleSide
    });
}

/**
 * Extract normal map and scale from a Three.js material.
 * Handles direct property, userData.textures, and array materials.
 */
function extractNormalMapFromMaterial(material) {
    if (!material) return { normalMap: null, normalScale: new THREE.Vector2(1, 1) };

    // Handle array of materials
    if (Array.isArray(material)) {
        for (const mat of material) {
            const result = extractNormalMapFromMaterial(mat);
            if (result.normalMap) return result;
        }
        return { normalMap: null, normalScale: new THREE.Vector2(1, 1) };
    }

    // Direct normalMap property (MeshPhysicalMaterial, MeshStandardMaterial)
    if (material.normalMap) {
        return {
            normalMap: material.normalMap,
            normalScale: material.normalScale ? material.normalScale.clone() : new THREE.Vector2(1, 1)
        };
    }

    // userData.textures (async-loaded textures)
    if (material.userData?.textures?.normalMap) {
        return {
            normalMap: material.userData.textures.normalMap,
            normalScale: new THREE.Vector2(1, 1)
        };
    }

    return { normalMap: null, normalScale: new THREE.Vector2(1, 1) };
}

/**
 * Apply a shading debug mode to the current model.
 * Modes: 'solid' | 'normal_mapped' | 'normal_geometry'
 */
function setShadingMode(mode) {
    state.shadingMode = mode;

    // Update the select element to match (for calls from JS)
    const select = document.getElementById('shading-mode-select');
    if (select && select.value !== mode) select.value = mode;

    if (!state.currentModel) return;

    if (mode === 'solid') {
        // Restore original materials
        state.currentModel.traverse((obj) => {
            if (!obj.isMesh) return;
            const orig = state.originalMaterialsMap.get(obj);
            if (orig !== undefined) {
                obj.material = orig;
            }
        });
        state.originalMaterialsMap.clear();
    } else {
        // Save original materials before first non-solid switch
        state.currentModel.traverse((obj) => {
            if (!obj.isMesh) return;
            if (!state.originalMaterialsMap.has(obj)) {
                state.originalMaterialsMap.set(obj, obj.material);
            }
        });

        state.currentModel.traverse((obj) => {
            if (!obj.isMesh) return;
            const origMat = state.originalMaterialsMap.get(obj) ?? obj.material;
            let normalMap = null;
            let normalScale = new THREE.Vector2(1, 1);

            if (mode === 'normal_mapped') {
                const extracted = extractNormalMapFromMaterial(origMat);
                normalMap = extracted.normalMap;
                normalScale = extracted.normalScale;
            }

            obj.material = createAbsNormalMaterial(normalMap, normalScale);
        });
    }
    requestRender();
}

window.toggleShadingPanel = function() {
    const panel = document.getElementById('shading-controls');
    panel.classList.toggle('collapsed');
};

window.setShadingMode = setShadingMode;

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
    requestRender();

    // Select material and sync UI
    if (matIndex >= 0) {
        selectMaterial(matIndex);
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
        requestRender();
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

/**
 * Mark the 3D view as needing a redraw (demand rendering).
 * Call this whenever the scene, camera, or materials change.
 */
function requestRender() {
    state.renderNeeded = true;
}

function animate() {
    requestAnimationFrame(animate);

    // OrbitControls.update() returns true while camera is still moving/damping.
    // Only call renderer.render() when actually needed to save CPU/GPU.
    const controlsActive = state.controls.update();
    if (state.renderNeeded || controlsActive) {
        state.renderer.render(state.scene, state.camera);
        state.renderNeeded = false;
    }

    // LiteGraph: respect dirty flags rather than forcing a full canvas redraw
    // every frame. draw(false, false) only redraws when nodes/links have changed.
    if (state.graphCanvas) {
        state.graphCanvas.draw(false, false);
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

    // Keyboard shortcuts for node graph fitting and undo/redo
    document.addEventListener('keydown', (e) => {
        // Undo/redo: allow even when focus is on the canvas (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z)
        if (e.ctrlKey || e.metaKey) {
            if (e.key === 'z' && !e.shiftKey) {
                e.preventDefault();
                historyUndo();
                return;
            }
            if (e.key === 'y' || (e.key === 'z' && e.shiftKey)) {
                e.preventDefault();
                historyRedo();
                return;
            }
        }

        // Ignore fitting shortcuts if focus is in an input/select/textarea
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
