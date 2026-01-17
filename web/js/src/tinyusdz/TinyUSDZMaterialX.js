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
    TinyUSDZOpenPBR,
    convertOpenPBRToMeshPhysicalMaterial,
    convertOpenPBRToMeshPhysicalMaterialLoaded,
    loadTextureFromUSD,
    extractValue,
    hasTexture,
    getTextureId,
    createColor,
    OPENPBR_TO_THREEJS_MAP,
    OPENPBR_TEXTURE_MAP
};
