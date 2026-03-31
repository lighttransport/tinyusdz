// Material Property Picker - Pick material values before lighting
// Uses render targets to capture material properties (base color, roughness, metalness, etc.)

import * as THREE from 'three';

let propertyPickerActive = false;
let propertyPickerRenderer = null;
let propertyPickerScene = null;
let propertyPickerCamera = null;

// Render targets for material properties
let baseColorTarget = null;
let materialPropsTarget = null; // R=metalness, G=roughness, B=unused, A=unused

let lastPickedProperties = null;

// Custom shader to render material properties without lighting
const materialPropertyShader = {
    vertexShader: `
        varying vec2 vUv;
        varying vec3 vNormal;

        void main() {
            vUv = uv;
            vNormal = normalize(normalMatrix * normal);
            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
    `,

    // Fragment shader for base color (with texture support)
    baseColorFragmentShader: `
        uniform vec3 baseColor;
        uniform sampler2D baseColorMap;
        uniform bool hasBaseColorMap;
        varying vec2 vUv;

        void main() {
            vec3 color = baseColor;

            if (hasBaseColorMap) {
                vec4 texColor = texture2D(baseColorMap, vUv);
                color = texColor.rgb;
            }

            gl_FragColor = vec4(color, 1.0);
        }
    `,

    // Fragment shader for material properties (metalness, roughness)
    materialPropsFragmentShader: `
        uniform float metalness;
        uniform float roughness;
        uniform sampler2D metalnessMap;
        uniform sampler2D roughnessMap;
        uniform bool hasMetalnessMap;
        uniform bool hasRoughnessMap;
        varying vec2 vUv;

        void main() {
            float m = metalness;
            float r = roughness;

            if (hasMetalnessMap) {
                m = texture2D(metalnessMap, vUv).b; // Metalness usually in blue channel
            }

            if (hasRoughnessMap) {
                r = texture2D(roughnessMap, vUv).g; // Roughness usually in green channel
            }

            gl_FragColor = vec4(m, r, 0.0, 1.0);
        }
    `
};

// Initialize material property picker
export function initializeMaterialPropertyPicker(renderer, scene, camera) {
    propertyPickerRenderer = renderer;
    propertyPickerScene = scene;
    propertyPickerCamera = camera;

    const width = renderer.domElement.width;
    const height = renderer.domElement.height;

    // Create render targets
    baseColorTarget = new THREE.WebGLRenderTarget(width, height, {
        minFilter: THREE.NearestFilter,
        magFilter: THREE.NearestFilter,
        format: THREE.RGBAFormat,
        type: THREE.UnsignedByteType
    });

    materialPropsTarget = new THREE.WebGLRenderTarget(width, height, {
        minFilter: THREE.NearestFilter,
        magFilter: THREE.NearestFilter,
        format: THREE.RGBAFormat,
        type: THREE.UnsignedByteType
    });

    console.log('Material property picker initialized');
}

// Toggle material property picker mode
export function toggleMaterialPropertyPickerMode() {
    propertyPickerActive = !propertyPickerActive;

    const panel = document.getElementById('material-property-panel');
    const button = document.getElementById('material-property-btn');
    const body = document.body;

    if (propertyPickerActive) {
        // Enable picker mode
        panel.classList.add('active');
        button.classList.add('active');
        body.classList.add('material-property-picker-mode');
        console.log('Material property picker mode: ON');
    } else {
        // Disable picker mode
        panel.classList.remove('active');
        button.classList.remove('active');
        body.classList.remove('material-property-picker-mode');
        console.log('Material property picker mode: OFF');
    }
}

// Check if material property picker is active
export function isMaterialPropertyPickerActive() {
    return propertyPickerActive;
}

