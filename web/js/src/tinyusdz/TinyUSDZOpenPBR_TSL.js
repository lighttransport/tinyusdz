/**
 * TinyUSDZ OpenPBR TSL Material
 *
 * Custom OpenPBR shading model implementation using Three.js NodeMaterial and TSL
 * (Three Shading Language) for WebGPU rendering.
 *
 * This module provides:
 * 1. OpenPBR uber shader with all material parameters
 * 2. Texture support for all channels
 * 3. Basic MaterialX node operations (add, mul, mix, etc.)
 * 4. Proper physically-based rendering following OpenPBR specification
 */

import * as THREE from 'three/webgpu';
import {
    // Core
    Fn,

    // Value constructors
    float,
    vec2,
    vec3,
    vec4,
    uniform,

    // Texture
    texture,
    uv,

    // Math operations
    add,
    sub,
    mul,
    div,
    pow,
    sqrt,
    abs,
    sign,
    floor,
    ceil,
    fract,
    min,
    max,
    clamp,
    saturate,
    mix,
    step,
    smoothstep,
    length,
    dot,
    cross,
    normalize,
    reflect,
    negate,
    exp,
    log,
    sin,
    cos,

    // Conditionals
    select,

    // Built-in nodes
    positionWorld,
    normalWorld,
    cameraPosition
} from 'three/tsl';

// ============================================================================
// Constants
// ============================================================================

const PI = Math.PI;
const INV_PI = 1.0 / PI;

// ============================================================================
// MaterialX Node Operations
// ============================================================================

/**
 * MaterialX-style node operations
 * These mirror the standard MaterialX node library
 */
