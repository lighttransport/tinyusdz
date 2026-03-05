/**
 * TinyUSDZ OpenPBR WebGL2 Material
 *
 * MaterialX node graph support for WebGL2 using Three.js MeshPhysicalMaterial.
 * This is the WebGL2 counterpart to TinyUSDZOpenPBR_TSL.js (which targets WebGPU).
 *
 * Features:
 * 1. OpenPBR parameter mapping to MeshPhysicalMaterial
 * 2. MaterialX node graph evaluation (compile-time node folding)
 * 3. Blender shader node support via MaterialX export
 * 4. Custom GLSL shader injection via onBeforeCompile
 * 5. Texture node support with proper colorspace handling
 */

import * as THREE from 'three';

// ============================================================================
// Constants
// ============================================================================

const PI = Math.PI;
const INV_PI = 1.0 / PI;

// ============================================================================
// MaterialX Node Evaluator for WebGL2
// ============================================================================

/**
 * Evaluates MaterialX node operations at material creation time.
 * For constant values, this pre-computes the result.
 * For texture-dependent operations, generates GLSL code.
 */
export const MtlxNodeEval = {
    // ========== Basic Math Operations ==========
    add: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return addValues(a, b);
        }
        return { op: 'add', in1: a, in2: b };
    },

    subtract: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return subtractValues(a, b);
        }
        return { op: 'subtract', in1: a, in2: b };
    },

    multiply: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return multiplyValues(a, b);
        }
        return { op: 'multiply', in1: a, in2: b };
    },

    divide: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return divideValues(a, b);
        }
        return { op: 'divide', in1: a, in2: b };
    },

    power: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return powerValues(a, b);
        }
        return { op: 'power', in1: a, in2: b };
    },

    // ========== Unary Operations ==========
    absval: (a) => {
        if (isConstant(a)) {
            return absValue(a);
        }
        return { op: 'absval', in: a };
    },

    negate: (a) => {
        if (isConstant(a)) {
            return negateValue(a);
        }
        return { op: 'negate', in: a };
    },

    sqrt: (a) => {
        if (isConstant(a)) {
            return sqrtValue(a);
        }
        return { op: 'sqrt', in: a };
    },

    floor: (a) => {
        if (isConstant(a)) {
            return floorValue(a);
        }
        return { op: 'floor', in: a };
    },

    ceil: (a) => {
        if (isConstant(a)) {
            return ceilValue(a);
        }
        return { op: 'ceil', in: a };
    },

    round: (a) => {
        if (isConstant(a)) {
            return roundValue(a);
        }
        return { op: 'round', in: a };
    },

    fract: (a) => {
        if (isConstant(a)) {
            return fractValue(a);
        }
        return { op: 'fract', in: a };
    },

    sign: (a) => {
        if (isConstant(a)) {
            return signValue(a);
        }
        return { op: 'sign', in: a };
    },

    sin: (a) => {
        if (isConstant(a)) {
            return sinValue(a);
        }
        return { op: 'sin', in: a };
    },

    cos: (a) => {
        if (isConstant(a)) {
            return cosValue(a);
        }
        return { op: 'cos', in: a };
    },

    exp: (a) => {
        if (isConstant(a)) {
            return expValue(a);
        }
        return { op: 'exp', in: a };
    },

    ln: (a) => {
        if (isConstant(a)) {
            return lnValue(a);
        }
        return { op: 'ln', in: a };
    },

    inversesqrt: (a) => {
        if (isConstant(a)) {
            return inversesqrtValue(a);
        }
        return { op: 'inversesqrt', in: a };
    },

    // ========== Comparison and Clamping ==========
    min: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return minValues(a, b);
        }
        return { op: 'min', in1: a, in2: b };
    },

    max: (a, b) => {
        if (isConstant(a) && isConstant(b)) {
            return maxValues(a, b);
        }
        return { op: 'max', in1: a, in2: b };
    },

    clamp: (val, minVal, maxVal) => {
        if (isConstant(val) && isConstant(minVal) && isConstant(maxVal)) {
            return clampValue(val, minVal, maxVal);
        }
        return { op: 'clamp', in: val, low: minVal, high: maxVal };
    },

    saturate: (a) => {
        return MtlxNodeEval.clamp(a, 0, 1);
    },

    // ========== Interpolation ==========
    mix: (a, b, t) => {
        if (isConstant(a) && isConstant(b) && isConstant(t)) {
            return mixValues(a, b, t);
        }
        return { op: 'mix', bg: a, fg: b, mix: t };
    },

    smoothstep: (edge0, edge1, x) => {
        if (isConstant(edge0) && isConstant(edge1) && isConstant(x)) {
            return smoothstepValue(edge0, edge1, x);
        }
        return { op: 'smoothstep', low: edge0, high: edge1, in: x };
    },

    step: (edge, x) => {
        if (isConstant(edge) && isConstant(x)) {
            return stepValue(edge, x);
        }
        return { op: 'step', edge: edge, in: x };
    },

    // ========== Vector Operations ==========
    normalize: (v) => {
        if (isConstant(v) && Array.isArray(v)) {
            return normalizeVector(v);
        }
        return { op: 'normalize', in: v };
    },

    length: (v) => {
        if (isConstant(v) && Array.isArray(v)) {
            return vectorLength(v);
        }
        return { op: 'length', in: v };
    },

    distance: (a, b) => {
        if (isConstant(a) && isConstant(b) && Array.isArray(a) && Array.isArray(b)) {
            return vectorLength(subtractValues(a, b));
        }
        return { op: 'distance', in1: a, in2: b };
    },

    dot: (a, b) => {
        if (isConstant(a) && isConstant(b) && Array.isArray(a) && Array.isArray(b)) {
            return dotProduct(a, b);
        }
        return { op: 'dot', in1: a, in2: b };
    },

    cross: (a, b) => {
        if (isConstant(a) && isConstant(b) && Array.isArray(a) && Array.isArray(b)) {
            return crossProduct(a, b);
        }
        return { op: 'cross', in1: a, in2: b };
    },

    reflect: (i, n) => {
        return { op: 'reflect', in1: i, in2: n };
    },

    // Rotate a 3D vector about an arbitrary axis by an angle in degrees.
    // Matches MaterialX GLSL mx_rotationMatrix (Rodrigues' formula, row-major convention).
    // USD/MaterialX stores matrices row-major; Three.js uses column-major, so the
    // constant-evaluated result is just an array — no matrix storage order issue.
    rotate3d: (v, amount, axis) => {
        if (isConstant(v) && isConstant(amount) && isConstant(axis)
            && Array.isArray(v) && Array.isArray(axis)) {
            return rotate3dVector(v, toScalar(amount), axis);
        }
        return { op: 'rotate3d', in: v, amount: amount, axis: axis };
    },

    // ========== Color Operations ==========
    luminance: (c) => {
        if (isConstant(c) && Array.isArray(c)) {
            // ITU-R BT.709 luminance
            return c[0] * 0.2126 + c[1] * 0.7152 + c[2] * 0.0722;
        }
        return { op: 'luminance', in: c };
    },

    rgbtohsv: (rgb) => {
        if (isConstant(rgb) && Array.isArray(rgb)) {
            return rgbToHsv(rgb);
        }
        return { op: 'rgbtohsv', in: rgb };
    },

    hsvtorgb: (hsv) => {
        if (isConstant(hsv) && Array.isArray(hsv)) {
            return hsvToRgb(hsv);
        }
        return { op: 'hsvtorgb', in: hsv };
    },

    hsvadjust: (color, amount) => {
        if (isConstant(color) && isConstant(amount)) {
            // Convert to HSV, adjust, convert back
            const hsv = rgbToHsv(color);
            const adjusted = [
                (hsv[0] + amount[0]) % 1.0,
                Math.max(0, Math.min(1, hsv[1] * amount[1])),
                Math.max(0, Math.min(1, hsv[2] * amount[2]))
            ];
            return hsvToRgb(adjusted);
        }
        return { op: 'hsvadjust', in: color, amount: amount };
    },

    contrast: (input, amount, pivot) => {
        if (isConstant(input) && isConstant(amount) && isConstant(pivot)) {
            return addValues(
                multiplyValues(subtractValues(input, pivot), amount),
                pivot
            );
        }
        return { op: 'contrast', in: input, amount: amount, pivot: pivot };
    },

    // Blender-style invert (1 - x, optionally mixed)
    invert: (color, fac = 1.0) => {
        if (isConstant(color) && isConstant(fac)) {
            const inverted = subtractValues([1, 1, 1], color);
            return mixValues(color, inverted, fac);
        }
        return { op: 'invert', in: color, fac: fac };
    },

    // ========== Remap ==========
    remap: (input, inLow, inHigh, outLow, outHigh) => {
        if (isConstant(input) && isConstant(inLow) && isConstant(inHigh) &&
            isConstant(outLow) && isConstant(outHigh)) {
            const t = divideValues(
                subtractValues(input, inLow),
                subtractValues(inHigh, inLow)
            );
            return addValues(
                multiplyValues(t, subtractValues(outHigh, outLow)),
                outLow
            );
        }
        return { op: 'remap', in: input, inlow: inLow, inhigh: inHigh, outlow: outLow, outhigh: outHigh };
    },

    // ========== Combine/Extract ==========
    combine2: (x, y) => {
        if (isConstant(x) && isConstant(y)) {
            return [toScalar(x), toScalar(y)];
        }
        return { op: 'combine2', in1: x, in2: y };
    },

    combine3: (x, y, z) => {
        if (isConstant(x) && isConstant(y) && isConstant(z)) {
            return [toScalar(x), toScalar(y), toScalar(z)];
        }
        return { op: 'combine3', in1: x, in2: y, in3: z };
    },

    combine4: (x, y, z, w) => {
        if (isConstant(x) && isConstant(y) && isConstant(z) && isConstant(w)) {
            return [toScalar(x), toScalar(y), toScalar(z), toScalar(w)];
        }
        return { op: 'combine4', in1: x, in2: y, in3: z, in4: w };
    },

    extract: (v, index) => {
        if (isConstant(v) && Array.isArray(v)) {
            return v[index] ?? 0;
        }
        return { op: 'extract', in: v, index: index };
    },

    // Swizzle helpers
    swizzle_x: (v) => MtlxNodeEval.extract(v, 0),
    swizzle_y: (v) => MtlxNodeEval.extract(v, 1),
    swizzle_z: (v) => MtlxNodeEval.extract(v, 2),
    swizzle_w: (v) => MtlxNodeEval.extract(v, 3),

    // ========== Texture Operations ==========
    // These always return deferred operations (need shader)
    image: (file, texcoord, defaultValue) => {
        return { op: 'image', file: file, texcoord: texcoord, default: defaultValue };
    },

    tiledimage: (file, texcoord, uvtiling, uvoffset, defaultValue) => {
        return {
            op: 'tiledimage',
            file: file,
            texcoord: texcoord,
            uvtiling: uvtiling,
            uvoffset: uvoffset,
            default: defaultValue
        };
    },

    texcoord: (index = 0) => {
        return { op: 'texcoord', index: index };
    },

    // ========== Geometry ==========
    position: (space = 'object') => {
        return { op: 'position', space: space };
    },

    normal: (space = 'object') => {
        return { op: 'normal', space: space };
    },

    tangent: (space = 'object') => {
        return { op: 'tangent', space: space };
    },

    // ========== Conditionals ==========
    ifgreater: (value1, value2, inTrue, inFalse) => {
        if (isConstant(value1) && isConstant(value2)) {
            return toScalar(value1) > toScalar(value2) ? inTrue : inFalse;
        }
        return { op: 'ifgreater', value1: value1, value2: value2, in1: inTrue, in2: inFalse };
    },

    ifequal: (value1, value2, inTrue, inFalse) => {
        if (isConstant(value1) && isConstant(value2)) {
            return Math.abs(toScalar(value1) - toScalar(value2)) < 1e-6 ? inTrue : inFalse;
        }
        return { op: 'ifequal', value1: value1, value2: value2, in1: inTrue, in2: inFalse };
    }
};

