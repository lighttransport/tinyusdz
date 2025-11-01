// TinyUSDZ MaterialX/OpenPBR Demo with Three.js
// This demo showcases OpenPBR material loading and editing with synthetic HDR environments

// Global variables
let scene, camera, renderer, controls;
let raycaster, mouse;
let selectedObject = null;
let tinyUSDZModule = null;
let currentLoader = null;
let materials = [];
let meshes = [];
let gui = null;
let paramFolder = null;
let environmentType = 'studio'; // 'studio' or 'white'
let pmremGenerator = null;
let currentColorSpace = 'srgb'; // 'srgb' or 'display-p3'
let displayP3Supported = false;
let textureCache = new Map(); // Cache for loaded textures
let textureEnabled = {}; // Track which textures are enabled per material
let textureColorSpace = {}; // Track color space per texture per material
let textureUVSet = {}; // Track UV set selection per texture per material

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

        if (textureColorSpaces.map !== undefined) {
            uniformsToAdd.mapColorSpace = { value: getColorSpaceIndex(textureColorSpaces.map) };
        }
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

        // Modify shader code to apply color space conversions
        // For base color map
        if (textureColorSpaces.map !== undefined) {
            shader.fragmentShader = shader.fragmentShader.replace(
                '#include <map_fragment>',
                `
                #ifdef USE_MAP
                    vec4 sampledDiffuseColor = texture2D( map, vMapUv );
                    sampledDiffuseColor.rgb = convertColorSpace(sampledDiffuseColor.rgb, mapColorSpace);
                    #ifdef DECODE_VIDEO_TEXTURE
                        sampledDiffuseColor = vec4( mix( pow( sampledDiffuseColor.rgb * 0.9478672986 + vec3( 0.0521327014 ), vec3( 2.4 ) ), sampledDiffuseColor.rgb * 0.0773993808, vec3( lessThanEqual( sampledDiffuseColor.rgb, vec3( 0.04045 ) ) ) ), sampledDiffuseColor.w );
                    #endif
                    diffuseColor *= sampledDiffuseColor;
                #endif
                `
            );
        }

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
            intensity: mat.emissiveIntensity || 1.0
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
        weight: { min: 0, max: 1, default: 0, step: 0.01 },
        color: { default: [1, 1, 1], type: 'color' },
        intensity: { min: 0, max: 10, default: 1, step: 0.1 }
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
        return new THREE.Color(r, g, b);
    }
}

// Load texture from USD data
function loadTextureFromUSD(textureId) {
    // Validate texture ID
    if (!validateTextureId(textureId, 'loadTextureFromUSD')) {
        return null;
    }

    if (!currentLoader) {
        console.error('loadTextureFromUSD: No USD loader available');
        return null;
    }

    // Check cache first
    if (textureCache.has(textureId)) {
        return textureCache.get(textureId);
    }

    try {
        // Get texture metadata
        const texData = currentLoader.getTexture(textureId);
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
        const imgData = currentLoader.getImage(texData.textureImageId);
        if (!imgData || !imgData.data) {
            console.warn(`Image ${texData.textureImageId} has no pixel data`);
            return null;
        }

        // Validate image dimensions
        if (imgData.width <= 0 || imgData.height <= 0) {
            console.error(`Invalid texture dimensions: ${imgData.width}x${imgData.height}`);
            return null;
        }

        // Validate channel count
        if (imgData.channels < 1 || imgData.channels > 4) {
            console.error(`Invalid channel count: ${imgData.channels}`);
            return null;
        }

        // Convert to Three.js texture
        const texture = createThreeTexture(imgData, texData);

        if (texture) {
            textureCache.set(textureId, texture);
            console.log(`Loaded texture ${textureId}: ${imgData.width}x${imgData.height}, ${imgData.channels} channels`);
        }

        return texture;

    } catch (error) {
        reportError('loadTextureFromUSD', error);
        return null;
    }
}

