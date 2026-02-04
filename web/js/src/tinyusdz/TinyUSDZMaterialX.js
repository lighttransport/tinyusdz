import * as THREE from 'three';
import { decodeEXR as decodeEXRWithFallback } from './EXRDecoder.js';

/**
 * TinyUSDZ MaterialX / OpenPBR Material Utilities
 *
 * This module provides utilities for converting MaterialX OpenPBR materials
 * from USD files to Three.js MeshPhysicalMaterial.
 *
 * Key features:
 * - OpenPBR parameter extraction from JSON format (flat and grouped)
 * - Texture loading from USD scene with caching (including EXR/HDR)
 * - Full OpenPBR to MeshPhysicalMaterial conversion
 * - Support for all OpenPBR layers: base, specular, transmission, subsurface,
 *   coat, sheen, fuzz, thin_film, emission, geometry
 */

// Reference to TinyUSDZ WASM module for HDR/EXR decoding
let _tinyusdz = null;

/**
 * Set TinyUSDZ WASM module for texture decoding
 * @param {Object} tinyusdz - TinyUSDZ WASM module
 */
export function setTinyUSDZ(tinyusdz) {
    _tinyusdz = tinyusdz;
}

// ============================================================================
// OpenPBR Parameter Mapping to Three.js MeshPhysicalMaterial
// ============================================================================

/**
 * Mapping from OpenPBR parameter names to Three.js material properties
 */
const OPENPBR_TO_THREEJS_MAP = {
    // Base layer
    'base_color': { property: 'color', type: 'color' },
    'base_weight': { property: null, type: 'scalar' }, // Not directly mapped
    'base_metalness': { property: 'metalness', type: 'scalar' },
    'base_roughness': { property: null, type: 'scalar' }, // Use specular_roughness instead
    'base_diffuse_roughness': { property: null, type: 'scalar' }, // Oren-Nayar, not in Three.js

    // Specular layer
    'specular_weight': { property: null, type: 'scalar' },
    'specular_color': { property: 'specularColor', type: 'color' },
    'specular_roughness': { property: 'roughness', type: 'scalar' },
    'specular_ior': { property: 'ior', type: 'scalar' },
    'specular_ior_level': { property: null, type: 'scalar' },
    'specular_anisotropy': { property: 'anisotropy', type: 'scalar' },
    'specular_rotation': { property: 'anisotropyRotation', type: 'scalar' },

    // Transmission
    'transmission_weight': { property: 'transmission', type: 'scalar' },
    'transmission_color': { property: 'attenuationColor', type: 'color' },
    'transmission_depth': { property: 'attenuationDistance', type: 'scalar' },

    // Subsurface
    'subsurface_weight': { property: null, type: 'scalar' }, // Complex SSS not in Three.js
    'subsurface_color': { property: null, type: 'color' },
    'subsurface_radius': { property: null, type: 'color' },
    'subsurface_scale': { property: null, type: 'scalar' },

    // Coat (clearcoat)
    'coat_weight': { property: 'clearcoat', type: 'scalar' },
    'coat_color': { property: null, type: 'color' }, // Not directly supported
    'coat_roughness': { property: 'clearcoatRoughness', type: 'scalar' },
    'coat_ior': { property: null, type: 'scalar' },

    // Sheen
    'sheen_weight': { property: 'sheen', type: 'scalar' },
    'sheen_color': { property: 'sheenColor', type: 'color' },
    'sheen_roughness': { property: 'sheenRoughness', type: 'scalar' },

    // Fuzz (similar to sheen in Three.js)
    'fuzz_weight': { property: 'sheen', type: 'scalar' },
    'fuzz_color': { property: 'sheenColor', type: 'color' },
    'fuzz_roughness': { property: 'sheenRoughness', type: 'scalar' },

    // Thin film (iridescence)
    'thin_film_weight': { property: 'iridescence', type: 'scalar' },
    'thin_film_thickness': { property: 'iridescenceThicknessRange', type: 'scalar' },
    'thin_film_ior': { property: 'iridescenceIOR', type: 'scalar' },

    // Emission
    'emission_color': { property: 'emissive', type: 'color' },
    'emission_luminance': { property: 'emissiveIntensity', type: 'scalar' },

    // Geometry
    'opacity': { property: 'opacity', type: 'scalar' },
    'geometry_opacity': { property: 'opacity', type: 'scalar' },
    'normal': { property: null, type: 'normal' },
    'geometry_normal': { property: null, type: 'normal' },
};

/**
 * Mapping from OpenPBR parameters to Three.js texture map names
 */
const OPENPBR_TEXTURE_MAP = {
    'base_color': 'map',
    'base_metalness': 'metalnessMap',
    'specular_roughness': 'roughnessMap',
    'specular_color': 'specularColorMap',
    'normal': 'normalMap',
    'geometry_normal': 'normalMap',
    'emission_color': 'emissiveMap',
    'opacity': 'alphaMap',
    'geometry_opacity': 'alphaMap',
    'coat_roughness': 'clearcoatRoughnessMap',
    'coat_normal': 'clearcoatNormalMap',
    'sheen_color': 'sheenColorMap',
    'sheen_roughness': 'sheenRoughnessMap',
    'thin_film_thickness': 'iridescenceThicknessMap',
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Extract scalar or color value from OpenPBR parameter
 * Handles both direct values and {type, value} wrapper format
 */
function extractValue(param) {
    if (param === undefined || param === null) return undefined;
    if (typeof param === 'number') return param;
    if (Array.isArray(param)) return param;
    if (typeof param === 'object') {
        if (param.value !== undefined) return param.value;
        if (param.type === 'value') return param.value;
    }
    return param;
}

/**
 * Check if parameter has a texture
 */
function hasTexture(param) {
    if (!param || typeof param !== 'object') return false;
    return param.textureId !== undefined && param.textureId >= 0;
}

/**
 * Get texture ID from parameter
 */
function getTextureId(param) {
    if (!hasTexture(param)) return -1;
    return param.textureId;
}

/**
 * Create THREE.Color from RGB array with optional color space handling
 */
function createColor(rgb, colorSpace = 'srgb') {
    if (!rgb || !Array.isArray(rgb)) return new THREE.Color(1, 1, 1);
    const color = new THREE.Color(rgb[0], rgb[1], rgb[2]);
    return color;
}

/**
 * Detect MIME type from image data
 */
function getMimeType(imgData) {
    if (imgData.uri) {
        const ext = imgData.uri.split('.').pop().toLowerCase().split('?')[0];
        const mimeTypes = {
            'png': 'image/png',
            'jpg': 'image/jpeg',
            'jpeg': 'image/jpeg',
            'webp': 'image/webp',
            'gif': 'image/gif',
            'exr': 'image/x-exr',
            'hdr': 'image/vnd.radiance'
        };
        if (mimeTypes[ext]) return mimeTypes[ext];
    }

    // Check magic bytes
    if (imgData.data && imgData.data.length >= 4) {
        const data = new Uint8Array(imgData.data);
        // PNG magic: 0x89 0x50 0x4E 0x47
        if (data[0] === 0x89 && data[1] === 0x50 && data[2] === 0x4E && data[3] === 0x47) return 'image/png';
        // JPEG magic: 0xFF 0xD8 0xFF
        if (data[0] === 0xFF && data[1] === 0xD8 && data[2] === 0xFF) return 'image/jpeg';
        // WEBP magic: RIFF....WEBP
        if (data[0] === 0x52 && data[1] === 0x49 && data[2] === 0x46 && data[3] === 0x46) return 'image/webp';
        // EXR magic: 0x76 0x2F 0x31 0x01
        if (data[0] === 0x76 && data[1] === 0x2F && data[2] === 0x31 && data[3] === 0x01) return 'image/x-exr';
        // HDR magic: "#?" (Radiance format)
        if (data[0] === 0x23 && data[1] === 0x3F) return 'image/vnd.radiance';
    }

    return 'image/png';
}

/**
 * Check if MIME type is HDR format (EXR or HDR)
 */
function isHDRFormat(mimeType) {
    return mimeType === 'image/x-exr' || mimeType === 'image/vnd.radiance';
}

/**
 * Decode HDR/EXR texture data and create Three.js DataTexture
 * @param {Uint8Array|ArrayBuffer} data - Image data
 * @param {string} mimeType - MIME type
 * @returns {THREE.DataTexture|null}
 */
function decodeHDRTexture(data, mimeType) {
    const buffer = data instanceof ArrayBuffer ? data : data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);

    if (mimeType === 'image/x-exr') {
        // Use EXR decoder with TinyUSDZ fallback
        const result = decodeEXRWithFallback(buffer, _tinyusdz, {
            outputFormat: 'float16',
            preferThreeJS: true,
        });

        if (result.success) {
            const texture = new THREE.DataTexture(
                result.data,
                result.width,
                result.height,
                THREE.RGBAFormat,
                result.format === 'float16' ? THREE.HalfFloatType : THREE.FloatType
            );
            texture.minFilter = THREE.LinearFilter;
            texture.magFilter = THREE.LinearFilter;
            texture.generateMipmaps = false;
            texture.flipY = true;
            texture.needsUpdate = true;
            return texture;
        }
    } else if (mimeType === 'image/vnd.radiance') {
        // Use TinyUSDZ HDR decoder (faster)
        if (_tinyusdz && typeof _tinyusdz.decodeHDR === 'function') {
            const uint8Array = data instanceof Uint8Array ? data : new Uint8Array(buffer);
            const result = _tinyusdz.decodeHDR(uint8Array, 'float16');

            if (result.success) {
                const texture = new THREE.DataTexture(
                    result.data,
                    result.width,
                    result.height,
                    THREE.RGBAFormat,
                    THREE.HalfFloatType
                );
                texture.minFilter = THREE.LinearFilter;
                texture.magFilter = THREE.LinearFilter;
                texture.generateMipmaps = false;
                texture.flipY = true;
                texture.needsUpdate = true;
                return texture;
            }
        }
    }

    return null;
}