// ============================================================================
// Helper Functions for Constant Evaluation
// ============================================================================

function isConstant(v) {
    if (v === null || v === undefined) return false;
    if (typeof v === 'number') return true;
    if (Array.isArray(v)) return v.every(x => typeof x === 'number');
    if (typeof v === 'object' && v.op) return false; // Deferred operation
    return false;
}

function toScalar(v) {
    if (typeof v === 'number') return v;
    if (Array.isArray(v)) return v[0];
    return 0;
}

function toArray(v, size = 3) {
    if (Array.isArray(v)) return v;
    if (typeof v === 'number') return Array(size).fill(v);
    return Array(size).fill(0);
}

function mapComponents(a, b, fn) {
    const aa = toArray(a);
    const bb = toArray(b, aa.length);
    return aa.map((v, i) => fn(v, bb[i]));
}

function mapSingle(a, fn) {
    if (typeof a === 'number') return fn(a);
    if (Array.isArray(a)) return a.map(fn);
    return 0;
}

function addValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return a + b;
    return mapComponents(a, b, (x, y) => x + y);
}

function subtractValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return a - b;
    return mapComponents(a, b, (x, y) => x - y);
}

function multiplyValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return a * b;
    return mapComponents(a, b, (x, y) => x * y);
}

function divideValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return b !== 0 ? a / b : 0;
    return mapComponents(a, b, (x, y) => y !== 0 ? x / y : 0);
}

function powerValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return Math.pow(a, b);
    return mapComponents(a, b, (x, y) => Math.pow(x, y));
}

function absValue(a) { return mapSingle(a, Math.abs); }
function negateValue(a) { return mapSingle(a, x => -x); }
function sqrtValue(a) { return mapSingle(a, x => Math.sqrt(Math.max(0, x))); }
function floorValue(a) { return mapSingle(a, Math.floor); }
function ceilValue(a) { return mapSingle(a, Math.ceil); }
function roundValue(a) { return mapSingle(a, Math.round); }
function fractValue(a) { return mapSingle(a, x => x - Math.floor(x)); }
function signValue(a) { return mapSingle(a, Math.sign); }
function sinValue(a) { return mapSingle(a, Math.sin); }
function cosValue(a) { return mapSingle(a, Math.cos); }
function expValue(a) { return mapSingle(a, Math.exp); }
function lnValue(a) { return mapSingle(a, x => Math.log(Math.max(1e-10, x))); }
function inversesqrtValue(a) { return mapSingle(a, x => 1.0 / Math.sqrt(Math.max(1e-10, x))); }

function minValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return Math.min(a, b);
    return mapComponents(a, b, Math.min);
}

function maxValues(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return Math.max(a, b);
    return mapComponents(a, b, Math.max);
}

function clampValue(val, minVal, maxVal) {
    return mapSingle(val, x => Math.max(toScalar(minVal), Math.min(toScalar(maxVal), x)));
}

function mixValues(a, b, t) {
    const tVal = toScalar(t);
    if (typeof a === 'number' && typeof b === 'number') {
        return a * (1 - tVal) + b * tVal;
    }
    const aa = toArray(a);
    const bb = toArray(b, aa.length);
    return aa.map((v, i) => v * (1 - tVal) + bb[i] * tVal);
}

function smoothstepValue(edge0, edge1, x) {
    const e0 = toScalar(edge0);
    const e1 = toScalar(edge1);
    const xVal = toScalar(x);
    const t = Math.max(0, Math.min(1, (xVal - e0) / (e1 - e0)));
    return t * t * (3 - 2 * t);
}

function stepValue(edge, x) {
    return toScalar(x) < toScalar(edge) ? 0 : 1;
}

function normalizeVector(v) {
    const len = vectorLength(v);
    return len > 1e-10 ? v.map(x => x / len) : v.map(() => 0);
}

function vectorLength(v) {
    return Math.sqrt(v.reduce((sum, x) => sum + x * x, 0));
}

function dotProduct(a, b) {
    return a.reduce((sum, x, i) => sum + x * (b[i] || 0), 0);
}

function crossProduct(a, b) {
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    ];
}

// Rotate vector v around unit axis by angleDeg degrees.
// Matches MaterialX GLSL mx_rotationMatrix exactly:
//   mat4 columns in GLSL → row-major rotation matrix in math.
// USD stores matrices row-major; Three.js column-major — but here we
// multiply explicitly so storage order doesn't matter.
function rotate3dVector(v, angleDeg, axis) {
    const ax = normalizeVector(axis);
    const rad = angleDeg * Math.PI / 180;
    const s = Math.sin(rad);
    const c = Math.cos(rad);
    const oc = 1 - c;

    // Build the same 3×3 rotation as mx_rotationMatrix.
    // GLSL column 0 = math row 0 when applied via M*v.
    // Row i of the effective matrix (from MaterialX GLSL source):
    //   row0: (oc*ax*ax+c,    oc*ax*ay+az*s,  oc*az*ax-ay*s)
    //   row1: (oc*ax*ay-az*s, oc*ay*ay+c,     oc*ay*az+ax*s)
    //   row2: (oc*az*ax+ay*s, oc*ay*az-ax*s,  oc*az*az+c)
    const [x, y, z] = ax;
    const r00 = oc * x * x + c,       r01 = oc * x * y + z * s, r02 = oc * z * x - y * s;
    const r10 = oc * x * y - z * s,   r11 = oc * y * y + c,     r12 = oc * y * z + x * s;
    const r20 = oc * z * x + y * s,   r21 = oc * y * z - x * s, r22 = oc * z * z + c;

    return [
        r00 * v[0] + r01 * v[1] + r02 * v[2],
        r10 * v[0] + r11 * v[1] + r12 * v[2],
        r20 * v[0] + r21 * v[1] + r22 * v[2]
    ];
}

function rgbToHsv(rgb) {
    const [r, g, b] = rgb;
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const d = max - min;

    let h = 0;
    const s = max === 0 ? 0 : d / max;
    const v = max;

    if (d !== 0) {
        if (max === r) h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
        else if (max === g) h = ((b - r) / d + 2) / 6;
        else h = ((r - g) / d + 4) / 6;
    }

    return [h, s, v];
}

function hsvToRgb(hsv) {
    const [h, s, v] = hsv;
    const i = Math.floor(h * 6);
    const f = h * 6 - i;
    const p = v * (1 - s);
    const q = v * (1 - f * s);
    const t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: return [v, t, p];
        case 1: return [q, v, p];
        case 2: return [p, v, t];
        case 3: return [p, q, v];
        case 4: return [t, p, v];
        case 5: return [v, p, q];
        default: return [v, v, v];
    }
}

// ============================================================================
// OpenPBR Default Parameters
// ============================================================================

export const DEFAULT_OPENPBR_PARAMS = {
    // Base layer
    base_weight: 1.0,
    base_color: [0.8, 0.8, 0.8],
    base_metalness: 0.0,
    base_diffuse_roughness: 0.0,

    // Specular layer
    specular_weight: 1.0,
    specular_color: [1.0, 1.0, 1.0],
    specular_roughness: 0.3,
    specular_ior: 1.5,
    specular_anisotropy: 0.0,
    specular_rotation: 0.0,

    // Transmission layer
    transmission_weight: 0.0,
    transmission_color: [1.0, 1.0, 1.0],
    transmission_depth: 0.0,
    transmission_scatter: [0.0, 0.0, 0.0],
    transmission_scatter_anisotropy: 0.0,
    transmission_dispersion: 0.0,

    // Subsurface layer
    subsurface_weight: 0.0,
    subsurface_color: [0.8, 0.8, 0.8],
    subsurface_radius: [1.0, 0.5, 0.25],
    subsurface_scale: 1.0,
    subsurface_anisotropy: 0.0,

    // Coat layer
    coat_weight: 0.0,
    coat_color: [1.0, 1.0, 1.0],
    coat_roughness: 0.0,
    coat_ior: 1.5,
    coat_anisotropy: 0.0,
    coat_rotation: 0.0,
    coat_affect_color: 0.0,
    coat_affect_roughness: 0.0,

    // Sheen layer
    sheen_weight: 0.0,
    sheen_color: [1.0, 1.0, 1.0],
    sheen_roughness: 0.3,

    // Fuzz layer
    fuzz_weight: 0.0,
    fuzz_color: [1.0, 1.0, 1.0],
    fuzz_roughness: 0.5,

    // Thin film
    thin_film_weight: 0.0,
    thin_film_thickness: 500.0,
    thin_film_ior: 1.5,

    // Emission
    emission_color: [0.0, 0.0, 0.0],
    emission_luminance: 0.0,

    // Geometry
    geometry_opacity: 1.0,
    geometry_thin_walled: false
};