export const MtlxNodes = {
    // Basic math operations
    add: (a, b) => add(a, b),
    subtract: (a, b) => sub(a, b),
    multiply: (a, b) => mul(a, b),
    divide: (a, b) => div(a, b),
    power: (a, b) => pow(a, b),

    // Unary operations
    absval: (a) => abs(a),
    sign: (a) => sign(a),
    floor: (a) => floor(a),
    ceil: (a) => ceil(a),
    round: (a) => floor(add(a, 0.5)),
    fract: (a) => fract(a),
    sqrt: (a) => sqrt(a),
    inversesqrt: (a) => div(float(1.0), sqrt(a)),
    negate: (a) => negate(a),
    exp: (a) => exp(a),
    ln: (a) => log(a),

    // Trigonometric (basic only)
    sin: (a) => sin(a),
    cos: (a) => cos(a),

    // Comparison and clamping
    min: (a, b) => min(a, b),
    max: (a, b) => max(a, b),
    clamp: (val, minVal, maxVal) => clamp(val, minVal, maxVal),
    saturate: (a) => saturate(a),

    // Interpolation
    mix: (a, b, t) => mix(a, b, t),
    smoothstep: (edge0, edge1, x) => smoothstep(edge0, edge1, x),
    step: (edge, x) => step(edge, x),

    // Vector operations
    normalize: (v) => normalize(v),
    length: (v) => length(v),
    distance: (a, b) => length(sub(a, b)),
    dot: (a, b) => dot(a, b),
    cross: (a, b) => cross(a, b),
    reflect: (i, n) => reflect(i, n),

    // Rotation — Rodrigues' formula matching MaterialX GLSL mx_rotationMatrix
    rotate3d: (v, amount, axis) => {
        const rad = mul(amount, float(Math.PI / 180));
        const s = sin(rad);
        const c = cos(rad);
        const oc = sub(float(1), c);
        const a = normalize(axis);
        const ax = a.x, ay = a.y, az = a.z;
        // Row 0
        const r00 = add(mul(oc, mul(ax, ax)), c);
        const r01 = add(mul(oc, mul(ax, ay)), mul(az, s));
        const r02 = sub(mul(oc, mul(az, ax)), mul(ay, s));
        // Row 1
        const r10 = sub(mul(oc, mul(ax, ay)), mul(az, s));
        const r11 = add(mul(oc, mul(ay, ay)), c);
        const r12 = add(mul(oc, mul(ay, az)), mul(ax, s));
        // Row 2
        const r20 = add(mul(oc, mul(az, ax)), mul(ay, s));
        const r21 = sub(mul(oc, mul(ay, az)), mul(ax, s));
        const r22 = add(mul(oc, mul(az, az)), c);

        return vec3(
            add(add(mul(r00, v.x), mul(r01, v.y)), mul(r02, v.z)),
            add(add(mul(r10, v.x), mul(r11, v.y)), mul(r12, v.z)),
            add(add(mul(r20, v.x), mul(r21, v.y)), mul(r22, v.z))
        );
    },

    // Color operations
    luminance: (c) => dot(c, vec3(0.2126, 0.7152, 0.0722)),

    // RGB to HSV - simplified version without complex conditionals
    rgbtohsv: (rgb) => {
        // Simplified: just return a placeholder - full HSV conversion needs complex conditionals
        // For now return value (brightness) as all components
        const maxC = max(max(rgb.x, rgb.y), rgb.z);
        return vec3(float(0), float(0), maxC);
    },

    // Contrast/brightness
    contrast: (input, amount, pivot) => {
        return add(mul(sub(input, pivot), amount), pivot);
    },

    // Remap
    remap: (input, inLow, inHigh, outLow, outHigh) => {
        const t = div(sub(input, inLow), sub(inHigh, inLow));
        return add(mul(t, sub(outHigh, outLow)), outLow);
    },

    // Conditional - use select with node comparison methods
    ifgreater: (value1, value2, inTrue, inFalse) => {
        return select(value1.greaterThan(value2), inTrue, inFalse);
    },

    ifequal: (value1, value2, inTrue, inFalse) => {
        return select(value1.equal(value2), inTrue, inFalse);
    },

    // Swizzle helpers
    swizzle_x: (v) => v.x,
    swizzle_y: (v) => v.y,
    swizzle_z: (v) => v.z,
    swizzle_w: (v) => v.w,
    swizzle_xy: (v) => vec2(v.x, v.y),
    swizzle_xyz: (v) => vec3(v.x, v.y, v.z),

    // Combine/extract
    combine2: (x, y) => vec2(x, y),
    combine3: (x, y, z) => vec3(x, y, z),
    combine4: (x, y, z, w) => vec4(x, y, z, w),
    extract: (v, index) => v.element(index),

    // ========================================================================
    // Colorspace Conversion Functions
    // ========================================================================

    /**
     * Convert sRGB to Linear RGB (proper sRGB transfer function)
     * Uses the official sRGB specification with linear segment
     * @param {Node} color - sRGB color (vec3 or single channel)
     * @returns {Node} Linear RGB color
     */
    srgbToLinear: (color) => {
        // sRGB to Linear:
        // if c <= 0.04045: c / 12.92
        // else: pow((c + 0.055) / 1.055, 2.4)
        const threshold = float(0.04045);
        const linearPart = div(color, float(12.92));
        const gammaPart = pow(div(add(color, float(0.055)), float(1.055)), float(2.4));
        return select(color.lessThanEqual(threshold), linearPart, gammaPart);
    },

    /**
     * Convert sRGB to Linear RGB (simplified gamma 2.2)
     * Faster but less accurate than proper sRGB conversion
     * @param {Node} color - sRGB color
     * @returns {Node} Linear RGB color
     */
    srgbToLinearFast: (color) => {
        return pow(color, float(2.2));
    },

    /**
     * Convert Linear RGB to sRGB (proper sRGB transfer function)
     * Uses the official sRGB specification with linear segment
     * @param {Node} color - Linear RGB color
     * @returns {Node} sRGB color
     */
    linearToSrgb: (color) => {
        // Linear to sRGB:
        // if c <= 0.0031308: c * 12.92
        // else: 1.055 * pow(c, 1/2.4) - 0.055
        const threshold = float(0.0031308);
        const linearPart = mul(color, float(12.92));
        const gammaPart = sub(mul(float(1.055), pow(color, float(1.0 / 2.4))), float(0.055));
        return select(color.lessThanEqual(threshold), linearPart, gammaPart);
    },

    /**
     * Convert Linear RGB to sRGB (simplified gamma 2.2)
     * Faster but less accurate than proper sRGB conversion
     * @param {Node} color - Linear RGB color
     * @returns {Node} sRGB color
     */
    linearToSrgbFast: (color) => {
        return pow(color, float(1.0 / 2.2));
    },

    /**
     * Apply colorspace conversion based on source and target colorspace
     * @param {Node} color - Input color
     * @param {string} sourceColorspace - Source colorspace (e.g., 'srgb_texture', 'lin_rec709')
     * @param {string} targetColorspace - Target colorspace (default: 'lin_rec709')
     * @returns {Node} Converted color
     */
    convertColorspace: (color, sourceColorspace, targetColorspace = 'lin_rec709') => {
        // Normalize colorspace names
        const src = (sourceColorspace || '').toLowerCase().replace(/[_-]/g, '');
        const tgt = (targetColorspace || 'linrec709').toLowerCase().replace(/[_-]/g, '');

        // If same colorspace, no conversion needed
        if (src === tgt) return color;

        // Source is sRGB, target is linear
        if ((src === 'srgb' || src === 'srgbtexture') &&
            (tgt === 'linrec709' || tgt === 'linsrgb' || tgt === 'linear')) {
            return MtlxNodes.srgbToLinear(color);
        }

        // Source is linear, target is sRGB
        if ((src === 'linrec709' || src === 'linsrgb' || src === 'linear') &&
            (tgt === 'srgb' || tgt === 'srgbtexture')) {
            return MtlxNodes.linearToSrgb(color);
        }

        // Raw/data textures - no conversion
        if (src === 'raw' || tgt === 'raw') {
            return color;
        }

        // Default: assume sRGB source needs linearization for rendering
        if (src === 'srgbtexture' || src === 'srgb') {
            return MtlxNodes.srgbToLinear(color);
        }

        // No conversion for unrecognized colorspaces
        return color;
    }
};