// Create material override for property rendering
function createPropertyMaterial(originalMaterial, mode) {
    const uniforms = {
        baseColor: { value: new THREE.Color(1, 1, 1) },
        baseColorMap: { value: null },
        hasBaseColorMap: { value: false },
        metalness: { value: 0.0 },
        roughness: { value: 1.0 },
        metalnessMap: { value: null },
        roughnessMap: { value: null },
        hasMetalnessMap: { value: false },
        hasRoughnessMap: { value: false }
    };

    // Extract properties from original material
    if (originalMaterial) {
        if (originalMaterial.color) {
            uniforms.baseColor.value.copy(originalMaterial.color);
        }

        if (originalMaterial.map) {
            uniforms.baseColorMap.value = originalMaterial.map;
            uniforms.hasBaseColorMap.value = true;
        }

        if (originalMaterial.metalness !== undefined) {
            uniforms.metalness.value = originalMaterial.metalness;
        }

        if (originalMaterial.roughness !== undefined) {
            uniforms.roughness.value = originalMaterial.roughness;
        }

        if (originalMaterial.metalnessMap) {
            uniforms.metalnessMap.value = originalMaterial.metalnessMap;
            uniforms.hasMetalnessMap.value = true;
        }

        if (originalMaterial.roughnessMap) {
            uniforms.roughnessMap.value = originalMaterial.roughnessMap;
            uniforms.hasRoughnessMap.value = true;
        }
    }

    const fragmentShader = mode === 'baseColor'
        ? materialPropertyShader.baseColorFragmentShader
        : materialPropertyShader.materialPropsFragmentShader;

    return new THREE.ShaderMaterial({
        uniforms: uniforms,
        vertexShader: materialPropertyShader.vertexShader,
        fragmentShader: fragmentShader
    });
}

// Render material properties to render targets
function renderMaterialProperties() {
    if (!propertyPickerScene || !propertyPickerCamera || !propertyPickerRenderer) {
        console.error('Material property picker not initialized');
        return false;
    }

    // Store original materials
    const originalMaterials = new Map();

    propertyPickerScene.traverse((object) => {
        if (object.isMesh && object.material) {
            originalMaterials.set(object, object.material);
        }
    });

    // Render base color
    propertyPickerScene.traverse((object) => {
        if (object.isMesh && originalMaterials.has(object)) {
            const originalMaterial = originalMaterials.get(object);
            object.material = createPropertyMaterial(originalMaterial, 'baseColor');
        }
    });

    propertyPickerRenderer.setRenderTarget(baseColorTarget);
    propertyPickerRenderer.render(propertyPickerScene, propertyPickerCamera);

    // Render material properties (metalness, roughness)
    propertyPickerScene.traverse((object) => {
        if (object.isMesh && originalMaterials.has(object)) {
            const originalMaterial = originalMaterials.get(object);
            object.material = createPropertyMaterial(originalMaterial, 'materialProps');
        }
    });

    propertyPickerRenderer.setRenderTarget(materialPropsTarget);
    propertyPickerRenderer.render(propertyPickerScene, propertyPickerCamera);

    // Restore original materials
    originalMaterials.forEach((material, object) => {
        object.material = material;
    });

    // Reset render target
    propertyPickerRenderer.setRenderTarget(null);

    return true;
}