// ============================================================================
// MaterialX Node Graph Processor for WebGL2
// ============================================================================

/**
 * Processes MaterialX node graphs and generates material properties
 * or GLSL shader code for WebGL2.
 */
export class MtlxNodeGraphProcessor {
    constructor() {
        this.nodeCache = new Map();
        this.textureRefs = new Map();
        this.uniformRefs = new Map();
        this.glslCode = [];
    }

    /**
     * Reset the processor state
     */
    clear() {
        this.nodeCache.clear();
        this.textureRefs.clear();
        this.uniformRefs.clear();
        this.glslCode = [];
    }

    /**
     * Process a MaterialX node
     * @param {Object} nodeData - Node definition
     * @param {Object} inputs - Resolved input values
     * @returns {*} Evaluated result or deferred operation
     */
    processNode(nodeData, inputs = {}) {
        const rawCategory = nodeData.category || nodeData.type || nodeData.nodeType;
        // Strip type suffixes like _color3, _float, _vector3 to get base category.
        // Also handle MaterialX variant tags like _color3FA (e.g. fractal3d_color3FA).
        const category = rawCategory?.replace(/_(color[34]\w*|float|vector[234]\w*|integer|boolean|string)$/, '') || '';

        // Map category to MtlxNodeEval function
        switch (category) {
            case 'constant':
                return this._processConstant(nodeData, inputs);

            case 'add':
                return MtlxNodeEval.add(inputs.in1, inputs.in2);

            case 'subtract':
                return MtlxNodeEval.subtract(inputs.in1, inputs.in2);

            case 'multiply':
                return MtlxNodeEval.multiply(inputs.in1, inputs.in2);

            case 'divide':
                return MtlxNodeEval.divide(inputs.in1, inputs.in2);

            case 'power':
                return MtlxNodeEval.power(inputs.in1, inputs.in2);

            case 'absval':
                return MtlxNodeEval.absval(inputs.in);

            case 'sign':
                return MtlxNodeEval.sign(inputs.in);

            case 'floor':
                return MtlxNodeEval.floor(inputs.in);

            case 'ceil':
                return MtlxNodeEval.ceil(inputs.in);

            case 'round':
                return MtlxNodeEval.round(inputs.in);

            case 'fract':
            case 'fractional':
                return MtlxNodeEval.fract(inputs.in);

            case 'sqrt':
                return MtlxNodeEval.sqrt(inputs.in);

            case 'inversesqrt':
                return MtlxNodeEval.inversesqrt(inputs.in);

            case 'negate':
                return MtlxNodeEval.negate(inputs.in);

            case 'exp':
                return MtlxNodeEval.exp(inputs.in);

            case 'ln':
                return MtlxNodeEval.ln(inputs.in);

            case 'sin':
                return MtlxNodeEval.sin(inputs.in);

            case 'cos':
                return MtlxNodeEval.cos(inputs.in);

            case 'min':
                return MtlxNodeEval.min(inputs.in1, inputs.in2);

            case 'max':
                return MtlxNodeEval.max(inputs.in1, inputs.in2);

            case 'clamp':
                return MtlxNodeEval.clamp(
                    inputs.in,
                    inputs.low ?? 0,
                    inputs.high ?? 1
                );

            case 'saturate':
                return MtlxNodeEval.saturate(inputs.in);

            case 'mix':
                return MtlxNodeEval.mix(inputs.bg, inputs.fg, inputs.mix);

            case 'smoothstep':
                return MtlxNodeEval.smoothstep(inputs.low, inputs.high, inputs.in);

            case 'step':
                return MtlxNodeEval.step(inputs.edge, inputs.in);

            case 'normalize':
                return MtlxNodeEval.normalize(inputs.in);

            case 'magnitude':
            case 'length':
                return MtlxNodeEval.length(inputs.in);

            case 'distance':
                return MtlxNodeEval.distance(inputs.in1, inputs.in2);

            case 'dotproduct':
            case 'dot':
                return MtlxNodeEval.dot(inputs.in1, inputs.in2);

            case 'crossproduct':
            case 'cross':
                return MtlxNodeEval.cross(inputs.in1, inputs.in2);

            case 'reflect':
                return MtlxNodeEval.reflect(inputs.in, inputs.normal);

            case 'rotate3d':
                return MtlxNodeEval.rotate3d(
                    inputs.in ?? [0, 0, 0],
                    inputs.amount ?? 0,
                    inputs.axis ?? [0, 1, 0]
                );

            case 'luminance':
                return MtlxNodeEval.luminance(inputs.in);

            case 'rgbtohsv':
                return MtlxNodeEval.rgbtohsv(inputs.in);

            case 'hsvtorgb':
                return MtlxNodeEval.hsvtorgb(inputs.in);

            case 'hsvadjust':
                return MtlxNodeEval.hsvadjust(inputs.in, inputs.amount);

            case 'contrast':
                return MtlxNodeEval.contrast(
                    inputs.in,
                    inputs.amount ?? 1,
                    inputs.pivot ?? 0.5
                );

            case 'invert':
                return MtlxNodeEval.invert(inputs.in, inputs.amount ?? 1);

            case 'remap':
                return MtlxNodeEval.remap(
                    inputs.in,
                    inputs.inlow ?? 0,
                    inputs.inhigh ?? 1,
                    inputs.outlow ?? 0,
                    inputs.outhigh ?? 1
                );

            case 'combine2':
                return MtlxNodeEval.combine2(inputs.in1, inputs.in2);

            case 'combine3':
                return MtlxNodeEval.combine3(inputs.in1, inputs.in2, inputs.in3);

            case 'combine4':
                return MtlxNodeEval.combine4(inputs.in1, inputs.in2, inputs.in3, inputs.in4);

            case 'extract':
            case 'separate':
            case 'separate3':
                return MtlxNodeEval.extract(inputs.in, nodeData.index ?? inputs.index ?? 0);

            case 'image':
                return MtlxNodeEval.image(
                    nodeData.file || inputs.file,
                    inputs.texcoord,
                    nodeData.default || inputs.default
                );

            case 'tiledimage':
                return MtlxNodeEval.tiledimage(
                    nodeData.file || inputs.file,
                    inputs.texcoord,
                    inputs.uvtiling ?? [1, 1],
                    inputs.uvoffset ?? [0, 0],
                    nodeData.default || inputs.default
                );

            case 'texcoord':
                return MtlxNodeEval.texcoord(nodeData.index ?? 0);

            case 'position':
                return MtlxNodeEval.position(nodeData.space ?? 'object');

            case 'normal':
                return MtlxNodeEval.normal(nodeData.space ?? 'object');

            case 'tangent':
                return MtlxNodeEval.tangent(nodeData.space ?? 'object');

            case 'ifgreater':
                return MtlxNodeEval.ifgreater(inputs.value1, inputs.value2, inputs.in1, inputs.in2);

            case 'ifequal':
                return MtlxNodeEval.ifequal(inputs.value1, inputs.value2, inputs.in1, inputs.in2);

            // Type conversion (color3↔vector3 pass-through in evaluation)
            case 'convert':
                return inputs.in;

            // Blender-specific nodes
            case 'gamma':
                return MtlxNodeEval.power(inputs.in, inputs.gamma ?? 1.0);

            case 'brightness':
                return MtlxNodeEval.add(inputs.in, inputs.amount ?? 0);

            case 'swizzle':
                return inputs.in; // Swizzle is handled via separate/combine chains

            // Colorspace conversion nodes (standard MaterialX)
            case 'srgb_to_linear':
                return this._srgbToLinear(inputs.in);

            case 'linear_to_srgb':
                return this._linearToSrgb(inputs.in);

            // Normal map nodes — pass through the input so shader dependency propagates upward
            case 'normalmap':
                return inputs.in ?? [0, 0, 1];

            case 'heighttonormal':
                return inputs.in ?? [0, 0, 1];

            // Procedural noise — position-dependent, always needs a shader
            // Return an op descriptor so needsShader() can detect the dependency chain
            case 'fractal3d':
                return { op: 'fractal3d', position: inputs.position };

            default:
                console.warn(`Unknown MaterialX node category: ${category}`);
                return inputs.in ?? inputs.in1 ?? 0;
        }
    }