// ============================================================================
// OpenPBR Parameter Structure
// ============================================================================

/**
 * Default OpenPBR parameters following the OpenPBR specification
 */
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

    // Fuzz layer (alternative to sheen)
    fuzz_weight: 0.0,
    fuzz_color: [1.0, 1.0, 1.0],
    fuzz_roughness: 0.5,

    // Thin film (iridescence)
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
// Colorspace Conversion Utilities (JavaScript-side)
// ============================================================================

/**
 * Convert a single sRGB value to linear
 * @param {number} value - sRGB value [0, 1]
 * @returns {number} Linear value
 */
function srgbToLinearValue(value) {
    return value <= 0.04045
        ? value / 12.92
        : Math.pow((value + 0.055) / 1.055, 2.4);
}

/**
 * Convert a single linear value to sRGB
 * @param {number} value - Linear value [0, 1]
 * @returns {number} sRGB value
 */
function linearToSrgbValue(value) {
    return value <= 0.0031308
        ? value * 12.92
        : 1.055 * Math.pow(value, 1.0 / 2.4) - 0.055;
}

/**
 * Convert color array from source colorspace to target colorspace
 * @param {Array|number} color - Color value(s)
 * @param {string} sourceColorspace - Source colorspace
 * @param {string} targetColorspace - Target colorspace (default: 'lin_rec709')
 * @returns {Array|number} Converted color
 */
export function convertColorJS(color, sourceColorspace, targetColorspace = 'lin_rec709') {
    const src = (sourceColorspace || '').toLowerCase().replace(/[_-]/g, '');
    const tgt = (targetColorspace || 'linrec709').toLowerCase().replace(/[_-]/g, '');

    // Same colorspace, no conversion
    if (src === tgt) return color;

    // Determine conversion function
    let convertFn = null;

    if ((src === 'srgb' || src === 'srgbtexture') &&
        (tgt === 'linrec709' || tgt === 'linsrgb' || tgt === 'linear')) {
        convertFn = srgbToLinearValue;
    } else if ((src === 'linrec709' || src === 'linsrgb' || src === 'linear') &&
               (tgt === 'srgb' || tgt === 'srgbtexture')) {
        convertFn = linearToSrgbValue;
    }

    if (!convertFn) return color;

    // Apply conversion
    if (typeof color === 'number') {
        return convertFn(color);
    }
    if (Array.isArray(color)) {
        return color.map(c => convertFn(c));
    }
    return color;
}

// ============================================================================
// OpenPBR TSL Material Class
// ============================================================================