// Pick material properties at position
export function pickMaterialPropertiesAtPosition(x, y, renderer) {
    if (!renderer) {
        console.error('No renderer provided for material property picking');
        return null;
    }

    // Render material properties to targets
    const success = renderMaterialProperties();
    if (!success) {
        return null;
    }

    // Get renderer size
    const width = renderer.domElement.width;
    const height = renderer.domElement.height;

    // Convert mouse coordinates to WebGL coordinates
    const pixelX = Math.floor(x);
    const pixelY = Math.floor(height - y); // Flip Y coordinate

    // Clamp to valid range
    const clampedX = Math.max(0, Math.min(width - 1, pixelX));
    const clampedY = Math.max(0, Math.min(height - 1, pixelY));

    const gl = renderer.getContext();

    try {
        // Read base color
        const baseColorBuffer = new Uint8Array(4);
        renderer.setRenderTarget(baseColorTarget);
        gl.readPixels(
            clampedX,
            clampedY,
            1, 1,
            gl.RGBA,
            gl.UNSIGNED_BYTE,
            baseColorBuffer
        );

        // Read material properties
        const materialPropsBuffer = new Uint8Array(4);
        renderer.setRenderTarget(materialPropsTarget);
        gl.readPixels(
            clampedX,
            clampedY,
            1, 1,
            gl.RGBA,
            gl.UNSIGNED_BYTE,
            materialPropsBuffer
        );

        // Reset render target
        renderer.setRenderTarget(null);

        // Extract values
        const baseColor = {
            r: baseColorBuffer[0],
            g: baseColorBuffer[1],
            b: baseColorBuffer[2]
        };

        const metalness = materialPropsBuffer[0] / 255.0;
        const roughness = materialPropsBuffer[1] / 255.0;

        // Convert base color to various formats
        const baseColorFloat = {
            r: baseColor.r / 255.0,
            g: baseColor.g / 255.0,
            b: baseColor.b / 255.0
        };

        // Convert to linear (assuming sRGB texture)
        const baseColorLinear = {
            r: sRGBToLinear(baseColorFloat.r),
            g: sRGBToLinear(baseColorFloat.g),
            b: sRGBToLinear(baseColorFloat.b)
        };

        const propertyData = {
            // Base color (texel value)
            baseColor: {
                rgb: baseColor,
                float: baseColorFloat,
                linear: baseColorLinear,
                hex: rgbToHex(baseColor.r, baseColor.g, baseColor.b)
            },

            // Material properties
            metalness: metalness,
            roughness: roughness,

            // Position
            position: { x: clampedX, y: clampedY }
        };

        return propertyData;
    } catch (error) {
        console.error('Error reading material properties:', error);
        return null;
    }
}

// sRGB to Linear conversion
function sRGBToLinear(value) {
    if (value <= 0.04045) {
        return value / 12.92;
    } else {
        return Math.pow((value + 0.055) / 1.055, 2.4);
    }
}