// Create Three.js texture from USD image data
function createThreeTexture(imgData, texData) {
    try {
        const { width, height, channels, data } = imgData;

        // Create canvas to convert image data
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');

        // Create ImageData
        const imageData = ctx.createImageData(width, height);
        const pixels = imageData.data;

        // Convert based on channel count
        if (channels === 1) {
            // Grayscale
            for (let i = 0; i < width * height; i++) {
                const gray = data[i];
                pixels[i * 4 + 0] = gray;
                pixels[i * 4 + 1] = gray;
                pixels[i * 4 + 2] = gray;
                pixels[i * 4 + 3] = 255;
            }
        } else if (channels === 2) {
            // Grayscale + Alpha
            for (let i = 0; i < width * height; i++) {
                const gray = data[i * 2];
                const alpha = data[i * 2 + 1];
                pixels[i * 4 + 0] = gray;
                pixels[i * 4 + 1] = gray;
                pixels[i * 4 + 2] = gray;
                pixels[i * 4 + 3] = alpha;
            }
        } else if (channels === 3) {
            // RGB
            for (let i = 0; i < width * height; i++) {
                pixels[i * 4 + 0] = data[i * 3 + 0];
                pixels[i * 4 + 1] = data[i * 3 + 1];
                pixels[i * 4 + 2] = data[i * 3 + 2];
                pixels[i * 4 + 3] = 255;
            }
        } else if (channels === 4) {
            // RGBA
            for (let i = 0; i < width * height * 4; i++) {
                pixels[i] = data[i];
            }
        }

        // Put image data on canvas
        ctx.putImageData(imageData, 0, 0);

        // Create Three.js texture from canvas
        const texture = new THREE.CanvasTexture(canvas);

        // Set color space to Linear since we handle conversion in shader
        // This prevents Three.js from doing its own sRGB conversion
        texture.colorSpace = THREE.LinearSRGBColorSpace;

        // Store original color space info for reference
        texture.userData = texture.userData || {};
        texture.userData.sourceColorSpace = imgData.colorSpace ||
            (channels >= 3 ? 'srgb' : 'linear');

        // Apply wrap modes from USD
        if (texData) {
            texture.wrapS = getThreeWrapMode(texData.wrapS);
            texture.wrapT = getThreeWrapMode(texData.wrapT);
        } else {
            texture.wrapS = THREE.RepeatWrapping;
            texture.wrapT = THREE.RepeatWrapping;
        }

        // Enable mipmaps and filtering
        texture.generateMipmaps = true;
        texture.minFilter = THREE.LinearMipmapLinearFilter;
        texture.magFilter = THREE.LinearFilter;
        texture.anisotropy = renderer ? renderer.capabilities.getMaxAnisotropy() : 4;

        texture.needsUpdate = true;

        return texture;

    } catch (error) {
        console.error('Error creating Three.js texture:', error);
        return null;
    }
}