/**
 * Create an OpenPBR-compatible MeshPhysicalMaterial for WebGPU
 *
 * This factory function creates a MeshPhysicalMaterial with OpenPBR parameter
 * naming and proper integration with Three.js WebGPU rendering.
 *
 * @param {Object} params - OpenPBR parameters
 * @param {Object} options - Additional options
 * @param {string} options.inputColorspace - Colorspace of input color values (default: 'srgb')
 *        - 'srgb': Input colors are in sRGB, will be converted to linear
 *        - 'lin_rec709' or 'linear': Input colors are already linear, no conversion
 */
export function createOpenPBRMaterial(params = {}, options = {}) {
    const inputColorspace = options.inputColorspace || 'srgb';
    const material = new THREE.MeshPhysicalMaterial();

    // Flag for type checking
    material.isOpenPBRNodeMaterial = true;

    // Store OpenPBR-specific values that don't map directly
    material._openPBR = {
        base_weight: params.base_weight ?? DEFAULT_OPENPBR_PARAMS.base_weight,
        base_diffuse_roughness: params.base_diffuse_roughness ?? DEFAULT_OPENPBR_PARAMS.base_diffuse_roughness,
        specular_weight: params.specular_weight ?? DEFAULT_OPENPBR_PARAMS.specular_weight,
        thin_film_thickness: params.thin_film_thickness ?? DEFAULT_OPENPBR_PARAMS.thin_film_thickness,
    };

    // Helper to convert color based on input colorspace
    // Three.js WebGPU expects linear color values
    const toLinearColor = (color) => {
        if (!color) return color;
        // Convert to linear if input is sRGB
        return convertColorJS(color, inputColorspace, 'lin_rec709');
    };

    // Set default/provided OpenPBR values mapped to MeshPhysicalMaterial
    const baseColor = toLinearColor(params.base_color ?? DEFAULT_OPENPBR_PARAMS.base_color);
    material.color = Array.isArray(baseColor)
        ? new THREE.Color(baseColor[0], baseColor[1], baseColor[2])
        : new THREE.Color(baseColor);

    material.metalness = params.base_metalness ?? DEFAULT_OPENPBR_PARAMS.base_metalness;
    material.roughness = params.specular_roughness ?? DEFAULT_OPENPBR_PARAMS.specular_roughness;
    material.ior = params.specular_ior ?? DEFAULT_OPENPBR_PARAMS.specular_ior;

    material.clearcoat = params.coat_weight ?? DEFAULT_OPENPBR_PARAMS.coat_weight;
    material.clearcoatRoughness = params.coat_roughness ?? DEFAULT_OPENPBR_PARAMS.coat_roughness;

    material.sheen = params.sheen_weight ?? DEFAULT_OPENPBR_PARAMS.sheen_weight;
    material.sheenRoughness = params.sheen_roughness ?? DEFAULT_OPENPBR_PARAMS.sheen_roughness;
    const sheenColor = toLinearColor(params.sheen_color ?? DEFAULT_OPENPBR_PARAMS.sheen_color);
    material.sheenColor = Array.isArray(sheenColor)
        ? new THREE.Color(sheenColor[0], sheenColor[1], sheenColor[2])
        : new THREE.Color(sheenColor);

    material.iridescence = params.thin_film_weight ?? DEFAULT_OPENPBR_PARAMS.thin_film_weight;
    material.iridescenceIOR = params.thin_film_ior ?? DEFAULT_OPENPBR_PARAMS.thin_film_ior;

    material.transmission = params.transmission_weight ?? DEFAULT_OPENPBR_PARAMS.transmission_weight;

    const emissionColor = toLinearColor(params.emission_color ?? DEFAULT_OPENPBR_PARAMS.emission_color);
    material.emissive = Array.isArray(emissionColor)
        ? new THREE.Color(emissionColor[0], emissionColor[1], emissionColor[2])
        : new THREE.Color(emissionColor);
    material.emissiveIntensity = params.emission_luminance ?? DEFAULT_OPENPBR_PARAMS.emission_luminance;

    if (params.geometry_opacity !== undefined) {
        material.opacity = params.geometry_opacity;
        material.transparent = params.geometry_opacity < 1.0;
    }

    return material;
}

/**
 * OpenPBR Material class wrapper (for backward compatibility)
 * Uses composition - wraps a MeshPhysicalMaterial
 */
export class OpenPBRNodeMaterial {
    constructor(params = {}) {
        // Create the underlying material
        this._material = createOpenPBRMaterial(params);

        // Copy reference for compatibility
        this.isOpenPBRNodeMaterial = true;
    }