// Convert RGB to Hex
function rgbToHex(r, g, b) {
    const toHex = (n) => {
        const hex = Math.round(n).toString(16).padStart(2, '0');
        return hex;
    };
    return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

// Display picked material properties in UI
export function displayPickedMaterialProperties(propertyData, mouseX, mouseY) {
    if (!propertyData) return;

    lastPickedProperties = propertyData;

    // Update base color swatch
    const swatch = document.getElementById('material-property-swatch');
    if (swatch) {
        swatch.style.backgroundColor = propertyData.baseColor.hex;
    }

    // Update base color RGB (0-255)
    const rgbElement = document.getElementById('material-property-rgb');
    if (rgbElement) {
        rgbElement.textContent = `${propertyData.baseColor.rgb.r}, ${propertyData.baseColor.rgb.g}, ${propertyData.baseColor.rgb.b}`;
    }

    // Update base color Hex
    const hexElement = document.getElementById('material-property-hex');
    if (hexElement) {
        hexElement.textContent = propertyData.baseColor.hex.toUpperCase();
    }

    // Update base color Float (0-1, sRGB)
    const floatElement = document.getElementById('material-property-float');
    if (floatElement) {
        const r = propertyData.baseColor.float.r.toFixed(4);
        const g = propertyData.baseColor.float.g.toFixed(4);
        const b = propertyData.baseColor.float.b.toFixed(4);
        floatElement.textContent = `${r}, ${g}, ${b}`;
    }

    // Update base color Linear (0-1, linear RGB)
    const linearElement = document.getElementById('material-property-linear');
    if (linearElement) {
        const r = propertyData.baseColor.linear.r.toFixed(4);
        const g = propertyData.baseColor.linear.g.toFixed(4);
        const b = propertyData.baseColor.linear.b.toFixed(4);
        linearElement.textContent = `${r}, ${g}, ${b}`;
    }

    // Update metalness
    const metalnessElement = document.getElementById('material-property-metalness');
    if (metalnessElement) {
        metalnessElement.textContent = propertyData.metalness.toFixed(4);
    }

    // Update roughness
    const roughnessElement = document.getElementById('material-property-roughness');
    if (roughnessElement) {
        roughnessElement.textContent = propertyData.roughness.toFixed(4);
    }

    // Update position
    const positionElement = document.getElementById('material-property-position');
    if (positionElement) {
        positionElement.textContent = `(${mouseX}, ${mouseY}) → (${propertyData.position.x}, ${propertyData.position.y})`;
    }

    console.log('Picked material properties:', propertyData);
}

// Handle click for material property picking
export function handleMaterialPropertyPickerClick(event, renderer) {
    if (!propertyPickerActive || !renderer) return false;

    // Get mouse position relative to canvas
    const rect = renderer.domElement.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;

    // Convert to device pixels
    const dpr = window.devicePixelRatio || 1;
    const canvasX = x * dpr;
    const canvasY = y * dpr;

    // Pick material properties at position
    const propertyData = pickMaterialPropertiesAtPosition(canvasX, canvasY, renderer);

    if (propertyData) {
        displayPickedMaterialProperties(propertyData, Math.floor(x), Math.floor(y));
        return true; // Event handled
    }

    return false;
}

// Copy material property value to clipboard
export function copyMaterialPropertyToClipboard(format) {
    if (!lastPickedProperties) {
        alert('No material properties picked yet');
        return;
    }

    let textToCopy = '';

    switch (format) {
        case 'rgb':
            textToCopy = `${lastPickedProperties.baseColor.rgb.r}, ${lastPickedProperties.baseColor.rgb.g}, ${lastPickedProperties.baseColor.rgb.b}`;
            break;
        case 'hex':
            textToCopy = lastPickedProperties.baseColor.hex.toUpperCase();
            break;
        case 'float':
            const r = lastPickedProperties.baseColor.float.r.toFixed(4);
            const g = lastPickedProperties.baseColor.float.g.toFixed(4);
            const b = lastPickedProperties.baseColor.float.b.toFixed(4);
            textToCopy = `${r}, ${g}, ${b}`;
            break;
        case 'linear':
            const rl = lastPickedProperties.baseColor.linear.r.toFixed(4);
            const gl = lastPickedProperties.baseColor.linear.g.toFixed(4);
            const bl = lastPickedProperties.baseColor.linear.b.toFixed(4);
            textToCopy = `${rl}, ${gl}, ${bl}`;
            break;
        case 'metalness':
            textToCopy = lastPickedProperties.metalness.toFixed(4);
            break;
        case 'roughness':
            textToCopy = lastPickedProperties.roughness.toFixed(4);
            break;
        case 'all':
            const all = {
                baseColor: {
                    rgb: lastPickedProperties.baseColor.rgb,
                    hex: lastPickedProperties.baseColor.hex,
                    float: lastPickedProperties.baseColor.float,
                    linear: lastPickedProperties.baseColor.linear
                },
                metalness: lastPickedProperties.metalness,
                roughness: lastPickedProperties.roughness
            };
            textToCopy = JSON.stringify(all, null, 2);
            break;
        default:
            console.error('Unknown format:', format);
            return;
    }

    navigator.clipboard.writeText(textToCopy).then(() => {
        console.log(`Copied ${format}:`, textToCopy);

        // Show feedback
        const btn = event.target;
        const originalText = btn.textContent;
        btn.textContent = '✓ Copied!';
        setTimeout(() => {
            btn.textContent = originalText;
        }, 1500);
    }).catch(err => {
        console.error('Failed to copy:', err);
        alert('Failed to copy to clipboard');
    });
}

// Get last picked material properties
export function getLastPickedMaterialProperties() {
    return lastPickedProperties;
}

// Reset material property picker state
export function resetMaterialPropertyPicker() {
    propertyPickerActive = false;
    lastPickedProperties = null;

    const panel = document.getElementById('material-property-panel');
    const button = document.getElementById('material-property-btn');
    const body = document.body;

    if (panel) panel.classList.remove('active');
    if (button) button.classList.remove('active');
    if (body) body.classList.remove('material-property-picker-mode');
}

// Resize render targets when window resizes
export function resizeMaterialPropertyTargets(width, height) {
    if (baseColorTarget) {
        baseColorTarget.setSize(width, height);
    }
    if (materialPropsTarget) {
        materialPropsTarget.setSize(width, height);
    }
}

// Make functions globally accessible
if (typeof window !== 'undefined') {
    window.toggleMaterialPropertyPicker = toggleMaterialPropertyPickerMode;
    window.copyMaterialProperty = copyMaterialPropertyToClipboard;
}