// ============================================================================
// Texture Loading
// ============================================================================

/**
 * Load texture from USD scene
 * @param {Object} usdScene - USD scene object with getTexture/getImage methods
 * @param {number} textureId - Texture ID
 * @param {Map} cache - Optional texture cache
 * @returns {Promise<THREE.Texture|null>}
 */
async function loadTextureFromUSD(usdScene, textureId, cache = null) {
    if (textureId === undefined || textureId < 0) return null;

    // Check cache
    if (cache && cache.has(textureId)) {
        return cache.get(textureId);
    }

    try {
        const texData = usdScene.getTexture(textureId);
        if (!texData || texData.textureImageId === undefined || texData.textureImageId < 0) {
            console.warn(`Texture ${textureId} has no valid image data`);
            return null;
        }

        const imgData = usdScene.getImage(texData.textureImageId);
        if (!imgData) {
            console.warn(`Image ${texData.textureImageId} not found`);
            return null;
        }

        let texture = null;

        // Case 1: URI only - try to find embedded alternative first
        if (imgData.uri && (imgData.bufferId === -1 || imgData.bufferId === undefined)) {
            // TinyUSDZ may create duplicate image entries: one with URI reference (bufferId=-1)
            // and one with embedded data (bufferId>=0). Try to find the embedded version.
            const filename = imgData.uri.replace(/^\.\//, ''); // Remove leading ./
            let foundEmbedded = false;

            if (typeof usdScene.numImages === 'function') {
                const numImages = usdScene.numImages();
                for (let i = 0; i < numImages; i++) {
                    const altImg = usdScene.getImage(i);
                    if (altImg.bufferId >= 0 && altImg.uri === filename) {
                        // Found embedded version - use it instead
                        const altImgData = altImg;
                        if (altImgData.data) {
                            if (altImgData.decoded) {
                                const image8Array = new Uint8ClampedArray(altImgData.data);
                                texture = new THREE.DataTexture(image8Array, altImgData.width, altImgData.height);
                                if (altImgData.channels === 1) texture.format = THREE.RedFormat;
                                else if (altImgData.channels === 2) texture.format = THREE.RGFormat;
                                else if (altImgData.channels === 4) texture.format = THREE.RGBAFormat;
                                texture.flipY = true;
                                texture.needsUpdate = true;
                            } else {
                                const mimeType = getMimeType(altImgData);
                                // Check if HDR format - use specialized decoder
                                if (isHDRFormat(mimeType)) {
                                    texture = decodeHDRTexture(altImgData.data, mimeType);
                                } else {
                                    const blob = new Blob([altImgData.data], { type: mimeType });
                                    const blobUrl = URL.createObjectURL(blob);
                                    const loader = new THREE.TextureLoader();
                                    texture = await loader.loadAsync(blobUrl);
                                    URL.revokeObjectURL(blobUrl);
                                }
                            }
                            foundEmbedded = true;
                            break;
                        }
                    }
                }
            }

            // Fall back to loading from URI if no embedded version found
            if (!foundEmbedded) {
                const loader = new THREE.TextureLoader();
                texture = await loader.loadAsync(imgData.uri);
            }
        }
        // Case 2 & 3: Embedded texture
        else if (imgData.bufferId >= 0 && imgData.data) {
            if (imgData.decoded) {
                // Already decoded - create DataTexture
                const image8Array = new Uint8ClampedArray(imgData.data);
                texture = new THREE.DataTexture(image8Array, imgData.width, imgData.height);

                if (imgData.channels === 1) texture.format = THREE.RedFormat;
                else if (imgData.channels === 2) texture.format = THREE.RGFormat;
                else if (imgData.channels === 4) texture.format = THREE.RGBAFormat;
                else {
                    console.error(`Unsupported channel count: ${imgData.channels}`);
                    return null;
                }

                texture.flipY = true;
                texture.needsUpdate = true;
            } else {
                // Needs decoding - check format and decode
                const mimeType = getMimeType(imgData);

                // Check if HDR format - use specialized decoder
                if (isHDRFormat(mimeType)) {
                    texture = decodeHDRTexture(imgData.data, mimeType);
                } else {
                    // Standard image - use Blob and TextureLoader
                    const blob = new Blob([imgData.data], { type: mimeType });
                    const blobUrl = URL.createObjectURL(blob);
                    const loader = new THREE.TextureLoader();
                    texture = await loader.loadAsync(blobUrl);
                    URL.revokeObjectURL(blobUrl);
                }
            }
        }

        if (texture && cache) {
            cache.set(textureId, texture);
        }

        return texture;

    } catch (error) {
        console.error(`Failed to load texture ${textureId}:`, error);
        return null;
    }
}

// ============================================================================
// OpenPBR to MeshPhysicalMaterial Conversion
// ============================================================================

/**
 * Convert OpenPBR material data to Three.js MeshPhysicalMaterial
 * Waits for all textures to load before returning the material.
 *
 * @param {Object} materialData - OpenPBR material data from USD
 * @param {Object} usdScene - USD scene for texture loading
 * @param {Object} options - Conversion options
 * @returns {Promise<THREE.MeshPhysicalMaterial>}
 */
async function convertOpenPBRToMeshPhysicalMaterialLoaded(materialData, usdScene = null, options = {}) {
    const material = new THREE.MeshPhysicalMaterial();

    // Store texture references for later management
    material.userData.textures = {};
    material.userData.materialType = 'OpenPBR';
    material.userData.openPBRData = materialData;

    // Get OpenPBR data - support multiple formats
    let pbr = null;

    // Check for grouped format first
    if (materialData.openPBR) {
        pbr = materialData.openPBR;
        material.userData.format = 'grouped';
    }
    // Check for flat format
    else if (materialData.base_color !== undefined ||
             materialData.base_metalness !== undefined ||
             materialData.specular_roughness !== undefined) {
        pbr = { flat: materialData };
        material.userData.format = 'flat';
    }
    // Check for openPBRShader format
    else if (materialData.openPBRShader) {
        pbr = { flat: materialData.openPBRShader };
        material.userData.format = 'flat';
    }

    if (!pbr) {
        console.warn('No OpenPBR data found in material');
        return material;
    }

    // Texture cache and delayed loading manager
    const textureCache = options.textureCache || new Map();
    const textureManager = options.textureLoadingManager || null;

    // Helper to apply parameter with optional texture
    const applyParam = async (paramName, paramValue, group = null) => {
        const mapping = OPENPBR_TO_THREEJS_MAP[paramName];
        if (!mapping || !mapping.property) return;

        const value = extractValue(paramValue);

        // Apply scalar or color value
        if (mapping.type === 'color' && Array.isArray(value)) {
            material[mapping.property] = createColor(value);
        } else if (mapping.type === 'scalar' && typeof value === 'number') {
            material[mapping.property] = value;
        }

        // Load and apply texture if present
        if (usdScene && hasTexture(paramValue)) {
            const texMapName = OPENPBR_TEXTURE_MAP[paramName];
            if (texMapName) {
                const textureId = getTextureId(paramValue);

                // If textureLoadingManager is provided, queue texture for later loading
                if (textureManager) {
                    textureManager.queueTexture(material, texMapName, textureId, usdScene);
                } else {
                    // Load immediately (original behavior)
                    const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                    if (texture) {
                        material[texMapName] = texture;
                        material.userData.textures[texMapName] = {
                            textureId: textureId,
                            texture: texture
                        };
                        material.needsUpdate = true;
                    }
                }
            }
        }
    };

    // Process flat format
    if (pbr.flat) {
        const flat = pbr.flat;

        // Base layer
        await applyParam('base_color', flat.base_color);
        await applyParam('base_metalness', flat.base_metalness);

        // Specular layer
        await applyParam('specular_roughness', flat.specular_roughness);
        await applyParam('specular_ior', flat.specular_ior);
        await applyParam('specular_color', flat.specular_color);
        await applyParam('specular_anisotropy', flat.specular_anisotropy);

        // Transmission
        await applyParam('transmission_weight', flat.transmission_weight);
        await applyParam('transmission_color', flat.transmission_color);

        // Coat
        await applyParam('coat_weight', flat.coat_weight);
        await applyParam('coat_roughness', flat.coat_roughness);

        // Sheen/Fuzz
        if (flat.sheen_weight !== undefined) {
            await applyParam('sheen_weight', flat.sheen_weight);
            await applyParam('sheen_color', flat.sheen_color);
            await applyParam('sheen_roughness', flat.sheen_roughness);
        } else if (flat.fuzz_weight !== undefined) {
            await applyParam('fuzz_weight', flat.fuzz_weight);
            await applyParam('fuzz_color', flat.fuzz_color);
            await applyParam('fuzz_roughness', flat.fuzz_roughness);
        }

        // Thin film (iridescence)
        if (flat.thin_film_weight !== undefined) {
            const weight = extractValue(flat.thin_film_weight);
            if (weight > 0) {
                material.iridescence = weight;
                const thickness = extractValue(flat.thin_film_thickness) || 500;
                material.iridescenceThicknessRange = [100, thickness];
                material.iridescenceIOR = extractValue(flat.thin_film_ior) || 1.5;
            }
        }

        // Emission
        if (flat.emission_color !== undefined) {
            const emissionColor = extractValue(flat.emission_color);
            if (emissionColor && Array.isArray(emissionColor)) {
                material.emissive = createColor(emissionColor);
            }
            // Load emission texture
            if (usdScene && hasTexture(flat.emission_color)) {
                const textureId = getTextureId(flat.emission_color);
                if (textureManager) {
                    textureManager.queueTexture(material, 'emissiveMap', textureId, usdScene);
                } else {
                    const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                    if (texture) {
                        material.emissiveMap = texture;
                        material.userData.textures.emissiveMap = { textureId, texture };
                    }
                }
            }
        }
        if (flat.emission_luminance !== undefined) {
            material.emissiveIntensity = extractValue(flat.emission_luminance) || 0;
        }

        // Geometry - opacity/alpha
        const opacityParam = flat.opacity !== undefined ? flat.opacity : flat.geometry_opacity;
        if (opacityParam !== undefined) {
            const opacityValue = extractValue(opacityParam);
            if (typeof opacityValue === 'number') {
                material.opacity = opacityValue;
                material.transparent = opacityValue < 1.0;
            }
            if (usdScene && hasTexture(opacityParam)) {
                const textureId = getTextureId(opacityParam);
                // For alpha maps, we need to set transparent=true even in delayed mode
                material.transparent = true;
                if (textureManager) {
                    textureManager.queueTexture(material, 'alphaMap', textureId, usdScene);
                } else {
                    const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                    if (texture) {
                        material.alphaMap = texture;
                        material.userData.textures.alphaMap = { textureId, texture };
                    }
                }
            }
        }

        // Normal map
        const normalParam = flat.normal !== undefined ? flat.normal : flat.geometry_normal;
        if (normalParam !== undefined && usdScene && hasTexture(normalParam)) {
            const textureId = getTextureId(normalParam);
            // Initialize normalScale even in delayed mode
            material.normalScale = new THREE.Vector2(1, 1);
            if (textureManager) {
                textureManager.queueTexture(material, 'normalMap', textureId, usdScene);
            } else {
                const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                if (texture) {
                    material.normalMap = texture;
                    material.userData.textures.normalMap = { textureId, texture };
                }
            }
        }
    }

    // Process grouped format
    else {
        // Base layer
        if (pbr.base) {
            await applyParam('base_color', pbr.base.base_color);
            await applyParam('base_metalness', pbr.base.base_metalness);
        }

        // Specular layer
        if (pbr.specular) {
            await applyParam('specular_roughness', pbr.specular.specular_roughness);
            await applyParam('specular_ior', pbr.specular.specular_ior);
            await applyParam('specular_color', pbr.specular.specular_color);
            await applyParam('specular_anisotropy', pbr.specular.specular_anisotropy);
        }

        // Transmission
        if (pbr.transmission) {
            await applyParam('transmission_weight', pbr.transmission.transmission_weight);
            await applyParam('transmission_color', pbr.transmission.transmission_color);
        }

        // Coat
        if (pbr.coat) {
            await applyParam('coat_weight', pbr.coat.coat_weight);
            await applyParam('coat_roughness', pbr.coat.coat_roughness);
        }

        // Sheen
        if (pbr.sheen) {
            await applyParam('sheen_weight', pbr.sheen.sheen_weight);
            await applyParam('sheen_color', pbr.sheen.sheen_color);
            await applyParam('sheen_roughness', pbr.sheen.sheen_roughness);
        }

        // Fuzz
        if (pbr.fuzz) {
            await applyParam('fuzz_weight', pbr.fuzz.fuzz_weight);
            await applyParam('fuzz_color', pbr.fuzz.fuzz_color);
            await applyParam('fuzz_roughness', pbr.fuzz.fuzz_roughness);
        }

        // Thin film
        if (pbr.thin_film) {
            const weight = extractValue(pbr.thin_film.thin_film_weight);
            if (weight > 0) {
                material.iridescence = weight;
                const thickness = extractValue(pbr.thin_film.thin_film_thickness) || 500;
                material.iridescenceThicknessRange = [100, thickness];
                material.iridescenceIOR = extractValue(pbr.thin_film.thin_film_ior) || 1.5;
            }
        }

        // Emission
        if (pbr.emission) {
            const emissionColor = extractValue(pbr.emission.emission_color);
            if (emissionColor && Array.isArray(emissionColor)) {
                material.emissive = createColor(emissionColor);
            }
            if (usdScene && hasTexture(pbr.emission.emission_color)) {
                const textureId = getTextureId(pbr.emission.emission_color);
                if (textureManager) {
                    textureManager.queueTexture(material, 'emissiveMap', textureId, usdScene);
                } else {
                    const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                    if (texture) {
                        material.emissiveMap = texture;
                        material.userData.textures.emissiveMap = { textureId, texture };
                    }
                }
            }
            if (pbr.emission.emission_luminance !== undefined) {
                material.emissiveIntensity = extractValue(pbr.emission.emission_luminance) || 0;
            }
        }

        // Geometry
        if (pbr.geometry) {
            const opacityParam = pbr.geometry.opacity !== undefined ? pbr.geometry.opacity : pbr.geometry.geometry_opacity;
            if (opacityParam !== undefined) {
                const opacityValue = extractValue(opacityParam);
                if (typeof opacityValue === 'number') {
                    material.opacity = opacityValue;
                    material.transparent = opacityValue < 1.0;
                }
                if (usdScene && hasTexture(opacityParam)) {
                    const textureId = getTextureId(opacityParam);
                    material.transparent = true;
                    if (textureManager) {
                        textureManager.queueTexture(material, 'alphaMap', textureId, usdScene);
                    } else {
                        const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                        if (texture) {
                            material.alphaMap = texture;
                            material.userData.textures.alphaMap = { textureId, texture };
                        }
                    }
                }
            }

            const normalParam = pbr.geometry.normal !== undefined ? pbr.geometry.normal : pbr.geometry.geometry_normal;
            if (normalParam !== undefined && usdScene && hasTexture(normalParam)) {
                const textureId = getTextureId(normalParam);
                material.normalScale = new THREE.Vector2(1, 1);
                if (textureManager) {
                    textureManager.queueTexture(material, 'normalMap', textureId, usdScene);
                } else {
                    const texture = await loadTextureFromUSD(usdScene, textureId, textureCache);
                    if (texture) {
                        material.normalMap = texture;
                        material.userData.textures.normalMap = { textureId, texture };
                    }
                }
            }
        }
    }

    // Apply environment map if provided
    if (options.envMap) {
        material.envMap = options.envMap;
        material.envMapIntensity = options.envMapIntensity || 1.0;
    }

    // Set material name
    if (materialData.name) {
        material.name = materialData.name;
    }

    return material;
}

/**
 * Convert OpenPBR material data to Three.js MeshPhysicalMaterial (legacy pattern)
 * Returns material immediately, textures load asynchronously in the background.
 * This matches the behavior of convertUsdMaterialToMeshPhysicalMaterial.
 *
 * @param {Object} materialData - OpenPBR material data from USD
 * @param {Object} usdScene - USD scene for texture loading
 * @param {Object} options - Conversion options
 * @returns {THREE.MeshPhysicalMaterial} Material (textures load asynchronously)
 */
function convertOpenPBRToMeshPhysicalMaterial(materialData, usdScene = null, options = {}) {
    const material = new THREE.MeshPhysicalMaterial();

    // Store texture references for later management
    material.userData.textures = {};
    material.userData.materialType = 'OpenPBR';
    material.userData.openPBRData = materialData;

    // Get OpenPBR data - support multiple formats
    let pbr = null;

    // Check for grouped format first
    if (materialData.openPBR) {
        pbr = materialData.openPBR;
        material.userData.format = 'grouped';
    }
    // Check for flat format
    else if (materialData.base_color !== undefined ||
             materialData.base_metalness !== undefined ||
             materialData.specular_roughness !== undefined) {
        pbr = { flat: materialData };
        material.userData.format = 'flat';
    }
    // Check for openPBRShader format
    else if (materialData.openPBRShader) {
        pbr = { flat: materialData.openPBRShader };
        material.userData.format = 'flat';
    }

    if (!pbr) {
        console.warn('No OpenPBR data found in material');
        return material;
    }

    // Texture cache
    const textureCache = options.textureCache || new Map();

    // Helper to apply parameter value (sync) and queue texture loading (async)
    const applyParam = (paramName, paramValue) => {
        const mapping = OPENPBR_TO_THREEJS_MAP[paramName];
        if (!mapping || !mapping.property) return;

        const value = extractValue(paramValue);

        // Apply scalar or color value immediately
        if (mapping.type === 'color' && Array.isArray(value)) {
            material[mapping.property] = createColor(value);
        } else if (mapping.type === 'scalar' && typeof value === 'number') {
            material[mapping.property] = value;
        }

        // Queue texture loading (fire-and-forget)
        if (usdScene && hasTexture(paramValue)) {
            const texMapName = OPENPBR_TEXTURE_MAP[paramName];
            if (texMapName) {
                loadTextureFromUSD(usdScene, getTextureId(paramValue), textureCache).then((texture) => {
                    if (texture) {
                        material[texMapName] = texture;
                        material.userData.textures[texMapName] = {
                            textureId: getTextureId(paramValue),
                            texture: texture
                        };
                        material.needsUpdate = true;
                    }
                }).catch((err) => {
                    console.error(`Failed to load texture for ${paramName}:`, err);
                });
            }
        }
    };

    // Process flat format
    if (pbr.flat) {
        const flat = pbr.flat;

        // Base layer
        applyParam('base_color', flat.base_color);
        applyParam('base_metalness', flat.base_metalness);

        // Specular layer
        applyParam('specular_roughness', flat.specular_roughness);
        applyParam('specular_ior', flat.specular_ior);
        applyParam('specular_color', flat.specular_color);
        applyParam('specular_anisotropy', flat.specular_anisotropy);

        // Transmission
        applyParam('transmission_weight', flat.transmission_weight);
        applyParam('transmission_color', flat.transmission_color);

        // Coat
        applyParam('coat_weight', flat.coat_weight);
        applyParam('coat_roughness', flat.coat_roughness);

        // Sheen/Fuzz
        if (flat.sheen_weight !== undefined) {
            applyParam('sheen_weight', flat.sheen_weight);
            applyParam('sheen_color', flat.sheen_color);
            applyParam('sheen_roughness', flat.sheen_roughness);
        } else if (flat.fuzz_weight !== undefined) {
            applyParam('fuzz_weight', flat.fuzz_weight);
            applyParam('fuzz_color', flat.fuzz_color);
            applyParam('fuzz_roughness', flat.fuzz_roughness);
        }

        // Thin film (iridescence)
        if (flat.thin_film_weight !== undefined) {
            const weight = extractValue(flat.thin_film_weight);
            if (weight > 0) {
                material.iridescence = weight;
                const thickness = extractValue(flat.thin_film_thickness) || 500;
                material.iridescenceThicknessRange = [100, thickness];
                material.iridescenceIOR = extractValue(flat.thin_film_ior) || 1.5;
            }
        }

        // Emission
        if (flat.emission_color !== undefined) {
            const emissionColor = extractValue(flat.emission_color);
            if (emissionColor && Array.isArray(emissionColor)) {
                material.emissive = createColor(emissionColor);
            }
            // Load emission texture (fire-and-forget)
            if (usdScene && hasTexture(flat.emission_color)) {
                loadTextureFromUSD(usdScene, getTextureId(flat.emission_color), textureCache).then((texture) => {
                    if (texture) {
                        material.emissiveMap = texture;
                        material.userData.textures.emissiveMap = { textureId: getTextureId(flat.emission_color), texture };
                        material.needsUpdate = true;
                    }
                }).catch((err) => {
                    console.error('Failed to load emission texture:', err);
                });
            }
        }
        if (flat.emission_luminance !== undefined) {
            material.emissiveIntensity = extractValue(flat.emission_luminance) || 0;
        }

        // Geometry - opacity/alpha
        const opacityParam = flat.opacity !== undefined ? flat.opacity : flat.geometry_opacity;
        if (opacityParam !== undefined) {
            const opacityValue = extractValue(opacityParam);
            if (typeof opacityValue === 'number') {
                material.opacity = opacityValue;
                material.transparent = opacityValue < 1.0;
            }
            if (usdScene && hasTexture(opacityParam)) {
                loadTextureFromUSD(usdScene, getTextureId(opacityParam), textureCache).then((texture) => {
                    if (texture) {
                        material.alphaMap = texture;
                        material.transparent = true;
                        material.userData.textures.alphaMap = { textureId: getTextureId(opacityParam), texture };
                        material.needsUpdate = true;
                    }
                }).catch((err) => {
                    console.error('Failed to load opacity texture:', err);
                });
            }
        }

        // Normal map
        const normalParam = flat.normal !== undefined ? flat.normal : flat.geometry_normal;
        if (normalParam !== undefined && usdScene && hasTexture(normalParam)) {
            loadTextureFromUSD(usdScene, getTextureId(normalParam), textureCache).then((texture) => {
                if (texture) {
                    material.normalMap = texture;
                    material.normalScale = new THREE.Vector2(1, 1);
                    material.userData.textures.normalMap = { textureId: getTextureId(normalParam), texture };
                    material.needsUpdate = true;
                }
            }).catch((err) => {
                console.error('Failed to load normal texture:', err);
            });
        }
    }

    // Process grouped format
    else {
        // Base layer
        if (pbr.base) {
            applyParam('base_color', pbr.base.base_color);
            applyParam('base_metalness', pbr.base.base_metalness);
        }

        // Specular layer
        if (pbr.specular) {
            applyParam('specular_roughness', pbr.specular.specular_roughness);
            applyParam('specular_ior', pbr.specular.specular_ior);
            applyParam('specular_color', pbr.specular.specular_color);
            applyParam('specular_anisotropy', pbr.specular.specular_anisotropy);
        }

        // Transmission
        if (pbr.transmission) {
            applyParam('transmission_weight', pbr.transmission.transmission_weight);
            applyParam('transmission_color', pbr.transmission.transmission_color);
        }

        // Coat
        if (pbr.coat) {
            applyParam('coat_weight', pbr.coat.coat_weight);
            applyParam('coat_roughness', pbr.coat.coat_roughness);
        }

        // Sheen
        if (pbr.sheen) {
            applyParam('sheen_weight', pbr.sheen.sheen_weight);
            applyParam('sheen_color', pbr.sheen.sheen_color);
            applyParam('sheen_roughness', pbr.sheen.sheen_roughness);
        }

        // Fuzz
        if (pbr.fuzz) {
            applyParam('fuzz_weight', pbr.fuzz.fuzz_weight);
            applyParam('fuzz_color', pbr.fuzz.fuzz_color);
            applyParam('fuzz_roughness', pbr.fuzz.fuzz_roughness);
        }

        // Thin film
        if (pbr.thin_film) {
            const weight = extractValue(pbr.thin_film.thin_film_weight);
            if (weight > 0) {
                material.iridescence = weight;
                const thickness = extractValue(pbr.thin_film.thin_film_thickness) || 500;
                material.iridescenceThicknessRange = [100, thickness];
                material.iridescenceIOR = extractValue(pbr.thin_film.thin_film_ior) || 1.5;
            }
        }

        // Emission
        if (pbr.emission) {
            const emissionColor = extractValue(pbr.emission.emission_color);
            if (emissionColor && Array.isArray(emissionColor)) {
                material.emissive = createColor(emissionColor);
            }
            if (usdScene && hasTexture(pbr.emission.emission_color)) {
                loadTextureFromUSD(usdScene, getTextureId(pbr.emission.emission_color), textureCache).then((texture) => {
                    if (texture) {
                        material.emissiveMap = texture;
                        material.userData.textures.emissiveMap = { textureId: getTextureId(pbr.emission.emission_color), texture };
                        material.needsUpdate = true;
                    }
                }).catch((err) => {
                    console.error('Failed to load emission texture:', err);
                });
            }
            if (pbr.emission.emission_luminance !== undefined) {
                material.emissiveIntensity = extractValue(pbr.emission.emission_luminance) || 0;
            }
        }

        // Geometry
        if (pbr.geometry) {
            const opacityParam = pbr.geometry.opacity !== undefined ? pbr.geometry.opacity : pbr.geometry.geometry_opacity;
            if (opacityParam !== undefined) {
                const opacityValue = extractValue(opacityParam);
                if (typeof opacityValue === 'number') {
                    material.opacity = opacityValue;
                    material.transparent = opacityValue < 1.0;
                }
                if (usdScene && hasTexture(opacityParam)) {
                    loadTextureFromUSD(usdScene, getTextureId(opacityParam), textureCache).then((texture) => {
                        if (texture) {
                            material.alphaMap = texture;
                            material.transparent = true;
                            material.userData.textures.alphaMap = { textureId: getTextureId(opacityParam), texture };
                            material.needsUpdate = true;
                        }
                    }).catch((err) => {
                        console.error('Failed to load opacity texture:', err);
                    });
                }
            }

            const normalParam = pbr.geometry.normal !== undefined ? pbr.geometry.normal : pbr.geometry.geometry_normal;
            if (normalParam !== undefined && usdScene && hasTexture(normalParam)) {
                loadTextureFromUSD(usdScene, getTextureId(normalParam), textureCache).then((texture) => {
                    if (texture) {
                        material.normalMap = texture;
                        material.normalScale = new THREE.Vector2(1, 1);
                        material.userData.textures.normalMap = { textureId: getTextureId(normalParam), texture };
                        material.needsUpdate = true;
                    }
                }).catch((err) => {
                    console.error('Failed to load normal texture:', err);
                });
            }
        }
    }

    // Apply environment map if provided
    if (options.envMap) {
        material.envMap = options.envMap;
        material.envMapIntensity = options.envMapIntensity || 1.0;
    }

    // Set material name
    if (materialData.name) {
        material.name = materialData.name;
    }

    return material;
}

// ============================================================================
// MaterialX NodeGraph Optimizer
// ============================================================================

/**
 * Optimization levels for NodeGraph processing
 */
const NodeGraphOptimizationLevel = {
    NONE: 0,           // No optimization
    BASIC: 1,          // Remove identity ops only
    STANDARD: 2,       // Pattern detection + identity removal
    AGGRESSIVE: 3      // All optimizations + constant folding
};

// Helper functions for value comparison
function _approxEqual(a, b, epsilon = 0.0001) {
    if (typeof a === 'number' && typeof b === 'number') {
        return Math.abs(a - b) < epsilon;
    }
    if (Array.isArray(a) && Array.isArray(b) && a.length === b.length) {
        return a.every((v, i) => _approxEqual(v, b[i], epsilon));
    }
    return a === b;
}

function _isWhite(value) {
    return Array.isArray(value) && value.length === 3 && _approxEqual(value, [1, 1, 1]);
}

function _isBlack(value) {
    return Array.isArray(value) && value.length === 3 && _approxEqual(value, [0, 0, 0]);
}

function _isOne(value) {
    return _approxEqual(value, 1);
}

function _isZero(value) {
    return _approxEqual(value, 0);
}

function _buildNodeMap(nodes) {
    const map = new Map();
    for (const node of nodes) {
        map.set(node.name, node);
    }
    return map;
}

function _getInputInfo(node, inputName) {
    if (!node.inputs) return null;
    const input = node.inputs.find(i => i.name === inputName);
    if (!input) return null;
    if (input.nodename) {
        return { type: 'connection', nodename: input.nodename, output: input.output || 'out' };
    }
    return { type: 'value', value: input.value };
}

/**
 * Detect Invert pattern: constant -> subtract([1,1,1], x) -> mix
 */
function _detectInvertPattern(nodes, nodeMap) {
    const patterns = [];
    for (const mixNode of nodes) {
        if (mixNode.category !== 'mix_color3') continue;
        const bgInput = _getInputInfo(mixNode, 'bg');
        const fgInput = _getInputInfo(mixNode, 'fg');
        const mixInput = _getInputInfo(mixNode, 'mix');
        if (!bgInput || !fgInput || bgInput.type !== 'connection' || fgInput.type !== 'connection') continue;

        const subtractNode = nodeMap.get(fgInput.nodename);
        if (!subtractNode || subtractNode.category !== 'subtract_color3') continue;

        const in1 = _getInputInfo(subtractNode, 'in1');
        const in2 = _getInputInfo(subtractNode, 'in2');
        if (!in1 || in1.type !== 'value' || !_isWhite(in1.value)) continue;
        if (!in2 || in2.type !== 'connection' || in2.nodename !== bgInput.nodename) continue;

        const factor = mixInput && mixInput.type === 'value' ? mixInput.value : 1;
        patterns.push({
            type: 'invert',
            outputNode: mixNode.name,
            inputNode: bgInput.nodename,
            subtractNode: subtractNode.name,
            factor: factor,
            nodesToRemove: [subtractNode.name],
            replacement: {
                name: mixNode.name,
                category: 'invert_color3',
                type: 'ND_invert_color3',
                inputs: [
                    { name: 'in', nodename: bgInput.nodename, output: 'out' },
                    { name: 'amount', type: 'float', value: factor }
                ]
            }
        });
    }
    return patterns;
}

/**
 * Detect Brightness/Contrast pattern: multiply -> add -> subtract -> max
 */
function _detectBrightnessContrastPattern(nodes, nodeMap) {
    const patterns = [];
    for (const maxNode of nodes) {
        if (maxNode.category !== 'max_color3') continue;
        const maxIn2 = _getInputInfo(maxNode, 'in2');
        if (!maxIn2 || maxIn2.type !== 'value' || !_isBlack(maxIn2.value)) continue;

        const maxIn1 = _getInputInfo(maxNode, 'in1');
        if (!maxIn1 || maxIn1.type !== 'connection') continue;

        const subtractNode = nodeMap.get(maxIn1.nodename);
        if (!subtractNode || subtractNode.category !== 'subtract_color3') continue;

        const subIn1 = _getInputInfo(subtractNode, 'in1');
        const subIn2 = _getInputInfo(subtractNode, 'in2');
        if (!subIn1 || subIn1.type !== 'connection') continue;

        const addNode = nodeMap.get(subIn1.nodename);
        if (!addNode || addNode.category !== 'add_color3') continue;

        const addIn1 = _getInputInfo(addNode, 'in1');
        const addIn2 = _getInputInfo(addNode, 'in2');

        let multiplyNodeName = null;
        let brightness = [0, 0, 0];

        if (addIn2?.type === 'connection') {
            const maybeMultiply = nodeMap.get(addIn2.nodename);
            if (maybeMultiply?.category === 'multiply_color3') {
                multiplyNodeName = addIn2.nodename;
                brightness = addIn1?.type === 'value' ? addIn1.value : [0, 0, 0];
            }
        }
        if (!multiplyNodeName && addIn1?.type === 'connection') {
            const maybeMultiply = nodeMap.get(addIn1.nodename);
            if (maybeMultiply?.category === 'multiply_color3') {
                multiplyNodeName = addIn1.nodename;
                brightness = addIn2?.type === 'value' ? addIn2.value : [0, 0, 0];
            }
        }
        if (!multiplyNodeName) continue;

        const multiplyNode = nodeMap.get(multiplyNodeName);
        const mulIn1 = _getInputInfo(multiplyNode, 'in1');
        const mulIn2 = _getInputInfo(multiplyNode, 'in2');

        let inputNodeName = null;
        let contrast = [1, 1, 1];

        if (mulIn1?.type === 'connection') {
            inputNodeName = mulIn1.nodename;
            contrast = mulIn2?.type === 'value' ? mulIn2.value : [1, 1, 1];
        } else if (mulIn2?.type === 'connection') {
            inputNodeName = mulIn2.nodename;
            contrast = mulIn1?.type === 'value' ? mulIn1.value : [1, 1, 1];
        }
        if (!inputNodeName) continue;

        patterns.push({
            type: 'brightness_contrast',
            outputNode: maxNode.name,
            inputNode: inputNodeName,
            brightness: brightness,
            contrast: contrast,
            nodesToRemove: [subtractNode.name, addNode.name, multiplyNodeName],
            replacement: {
                name: maxNode.name,
                category: 'brightness_contrast_color3',
                type: 'ND_brightness_contrast_color3',
                inputs: [
                    { name: 'in', nodename: inputNodeName, output: 'out' },
                    { name: 'brightness', type: 'color3f', value: brightness },
                    { name: 'contrast', type: 'color3f', value: contrast }
                ]
            }
        });
    }
    return patterns;
}

/**
 * Detect HSV Adjust pattern: combine3 -> hsvadjust
 */
function _detectHSVAdjustPattern(nodes, nodeMap) {
    const patterns = [];
    for (const hsvNode of nodes) {
        if (hsvNode.category !== 'hsvadjust_color3') continue;
        const amountInput = _getInputInfo(hsvNode, 'amount');
        const inInput = _getInputInfo(hsvNode, 'in');
        if (!amountInput || amountInput.type !== 'connection' || !inInput) continue;

        const combineNode = nodeMap.get(amountInput.nodename);
        if (!combineNode || combineNode.category !== 'combine3_vector3') continue;

        const in1 = _getInputInfo(combineNode, 'in1');
        const in2 = _getInputInfo(combineNode, 'in2');
        const in3 = _getInputInfo(combineNode, 'in3');

        patterns.push({
            type: 'hsv_adjust',
            outputNode: hsvNode.name,
            inputNode: inInput.type === 'connection' ? inInput.nodename : null,
            hue: in1?.type === 'value' ? in1.value : 0,
            saturation: in2?.type === 'value' ? in2.value : 1,
            value: in3?.type === 'value' ? in3.value : 1,
            nodesToRemove: [combineNode.name],
            replacement: {
                name: hsvNode.name,
                category: 'hsv_adjust_color3',
                type: 'ND_hsv_adjust_color3',
                inputs: inInput.type === 'connection' ? [
                    { name: 'in', nodename: inInput.nodename, output: 'out' },
                    { name: 'hue', type: 'float', value: in1?.value ?? 0 },
                    { name: 'saturation', type: 'float', value: in2?.value ?? 1 },
                    { name: 'value', type: 'float', value: in3?.value ?? 1 }
                ] : [
                    { name: 'in', type: 'color3f', value: inInput.value },
                    { name: 'hue', type: 'float', value: in1?.value ?? 0 },
                    { name: 'saturation', type: 'float', value: in2?.value ?? 1 },
                    { name: 'value', type: 'float', value: in3?.value ?? 1 }
                ]
            }
        });
    }
    return patterns;
}

/**
 * Detect chained normalize pattern: normalize -> normalize
 * Only the last normalize is needed
 */
function _detectChainedNormalizePattern(nodes, nodeMap) {
    const patterns = [];
    for (const node of nodes) {
        if (node.category !== 'normalize_vector3' && node.category !== 'normalize_float') continue;
        const inInput = _getInputInfo(node, 'in');
        if (!inInput || inInput.type !== 'connection') continue;

        const prevNode = nodeMap.get(inInput.nodename);
        if (!prevNode) continue;

        // Check if previous node is also a normalize
        if (prevNode.category === node.category) {
            const prevInput = _getInputInfo(prevNode, 'in');
            if (prevInput?.type === 'connection') {
                patterns.push({
                    type: 'chained_normalize',
                    outputNode: node.name,
                    redundantNode: prevNode.name,
                    inputNode: prevInput.nodename,
                    nodesToRemove: [prevNode.name],
                    replacement: {
                        ...node,
                        inputs: [{ name: 'in', nodename: prevInput.nodename, output: prevInput.output || 'out' }]
                    }
                });
            }
        }
    }
    return patterns;
}

/**
 * Detect luminance + extract pattern: luminance_color3 -> extract_color3(index=0)
 * Extract after luminance is redundant since luminance outputs a scalar-like value
 */
function _detectLuminanceExtractPattern(nodes, nodeMap) {
    const patterns = [];
    for (const extractNode of nodes) {
        if (extractNode.category !== 'extract_color3') continue;
        const inInput = _getInputInfo(extractNode, 'in');
        const indexInput = _getInputInfo(extractNode, 'index');
        if (!inInput || inInput.type !== 'connection') continue;

        // Check if index is 0 (red channel, which contains luminance result)
        if (indexInput?.type === 'value' && indexInput.value === 0) {
            const prevNode = nodeMap.get(inInput.nodename);
            if (prevNode?.category === 'luminance_color3') {
                // The luminance node outputs to all channels equally, extract[0] is redundant
                patterns.push({
                    type: 'luminance_extract',
                    outputNode: extractNode.name,
                    luminanceNode: prevNode.name,
                    nodesToRemove: [], // Don't remove luminance, just bypass extract
                    replacement: null  // Will be handled as passthrough
                });
            }
        }
    }
    return patterns;
}

/**
 * Detect convert color3<->vector3 roundtrip: convert_color3_vector3 -> convert_vector3_color3
 * This is a no-op and can be removed
 */
function _detectConvertRoundtripPattern(nodes, nodeMap) {
    const patterns = [];
    for (const node of nodes) {
        if (node.category !== 'convert_vector3' && node.type !== 'ND_convert_vector3_color3') continue;
        const inInput = _getInputInfo(node, 'in');
        if (!inInput || inInput.type !== 'connection') continue;

        const prevNode = nodeMap.get(inInput.nodename);
        if (!prevNode) continue;

        // Check if previous is color3 to vector3 conversion
        if (prevNode.category === 'convert_color3' || prevNode.type === 'ND_convert_color3_vector3') {
            const origInput = _getInputInfo(prevNode, 'in');
            if (origInput?.type === 'connection') {
                patterns.push({
                    type: 'convert_roundtrip',
                    outputNode: node.name,
                    convertNode: prevNode.name,
                    inputNode: origInput.nodename,
                    nodesToRemove: [prevNode.name, node.name],
                    passthrough: { nodename: origInput.nodename, output: origInput.output || 'out' }
                });
            }
        }
    }
    return patterns;
}

/**
 * Detect separate + combine passthrough pattern
 * If extract_color3 feeds directly into combine3_color3 in RGB order, it's a no-op
 */
function _detectSeparateCombinePassthrough(nodes, nodeMap) {
    const patterns = [];
    for (const combineNode of nodes) {
        if (combineNode.category !== 'combine3_color3') continue;

        const in1 = _getInputInfo(combineNode, 'in1');
        const in2 = _getInputInfo(combineNode, 'in2');
        const in3 = _getInputInfo(combineNode, 'in3');

        // All inputs must be connections
        if (!in1?.nodename || !in2?.nodename || !in3?.nodename) continue;

        const extract1 = nodeMap.get(in1.nodename);
        const extract2 = nodeMap.get(in2.nodename);
        const extract3 = nodeMap.get(in3.nodename);

        // All must be extract_color3
        if (extract1?.category !== 'extract_color3' ||
            extract2?.category !== 'extract_color3' ||
            extract3?.category !== 'extract_color3') continue;

        // All must extract from the same source
        const src1 = _getInputInfo(extract1, 'in');
        const src2 = _getInputInfo(extract2, 'in');
        const src3 = _getInputInfo(extract3, 'in');

        if (!src1?.nodename || src1.nodename !== src2?.nodename || src1.nodename !== src3?.nodename) continue;

        // Check indices are 0, 1, 2 (RGB order)
        const idx1 = _getInputInfo(extract1, 'index');
        const idx2 = _getInputInfo(extract2, 'index');
        const idx3 = _getInputInfo(extract3, 'index');

        if (idx1?.value === 0 && idx2?.value === 1 && idx3?.value === 2) {
            patterns.push({
                type: 'separate_combine_passthrough',
                outputNode: combineNode.name,
                extractNodes: [extract1.name, extract2.name, extract3.name],
                inputNode: src1.nodename,
                nodesToRemove: [extract1.name, extract2.name, extract3.name, combineNode.name],
                passthrough: { nodename: src1.nodename, output: src1.output || 'out' }
            });
        }
    }
    return patterns;
}

/**
 * Detect clamp with default values (0, 1) - common saturation pattern
 */
function _detectClampPattern(nodes, nodeMap) {
    const patterns = [];
    for (const node of nodes) {
        if (node.category !== 'clamp_float' && node.category !== 'clamp_color3') continue;

        const inInput = _getInputInfo(node, 'in');
        const lowInput = _getInputInfo(node, 'low');
        const highInput = _getInputInfo(node, 'high');

        if (!inInput || inInput.type !== 'connection') continue;

        // Check for clamp(x, 0, 1) - saturate pattern
        const isLowZero = lowInput?.type === 'value' && (_isZero(lowInput.value) || _isBlack(lowInput.value));
        const isHighOne = highInput?.type === 'value' && (_isOne(highInput.value) || _isWhite(highInput.value));

        if (isLowZero && isHighOne) {
            patterns.push({
                type: 'saturate',
                outputNode: node.name,
                inputNode: inInput.nodename,
                nodesToRemove: [],
                replacement: {
                    name: node.name,
                    category: node.category === 'clamp_color3' ? 'saturate_color3' : 'saturate_float',
                    type: node.category === 'clamp_color3' ? 'ND_saturate_color3' : 'ND_saturate_float',
                    inputs: [{ name: 'in', nodename: inInput.nodename, output: 'out' }]
                }
            });
        }
    }
    return patterns;
}

/**
 * Detect geometry normal/tangent setup chain
 * Blender exports: normal -> normalize -> tangent -> normalize -> rotate3d -> normalize
 * This common pattern can be marked as a geometry setup block
 */
function _detectGeometrySetupPattern(nodes, nodeMap) {
    const patterns = [];

    // Find normal_vector3 nodes as starting points
    for (const normalNode of nodes) {
        if (normalNode.category !== 'normal_vector3') continue;

        // Look for the chain: normal -> normalize
        const normalRefs = nodes.filter(n =>
            n.category === 'normalize_vector3' &&
            _getInputInfo(n, 'in')?.nodename === normalNode.name
        );

        if (normalRefs.length === 0) continue;

        // Found a geometry setup - mark the chain for potential optimization
        // For now, we just detect it but don't modify (it's a useful pattern to preserve)
        patterns.push({
            type: 'geometry_setup',
            normalNode: normalNode.name,
            normalizeNodes: normalRefs.map(n => n.name),
            nodesToRemove: [], // Don't remove, just mark
            replacement: null
        });
    }

    return patterns;
}

/**
 * Detect identity operations (multiply by 1, add 0, etc.)
 */
function _detectIdentityOps(nodes, nodeMap) {
    const identities = [];
    for (const node of nodes) {
        let isIdentity = false;
        let inputConnection = null;

        if (node.category === 'multiply_color3' || node.category === 'multiply_float') {
            const in1 = _getInputInfo(node, 'in1');
            const in2 = _getInputInfo(node, 'in2');
            if (in1?.type === 'connection' && in2?.type === 'value' && (_isOne(in2.value) || _isWhite(in2.value))) {
                isIdentity = true; inputConnection = in1;
            } else if (in2?.type === 'connection' && in1?.type === 'value' && (_isOne(in1.value) || _isWhite(in1.value))) {
                isIdentity = true; inputConnection = in2;
            }
        }
        if (node.category === 'add_color3' || node.category === 'add_float') {
            const in1 = _getInputInfo(node, 'in1');
            const in2 = _getInputInfo(node, 'in2');
            if (in1?.type === 'connection' && in2?.type === 'value' && (_isZero(in2.value) || _isBlack(in2.value))) {
                isIdentity = true; inputConnection = in1;
            } else if (in2?.type === 'connection' && in1?.type === 'value' && (_isZero(in1.value) || _isBlack(in1.value))) {
                isIdentity = true; inputConnection = in2;
            }
        }
        if (node.category === 'subtract_color3' || node.category === 'subtract_float') {
            const in1 = _getInputInfo(node, 'in1');
            const in2 = _getInputInfo(node, 'in2');
            if (in1?.type === 'connection' && in2?.type === 'value' && (_isZero(in2.value) || _isBlack(in2.value))) {
                isIdentity = true; inputConnection = in1;
            }
        }
        if (node.category === 'power_color3' || node.category === 'power_float') {
            const in1 = _getInputInfo(node, 'in1');
            const in2 = _getInputInfo(node, 'in2');
            if (in1?.type === 'connection' && in2?.type === 'value' && (_isOne(in2.value) || _isWhite(in2.value))) {
                isIdentity = true; inputConnection = in1;
            }
        }
        if (node.category === 'mix_color3' || node.category === 'mix_float') {
            const mixInput = _getInputInfo(node, 'mix');
            const bgInput = _getInputInfo(node, 'bg');
            const fgInput = _getInputInfo(node, 'fg');
            // mix=0: use bg only
            if (mixInput?.type === 'value' && _isZero(mixInput.value) && bgInput?.type === 'connection') {
                isIdentity = true; inputConnection = bgInput;
            }
            // mix=1: use fg only
            else if (mixInput?.type === 'value' && _isOne(mixInput.value) && fgInput?.type === 'connection') {
                isIdentity = true; inputConnection = fgInput;
            }
        }
        // divide by 1
        if (node.category === 'divide_color3' || node.category === 'divide_float') {
            const in1 = _getInputInfo(node, 'in1');
            const in2 = _getInputInfo(node, 'in2');
            if (in1?.type === 'connection' && in2?.type === 'value' && (_isOne(in2.value) || _isWhite(in2.value))) {
                isIdentity = true; inputConnection = in1;
            }
        }
        // clamp where input is already in range (rare but possible)
        // rotate3d with amount=0
        if (node.category === 'rotate3d_vector3') {
            const amount = _getInputInfo(node, 'amount');
            const inInput = _getInputInfo(node, 'in');
            if (amount?.type === 'value' && _isZero(amount.value) && inInput?.type === 'connection') {
                isIdentity = true; inputConnection = inInput;
            }
        }

        if (isIdentity && inputConnection) {
            identities.push({ node: node.name, passthrough: inputConnection.nodename, output: inputConnection.output || 'out' });
        }
    }
    return identities;
}

/**
 * Apply pattern optimizations to a node graph
 */
function _applyPatternOptimizations(nodeGraph, patterns, identities) {
    if (!nodeGraph?.nodegraph) return nodeGraph;

    const ng = nodeGraph.nodegraph;
    const nodes = [...ng.nodes];

    const nodesToRemove = new Set();
    const replacements = new Map();
    const passthroughMap = new Map();

    // Process patterns
    for (const pattern of patterns) {
        // Add nodes to remove
        if (pattern.nodesToRemove) {
            for (const name of pattern.nodesToRemove) nodesToRemove.add(name);
        }
        // Add replacement if exists
        if (pattern.replacement) {
            replacements.set(pattern.outputNode, pattern.replacement);
        }
        // Handle passthrough patterns (like convert roundtrip, separate+combine)
        if (pattern.passthrough) {
            passthroughMap.set(pattern.outputNode, pattern.passthrough);
            nodesToRemove.add(pattern.outputNode);
        }
    }

    // Process identity operations
    for (const identity of identities) {
        passthroughMap.set(identity.node, { nodename: identity.passthrough, output: identity.output });
        nodesToRemove.add(identity.node);
    }

    // Resolve chained passthroughs (A->B->C becomes A->C)
    let changed = true;
    while (changed) {
        changed = false;
        for (const [node, target] of passthroughMap) {
            if (passthroughMap.has(target.nodename)) {
                const finalTarget = passthroughMap.get(target.nodename);
                passthroughMap.set(node, finalTarget);
                changed = true;
            }
        }
    }

    const newNodes = [];
    for (const node of nodes) {
        if (nodesToRemove.has(node.name) && !replacements.has(node.name)) continue;

        let newNode = replacements.get(node.name) || { ...node };
        if (newNode && newNode.inputs) {
            newNode.inputs = newNode.inputs.map(input => {
                if (input.nodename && passthroughMap.has(input.nodename)) {
                    const pt = passthroughMap.get(input.nodename);
                    return { ...input, nodename: pt.nodename, output: pt.output };
                }
                return input;
            });
        }
        if (newNode) newNodes.push(newNode);
    }

    const newOutputs = (ng.outputs || []).map(output => {
        if (output.nodename && passthroughMap.has(output.nodename)) {
            const pt = passthroughMap.get(output.nodename);
            return { ...output, nodename: pt.nodename, output: pt.output };
        }
        return output;
    });

    return {
        ...nodeGraph,
        nodegraph: { ...ng, nodes: newNodes, outputs: newOutputs },
        optimizationInfo: {
            patternsApplied: patterns.filter(p => p.nodesToRemove?.length > 0 || p.replacement).map(p => p.type),
            identitiesRemoved: identities.length,
            nodesRemoved: nodesToRemove.size,
            originalNodeCount: nodes.length,
            optimizedNodeCount: newNodes.length
        }
    };
}

/**
 * Optimize a MaterialX NodeGraph by detecting and simplifying common patterns
 *
 * @param {Object} nodeGraph - The nodeGraph JSON object (from material.openPBR.nodeGraph)
 * @param {number} level - Optimization level (use NodeGraphOptimizationLevel)
 * @returns {Object} Optimized nodeGraph with optimizationInfo
 */
function optimizeNodeGraph(nodeGraph, level = NodeGraphOptimizationLevel.STANDARD) {
    if (!nodeGraph?.nodegraph || level === NodeGraphOptimizationLevel.NONE) return nodeGraph;

    const nodes = nodeGraph.nodegraph.nodes || [];
    const nodeMap = _buildNodeMap(nodes);

    let patterns = [];
    let identities = [];

    if (level >= NodeGraphOptimizationLevel.STANDARD) {
        patterns = [
            ..._detectInvertPattern(nodes, nodeMap),
            ..._detectBrightnessContrastPattern(nodes, nodeMap),
            ..._detectHSVAdjustPattern(nodes, nodeMap),
            ..._detectChainedNormalizePattern(nodes, nodeMap),
            ..._detectLuminanceExtractPattern(nodes, nodeMap),
            ..._detectConvertRoundtripPattern(nodes, nodeMap),
            ..._detectSeparateCombinePassthrough(nodes, nodeMap),
            ..._detectClampPattern(nodes, nodeMap),
            ..._detectGeometrySetupPattern(nodes, nodeMap)
        ];
    }

    if (level >= NodeGraphOptimizationLevel.BASIC) {
        identities = _detectIdentityOps(nodes, nodeMap);
    }

    return _applyPatternOptimizations(nodeGraph, patterns, identities);
}

/**
 * Analyze a NodeGraph and return optimization opportunities without applying them
 *
 * @param {Object} nodeGraph - The nodeGraph JSON object
 * @returns {Object} Analysis results with patterns and potential reductions
 */
function analyzeNodeGraph(nodeGraph) {
    if (!nodeGraph?.nodegraph) return { error: 'Invalid node graph' };

    const nodes = nodeGraph.nodegraph.nodes || [];
    const nodeMap = _buildNodeMap(nodes);

    // Detect all patterns
    const invertPatterns = _detectInvertPattern(nodes, nodeMap);
    const bcPatterns = _detectBrightnessContrastPattern(nodes, nodeMap);
    const hsvPatterns = _detectHSVAdjustPattern(nodes, nodeMap);
    const chainedNormalizePatterns = _detectChainedNormalizePattern(nodes, nodeMap);
    const luminanceExtractPatterns = _detectLuminanceExtractPattern(nodes, nodeMap);
    const convertRoundtripPatterns = _detectConvertRoundtripPattern(nodes, nodeMap);
    const separateCombinePatterns = _detectSeparateCombinePassthrough(nodes, nodeMap);
    const clampPatterns = _detectClampPattern(nodes, nodeMap);
    const geometrySetupPatterns = _detectGeometrySetupPattern(nodes, nodeMap);
    const identities = _detectIdentityOps(nodes, nodeMap);

    const allNodesToRemove = new Set([
        ...invertPatterns.flatMap(p => p.nodesToRemove),
        ...bcPatterns.flatMap(p => p.nodesToRemove),
        ...hsvPatterns.flatMap(p => p.nodesToRemove),
        ...chainedNormalizePatterns.flatMap(p => p.nodesToRemove),
        ...luminanceExtractPatterns.flatMap(p => p.nodesToRemove),
        ...convertRoundtripPatterns.flatMap(p => p.nodesToRemove),
        ...separateCombinePatterns.flatMap(p => p.nodesToRemove),
        ...clampPatterns.flatMap(p => p.nodesToRemove),
        ...geometrySetupPatterns.flatMap(p => p.nodesToRemove),
        ...identities.map(i => i.node)
    ]);

    const totalPatterns = invertPatterns.length + bcPatterns.length + hsvPatterns.length +
        chainedNormalizePatterns.length + luminanceExtractPatterns.length + convertRoundtripPatterns.length +
        separateCombinePatterns.length + clampPatterns.length + geometrySetupPatterns.length;

    return {
        totalNodes: nodes.length,
        patterns: {
            invert: invertPatterns.map(p => ({ outputNode: p.outputNode, inputNode: p.inputNode, factor: p.factor })),
            brightnessContrast: bcPatterns.map(p => ({ outputNode: p.outputNode, inputNode: p.inputNode, brightness: p.brightness, contrast: p.contrast })),
            hsvAdjust: hsvPatterns.map(p => ({ outputNode: p.outputNode, inputNode: p.inputNode, hue: p.hue, saturation: p.saturation, value: p.value })),
            chainedNormalize: chainedNormalizePatterns.map(p => ({ outputNode: p.outputNode, chainLength: p.chainLength })),
            luminanceExtract: luminanceExtractPatterns.map(p => ({ outputNode: p.outputNode, inputNode: p.inputNode })),
            convertRoundtrip: convertRoundtripPatterns.map(p => ({ outputNode: p.outputNode, inputNode: p.inputNode })),
            separateCombine: separateCombinePatterns.map(p => ({ outputNode: p.outputNode, inputNode: p.inputNode })),
            clamp: clampPatterns.map(p => ({ outputNode: p.outputNode, isDefaultRange: p.isDefaultRange })),
            geometrySetup: geometrySetupPatterns.map(p => ({ outputNode: p.outputNode, mergedCount: p.mergedCount }))
        },
        identityOps: identities,
        potentialReduction: {
            patternsFound: totalPatterns,
            identitiesFound: identities.length,
            nodesRemovable: allNodesToRemove.size
        }
    };
}

/**
 * Get a human-readable summary of optimization results
 *
 * @param {Object} optimizedGraph - Graph returned from optimizeNodeGraph()
 * @returns {string} Human-readable summary
 */
function getOptimizationSummary(optimizedGraph) {
    if (!optimizedGraph?.optimizationInfo) return 'No optimization info available';
    const info = optimizedGraph.optimizationInfo;
    return [
        `Node count: ${info.originalNodeCount} -> ${info.optimizedNodeCount} (${info.nodesRemoved} removed)`,
        `Patterns applied: ${info.patternsApplied.length > 0 ? info.patternsApplied.join(', ') : 'none'}`,
        `Identity ops removed: ${info.identitiesRemoved}`
    ].join('\n');
}

// ============================================================================
// TinyUSDZOpenPBR Class (Legacy compatibility)
// ============================================================================

/**
 * A minimal OpenPBR material representation for compatibility
 */
class TinyUSDZOpenPBR {
    constructor(opts = {}) {
        this.baseColor = opts.baseColor !== undefined ? new THREE.Color(opts.baseColor) : new THREE.Color(1, 1, 1);
        this.opacity = opts.opacity !== undefined ? opts.opacity : 1.0;
        this.metallic = opts.metallic !== undefined ? opts.metallic : 0.0;
        this.roughness = opts.roughness !== undefined ? opts.roughness : 0.5;
        this.emissive = opts.emissive !== undefined ? new THREE.Color(opts.emissive) : new THREE.Color(0, 0, 0);
        this.emissiveIntensity = opts.emissiveIntensity !== undefined ? opts.emissiveIntensity : 0.0;

        // Texture map placeholders
        this.baseColorMap = opts.baseColorMap || null;
        this.metallicRoughnessMap = opts.metallicRoughnessMap || null;
        this.normalMap = opts.normalMap || null;
        this.aoMap = opts.aoMap || null;
        this.emissiveMap = opts.emissiveMap || null;

        // UV transform
        this.uvScale = opts.uvScale || new THREE.Vector2(1, 1);
        this.uvOffset = opts.uvOffset || new THREE.Vector2(0, 0);

        this.name = opts.name || '';
    }

    /**
     * Convert to MeshPhysicalMaterial
     */
    toMeshPhysicalMaterial() {
        const material = new THREE.MeshPhysicalMaterial({
            color: this.baseColor,
            metalness: this.metallic,
            roughness: this.roughness,
            emissive: this.emissive,
            emissiveIntensity: this.emissiveIntensity,
            opacity: this.opacity,
            transparent: this.opacity < 1.0,
            map: this.baseColorMap,
            metalnessMap: this.metallicRoughnessMap,
            roughnessMap: this.metallicRoughnessMap,
            normalMap: this.normalMap,
            aoMap: this.aoMap,
            emissiveMap: this.emissiveMap
        });

        material.name = this.name;
        material.userData.openPBR = this;

        return material;
    }
}

// ============================================================================
// Exports
// ============================================================================

export {
    // Material conversion
    TinyUSDZOpenPBR,
    convertOpenPBRToMeshPhysicalMaterial,
    convertOpenPBRToMeshPhysicalMaterialLoaded,
    loadTextureFromUSD,
    extractValue,
    hasTexture,
    getTextureId,
    createColor,
    OPENPBR_TO_THREEJS_MAP,
    OPENPBR_TEXTURE_MAP,
    // NodeGraph optimization
    NodeGraphOptimizationLevel,
    optimizeNodeGraph,
    analyzeNodeGraph,
    getOptimizationSummary
};