    // Get the underlying Three.js material
    get material() {
        return this._material;
    }

    // Proxy commonly used properties
    get color() { return this._material.color; }
    get metalness() { return this._material.metalness; }
    set metalness(v) { this._material.metalness = v; }
    get roughness() { return this._material.roughness; }
    set roughness(v) { this._material.roughness = v; }
    get ior() { return this._material.ior; }
    set ior(v) { this._material.ior = v; }
    get clearcoat() { return this._material.clearcoat; }
    set clearcoat(v) { this._material.clearcoat = v; }
    get clearcoatRoughness() { return this._material.clearcoatRoughness; }
    set clearcoatRoughness(v) { this._material.clearcoatRoughness = v; }
    get sheen() { return this._material.sheen; }
    set sheen(v) { this._material.sheen = v; }
    get iridescence() { return this._material.iridescence; }
    set iridescence(v) { this._material.iridescence = v; }
    get transmission() { return this._material.transmission; }
    set transmission(v) { this._material.transmission = v; }
    get emissive() { return this._material.emissive; }
    get emissiveIntensity() { return this._material.emissiveIntensity; }
    set emissiveIntensity(v) { this._material.emissiveIntensity = v; }
    get opacity() { return this._material.opacity; }
    set opacity(v) { this._material.opacity = v; this._material.transparent = v < 1.0; }
    get _openPBR() { return this._material._openPBR; }
    get needsUpdate() { return this._material.needsUpdate; }
    set needsUpdate(v) { this._material.needsUpdate = v; }
    get userData() { return this._material.userData; }
    get name() { return this._material.name; }
    set name(v) { this._material.name = v; }
    get map() { return this._material.map; }
    set map(v) { this._material.map = v; }
    get transparent() { return this._material.transparent; }
    set transparent(v) { this._material.transparent = v; }
}

// ============================================================================
// MaterialX Node Graph Processor
// ============================================================================

/**
 * Process a MaterialX node graph and convert it to TSL nodes
 *
 * This is a simplified implementation that handles basic node types.
 * More complex nodes can be added as needed.
 */
export class MtlxNodeGraphProcessor {
    constructor() {
        this.nodeCache = new Map();
    }

    /**
     * Process a single MaterialX node and return a TSL node
     */
    processNode(nodeData, inputs = {}) {
        const nodeType = nodeData.type || nodeData.nodeType;

        switch (nodeType) {
            case 'constant':
                return this._processConstant(nodeData, inputs);

            case 'add':
                return MtlxNodes.add(inputs.in1, inputs.in2);

            case 'subtract':
                return MtlxNodes.subtract(inputs.in1, inputs.in2);

            case 'multiply':
                return MtlxNodes.multiply(inputs.in1, inputs.in2);

            case 'divide':
                return MtlxNodes.divide(inputs.in1, inputs.in2);

            case 'mix':
                return MtlxNodes.mix(inputs.bg, inputs.fg, inputs.mix);

            case 'clamp':
                return MtlxNodes.clamp(inputs.in, inputs.low || float(0), inputs.high || float(1));

            case 'power':
                return MtlxNodes.power(inputs.in1, inputs.in2);

            case 'dotproduct':
                return MtlxNodes.dot(inputs.in1, inputs.in2);

            case 'normalize':
                return MtlxNodes.normalize(inputs.in);

            case 'image':
            case 'tiledimage':
                return this._processImage(nodeData, inputs);

            case 'texcoord':
                return uv();

            case 'position':
                return positionWorld;

            case 'normal':
                return normalWorld;

            case 'tangent':
                // tangentWorld not available in minimal imports, use normalWorld as fallback
                return normalWorld;

            case 'extract':
                return MtlxNodes.extract(inputs.in, nodeData.index || 0);

            case 'combine2':
                return MtlxNodes.combine2(inputs.in1, inputs.in2);

            case 'combine3':
                return MtlxNodes.combine3(inputs.in1, inputs.in2, inputs.in3);

            case 'combine4':
                return MtlxNodes.combine4(inputs.in1, inputs.in2, inputs.in3, inputs.in4);

            case 'luminance':
                return MtlxNodes.luminance(inputs.in);

            case 'remap':
                return MtlxNodes.remap(
                    inputs.in,
                    inputs.inlow || float(0),
                    inputs.inhigh || float(1),
                    inputs.outlow || float(0),
                    inputs.outhigh || float(1)
                );

            case 'smoothstep':
                return MtlxNodes.smoothstep(inputs.low, inputs.high, inputs.in);

            case 'ifgreater':
                return MtlxNodes.ifgreater(inputs.value1, inputs.value2, inputs.in1, inputs.in2);

            case 'rotate3d':
                return MtlxNodes.rotate3d(
                    inputs.in || vec3(0, 0, 0),
                    inputs.amount || float(0),
                    inputs.axis || vec3(0, 1, 0)
                );

            default:
                console.warn(`Unknown MaterialX node type: ${nodeType}`);
                return float(0);
        }
    }