    /**
     * Process constant node
     */
    _processConstant(nodeData, inputs = {}) {
        // Prefer resolved inputs (from processGraph), fall back to nodeData.value
        const value = (inputs && inputs.value !== undefined) ? inputs.value : nodeData.value;
        const valueType = nodeData.type || nodeData.valueType || 'float';

        if (Array.isArray(value)) {
            return value.slice(); // Return copy
        }
        return value;
    }

    /**
     * Get value from node input definition
     */
    _getInputValue(inputDef) {
        if (inputDef === null || inputDef === undefined) {
            return undefined;
        }

        // Direct value
        if (typeof inputDef === 'number' || Array.isArray(inputDef)) {
            return inputDef;
        }

        // Object with value property
        if (typeof inputDef === 'object') {
            if (inputDef.value !== undefined) {
                return inputDef.value;
            }
            if (inputDef.nodename) {
                return this.nodeCache.get(inputDef.nodename);
            }
            if (inputDef.connection) {
                return this.nodeCache.get(inputDef.connection);
            }
        }

        return undefined;
    }

    /**
     * Process a complete MaterialX node graph
     * @param {Object} graphData - Node graph data
     * @param {Object} textures - Map of texture names to THREE.Texture
     * @returns {Object} Output values mapped by output name
     */
    processGraph(graphData, textures = {}) {
        this.clear();

        const nodegraph = graphData.nodegraph || graphData;
        const nodes = nodegraph.nodes || [];
        const outputs = nodegraph.outputs || [];

        // Build dependency order (simple topological processing)
        // Assumes nodes are already in reasonable order or we make multiple passes
        const maxPasses = 10;
        let pass = 0;
        let processed = new Set();

        while (processed.size < nodes.length && pass < maxPasses) {
            for (const node of nodes) {
                const nodeName = node.name || node.id;
                if (processed.has(nodeName)) continue;

                // Check if all input dependencies are resolved
                let canProcess = true;
                const inputs = {};

                if (node.inputs) {
                    for (const input of node.inputs) {
                        const inputName = input.name;

                        if (input.nodename) {
                            // Connection to another node
                            if (!this.nodeCache.has(input.nodename)) {
                                canProcess = false;
                                break;
                            }
                            inputs[inputName] = this.nodeCache.get(input.nodename);
                        } else if (input.value !== undefined) {
                            // Direct value
                            inputs[inputName] = input.value;
                        }

                        // Handle colorspace conversion if specified
                        if (input.colorspace && inputs[inputName]) {
                            inputs[inputName] = this._applyColorspace(
                                inputs[inputName],
                                input.colorspace
                            );
                        }
                    }
                }

                if (!canProcess) continue;

                // Process the node
                const result = this.processNode(node, inputs);
                this.nodeCache.set(nodeName, result);
                processed.add(nodeName);
            }
            pass++;
        }

        // Extract outputs
        const result = {};
        for (const output of outputs) {
            const outputName = output.name;
            if (output.nodename && this.nodeCache.has(output.nodename)) {
                result[outputName] = this.nodeCache.get(output.nodename);
            }
        }

        return result;
    }

    /**
     * Apply colorspace conversion
     */
    _applyColorspace(value, colorspace) {
        if (!isConstant(value)) {
            return { op: 'colorspace', in: value, colorspace: colorspace };
        }

        // For constant values, apply conversion
        if (colorspace === 'srgb_texture' || colorspace === 'sRGB') {
            // sRGB to linear
            return this._srgbToLinear(value);
        } else if (colorspace === 'lin_rec709' || colorspace === 'linear') {
            // Already linear
            return value;
        }

        return value;
    }

    _srgbToLinear(color) {
        if (typeof color === 'number') {
            return color <= 0.04045
                ? color / 12.92
                : Math.pow((color + 0.055) / 1.055, 2.4);
        }
        if (Array.isArray(color)) {
            return color.map(c => this._srgbToLinear(c));
        }
        return color;
    }

    _linearToSrgb(color) {
        if (typeof color === 'number') {
            return color <= 0.0031308
                ? color * 12.92
                : 1.055 * Math.pow(color, 1.0 / 2.4) - 0.055;
        }
        if (Array.isArray(color)) {
            return color.map(c => this._linearToSrgb(c));
        }
        return color;
    }