// Convert USD wrap mode to Three.js wrap mode
function getThreeWrapMode(wrapMode) {
    if (!wrapMode) return THREE.RepeatWrapping;

    const mode = wrapMode.toLowerCase();
    if (mode.includes('repeat')) {
        return THREE.RepeatWrapping;
    } else if (mode.includes('clamp')) {
        return THREE.ClampToEdgeWrapping;
    } else if (mode.includes('mirror')) {
        return THREE.MirroredRepeatWrapping;
    }
    return THREE.RepeatWrapping;
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

    // Load TinyUSDZ WASM module
    await loadTinyUSDZ();

    // Create synthetic HDR environment
    createSyntheticHDR('studio');

    // Setup GUI
    setupGUI();

    // Setup file input
    setupFileInput();

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
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
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
    controls = new THREE.OrbitControls(camera, renderer.domElement);
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

// Load TinyUSDZ WASM module
async function loadTinyUSDZ() {
    updateStatus('Loading TinyUSDZ WASM module...');

    return new Promise((resolve, reject) => {
        const script = document.createElement('script');
        script.src = '../dist/tinyusdz.js';
        script.onload = async () => {
            try {
                // Initialize the module
                const Module = {
                    onRuntimeInitialized: function() {
                        tinyUSDZModule = this;
                        console.log('TinyUSDZ module loaded successfully');
                        updateStatus('TinyUSDZ module loaded', 'success');
                        resolve();
                    }
                };

                // The tinyusdz.js script will use the global Module object
                window.Module = Module;

                // Load the WASM file
                const wasmScript = document.createElement('script');
                wasmScript.src = '../dist/tinyusdz.wasm.js';
                document.head.appendChild(wasmScript);
            } catch (error) {
                console.error('Failed to initialize TinyUSDZ:', error);
                updateStatus('Failed to load TinyUSDZ: ' + error.message, 'error');
                reject(error);
            }
        };
        script.onerror = (error) => {
            console.error('Failed to load TinyUSDZ script:', error);
            updateStatus('Failed to load TinyUSDZ script', 'error');
            reject(error);
        };
        document.head.appendChild(script);
    });
}

// Create synthetic HDR environment map
function createSyntheticHDR(type) {
    environmentType = type;

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

    // Optionally set as background
    // scene.background = renderTarget.texture;

    texture.dispose();
}

// Toggle between HDR environments
function toggleEnvironment() {
    if (environmentType === 'studio') {
        createSyntheticHDR('white');
    } else {
        createSyntheticHDR('studio');
    }
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

// Setup GUI for OpenPBR parameters
function setupGUI() {
    if (gui) {
        gui.destroy();
    }

    gui = new dat.GUI({ width: 350 });
    gui.domElement.style.position = 'absolute';
    gui.domElement.style.top = '150px';
    gui.domElement.style.right = '10px';

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
        background: '#1a1a1a'
    };

    envFolder.add(envParams, 'type', ['studio', 'white']).onChange(value => {
        createSyntheticHDR(value);
    });

    envFolder.add(envParams, 'exposure', 0, 3, 0.01).onChange(value => {
        renderer.toneMappingExposure = value;
    });

    envFolder.addColor(envParams, 'background').onChange(value => {
        scene.background = new THREE.Color(value);
    });

    envFolder.open();
}

// Load USD file
async function loadUSDFile(arrayBuffer, filename) {
    if (!tinyUSDZModule) {
        updateStatus('TinyUSDZ module not loaded', 'error');
        return;
    }

    showLoading(true);
    updateStatus(`Loading ${filename}...`);

    try {
        // Clean up previous loader
        if (currentLoader) {
            currentLoader.delete();
            currentLoader = null;
        }

        // Clear the scene
        clearScene();

        // Create new loader
        currentLoader = new tinyUSDZModule.TinyUSDZLoaderNative();

        // Convert ArrayBuffer to Uint8Array
        const uint8Array = new Uint8Array(arrayBuffer);

        // Load the USD file
        const success = currentLoader.loadFromBinary(uint8Array, filename);

        if (!success) {
            throw new Error('Failed to parse USD file');
        }

        // Get scene information
        const numMeshes = currentLoader.numMeshes();
        const numMaterials = currentLoader.numMaterials();

        console.log(`Loaded: ${numMeshes} meshes, ${numMaterials} materials`);

        // Update UI
        document.getElementById('model-info').style.display = 'block';
        document.getElementById('object-count').textContent = numMeshes;
        document.getElementById('material-count').textContent = numMaterials;

        // Load materials
        loadMaterials();

        // Load meshes
        loadMeshes();

        // Update material panel
        updateMaterialPanel();

        // Fit camera to scene
        fitCameraToScene();

        updateStatus(`Loaded: ${numMeshes} objects, ${numMaterials} materials`, 'success');

    } catch (error) {
        console.error('Error loading USD file:', error);
        updateStatus(`Error: ${error.message}`, 'error');
    } finally {
        showLoading(false);
    }
}

// Load materials from USD
function loadMaterials() {
    if (!currentLoader) {
        console.error('loadMaterials: No USD loader available');
        return;
    }

    materials = [];
    const numMaterials = currentLoader.numMaterials();

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
            const result = currentLoader.getMaterialWithFormat(i, 'json');

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
            const threeMaterial = createOpenPBRMaterial(materialData);
            if (!threeMaterial) {
                throw new Error('Failed to create Three.js material');
            }

            materials.push({
                index: i,
                name: materialData.name || `Material_${i}`,
                data: materialData,
                threeMaterial: threeMaterial,
                parameters: extractOpenPBRParams(materialData)
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
                    roughness: 0.5
                }),
                parameters: {}
            });
        }
    }

    console.log(`Successfully loaded ${materials.length} materials`);
}

