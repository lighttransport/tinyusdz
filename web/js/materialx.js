// TinyUSDZ MaterialX/OpenPBR Demo with Three.js
// This demo showcases OpenPBR material loading and editing with synthetic HDR environments

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RGBELoader } from 'three/examples/jsm/loaders/RGBELoader.js';
import { EXRLoader } from 'three/examples/jsm/loaders/EXRLoader.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { MaterialXLoader } from 'three/examples/jsm/loaders/MaterialXLoader.js';
import { convertOpenPBRToMaterialXML } from './convert-openpbr-to-mtlx.js';
import {
    initializeNodeGraph,
    registerMaterialXNodeTypes,
    showNodeGraph,
    hideNodeGraph,
    toggleNodeGraphVisibility
} from './materialx-node-graph.js';
import {
    showMaterialJSON,
    hideMaterialJSON,
    toggleMaterialJSONVisibility,
    switchMaterialTab
} from './material-json-viewer.js';
import {
    initializeColorPicker,
    toggleColorPickerMode,
    isColorPickerActive,
    handleColorPickerClick
} from './color-picker.js';
import {
    initializeMaterialPropertyPicker,
    toggleMaterialPropertyPickerMode,
    isMaterialPropertyPickerActive,
    handleMaterialPropertyPickerClick,
    resizeMaterialPropertyTargets
} from './material-property-picker.js';

// Embedded default OpenPBR scene (simple sphere with material)
const EMBEDDED_USDA_SCENE = `#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Sphere "MetallicSphere"
    {
        double radius = 1.0
        rel material:binding = </World/_materials/MetallicMaterial>
    }

    def Scope "_materials"
    {
        def Material "MetallicMaterial"
        {
            token outputs:surface.connect = </World/_materials/MetallicMaterial/OpenPBRSurface.outputs:surface>

            def Shader "OpenPBRSurface"
            {
                uniform token info:id = "OpenPBRSurface"

                # Base layer - metallic gold color
                color3f inputs:base_color = (0.9, 0.7, 0.3)
                float inputs:base_metalness = 0.85
                float inputs:base_weight = 1.0

                # Specular layer
                float inputs:specular_roughness = 0.25
                float inputs:specular_ior = 1.5
                float inputs:specular_weight = 1.0

                token outputs:surface
            }
        }
    }
}
`;

// AOV (Arbitrary Output Variable) Modes
const AOV_MODES = {
    NONE: 'none',
    NORMALS_WORLD: 'normals_world',
    NORMALS_VIEW: 'normals_view',
    TANGENTS: 'tangents',
    BINORMALS: 'binormals',
    TEXCOORD_0: 'texcoord_0',
    TEXCOORD_1: 'texcoord_1',
    POSITION_WORLD: 'position_world',
    POSITION_VIEW: 'position_view',
    DEPTH: 'depth',
    MATERIAL_ID: 'material_id',
    ALBEDO: 'albedo',
    ROUGHNESS: 'roughness',
    METALNESS: 'metalness',
    SPECULAR: 'specular',
    COAT: 'coat',
    TRANSMISSION: 'transmission',
    EMISSIVE: 'emissive'
};

// Global variables
let scene, camera, renderer, controls;
let raycaster, mouse;
let selectedObject = null;
let boundingBoxHelper = null; // Bounding box helper for selected object
let currentLoader = null; // TinyUSDZLoader instance
let currentNativeLoader = null; // Native TinyUSDZLoaderNative instance for low-level API
let materials = [];
let meshes = [];
let gui = null;
let paramFolder = null;
let environmentType = 'goegap_1k'; // 'studio', 'white', 'goegap_1k', or 'env_sunsky_sunset'
let showBackgroundEnvmap = true; // Toggle for showing environment map as background
let toneMappingType = 'none'; // 'none', 'aces', 'agx', 'neutral', 'aces13', 'aces20'
let exposureValue = 1.0; // Exposure value (works independently of tone mapping)
let pmremGenerator = null;
let acesTonemapPass = null; // Custom ACES tonemapping pass
let currentColorSpace = 'srgb'; // 'srgb' or 'display-p3'
let displayP3Supported = false;
let textureCache = new Map(); // Cache for loaded textures
let textureEnabled = {}; // Track which textures are enabled per material
let textureColorSpace = {}; // Track color space per texture per material
let textureUVSet = {}; // Track UV set selection per texture per material
let showingNormals = false; // Track if normal material is being shown
let originalMaterials = new Map(); // Store original materials when showing normals
let currentFileUpAxis = 'Y'; // Store the current file's upAxis (Y or Z)
let applyUpAxisConversion = true; // Apply Z-up to Y-up conversion by default
let sceneRoot = null; // Root object for the USD scene (for upAxis conversion)
let currentSceneMetadata = null; // Store the current USD scene metadata
let composer = null; // Effect composer for post-processing
let falseColorPass = null; // False color shader pass
let showingFalseColor = false; // Track if false color is being shown
let useNodeMaterial = false; // Toggle between MeshPhysicalMaterial and NodeMaterial (via MaterialXLoader)
let materialXLoader = null; // MaterialXLoader instance
let currentAOVMode = AOV_MODES.NONE; // Current AOV visualization mode
let aovOriginalMaterials = new Map(); // Store original materials when showing AOVs
let preferredMaterialType = 'auto'; // 'auto', 'openpbr', 'usdpreviewsurface' - Which material type to prefer when both are available

// Available texture color spaces
const TEXTURE_COLOR_SPACES = {
    'srgb': 'sRGB',
    'linear': 'Linear (Raw)',
    'rec709': 'Rec.709',
    'aces': 'ACES2065-1',
    'acescg': 'ACEScg'
};

// Shader code for color space conversions
const COLOR_SPACE_SHADER_FUNCTIONS = `
// sRGB to Linear conversion
vec3 sRGBToLinear(vec3 color) {
    vec3 linearRGBLo = color / 12.92;
    vec3 linearRGBHi = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(linearRGBLo, linearRGBHi, step(vec3(0.04045), color));
}

// Linear to sRGB conversion
vec3 linearToSRGB(vec3 color) {
    vec3 sRGBLo = color * 12.92;
    vec3 sRGBHi = pow(color, vec3(1.0/2.4)) * 1.055 - 0.055;
    return mix(sRGBLo, sRGBHi, step(vec3(0.0031308), color));
}

// Rec.709 to Linear (same transfer function as sRGB)
vec3 rec709ToLinear(vec3 color) {
    return sRGBToLinear(color);
}

// ACES AP0 (ACES2065-1) to Linear (simplified - using Bradford chromatic adaptation)
vec3 acesToLinear(vec3 color) {
    // ACES AP0 to sRGB/Linear D65 matrix (simplified)
    mat3 AP0_to_sRGB = mat3(
        2.52169, -1.13413, -0.38756,
        -0.27648, 1.37272, -0.09624,
        -0.01538, -0.15298, 1.16835
    );
    return AP0_to_sRGB * color;
}

// ACEScg (AP1) to Linear
vec3 acescgToLinear(vec3 color) {
    // ACES AP1 to sRGB/Linear D65 matrix
    mat3 AP1_to_sRGB = mat3(
        1.70505, -0.62179, -0.08326,
        -0.13026, 1.14080, -0.01055,
        -0.02400, -0.12897, 1.15297
    );
    return AP1_to_sRGB * color;
}

// Main color space conversion function
vec3 convertColorSpace(vec3 color, int colorSpace) {
    if (colorSpace == 0) {
        // sRGB
        return sRGBToLinear(color);
    } else if (colorSpace == 1) {
        // Linear (no conversion)
        return color;
    } else if (colorSpace == 2) {
        // Rec.709
        return rec709ToLinear(color);
    } else if (colorSpace == 3) {
        // ACES2065-1 (AP0)
        return acesToLinear(color);
    } else if (colorSpace == 4) {
        // ACEScg (AP1)
        return acescgToLinear(color);
    }
    return color;
}
`;

// False Color View Transform Shader (Blender-style)
// Maps scene-linear luminance to a heat map for exposure visualization
const FalseColorShader = {
    uniforms: {
        'tDiffuse': { value: null },
        'middleGrey': { value: 0.18 } // Standard middle grey in scene-linear
    },

    vertexShader: `
        varying vec2 vUv;
        void main() {
            vUv = uv;
            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
    `,

    fragmentShader: `
        uniform sampler2D tDiffuse;
        uniform float middleGrey;
        varying vec2 vUv;

        // Convert RGB to relative luminance (Rec. 709 weights)
        float getLuminance(vec3 color) {
            return dot(color, vec3(0.2126, 0.7152, 0.0722));
        }

        // Convert linear luminance to EV (exposure value) relative to middle grey
        float luminanceToEV(float luminance) {
            // EV = log2(luminance / middleGrey)
            return log2(max(luminance, 0.0001) / middleGrey);
        }

        // Map EV to false color (Blender-style heat map)
        vec3 evToFalseColor(float ev) {
            // Color stops based on Blender's false color:
            // < -7.5 EV: Black (underexposed)
            // -7.5 to -5.0: Purple/Blue (deep shadows)
            // -5.0 to -2.5: Blue to Cyan (shadows)
            // -2.5 to -0.5: Cyan to Green (low mids)
            // -0.5 to 0.5: Green to Grey (middle grey zone)
            // 0.5 to 2.5: Yellow to Orange (highlights)
            // 2.5 to 4.5: Orange to Red (bright highlights)
            // > 4.5: Red to White (overexposed)

            vec3 color;

            if (ev < -7.5) {
                // Deep underexposure - black to dark purple
                float t = smoothstep(-10.0, -7.5, ev);
                color = mix(vec3(0.0, 0.0, 0.0), vec3(0.2, 0.0, 0.4), t);
            }
            else if (ev < -5.0) {
                // Deep shadows - purple to blue
                float t = smoothstep(-7.5, -5.0, ev);
                color = mix(vec3(0.2, 0.0, 0.4), vec3(0.0, 0.0, 0.8), t);
            }
            else if (ev < -2.5) {
                // Shadows - blue to cyan
                float t = smoothstep(-5.0, -2.5, ev);
                color = mix(vec3(0.0, 0.0, 0.8), vec3(0.0, 0.6, 0.8), t);
            }
            else if (ev < -0.5) {
                // Low midtones - cyan to green
                float t = smoothstep(-2.5, -0.5, ev);
                color = mix(vec3(0.0, 0.6, 0.8), vec3(0.0, 0.7, 0.3), t);
            }
            else if (ev < 0.5) {
                // Middle grey zone - green to grey
                float t = smoothstep(-0.5, 0.5, ev);
                color = mix(vec3(0.0, 0.7, 0.3), vec3(0.5, 0.5, 0.5), t);
            }
            else if (ev < 2.5) {
                // Highlights - grey/yellow to orange
                float t = smoothstep(0.5, 2.5, ev);
                color = mix(vec3(0.5, 0.5, 0.5), vec3(1.0, 0.7, 0.0), t);
            }
            else if (ev < 4.5) {
                // Bright highlights - orange to red
                float t = smoothstep(2.5, 4.5, ev);
                color = mix(vec3(1.0, 0.7, 0.0), vec3(1.0, 0.0, 0.0), t);
            }
            else if (ev < 6.5) {
                // Very bright - red to pink/white
                float t = smoothstep(4.5, 6.5, ev);
                color = mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 0.8, 0.8), t);
            }
            else {
                // Overexposed - white
                float t = smoothstep(6.5, 10.0, ev);
                color = mix(vec3(1.0, 0.8, 0.8), vec3(1.0, 1.0, 1.0), t);
            }

            return color;
        }

        void main() {
            vec4 texel = texture2D(tDiffuse, vUv);

            // Calculate luminance from scene-linear RGB
            float luminance = getLuminance(texel.rgb);

            // Convert to EV
            float ev = luminanceToEV(luminance);

            // Map to false color
            vec3 falseColor = evToFalseColor(ev);

            gl_FragColor = vec4(falseColor, 1.0);
        }
    `
};

// ACES Tonemapping Shader
// Supports ACES 1.3 and ACES 2.0 as used in Blender 5.0
const ACESTonemapShader = {
    uniforms: {
        'tDiffuse': { value: null },
        'exposure': { value: 1.0 },
        'acesVersion': { value: 0 } // 0 = ACES 1.3, 1 = ACES 2.0
    },

    vertexShader: `
        varying vec2 vUv;
        void main() {
            vUv = uv;
            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
    `,

    fragmentShader: `
        uniform sampler2D tDiffuse;
        uniform float exposure;
        uniform int acesVersion;
        varying vec2 vUv;

        // ACES matrices and functions

        // sRGB to ACES AP0 (ACES2065-1) conversion matrix
        mat3 sRGB_to_AP0 = mat3(
            0.4397010, 0.3829780, 0.1773350,
            0.0897923, 0.8134230, 0.0967616,
            0.0175440, 0.1115440, 0.8707040
        );

        // ACES AP0 to sRGB conversion matrix
        mat3 AP0_to_sRGB = mat3(
            2.52169, -1.13413, -0.38756,
            -0.27648, 1.37272, -0.09624,
            -0.01538, -0.15298, 1.16835
        );

        // ACES AP1 (ACEScg) to AP0 conversion
        mat3 AP1_to_AP0 = mat3(
            0.6954522, 0.1406787, 0.1638691,
            0.0447946, 0.8596711, 0.0955343,
            -0.0055259, 0.0040253, 1.0015006
        );

        // ACES AP0 to AP1 conversion
        mat3 AP0_to_AP1 = mat3(
            1.4514393, -0.2365107, -0.2149286,
            -0.0765538, 1.1762297, -0.0996759,
            0.0083161, -0.0060324, 0.9977163
        );

        // ACES 1.3 RRT + ODT (Reference Rendering Transform + Output Device Transform)
        // This is the ACES Filmic tonemapper used in Blender 4.x and earlier
        vec3 ACESFilmic_1_3(vec3 color) {
            // Convert from AP0 to AP1 (ACEScg)
            // Stephen Hill's fit is designed for AP1 color space
            vec3 x = AP0_to_AP1 * color;

            // Apply the ACES filmic curve (RRT + ODT approximation)
            // This approximation combines RRT and sRGB ODT into one formula
            float a = 2.51;
            float b = 0.03;
            float c = 2.43;
            float d = 0.59;
            float e = 0.14;
            vec3 mapped = (x * (a * x + b)) / (x * (c * x + d) + e);

            // Convert from AP1 to linear sRGB
            mat3 AP1_to_sRGB = mat3(
                1.70505, -0.62179, -0.08326,
                -0.13026, 1.14080, -0.01055,
                0.00610, -0.00969, 1.00360
            );

            return clamp(AP1_to_sRGB * mapped, 0.0, 1.0);
        }

        // ACES 2.0 OpenDRT (Open Display Rendering Transform)
        // New tonemapper introduced in Blender 5.0
        vec3 OpenDRT_2_0(vec3 color) {
            // Convert from AP0 to AP1 (ACEScg) for OpenDRT processing
            vec3 aces = AP0_to_AP1 * color;

            // OpenDRT operates in AP1 space
            // Simplified approximation of the OpenDRT tone curve

            // Calculate luminance in AP1 space
            const vec3 AP1_RGB2Y = vec3(0.2722287, 0.6740818, 0.0536895);
            float Y = max(dot(aces, AP1_RGB2Y), 1e-10);

            // Tonescale parameters (simplified OpenDRT-like curve)
            const float c_t = 1.55; // Toe compression
            const float s_t = 0.99; // Toe smoothness
            const float c_d = 10.5; // Shoulder compression (controls peak)
            const float w_g = 0.14; // Gray point

            // Generalized Reinhard-style curve with toe and shoulder control
            float peak = 100.0; // Display peak luminance in nits
            float norm = Y / peak;

            // Toe-shoulder curve
            float num = norm * (1.0 + norm / (c_d * c_d));
            float denom = 1.0 + norm;
            float Y_out = num / denom;

            // Apply subtle S-curve for contrast
            Y_out = pow(Y_out, 0.9);

            // Preserve color ratios (chromatic adaptation)
            vec3 rgb_out = aces * (Y_out / Y);

            // Convert from AP1 to linear sRGB for display
            mat3 AP1_to_sRGB = mat3(
                1.70505, -0.62179, -0.08326,
                -0.13026, 1.14080, -0.01055,
                0.00610, -0.00969, 1.00360
            );

            vec3 result = AP1_to_sRGB * rgb_out;

            return clamp(result, 0.0, 1.0);
        }

        void main() {
            vec4 texel = texture2D(tDiffuse, vUv);
            vec3 color = texel.rgb;

            // Apply exposure
            color *= exposure;

            // Convert from sRGB to ACES AP0 (ACES2065-1)
            vec3 aces = sRGB_to_AP0 * color;

            // Apply selected ACES tonemapping
            vec3 result;
            if (acesVersion == 1) {
                // ACES 2.0 OpenDRT (Blender 5.0)
                result = OpenDRT_2_0(aces);
            } else {
                // ACES 1.3 Filmic (default, Blender 4.x)
                result = ACESFilmic_1_3(aces);
            }

            gl_FragColor = vec4(result, texel.a);
        }
    `
};

// AOV (Arbitrary Output Variable) System
// Provides various visualization modes for debugging and analysis