    /**
     * Check if the evaluated result needs shader code
     * (i.e., contains texture or geometry references)
     */
    needsShader(value) {
        if (!value || typeof value !== 'object') return false;
        if (value.op) {
            const shaderOps = ['image', 'tiledimage', 'texcoord', 'position', 'normal', 'tangent', 'colorspace', 'fractal3d'];
            if (shaderOps.includes(value.op)) return true;
            // Check nested operations
            for (const key of Object.keys(value)) {
                if (key !== 'op' && this.needsShader(value[key])) return true;
            }
        }
        return false;
    }
}

// ============================================================================
// OpenPBR Material Factory for WebGL2
// ============================================================================

/**
 * Create an OpenPBR-compatible MeshPhysicalMaterial for WebGL2
 * @param {Object} params - OpenPBR parameters
 * @param {Object} textures - Map of texture names to THREE.Texture
 * @returns {THREE.MeshPhysicalMaterial}
 */
export function createOpenPBRMaterial(params = {}, textures = {}) {
    const material = new THREE.MeshPhysicalMaterial();
    const geometryParams = (params.geometry && typeof params.geometry === 'object') ? params.geometry : null;

    // Flag for type checking
    material.isOpenPBRMaterial = true;

    // Store OpenPBR-specific values
    material._openPBR = {
        base_weight: params.base_weight ?? DEFAULT_OPENPBR_PARAMS.base_weight,
        base_diffuse_roughness: params.base_diffuse_roughness ?? DEFAULT_OPENPBR_PARAMS.base_diffuse_roughness,
        specular_weight: params.specular_weight ?? DEFAULT_OPENPBR_PARAMS.specular_weight,
        thin_film_thickness: params.thin_film_thickness ?? DEFAULT_OPENPBR_PARAMS.thin_film_thickness,
    };

    // Apply base color
    const baseColor = params.base_color ?? DEFAULT_OPENPBR_PARAMS.base_color;
    material.color = toThreeColor(baseColor);

    // Apply metalness
    material.metalness = params.base_metalness ?? DEFAULT_OPENPBR_PARAMS.base_metalness;

    // Apply roughness
    material.roughness = params.specular_roughness ?? DEFAULT_OPENPBR_PARAMS.specular_roughness;

    // Apply IOR
    material.ior = params.specular_ior ?? DEFAULT_OPENPBR_PARAMS.specular_ior;

    // Apply clearcoat
    material.clearcoat = params.coat_weight ?? DEFAULT_OPENPBR_PARAMS.coat_weight;
    material.clearcoatRoughness = params.coat_roughness ?? DEFAULT_OPENPBR_PARAMS.coat_roughness;

    // Apply sheen
    material.sheen = params.sheen_weight ?? DEFAULT_OPENPBR_PARAMS.sheen_weight;
    material.sheenRoughness = params.sheen_roughness ?? DEFAULT_OPENPBR_PARAMS.sheen_roughness;
    material.sheenColor = toThreeColor(params.sheen_color ?? DEFAULT_OPENPBR_PARAMS.sheen_color);

    // Apply iridescence
    material.iridescence = params.thin_film_weight ?? DEFAULT_OPENPBR_PARAMS.thin_film_weight;
    material.iridescenceIOR = params.thin_film_ior ?? DEFAULT_OPENPBR_PARAMS.thin_film_ior;
    material.iridescenceThicknessRange = [0, params.thin_film_thickness ?? DEFAULT_OPENPBR_PARAMS.thin_film_thickness];

    // Apply transmission
    material.transmission = params.transmission_weight ?? DEFAULT_OPENPBR_PARAMS.transmission_weight;

    // Apply emission
    material.emissive = toThreeColor(params.emission_color ?? DEFAULT_OPENPBR_PARAMS.emission_color);
    material.emissiveIntensity = params.emission_luminance ?? DEFAULT_OPENPBR_PARAMS.emission_luminance;

    // Apply opacity
    const geometryOpacity = params.geometry_opacity ?? geometryParams?.geometry_opacity ?? geometryParams?.opacity;
    if (geometryOpacity !== undefined) {
        material.opacity = geometryOpacity;
        material.transparent = geometryOpacity < 1.0;
    }

    // Apply normal map strength if authored.
    const normalMapScale = params.normal_map_scale ?? geometryParams?.normal_map_scale;
    if (typeof normalMapScale === 'number' && Number.isFinite(normalMapScale)) {
        material.normalScale = new THREE.Vector2(normalMapScale, normalMapScale);
    }

    // Apply textures
    if (textures.base_color) {
        material.map = textures.base_color;
    }
    if (textures.normal || textures.geometry_normal) {
        material.normalMap = textures.normal || textures.geometry_normal;
    }
    if (textures.specular_roughness) {
        material.roughnessMap = textures.specular_roughness;
    }
    if (textures.base_metalness) {
        material.metalnessMap = textures.base_metalness;
    }
    if (textures.emission_color) {
        material.emissiveMap = textures.emission_color;
    }
    if (textures.geometry_opacity) {
        material.alphaMap = textures.geometry_opacity;
        material.transparent = true;
    }
    if (textures.coat_weight) {
        material.clearcoatMap = textures.coat_weight;
    }
    if (textures.coat_roughness) {
        material.clearcoatRoughnessMap = textures.coat_roughness;
    }

    return material;
}

/**
 * Convert value to THREE.Color
 */
function toThreeColor(value) {
    if (value instanceof THREE.Color) return value;
    if (Array.isArray(value)) {
        return new THREE.Color(value[0], value[1], value[2]);
    }
    if (typeof value === 'number') {
        return new THREE.Color(value, value, value);
    }
    return new THREE.Color(0.5, 0.5, 0.5);
}

// ============================================================================
// MaterialX to WebGL2 Material Converter
// ============================================================================

/**
 * Convert MaterialX node graph to WebGL2 MeshPhysicalMaterial
 */
export class MtlxMaterialConverter {
    constructor() {
        this.processor = new MtlxNodeGraphProcessor();
        this.textureLoader = new THREE.TextureLoader();
    }

    /**
     * Create material from MaterialX data
     * @param {Object} materialData - Full material data with OpenPBR and nodeGraph
     * @param {Object} options - Conversion options
     * @returns {THREE.MeshPhysicalMaterial}
     */
    createMaterial(materialData, options = {}) {
        const { textureBasePath = '', loadTextures = true } = options;

        // Extract OpenPBR parameters
        const openPBR = materialData.openPBR || {};
        const nodeGraph = openPBR.nodeGraph || materialData.nodeGraph;
        const params = { ...openPBR };
        const textures = {};

        // Process node graph if present
        if (nodeGraph) {
            const graphOutputs = this.processor.processGraph(nodeGraph);

            // Map outputs to material parameters
            this._mapOutputsToParams(graphOutputs, params, textures, {
                textureBasePath,
                loadTextures
            });
        }

        // Create material
        const material = createOpenPBRMaterial(params, textures);
        material.name = materialData.name || 'OpenPBRMaterial';

        // Handle shader customization for texture-dependent operations
        if (Object.keys(textures).length > 0) {
            this._setupShaderCustomization(material, params, textures);
        }

        return material;
    }