    _processConstant(nodeData, inputs = {}) {
        // Prefer resolved inputs (from processGraph), fall back to nodeData.value
        const value = (inputs && inputs.value !== undefined) ? inputs.value : nodeData.value;
        const valueType = nodeData.valueType || 'float';

        switch (valueType) {
            case 'float':
                return float(value);
            case 'color3':
            case 'vector3':
                return vec3(value[0], value[1], value[2]);
            case 'color4':
            case 'vector4':
                return vec4(value[0], value[1], value[2], value[3] || 1.0);
            case 'vector2':
                return vec2(value[0], value[1]);
            default:
                return float(value);
        }
    }

    _processImage(nodeData, inputs) {
        // Process texture with colorspace conversion
        const texCoord = inputs.texcoord || uv();

        if (nodeData.texture) {
            // Sample the texture
            let texColor = texture(nodeData.texture, texCoord);

            // Apply colorspace conversion if specified
            // Default assumption: color textures are in sRGB and need linearization
            const colorspace = nodeData.colorspace || nodeData.sourceColorspace || 'srgb_texture';
            const isColorTexture = !nodeData.isData && !nodeData.isNormalMap;

            if (isColorTexture) {
                // Convert color channels (RGB) from source colorspace to linear
                // Alpha channel is not affected by colorspace conversion
                const linearRGB = MtlxNodes.convertColorspace(
                    vec3(texColor.x, texColor.y, texColor.z),
                    colorspace,
                    'lin_rec709'
                );
                texColor = vec4(linearRGB.x, linearRGB.y, linearRGB.z, texColor.w);
            }

            return texColor;
        }

        // Return white if no texture
        return vec4(1.0, 1.0, 1.0, 1.0);
    }

    /**
     * Process a complete node graph
     */
    processGraph(graphData, textures = {}) {
        const nodes = graphData.nodes || [];
        const outputs = graphData.outputs || {};

        // Process nodes in order (assumes topological sort)
        for (const node of nodes) {
            const inputs = {};

            // Resolve input connections
            if (node.inputs) {
                for (const [inputName, inputData] of Object.entries(node.inputs)) {
                    if (inputData.connection) {
                        // Get from cache
                        inputs[inputName] = this.nodeCache.get(inputData.connection);
                    } else if (inputData.value !== undefined) {
                        // Direct value
                        inputs[inputName] = this._valueToNode(inputData.value, inputData.type);
                    }
                }
            }

            // Add texture if specified
            if (node.texture && textures[node.texture]) {
                node.texture = textures[node.texture];
            }

            // Process the node
            const result = this.processNode(node, inputs);
            this.nodeCache.set(node.name || node.id, result);
        }

        // Return output nodes
        const result = {};
        for (const [outputName, outputData] of Object.entries(outputs)) {
            result[outputName] = this.nodeCache.get(outputData.connection);
        }

        return result;
    }

    _valueToNode(value, type) {
        if (typeof value === 'number') {
            return float(value);
        } else if (Array.isArray(value)) {
            if (value.length === 2) return vec2(value[0], value[1]);
            if (value.length === 3) return vec3(value[0], value[1], value[2]);
            if (value.length === 4) return vec4(value[0], value[1], value[2], value[3]);
        }
        return float(value);
    }

    clear() {
        this.nodeCache.clear();
    }
}

// ============================================================================
// Note: BRDF helper functions (fresnelSchlick, distributionGGX, etc.) are
// defined inside OpenPBRNodeMaterial._buildShader() as they need Fn() context
// ============================================================================