// Create AOV visualization material
function createAOVMaterial(aovMode, materialData = null) {
    let material;

    switch(aovMode) {
        case AOV_MODES.NORMALS_WORLD:
            material = new THREE.MeshNormalMaterial();
            material.name = 'AOV_NormalsWorld';
            break;

        case AOV_MODES.NORMALS_VIEW:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec3 vNormal;
                    void main() {
                        vNormal = normalize(normalMatrix * normal);
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec3 vNormal;
                    void main() {
                        gl_FragColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
                    }
                `,
                name: 'AOV_NormalsView'
            });
            break;

        case AOV_MODES.TANGENTS:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    attribute vec4 tangent;
                    varying vec3 vTangent;
                    void main() {
                        // Transform tangent to world space
                        vTangent = normalize(normalMatrix * tangent.xyz);
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec3 vTangent;
                    void main() {
                        // Map tangent from [-1,1] to [0,1] for visualization
                        gl_FragColor = vec4(vTangent * 0.5 + 0.5, 1.0);
                    }
                `,
                name: 'AOV_Tangents'
            });
            break;

        case AOV_MODES.BINORMALS:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    attribute vec4 tangent;
                    varying vec3 vBinormal;
                    void main() {
                        vec3 worldNormal = normalize(normalMatrix * normal);
                        vec3 worldTangent = normalize(normalMatrix * tangent.xyz);
                        // Calculate binormal (bitangent)
                        vBinormal = cross(worldNormal, worldTangent) * tangent.w;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec3 vBinormal;
                    void main() {
                        gl_FragColor = vec4(normalize(vBinormal) * 0.5 + 0.5, 1.0);
                    }
                `,
                name: 'AOV_Binormals'
            });
            break;

        case AOV_MODES.TEXCOORD_0:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    void main() {
                        // Visualize UV coordinates with a checkerboard pattern
                        vec2 checker = fract(vUv * 8.0);
                        float pattern = step(0.5, checker.x) * step(0.5, checker.y) +
                                       (1.0 - step(0.5, checker.x)) * (1.0 - step(0.5, checker.y));
                        vec3 color = mix(vec3(vUv, 0.0), vec3(vUv, 1.0), pattern);
                        gl_FragColor = vec4(color, 1.0);
                    }
                `,
                name: 'AOV_TexCoord0'
            });
            break;

        case AOV_MODES.TEXCOORD_1:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    attribute vec2 uv2;
                    varying vec2 vUv2;
                    void main() {
                        vUv2 = uv2;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv2;
                    void main() {
                        vec2 checker = fract(vUv2 * 8.0);
                        float pattern = step(0.5, checker.x) * step(0.5, checker.y) +
                                       (1.0 - step(0.5, checker.x)) * (1.0 - step(0.5, checker.y));
                        vec3 color = mix(vec3(vUv2, 0.0), vec3(vUv2, 1.0), pattern);
                        gl_FragColor = vec4(color, 1.0);
                    }
                `,
                name: 'AOV_TexCoord1'
            });
            break;

        case AOV_MODES.POSITION_WORLD:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec3 vWorldPosition;
                    void main() {
                        vec4 worldPos = modelMatrix * vec4(position, 1.0);
                        vWorldPosition = worldPos.xyz;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec3 vWorldPosition;
                    void main() {
                        // Normalize world position for visualization
                        vec3 color = fract(vWorldPosition * 0.5);
                        gl_FragColor = vec4(color, 1.0);
                    }
                `,
                name: 'AOV_PositionWorld'
            });
            break;

        case AOV_MODES.POSITION_VIEW:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec3 vViewPosition;
                    void main() {
                        vec4 viewPos = modelViewMatrix * vec4(position, 1.0);
                        vViewPosition = viewPos.xyz;
                        gl_Position = projectionMatrix * viewPos;
                    }
                `,
                fragmentShader: `
                    varying vec3 vViewPosition;
                    void main() {
                        // Visualize view space position (depth gradient)
                        float depth = -vViewPosition.z;
                        vec3 color = vec3(depth * 0.1);
                        gl_FragColor = vec4(color, 1.0);
                    }
                `,
                name: 'AOV_PositionView'
            });
            break;

        case AOV_MODES.DEPTH:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying float vDepth;
                    void main() {
                        vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
                        vDepth = -mvPosition.z;
                        gl_Position = projectionMatrix * mvPosition;
                    }
                `,
                fragmentShader: `
                    varying float vDepth;
                    uniform float cameraNear;
                    uniform float cameraFar;

                    void main() {
                        // Normalize depth between near and far
                        float normalizedDepth = (vDepth - cameraNear) / (cameraFar - cameraNear);
                        gl_FragColor = vec4(vec3(normalizedDepth), 1.0);
                    }
                `,
                uniforms: {
                    cameraNear: { value: 0.1 },
                    cameraFar: { value: 1000.0 }
                },
                name: 'AOV_Depth'
            });
            break;

        case AOV_MODES.MATERIAL_ID:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    void main() {
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    uniform vec3 materialColor;
                    void main() {
                        gl_FragColor = vec4(materialColor, 1.0);
                    }
                `,
                uniforms: {
                    materialColor: { value: new THREE.Color(Math.random(), Math.random(), Math.random()) }
                },
                name: 'AOV_MaterialID'
            });
            break;

        case AOV_MODES.ALBEDO:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform vec3 baseColor;
                    uniform sampler2D baseColorMap;
                    uniform bool hasBaseColorMap;

                    void main() {
                        vec3 albedo = baseColor;
                        if (hasBaseColorMap) {
                            vec4 texColor = texture2D(baseColorMap, vUv);
                            albedo *= texColor.rgb;
                        }
                        gl_FragColor = vec4(albedo, 1.0);
                    }
                `,
                uniforms: {
                    baseColor: { value: new THREE.Color(1, 1, 1) },
                    baseColorMap: { value: null },
                    hasBaseColorMap: { value: false }
                },
                name: 'AOV_Albedo'
            });

            // Copy material properties if available
            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                material.uniforms.baseColor.value.copy(srcMat.color || new THREE.Color(1, 1, 1));
                if (srcMat.map) {
                    material.uniforms.baseColorMap.value = srcMat.map;
                    material.uniforms.hasBaseColorMap.value = true;
                }
            }
            break;

        case AOV_MODES.ROUGHNESS:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform float roughness;
                    uniform sampler2D roughnessMap;
                    uniform bool hasRoughnessMap;

                    void main() {
                        float rough = roughness;
                        if (hasRoughnessMap) {
                            rough *= texture2D(roughnessMap, vUv).g;
                        }
                        gl_FragColor = vec4(vec3(rough), 1.0);
                    }
                `,
                uniforms: {
                    roughness: { value: 1.0 },
                    roughnessMap: { value: null },
                    hasRoughnessMap: { value: false }
                },
                name: 'AOV_Roughness'
            });

            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                material.uniforms.roughness.value = srcMat.roughness || 1.0;
                if (srcMat.roughnessMap) {
                    material.uniforms.roughnessMap.value = srcMat.roughnessMap;
                    material.uniforms.hasRoughnessMap.value = true;
                }
            }
            break;

        case AOV_MODES.METALNESS:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform float metalness;
                    uniform sampler2D metalnessMap;
                    uniform bool hasMetalnessMap;

                    void main() {
                        float metal = metalness;
                        if (hasMetalnessMap) {
                            metal *= texture2D(metalnessMap, vUv).b;
                        }
                        gl_FragColor = vec4(vec3(metal), 1.0);
                    }
                `,
                uniforms: {
                    metalness: { value: 0.0 },
                    metalnessMap: { value: null },
                    hasMetalnessMap: { value: false }
                },
                name: 'AOV_Metalness'
            });

            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                material.uniforms.metalness.value = srcMat.metalness || 0.0;
                if (srcMat.metalnessMap) {
                    material.uniforms.metalnessMap.value = srcMat.metalnessMap;
                    material.uniforms.hasMetalnessMap.value = true;
                }
            }
            break;

        case AOV_MODES.SPECULAR:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform vec3 specularColor;
                    uniform float specularIntensity;

                    void main() {
                        gl_FragColor = vec4(specularColor * specularIntensity, 1.0);
                    }
                `,
                uniforms: {
                    specularColor: { value: new THREE.Color(1, 1, 1) },
                    specularIntensity: { value: 1.0 }
                },
                name: 'AOV_Specular'
            });

            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                if (srcMat.specularColor) {
                    material.uniforms.specularColor.value.copy(srcMat.specularColor);
                }
                material.uniforms.specularIntensity.value = srcMat.specularIntensity || 1.0;
            }
            break;

        case AOV_MODES.COAT:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform float clearcoat;
                    uniform float clearcoatRoughness;

                    void main() {
                        // Visualize clearcoat as intensity, clearcoat roughness as blue
                        gl_FragColor = vec4(clearcoat, clearcoat, clearcoatRoughness, 1.0);
                    }
                `,
                uniforms: {
                    clearcoat: { value: 0.0 },
                    clearcoatRoughness: { value: 0.0 }
                },
                name: 'AOV_Coat'
            });

            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                material.uniforms.clearcoat.value = srcMat.clearcoat || 0.0;
                material.uniforms.clearcoatRoughness.value = srcMat.clearcoatRoughness || 0.0;
            }
            break;

        case AOV_MODES.TRANSMISSION:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform float transmission;
                    uniform vec3 attenuationColor;

                    void main() {
                        gl_FragColor = vec4(attenuationColor * transmission, 1.0);
                    }
                `,
                uniforms: {
                    transmission: { value: 0.0 },
                    attenuationColor: { value: new THREE.Color(1, 1, 1) }
                },
                name: 'AOV_Transmission'
            });

            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                material.uniforms.transmission.value = srcMat.transmission || 0.0;
                if (srcMat.attenuationColor) {
                    material.uniforms.attenuationColor.value.copy(srcMat.attenuationColor);
                }
            }
            break;

        case AOV_MODES.EMISSIVE:
            material = new THREE.ShaderMaterial({
                vertexShader: `
                    varying vec2 vUv;
                    void main() {
                        vUv = uv;
                        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    }
                `,
                fragmentShader: `
                    varying vec2 vUv;
                    uniform vec3 emissive;
                    uniform sampler2D emissiveMap;
                    uniform bool hasEmissiveMap;
                    uniform float emissiveIntensity;

                    void main() {
                        vec3 emissiveColor = emissive;
                        if (hasEmissiveMap) {
                            emissiveColor *= texture2D(emissiveMap, vUv).rgb;
                        }
                        gl_FragColor = vec4(emissiveColor * emissiveIntensity, 1.0);
                    }
                `,
                uniforms: {
                    emissive: { value: new THREE.Color(0, 0, 0) },
                    emissiveMap: { value: null },
                    hasEmissiveMap: { value: false },
                    emissiveIntensity: { value: 0.0 }
                },
                name: 'AOV_Emissive'
            });

            if (materialData && materialData.threeMaterial) {
                const srcMat = materialData.threeMaterial;
                material.uniforms.emissive.value.copy(srcMat.emissive || new THREE.Color(0, 0, 0));
                if (srcMat.emissiveMap) {
                    material.uniforms.emissiveMap.value = srcMat.emissiveMap;
                    material.uniforms.hasEmissiveMap.value = true;
                }
                material.uniforms.emissiveIntensity.value = srcMat.emissiveIntensity || 0.0;
            }
            break;

        default:
            return null;
    }

    return material;
}

// Apply AOV visualization to all meshes in the scene
function applyAOVMode(aovMode) {
    if (aovMode === AOV_MODES.NONE) {
        restoreOriginalMaterials();
        return;
    }

    // Store original materials if not already stored
    if (aovOriginalMaterials.size === 0) {
        scene.traverse((object) => {
            if (object.isMesh && object.material) {
                aovOriginalMaterials.set(object.uuid, object.material);
            }
        });
    }

    // Apply AOV materials to all meshes
    scene.traverse((object) => {
        if (object.isMesh && object.material) {
            // Find the material data if available
            let materialData = null;
            for (let mat of materials) {
                if (mat.threeMaterial === object.material ||
                    mat.threeMaterial === aovOriginalMaterials.get(object.uuid)) {
                    materialData = mat;
                    break;
                }
            }

            const aovMaterial = createAOVMaterial(aovMode, materialData);
            if (aovMaterial) {
                // Update depth uniforms if needed
                if (aovMode === AOV_MODES.DEPTH && aovMaterial.uniforms) {
                    aovMaterial.uniforms.cameraNear.value = camera.near;
                    aovMaterial.uniforms.cameraFar.value = camera.far;
                }
                object.material = aovMaterial;
            }
        }
    });

    currentAOVMode = aovMode;
    console.log(`AOV mode set to: ${aovMode}`);
}

// Restore original materials
function restoreOriginalMaterials() {
    scene.traverse((object) => {
        if (object.isMesh && aovOriginalMaterials.has(object.uuid)) {
            object.material = aovOriginalMaterials.get(object.uuid);
        }
    });

    aovOriginalMaterials.clear();
    currentAOVMode = AOV_MODES.NONE;
    console.log('AOV mode disabled, original materials restored');
}

// Set AOV mode (wrapper function for UI)
function setAOVMode(mode) {
    applyAOVMode(mode);
}

// Get color space index for shader uniform
function getColorSpaceIndex(colorSpaceName) {
    const mapping = {
        'srgb': 0,
        'linear': 1,
        'rec709': 2,
        'aces': 3,
        'acescg': 4
    };
    return mapping[colorSpaceName] || 0;
}

// Apply color space shader modifications to material
function applyColorSpaceShader(material, textureColorSpaces) {
    // Store original onBeforeCompile if exists
    const originalOnBeforeCompile = material.onBeforeCompile;

    material.onBeforeCompile = function(shader) {
        // Call original if it exists
        if (originalOnBeforeCompile) {
            originalOnBeforeCompile.call(this, shader);
        }

        // Add color space conversion functions to shader
        shader.fragmentShader = COLOR_SPACE_SHADER_FUNCTIONS + '\n' + shader.fragmentShader;

        // Add uniforms for each texture's color space
        const uniformsToAdd = {};

        // Disabled for a while.
        //if (textureColorSpaces.map !== undefined) {
        //    uniformsToAdd.mapColorSpace = { value: getColorSpaceIndex(textureColorSpaces.map) };
        //}
        if (textureColorSpaces.normalMap !== undefined) {
            uniformsToAdd.normalMapColorSpace = { value: getColorSpaceIndex(textureColorSpaces.normalMap) };
        }
        if (textureColorSpaces.roughnessMap !== undefined) {
            uniformsToAdd.roughnessMapColorSpace = { value: getColorSpaceIndex(textureColorSpaces.roughnessMap) };
        }
        if (textureColorSpaces.metalnessMap !== undefined) {
            uniformsToAdd.metalnessMapColorSpace = { value: getColorSpaceIndex(textureColorSpaces.metalnessMap) };
        }
        if (textureColorSpaces.emissiveMap !== undefined) {
            uniformsToAdd.emissiveMapColorSpace = { value: getColorSpaceIndex(textureColorSpaces.emissiveMap) };
        }

        // Add uniforms to shader
        shader.uniforms = Object.assign(shader.uniforms, uniformsToAdd);

        //// Modify shader code to apply color space conversions
        //// For base color map
        //if (textureColorSpaces.map !== undefined) {
        //    shader.fragmentShader = shader.fragmentShader.replace(
        //        '#include <map_fragment>',
        //        `
        //        #ifdef USE_MAP
        //            vec4 sampledDiffuseColor = texture2D( map, vMapUv );
        //            sampledDiffuseColor.rgb = convertColorSpace(sampledDiffuseColor.rgb, mapColorSpace);
        //            #ifdef DECODE_VIDEO_TEXTURE
        //                sampledDiffuseColor = vec4( mix( pow( sampledDiffuseColor.rgb * 0.9478672986 + vec3( 0.0521327014 ), vec3( 2.4 ) ), sampledDiffuseColor.rgb * 0.0773993808, vec3( lessThanEqual( sampledDiffuseColor.rgb, vec3( 0.04045 ) ) ) ), sampledDiffuseColor.w );
        //            #endif
        //            diffuseColor *= sampledDiffuseColor;
        //        #endif
        //        `
        //    );
        //}

        // For roughness map
        if (textureColorSpaces.roughnessMap !== undefined) {
            shader.fragmentShader = shader.fragmentShader.replace(
                '#include <roughnessmap_fragment>',
                `
                #ifdef USE_ROUGHNESSMAP
                    vec4 texelRoughness = texture2D( roughnessMap, vRoughnessMapUv );
                    texelRoughness.rgb = convertColorSpace(texelRoughness.rgb, roughnessMapColorSpace);
                    roughnessFactor *= texelRoughness.g;
                #endif
                `
            );
        }

        // For metalness map
        if (textureColorSpaces.metalnessMap !== undefined) {
            shader.fragmentShader = shader.fragmentShader.replace(
                '#include <metalnessmap_fragment>',
                `
                #ifdef USE_METALNESSMAP
                    vec4 texelMetalness = texture2D( metalnessMap, vMetalnessMapUv );
                    texelMetalness.rgb = convertColorSpace(texelMetalness.rgb, metalnessMapColorSpace);
                    metalnessFactor *= texelMetalness.b;
                #endif
                `
            );
        }

        // For emissive map
        if (textureColorSpaces.emissiveMap !== undefined) {
            shader.fragmentShader = shader.fragmentShader.replace(
                '#include <emissivemap_fragment>',
                `
                #ifdef USE_EMISSIVEMAP
                    vec4 emissiveColor = texture2D( emissiveMap, vEmissiveMapUv );
                    emissiveColor.rgb = convertColorSpace(emissiveColor.rgb, emissiveMapColorSpace);
                    totalEmissiveRadiance *= emissiveColor.rgb;
                #endif
                `
            );
        }

        // Store uniforms reference for later updates
        material.userData.colorSpaceUniforms = shader.uniforms;
    };

    // Mark material as needing shader recompilation
    material.needsUpdate = true;
}

// Update color space for a specific texture in the material
function updateTextureColorSpace(material, mapName, colorSpace) {
    if (!material.userData.colorSpaceSettings) {
        material.userData.colorSpaceSettings = {};
    }

    material.userData.colorSpaceSettings[mapName] = colorSpace;

    // Update uniform if shader is compiled
    if (material.userData.colorSpaceUniforms) {
        const uniformName = mapName + 'ColorSpace';
        if (material.userData.colorSpaceUniforms[uniformName]) {
            material.userData.colorSpaceUniforms[uniformName].value = getColorSpaceIndex(colorSpace);
        }
    } else {
        // Shader not compiled yet, reapply
        applyColorSpaceShader(material, material.userData.colorSpaceSettings);
    }
}

// Export material to JSON format
function exportMaterialToJSON(material) {
    const exportData = {
        materialName: material.name,
        exportDate: new Date().toISOString(),
        version: "1.0",
        hasOpenPBR: material.data?.hasOpenPBR || false,
        hasUsdPreviewSurface: material.data?.hasUsdPreviewSurface || false,
        openPBR: {},
        textures: {},
        colorSpace: {}
    };

    // Export OpenPBR parameters from the Three.js material
    const mat = material.threeMaterial;

    // Base layer
    exportData.openPBR.base = {
        color: [mat.color.r, mat.color.g, mat.color.b],
        metalness: mat.metalness,
        weight: 1.0
    };

    // Specular layer
    exportData.openPBR.specular = {
        roughness: mat.roughness,
        ior: mat.ior,
        weight: 1.0
    };

    if (mat.specularColor) {
        exportData.openPBR.specular.color = [
            mat.specularColor.r,
            mat.specularColor.g,
            mat.specularColor.b
        ];
    }

    // Transmission
    if (mat.transmission > 0) {
        exportData.openPBR.transmission = {
            weight: mat.transmission
        };
        if (mat.attenuationColor) {
            exportData.openPBR.transmission.color = [
                mat.attenuationColor.r,
                mat.attenuationColor.g,
                mat.attenuationColor.b
            ];
        }
    }

    // Coat (clearcoat)
    if (mat.clearcoat > 0) {
        exportData.openPBR.coat = {
            weight: mat.clearcoat,
            roughness: mat.clearcoatRoughness
        };
    }

    // Emission
    if (mat.emissive && (mat.emissive.r > 0 || mat.emissive.g > 0 || mat.emissive.b > 0)) {
        exportData.openPBR.emission = {
            color: [mat.emissive.r, mat.emissive.g, mat.emissive.b],
            intensity: mat.emissiveIntensity || 0.0
        };
    }

    // Geometry
    exportData.openPBR.geometry = {
        opacity: mat.opacity
    };

    // Export texture information
    if (mat.userData.textures) {
        Object.entries(mat.userData.textures).forEach(([mapName, texInfo]) => {
            const enabled = textureEnabled[material.index]?.[mapName] !== false;
            const colorSpace = mat.userData.colorSpaceSettings?.[mapName] || 'srgb';
            const uvSet = textureUVSet[material.index]?.[mapName] || 0;

            exportData.textures[mapName] = {
                textureId: texInfo.textureId,
                enabled: enabled,
                colorSpace: colorSpace,
                uvSet: uvSet,
                mapType: formatTextureName(mapName)
            };
        });
    }

    // Export color space settings
    if (mat.userData.colorSpaceSettings) {
        exportData.colorSpace = mat.userData.colorSpaceSettings;
    }

    return exportData;
}

// Export material to MaterialX XML (.mtlx) format
function exportMaterialToMaterialX(material) {
    const mat = material.threeMaterial;
    const materialName = material.name.replace(/[^a-zA-Z0-9_]/g, '_');

    let xml = '<?xml version="1.0"?>\n';
    xml += '<materialx version="1.38" xmlns:xi="http://www.w3.org/2001/XInclude">\n';
    xml += `  <!-- Exported from TinyUSDZ MaterialX Demo on ${new Date().toISOString()} -->\n\n`;

    // Create material definition
    xml += `  <surfacematerial name="${materialName}" type="material">\n`;
    xml += `    <input name="surfaceshader" type="surfaceshader" nodename="${materialName}_shader" />\n`;
    xml += '  </surfacematerial>\n\n';

    // Create OpenPBR Surface shader
    xml += `  <open_pbr_surface name="${materialName}_shader" type="surfaceshader">\n`;

    // Base color
    if (mat.map && textureEnabled[material.index]?.map !== false) {
        xml += `    <input name="base_color" type="color3" nodename="${materialName}_base_color_texture" />\n`;
    } else {
        xml += `    <input name="base_color" type="color3" value="${mat.color.r}, ${mat.color.g}, ${mat.color.b}" />\n`;
    }

    // Base weight
    xml += '    <input name="base_weight" type="float" value="1.0" />\n';

    // Metalness
    if (mat.metalnessMap && textureEnabled[material.index]?.metalnessMap !== false) {
        xml += `    <input name="base_metalness" type="float" nodename="${materialName}_metalness_texture" />\n`;
    } else {
        xml += `    <input name="base_metalness" type="float" value="${mat.metalness}" />\n`;
    }

    // Roughness
    if (mat.roughnessMap && textureEnabled[material.index]?.roughnessMap !== false) {
        xml += `    <input name="specular_roughness" type="float" nodename="${materialName}_roughness_texture" />\n`;
    } else {
        xml += `    <input name="specular_roughness" type="float" value="${mat.roughness}" />\n`;
    }

    // IOR
    xml += `    <input name="specular_ior" type="float" value="${mat.ior}" />\n`;

    // Transmission
    if (mat.transmission > 0) {
        xml += `    <input name="transmission_weight" type="float" value="${mat.transmission}" />\n`;
    }

    // Clearcoat
    if (mat.clearcoat > 0) {
        xml += `    <input name="coat_weight" type="float" value="${mat.clearcoat}" />\n`;
        xml += `    <input name="coat_roughness" type="float" value="${mat.clearcoatRoughness}" />\n`;
    }

    // Emission
    if (mat.emissive && (mat.emissive.r > 0 || mat.emissive.g > 0 || mat.emissive.b > 0)) {
        if (mat.emissiveMap && textureEnabled[material.index]?.emissiveMap !== false) {
            xml += `    <input name="emission_color" type="color3" nodename="${materialName}_emission_texture" />\n`;
        } else {
            xml += `    <input name="emission_color" type="color3" value="${mat.emissive.r}, ${mat.emissive.g}, ${mat.emissive.b}" />\n`;
        }
        xml += `    <input name="emission_luminance" type="float" value="${mat.emissiveIntensity || 1.0}" />\n`;
    }

    // Opacity
    if (mat.opacity < 1.0) {
        xml += `    <input name="geometry_opacity" type="float" value="${mat.opacity}" />\n`;
    }

    // Normal map
    if (mat.normalMap && textureEnabled[material.index]?.normalMap !== false) {
        xml += `    <input name="geometry_normal" type="vector3" nodename="${materialName}_normal_texture" />\n`;
    }

    xml += '  </open_pbr_surface>\n\n';

    // Add texture nodes
    if (mat.userData.textures) {
        Object.entries(mat.userData.textures).forEach(([mapName, texInfo]) => {
            const enabled = textureEnabled[material.index]?.[mapName] !== false;
            if (!enabled) return;

            const colorSpace = mat.userData.colorSpaceSettings?.[mapName] || 'srgb';
            const nodeName = `${materialName}_${mapName.replace('Map', '')}_texture`;

            // Determine output type based on map type
            let outputType = 'color3';
            let channels = 'rgb';

            if (mapName === 'roughnessMap' || mapName === 'metalnessMap' || mapName === 'aoMap') {
                outputType = 'float';
                channels = mapName === 'roughnessMap' ? 'g' : 'b';
            } else if (mapName === 'normalMap') {
                outputType = 'vector3';
                channels = 'rgb';
            }

            xml += `  <image name="${nodeName}" type="${outputType}">\n`;
            xml += `    <input name="file" type="filename" value="texture_${texInfo.textureId}.png" />\n`;
            xml += `    <input name="colorspace" type="string" value="${colorSpace}" />\n`;

            // Add UV set selection if specified
            const uvSet = textureUVSet[material.index]?.[mapName];
            if (uvSet !== undefined && uvSet > 0) {
                // Reference to UV coordinate node (would need to be defined separately)
                xml += `    <!-- Using UV set ${uvSet} for this texture -->\n`;
                xml += `    <input name="texcoord" type="vector2" value="0.0, 0.0" uiname="UV${uvSet}" />\n`;
            }

            if (outputType === 'float') {
                xml += `    <input name="channel" type="string" value="${channels}" />\n`;
            }
            xml += '  </image>\n\n';
        });
    }

    xml += '</materialx>\n';

    return xml;
}

// Download data as file
function downloadFile(content, filename, mimeType) {
    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
}

// Export current material to JSON
function exportCurrentMaterialJSON(material) {
    const jsonData = exportMaterialToJSON(material);
    const jsonString = JSON.stringify(jsonData, null, 2);
    const filename = `${material.name || 'material'}.json`;
    downloadFile(jsonString, filename, 'application/json');
    updateStatus(`Exported ${filename}`, 'success');
}