    /**
     * Map node graph outputs to OpenPBR parameters
     */
    _mapOutputsToParams(outputs, params, textures, options) {
        // Common output name mappings
        const outputMappings = {
            // Base color
            'base_color': 'base_color',
            'baseColor': 'base_color',
            'diffuseColor': 'base_color',

            // Roughness
            'specular_roughness': 'specular_roughness',
            'roughness': 'specular_roughness',

            // Metalness
            'base_metalness': 'base_metalness',
            'metalness': 'base_metalness',
            'metallic': 'base_metalness',

            // Normal
            'normal': 'normal',
            'normalMap': 'normal',
            'geometry_normal': 'normal',

            // Emission
            'emission_color': 'emission_color',
            'emissive': 'emission_color',

            // Opacity
            'geometry_opacity': 'geometry_opacity',
            'opacity': 'geometry_opacity',
            'alpha': 'geometry_opacity',

            // Coat
            'coat_weight': 'coat_weight',
            'clearcoat': 'coat_weight',
            'coat_roughness': 'coat_roughness',
            'clearcoatRoughness': 'coat_roughness'
        };

        for (const [outputName, value] of Object.entries(outputs)) {
            const paramName = outputMappings[outputName] || outputName;

            if (value === undefined || value === null) continue;

            if (this.processor.needsShader(value)) {
                // Contains texture or geometry reference - handle in shader
                if (value.op === 'image' || value.op === 'tiledimage') {
                    // Load texture
                    if (options.loadTextures && value.file) {
                        const texturePath = options.textureBasePath + value.file;
                        const texture = this.textureLoader.load(texturePath);
                        texture.wrapS = THREE.RepeatWrapping;
                        texture.wrapT = THREE.RepeatWrapping;
                        textures[paramName] = texture;
                    }
                }
            } else if (isConstant(value)) {
                // Constant value - set directly
                params[paramName] = value;
            }
        }
    }

    /**
     * Setup shader customization for complex node graphs
     */
    _setupShaderCustomization(material, params, textures) {
        // For WebGL2, we can use onBeforeCompile to inject custom GLSL
        // This is needed for operations that can't be pre-computed

        material.onBeforeCompile = (shader) => {
            // Add uniforms for node graph operations
            // This would be extended based on what deferred operations exist

            // Example: HSV adjust uniform
            if (params._hsvAdjust) {
                shader.uniforms.hsvAdjust = { value: new THREE.Vector3(...params._hsvAdjust) };

                // Inject HSV conversion functions
                shader.fragmentShader = shader.fragmentShader.replace(
                    '#include <common>',
                    `#include <common>
                    uniform vec3 hsvAdjust;

                    vec3 rgb2hsv(vec3 c) {
                        vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
                        vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
                        vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
                        float d = q.x - min(q.w, q.y);
                        float e = 1.0e-10;
                        return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
                    }

                    vec3 hsv2rgb(vec3 c) {
                        vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
                        vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                        return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
                    }
                    `
                );
            }
        };
    }

    /**
     * Load texture with proper settings
     */
    loadTexture(path, colorSpace = THREE.SRGBColorSpace) {
        const texture = this.textureLoader.load(path);
        texture.colorSpace = colorSpace;
        texture.wrapS = THREE.RepeatWrapping;
        texture.wrapT = THREE.RepeatWrapping;
        return texture;
    }
}

// ============================================================================
// GLSL Code Generator for Complex Node Graphs
// ============================================================================

/**
 * Generate GLSL code for MaterialX nodes that can't be pre-evaluated
 */
export class GlslCodeGenerator {
    constructor() {
        this.varCounter = 0;
        this.uniforms = new Map();
        this.textures = new Map();
        this.code = [];
    }

    reset() {
        this.varCounter = 0;
        this.uniforms.clear();
        this.textures.clear();
        this.code = [];
    }