// Create Three.js material from OpenPBR data
function createOpenPBRMaterial(materialData) {
    const material = new THREE.MeshPhysicalMaterial();

    // Store texture references for later management
    material.userData.textures = {};

    if (materialData.hasOpenPBR && materialData.openPBR) {
        const pbr = materialData.openPBR;

        // Base parameters
        if (pbr.base) {
            // Base color
            if (pbr.base.color) {
                const colorValue = Array.isArray(pbr.base.color) ? pbr.base.color :
                                  (pbr.base.color.value || [1, 1, 1]);
                material.color = createColorWithSpace(...colorValue);

                // Check for base color texture
                const colorTexId = pbr.base.color.textureId;
                if (colorTexId !== undefined && colorTexId >= 0) {
                    const texture = loadTextureFromUSD(colorTexId);
                    if (texture) {
                        material.map = texture;
                        material.userData.textures.map = { textureId: colorTexId, texture };
                    }
                }
            }

            // Metalness
            if (pbr.base.metalness !== undefined) {
                const metalnessValue = typeof pbr.base.metalness === 'number' ? pbr.base.metalness :
                                      (pbr.base.metalness.value || 0);
                material.metalness = metalnessValue;

                // Check for metalness texture
                const metalnessTexId = pbr.base.metalness.textureId;
                if (metalnessTexId !== undefined && metalnessTexId >= 0) {
                    const texture = loadTextureFromUSD(metalnessTexId);
                    if (texture) {
                        material.metalnessMap = texture;
                        material.userData.textures.metalnessMap = { textureId: metalnessTexId, texture };
                    }
                }
            }
        }

        // Specular parameters
        if (pbr.specular) {
            // Roughness
            if (pbr.specular.roughness !== undefined) {
                const roughnessValue = typeof pbr.specular.roughness === 'number' ? pbr.specular.roughness :
                                      (pbr.specular.roughness.value || 0.3);
                material.roughness = roughnessValue;

                // Check for roughness texture
                const roughnessTexId = pbr.specular.roughness.textureId;
                if (roughnessTexId !== undefined && roughnessTexId >= 0) {
                    const texture = loadTextureFromUSD(roughnessTexId);
                    if (texture) {
                        material.roughnessMap = texture;
                        material.userData.textures.roughnessMap = { textureId: roughnessTexId, texture };
                    }
                }
            }

            if (pbr.specular.ior !== undefined) {
                material.ior = typeof pbr.specular.ior === 'number' ? pbr.specular.ior :
                              (pbr.specular.ior.value || 1.5);
            }

            if (pbr.specular.color) {
                const colorValue = Array.isArray(pbr.specular.color) ? pbr.specular.color :
                                  (pbr.specular.color.value || [1, 1, 1]);
                material.specularColor = createColorWithSpace(...colorValue);
            }
        }

        // Transmission
        if (pbr.transmission) {
            if (pbr.transmission.weight !== undefined) {
                material.transmission = pbr.transmission.weight;
            }
            if (pbr.transmission.color) {
                material.attenuationColor = createColorWithSpace(...pbr.transmission.color);
            }
        }

        // Coat (clearcoat)
        if (pbr.coat) {
            if (pbr.coat.weight !== undefined) {
                material.clearcoat = pbr.coat.weight;
            }
            if (pbr.coat.roughness !== undefined) {
                material.clearcoatRoughness = pbr.coat.roughness;
            }
        }

        // Emission
        if (pbr.emission) {
            if (pbr.emission.color) {
                const emissiveValue = Array.isArray(pbr.emission.color) ? pbr.emission.color :
                                     (pbr.emission.color.value || [0, 0, 0]);
                material.emissive = createColorWithSpace(...emissiveValue);

                // Check for emission texture
                const emissiveTexId = pbr.emission.color.textureId;
                if (emissiveTexId !== undefined && emissiveTexId >= 0) {
                    const texture = loadTextureFromUSD(emissiveTexId);
                    if (texture) {
                        material.emissiveMap = texture;
                        material.userData.textures.emissiveMap = { textureId: emissiveTexId, texture };
                    }
                }
            }
            if (pbr.emission.intensity !== undefined) {
                material.emissiveIntensity = typeof pbr.emission.intensity === 'number' ? pbr.emission.intensity :
                                            (pbr.emission.intensity.value || 1);
            }
        }

        // Geometry (check for normal and bump maps)
        if (pbr.geometry) {
            // Normal map
            if (pbr.geometry.normal) {
                const normalTexId = pbr.geometry.normal.textureId;
                if (normalTexId !== undefined && normalTexId >= 0) {
                    const texture = loadTextureFromUSD(normalTexId);
                    if (texture) {
                        material.normalMap = texture;
                        material.normalScale = new THREE.Vector2(1, 1);
                        material.userData.textures.normalMap = { textureId: normalTexId, texture };
                    }
                }
            }
        }

        // Geometry
        if (pbr.geometry) {
            if (pbr.geometry.opacity !== undefined) {
                material.opacity = pbr.geometry.opacity;
                material.transparent = pbr.geometry.opacity < 1;
            }
        }

        // Thin film
        if (pbr.thin_film) {
            if (pbr.thin_film.thickness !== undefined) {
                material.thickness = pbr.thin_film.thickness;
            }
        }

        // Subsurface (approximation with subsurface scattering)
        if (pbr.subsurface && pbr.subsurface.weight > 0) {
            // Three.js doesn't have direct subsurface support, but we can approximate
            material.transmission = Math.max(material.transmission, pbr.subsurface.weight * 0.5);
        }

    } else if (materialData.hasUsdPreviewSurface && materialData.usdPreviewSurface) {
        // Fallback to UsdPreviewSurface
        const preview = materialData.usdPreviewSurface;

        if (preview.diffuseColor) {
            material.color = createColorWithSpace(...preview.diffuseColor);
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
        applyColorSpaceShader(material, material.userData.colorSpaceSettings);
    }

    material.needsUpdate = true;
    return material;
}

// Extract OpenPBR parameters for GUI
function extractOpenPBRParams(materialData) {
    if (!materialData.hasOpenPBR || !materialData.openPBR) {
        return {};
    }

    return materialData.openPBR;
}

// Load meshes from USD
function loadMeshes() {
    if (!currentLoader) return;

    meshes = [];
    const numMeshes = currentLoader.numMeshes();

    for (let i = 0; i < numMeshes; i++) {
        try {
            const meshData = currentLoader.getMesh(i);

            if (!meshData) {
                console.warn(`Failed to load mesh ${i}`);
                continue;
            }

            // Create Three.js geometry
            const geometry = new THREE.BufferGeometry();

            // Add vertices
            if (meshData.vertices && meshData.vertices.length > 0) {
                const vertices = new Float32Array(meshData.vertices);
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

            // Add faces (indices)
            if (meshData.indices && meshData.indices.length > 0) {
                const indices = new Uint32Array(meshData.indices);
                geometry.setIndex(new THREE.BufferAttribute(indices, 1));
            }

            // Get material index
            const materialIndex = meshData.materialIndex || 0;
            const material = materials[materialIndex]?.threeMaterial || new THREE.MeshPhysicalMaterial();

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
            }

            scene.add(mesh);
            meshes.push(mesh);

        } catch (error) {
            console.error(`Error loading mesh ${i}:`, error);
        }
    }
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
        valueSpan.style.color = '#fff';
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

    paramFolder = gui.addFolder(`Material: ${material.name}`);

    if (!material.parameters || Object.keys(material.parameters).length === 0) {
        paramFolder.add({ message: 'No OpenPBR parameters' }, 'message').name('Info');
        paramFolder.open();
        return;
    }

    const params = material.parameters;
    const threeMat = material.threeMaterial;

    // Create controls for each parameter group
    Object.entries(OPENPBR_PARAMS).forEach(([groupName, groupParams]) => {
        if (params[groupName]) {
            const groupFolder = paramFolder.addFolder(groupName);

            Object.entries(groupParams).forEach(([paramName, paramDef]) => {
                const value = params[groupName][paramName];
                if (value === undefined) return;

                if (paramDef.type === 'color') {
                    // Color picker
                    const colorObj = {
                        color: [value[0] * 255, value[1] * 255, value[2] * 255]
                    };
                    groupFolder.addColor(colorObj, 'color').name(paramName).onChange(val => {
                        const r = val[0] / 255;
                        const g = val[1] / 255;
                        const b = val[2] / 255;
                        params[groupName][paramName] = [r, g, b];
                        updateMaterialFromParams(threeMat, params);
                    });
                } else if (paramDef.type === 'boolean') {
                    // Checkbox
                    const boolObj = { [paramName]: value };
                    groupFolder.add(boolObj, paramName).onChange(val => {
                        params[groupName][paramName] = val;
                        updateMaterialFromParams(threeMat, params);
                    });
                } else {
                    // Slider
                    const sliderObj = { [paramName]: value };
                    groupFolder.add(sliderObj, paramName, paramDef.min, paramDef.max, paramDef.step)
                        .onChange(val => {
                            params[groupName][paramName] = val;
                            updateMaterialFromParams(threeMat, params);
                        });
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
            const [r, g, b] = srgbToDisplayP3(...params.base.color);
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
            material.specularColor = createColorWithSpace(...params.specular.color);
        }
    }

    // Transmission
    if (params.transmission) {
        if (params.transmission.weight !== undefined) {
            material.transmission = params.transmission.weight;
        }
        if (params.transmission.color) {
            material.attenuationColor = createColorWithSpace(...params.transmission.color);
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
    if (params.emission) {
        if (params.emission.color) {
            const [r, g, b] = srgbToDisplayP3(...params.emission.color);
            material.emissive.setRGB(r, g, b);
        }
        if (params.emission.intensity !== undefined) {
            material.emissiveIntensity = params.emission.intensity;
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
    // Remove meshes
    meshes.forEach(mesh => {
        mesh.geometry.dispose();
        scene.remove(mesh);
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

    // Clear UI panels
    document.getElementById('material-panel').style.display = 'none';
    document.getElementById('texture-panel').style.display = 'none';
    document.getElementById('model-info').style.display = 'none';
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

        // Try to load a sample USD file from the models directory
        const response = await fetch('../../models/mesh.usda');

        if (!response.ok) {
            throw new Error(`Failed to fetch sample model: ${response.statusText}`);
        }

        const arrayBuffer = await response.arrayBuffer();
        await loadUSDFile(arrayBuffer, 'sample.usda');

    } catch (error) {
        console.error('Error loading sample model:', error);
        updateStatus(`Failed to load sample: ${error.message}`, 'error');

        // Try alternative sample
        try {
            const response = await fetch('../../models/simple-plane.usda');
            if (response.ok) {
                const arrayBuffer = await response.arrayBuffer();
                await loadUSDFile(arrayBuffer, 'simple-plane.usda');
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

    selectedObject = object;
    selectedObject.scale.set(1.1, 1.1, 1.1);

    // Update UI
    document.getElementById('selected-object').textContent = object.name;

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
}

function animate() {
    requestAnimationFrame(animate);
    controls.update();
    renderer.render(scene, camera);
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', init);

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
        side: THREE.DoubleSide
    });

    // Apply base parameters
    if (materialData.base.color) {
        material.color = createColorWithSpace(...materialData.base.color);
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
        material.emissive = createColorWithSpace(...materialData.emission.color);
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
        const loader = new THREE.EXRLoader();
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
        const loader = new THREE.RGBELoader();
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
        texture.rotation = value;
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

// Export functions for HTML onclick handlers
window.toggleEnvironment = toggleEnvironment;
window.loadSampleModel = loadSampleModel;
window.toggleColorSpace = toggleColorSpace;
window.exportSelectedMaterialJSON = exportSelectedMaterialJSON;
window.exportSelectedMaterialMTLX = exportSelectedMaterialMTLX;
window.importMaterialXFile = importMaterialXFile;
window.loadHDRTextureForMaterial = loadHDRTextureForMaterial;