// Export current material to MaterialX XML
function exportCurrentMaterialMTLX(material) {
    const xmlData = exportMaterialToMaterialX(material);
    const filename = `${material.name || 'material'}.mtlx`;
    downloadFile(xmlData, filename, 'application/xml');
    updateStatus(`Exported ${filename}`, 'success');
}

// Global variable to track selected material for export
let selectedMaterialForExport = null;

// Export selected material to JSON (called from HTML button)
function exportSelectedMaterialJSON() {
    if (selectedMaterialForExport) {
        exportCurrentMaterialJSON(selectedMaterialForExport);
    } else {
        updateStatus('No material selected', 'error');
    }
}

// Export selected material to MaterialX (called from HTML button)
function exportSelectedMaterialMTLX() {
    if (selectedMaterialForExport) {
        exportCurrentMaterialMTLX(selectedMaterialForExport);
    } else {
        updateStatus('No material selected', 'error');
    }
}

// UsdPreviewSurface parameter definitions with ranges and defaults
const USDPREVIEWSURFACE_PARAMS = {
    diffuse: {
        diffuseColor: { default: [0.18, 0.18, 0.18], type: 'color' }
    },
    specular: {
        roughness: { min: 0, max: 1, default: 0.5, step: 0.01 },
        metallic: { min: 0, max: 1, default: 0, step: 0.01 },
        specularColor: { default: [1, 1, 1], type: 'color' },
        ior: { min: 1, max: 3, default: 1.5, step: 0.01 }
    },
    clearcoat: {
        clearcoat: { min: 0, max: 1, default: 0, step: 0.01 },
        clearcoatRoughness: { min: 0, max: 1, default: 0.01, step: 0.01 }
    },
    emission: {
        emissiveColor: { default: [0, 0, 0], type: 'color' }
    },
    geometry: {
        opacity: { min: 0, max: 1, default: 1, step: 0.01 },
        normal: { default: [0, 0, 1], type: 'vector' }
    },
    displacement: {
        displacement: { min: -10, max: 10, default: 0, step: 0.01 }
    },
    occlusion: {
        occlusion: { min: 0, max: 1, default: 1, step: 0.01 }
    }
};