    /**
     * Generate GLSL code for a deferred operation
     */
    generate(op, outputType = 'vec3') {
        if (!op || typeof op !== 'object' || !op.op) {
            // Constant value
            return this._formatConstant(op, outputType);
        }

        const varName = this._newVar();

        switch (op.op) {
            case 'add':
                return this._binary(varName, '+', op.in1, op.in2, outputType);
            case 'subtract':
                return this._binary(varName, '-', op.in1, op.in2, outputType);
            case 'multiply':
                return this._binary(varName, '*', op.in1, op.in2, outputType);
            case 'divide':
                return this._binary(varName, '/', op.in1, op.in2, outputType);
            case 'power':
                return this._func2(varName, 'pow', op.in1, op.in2, outputType);
            case 'min':
                return this._func2(varName, 'min', op.in1, op.in2, outputType);
            case 'max':
                return this._func2(varName, 'max', op.in1, op.in2, outputType);
            case 'dot':
                return this._func2(varName, 'dot', op.in1, op.in2, 'float');
            case 'cross':
                return this._func2(varName, 'cross', op.in1, op.in2, 'vec3');

            case 'absval':
                return this._func1(varName, 'abs', op.in, outputType);
            case 'sqrt':
                return this._func1(varName, 'sqrt', op.in, outputType);
            case 'floor':
                return this._func1(varName, 'floor', op.in, outputType);
            case 'ceil':
                return this._func1(varName, 'ceil', op.in, outputType);
            case 'fract':
                return this._func1(varName, 'fract', op.in, outputType);
            case 'sin':
                return this._func1(varName, 'sin', op.in, outputType);
            case 'cos':
                return this._func1(varName, 'cos', op.in, outputType);
            case 'normalize':
                return this._func1(varName, 'normalize', op.in, outputType);
            case 'length':
                return this._func1(varName, 'length', op.in, 'float');
            case 'negate':
                this.code.push(`${outputType} ${varName} = -${this.generate(op.in, outputType)};`);
                return varName;

            case 'clamp':
                return this._func3(varName, 'clamp', op.in, op.low, op.high, outputType);
            case 'mix':
                return this._func3(varName, 'mix', op.bg, op.fg, op.mix, outputType);
            case 'smoothstep':
                return this._func3(varName, 'smoothstep', op.low, op.high, op.in, outputType);

            case 'combine2':
                this.code.push(`vec2 ${varName} = vec2(${this.generate(op.in1, 'float')}, ${this.generate(op.in2, 'float')});`);
                return varName;
            case 'combine3':
                this.code.push(`vec3 ${varName} = vec3(${this.generate(op.in1, 'float')}, ${this.generate(op.in2, 'float')}, ${this.generate(op.in3, 'float')});`);
                return varName;
            case 'combine4':
                this.code.push(`vec4 ${varName} = vec4(${this.generate(op.in1, 'float')}, ${this.generate(op.in2, 'float')}, ${this.generate(op.in3, 'float')}, ${this.generate(op.in4, 'float')});`);
                return varName;

            case 'extract':
                const comp = ['x', 'y', 'z', 'w'][op.index] || 'x';
                this.code.push(`float ${varName} = ${this.generate(op.in, 'vec4')}.${comp};`);
                return varName;

            case 'image':
            case 'tiledimage':
                return this._generateTextureRead(varName, op, outputType);

            case 'texcoord':
                return 'vUv';

            case 'position':
                return op.space === 'world' ? 'vWorldPosition' : 'vPosition';

            case 'normal':
                return op.space === 'world' ? 'vNormal' : 'normal';

            case 'luminance':
                this.code.push(`float ${varName} = dot(${this.generate(op.in, 'vec3')}, vec3(0.2126, 0.7152, 0.0722));`);
                return varName;

            case 'invert':
                const facVar = this.generate(op.fac, 'float');
                const inVar = this.generate(op.in, 'vec3');
                this.code.push(`vec3 ${varName} = mix(${inVar}, vec3(1.0) - ${inVar}, ${facVar});`);
                return varName;

            case 'hsvadjust':
                return this._generateHsvAdjust(varName, op);

            case 'remap':
                const inVal = this.generate(op.in, outputType);
                const inLow = this.generate(op.inlow, outputType);
                const inHigh = this.generate(op.inhigh, outputType);
                const outLow = this.generate(op.outlow, outputType);
                const outHigh = this.generate(op.outhigh, outputType);
                this.code.push(`${outputType} ${varName} = ${outLow} + (${inVal} - ${inLow}) / (${inHigh} - ${inLow}) * (${outHigh} - ${outLow});`);
                return varName;

            case 'colorspace':
                return this._generateColorspace(varName, op, outputType);

            default:
                console.warn(`Unknown GLSL op: ${op.op}`);
                return this._formatConstant(0, outputType);
        }
    }

    _newVar() {
        return `mtlx_v${this.varCounter++}`;
    }

    _formatConstant(value, type) {
        if (typeof value === 'number') {
            const numStr = value.toFixed(6);
            switch (type) {
                case 'float': return numStr;
                case 'vec2': return `vec2(${numStr})`;
                case 'vec3': return `vec3(${numStr})`;
                case 'vec4': return `vec4(${numStr})`;
                default: return numStr;
            }
        }
        if (Array.isArray(value)) {
            const nums = value.map(v => v.toFixed(6));
            switch (value.length) {
                case 2: return `vec2(${nums.join(', ')})`;
                case 3: return `vec3(${nums.join(', ')})`;
                case 4: return `vec4(${nums.join(', ')})`;
                default: return nums[0];
            }
        }
        return '0.0';
    }

    _binary(varName, op, in1, in2, type) {
        this.code.push(`${type} ${varName} = ${this.generate(in1, type)} ${op} ${this.generate(in2, type)};`);
        return varName;
    }

    _func1(varName, func, in1, type) {
        this.code.push(`${type} ${varName} = ${func}(${this.generate(in1, type)});`);
        return varName;
    }

    _func2(varName, func, in1, in2, type) {
        const inType = type === 'float' && (func === 'dot') ? 'vec3' : type;
        this.code.push(`${type} ${varName} = ${func}(${this.generate(in1, inType)}, ${this.generate(in2, inType)});`);
        return varName;
    }

    _func3(varName, func, in1, in2, in3, type) {
        const thirdType = func === 'mix' || func === 'clamp' ? type : type;
        this.code.push(`${type} ${varName} = ${func}(${this.generate(in1, type)}, ${this.generate(in2, type)}, ${this.generate(in3, func === 'mix' ? 'float' : type)});`);
        return varName;
    }

    _generateTextureRead(varName, op, type) {
        const texName = `tex_${this.textures.size}`;
        this.textures.set(texName, op.file);

        let uvExpr = 'vUv';
        if (op.texcoord) {
            uvExpr = this.generate(op.texcoord, 'vec2');
        }

        if (op.op === 'tiledimage' && op.uvtiling) {
            const tiling = this.generate(op.uvtiling, 'vec2');
            const offset = this.generate(op.uvoffset ?? [0, 0], 'vec2');
            uvExpr = `${uvExpr} * ${tiling} + ${offset}`;
        }

        this.code.push(`vec4 ${varName}_raw = texture2D(${texName}, ${uvExpr});`);

        if (type === 'vec4') {
            return `${varName}_raw`;
        } else if (type === 'vec3') {
            return `${varName}_raw.rgb`;
        } else if (type === 'float') {
            return `${varName}_raw.r`;
        }
        return `${varName}_raw`;
    }

    _generateHsvAdjust(varName, op) {
        const inColor = this.generate(op.in, 'vec3');
        const amount = this.generate(op.amount, 'vec3');

        this.code.push(`
// HSV adjustment
vec3 ${varName}_hsv = rgb2hsv(${inColor});
${varName}_hsv.x = fract(${varName}_hsv.x + ${amount}.x);
${varName}_hsv.y = clamp(${varName}_hsv.y * ${amount}.y, 0.0, 1.0);
${varName}_hsv.z = clamp(${varName}_hsv.z * ${amount}.z, 0.0, 1.0);
vec3 ${varName} = hsv2rgb(${varName}_hsv);`);
        return varName;
    }

    _generateColorspace(varName, op, type) {
        const inVal = this.generate(op.in, type);

        if (op.colorspace === 'srgb_texture' || op.colorspace === 'sRGB') {
            // sRGB to linear
            this.code.push(`${type} ${varName} = pow(${inVal}, ${type === 'vec3' ? 'vec3(2.2)' : '2.2'});`);
        } else {
            this.code.push(`${type} ${varName} = ${inVal};`);
        }
        return varName;
    }

    /**
     * Get complete GLSL code
     */
    getCode() {
        return this.code.join('\n');
    }

    /**
     * Get uniforms for textures
     */
    getTextureUniforms() {
        return this.textures;
    }
}

// ============================================================================
// Export all for convenience
// ============================================================================

export { MtlxNodeEval as MtlxNodes };  // Alias for compatibility with WebGPU version