// OpenPBR parameter definitions with ranges and defaults
const OPENPBR_PARAMS = {
    base: {
        weight: { min: 0, max: 1, default: 1, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        metalness: { min: 0, max: 1, default: 0, step: 0.01 },
        diffuse_roughness: { min: 0, max: 1, default: 0, step: 0.01 }
    },
    specular: {
        weight: { min: 0, max: 1, default: 1, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        roughness: { min: 0, max: 1, default: 0.3, step: 0.01 },
        ior: { min: 1, max: 3, default: 1.5, step: 0.01 },
        anisotropy: { min: 0, max: 1, default: 0, step: 0.01 },
        rotation: { min: 0, max: 1, default: 0, step: 0.01 }
    },
    transmission: {
        weight: { min: 0, max: 1, default: 0, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        depth: { min: 0, max: 100, default: 0, step: 0.1 },
        scatter: { default: [0, 0, 0], type: 'color' },
        scatter_anisotropy: { min: -1, max: 1, default: 0, step: 0.01 },
        dispersion: { min: 0, max: 100, default: 0, step: 0.1 }
    },
    subsurface: {
        weight: { min: 0, max: 1, default: 0, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        radius: { default: [1, 1, 1], type: 'color' },
        scale: { min: 0, max: 100, default: 1, step: 0.1 },
        anisotropy: { min: -1, max: 1, default: 0, step: 0.01 }
    },
    coat: {
        weight: { min: 0, max: 1, default: 0, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        roughness: { min: 0, max: 1, default: 0, step: 0.01 },
        anisotropy: { min: 0, max: 1, default: 0, step: 0.01 },
        rotation: { min: 0, max: 1, default: 0, step: 0.01 },
        ior: { min: 1, max: 3, default: 1.5, step: 0.01 },
        affect_color: { min: 0, max: 1, default: 0, step: 0.01 },
        affect_roughness: { min: 0, max: 1, default: 0, step: 0.01 }
    },
    thin_film: {
        thickness: { min: 0, max: 2000, default: 0, step: 10 },
        ior: { min: 1, max: 3, default: 1.5, step: 0.01 }
    },
    emission: {
        luminance: { min: 0, max: 10, default: 0, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        weight: { min: 0, max: 1, default: 1, step: 0.01 }
    },
    geometry: {
        opacity: { min: 0, max: 1, default: 1, step: 0.01 },
        thin_walled: { default: false, type: 'boolean' },
        normal: { default: [0, 0, 1], type: 'vector' },
        tangent: { default: [1, 0, 0], type: 'vector' }
    }
};

// Check for Display-P3 color space support
function checkDisplayP3Support() {
    // Check if the browser supports Display-P3
    if (window.matchMedia && window.matchMedia('(color-gamut: p3)').matches) {
        displayP3Supported = true;
        console.log('Display-P3 color space is supported');

        // Check if WebGL context supports Display-P3
        const canvas = document.createElement('canvas');
        const gl = canvas.getContext('webgl2', { colorSpace: 'display-p3' });
        if (gl && gl.drawingBufferColorSpace) {
            console.log('WebGL2 Display-P3 rendering is supported');
            return true;
        }
    }
    return false;
}

// Convert color from sRGB to Display-P3 (simplified linear conversion)
function srgbToDisplayP3(r, g, b) {
    // This is a simplified conversion - in production you'd use proper color matrices
    // Display-P3 uses the same white point as sRGB but has a wider gamut
    // For now, we'll just slightly enhance the saturation for demonstration
    if (currentColorSpace === 'display-p3') {
        // Enhance color vibrancy slightly for Display-P3
        const gray = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        const factor = 1.1; // Slight enhancement factor
        r = gray + (r - gray) * factor;
        g = gray + (g - gray) * factor;
        b = gray + (b - gray) * factor;
        // Clamp values
        r = Math.min(1, Math.max(0, r));
        g = Math.min(1, Math.max(0, g));
        b = Math.min(1, Math.max(0, b));
    }
    return [r, g, b];
}

// Create Three.js Color object with proper color space
function createColorWithSpace(r, g, b) {
    if (currentColorSpace === 'display-p3' && displayP3Supported) {
        const [r2, g2, b2] = srgbToDisplayP3(r, g, b);
        const color = new THREE.Color(r2, g2, b2);
        color.colorSpace = THREE.DisplayP3ColorSpace || THREE.SRGBColorSpace;
        return color;
    } else {
        const color = new THREE.Color(r, g, b);
        return color;
    }
}

// Get MIME type from texture image data
function getMimeTypeForTexture(imgData) {
    // Helper to get file extension
    const getFileExtension = (uri) => {
        if (!uri || typeof uri !== 'string') return '';
        const cleanUri = uri.split('?')[0].split('#')[0];
        const lastDotIndex = cleanUri.lastIndexOf('.');
        if (lastDotIndex === -1 || lastDotIndex === cleanUri.length - 1) {
            return '';
        }
        return cleanUri.substring(lastDotIndex + 1).toLowerCase();
    };

    // Try to determine from URI
    if (imgData.uri) {
        const ext = getFileExtension(imgData.uri);
        const mimeTypes = {
            'jpg': 'image/jpeg',
            'jpeg': 'image/jpeg',
            'png': 'image/png',
            'gif': 'image/gif',
            'webp': 'image/webp',
            'bmp': 'image/bmp',
            'hdr': 'image/vnd.radiance',
            'exr': 'image/x-exr'
        };
        if (mimeTypes[ext]) {
            return mimeTypes[ext];
        }
    }

    // Try to detect from magic bytes if available
    if (imgData.data) {
        const data = new Uint8Array(imgData.data);
        if (data.length >= 4) {
            // PNG magic bytes: 89 50 4E 47
            if (data[0] === 0x89 && data[1] === 0x50 && data[2] === 0x4E && data[3] === 0x47) {
                return 'image/png';
            }
            // JPEG magic bytes: FF D8 FF
            if (data[0] === 0xFF && data[1] === 0xD8 && data[2] === 0xFF) {
                return 'image/jpeg';
            }
            // WEBP magic bytes: 52 49 46 46 ... 57 45 42 50
            if (data[0] === 0x52 && data[1] === 0x49 && data[2] === 0x46 && data[3] === 0x46) {
                return 'image/webp';
            }
        }
    }

    // Default fallback
    return 'image/png';
}

// Load texture from USD data
async function loadTextureFromUSD(textureId) {
    console.log("loadTextureFromUSD ID:", textureId);
    // Validate texture ID
    if (!validateTextureId(textureId, 'loadTextureFromUSD')) {
        return null;
    }

    if (!currentNativeLoader) {
        console.error('loadTextureFromUSD: No USD loader available');
        return null;
    }

    // Check cache first
    if (textureCache.has(textureId)) {
        return textureCache.get(textureId);
    }

    try {
        console.log("Loading texture ID:", textureId);
        // Get texture metadata
        const texData = currentNativeLoader.getTexture(textureId);
        if (!texData || texData.textureImageId === undefined) {
            console.warn(`Texture ${textureId} has no image data`);
            return null;
        }

        // Validate image ID
        if (texData.textureImageId < 0) {
            console.warn(`Texture ${textureId} has invalid image ID: ${texData.textureImageId}`);
            return null;
        }

        // Get image data
        const imgData = currentNativeLoader.getImage(texData.textureImageId);
        if (!imgData) {
            console.warn(`Image ${texData.textureImageId} not found`);
            return null;
        }

        console.log(`Loading texture ${textureId}: uri="${imgData.uri}", bufferId=${imgData.bufferId}, decoded=${imgData.decoded}, hasData=${!!imgData.data}`);

        // Handle 3 cases: URI only, embedded undecoded, and decoded
        // Case 1: URI only (need to fetch from external file)
        if (imgData.uri && (imgData.bufferId === -1 || imgData.bufferId === undefined)) {
            console.log(`Loading texture from URI: ${imgData.uri}`);
            const loader = new THREE.TextureLoader();

            try {
                const texture = await loader.loadAsync(imgData.uri);
                // TODO: Temporarily disabled - causes shader error
                // texture.colorSpace = THREE.SRGBColorSpace;
                textureCache.set(textureId, texture);
                console.log(`Successfully loaded texture ${textureId} from URI`);
                return texture;
            } catch (error) {
                console.error(`Failed to load texture ${textureId} from URI: ${imgData.uri}`, error);
                return null;
            }
        }

        // Case 2 & 3: Embedded texture (either decoded or needs decoding)
        if (imgData.bufferId >= 0 && imgData.data) {
            if (imgData.decoded) {
                // Case 3: Already decoded - use DataTexture
                console.log(`Creating DataTexture for texture ${textureId}: ${imgData.width}x${imgData.height}, ${imgData.channels} channels`);

                const image8Array = new Uint8ClampedArray(imgData.data);
                const texture = new THREE.DataTexture(image8Array, imgData.width, imgData.height);

                if (imgData.channels === 1) {
                    texture.format = THREE.RedFormat;
                } else if (imgData.channels === 2) {
                    texture.format = THREE.RGFormat;
                } else if (imgData.channels === 3) {
                    console.error(`RGB format (3 channels) is not supported in recent Three.js. Texture ${textureId} will not display correctly.`);
                    return null;
                } else if (imgData.channels === 4) {
                    texture.format = THREE.RGBAFormat;
                } else {
                    console.error(`Unsupported image channels: ${imgData.channels}`);
                    return null;
                }

                texture.flipY = true;
                texture.needsUpdate = true;
                // TODO: Temporarily disabled - causes shader error
                // texture.colorSpace = THREE.SRGBColorSpace;

                textureCache.set(textureId, texture);
                console.log(`Loaded decoded texture ${textureId}`);
                return texture;

            } else {
                // Case 2: Embedded but not decoded - create Blob URL and load
                console.log(`Creating Blob for undecoded texture ${textureId}`);

                const mimeType = getMimeTypeForTexture(imgData);
                const blob = new Blob([imgData.data], { type: mimeType });
                const blobUrl = URL.createObjectURL(blob);

                const loader = new THREE.TextureLoader();
                try {
                    const texture = await loader.loadAsync(blobUrl);
                    // TODO: Temporarily disabled - causes shader error
                    // texture.colorSpace = THREE.SRGBColorSpace;
                    textureCache.set(textureId, texture);
                    URL.revokeObjectURL(blobUrl); // Clean up blob URL
                    console.log(`Successfully loaded texture ${textureId} from Blob`);
                    return texture;
                } catch (error) {
                    URL.revokeObjectURL(blobUrl);
                    console.error(`Failed to load texture ${textureId} from Blob`, error);
                    return null;
                }
            }
        }

        console.warn(`Texture ${textureId} has invalid state`);
        return null;

    } catch (error) {
        reportError('loadTextureFromUSD', error);
        return null;
    }
}

// Apply texture to material parameter
function applyTextureToMaterial(material, paramName, texture, isEnabled = true) {
    if (!texture || !isEnabled) {
        // Clear texture
        material[paramName] = null;
        return;
    }

    material[paramName] = texture;
    material.needsUpdate = true;
}

// Map OpenPBR parameter names to Three.js texture map names
function getMapNameForParameter(groupName, paramName) {
    const paramKey = `${groupName}_${paramName}`;
    const mapping = {
        'base_color': 'map',
        'specular_roughness': 'roughnessMap',
        'geometry_normal': 'normalMap',
        'coat_normal': 'clearcoatNormalMap',
        'emission_color': 'emissiveMap',
        'base_metalness': 'metalnessMap'
    };
    return mapping[paramKey] || null;
}

// Get texture info for a material parameter (from userData.textures)
function getTextureInfoForParameter(material, groupName, paramName) {
    if (!material || !material.threeMaterial || !material.threeMaterial.userData.textures) {
        return null;
    }

    const mapName = getMapNameForParameter(groupName, paramName);
    if (!mapName) {
        return null;
    }

    const texInfo = material.threeMaterial.userData.textures[mapName];
    if (texInfo) {
        return {
            ...texInfo,
            mapName: mapName
        };
    }

    return null;
}

// Get texture info for a material parameter
function getTextureInfo(materialData, category, paramName) {
    if (!materialData || !materialData.hasOpenPBR || !materialData.openPBR) {
        return null;
    }

    const params = materialData.openPBR[category];
    if (!params || !params[paramName]) {
        return null;
    }

    const param = params[paramName];

    // Check if parameter has a texture
    if (param.textureId !== undefined && param.textureId >= 0) {
        return {
            textureId: param.textureId,
            value: param.value || param,
            hasTexture: true
        };
    }

    return null;
}

// Initialize the application
async function init() {
    updateStatus('Initializing Three.js scene...');

    // Check for Display-P3 support
    displayP3Supported = checkDisplayP3Support();

    // Show color space toggle button if Display-P3 is supported
    if (displayP3Supported) {
        const colorSpaceBtn = document.getElementById('color-space-btn');
        if (colorSpaceBtn) {
            colorSpaceBtn.style.display = 'inline-block';
            colorSpaceBtn.textContent = 'Use P3';
        }
    }

    // Setup Three.js scene
    setupScene();

    // Setup interaction
    setupInteraction();

    // Setup post-processing composer
    setupComposer();

    // Load TinyUSDZ WASM module
    await loadTinyUSDZ();

    // Create synthetic HDR environment
    createSyntheticHDR('studio');

    // Setup GUI
    setupGUI();

    // Setup file input
    setupFileInput();

    // Initialize node graph system
    if (initializeNodeGraph()) {
        registerMaterialXNodeTypes();
        console.log('Node graph system initialized');
    }

    // Initialize color picker
    initializeColorPicker(renderer);
    console.log('Color picker initialized');

    // Initialize material property picker
    initializeMaterialPropertyPicker(renderer, scene, camera);
    console.log('Material property picker initialized');

    // Load embedded default scene
    await loadEmbeddedScene();

    updateStatus('Ready. Load a USD file or click "Load Sample"', 'success');
}

// Setup Three.js scene
function setupScene() {
    const container = document.getElementById('canvas-container');

    // Scene
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x1a1a1a);

    // Camera
    camera = new THREE.PerspectiveCamera(
        45,
        window.innerWidth / window.innerHeight,
        0.1,
        1000
    );
    camera.position.set(0, 2, 5);
    camera.lookAt(0, 0, 0);

    // Renderer with color space configuration
    const rendererConfig = {
        antialias: true,
        // Set color space based on support and current setting
        colorSpace: (displayP3Supported && currentColorSpace === 'display-p3') ? 'display-p3' : 'srgb'
    };

    renderer = new THREE.WebGLRenderer(rendererConfig);
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.toneMapping = getToneMappingConstant(toneMappingType);
    renderer.toneMappingExposure = 1;

    // Set output color space based on current setting
    if (displayP3Supported && currentColorSpace === 'display-p3') {
        // Use Display-P3 color space
        renderer.outputColorSpace = THREE.DisplayP3ColorSpace || THREE.SRGBColorSpace;
        console.log('Using Display-P3 output color space');
    } else {
        // Use sRGB color space (default)
        renderer.outputColorSpace = THREE.SRGBColorSpace;
        console.log('Using sRGB output color space');
    }

    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    container.appendChild(renderer.domElement);

    // PMREM Generator for environment maps
    pmremGenerator = new THREE.PMREMGenerator(renderer);
    pmremGenerator.compileEquirectangularShader();

    // Controls
    controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.screenSpacePanning = false;
    controls.minDistance = 0.5;
    controls.maxDistance = 50;

    // Lights (basic setup, will be enhanced with HDR)
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.3);
    scene.add(ambientLight);

    const directionalLight = new THREE.DirectionalLight(0xffffff, 0.7);
    directionalLight.position.set(5, 5, 5);
    directionalLight.castShadow = true;
    directionalLight.shadow.camera.near = 0.1;
    directionalLight.shadow.camera.far = 50;
    directionalLight.shadow.camera.left = -10;
    directionalLight.shadow.camera.right = 10;
    directionalLight.shadow.camera.top = 10;
    directionalLight.shadow.camera.bottom = -10;
    directionalLight.shadow.mapSize.width = 2048;
    directionalLight.shadow.mapSize.height = 2048;
    scene.add(directionalLight);

    // Grid helper
    const gridHelper = new THREE.GridHelper(10, 10, 0x444444, 0x222222);
    scene.add(gridHelper);

    // Create scene root for USD content (for upAxis conversion)
    sceneRoot = new THREE.Group();
    sceneRoot.name = 'USD_Scene_Root';
    scene.add(sceneRoot);

    // Handle window resize
    window.addEventListener('resize', onWindowResize, false);

    // Start render loop
    animate();
}

// Setup interaction for object selection
function setupInteraction() {
    raycaster = new THREE.Raycaster();
    mouse = new THREE.Vector2();

    renderer.domElement.addEventListener('click', onMouseClick, false);
    renderer.domElement.addEventListener('mousemove', onMouseMove, false);
}

// Setup post-processing composer with false color and ACES tonemap effects
function setupComposer() {
    // Create effect composer
    composer = new EffectComposer(renderer);

    // Add render pass (main scene render)
    const renderPass = new RenderPass(scene, camera);
    composer.addPass(renderPass);

    // Add ACES tonemap pass (for custom ACES 1.3 / 2.0 tonemapping)
    acesTonemapPass = new ShaderPass(ACESTonemapShader);
    acesTonemapPass.enabled = false; // Will be enabled when ACES 1.3/2.0 is selected
    acesTonemapPass.uniforms['exposure'].value = exposureValue;
    composer.addPass(acesTonemapPass);

    // Add false color pass (initially disabled via renderToScreen)
    falseColorPass = new ShaderPass(FalseColorShader);
    falseColorPass.enabled = false; // Will be enabled when false color is toggled
    composer.addPass(falseColorPass);

    console.log('Effect composer initialized with ACES tonemap and false color passes');
}

// Toggle false color view transform
function toggleFalseColor() {
    showingFalseColor = !showingFalseColor;

    if (!composer) {
        setupComposer();
    }

    if (falseColorPass) {
        falseColorPass.enabled = showingFalseColor;
        // When false color is enabled, make it render to screen
        // When disabled, check if ACES pass should render to screen
        falseColorPass.renderToScreen = showingFalseColor;

        // Update ACES pass renderToScreen
        if (acesTonemapPass && acesTonemapPass.enabled) {
            acesTonemapPass.renderToScreen = !showingFalseColor;
        }

        // Update the render pass
        if (composer.passes[0]) {
            // Render pass should render to screen only if no post-processing is active
            const hasActivePostProcess = showingFalseColor ||
                                         (acesTonemapPass && acesTonemapPass.enabled);
            composer.passes[0].renderToScreen = !hasActivePostProcess;
        }
    }

    console.log('False color:', showingFalseColor ? 'enabled' : 'disabled');
}

// Load TinyUSDZ WASM module
async function loadTinyUSDZ() {
    updateStatus('Loading TinyUSDZ WASM module...');

    try {
        // Create a TinyUSDZLoader instance
        currentLoader = new TinyUSDZLoader();

        // Initialize the loader (wait for WASM module to load)
        // Use memory64: false for browser compatibility
        // Use useZstdCompressedWasm: false since compressed WASM is not available
        await currentLoader.init({ useZstdCompressedWasm: false, useMemory64: false });

        console.log('TinyUSDZ module loaded successfully');
        updateStatus('TinyUSDZ module loaded', 'success');
    } catch (error) {
        console.error('Failed to initialize TinyUSDZ:', error);
        updateStatus('Failed to load TinyUSDZ: ' + error.message, 'error');
        throw error;
    }
}

// Get Three.js tone mapping constant from string
function getToneMappingConstant(type) {
    switch (type) {
        case 'none':
            return THREE.NoToneMapping;
        case 'aces':
            return THREE.ACESFilmicToneMapping;
        case 'aces13':
        case 'aces20':
            // Custom ACES tonemapping handled via post-processing
            // Use NoToneMapping for renderer, post-process will handle it
            return THREE.NoToneMapping;
        case 'agx':
            // AgX was added in Three.js r152
            return THREE.AgXToneMapping || THREE.ACESFilmicToneMapping;
        case 'neutral':
            // Neutral was added in Three.js r155
            return THREE.NeutralToneMapping || THREE.LinearToneMapping;
        case 'linear':
            return THREE.LinearToneMapping;
        case 'reinhard':
            return THREE.ReinhardToneMapping;
        case 'cineon':
            return THREE.CineonToneMapping;
        default:
            return THREE.NoToneMapping;
    }
}

// Apply custom tonemapping if needed (for ACES 1.3/2.0)
function applyCustomToneMapping(type) {
    if (!composer || !acesTonemapPass) {
        setupComposer();
    }

    // Disable ACES pass by default
    if (acesTonemapPass) {
        acesTonemapPass.enabled = false;
        acesTonemapPass.renderToScreen = false;
    }

    // Enable custom ACES tonemapping if needed
    if (type === 'aces13' || type === 'aces20') {
        if (acesTonemapPass) {
            acesTonemapPass.enabled = true;
            // ACES should render to screen if false color is not enabled
            acesTonemapPass.renderToScreen = !showingFalseColor;

            // Set ACES version: 0 = ACES 1.3, 1 = ACES 2.0
            acesTonemapPass.uniforms['acesVersion'].value = type === 'aces20' ? 1 : 0;
            acesTonemapPass.uniforms['exposure'].value = exposureValue;

            console.log(`Enabled custom ${type.toUpperCase()} tonemapping via post-processing`);
        }
    } else {
        // For built-in tonemappers, disable ACES pass
        if (acesTonemapPass) {
            acesTonemapPass.enabled = false;
        }
    }

    // Update render pass renderToScreen based on active post-processing
    if (composer && composer.passes[0]) {
        const needsComposer = (type === 'aces13' || type === 'aces20') || showingFalseColor;
        composer.passes[0].renderToScreen = !needsComposer;
    }
}

// Load HDR environment map from file
function loadHDREnvironment(filepath) {
    const loader = new RGBELoader();

    loader.load(filepath, (texture) => {
        texture.mapping = THREE.EquirectangularReflectionMapping;

        // Generate environment map
        const renderTarget = pmremGenerator.fromEquirectangular(texture);
        scene.environment = renderTarget.texture;

        // Conditionally set as background based on toggle state
        if (showBackgroundEnvmap) {
            scene.background = renderTarget.texture;
        } else {
            scene.background = new THREE.Color(0x1a1a1a);
        }

        texture.dispose();
        console.log(`Loaded HDR environment: ${filepath}`);
    }, undefined, (error) => {
        console.error('Error loading HDR environment:', error);
        updateStatus('Failed to load HDR environment: ' + filepath, 'error');
    });
}

// Create synthetic HDR environment map
function createSyntheticHDR(type) {
    environmentType = type;

    // Check if this is a file-based HDR environment
    if (type === 'goegap_1k' || type === 'env_sunsky_sunset') {
        const filepath = `./assets/textures/${type}.hdr`;
        loadHDREnvironment(filepath);
        return;
    }

    // Create a canvas to generate the HDR texture
    const size = 512;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');

    if (type === 'white') {
        // All white environment
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, size, size);
    } else if (type === 'studio') {
        // Studio lighting gradient
        const gradient = ctx.createLinearGradient(0, 0, 0, size);
        gradient.addColorStop(0, '#ffffff');
        gradient.addColorStop(0.3, '#f0f0f0');
        gradient.addColorStop(0.5, '#e0e0e0');
        gradient.addColorStop(0.7, '#d0d0d0');
        gradient.addColorStop(1, '#a0a0a0');
        ctx.fillStyle = gradient;
        ctx.fillRect(0, 0, size, size);

        // Add some soft spots for key lighting
        ctx.globalCompositeOperation = 'lighter';

        // Key light
        const keyGradient = ctx.createRadialGradient(size * 0.3, size * 0.3, 0, size * 0.3, size * 0.3, size * 0.3);
        keyGradient.addColorStop(0, 'rgba(255, 255, 255, 0.3)');
        keyGradient.addColorStop(1, 'rgba(255, 255, 255, 0)');
        ctx.fillStyle = keyGradient;
        ctx.fillRect(0, 0, size, size);

        // Fill light
        const fillGradient = ctx.createRadialGradient(size * 0.7, size * 0.5, 0, size * 0.7, size * 0.5, size * 0.4);
        fillGradient.addColorStop(0, 'rgba(240, 240, 255, 0.2)');
        fillGradient.addColorStop(1, 'rgba(240, 240, 255, 0)');
        ctx.fillStyle = fillGradient;
        ctx.fillRect(0, 0, size, size);
    }

    // Create texture from canvas
    const texture = new THREE.CanvasTexture(canvas);
    texture.mapping = THREE.EquirectangularReflectionMapping;
    texture.encoding = THREE.sRGBEncoding;

    // Generate environment map
    const renderTarget = pmremGenerator.fromEquirectangular(texture);
    scene.environment = renderTarget.texture;

    // Conditionally set as background based on toggle state
    if (showBackgroundEnvmap) {
        scene.background = renderTarget.texture;
    } else {
        scene.background = new THREE.Color(0x1a1a1a);
    }

    texture.dispose();
}

// Toggle between HDR environments
function toggleEnvironment() {
    const envTypes = ['studio', 'white', 'goegap_1k', 'env_sunsky_sunset'];
    const currentIndex = envTypes.indexOf(environmentType);
    const nextIndex = (currentIndex + 1) % envTypes.length;
    createSyntheticHDR(envTypes[nextIndex]);
}

// Toggle environment map as background
function toggleBackgroundEnvmap() {
    showBackgroundEnvmap = !showBackgroundEnvmap;
    // Reapply the current environment to update the background
    createSyntheticHDR(environmentType);
    console.log(`Background envmap: ${showBackgroundEnvmap ? 'ON' : 'OFF'}`);
}

// Toggle color space (for quick switching via button)
function toggleColorSpace() {
    if (!displayP3Supported) {
        updateStatus('Display-P3 is not supported on this device', 'error');
        return;
    }

    const newColorSpace = currentColorSpace === 'srgb' ? 'display-p3' : 'srgb';
    const success = switchColorSpace(newColorSpace);

    if (success) {
        updateStatus(`Switched to ${newColorSpace.toUpperCase()} color space`, 'success');

        // Update button text
        const btn = document.getElementById('color-space-btn');
        if (btn) {
            btn.textContent = currentColorSpace === 'display-p3' ? 'Use sRGB' : 'Use P3';
        }
    }
}

// Switch color space
function switchColorSpace(colorSpace) {
    if (!displayP3Supported && colorSpace === 'display-p3') {
        console.warn('Display-P3 is not supported on this device/browser');
        return false;
    }

    currentColorSpace = colorSpace;

    // Update renderer output color space
    if (colorSpace === 'display-p3' && displayP3Supported) {
        renderer.outputColorSpace = THREE.DisplayP3ColorSpace || THREE.SRGBColorSpace;

        // Update WebGL context if possible
        if (renderer.domElement && renderer.domElement.getContext) {
            try {
                const gl = renderer.domElement.getContext('webgl2');
                if (gl && gl.drawingBufferColorSpace) {
                    gl.drawingBufferColorSpace = 'display-p3';
                }
            } catch (e) {
                console.warn('Could not update WebGL context color space:', e);
            }
        }
        console.log('Switched to Display-P3 color space');
    } else {
        renderer.outputColorSpace = THREE.SRGBColorSpace;

        // Update WebGL context if possible
        if (renderer.domElement && renderer.domElement.getContext) {
            try {
                const gl = renderer.domElement.getContext('webgl2');
                if (gl && gl.drawingBufferColorSpace) {
                    gl.drawingBufferColorSpace = 'srgb';
                }
            } catch (e) {
                console.warn('Could not update WebGL context color space:', e);
            }
        }
        console.log('Switched to sRGB color space');
    }

    // Update materials to ensure proper color space conversion
    materials.forEach(material => {
        if (material.threeMaterial) {
            material.threeMaterial.needsUpdate = true;
        }
    });

    // Re-render scene
    renderer.render(scene, camera);

    return true;
}

// Toggle normal material for debugging
function toggleNormalMaterial(show) {
    showingNormals = show;

    meshes.forEach(mesh => {
        if (show) {
            // Store original material if not already stored
            if (!originalMaterials.has(mesh.uuid)) {
                originalMaterials.set(mesh.uuid, mesh.material);
            }
            // Apply normal material
            mesh.material = new THREE.MeshNormalMaterial({
                flatShading: false,
                side: THREE.DoubleSide
            });
        } else {
            // Restore original material
            if (originalMaterials.has(mesh.uuid)) {
                mesh.material = originalMaterials.get(mesh.uuid);
            }
        }
    });

    console.log(`Normal material ${show ? 'enabled' : 'disabled'}`);
}

// Apply upAxis conversion to scene
function applyUpAxisConversionToScene() {
    console.log(`=== applyUpAxisConversionToScene() called ===`);
    console.log(`  applyUpAxisConversion: ${applyUpAxisConversion}`);
    console.log(`  currentFileUpAxis: "${currentFileUpAxis}"`);
    console.log(`  currentFileUpAxis === 'Z': ${currentFileUpAxis === 'Z'}`);
    console.log(`  sceneRoot exists: ${!!sceneRoot}`);
    console.log(`  sceneRoot children count: ${sceneRoot ? sceneRoot.children.length : 'N/A'}`);

    if (!sceneRoot) {
        console.error(`ERROR: sceneRoot is null or undefined - cannot apply upAxis conversion`);
        return;
    }

    console.log(`  Current sceneRoot rotation BEFORE applying conversion:`);
    console.log(`    x=${sceneRoot.rotation.x}, y=${sceneRoot.rotation.y}, z=${sceneRoot.rotation.z}`);

    if (applyUpAxisConversion && currentFileUpAxis === 'Z') {
        // Apply Z-up to Y-up conversion (-90 degrees around X axis)
        console.log(`  -> Applying Z-up to Y-up conversion...`);
        sceneRoot.rotation.x = -Math.PI / 2;
        sceneRoot.rotation.y = 0;
        sceneRoot.rotation.z = 0;
        console.log(`  ✓ Applied Z-up to Y-up conversion (file upAxis="${currentFileUpAxis}")`);
        console.log(`    sceneRoot.rotation AFTER: x=${sceneRoot.rotation.x.toFixed(4)}, y=${sceneRoot.rotation.y.toFixed(4)}, z=${sceneRoot.rotation.z.toFixed(4)}`);
        console.log(`    sceneRoot has ${sceneRoot.children.length} children`);
    } else {
        // Reset rotation (either disabled or file is already Y-up)
        console.log(`  -> No conversion needed or disabled`);
        sceneRoot.rotation.x = 0;
        sceneRoot.rotation.y = 0;
        sceneRoot.rotation.z = 0;
        if (currentFileUpAxis !== 'Z') {
            console.log(`  ✓ No rotation needed (file upAxis="${currentFileUpAxis}" is already Y-up compatible)`);
        } else {
            console.log(`  ○ Reset rotation (conversion disabled, file is Z-up but conversion is off)`);
        }
        console.log(`    sceneRoot.rotation: x=${sceneRoot.rotation.x}, y=${sceneRoot.rotation.y}, z=${sceneRoot.rotation.z}`);
    }
}

// Toggle upAxis conversion
function toggleUpAxisConversion() {
    applyUpAxisConversion = !applyUpAxisConversion;
    applyUpAxisConversionToScene();
}

// Setup GUI for OpenPBR parameters
function setupGUI() {
    if (gui) {
        gui.destroy();
    }

    gui = new GUI({ width: 350 });
    gui.domElement.style.position = 'absolute';
    gui.domElement.style.top = '150px';
    gui.domElement.style.right = '10px';
    gui.domElement.style.maxHeight = 'calc(100vh - 160px)';
    gui.domElement.style.overflowY = 'auto';

    // Color Space controls
    const colorFolder = gui.addFolder('Color Space');
    const colorParams = {
        colorSpace: currentColorSpace,
        supported: displayP3Supported ? 'Display-P3 Supported ✓' : 'sRGB Only'
    };

    // Show support status (read-only)
    colorFolder.add(colorParams, 'supported').name('Status').listen();

    // Color space selector (only show if Display-P3 is supported)
    if (displayP3Supported) {
        colorFolder.add(colorParams, 'colorSpace', ['srgb', 'display-p3'])
            .name('Output Space')
            .onChange(value => {
                const success = switchColorSpace(value);
                if (!success) {
                    colorParams.colorSpace = 'srgb';
                    updateStatus('Display-P3 not available on this device', 'error');
                } else {
                    updateStatus(`Switched to ${value.toUpperCase()} color space`, 'success');
                }
            });

        // Add info about the current color gamut
        const gamutInfo = {
            info: 'Wider gamut for more vibrant colors'
        };
        colorFolder.add(gamutInfo, 'info').name('P3 Benefit');
    } else {
        // Show message when Display-P3 is not supported
        const noP3Info = {
            info: 'Upgrade to a P3 display for wider gamut'
        };
        colorFolder.add(noP3Info, 'info').name('Note');
    }

    colorFolder.open();

    // Environment controls
    const envFolder = gui.addFolder('Environment');
    const envParams = {
        type: environmentType,
        exposure: 1,
        background: '#1a1a1a',
        showEnvmapBg: showBackgroundEnvmap,
        toneMapping: toneMappingType
    };

    envFolder.add(envParams, 'type', ['studio', 'white', 'goegap_1k', 'env_sunsky_sunset']).onChange(value => {
        createSyntheticHDR(value);
    });

    envFolder.add(envParams, 'toneMapping', {
        'None': 'none',
        'AgX (Blender)': 'agx',
        'ACES 1.3 (Blender 4.x)': 'aces13',
        'ACES 2.0 OpenDRT (Blender 5.0)': 'aces20',
        'ACES Filmic (Three.js)': 'aces',
        'Neutral': 'neutral',
        'Linear': 'linear',
        'Reinhard': 'reinhard',
        'Cineon': 'cineon'
    }).name('Tone Mapping').onChange(value => {
        toneMappingType = value;
        renderer.toneMapping = getToneMappingConstant(value);
        applyCustomToneMapping(value);
    });

    envFolder.add(envParams, 'exposure', 0, 3, 0.01).name('Exposure').onChange(value => {
        exposureValue = value;
        renderer.toneMappingExposure = value;

        // Update ACES tonemap pass exposure if active
        if (acesTonemapPass && acesTonemapPass.enabled) {
            acesTonemapPass.uniforms['exposure'].value = value;
        }

        // Apply exposure to all materials by adjusting environment intensity
        scene.traverse((object) => {
            if (object.isMesh && object.material) {
                const material = object.material;
                if (material.envMapIntensity !== undefined) {
                    material.envMapIntensity = value;
                }
            }
        });
    });

    envFolder.add(envParams, 'showEnvmapBg').name('Show Envmap as BG').onChange(value => {
        showBackgroundEnvmap = value;
        createSyntheticHDR(environmentType);
    });

    envFolder.addColor(envParams, 'background').name('Solid BG Color').onChange(value => {
        if (!showBackgroundEnvmap) {
            scene.background = new THREE.Color(value);
        }
    });

    envFolder.open();

    // Debug/Visualization controls
    const debugFolder = gui.addFolder('Debug');
    const debugParams = {
        showNormals: false,
        applyUpAxisConversion: applyUpAxisConversion,
        fitToScene: function() {
            fitCameraToScene();
        }
    };

    debugFolder.add(debugParams, 'showNormals')
        .name('Show Normals')
        .onChange(value => {
            toggleNormalMaterial(value);
        });

    debugFolder.add(debugParams, 'applyUpAxisConversion')
        .name('Z-up to Y-up')
        .onChange(value => {
            applyUpAxisConversion = value;
            toggleUpAxisConversion();
        });

    debugFolder.add(debugParams, 'fitToScene')
        .name('Fit to Scene');

    debugFolder.close();

    // Material Rendering controls
    const materialFolder = gui.addFolder('Material Rendering');
    const materialParams = {
        preferredType: preferredMaterialType,
        useNodeMaterial: useNodeMaterial,
        reloadMaterials: async function() {
            console.log("Reloading materials with new settings...");
            await loadMaterials();
        }
    };

    materialFolder.add(materialParams, 'preferredType', {
        'Auto (Prefer OpenPBR)': 'auto',
        'OpenPBR/MaterialX': 'openpbr',
        'UsdPreviewSurface': 'usdpreviewsurface'
    }).name('Material Type').onChange(async (value) => {
        preferredMaterialType = value;
        console.log(`Preferred material type changed to: ${value}`);
        await loadMaterials();
    });

    materialFolder.add(materialParams, 'useNodeMaterial').name('Use NodeMaterial (MaterialX)').onChange(async (value) => {
        useNodeMaterial = value;
        console.log(`Material type changed to: ${value ? 'NodeMaterial (via MaterialXLoader)' : 'MeshPhysicalMaterial'}`);
        await loadMaterials();
    });

    materialFolder.add(materialParams, 'reloadMaterials').name('🔄 Reload Materials');
    materialFolder.open();

    // AOV (Arbitrary Output Variable) controls
    const aovFolder = gui.addFolder('AOV Visualization');
    const aovParams = {
        mode: currentAOVMode
    };

    aovFolder.add(aovParams, 'mode', {
        'None (Material)': AOV_MODES.NONE,
        '─── Geometry ───': '',
        'World Normals': AOV_MODES.NORMALS_WORLD,
        'View Normals': AOV_MODES.NORMALS_VIEW,
        'Tangents': AOV_MODES.TANGENTS,
        'Binormals': AOV_MODES.BINORMALS,
        'UV Coords 0': AOV_MODES.TEXCOORD_0,
        'UV Coords 1': AOV_MODES.TEXCOORD_1,
        'World Position': AOV_MODES.POSITION_WORLD,
        'View Position': AOV_MODES.POSITION_VIEW,
        'Depth': AOV_MODES.DEPTH,
        '─── Material ───': '',
        'Albedo': AOV_MODES.ALBEDO,
        'Roughness': AOV_MODES.ROUGHNESS,
        'Metalness': AOV_MODES.METALNESS,
        'Specular': AOV_MODES.SPECULAR,
        'Coat': AOV_MODES.COAT,
        'Transmission': AOV_MODES.TRANSMISSION,
        'Emissive': AOV_MODES.EMISSIVE,
        '─── Utility ───': '',
        'Material ID': AOV_MODES.MATERIAL_ID
    }).name('AOV Mode').onChange(value => {
        if (value === '') return; // Ignore separator selections
        setAOVMode(value);
        aovParams.mode = value;
    });

    aovFolder.close();
}

// Load USD file
async function loadUSDFile(arrayBuffer, filename) {
    if (!currentLoader) {
        updateStatus('TinyUSDZ module not loaded', 'error');
        return;
    }

    showLoading(true);
    updateStatus(`Loading ${filename}...`);

    try {
        // Clean up previous native loader
        if (currentNativeLoader) {
            currentNativeLoader.delete();
            currentNativeLoader = null;
        }

        // Clear the scene
        clearScene();

        // Create new native loader from the TinyUSDZLoader instance
        currentNativeLoader = new currentLoader.native_.TinyUSDZLoaderNative();

        // Convert ArrayBuffer to Uint8Array
        const uint8Array = new Uint8Array(arrayBuffer);

        // Load the USD file
        const success = currentNativeLoader.loadFromBinary(uint8Array, filename);

        if (!success) {
            throw new Error('Failed to parse USD file');
        }

        // Get scene metadata (including upAxis)
        console.log(`=== Extracting scene metadata ===`);
        const sceneMetadata = currentNativeLoader.getSceneMetadata ? currentNativeLoader.getSceneMetadata() : {};
        console.log(`Raw sceneMetadata:`, sceneMetadata);
        console.log(`sceneMetadata.upAxis type:`, typeof sceneMetadata.upAxis);
        console.log(`sceneMetadata.upAxis value:`, sceneMetadata.upAxis);
        console.log(`sceneMetadata.upAxis === 'Z':`, sceneMetadata.upAxis === 'Z');
        console.log(`sceneMetadata.upAxis === 'Y':`, sceneMetadata.upAxis === 'Y');
        currentFileUpAxis = sceneMetadata.upAxis || 'Y';
        console.log(`Set currentFileUpAxis to: "${currentFileUpAxis}"`);
        console.log(`currentFileUpAxis type:`, typeof currentFileUpAxis);
        console.log(`currentFileUpAxis === 'Z':`, currentFileUpAxis === 'Z');

        // Store complete scene metadata
        currentSceneMetadata = {
            upAxis: currentFileUpAxis,
            metersPerUnit: sceneMetadata.metersPerUnit || 1.0,
            framesPerSecond: sceneMetadata.framesPerSecond,
            timeCodesPerSecond: sceneMetadata.timeCodesPerSecond,
            startTimeCode: sceneMetadata.startTimeCode,
            endTimeCode: sceneMetadata.endTimeCode,
            autoPlay: sceneMetadata.autoPlay,
            comment: sceneMetadata.comment || '',
            copyright: sceneMetadata.copyright || '',
            author: sceneMetadata.author || '',
            defaultPrim: sceneMetadata.defaultPrim || ''
        };

        console.log(`=== USD Scene Metadata ===`);
        console.log(`upAxis: "${currentSceneMetadata.upAxis}"`);
        console.log(`metersPerUnit: ${currentSceneMetadata.metersPerUnit}`);
        if (currentSceneMetadata.framesPerSecond !== null && currentSceneMetadata.framesPerSecond !== undefined) {
            console.log(`framesPerSecond: ${currentSceneMetadata.framesPerSecond}`);
        }
        if (currentSceneMetadata.comment) {
            console.log(`comment: "${currentSceneMetadata.comment}"`);
        }

        // Get scene information
        const numMeshes = currentNativeLoader.numMeshes();
        const numMaterials = currentNativeLoader.numMaterials();

        console.log(`Loaded: ${numMeshes} meshes, ${numMaterials} materials`);

        // Update UI
        document.getElementById('model-info').style.display = 'block';
        document.getElementById('object-count').textContent = numMeshes;
        document.getElementById('material-count').textContent = numMaterials;

        // Update scene metadata panel
        updateSceneMetadataPanel();

        // Load materials
        await loadMaterials();

        // Load meshes
        loadMeshes();

        // Apply Z-up to Y-up conversion if enabled AND the file is actually Z-up
        console.log(`\n=== CALLING applyUpAxisConversionToScene() ===`);
        console.log(`About to apply conversion with:`);
        console.log(`  currentFileUpAxis: "${currentFileUpAxis}"`);
        console.log(`  applyUpAxisConversion: ${applyUpAxisConversion}`);
        console.log(`  sceneRoot: ${!!sceneRoot}`);
        console.log(`  sceneRoot.children.length: ${sceneRoot ? sceneRoot.children.length : 'N/A'}`);

        // HACK
        //applyUpAxisConversionToScene();
        console.log(`\nIMMEDIATELY AFTER applyUpAxisConversionToScene():`);
        console.log(`  sceneRoot.rotation: x=${sceneRoot?.rotation.x.toFixed(4)}, y=${sceneRoot?.rotation.y.toFixed(4)}, z=${sceneRoot?.rotation.z.toFixed(4)}`);

        // Update material panel
        updateMaterialPanel();

        // Fit camera to scene
        fitCameraToScene();

        updateStatus(`Loaded: ${numMeshes} objects, ${numMaterials} materials`, 'success');

        // Final summary - verify rotation is still applied
        console.log(`\n================================================`);
        console.log(`=== LOAD COMPLETE SUMMARY ===`);
        console.log(`File: ${filename}`);
        console.log(`UpAxis from file metadata: "${currentFileUpAxis}"`);
        console.log(`Conversion enabled: ${applyUpAxisConversion}`);
        console.log(`Meshes loaded: ${meshes.length}`);
        console.log(`Meshes in sceneRoot: ${sceneRoot ? sceneRoot.children.length : 0}`);
        console.log(`\nFINAL sceneRoot rotation:`);
        console.log(`  x=${sceneRoot?.rotation.x.toFixed(4)} (should be ${currentFileUpAxis === 'Z' && applyUpAxisConversion ? '-1.5708 (-90°)' : '0.0000'})`);
        console.log(`  y=${sceneRoot?.rotation.y.toFixed(4)} (should be 0.0000)`);
        console.log(`  z=${sceneRoot?.rotation.z.toFixed(4)} (should be 0.0000)`);

        if (currentFileUpAxis === 'Z' && applyUpAxisConversion) {
            const expectedRotation = -Math.PI / 2;
            const actualRotation = sceneRoot?.rotation.x || 0;
            const isCorrect = Math.abs(actualRotation - expectedRotation) < 0.001;
            console.log(`\nRotation check: ${isCorrect ? '✓ CORRECT' : '✗ WRONG!'}`);
            if (!isCorrect) {
                console.error(`ERROR: Expected rotation ${expectedRotation.toFixed(4)} but got ${actualRotation.toFixed(4)}`);
            }
        }
        console.log(`================================================\n`);

    } catch (error) {
        console.error('Error loading USD file:', error);
        updateStatus(`Error: ${error.message}`, 'error');
    } finally {
        showLoading(false);
    }
}

// Load embedded default scene
async function loadEmbeddedScene() {

    //const usd_filename = {'suzanne-materialx.usda': './assets/suzanne-materialx.usda'};
    const usd_filename = 'fancy-teapot-mtlx.usdz';
    const usd_filepath = './assets/fancy-teapot-mtlx.usdz';

    console.log(`Loading default scene from ${usd_filename}...`);

    try {
        // Fetch the suzanne-materialx.usda file
        const response = await fetch(usd_filepath);
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.statusText}`);
        }

        const arrayBuffer = await response.arrayBuffer();
        await loadUSDFile(arrayBuffer, usd_filename);

        console.log(`Successfully loaded ${usd_filename}`);
    } catch (error) {
        console.error(`Error loading ${usd_filename}:`, error);
        updateStatus('Failed to load default scene, using fallback', 'error');

        // Fallback to embedded scene
        const encoder = new TextEncoder();
        const usdaBytes = encoder.encode(EMBEDDED_USDA_SCENE);
        const arrayBuffer = usdaBytes.buffer;
        await loadUSDFile(arrayBuffer, 'embedded_scene.usda');
    }
}

// Load materials from USD
async function loadMaterials() {
    if (!currentNativeLoader) {
        console.error('loadMaterials: No USD loader available');
        return;
    }

    materials = [];
    const numMaterials = currentNativeLoader.numMaterials();

    if (numMaterials === 0) {
        console.warn('No materials found in USD file');
        return;
    }

    console.log(`Loading ${numMaterials} materials...`);

    for (let i = 0; i < numMaterials; i++) {
        // Validate material index
        if (!validateMaterialIndex(i, numMaterials, `loadMaterials[${i}]`)) {
            continue;
        }

        try {
            // Get material in JSON format for OpenPBR data
            const result = currentNativeLoader.getMaterialWithFormat(i, 'json');

            if (result.error) {
                reportError(`Material ${i}`, new Error(result.error));
                continue;
            }

            if (!result.data) {
                console.error(`Material ${i} has no data`);
                continue;
            }

            const materialData = JSON.parse(result.data);
            console.log(`Material ${i}:`, materialData);

            // Create Three.js material from OpenPBR data
            const threeMaterial = await createOpenPBRMaterial(materialData);
            if (!threeMaterial) {
                throw new Error('Failed to create Three.js material');
            }

            // Extract parameters based on which material type is active
            const activeMaterialType = threeMaterial.userData.activeMaterialType;
            let parameters = {};

            if (activeMaterialType === 'UsdPreviewSurface') {
                parameters = extractUsdPreviewSurfaceParams(materialData);
            } else if (activeMaterialType === 'OpenPBR') {
                parameters = extractOpenPBRParams(materialData);
            }

            materials.push({
                index: i,
                name: materialData.name || `Material_${i}`,
                data: materialData,
                threeMaterial: threeMaterial,
                parameters: parameters,
                materialType: activeMaterialType || 'Unknown'
            });

        } catch (error) {
            reportError(`Material ${i}`, error);
            // Create a default fallback material
            materials.push({
                index: i,
                name: `Material_${i}_Fallback`,
                data: null,
                threeMaterial: new THREE.MeshPhysicalMaterial({
                    color: 0x808080,
                    metalness: 0.5,
                    roughness: 0.5,
                    envMapIntensity: exposureValue
                }),
                parameters: {}
            });
        }
    }

    console.log(`Successfully loaded ${materials.length} materials`);
}

// Create Three.js material from OpenPBR/UsdPreviewSurface data
async function createOpenPBRMaterial(materialData) {

    // Determine which material type to use based on preference and availability
    const hasOpenPBR = materialData.hasOpenPBR;
    const hasUsdPreviewSurface = materialData.hasUsdPreviewSurface;

    let useOpenPBR = false;
    let useUsdPreview = false;

    if (preferredMaterialType === 'auto') {
        // Auto mode: prefer OpenPBR if available, otherwise UsdPreviewSurface
        useOpenPBR = hasOpenPBR;
        useUsdPreview = !hasOpenPBR && hasUsdPreviewSurface;
    } else if (preferredMaterialType === 'openpbr') {
        // Force OpenPBR if available
        useOpenPBR = hasOpenPBR;
        useUsdPreview = !hasOpenPBR && hasUsdPreviewSurface; // Fallback
    } else if (preferredMaterialType === 'usdpreviewsurface') {
        // Force UsdPreviewSurface if available
        useUsdPreview = hasUsdPreviewSurface;
        useOpenPBR = !hasUsdPreviewSurface && hasOpenPBR; // Fallback
    }

    console.log(`Material type selection: hasOpenPBR=${hasOpenPBR}, hasUsdPreviewSurface=${hasUsdPreviewSurface}, preferredType=${preferredMaterialType}, using OpenPBR=${useOpenPBR}, using UsdPreview=${useUsdPreview}`);

    // === NodeMaterial Support via MaterialXLoader ===
    if (useNodeMaterial && useOpenPBR) {
        console.log("=== Creating NodeMaterial via MaterialXLoader ===");
        try {
            const mtlxXML = convertOpenPBRToMaterialXML(materialData, materialData.name || 'Material');
            console.log("Generated MaterialX XML:", mtlxXML);

            if (!materialXLoader) {
                materialXLoader = new MaterialXLoader();
            }

            const mtlxMaterials = await new Promise((resolve, reject) => {
                materialXLoader.parse(mtlxXML, '', (materials) => {
                    resolve(materials);
                }, (error) => {
                    console.error("MaterialXLoader error:", error);
                    reject(error);
                });
            });

            if (mtlxMaterials && Object.keys(mtlxMaterials).length > 0) {
                const nodeMaterial = Object.values(mtlxMaterials)[0];
                console.log("Created NodeMaterial:", nodeMaterial);
                nodeMaterial.envMapIntensity = exposureValue;
                nodeMaterial.userData.materialData = materialData;
                nodeMaterial.userData.isNodeMaterial = true;
                nodeMaterial.name = materialData.name || 'NodeMaterial';
                return nodeMaterial;
            } else {
                console.warn("MaterialXLoader returned no materials, falling back to MeshPhysicalMaterial");
            }
        } catch (error) {
            console.error("Failed to create NodeMaterial:", error);
            console.warn("Falling back to MeshPhysicalMaterial");
        }
    }
    // === End NodeMaterial Support ===

    const material = new THREE.MeshPhysicalMaterial();

    // Set initial environment map intensity based on current exposure
    material.envMapIntensity = exposureValue;

    // Store texture references for later management
    material.userData.textures = {};

    // Store which material type is being used
    material.userData.activeMaterialType = useOpenPBR ? 'OpenPBR' : (useUsdPreview ? 'UsdPreviewSurface' : 'None');

    if (useOpenPBR && materialData.hasOpenPBR) {
        console.log("=== Material has OpenPBR ===");
        console.log("Available keys:", Object.keys(materialData));

        // Try multiple property name variations for flat format
        // Sometimes the properties are nested, sometimes they're directly on materialData
        let pbrFlat = materialData.openPBRShader || materialData.openPBR_surface || materialData.openpbr_surface;
        const pbrGrouped = materialData.openPBR;

        // If no nested object found, check if flat properties exist directly on materialData
        if (!pbrFlat && (materialData.base_color !== undefined ||
                        materialData.base_metalness !== undefined ||
                        materialData.specular_roughness !== undefined)) {
            console.log("Flat format properties found directly on materialData");
            pbrFlat = materialData;
        }

        console.log("pbrFlat:", pbrFlat);
        console.log("pbrGrouped:", pbrGrouped);

        // Use flat format if available
        if (pbrFlat) {
            // New flat format: base_color, base_metalness, specular_roughness, etc.

            // Base color
            if (pbrFlat.base_color) {
                // Check if it's a texture reference (object with textureId) or a color value (array)
                if (Array.isArray(pbrFlat.base_color)) {
                    material.color = createColorWithSpace(...pbrFlat.base_color);
                } else if (typeof pbrFlat.base_color === 'object' && pbrFlat.base_color.textureId !== undefined) {
                    // It's a texture
                    const colorTexId = pbrFlat.base_color.textureId;
                    if (colorTexId >= 0) {
                        const texture = await loadTextureFromUSD(colorTexId);
                        if (texture) {
                            material.map = texture;
                            material.userData.textures.map = { textureId: colorTexId, texture };
                        }
                    }
                    // Also use the value if present
                    if (pbrFlat.base_color.value && Array.isArray(pbrFlat.base_color.value)) {
                        material.color = createColorWithSpace(...pbrFlat.base_color.value);
                    }
                }
            }

            // Metalness
            if (pbrFlat.base_metalness !== undefined) {
                if (typeof pbrFlat.base_metalness === 'number') {
                    material.metalness = pbrFlat.base_metalness;
                } else if (typeof pbrFlat.base_metalness === 'object') {
                    // TODO: Temporarily disabled - enable later
                    // const metalnessTexId = pbrFlat.base_metalness.textureId;
                    // if (metalnessTexId !== undefined && metalnessTexId >= 0) {
                    //     const texture = await loadTextureFromUSD(metalnessTexId);
                    //     if (texture) {
                    //         material.metalnessMap = texture;
                    //         material.userData.textures.metalnessMap = { textureId: metalnessTexId, texture };
                    //     }
                    // }
                    if (pbrFlat.base_metalness.value !== undefined) {
                        material.metalness = pbrFlat.base_metalness.value;
                    }
                }
            }

            // Roughness
            if (pbrFlat.specular_roughness !== undefined) {
                if (typeof pbrFlat.specular_roughness === 'number') {
                    material.roughness = pbrFlat.specular_roughness;
                } else if (typeof pbrFlat.specular_roughness === 'object') {
                    // TODO: Temporarily disabled - enable later
                    // const roughnessTexId = pbrFlat.specular_roughness.textureId;
                    // if (roughnessTexId !== undefined && roughnessTexId >= 0) {
                    //     const texture = await loadTextureFromUSD(roughnessTexId);
                    //     if (texture) {
                    //         material.roughnessMap = texture;
                    //         material.userData.textures.roughnessMap = { textureId: roughnessTexId, texture };
                    //     }
                    // }
                    if (pbrFlat.specular_roughness.value !== undefined) {
                        material.roughness = pbrFlat.specular_roughness.value;
                    }
                }
            }

            // IOR
            if (pbrFlat.specular_ior !== undefined) {
                material.ior = typeof pbrFlat.specular_ior === 'number' ? pbrFlat.specular_ior :
                              (pbrFlat.specular_ior.value || 1.5);
            }

            // Specular color
            if (pbrFlat.specular_color) {
                if (Array.isArray(pbrFlat.specular_color)) {
                    material.specularColor = createColorWithSpace(...pbrFlat.specular_color);
                } else if (typeof pbrFlat.specular_color === 'object' && pbrFlat.specular_color.value) {
                    material.specularColor = createColorWithSpace(...pbrFlat.specular_color.value);
                }
            }

            // Transmission
            if (pbrFlat.transmission_weight !== undefined) {
                material.transmission = typeof pbrFlat.transmission_weight === 'number' ? pbrFlat.transmission_weight :
                                       (pbrFlat.transmission_weight.value || 0);
            }
            if (pbrFlat.transmission_color) {
                if (Array.isArray(pbrFlat.transmission_color)) {
                    material.attenuationColor = createColorWithSpace(...pbrFlat.transmission_color);
                } else if (typeof pbrFlat.transmission_color === 'object' && pbrFlat.transmission_color.value) {
                    material.attenuationColor = createColorWithSpace(...pbrFlat.transmission_color.value);
                }
            }

            // Coat (clearcoat)
            if (pbrFlat.coat_weight !== undefined) {
                material.clearcoat = typeof pbrFlat.coat_weight === 'number' ? pbrFlat.coat_weight :
                                    (pbrFlat.coat_weight.value || 0);
            }
            if (pbrFlat.coat_roughness !== undefined) {
                material.clearcoatRoughness = typeof pbrFlat.coat_roughness === 'number' ? pbrFlat.coat_roughness :
                                             (pbrFlat.coat_roughness.value || 0);
            }

            // Emission
            // In OpenPBR: final_emission = emission_color * emission_luminance
            // We need to load both and set them on the material
            let emissionColor = null;
            let emissionLuminance = 1.0;

            console.log("=== Loading Emission (Flat Format) ===");
            console.log("pbrFlat.emission_color:", pbrFlat.emission_color);
            console.log("pbrFlat.emission_luminance:", pbrFlat.emission_luminance);

            if (pbrFlat.emission_color) {
                if (Array.isArray(pbrFlat.emission_color)) {
                    emissionColor = pbrFlat.emission_color;
                } else if (typeof pbrFlat.emission_color === 'object') {
                    // TODO: Temporarily disabled - enable later
                    // const emissiveTexId = pbrFlat.emission_color.textureId;
                    // if (emissiveTexId !== undefined && emissiveTexId >= 0) {
                    //     const texture = await loadTextureFromUSD(emissiveTexId);
                    //     if (texture) {
                    //         material.emissiveMap = texture;
                    //         material.userData.textures.emissiveMap = { textureId: emissiveTexId, texture };
                    //     }
                    // }
                    if (pbrFlat.emission_color.value && Array.isArray(pbrFlat.emission_color.value)) {
                        emissionColor = pbrFlat.emission_color.value;
                    }
                }
            }
            if (pbrFlat.emission_luminance !== undefined) {
                emissionLuminance = typeof pbrFlat.emission_luminance === 'number' ? pbrFlat.emission_luminance :
                                   (typeof pbrFlat.emission_luminance === 'object' && pbrFlat.emission_luminance.value !== undefined ? pbrFlat.emission_luminance.value : 0.0);
            }

            console.log("Extracted emissionColor:", emissionColor);
            console.log("Extracted emissionLuminance:", emissionLuminance);

            // Apply emission: color and intensity
            if (emissionColor) {
                material.emissive = createColorWithSpace(...emissionColor);
                material.emissiveIntensity = emissionLuminance;
                console.log("Applied to material.emissive:", material.emissive);
                console.log("Applied to material.emissiveIntensity:", material.emissiveIntensity);
            } else {
                console.log("No emission color found, skipping emission setup");
            }

            // Normal map
            // TODO: Temporarily disabled - enable later
            // if (pbrFlat.geometry_normal) {
            //     const normalTexId = typeof pbrFlat.geometry_normal === 'object' ? pbrFlat.geometry_normal.textureId : undefined;
            //     if (normalTexId !== undefined && normalTexId >= 0) {
            //         const texture = await loadTextureFromUSD(normalTexId);
            //         if (texture) {
            //             material.normalMap = texture;
            //             material.normalScale = new THREE.Vector2(1, 1);
            //             material.userData.textures.normalMap = { textureId: normalTexId, texture };
            //         }
            //     }
            // }

            // Opacity
            if (pbrFlat.opacity !== undefined) {
                const opacityValue = typeof pbrFlat.opacity === 'number' ? pbrFlat.opacity :
                                    (pbrFlat.opacity.value !== undefined ? pbrFlat.opacity.value : 1);
                material.opacity = opacityValue;
                material.transparent = opacityValue < 1.0;

                // TODO: Temporarily disabled - enable later
                // Check for opacity texture
                // if (typeof pbrFlat.opacity === 'object' && pbrFlat.opacity.textureId !== undefined) {
                //     const opacityTexId = pbrFlat.opacity.textureId;
                //     if (opacityTexId >= 0) {
                //         const texture = await loadTextureFromUSD(opacityTexId);
                //         if (texture) {
                //             material.alphaMap = texture;
                //             material.userData.textures.alphaMap = { textureId: opacityTexId, texture };
                //             material.transparent = true;
                //         }
                //     }
                // }
            }

        } else if (pbrGrouped) {
            // Grouped format - uses nested structure with underscore naming
            const pbr = pbrGrouped;

            console.log("=== Using grouped format ===");
            console.log("pbr keys:", Object.keys(pbr));
            console.log("pbr.base:", pbr.base);

        // Base parameters
        if (pbr.base) {
            // Base color - use base_color (with underscore)
            if (pbr.base.base_color !== undefined) {
                console.log("base.base_color", pbr.base.base_color);
                const colorValue = Array.isArray(pbr.base.base_color) ? pbr.base.base_color :
                                  (pbr.base.base_color.value || [1, 1, 1]);
                material.color = createColorWithSpace(...colorValue);

                // Check for base color texture
                if (typeof pbr.base.base_color === 'object' && pbr.base.base_color.textureId !== undefined) {
                    const colorTexId = pbr.base.base_color.textureId;
                    console.log("colorTexId", colorTexId);
                    if (colorTexId >= 0) {
                        const texture = await loadTextureFromUSD(colorTexId);
                        if (texture) {
                            console.log("base_tex", texture);
                            material.map = texture;
                            material.userData.textures.map = { textureId: colorTexId, texture };
                        }
                    }
                }
            }

            // Metalness - use base_metalness (with underscore)
            if (pbr.base.base_metalness !== undefined) {
                const metalnessValue = typeof pbr.base.base_metalness === 'number' ? pbr.base.base_metalness :
                                      (pbr.base.base_metalness.value || 0);
                material.metalness = metalnessValue;

                // TODO: Temporarily disabled - enable later
                // Check for metalness texture
                // if (typeof pbr.base.base_metalness === 'object' && pbr.base.base_metalness.textureId !== undefined) {
                //     const metalnessTexId = pbr.base.base_metalness.textureId;
                //     if (metalnessTexId >= 0) {
                //         const texture = await loadTextureFromUSD(metalnessTexId);
                //         if (texture) {
                //             material.metalnessMap = texture;
                //             material.userData.textures.metalnessMap = { textureId: metalnessTexId, texture };
                //         }
                //     }
                // }
            }
        }

        // Specular parameters
        if (pbr.specular) {
            // Roughness - use specular_roughness (with underscore)
            if (pbr.specular.specular_roughness !== undefined) {
                const roughnessValue = typeof pbr.specular.specular_roughness === 'number' ? pbr.specular.specular_roughness :
                                      (pbr.specular.specular_roughness.value || 0.3);
                material.roughness = roughnessValue;

                // TODO: Temporarily disabled - enable later
                // Check for roughness texture
                // if (typeof pbr.specular.specular_roughness === 'object' && pbr.specular.specular_roughness.textureId !== undefined) {
                //     const roughnessTexId = pbr.specular.specular_roughness.textureId;
                //     if (roughnessTexId >= 0) {
                //         const texture = await loadTextureFromUSD(roughnessTexId);
                //         if (texture) {
                //             material.roughnessMap = texture;
                //             material.userData.textures.roughnessMap = { textureId: roughnessTexId, texture };
                //         }
                //     }
                // }
            }

            // IOR - use specular_ior (with underscore)
            if (pbr.specular.specular_ior !== undefined) {
                material.ior = typeof pbr.specular.specular_ior === 'number' ? pbr.specular.specular_ior :
                              (pbr.specular.specular_ior.value || 1.5);
            }

            // Specular color - use specular_color (with underscore)
            if (pbr.specular.specular_color !== undefined) {
                const colorValue = Array.isArray(pbr.specular.specular_color) ? pbr.specular.specular_color :
                                  (pbr.specular.specular_color.value || [1, 1, 1]);
                material.specularColor = createColorWithSpace(...colorValue);
            }
        }

        // Transmission
        if (pbr.transmission) {
            // Use transmission_weight (with underscore)
            if (pbr.transmission.transmission_weight !== undefined) {
                material.transmission = typeof pbr.transmission.transmission_weight === 'number' ? pbr.transmission.transmission_weight :
                                       (pbr.transmission.transmission_weight.value || 0);
            }
            // Use transmission_color (with underscore)
            if (pbr.transmission.transmission_color !== undefined) {
                const colorValue = Array.isArray(pbr.transmission.transmission_color) ? pbr.transmission.transmission_color :
                                  (pbr.transmission.transmission_color.value || [1, 1, 1]);
                material.attenuationColor = createColorWithSpace(...colorValue);
            }
        }

        // Coat (clearcoat)
        if (pbr.coat) {
            // Use coat_weight (with underscore)
            if (pbr.coat.coat_weight !== undefined) {
                material.clearcoat = typeof pbr.coat.coat_weight === 'number' ? pbr.coat.coat_weight :
                                    (pbr.coat.coat_weight.value || 0);
            }
            // Use coat_roughness (with underscore)
            if (pbr.coat.coat_roughness !== undefined) {
                material.clearcoatRoughness = typeof pbr.coat.coat_roughness === 'number' ? pbr.coat.coat_roughness :
                                             (pbr.coat.coat_roughness.value || 0);
            }
        }

        // Emission
        // In OpenPBR: final_emission = emission_color * emission_luminance
        if (pbr.emission) {
            console.log("=== Loading Emission (Grouped Format) ===");
            console.log("pbr.emission:", pbr.emission);
            console.log("pbr.emission.emission_color:", pbr.emission.emission_color);
            console.log("pbr.emission.emission_luminance:", pbr.emission.emission_luminance);

            let emissionColor = null;
            let emissionLuminance = 1.0;

            // Use emission_color (with underscore)
            if (pbr.emission.emission_color !== undefined) {
                emissionColor = Array.isArray(pbr.emission.emission_color) ? pbr.emission.emission_color :
                               (pbr.emission.emission_color.value || null);

                // TODO: Temporarily disabled - enable later
                // Check for emission texture
                // if (typeof pbr.emission.emission_color === 'object' && pbr.emission.emission_color.textureId !== undefined) {
                //     const emissiveTexId = pbr.emission.emission_color.textureId;
                //     if (emissiveTexId >= 0) {
                //         const texture = await loadTextureFromUSD(emissiveTexId);
                //         if (texture) {
                //             material.emissiveMap = texture;
                //             material.userData.textures.emissiveMap = { textureId: emissiveTexId, texture };
                //         }
                //     }
                // }
            }
            // Use emission_luminance (with underscore)
            if (pbr.emission.emission_luminance !== undefined) {
                console.log("XYZ: pbr.emission.emission_luminance:", pbr.emission.emission_luminance, typeof pbr.emission.emission_luminance);
                emissionLuminance = typeof pbr.emission.emission_luminance === 'number' ? pbr.emission.emission_luminance :
                                   (typeof pbr.emission.emission_luminance === 'object' && pbr.emission.emission_luminance.value !== undefined ? pbr.emission.emission_luminance.value : 0.0);
            }

            // Apply emission: color and intensity
            if (emissionColor) {
                material.emissive = createColorWithSpace(...emissionColor);
                material.emissiveIntensity = emissionLuminance;
                console.log("Applied to material.emissive (grouped):", material.emissive);
                console.log("Applied to material.emissiveIntensity (grouped):", material.emissiveIntensity);
            } else {
                console.log("No emission color found (grouped), skipping emission setup");
            }
        }

        // Geometry (check for normal and bump maps)
        //if (pbr.geometry) {
        //    // TODO: Temporarily disabled - enable later
        //    // Normal map - use geometry_normal (with underscore)
        //    // if (pbr.geometry.geometry_normal !== undefined) {
        //    //     if (typeof pbr.geometry.geometry_normal === 'object' && pbr.geometry.geometry_normal.textureId !== undefined) {
        //    //         const normalTexId = pbr.geometry.geometry_normal.textureId;
        //    //         if (normalTexId >= 0) {
        //    //             const texture = await loadTextureFromUSD(normalTexId);
        //    //             if (texture) {
        //    //                 material.normalMap = texture;
        //    //                 material.normalScale = new THREE.Vector2(1, 1);
        //    //                 material.userData.textures.normalMap = { textureId: normalTexId, texture };
        //    //             }
        //    //         }
        //    //     }
        //    // }

        //    // Opacity - use geometry_opacity (with underscore)
        //    if (pbr.geometry.geometry_opacity !== undefined) {
        //        const opacityValue = typeof pbr.geometry.geometry_opacity === 'number' ? pbr.geometry.geometry_opacity :
        //                            (pbr.geometry.geometry_opacity.value !== undefined ? pbr.geometry.geometry_opacity.value : 1);
        //        material.opacity = opacityValue;
        //        material.transparent = opacityValue < 1;

        //        // TODO: Temporarily disabled - enable later
        //        // Check for opacity texture
        //        // if (typeof pbr.geometry.geometry_opacity === 'object' && pbr.geometry.geometry_opacity.textureId !== undefined) {
        //        //     const opacityTexId = pbr.geometry.geometry_opacity.textureId;
        //        //     if (opacityTexId >= 0) {
        //        //         const texture = await loadTextureFromUSD(opacityTexId);
        //        //         if (texture) {
        //        //             material.alphaMap = texture;
        //        //             material.userData.textures.alphaMap = { textureId: opacityTexId, texture };
        //        //             material.transparent = true;
        //        //         }
        //        //     }
        //        // }
        //    }
        //}

        // Thin film
        //if (pbr.thin_film) {
        //    if (pbr.thin_film.thickness !== undefined) {
        //        material.thickness = pbr.thin_film.thickness;
        //    }
        //}

        // Subsurface (approximation with subsurface scattering)
        //if (pbr.subsurface && pbr.subsurface.weight > 0) {
        //    // Three.js doesn't have direct subsurface support, but we can approximate
        //    material.transmission = Math.max(material.transmission, pbr.subsurface.weight * 0.5);
        //}
        } // end of else if (pbrGrouped)

    } else if (useUsdPreview && materialData.hasUsdPreviewSurface && materialData.usdPreviewSurface) {
        // Fallback to UsdPreviewSurface
        const preview = materialData.usdPreviewSurface;

        if (preview.diffuseColor) {
            const colorValue = Array.isArray(preview.diffuseColor) ? preview.diffuseColor :
                              (preview.diffuseColor.value || [1, 1, 1]);
            material.color = createColorWithSpace(...colorValue);
        }
        if (preview.metallic !== undefined) {
            material.metalness = preview.metallic;
        }
        if (preview.roughness !== undefined) {
            material.roughness = preview.roughness;
        }
        if (preview.opacity !== undefined) {
            material.opacity = preview.opacity;
            material.transparent = preview.opacity < 1;
        }
    }

    // Initialize color space settings for textures
    if (!material.userData.colorSpaceSettings) {
        material.userData.colorSpaceSettings = {};
    }

    // Set default color space for each texture map
    // Color textures default to sRGB, data textures default to linear
    const textureDefaults = {
        map: 'srgb',              // Base color: sRGB
        emissiveMap: 'srgb',      // Emissive: sRGB
        normalMap: 'linear',      // Normal: Linear (data)
        roughnessMap: 'linear',   // Roughness: Linear (data)
        metalnessMap: 'linear',   // Metalness: Linear (data)
        aoMap: 'linear',          // AO: Linear (data)
        bumpMap: 'linear',        // Bump: Linear (data)
        displacementMap: 'linear' // Displacement: Linear (data)
    };

    // Initialize color space for textures that are present
    Object.keys(material.userData.textures || {}).forEach(mapName => {
        if (textureDefaults[mapName]) {
            material.userData.colorSpaceSettings[mapName] = textureDefaults[mapName];
        }
    });

    // Apply color space shader if textures are present
    if (Object.keys(material.userData.textures || {}).length > 0) {
        // HACK. disabled 
        //applyColorSpaceShader(material, material.userData.colorSpaceSettings);
    }

    material.needsUpdate = true;
    return material;
}

// Extract UsdPreviewSurface parameters for GUI
function extractUsdPreviewSurfaceParams(materialData) {
    if (!materialData.hasUsdPreviewSurface || !materialData.usdPreviewSurface) {
        return {};
    }

    const preview = materialData.usdPreviewSurface;
    const params = {
        diffuse: {},
        specular: {},
        clearcoat: {},
        emission: {},
        geometry: {},
        displacement: {},
        occlusion: {}
    };

    // Helper to unwrap value
    const unwrapValue = (val) => {
        if (val && typeof val === 'object' && val.value !== undefined) {
            return val.value;
        }
        return val;
    };

    // Diffuse
    if (preview.diffuseColor !== undefined) {
        params.diffuse.diffuseColor = unwrapValue(preview.diffuseColor);
    }

    // Specular
    if (preview.roughness !== undefined) {
        params.specular.roughness = unwrapValue(preview.roughness);
    }
    if (preview.metallic !== undefined) {
        params.specular.metallic = unwrapValue(preview.metallic);
    }
    if (preview.specularColor !== undefined) {
        params.specular.specularColor = unwrapValue(preview.specularColor);
    }
    if (preview.ior !== undefined) {
        params.specular.ior = unwrapValue(preview.ior);
    }

    // Clearcoat
    if (preview.clearcoat !== undefined) {
        params.clearcoat.clearcoat = unwrapValue(preview.clearcoat);
    }
    if (preview.clearcoatRoughness !== undefined) {
        params.clearcoat.clearcoatRoughness = unwrapValue(preview.clearcoatRoughness);
    }

    // Emission
    if (preview.emissiveColor !== undefined) {
        params.emission.emissiveColor = unwrapValue(preview.emissiveColor);
    }

    // Geometry
    if (preview.opacity !== undefined) {
        params.geometry.opacity = unwrapValue(preview.opacity);
    }
    if (preview.normal !== undefined) {
        params.geometry.normal = unwrapValue(preview.normal);
    }

    // Displacement
    if (preview.displacement !== undefined) {
        params.displacement.displacement = unwrapValue(preview.displacement);
    }

    // Occlusion
    if (preview.occlusion !== undefined) {
        params.occlusion.occlusion = unwrapValue(preview.occlusion);
    }

    console.log("=== Extracted UsdPreviewSurface params ===", params);
    return params;
}

// Extract OpenPBR parameters for GUI
function extractOpenPBRParams(materialData) {
    if (!materialData.hasOpenPBR) {
        return {};
    }

    // Try multiple property name variations for flat format (same logic as createOpenPBRMaterial)
    let pbrFlat = materialData.openPBRShader || materialData.openPBR_surface || materialData.openpbr_surface;
    const pbrGrouped = materialData.openPBR;

    // If no nested object found, check if flat properties exist directly on materialData
    if (!pbrFlat && (materialData.base_color !== undefined ||
                    materialData.base_metalness !== undefined ||
                    materialData.specular_roughness !== undefined)) {
        pbrFlat = materialData;
    }

    // Use flat format if available, otherwise grouped format
    const pbr = pbrFlat || pbrGrouped;
    if (!pbr) {
        console.warn("extractOpenPBRParams: No PBR data found");
        return {};
    }

    console.log("=== Extracting OpenPBR params ===");
    console.log("Using pbrFlat:", !!pbrFlat);
    console.log("Using pbrGrouped:", !!pbrGrouped);
    console.log("pbr keys:", Object.keys(pbr));

    // The native loader returns flat parameters like base_weight, base_color, etc.
    // We need to group them for the GUI: {base: {weight, color}, specular: {...}}
    const params = {
        base: {},
        specular: {},
        transmission: {},
        subsurface: {},
        coat: {},
        emission: {},
        geometry: {}
    };

    // Helper function to unwrap parameter values
    // The C++ serializer wraps values in objects like {type: "value", value: X} or {type: "texture", textureId: Y, value: X}
    const unwrapValue = (val) => {
        if (val && typeof val === 'object') {
            if (val.value !== undefined) {
                // It's wrapped - return the actual value
                return val.value;
            } else if (val.textureId !== undefined && val.value === undefined) {
                // It's a texture-only reference, preserve the object so GUI can show it with texture info
                return val;
            }
        }
        // It's already a plain value
        return val;
    };

    // Check if we have a nested grouped format (from C++ serializer)
    // The C++ serializer outputs: { base: { base_weight: {type, value}, ... }, specular: { ... } }
    const groupKeys = ['base', 'specular', 'transmission', 'subsurface', 'coat', 'emission'];
    const hasNestedGroups = groupKeys.some(key => pbr[key] && typeof pbr[key] === 'object');

    if (hasNestedGroups && pbrGrouped) {
        // Handle nested grouped format from C++ serializer
        console.log("=== Using nested grouped format ===");

        groupKeys.forEach(groupName => {
            if (pbr[groupName] && typeof pbr[groupName] === 'object') {
                params[groupName] = {};
                Object.entries(pbr[groupName]).forEach(([paramKey, paramValue]) => {
                    // Remove group prefix from parameter name if present
                    // e.g., "base_weight" -> "weight", "specular_color" -> "color"
                    const prefix = groupName + '_';
                    const cleanKey = paramKey.startsWith(prefix) ? paramKey.substring(prefix.length) : paramKey;

                    const unwrappedValue = unwrapValue(paramValue);
                    if (unwrappedValue !== null) {
                        params[groupName][cleanKey] = unwrappedValue;
                    }
                });
            }
        });
    } else {
        // Handle flat format (old behavior)
        console.log("=== Using flat format ===");

        Object.entries(pbr).forEach(([key, value]) => {
            // Skip type field and internal fields
            if (key === 'type' || key === 'hasOpenPBR') return;

            const extractedValue = unwrapValue(value);
            if (extractedValue === null) return;

            // Split parameter name (e.g., "base_weight" -> ["base", "weight"])
            const parts = key.split('_');
            if (parts.length >= 2) {
                const group = parts[0]; // base, specular, transmission, etc.
                const param = parts.slice(1).join('_'); // weight, color, roughness, etc.

                // Map group names
                const groupMap = {
                    'base': 'base',
                    'specular': 'specular',
                    'transmission': 'transmission',
                    'subsurface': 'subsurface',
                    'coat': 'coat',
                    'emission': 'emission'
                };

                if (groupMap[group]) {
                    params[groupMap[group]][param] = extractedValue;
                } else if (key === 'opacity' || key === 'normal' || key === 'tangent') {
                    // Geometry parameters
                    params.geometry[key] = extractedValue;
                }
            }
        });
    }

    console.log("=== Extracted params ===", params);
    return params;
}

// Load meshes from USD
function loadMeshes() {
    if (!currentNativeLoader) return;

    meshes = [];
    const numMeshes = currentNativeLoader.numMeshes();
    console.log(`Loading ${numMeshes} meshes...`);

    for (let i = 0; i < numMeshes; i++) {
        try {
            const meshData = currentNativeLoader.getMesh(i);
            console.log(`Mesh ${i} data:`, meshData);

            if (!meshData) {
                console.warn(`Failed to load mesh ${i}`);
                continue;
            }

            // Create Three.js geometry
            const geometry = new THREE.BufferGeometry();

            // Add vertices (try both 'points' and 'vertices' for compatibility)
            const vertexData = meshData.points || meshData.vertices;
            if (vertexData && vertexData.length > 0) {
                const vertices = new Float32Array(vertexData);
                geometry.setAttribute('position', new THREE.BufferAttribute(vertices, 3));
            }

            // Add normals
            if (meshData.normals && meshData.normals.length > 0) {
                const normals = new Float32Array(meshData.normals);
                geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3));
            } else {
                geometry.computeVertexNormals();
            }

            // Add UV sets
            // Support new uvSets structure (multiple UV channels)
            if (meshData.uvSets) {
                // Load all available UV sets (uv0, uv1, uv2, etc.)
                for (const uvSetKey in meshData.uvSets) {
                    const uvSet = meshData.uvSets[uvSetKey];
                    if (uvSet && uvSet.data && uvSet.data.length > 0) {
                        const uvs = new Float32Array(uvSet.data);
                        const slotId = uvSet.slotId || 0;

                        // Three.js uses 'uv' for first set, 'uv1', 'uv2', etc. for additional sets
                        const attributeName = slotId === 0 ? 'uv' : `uv${slotId}`;
                        geometry.setAttribute(attributeName, new THREE.BufferAttribute(uvs, 2));

                        console.log(`Mesh ${i}: Added UV set ${slotId} as attribute '${attributeName}'`);
                    }
                }
            }
            // Fallback to legacy 'uvs' or 'texcoords' field for backward compatibility
            else if (meshData.uvs && meshData.uvs.length > 0) {
                const uvs = new Float32Array(meshData.uvs);
                geometry.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
            } else if (meshData.texcoords && meshData.texcoords.length > 0) {
                const uvs = new Float32Array(meshData.texcoords);
                geometry.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
            }

            // Add faces (indices) - try both 'faceVertexIndices' and 'indices'
            const indexData = meshData.faceVertexIndices || meshData.indices;
            if (indexData && indexData.length > 0) {
                const indices = new Uint32Array(indexData);
                geometry.setIndex(new THREE.BufferAttribute(indices, 1));
            }

            // Get material index
            const materialIndex = meshData.materialIndex || 0;
            const material = materials[materialIndex]?.threeMaterial || new THREE.MeshPhysicalMaterial({ envMapIntensity: exposureValue });

            // Create mesh
            const mesh = new THREE.Mesh(geometry, material);
            mesh.name = meshData.name || `Mesh_${i}`;
            mesh.userData = {
                index: i,
                materialIndex: materialIndex,
                usdData: meshData
            };
            mesh.castShadow = true;
            mesh.receiveShadow = true;

            // Apply transform if available
            if (meshData.transform) {
                const matrix = new THREE.Matrix4();
                matrix.fromArray(meshData.transform);
                mesh.applyMatrix4(matrix);
                console.log(`Mesh ${i} has transform matrix:`, meshData.transform);

                // Extract position from matrix for diagnostic
                const position = new THREE.Vector3();
                const scale = new THREE.Vector3();
                matrix.decompose(position, new THREE.Quaternion(), scale);
                console.log(`  Transform includes: position=(${position.x.toFixed(2)}, ${position.y.toFixed(2)}, ${position.z.toFixed(2)})`);
            }

            sceneRoot.add(mesh);
            meshes.push(mesh);
            console.log(`Added mesh ${i} to sceneRoot:`, mesh.name, `vertices: ${geometry.attributes.position?.count || 0}`);

        } catch (error) {
            console.error(`Error loading mesh ${i}:`, error);
        }
    }
    console.log(`Total meshes added to scene: ${meshes.length}`);
}

// Update scene metadata panel
function updateSceneMetadataPanel() {
    const panel = document.getElementById('scene-metadata');
    const content = document.getElementById('metadata-content');

    if (!currentSceneMetadata) {
        panel.style.display = 'none';
        return;
    }

    panel.style.display = 'block';

    let html = '';

    // upAxis
    html += `<div><strong>Up Axis:</strong> ${currentSceneMetadata.upAxis}</div>`;

    // metersPerUnit
    html += `<div><strong>Meters/Unit:</strong> ${currentSceneMetadata.metersPerUnit}</div>`;

    // Animation metadata (only if present)
    if (currentSceneMetadata.framesPerSecond !== null && currentSceneMetadata.framesPerSecond !== undefined) {
        html += `<div><strong>FPS:</strong> ${currentSceneMetadata.framesPerSecond}</div>`;
    }

    if (currentSceneMetadata.timeCodesPerSecond !== null && currentSceneMetadata.timeCodesPerSecond !== undefined) {
        html += `<div><strong>TimeCodes/Sec:</strong> ${currentSceneMetadata.timeCodesPerSecond}</div>`;
    }

    if (currentSceneMetadata.startTimeCode !== null && currentSceneMetadata.startTimeCode !== undefined) {
        html += `<div><strong>Start Time:</strong> ${currentSceneMetadata.startTimeCode}</div>`;
    }

    if (currentSceneMetadata.endTimeCode !== null && currentSceneMetadata.endTimeCode !== undefined) {
        html += `<div><strong>End Time:</strong> ${currentSceneMetadata.endTimeCode}</div>`;
    }

    if (currentSceneMetadata.autoPlay !== null && currentSceneMetadata.autoPlay !== undefined) {
        html += `<div><strong>Auto Play:</strong> ${currentSceneMetadata.autoPlay ? 'Yes' : 'No'}</div>`;
    }

    // Default prim
    if (currentSceneMetadata.defaultPrim) {
        html += `<div><strong>Default Prim:</strong> ${currentSceneMetadata.defaultPrim}</div>`;
    }

    // Author
    if (currentSceneMetadata.author) {
        html += `<div><strong>Author:</strong> ${currentSceneMetadata.author}</div>`;
    }

    // Comment (can be multiline)
    if (currentSceneMetadata.comment) {
        const commentLines = currentSceneMetadata.comment.split('\n');
        if (commentLines.length === 1) {
            html += `<div><strong>Comment:</strong> ${currentSceneMetadata.comment}</div>`;
        } else {
            html += `<div><strong>Comment:</strong></div>`;
            html += `<div style="padding-left: 10px; font-style: italic; color: #bbb;">`;
            commentLines.forEach(line => {
                html += `${line}<br>`;
            });
            html += `</div>`;
        }
    }

    // Copyright
    if (currentSceneMetadata.copyright) {
        html += `<div><strong>Copyright:</strong> ${currentSceneMetadata.copyright}</div>`;
    }

    content.innerHTML = html;
}

// Update material panel
function updateMaterialPanel() {
    const panel = document.getElementById('material-panel');
    const list = document.getElementById('material-list');

    if (materials.length === 0) {
        panel.style.display = 'none';
        return;
    }

    panel.style.display = 'block';
    list.innerHTML = '';

    materials.forEach((material, index) => {
        const item = document.createElement('div');
        item.className = 'material-item';
        item.textContent = material.name;
        item.dataset.index = index;
        item.onclick = () => selectMaterial(index);
        list.appendChild(item);
    });
}

// Update texture panel for selected material
function updateTexturePanel(material) {
    const panel = document.getElementById('texture-panel');
    const list = document.getElementById('texture-list');

    if (!material || !material.threeMaterial || !material.threeMaterial.userData.textures) {
        panel.style.display = 'none';
        return;
    }

    const textures = material.threeMaterial.userData.textures;
    const textureKeys = Object.keys(textures);

    if (textureKeys.length === 0) {
        panel.style.display = 'none';
        return;
    }

    panel.style.display = 'block';
    list.innerHTML = '';

    // Initialize texture enabled state for this material if not exists
    if (!textureEnabled[material.index]) {
        textureEnabled[material.index] = {};
    }

    textureKeys.forEach(mapName => {
        const texInfo = textures[mapName];
        const texture = texInfo.texture;

        // Initialize enabled state
        if (textureEnabled[material.index][mapName] === undefined) {
            textureEnabled[material.index][mapName] = true;
        }

        const isEnabled = textureEnabled[material.index][mapName];

        // Create texture item
        const item = document.createElement('div');
        item.className = 'texture-item' + (isEnabled ? '' : ' disabled');

        // Header with name and toggle
        const header = document.createElement('div');
        header.className = 'texture-header';

        const name = document.createElement('span');
        name.className = 'texture-name';
        name.textContent = formatTextureName(mapName);
        header.appendChild(name);

        const toggle = document.createElement('button');
        toggle.className = 'texture-toggle' + (isEnabled ? '' : ' disabled');
        toggle.textContent = isEnabled ? 'ON' : 'OFF';
        toggle.onclick = () => toggleTexture(material, mapName);
        header.appendChild(toggle);

        item.appendChild(header);

        // Texture preview (thumbnail)
        const preview = createTextureThumbnail(texture);
        if (preview) {
            preview.className = 'texture-preview';
            preview.onclick = () => enlargeTexture(texture, formatTextureName(mapName));
            item.appendChild(preview);
        }

        // Texture info
        const info = document.createElement('div');
        info.className = 'texture-info';
        info.textContent = `${texture.image.width}x${texture.image.height} • ID: ${texInfo.textureId}`;
        item.appendChild(info);

        // Color space selector
        const colorSpaceDiv = document.createElement('div');
        colorSpaceDiv.className = 'texture-colorspace';
        colorSpaceDiv.style.marginTop = '8px';

        const colorSpaceLabel = document.createElement('label');
        colorSpaceLabel.textContent = 'Color Space: ';
        colorSpaceLabel.style.fontSize = '10px';
        colorSpaceLabel.style.color = '#aaa';
        colorSpaceDiv.appendChild(colorSpaceLabel);

        const colorSpaceSelect = document.createElement('select');
        colorSpaceSelect.style.fontSize = '10px';
        colorSpaceSelect.style.padding = '2px 5px';
        colorSpaceSelect.style.background = '#333';
        colorSpaceSelect.style.color = 'white';
        colorSpaceSelect.style.border = '1px solid #555';
        colorSpaceSelect.style.borderRadius = '3px';
        colorSpaceSelect.style.cursor = 'pointer';

        // Get current color space for this texture
        const currentColorSpace = material.threeMaterial.userData.colorSpaceSettings[mapName] || 'srgb';

        // Populate options
        Object.entries(TEXTURE_COLOR_SPACES).forEach(([value, label]) => {
            const option = document.createElement('option');
            option.value = value;
            option.textContent = label;
            option.selected = (value === currentColorSpace);
            colorSpaceSelect.appendChild(option);
        });

        // Handle color space change
        colorSpaceSelect.onchange = (e) => {
            const newColorSpace = e.target.value;
            changeTextureColorSpace(material, mapName, newColorSpace);
        };

        colorSpaceDiv.appendChild(colorSpaceSelect);
        item.appendChild(colorSpaceDiv);

        // UV Set selector
        const uvSetDiv = createUVSetSelector(material, mapName);
        if (uvSetDiv) {
            item.appendChild(uvSetDiv);
        }

        // Add texture transform controls
        if (isEnabled && texture) {
            const transformDiv = createTextureTransformUI(material, mapName, texture);
            if (transformDiv) {
                item.appendChild(transformDiv);
            }
        }

        list.appendChild(item);
    });
}

// Create UV set selector for a texture
function createUVSetSelector(material, mapName) {
    // Get the associated mesh to determine available UV sets
    const meshWithMaterial = meshes.find(m => m.userData.materialIndex === material.index);
    if (!meshWithMaterial) {
        return null; // No mesh found with this material
    }

    const geometry = meshWithMaterial.geometry;
    const availableUVSets = [];

    // Detect available UV sets in the geometry
    for (let i = 0; i < 8; i++) { // Check up to 8 UV sets (common limit)
        const attrName = i === 0 ? 'uv' : `uv${i}`;
        if (geometry.attributes[attrName]) {
            availableUVSets.push({ index: i, name: attrName });
        }
    }

    // Only show selector if there are multiple UV sets
    if (availableUVSets.length <= 1) {
        return null;
    }

    // Initialize UV set tracking for this material
    if (!textureUVSet[material.index]) {
        textureUVSet[material.index] = {};
    }

    // Get current UV set for this texture (default to 0)
    const currentUVSet = textureUVSet[material.index][mapName] !== undefined
        ? textureUVSet[material.index][mapName]
        : 0;

    const uvSetDiv = document.createElement('div');
    uvSetDiv.className = 'texture-uvset';
    uvSetDiv.style.marginTop = '8px';

    const uvSetLabel = document.createElement('label');
    uvSetLabel.textContent = 'UV Set: ';
    uvSetLabel.style.fontSize = '10px';
    uvSetLabel.style.color = '#aaa';
    uvSetDiv.appendChild(uvSetLabel);

    const uvSetSelect = document.createElement('select');
    uvSetSelect.style.fontSize = '10px';
    uvSetSelect.style.padding = '2px 5px';
    uvSetSelect.style.background = '#333';
    uvSetSelect.style.color = 'white';
    uvSetSelect.style.border = '1px solid #555';
    uvSetSelect.style.borderRadius = '3px';
    uvSetSelect.style.cursor = 'pointer';

    // Populate UV set options
    availableUVSets.forEach(uvSet => {
        const option = document.createElement('option');
        option.value = uvSet.index;
        option.textContent = `UV${uvSet.index} (${uvSet.name})`;
        option.selected = (uvSet.index === currentUVSet);
        uvSetSelect.appendChild(option);
    });

    // Handle UV set change
    uvSetSelect.onchange = (e) => {
        const newUVSet = parseInt(e.target.value);
        changeTextureUVSet(material, mapName, newUVSet);
    };

    uvSetDiv.appendChild(uvSetSelect);
    return uvSetDiv;
}

// Change UV set for a texture
function changeTextureUVSet(material, mapName, uvSetIndex) {
    // Store the UV set selection
    if (!textureUVSet[material.index]) {
        textureUVSet[material.index] = {};
    }
    textureUVSet[material.index][mapName] = uvSetIndex;

    // Update the material's shader to use the selected UV set
    // This requires updating the shader's UV attribute mapping
    const threeMaterial = material.threeMaterial;

    if (!threeMaterial.userData.uvSetMappings) {
        threeMaterial.userData.uvSetMappings = {};
    }
    threeMaterial.userData.uvSetMappings[mapName] = uvSetIndex;

    console.log(`Changed UV set for ${mapName} to UV${uvSetIndex}`);

    // For Three.js MeshPhysicalMaterial, we need to use a custom shader
    // to support per-texture UV set selection. For now, we'll store the
    // preference and apply it when we implement custom material shaders.

    // Mark material as needing update
    threeMaterial.needsUpdate = true;
}

// Create texture transform UI controls
function createTextureTransformUI(material, mapName, texture) {
    // Initialize texture transforms
    if (!texture.offset) texture.offset = new THREE.Vector2(0, 0);
    if (!texture.repeat) texture.repeat = new THREE.Vector2(1, 1);
    if (texture.rotation === undefined) texture.rotation = 0;

    const transformDiv = document.createElement('div');
    transformDiv.className = 'texture-transform';
    transformDiv.style.marginTop = '10px';
    transformDiv.style.paddingTop = '10px';
    transformDiv.style.borderTop = '1px solid rgba(255, 255, 255, 0.1)';
    transformDiv.style.fontSize = '10px';

    // Header
    const header = document.createElement('div');
    header.textContent = 'Transform:';
    header.style.color = '#aaa';
    header.style.marginBottom = '5px';
    header.style.fontWeight = 'bold';
    transformDiv.appendChild(header);

    // Helper function to create slider
    const createSlider = (label, value, min, max, step, onChange) => {
        const row = document.createElement('div');
        row.style.marginBottom = '5px';
        row.style.display = 'flex';
        row.style.alignItems = 'center';
        row.style.gap = '5px';

        const labelSpan = document.createElement('span');
        labelSpan.textContent = label;
        labelSpan.style.minWidth = '60px';
        labelSpan.style.color = '#999';
        row.appendChild(labelSpan);

        const slider = document.createElement('input');
        slider.type = 'range';
        slider.min = min;
        slider.max = max;
        slider.step = step;
        slider.value = value;
        slider.style.flex = '1';
        slider.style.cursor = 'pointer';
        row.appendChild(slider);

        const valueSpan = document.createElement('span');
        valueSpan.textContent = value.toFixed(2);
        valueSpan.style.minWidth = '40px';
        valueSpan.style.textAlign = 'right';
        valueSpan.style.color = 'white';
        row.appendChild(valueSpan);

        slider.oninput = (e) => {
            const val = parseFloat(e.target.value);
            valueSpan.textContent = val.toFixed(2);
            onChange(val);
        };

        transformDiv.appendChild(row);
        return slider;
    };

    // Offset X
    createSlider('Offset X', texture.offset.x, -2, 2, 0.01, (value) => {
        texture.offset.x = value;
        texture.needsUpdate = true;
    });

    // Offset Y
    createSlider('Offset Y', texture.offset.y, -2, 2, 0.01, (value) => {
        texture.offset.y = value;
        texture.needsUpdate = true;
    });

    // Scale X
    createSlider('Scale X', texture.repeat.x, 0.1, 10, 0.1, (value) => {
        texture.repeat.x = value;
        texture.needsUpdate = true;
    });

    // Scale Y
    createSlider('Scale Y', texture.repeat.y, 0.1, 10, 0.1, (value) => {
        texture.repeat.y = value;
        texture.needsUpdate = true;
    });

    // Rotation
    createSlider('Rotation', texture.rotation * (180 / Math.PI), 0, 360, 1, (value) => {
        texture.rotation = value * (Math.PI / 180);
        texture.needsUpdate = true;
    });

    // Reset button
    const resetBtn = document.createElement('button');
    resetBtn.textContent = 'Reset Transform';
    resetBtn.className = 'texture-toggle';
    resetBtn.style.width = '100%';
    resetBtn.style.marginTop = '5px';
    resetBtn.style.fontSize = '10px';
    resetBtn.onclick = () => {
        texture.offset.set(0, 0);
        texture.repeat.set(1, 1);
        texture.rotation = 0;
        texture.needsUpdate = true;
        // Refresh panel to update sliders
        updateTexturePanel(material);
    };
    transformDiv.appendChild(resetBtn);

    return transformDiv;
}

// Change texture color space
function changeTextureColorSpace(material, mapName, colorSpace) {
    updateTextureColorSpace(material.threeMaterial, mapName, colorSpace);
    updateStatus(`Changed ${formatTextureName(mapName)} to ${TEXTURE_COLOR_SPACES[colorSpace]}`, 'success');
    console.log(`Updated ${mapName} color space to ${colorSpace}`);
}

// Format texture map name for display
function formatTextureName(mapName) {
    const names = {
        'map': 'Base Color',
        'normalMap': 'Normal',
        'roughnessMap': 'Roughness',
        'metalnessMap': 'Metalness',
        'emissiveMap': 'Emission',
        'aoMap': 'Ambient Occlusion',
        'bumpMap': 'Bump',
        'displacementMap': 'Displacement',
        'alphaMap': 'Alpha'
    };
    return names[mapName] || mapName;
}

// Create texture thumbnail
function createTextureThumbnail(texture) {
    if (!texture || !texture.image) return null;

    const canvas = document.createElement('canvas');
    const maxSize = 200;

    const width = texture.image.width;
    const height = texture.image.height;
    const scale = Math.min(maxSize / width, maxSize / height);

    canvas.width = width * scale;
    canvas.height = height * scale;

    const ctx = canvas.getContext('2d');
    ctx.drawImage(texture.image, 0, 0, canvas.width, canvas.height);

    return canvas;
}

// Toggle texture on/off
function toggleTexture(material, mapName) {
    const matIndex = material.index;
    const currentState = textureEnabled[matIndex][mapName];
    const newState = !currentState;

    textureEnabled[matIndex][mapName] = newState;

    // Apply or remove texture
    const texInfo = material.threeMaterial.userData.textures[mapName];
    if (newState) {
        material.threeMaterial[mapName] = texInfo.texture;
    } else {
        material.threeMaterial[mapName] = null;
    }

    material.threeMaterial.needsUpdate = true;

    // Update UI
    updateTexturePanel(material);
}

// Enlarge texture in a modal
function enlargeTexture(texture, name) {
    // Create modal overlay
    const modal = document.createElement('div');
    modal.style.position = 'fixed';
    modal.style.top = '0';
    modal.style.left = '0';
    modal.style.width = '100%';
    modal.style.height = '100%';
    modal.style.background = 'rgba(0, 0, 0, 0.9)';
    modal.style.zIndex = '10000';
    modal.style.display = 'flex';
    modal.style.flexDirection = 'column';
    modal.style.justifyContent = 'center';
    modal.style.alignItems = 'center';
    modal.style.cursor = 'pointer';

    // Title
    const title = document.createElement('div');
    title.textContent = name;
    title.style.color = 'white';
    title.style.fontSize = '24px';
    title.style.marginBottom = '20px';
    modal.appendChild(title);

    // Image
    const img = document.createElement('canvas');
    img.width = texture.image.width;
    img.height = texture.image.height;
    const ctx = img.getContext('2d');
    ctx.drawImage(texture.image, 0, 0);

    img.style.maxWidth = '90%';
    img.style.maxHeight = '80%';
    img.style.objectFit = 'contain';
    modal.appendChild(img);

    // Info
    const info = document.createElement('div');
    info.textContent = `${texture.image.width}x${texture.image.height} • Click to close`;
    info.style.color = '#999';
    info.style.fontSize = '14px';
    info.style.marginTop = '20px';
    modal.appendChild(info);

    // Close on click
    modal.onclick = () => {
        document.body.removeChild(modal);
    };

    document.body.appendChild(modal);
}

// Select material
function selectMaterial(index) {
    const material = materials[index];
    if (!material) return;

    // Update UI
    document.querySelectorAll('.material-item').forEach(item => {
        item.classList.remove('selected');
    });
    document.querySelector(`[data-index="${index}"]`).classList.add('selected');

    // Set selected material for export
    selectedMaterialForExport = material;

    // Make globally accessible for node graph
    window.selectedMaterialForExport = material;

    // Show export buttons
    const exportSection = document.getElementById('material-export');
    if (exportSection) {
        exportSection.style.display = 'block';
    }

    // Create parameter controls
    createParameterControls(material);

    // Update texture panel
    updateTexturePanel(material);

    // Highlight meshes with this material
    highlightMeshesWithMaterial(index);
}

// Create parameter controls for selected material
function createParameterControls(material) {
    if (paramFolder) {
        gui.removeFolder(paramFolder);
    }

    const materialTypeLabel = material.materialType || 'Unknown';
    paramFolder = gui.addFolder(`Material: ${material.name} [${materialTypeLabel}]`);

    if (!material.parameters || Object.keys(material.parameters).length === 0) {
        paramFolder.add({ message: `No ${materialTypeLabel} parameters` }, 'message').name('Info');
        paramFolder.open();
        return;
    }

    const params = material.parameters;
    const threeMat = material.threeMaterial;

    // Determine which parameter definitions to use
    const paramDefs = material.materialType === 'UsdPreviewSurface' ? USDPREVIEWSURFACE_PARAMS : OPENPBR_PARAMS;

    // Create controls for each parameter group
    Object.entries(paramDefs).forEach(([groupName, groupParams]) => {
        if (params[groupName]) {
            // Add support status labels to certain groups
            let folderLabel = groupName;
            if (groupName === 'specular') {
                folderLabel = 'specular [limited]';
            } else if (groupName === 'subsurface') {
                folderLabel = 'subsurface [not supported]';
            } else if (groupName === 'transmission') {
                folderLabel = 'transmission [not supported]';
            } else if (groupName === 'emission') {
                folderLabel = 'emission [TODO]';
            }

            const groupFolder = paramFolder.addFolder(folderLabel);

            Object.entries(groupParams).forEach(([paramName, paramDef]) => {
                let rawValue = params[groupName][paramName];
                if (rawValue === undefined) return;

                // Check if this parameter has a texture
                const hasTexture = rawValue && typeof rawValue === 'object' && rawValue.textureId !== undefined;

                // Extract actual value if it's wrapped in an object with {name, type, value} structure
                const value = (rawValue && typeof rawValue === 'object' && rawValue.value !== undefined)
                    ? rawValue.value
                    : (hasTexture ? [1, 1, 1] : rawValue); // Default color if texture-only

                if (paramDef.type === 'color') {
                    // Color picker
                    const colorValue = Array.isArray(value) ? value : [1, 1, 1];
                    const colorObj = {
                        //color: [colorValue[0] * 255, colorValue[1] * 255, colorValue[2] * 255]
                        color: [colorValue[0], colorValue[1], colorValue[2]]
                    };
                    const controller = groupFolder.addColor(colorObj, 'color').name(paramName).onChange(val => {
                        const r = val[0]; // / 255;
                        const g = val[1]; // / 255;
                        const b = val[2]; // / 255;
                        console.log("[param color] ", paramName, r, g, b);
                        params[groupName][paramName] = [r, g, b];
                        updateMaterialFromParams(threeMat, params);
                    });

                    // Add texture view button if this parameter has a texture
                    if (hasTexture) {
                        const textureInfo = getTextureInfoForParameter(material, groupName, paramName);
                        if (textureInfo && textureInfo.texture) {
                            const viewBtn = document.createElement('button');
                            viewBtn.textContent = '🖼️';
                            viewBtn.title = 'View texture';
                            viewBtn.style.cssText = 'margin-left: 5px; padding: 2px 8px; background: #2196F3; color: white; border: none; border-radius: 3px; cursor: pointer; font-size: 14px;';
                            viewBtn.onclick = () => {
                                enlargeTexture(textureInfo.texture, formatTextureName(textureInfo.mapName || paramName));
                            };
                            controller.domElement.parentElement.appendChild(viewBtn);
                        }
                    }
                } else if (paramDef.type === 'boolean') {
                    // Checkbox
                    const boolObj = { [paramName]: !!value };
                    groupFolder.add(boolObj, paramName).onChange(val => {
                        params[groupName][paramName] = val;
                        updateMaterialFromParams(threeMat, params);
                    });
                } else {
                    // Slider
                    const numValue = typeof value === 'number' ? value : parseFloat(value) || 0;
                    const sliderObj = { [paramName]: numValue };
                    const controller = groupFolder.add(sliderObj, paramName, paramDef.min, paramDef.max, paramDef.step)
                        .onChange(val => {
                            params[groupName][paramName] = val;
                            updateMaterialFromParams(threeMat, params);
                        });

                    // Set decimal places based on step size
                    if (paramDef.step < 1) {
                        const decimals = Math.max(0, -Math.floor(Math.log10(paramDef.step)));
                        controller.decimals(decimals);
                    }
                }
            });

            // Open first few folders
            if (['base', 'specular', 'emission'].includes(groupName)) {
                groupFolder.open();
            }
        }
    });

    // Add export controls
    const exportFolder = paramFolder.addFolder('Export Material');

    const exportControls = {
        'Export JSON': () => exportCurrentMaterialJSON(material),
        'Export MTLX': () => exportCurrentMaterialMTLX(material)
    };

    exportFolder.add(exportControls, 'Export JSON').name('📄 Export to JSON');
    exportFolder.add(exportControls, 'Export MTLX').name('📄 Export to MaterialX XML');

    exportFolder.open();
    paramFolder.open();
}

// Update Three.js material from OpenPBR parameters
function updateMaterialFromParams(material, params) {
    // Base parameters
    if (params.base) {
        if (params.base.color) {
            console.log("[base_color] ", params.base.color);
            // Handle both array values and texture objects
            const colorValue = Array.isArray(params.base.color) ? params.base.color :
                              (params.base.color.value || [1, 1, 1]);
            const [r, g, b] = srgbToDisplayP3(...colorValue);
            material.color.setRGB(r, g, b);
        }
        if (params.base.metalness !== undefined) {
            material.metalness = params.base.metalness;
        }
    }

    // Specular parameters
    if (params.specular) {
        if (params.specular.roughness !== undefined) {
            material.roughness = params.specular.roughness;
        }
        if (params.specular.ior !== undefined) {
            material.ior = params.specular.ior;
        }
        if (params.specular.color) {
            const colorValue = Array.isArray(params.specular.color) ? params.specular.color :
                              (params.specular.color.value || [1, 1, 1]);
            material.specularColor = createColorWithSpace(...colorValue);
        }
    }

    // Transmission
    if (params.transmission) {
        if (params.transmission.weight !== undefined) {
            material.transmission = params.transmission.weight;
        }
        if (params.transmission.color) {
            const colorValue = Array.isArray(params.transmission.color) ? params.transmission.color :
                              (params.transmission.color.value || [1, 1, 1]);
            material.attenuationColor = createColorWithSpace(...colorValue);
        }
    }

    // Coat
    if (params.coat) {
        if (params.coat.weight !== undefined) {
            material.clearcoat = params.coat.weight;
        }
        if (params.coat.roughness !== undefined) {
            material.clearcoatRoughness = params.coat.roughness;
        }
    }

    // Emission
    // In OpenPBR, final emission = emission_color * emission_luminance
    // Three.js multiplies emissive * emissiveIntensity internally, which matches this
    if (params.emission) {
        if (params.emission.color) {
            const colorValue = Array.isArray(params.emission.color) ? params.emission.color :
                              (params.emission.color.value || [0, 0, 0]);
            material.emissive = createColorWithSpace(...colorValue);
        }
        material.emissiveIntensity = 0.0; // default no emission
        if (params.emission.luminance !== undefined) {
            material.emissiveIntensity = params.emission.luminance;
        }
    }

    // Geometry
    if (params.geometry) {
        if (params.geometry.opacity !== undefined) {
            material.opacity = params.geometry.opacity;
            material.transparent = params.geometry.opacity < 1;
        }
    }

    material.needsUpdate = true;
}

// Highlight meshes with specific material
function highlightMeshesWithMaterial(materialIndex) {
    // Reset all meshes
    meshes.forEach(mesh => {
        mesh.scale.set(1, 1, 1);
    });

    // Highlight meshes with selected material
    meshes.forEach(mesh => {
        if (mesh.userData.materialIndex === materialIndex) {
            mesh.scale.set(1.05, 1.05, 1.05);
        }
    });
}

// Clear scene
function clearScene() {
    // Deselect object and remove bounding box
    deselectObject();

    // Remove meshes (they're in sceneRoot, not directly in scene)
    meshes.forEach(mesh => {
        mesh.geometry.dispose();
        if (sceneRoot) {
            sceneRoot.remove(mesh);
        }
    });
    meshes = [];

    // Dispose materials and textures
    materials.forEach(mat => {
        if (mat.threeMaterial) {
            // Dispose textures
            if (mat.threeMaterial.userData.textures) {
                Object.values(mat.threeMaterial.userData.textures).forEach(texInfo => {
                    if (texInfo.texture) {
                        texInfo.texture.dispose();
                    }
                });
            }
            mat.threeMaterial.dispose();
        }
    });
    materials = [];

    // Clear texture cache
    textureCache.forEach(texture => {
        texture.dispose();
    });
    textureCache.clear();
    textureEnabled = {};

    // Clear original materials map
    originalMaterials.clear();
    showingNormals = false;

    // Reset sceneRoot rotation (for upAxis conversion)
    if (sceneRoot) {
        sceneRoot.rotation.set(0, 0, 0);
    }

    // Reset upAxis to default
    currentFileUpAxis = 'Y';

    // Clear scene metadata
    currentSceneMetadata = null;

    // Clear UI panels
    document.getElementById('material-panel').style.display = 'none';
    document.getElementById('texture-panel').style.display = 'none';
    document.getElementById('model-info').style.display = 'none';
    document.getElementById('scene-metadata').style.display = 'none';
    document.getElementById('selected-object').textContent = 'None';
}

// Fit camera to scene
function fitCameraToScene() {
    if (meshes.length === 0) return;

    const box = new THREE.Box3();
    meshes.forEach(mesh => {
        box.expandByObject(mesh);
    });

    const center = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z);
    const fov = camera.fov * (Math.PI / 180);
    let cameraZ = Math.abs(maxDim / 2 / Math.tan(fov / 2));
    cameraZ *= 1.5; // Add some padding

    camera.position.set(center.x, center.y + size.y * 0.5, center.z + cameraZ);
    camera.lookAt(center);
    controls.target.copy(center);
    controls.update();
}

// Setup file input
function setupFileInput() {
    const fileInput = document.getElementById('file-input');
    fileInput.addEventListener('change', (event) => {
        const file = event.target.files[0];
        if (file) {
            const reader = new FileReader();
            reader.onload = (e) => {
                loadUSDFile(e.target.result, file.name);
            };
            reader.readAsArrayBuffer(file);
        }
    });
}

// Load sample model
async function loadSampleModel() {
    try {
        updateStatus('Loading sample model...');
        showLoading(true);

        // Try to load OpenPBR MaterialX sample first (emissive plane with cat texture)
        const response = await fetch('../../models/openpbr-emissive-plane.usda');

        if (!response.ok) {
            throw new Error(`Failed to fetch sample model: ${response.statusText}`);
        }

        const arrayBuffer = await response.arrayBuffer();
        await loadUSDFile(arrayBuffer, 'openpbr-emissive-plane.usda');

    } catch (error) {
        console.error('Error loading sample model:', error);
        updateStatus(`Failed to load sample: ${error.message}`, 'error');

        // Try alternative sample - multi-object scene
        try {
            const response = await fetch('../../models/openpbr-multi-object.usda');
            if (response.ok) {
                const arrayBuffer = await response.arrayBuffer();
                await loadUSDFile(arrayBuffer, 'openpbr-multi-object.usda');
            } else {
                // Final fallback to simple plane
                const response2 = await fetch('../../models/simple-plane.usda');
                if (response2.ok) {
                    const arrayBuffer2 = await response2.arrayBuffer();
                    await loadUSDFile(arrayBuffer2, 'simple-plane.usda');
                }
            }
        } catch (err) {
            console.error('Alternative sample also failed:', err);
        }
    } finally {
        showLoading(false);
    }
}

// Mouse interaction handlers
function onMouseClick(event) {
    // Check if material property picker mode is active (priority 1)
    if (isMaterialPropertyPickerActive()) {
        // Handle material property picking
        const handled = handleMaterialPropertyPickerClick(event, renderer);
        if (handled) {
            return; // Don't do other modes
        }
    }

    // Check if color picker mode is active (priority 2)
    if (isColorPickerActive()) {
        // Handle color picking
        const handled = handleColorPickerClick(event, renderer);
        if (handled) {
            return; // Don't do object selection in color picker mode
        }
    }

    // Normal object selection mode
    // Calculate mouse position in normalized device coordinates
    mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
    mouse.y = -(event.clientY / window.innerHeight) * 2 + 1;

    // Update the picking ray with the camera and mouse position
    raycaster.setFromCamera(mouse, camera);

    // Calculate objects intersecting the picking ray
    const intersects = raycaster.intersectObjects(meshes);

    if (intersects.length > 0) {
        selectObject(intersects[0].object);
    } else {
        deselectObject();
    }
}

function onMouseMove(event) {
    // Update mouse position for hover effects
    mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
    mouse.y = -(event.clientY / window.innerHeight) * 2 + 1;
}

// Object selection
function selectObject(object) {
    // Deselect previous
    if (selectedObject) {
        selectedObject.scale.set(1, 1, 1);
    }

    // Remove previous bounding box
    if (boundingBoxHelper) {
        scene.remove(boundingBoxHelper);
        boundingBoxHelper.dispose();
        boundingBoxHelper = null;
    }

    selectedObject = object;
    selectedObject.scale.set(1.1, 1.1, 1.1);

    // Create bounding box helper
    boundingBoxHelper = new THREE.BoxHelper(object, 0x00ff00);
    scene.add(boundingBoxHelper);

    // Update UI
    document.getElementById('selected-object').textContent = object.name;

    console.log(`Selected: ${object.name}, BBox added`);

    // Select corresponding material
    const materialIndex = object.userData.materialIndex;
    if (materialIndex !== undefined) {
        selectMaterial(materialIndex);
    }
}

function deselectObject() {
    if (selectedObject) {
        selectedObject.scale.set(1, 1, 1);
        selectedObject = null;
    }

    // Remove bounding box
    if (boundingBoxHelper) {
        scene.remove(boundingBoxHelper);
        boundingBoxHelper.dispose();
        boundingBoxHelper = null;
    }

    document.getElementById('selected-object').textContent = 'None';
}

// Utility functions
function updateStatus(message, type = '') {
    const statusEl = document.getElementById('status');
    statusEl.textContent = message;
    statusEl.className = 'status' + (type ? ` ${type}` : '');
}

function showLoading(show) {
    document.getElementById('loading-overlay').style.display = show ? 'flex' : 'none';
}

function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);

    // Update composer size if it exists
    if (composer) {
        composer.setSize(window.innerWidth, window.innerHeight);
    }

    // Update material property picker render targets
    const width = renderer.domElement.width;
    const height = renderer.domElement.height;
    resizeMaterialPropertyTargets(width, height);
}

function animate() {
    requestAnimationFrame(animate);
    controls.update();

    // Update bounding box if an object is selected
    if (boundingBoxHelper && selectedObject) {
        boundingBoxHelper.update();
    }

    // Use composer if any post-processing is enabled (false color or custom ACES)
    const useComposer = showingFalseColor ||
                        (toneMappingType === 'aces13' || toneMappingType === 'aces20');

    if (useComposer && composer) {
        composer.render();
    } else {
        renderer.render(scene, camera);
    }
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', init);

// Expose functions to global scope for HTML onclick handlers
window.loadSampleModel = loadSampleModel;
window.toggleEnvironment = toggleEnvironment;
window.toggleBackgroundEnvmap = toggleBackgroundEnvmap;
window.toggleColorSpace = toggleColorSpace;
window.toggleFalseColor = toggleFalseColor;
window.importMaterialXFile = importMaterialXFile;
window.loadHDRTextureForMaterial = loadHDRTextureForMaterial;
window.exportSelectedMaterialJSON = exportSelectedMaterialJSON;
window.exportSelectedMaterialMTLX = exportSelectedMaterialMTLX;

// Import MaterialX XML file and apply to selected object
async function importMaterialXFile() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.mtlx,.xml';

    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;

        try {
            showLoading(true);
            updateStatus('Importing MaterialX file...');

            const text = await file.text();
            const materialData = parseMaterialXXML(text);

            if (!materialData) {
                throw new Error('Failed to parse MaterialX XML');
            }

            // Apply to selected object or create new material
            if (selectedObject) {
                applyImportedMaterial(selectedObject, materialData);
                updateStatus(`Applied MaterialX to ${selectedObject.name}`, 'success');
            } else {
                updateStatus('No object selected. Select an object first.', 'error');
            }

        } catch (error) {
            console.error('Error importing MaterialX:', error);
            updateStatus(`Import failed: ${error.message}`, 'error');
        } finally {
            showLoading(false);
        }
    };

    input.click();
}

// Parse MaterialX XML to extract OpenPBR parameters
function parseMaterialXXML(xmlText) {
    try {
        const parser = new DOMParser();
        const xmlDoc = parser.parseFromString(xmlText, 'text/xml');

        // Check for parsing errors
        const parserError = xmlDoc.getElementsByTagName('parsererror');
        if (parserError.length > 0) {
            throw new Error('XML parsing error: ' + parserError[0].textContent);
        }

        // Find open_pbr_surface node
        const pbrNode = xmlDoc.querySelector('open_pbr_surface');
        if (!pbrNode) {
            throw new Error('No open_pbr_surface node found in MaterialX file');
        }

        const materialName = pbrNode.getAttribute('name') || 'Imported_Material';

        // Extract parameters
        const material = {
            name: materialName,
            base: {},
            specular: {},
            transmission: {},
            coat: {},
            emission: {},
            geometry: {},
            textures: {}
        };

        // Parse all input nodes
        const inputs = pbrNode.getElementsByTagName('input');
        for (let input of inputs) {
            const name = input.getAttribute('name');
            const type = input.getAttribute('type');
            const value = input.getAttribute('value');
            const nodename = input.getAttribute('nodename');

            // Handle texture references
            if (nodename) {
                material.textures[name] = {
                    nodename: nodename,
                    nodeRef: nodename
                };
                continue;
            }

            // Parse values based on parameter name
            if (name === 'base_color' && value) {
                material.base.color = parseColor3(value);
            } else if (name === 'base_weight' && value) {
                material.base.weight = parseFloat(value);
            } else if (name === 'base_metalness' && value) {
                material.base.metalness = parseFloat(value);
            } else if (name === 'base_diffuse_roughness' && value) {
                material.base.roughness = parseFloat(value);
            } else if (name === 'specular_weight' && value) {
                material.specular.weight = parseFloat(value);
            } else if (name === 'specular_color' && value) {
                material.specular.color = parseColor3(value);
            } else if (name === 'specular_roughness' && value) {
                material.specular.roughness = parseFloat(value);
            } else if (name === 'specular_ior' && value) {
                material.specular.ior = parseFloat(value);
            } else if (name === 'specular_anisotropy' && value) {
                material.specular.anisotropy = parseFloat(value);
            } else if (name === 'transmission_weight' && value) {
                material.transmission.weight = parseFloat(value);
            } else if (name === 'transmission_color' && value) {
                material.transmission.color = parseColor3(value);
            } else if (name === 'coat_weight' && value) {
                material.coat.weight = parseFloat(value);
            } else if (name === 'coat_roughness' && value) {
                material.coat.roughness = parseFloat(value);
            } else if (name === 'emission_color' && value) {
                material.emission.color = parseColor3(value);
            } else if (name === 'emission_luminance' && value) {
                material.emission.luminance = parseFloat(value);
            } else if (name === 'geometry_opacity' && value) {
                material.geometry.opacity = parseFloat(value);
            }
        }

        // Parse texture nodes
        const imageNodes = xmlDoc.getElementsByTagName('image');
        for (let imageNode of imageNodes) {
            const nodeName = imageNode.getAttribute('name');
            const fileInput = imageNode.querySelector('input[name="file"]');
            const colorspaceInput = imageNode.querySelector('input[name="colorspace"]');
            const channelInput = imageNode.querySelector('input[name="channel"]');

            if (fileInput && nodeName) {
                const filename = fileInput.getAttribute('value');
                const colorspace = colorspaceInput ? colorspaceInput.getAttribute('value') : 'srgb';
                const channel = channelInput ? channelInput.getAttribute('value') : 'rgb';

                material.textures[nodeName] = {
                    file: filename,
                    colorspace: colorspace,
                    channel: channel
                };
            }
        }

        return material;

    } catch (error) {
        console.error('Error parsing MaterialX XML:', error);
        throw error;
    }
}

// Parse color3 string "r, g, b" to array [r, g, b]
function parseColor3(colorStr) {
    const parts = colorStr.split(',').map(s => parseFloat(s.trim()));
    if (parts.length === 3 && parts.every(n => !isNaN(n))) {
        return parts;
    }
    return [0.8, 0.8, 0.8]; // Default gray
}

// Apply imported MaterialX material to object
function applyImportedMaterial(object, materialData) {
    // Create new Three.js material
    const material = new THREE.MeshPhysicalMaterial({
        name: materialData.name,
        side: THREE.DoubleSide,
        envMapIntensity: exposureValue
    });

    // Apply base parameters
    if (materialData.base.color) {
        const colorValue = Array.isArray(materialData.base.color) ? materialData.base.color :
                          (materialData.base.color.value || [1, 1, 1]);
        material.color = createColorWithSpace(...colorValue);
    }
    if (materialData.base.metalness !== undefined) {
        material.metalness = materialData.base.metalness;
    }
    if (materialData.base.roughness !== undefined) {
        material.roughness = materialData.base.roughness;
    }

    // Apply specular parameters
    if (materialData.specular.roughness !== undefined) {
        material.roughness = materialData.specular.roughness;
    }
    if (materialData.specular.ior !== undefined) {
        material.ior = materialData.specular.ior;
    }

    // Apply transmission
    if (materialData.transmission.weight !== undefined) {
        material.transmission = materialData.transmission.weight;
        material.thickness = 1.0;
    }

    // Apply coat (clearcoat)
    if (materialData.coat.weight !== undefined) {
        material.clearcoat = materialData.coat.weight;
    }
    if (materialData.coat.roughness !== undefined) {
        material.clearcoatRoughness = materialData.coat.roughness;
    }

    // Apply emission
    if (materialData.emission.color) {
        const colorValue = Array.isArray(materialData.emission.color) ? materialData.emission.color :
                          (materialData.emission.color.value || [0, 0, 0]);
        material.emissive = createColorWithSpace(...colorValue);
    }
    if (materialData.emission.luminance !== undefined) {
        material.emissiveIntensity = materialData.emission.luminance;
    }

    // Apply geometry
    if (materialData.geometry.opacity !== undefined) {
        material.opacity = materialData.geometry.opacity;
        material.transparent = material.opacity < 1.0;
    }

    // Apply material to object
    object.material = material;
    material.needsUpdate = true;

    // Update GUI if this is the selected object
    if (object === selectedObject) {
        updateGUIForMaterial(material);
    }

    console.log('Applied imported MaterialX material:', materialData.name);
}

// Load external texture file (for HDR/EXR support)
function loadExternalTexture(file, onLoad, onError) {
    const filename = file.name.toLowerCase();

    // Check file extension
    if (filename.endsWith('.exr')) {
        // Use Three.js EXRLoader
        const loader = new EXRLoader();
        const reader = new FileReader();

        reader.onload = (e) => {
            loader.load(
                URL.createObjectURL(new Blob([e.target.result])),
                (texture) => {
                    texture.mapping = THREE.EquirectangularReflectionMapping;
                    texture.colorSpace = THREE.LinearSRGBColorSpace;
                    if (onLoad) onLoad(texture);
                },
                undefined,
                (error) => {
                    console.error('Error loading EXR:', error);
                    if (onError) onError(error);
                }
            );
        };

        reader.readAsArrayBuffer(file);

    } else if (filename.endsWith('.hdr')) {
        // Use Three.js RGBELoader
        const loader = new RGBELoader();
        const reader = new FileReader();

        reader.onload = (e) => {
            loader.load(
                URL.createObjectURL(new Blob([e.target.result])),
                (texture) => {
                    texture.mapping = THREE.EquirectangularReflectionMapping;
                    texture.colorSpace = THREE.LinearSRGBColorSpace;
                    if (onLoad) onLoad(texture);
                },
                undefined,
                (error) => {
                    console.error('Error loading HDR:', error);
                    if (onError) onError(error);
                }
            );
        };

        reader.readAsArrayBuffer(file);

    } else {
        // Regular image texture
        const reader = new FileReader();
        reader.onload = (e) => {
            const loader = new THREE.TextureLoader();
            loader.load(
                e.target.result,
                (texture) => {
                    texture.colorSpace = THREE.SRGBColorSpace;
                    if (onLoad) onLoad(texture);
                },
                undefined,
                (error) => {
                    console.error('Error loading texture:', error);
                    if (onError) onError(error);
                }
            );
        };
        reader.readAsDataURL(file);
    }
}

// Load HDR/EXR texture for material parameter
function loadHDRTextureForMaterial() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.hdr,.exr,.png,.jpg,.jpeg';

    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;

        if (!selectedObject) {
            updateStatus('Select an object first', 'error');
            return;
        }

        showLoading(true);
        updateStatus('Loading texture...');

        loadExternalTexture(
            file,
            (texture) => {
                // Apply to selected material's base color map
                if (selectedObject.material) {
                    selectedObject.material.map = texture;
                    selectedObject.material.needsUpdate = true;
                    updateStatus(`Loaded ${file.name}`, 'success');
                }
                showLoading(false);
            },
            (error) => {
                updateStatus(`Failed to load texture: ${error.message}`, 'error');
                showLoading(false);
            }
        );
    };

    input.click();
}

// Add texture transform controls to GUI
function addTextureTransformControls(folder, material, textureName) {
    const texture = material[textureName];
    if (!texture) return;

    // Initialize transform if not present
    if (!texture.offset) texture.offset = new THREE.Vector2(0, 0);
    if (!texture.repeat) texture.repeat = new THREE.Vector2(1, 1);
    if (texture.rotation === undefined) texture.rotation = 0;

    const transformFolder = folder.addFolder(`${textureName} Transform`);

    // Offset controls
    const offsetParams = {
        offsetX: texture.offset.x,
        offsetY: texture.offset.y
    };

    transformFolder.add(offsetParams, 'offsetX', -2, 2, 0.01).name('Offset X').onChange((value) => {
        texture.offset.x = value;
        texture.needsUpdate = true;
    });

    transformFolder.add(offsetParams, 'offsetY', -2, 2, 0.01).name('Offset Y').onChange((value) => {
        texture.offset.y = value;
        texture.needsUpdate = true;
    });

    // Scale/Repeat controls
    const scaleParams = {
        scaleX: texture.repeat.x,
        scaleY: texture.repeat.y
    };

    transformFolder.add(scaleParams, 'scaleX', 0.1, 10, 0.1).name('Scale X').onChange((value) => {
        texture.repeat.x = value;
        texture.needsUpdate = true;
    });

    transformFolder.add(scaleParams, 'scaleY', 0.1, 10, 0.1).name('Scale Y').onChange((value) => {
        texture.repeat.y = value;
        texture.needsUpdate = true;
    });

    // Rotation control
    const rotationParam = { rotation: texture.rotation };
    transformFolder.add(rotationParam, 'rotation', 0, Math.PI * 2, 0.01).name('Rotation').onChange((value) => {
        texture.rotation = value * (Math.PI / 180);
        texture.needsUpdate = true;
    });

    return transformFolder;
}

// Validate texture ID
function validateTextureId(textureId, context = '') {
    if (textureId === undefined || textureId === null) {
        console.warn(`${context}: Texture ID is undefined or null`);
        return false;
    }

    if (textureId < 0) {
        console.warn(`${context}: Invalid texture ID ${textureId} (negative)`);
        return false;
    }

    if (!Number.isInteger(textureId)) {
        console.warn(`${context}: Texture ID ${textureId} is not an integer`);
        return false;
    }

    return true;
}

// Validate material index
function validateMaterialIndex(index, maxIndex, context = '') {
    if (index === undefined || index === null) {
        console.error(`${context}: Material index is undefined or null`);
        return false;
    }

    if (index < 0 || index >= maxIndex) {
        console.error(`${context}: Material index ${index} out of range [0, ${maxIndex})`);
        return false;
    }

    return true;
}

// Enhanced error reporting
function reportError(context, error, severity = 'error') {
    const message = `[${context}] ${error.message || error}`;
    console.error(message, error);

    // Show user-friendly error
    let userMessage = message;
    if (error.message) {
        // Simplify common errors
        if (error.message.includes('texture')) {
            userMessage = `Texture loading failed. Check console for details.`;
        } else if (error.message.includes('material')) {
            userMessage = `Material processing failed. Check console for details.`;
        } else if (error.message.includes('parse')) {
            userMessage = `File parsing failed. Check file format.`;
        }
    }

    updateStatus(userMessage, severity);
}