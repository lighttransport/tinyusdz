import * as THREE from 'three';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';
import { EXRLoader } from 'three/examples/jsm/loaders/EXRLoader.js';

import { LoaderUtils } from "three"
import { convertOpenPBRToMeshPhysicalMaterialLoaded } from './TinyUSDZMaterialX.js';
import { decodeEXR as decodeEXRWithFallback } from './EXRDecoder.js';

/**
 * TextureLoadingManager - Manages delayed/progressive texture loading.
 *
 * This allows the scene to render immediately with basic materials,
 * while textures load in the background with progress reporting.
 *
 * Usage:
 *   const manager = new TextureLoadingManager();
 *
 *   // Queue texture tasks during material setup
 *   manager.queueTexture(material, 'map', textureId, usdScene);
 *
 *   // Start loading after scene is rendered
 *   await manager.startLoading({
 *     onProgress: (info) => console.log(`${info.loaded}/${info.total} textures`),
 *     concurrency: 2,  // Load 2 textures at a time
 *     yieldInterval: 16  // Yield to browser every 16ms
 *   });
 */
class TextureLoadingManager {
    constructor() {
        this.queue = [];           // Pending texture tasks
        this.loaded = 0;           // Number of loaded textures
        this.failed = 0;           // Number of failed textures
        this.total = 0;            // Total textures to load
        this.isLoading = false;    // Loading in progress
        this.aborted = false;      // Loading was aborted
    }

    /**
     * Queue a texture to be loaded later
     * @param {THREE.Material} material - Target material
     * @param {string} mapProperty - Property name (e.g., 'map', 'normalMap')
     * @param {number} textureId - USD texture ID
     * @param {Object} usdScene - USD scene/loader instance
     * @param {Object} options - Additional options (e.g., normalScale)
     */
    queueTexture(material, mapProperty, textureId, usdScene, options = {}) {
        this.queue.push({
            material,
            mapProperty,
            textureId,
            usdScene,
            options,
            status: 'pending'
        });
        this.total = this.queue.length;
    }

    /**
     * Get current loading status
     */
    getStatus() {
        return {
            total: this.total,
            loaded: this.loaded,
            failed: this.failed,
            pending: this.total - this.loaded - this.failed,
            percentage: this.total > 0 ? (this.loaded / this.total) * 100 : 0,
            isLoading: this.isLoading,
            isComplete: this.loaded + this.failed >= this.total && !this.isLoading
        };
    }

    /**
     * Abort loading
     */
    abort() {
        this.aborted = true;
    }

    /**
     * Reset manager for new loading session
     */
    reset() {
        this.queue = [];
        this.loaded = 0;
        this.failed = 0;
        this.total = 0;
        this.isLoading = false;
        this.aborted = false;
    }

    /**
     * Start loading queued textures with progress reporting
     * @param {Object} options - Loading options
     * @param {Function} options.onProgress - Progress callback ({loaded, total, percentage, currentTexture})
     * @param {Function} options.onTextureLoaded - Called when each texture loads (material, mapProperty, texture)
     * @param {number} options.concurrency - Number of concurrent loads (default: 1)
     * @param {number} options.yieldInterval - ms between browser yields (default: 16)
     * @returns {Promise<Object>} - Final status {loaded, failed, total}
     */
    async startLoading(options = {}) {
        const {
            onProgress = null,
            onTextureLoaded = null,
            concurrency = 1,
            yieldInterval = 16
        } = options;

        if (this.isLoading) {
            console.warn('TextureLoadingManager: Already loading');
            return this.getStatus();
        }

        this.isLoading = true;
        this.aborted = false;
        let lastYieldTime = performance.now();

        // Report initial progress
        if (onProgress) {
            onProgress({
                loaded: 0,
                total: this.total,
                percentage: 0,
                currentTexture: null
            });
        }

        // Yield to allow initial render without textures
        await new Promise(r => requestAnimationFrame(r));

        // Process queue with concurrency control
        const pendingTasks = [...this.queue];
        const activeTasks = new Set();

        const loadTexture = async (task) => {
            if (this.aborted) return;

            task.status = 'loading';
            const { material, mapProperty, textureId, usdScene, options: taskOptions } = task;

            try {
                const texture = await TinyUSDZLoaderUtils.getTextureFromUSD(usdScene, textureId);

                if (texture && !this.aborted) {
                    material[mapProperty] = texture;

                    // Apply special options (e.g., normal map scale)
                    if (taskOptions.normalScale !== undefined && mapProperty === 'normalMap' && material.normalScale) {
                        material.normalScale.set(taskOptions.normalScale, taskOptions.normalScale);
                    }

                    material.needsUpdate = true;
                    task.status = 'loaded';
                    this.loaded++;

                    if (onTextureLoaded) {
                        onTextureLoaded(material, mapProperty, texture);
                    }
                }
            } catch (err) {
                console.warn(`Failed to load texture ${textureId} for ${mapProperty}:`, err.message);
                task.status = 'failed';
                this.failed++;
            }

            // Report progress
            if (onProgress && !this.aborted) {
                onProgress({
                    loaded: this.loaded,
                    failed: this.failed,
                    total: this.total,
                    percentage: (this.loaded / this.total) * 100,
                    currentTexture: `${mapProperty} (${textureId})`
                });
            }

            // Yield to browser periodically
            const now = performance.now();
            if (now - lastYieldTime >= yieldInterval) {
                lastYieldTime = now;
                await new Promise(r => requestAnimationFrame(r));
            }
        };

        // Process with concurrency limit
        while (pendingTasks.length > 0 || activeTasks.size > 0) {
            if (this.aborted) break;

            // Start new tasks up to concurrency limit
            while (pendingTasks.length > 0 && activeTasks.size < concurrency) {
                const task = pendingTasks.shift();
                const promise = loadTexture(task).then(() => {
                    activeTasks.delete(promise);
                });
                activeTasks.add(promise);
            }

            // Wait for at least one task to complete
            if (activeTasks.size > 0) {
                await Promise.race(activeTasks);
            }
        }

        this.isLoading = false;

        // Final progress report
        if (onProgress) {
            onProgress({
                loaded: this.loaded,
                failed: this.failed,
                total: this.total,
                percentage: 100,
                currentTexture: null,
                isComplete: true
            });
        }

        return this.getStatus();
    }
}

// Export the manager class
export { TextureLoadingManager };

class TinyUSDZLoaderUtils extends LoaderUtils {

    // Static reference to TinyUSDZ WASM module for EXR fallback
    static _tinyusdz = null;

    // Yield interval for UI updates (ms)
    static YIELD_INTERVAL_MS = 16; // ~60fps

    constructor() {
        super();
    }

    /**
     * Yield to browser to allow UI repaint during long-running async operations.
     * Uses requestAnimationFrame for optimal frame timing.
     * @returns {Promise<void>}
     */
    static yieldToUI() {
        return new Promise(resolve => {
            // Use requestAnimationFrame for smoother updates
            // Falls back to setTimeout if RAF is not available
            if (typeof requestAnimationFrame === 'function') {
                requestAnimationFrame(() => resolve());
            } else {
                setTimeout(resolve, 0);
            }
        });
    }

    /**
     * Conditional yield - only yields if enough time has passed since last yield
     * @param {Object} state - State object with lastYieldTime property
     * @returns {Promise<void>}
     */
    static async maybeYieldToUI(state) {
        const now = performance.now();
        if (!state.lastYieldTime || (now - state.lastYieldTime) >= this.YIELD_INTERVAL_MS) {
            state.lastYieldTime = now;
            await this.yieldToUI();
        }
    }

    /**
     * Set TinyUSDZ WASM module for EXR decoding fallback
     * @param {Object} tinyusdz - TinyUSDZ WASM module instance
     */
    static setTinyUSDZ(tinyusdz) {
        TinyUSDZLoaderUtils._tinyusdz = tinyusdz;
    }

    /**
     * Get TinyUSDZ WASM module
     * @returns {Object|null}
     */
    static getTinyUSDZ() {
        return TinyUSDZLoaderUtils._tinyusdz;
    }

    // Extract file extension from URI/path
    static getFileExtension(uri) {
        if (!uri || typeof uri !== 'string') return '';

        // Remove query parameters and hash
        const cleanUri = uri.split('?')[0].split('#')[0];

        // Get the last part after the last dot
        const lastDotIndex = cleanUri.lastIndexOf('.');
        if (lastDotIndex === -1 || lastDotIndex === cleanUri.length - 1) {
            return '';
        }

        return cleanUri.substring(lastDotIndex + 1).toLowerCase();
    }

    // Determine MIME type from file extension
    static getMimeTypeFromExtension(extension) {
        const mimeTypes = {
            // Images
            'jpg': 'image/jpeg',
            'jpeg': 'image/jpeg',
            'png': 'image/png',
            'gif': 'image/gif',
            'webp': 'image/webp',
            'bmp': 'image/bmp',
            'tiff': 'image/tiff',
            'tif': 'image/tiff',
            'svg': 'image/svg+xml',
            'ico': 'image/x-icon',

            // HDR/EXR formats
            'hdr': 'image/vnd.radiance',
            'exr': 'image/x-exr',
            'rgbe': 'image/vnd.radiance',

            // 3D/USD formats
            'usd': 'model/vnd.usdz+zip',
            'usda': 'model/vnd.usd+ascii',
            'usdc': 'model/vnd.usd+binary',
            'usdz': 'model/vnd.usdz+zip',

            // Other common formats
            'json': 'application/json',
            'xml': 'application/xml',
            'txt': 'text/plain',
            'bin': 'application/octet-stream'
        };

        return mimeTypes[extension.toLowerCase()] || null;
    }

    // Helper method to determine MIME type
    static getMimeType(texImage) {

        if (texImage.uri) {
            const mime = this.getMimeTypeFromExtension(this.getFileExtension(texImage.uri));
            if (mime != null) {
                return mime;
            }
        }

        // Try to detect from magic bytes if available
        const data = new Uint8Array(texImage.data);
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
            // EXR magic bytes: 76 2F 31 01
            if (data[0] === 0x76 && data[1] === 0x2F && data[2] === 0x31 && data[3] === 0x01) {
                return 'image/x-exr';
            }
            // HDR magic bytes: "#?" (Radiance format)
            if (data[0] === 0x23 && data[1] === 0x3F) {
                return 'image/vnd.radiance';
            }
        }

        // Default fallback
        return 'image/png';
    }

    static async getTextureFromUSD(usdScene, textureId) {
        if (textureId === undefined) return Promise.reject(new Error("textureId undefined"));


        const tex = usdScene.getTexture(textureId);

        const texImage = usdScene.getImage(tex.textureImageId);

        // there are 3 states for texture:
        // 1. URI only. Need to fetch texture(file) from URI in JS layer.
        // 2. Texture is loaded from USDZ file, but not yet decoded(Use Three.js or JS library to decode)
        // 3. Texture is decoded and ready to use in Three.js.

        if (texImage.uri && (texImage.bufferId == -1)) {
            // Case 1: URI only
            const lowerUri = texImage.uri.toLowerCase();

            if (lowerUri.endsWith('.exr')) {
                // EXR: Use EXRLoader
                return new EXRLoader().loadAsync(texImage.uri);
            } else if (lowerUri.endsWith('.hdr')) {
                // HDR: Use HDRLoader
                return new HDRLoader().loadAsync(texImage.uri);
            } else {
                // Standard image
                return new THREE.TextureLoader().loadAsync(texImage.uri);
            }

        } else if (texImage.bufferId >= 0 && texImage.data) {

            if (texImage.decoded) {

                const image8Array = new Uint8ClampedArray(texImage.data);
                const texture = new THREE.DataTexture(image8Array, texImage.width, texImage.height);
                if (texImage.channels == 1) {
                    texture.format = THREE.RedFormat;
                } else if (texImage.channels == 2) {
                    texture.format = THREE.RGFormat;
                } else if (texImage.channels == 3) {
                    // Recent three.js does not support RGBFormat.
                    return Promise.reject(new Error("RGB image is not supported"));
                } else if (texImage.channels == 4) {
                    texture.format = THREE.RGBAFormat;
                } else {
                    return Promise.reject(new Error("Unsupported image channels: " + texImage.channels));
                }
                texture.flipY = true;
                texture.needsUpdate = true;

                return Promise.resolve(texture);

            } else {
                // Case 2: Embedded but not decoded - check format
                try {
                    const mimeType = this.getMimeType(texImage);

                    // Check if HDR/EXR format - use specialized decoders
                    if (mimeType === 'image/x-exr') {
                        // EXR: Use TinyUSDZ fallback decoder
                        const texture = this.decodeEXRFromBuffer(texImage.data, 'float16');
                        if (texture) {
                            texture.flipY = true;
                            return Promise.resolve(texture);
                        }
                        // Fallback to Three.js EXRLoader with blob URL
                        const blob = new Blob([texImage.data], { type: mimeType });
                        const blobUrl = URL.createObjectURL(blob);
                        return new EXRLoader().loadAsync(blobUrl).finally(() => URL.revokeObjectURL(blobUrl));
                    } else if (mimeType === 'image/vnd.radiance') {
                        // HDR: Use TinyUSDZ decoder (faster)
                        const tinyusdz = TinyUSDZLoaderUtils._tinyusdz;
                        if (tinyusdz && typeof tinyusdz.decodeHDR === 'function') {
                            const uint8Array = texImage.data instanceof Uint8Array
                                ? texImage.data
                                : new Uint8Array(texImage.data);
                            const result = tinyusdz.decodeHDR(uint8Array, 'float16');
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
                                texture.flipY = true;
                                texture.needsUpdate = true;
                                return Promise.resolve(texture);
                            }
                        }
                        // Fallback to Three.js HDRLoader
                        const blob = new Blob([texImage.data], { type: mimeType });
                        const blobUrl = URL.createObjectURL(blob);
                        return new HDRLoader().loadAsync(blobUrl).finally(() => URL.revokeObjectURL(blobUrl));
                    } else {
                        // Standard image format
                        const blob = new Blob([texImage.data], { type: mimeType });
                        const blobUrl = URL.createObjectURL(blob);
                        const loader = new THREE.TextureLoader();
                        return loader.loadAsync(blobUrl).finally(() => URL.revokeObjectURL(blobUrl));
                    }
                } catch (error) {
                    console.error("Failed to decode texture data:", error);
                    return Promise.reject(new Error("Failed to decode texture data"));
                }
            }

        } else {
            return Promise.reject(new Error("Invalid USD texture info"));
        }
    }

    static createDefaultMaterial() {
        return new THREE.MeshPhysicalMaterial({
            color: new THREE.Color(0.18, 0.18, 0.18),
            emissive: 0x000000,
            metalness: 0.0,
            roughness: 0.5,
            transparent: false,
            depthTest: true,
            side: THREE.FrontSide
        });
    }

    //
    // Convert UsdPreviewSureface to MeshPhysicalMaterial
    // - [x] diffuseColor -> color
    // - [x] ior -> ior
    // - [x] clearcoat -> clearcoat
    // - [x] clearcoatRoughness -> clearcoatRoughness
    // - [x] specularColor -> specular
    // - [x] roughness -> roughness
    // - [x] metallic -> metalness
    // - [x] emissiveColor -> emissive
    // - [x] opacity -> opacity (TODO: map to .transmission?)
    // - [x] occlusion -> aoMap
    // - [x] normal -> normalMap
    // - [x] displacement -> displacementMap
    //
    // Options:
    // - textureLoadingManager: TextureLoadingManager instance for delayed texture loading
    //   If provided, textures are queued instead of loaded immediately
    //
    static convertUsdMaterialToMeshPhysicalMaterial(usdMaterial, usdScene, options = {}) {
        const material = new THREE.MeshPhysicalMaterial();
        const textureManager = options.textureLoadingManager || null;

        // Helper to load texture immediately or queue for later
        const loadOrQueueTexture = (mapProperty, textureId, textureOptions = {}) => {
            if (textureManager) {
                // Delayed mode: queue texture for later loading
                textureManager.queueTexture(material, mapProperty, textureId, usdScene, textureOptions);
            } else {
                // Immediate mode: load texture now (original behavior)
                this.getTextureFromUSD(usdScene, textureId).then((texture) => {
                    material[mapProperty] = texture;
                    material.needsUpdate = true;
                }).catch((err) => {
                    console.error(`failed to load ${mapProperty} texture:`, err);
                });
            }
        };

        // Diffuse color and texture
        material.color = new THREE.Color(0.18, 0.18, 0.18);
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'diffuseColor')) {
            const color = usdMaterial.diffuseColor;
            material.color = new THREE.Color(color[0], color[1], color[2]);
        }

        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'diffuseColorTextureId')) {
            loadOrQueueTexture('map', usdMaterial.diffuseColorTextureId);
        }

        // IOR
        material.ior = 1.5;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'ior')) {
            material.ior = usdMaterial.ior;
        }

        // Clearcoat
        material.clearcoat = 0.0;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'clearcoat')) {
            material.clearcoat = usdMaterial.clearcoat;
        }

        material.clearcoatRoughness = 0.0;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'clearcoatRoughness')) {
            material.clearcoatRoughness = usdMaterial.clearcoatRoughness;
        }

        // Workflow selection
        material.useSpecularWorkflow = false;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'useSpecularWorkflow')) {
            material.useSpecularWorkflow = usdMaterial.useSpecularWorkflow;
        }

        if (material.useSpecularWorkflow) {
            material.specularColor = new THREE.Color(0.0, 0.0, 0.0);
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'specularColor')) {
                const color = usdMaterial.specularColor;
                material.specularColor = new THREE.Color(color[0], color[1], color[2]);
            }
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'specularColorTextureId')) {
                loadOrQueueTexture('specularColorMap', usdMaterial.specularColorTextureId);
            }
        } else {
            material.metalness = 0.0;
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'metallic')) {
                material.metalness = usdMaterial.metallic;
            }
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'metallicTextureId')) {
                loadOrQueueTexture('metalnessMap', usdMaterial.metallicTextureId);
            }
        }

        // Roughness
        material.roughness = 0.5;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'roughness')) {
            material.roughness = usdMaterial.roughness;
        }
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'roughnessTextureId')) {
            loadOrQueueTexture('roughnessMap', usdMaterial.roughnessTextureId);
        }

        // Emissive
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'emissiveColor')) {
            const color = usdMaterial.emissiveColor;
            material.emissive = new THREE.Color(color[0], color[1], color[2]);
        }
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'emissiveColorTextureId')) {
            loadOrQueueTexture('emissiveMap', usdMaterial.emissiveColorTextureId);
        }

        // Opacity
        material.opacity = 1.0;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'opacity')) {
            material.opacity = usdMaterial.opacity;
            if (material.opacity < 1.0) {
                material.transparent = true;
            }
        }
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'opacityTextureId')) {
            loadOrQueueTexture('alphaMap', usdMaterial.opacityTextureId);
        }

        // Ambient Occlusion
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'occlusionTextureId')) {
            loadOrQueueTexture('aoMap', usdMaterial.occlusionTextureId);
        }

        // Normal Map
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'normalTextureId')) {
            loadOrQueueTexture('normalMap', usdMaterial.normalTextureId);
        }

        // Displacement Map
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'displacementTextureId')) {
            loadOrQueueTexture('displacementMap', usdMaterial.displacementTextureId, { displacementScale: 1.0 });
        }

        return material;
    }

    //
    // Material Type Detection
    //
    // Returns an object describing what material types are available:
    // {
    //   hasOpenPBR: boolean,           // Has OpenPBR (MaterialX) data
    //   hasUsdPreviewSurface: boolean, // Has UsdPreviewSurface data
    //   hasBoth: boolean,              // Has both material types
    //   hasNone: boolean,              // Has no material data
    //   recommended: string            // Recommended type: 'openpbr', 'usdpreviewsurface', or 'none'
    // }
    //
    // Usage:
    //   const materialData = usdScene.getMaterial(materialId, 'json');
    //   const typeInfo = TinyUSDZLoaderUtils.getMaterialType(materialData);
    //
    static getMaterialType(materialData) {
        // Parse JSON if needed
        let parsedMaterial = materialData;
        if (typeof materialData === 'string') {
            try {
                parsedMaterial = JSON.parse(materialData);
            } catch (e) {
                console.error('Failed to parse material JSON:', e);
                return {
                    hasOpenPBR: false,
                    hasUsdPreviewSurface: false,
                    hasBoth: false,
                    hasNone: true,
                    recommended: 'none'
                };
            }
        }

        if (!parsedMaterial) {
            return {
                hasOpenPBR: false,
                hasUsdPreviewSurface: false,
                hasBoth: false,
                hasNone: true,
                recommended: 'none'
            };
        }

        const hasOpenPBR = !!parsedMaterial.hasOpenPBR;
        const hasUsdPreviewSurface = !!parsedMaterial.hasUsdPreviewSurface;
        const hasBoth = hasOpenPBR && hasUsdPreviewSurface;
        const hasNone = !hasOpenPBR && !hasUsdPreviewSurface;

        // Determine recommended type (prefer OpenPBR when both are available)
        let recommended = 'none';
        if (hasOpenPBR) {
            recommended = 'openpbr';
        } else if (hasUsdPreviewSurface) {
            recommended = 'usdpreviewsurface';
        }

        return {
            hasOpenPBR,
            hasUsdPreviewSurface,
            hasBoth,
            hasNone,
            recommended
        };
    }

    //
    // Get material type as a human-readable string
    //
    // Returns: 'OpenPBR', 'UsdPreviewSurface', 'Both', or 'None'
    //
    static getMaterialTypeString(materialData) {
        const typeInfo = this.getMaterialType(materialData);

        if (typeInfo.hasBoth) return 'Both';
        if (typeInfo.hasOpenPBR) return 'OpenPBR';
        if (typeInfo.hasUsdPreviewSurface) return 'UsdPreviewSurface';
        return 'None';
    }

    //
    // Convert OpenPBR (MaterialX) to MeshPhysicalMaterial
    // Supports all OpenPBR layers: base, specular, transmission, coat, sheen, fuzz, thin_film, emission
    //
    // Usage:
    //   const materialData = usdScene.getMaterial(materialId, 'json');
    //   const material = await TinyUSDZLoaderUtils.convertOpenPBRMaterialToMeshPhysicalMaterial(materialData, usdScene, options);
    //
    static async convertOpenPBRMaterialToMeshPhysicalMaterial(materialData, usdScene, options = {}) {
        // Parse JSON material data if it's a string
        let parsedMaterial = materialData;
        if (typeof materialData === 'string') {
            try {
                parsedMaterial = JSON.parse(materialData);
            } catch (e) {
                console.error('Failed to parse material JSON:', e);
                return this.createDefaultMaterial();
            }
        }

        // Check if material has OpenPBR data
        if (!parsedMaterial || !parsedMaterial.hasOpenPBR) {
            console.warn('Material does not have OpenPBR data, falling back to UsdPreviewSurface');
            // Fall back to UsdPreviewSurface if available
            if (parsedMaterial && parsedMaterial.hasUsdPreviewSurface) {
                // Extract surfaceShader data from the JSON structure
                const shaderData = parsedMaterial.surfaceShader || parsedMaterial;
                return this.convertUsdMaterialToMeshPhysicalMaterial(shaderData, usdScene, options);
            }
            return this.createDefaultMaterial();
        }

        try {
            // Use the TinyUSDZMaterialX converter (Loaded version waits for textures(if textureLoadingManager is null))
            const material = await convertOpenPBRToMeshPhysicalMaterialLoaded(parsedMaterial, usdScene, {
                envMap: options.envMap || null,
                envMapIntensity: options.envMapIntensity || 1.0,
                textureCache: options.textureCache || new Map(),
                textureLoadingManager: options.textureLoadingManager || null
            });

            // Apply sideness based on USD doubleSided attribute
            if (options.doubleSided !== undefined) {
                material.side = options.doubleSided ? THREE.DoubleSide : THREE.FrontSide;
            }

            return material;

        } catch (error) {
            console.error('Failed to convert OpenPBR material:', error);
            return this.createDefaultMaterial();
        }
    }

    //
    // Smart material conversion: automatically selects OpenPBR or UsdPreviewSurface
    //
    // Options:
    //   preferredMaterialType: 'auto' | 'openpbr' | 'usdpreviewsurface'
    //     - 'auto': Prefer OpenPBR when both are available (recommended)
    //     - 'openpbr': Force OpenPBR if available, fallback to UsdPreviewSurface
    //     - 'usdpreviewsurface': Force UsdPreviewSurface if available, fallback to OpenPBR
    //
    // Usage:
    //   const materialData = usdScene.getMaterial(materialId, 'json');
    //   const material = await TinyUSDZLoaderUtils.convertMaterial(materialData, usdScene, options);
    //
    static async convertMaterial(materialData, usdScene, options = {}) {
        // Get material type info
        const typeInfo = this.getMaterialType(materialData);

        // If no material data, return default
        if (typeInfo.hasNone) {
            return this.createDefaultMaterial();
        }

        // Parse material data for conversion
        let parsedMaterial = materialData;
        if (typeof materialData === 'string') {
            try {
                parsedMaterial = JSON.parse(materialData);
            } catch (e) {
                console.error('Failed to parse material JSON:', e);
                return this.createDefaultMaterial();
            }
        }

        // Determine which material type to use based on preference
        const preferredType = options.preferredMaterialType || 'auto';
        let useOpenPBR = false;
        let useUsdPreviewSurface = false;

        switch (preferredType) {
            case 'auto':
                // Auto mode: prefer OpenPBR when available (including when both are present)
                if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                } else if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                }
                break;

            case 'openpbr':
                // Force OpenPBR if available, fallback to UsdPreviewSurface
                if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                } else if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                    console.warn('OpenPBR requested but not available, falling back to UsdPreviewSurface');
                }
                break;

            case 'usdpreviewsurface':
                // Force UsdPreviewSurface if available, fallback to OpenPBR
                if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                } else if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                    console.warn('UsdPreviewSurface requested but not available, falling back to OpenPBR');
                }
                break;

            default:
                // Unknown preference, use auto behavior
                if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                } else if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                }
        }

        // Log material type selection for debugging
        if (typeInfo.hasBoth) {
        }

        // Convert using selected material type
        if (useOpenPBR) {
            return this.convertOpenPBRMaterialToMeshPhysicalMaterial(parsedMaterial, usdScene, options);
        } else if (useUsdPreviewSurface) {
            // Extract surfaceShader data from the JSON structure
            // The JSON format nests shader properties under surfaceShader
            const shaderData = parsedMaterial.surfaceShader || parsedMaterial;
            // Pass options through to support textureLoadingManager
            return this.convertUsdMaterialToMeshPhysicalMaterial(shaderData, usdScene, options);
        }

        return this.createDefaultMaterial();
    }

    static convertUsdMeshToThreeMesh(mesh) {
        const geometry = new THREE.BufferGeometry();
        // IMPORTANT: Copy all typed arrays from WASM heap into JS-owned buffers.
        // The C++ TinyUSDZLoaderNative object is explicitly deleted via .delete()
        // at the end of processUSDScene to free WASM heap memory. After deletion,
        // typed_memory_view references into render_scene_ become stale (data freed
        // by C++ destructor, overwritten by allocator bookkeeping).

        // Validate WASM buffer health before copying.
        // Emscripten typed_memory_view returns TypedArrays sharing the WASM
        // heap's ArrayBuffer. If memory.grow() is called (heap resize), ALL
        // existing views' backing buffer is detached (byteLength becomes 0).
        // Detect this and re-fetch the mesh from WASM if needed.
        const meshName = mesh.primName || mesh.absPath || '(unknown)';
        if (mesh.points && mesh.points.buffer && mesh.points.buffer.byteLength === 0) {
          console.error(`[WASM] DETACHED buffer for mesh "${meshName}" points! WASM heap likely grew.`);
        }
        if (mesh.faceVertexIndices && mesh.faceVertexIndices.buffer && mesh.faceVertexIndices.buffer.byteLength === 0) {
          console.error(`[WASM] DETACHED buffer for mesh "${meshName}" faceVertexIndices! WASM heap likely grew.`);
        }

        geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(mesh.points), 3));

        if (Object.prototype.hasOwnProperty.call(mesh, 'faceVertexIndices')) {
          if (mesh.faceVertexIndices.length >0 ) {
            const indices = new Uint32Array(mesh.faceVertexIndices);
            // Validate: check for out-of-range indices (common corruption indicator)
            const numVertices = mesh.points.length / 3;
            let maxIdx = 0, oobCount = 0, zeroIdxCount = 0;
            for (let i = 0; i < indices.length; i++) {
              if (indices[i] >= numVertices) oobCount++;
              if (indices[i] === 0) zeroIdxCount++;
              if (indices[i] > maxIdx) maxIdx = indices[i];
            }
            if (oobCount > 0) {
              console.error(`[MESH] "${meshName}": ${oobCount}/${indices.length} indices OUT OF RANGE (max idx=${maxIdx}, numVerts=${numVertices})`);
            }
            // High zero-index ratio can indicate corrupted/zeroed-out index data
            const zeroRatio = zeroIdxCount / indices.length;
            if (zeroRatio > 0.3 && indices.length > 100) {
              console.warn(`[MESH] "${meshName}": ${(zeroRatio*100).toFixed(1)}% of indices are 0 (${zeroIdxCount}/${indices.length}) — possible data corruption`);
            }
            geometry.setIndex(new THREE.BufferAttribute(indices, 1));
          }
        }

        if (Object.prototype.hasOwnProperty.call(mesh, 'texcoords')) {
            geometry.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(mesh.texcoords), 2));
        }

        // TODO: uv1

        // faceVarying normals
        if (Object.prototype.hasOwnProperty.call(mesh, 'normals')) {
            geometry.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(mesh.normals), 3));
        } else {
            geometry.computeVertexNormals();
        }

        if (Object.prototype.hasOwnProperty.call(mesh, 'vertexColors')) {
            geometry.setAttribute('color', new THREE.BufferAttribute(new Float32Array(mesh.vertexColors), 3));
        }

        // Only compute tangents if we have both UV coordinates and normals
        if (Object.prototype.hasOwnProperty.call(mesh, 'tangents')) {
            geometry.setAttribute('tangent', new THREE.BufferAttribute(new Float32Array(mesh.tangents), 4));
        } else if (Object.prototype.hasOwnProperty.call(mesh, 'texcoords') && (Object.prototype.hasOwnProperty.call(mesh, 'normals') || geometry.attributes.normal)) {
            // TODO: try MikTSpace tangent algorithm: https://threejs.org/docs/#examples/en/utils/BufferGeometryUtils.computeMikkTSpaceTangents 
            geometry.computeTangents();
        }

        // TODO: vertex opacities(per-vertex alpha)

        // Three.js does not have sideness attribute in Mesh.
        // Store doubleSided param to customData
        if (Object.prototype.hasOwnProperty.call(mesh, 'doubleSided')) {
          geometry.userData['doubleSided'] = mesh.doubleSided;
        } else {
        }

        // Store submesh data for multi-material support (pre-computed in C++)
        if (Object.prototype.hasOwnProperty.call(mesh, 'submeshes') && mesh.submeshes.length > 0) {
          geometry.userData['submeshes'] = mesh.submeshes;
        }

        return geometry;
    }

    static async setupMesh(mesh /* TinyUSDZLoaderNative::RenderMesh */, defaultMtl, usdScene, options) {

        const geometry = this.convertUsdMeshToThreeMesh(mesh);

        const normalMtl = new THREE.MeshNormalMaterial();

        let mtl = null;

        if (options.overrideMaterial) {
            mtl = defaultMtl || normalMtl
        } else {

            // Validate materialId before attempting to get material
            // materialId can be undefined, -1 (no material), or out of range
            const hasMaterial = mesh.materialId !== undefined && mesh.materialId >= 0;

            // Get material data in JSON format to access OpenPBR/MaterialX data
            // Using getMaterialWithFormat ensures we get the full material structure including OpenPBR
            let usdMaterialData = null;
            if (hasMaterial) {
                if (typeof usdScene.getMaterialWithFormat === 'function') {
                    const result = usdScene.getMaterialWithFormat(mesh.materialId, 'json');
                    if (!result.error) {
                        usdMaterialData = JSON.parse(result.data);
                    } else {
                        console.warn(`Failed to get material ${mesh.materialId} with format: ${result.error}`);
                    }
                } else {
                    // Fallback to getMaterial if getMaterialWithFormat is not available
                    usdMaterialData = usdScene.getMaterial(mesh.materialId);
                }
            }


            let pbrMaterial;
            if (usdMaterialData) {
                // Use smart convertMaterial to handle both OpenPBR and UsdPreviewSurface
                pbrMaterial = await this.convertMaterial(usdMaterialData, usdScene, {
                    preferredMaterialType: options.preferredMaterialType || 'auto',
                    envMap: options.envMap || null,
                    envMapIntensity: options.envMapIntensity || 1.0,
                    textureCache: options.textureCache || new Map(),
                    doubleSided: geometry.userData['doubleSided'],
                    textureLoadingManager: options.textureLoadingManager || null
                });

                // Store material metadata for UI and reloading
                pbrMaterial.userData.rawData = usdMaterialData;
                pbrMaterial.userData.typeInfo = this.getMaterialType(usdMaterialData);
                pbrMaterial.userData.typeString = this.getMaterialTypeString(usdMaterialData);
            } else {
                // No valid material - create default material
                pbrMaterial = defaultMtl || new THREE.MeshPhysicalMaterial({
                    color: 0x888888,
                    roughness: 0.5,
                    metalness: 0.0
                });
            }

            // Setting envmap is required for PBR materials to work correctly(e.g. clearcoat)
            pbrMaterial.envMap = options.envMap || null;
            pbrMaterial.envMapIntensity = options.envMapIntensity || 1.0;


            // Sideness is determined by the mesh's USD doubleSided attribute
            if (Object.prototype.hasOwnProperty.call(geometry.userData, 'doubleSided')) {
              if (geometry.userData.doubleSided) {
                pbrMaterial.side = THREE.DoubleSide;
              } else {
                pbrMaterial.side = THREE.FrontSide;
              }
            } else {
              // No doubleSided attribute in USD - default to FrontSide
              pbrMaterial.side = THREE.FrontSide;
            }

            mtl = pbrMaterial || defaultMtl || normalMtl;
        }

        // Handle GeomSubsets (per-face materials)
        if (geometry.userData['submeshes'] && geometry.userData['submeshes'].length > 0) {
            const submeshes = geometry.userData['submeshes'];

            // Build materials array indexed by materialId
            const materials = [];
            const materialIdToIndex = new Map();

            // First pass: collect unique material IDs
            for (const submesh of submeshes) {
                const matId = submesh.materialId;
                if (!materialIdToIndex.has(matId)) {
                    materialIdToIndex.set(matId, materials.length);
                    materials.push(null); // Placeholder
                }
            }

            // Second pass: load materials
            for (const [matId, matIndex] of materialIdToIndex.entries()) {
                if (matId >= 0) {
                    const materialData = usdScene.getMaterialWithFormat ?
                        JSON.parse(usdScene.getMaterialWithFormat(matId, 'json').data) :
                        usdScene.getMaterial(matId);

                    const material = await this.convertMaterial(materialData, usdScene, {
                        preferredMaterialType: options.preferredMaterialType || 'auto',
                        envMap: options.envMap || null,
                        envMapIntensity: options.envMapIntensity || 1.0,
                        textureCache: options.textureCache || new Map(),
                        doubleSided: geometry.userData['doubleSided'],
                        textureLoadingManager: options.textureLoadingManager || null
                    });

                    material.envMap = options.envMap || null;
                    material.envMapIntensity = options.envMapIntensity || 1.0;
                    material.side = geometry.userData['doubleSided'] ? THREE.DoubleSide : THREE.FrontSide;

                    // Store material metadata for UI and reloading
                    material.userData.rawData = materialData;
                    material.userData.typeInfo = this.getMaterialType(materialData);
                    material.userData.typeString = this.getMaterialTypeString(materialData);

                    materials[matIndex] = material;
                } else {
                    materials[matIndex] = mtl; // Use default material
                }
            }

            // Third pass: add geometry groups using pre-computed submesh data (from C++)
            for (const submesh of submeshes) {
                const matIndex = materialIdToIndex.get(submesh.materialId);
                geometry.addGroup(submesh.start, submesh.count, matIndex);
            }


            // Create mesh with multi-material array
            const threeMesh = new THREE.Mesh(geometry, materials);
            return threeMesh;
        } else {
            // Single material mesh
            const threeMesh = new THREE.Mesh(geometry, mtl);
            return threeMesh;
        }
    }


    // arr = float array with 16 elements(row major order)
    static toMatrix4(a) {
        const m = new THREE.Matrix4();

        //m.set(a[0], a[1], a[2], a[3],
        //    a[4], a[5], a[6], a[7],
        //    a[8], a[9], a[10], a[11],
        //    a[12], a[13], a[14], a[15]);
        m.set(a[0], a[4], a[8], a[12],
            a[1], a[5], a[9], a[13],
            a[2], a[6], a[10], a[14],
            a[3], a[7], a[11], a[15]);

        return m;
    }

    /**
     * Count total nodes in USD hierarchy (for progress estimation)
     * @private
     */
    static _countNodes(usdNode) {
        let count = 1;
        if (usdNode.children) {
            for (const child of usdNode.children) {
                count += this._countNodes(child);
            }
        }
        return count;
    }

    /**
     * Count total meshes in USD hierarchy
     * @private
     */
    static _countMeshes(usdNode) {
        let count = usdNode.nodeType === 'mesh' ? 1 : 0;
        if (usdNode.children) {
            for (const child of usdNode.children) {
                count += this._countMeshes(child);
            }
        }
        return count;
    }

    // Supported options:
    // - 'overrideMaterial' : Override usd material with defaultMtl.
    // - 'onProgress' : Progress callback (info) => void
    //     info: { stage: 'building'|'textures', percentage: number, message: string }
    // - '_progressState' : Internal state for progress tracking (auto-created)

    /**
     * Build a Three.js scene graph from a USD node hierarchy
     * Includes browser yields to allow UI updates during scene building.
     *
     * @param {Object} usdNode - USD node from TinyUSDZLoader
     * @param {THREE.Material} defaultMtl - Default material to use
     * @param {Object} usdScene - USD scene object (TinyUSDZLoaderNative)
     * @param {Object} options - Build options
     * @param {Function} options.onProgress - Progress callback ({stage, percentage, message}) => void
     * @returns {Promise<THREE.Object3D>} Three.js node
     */
    static async buildThreeNode(usdNode /* TinyUSDZLoader.Node */, defaultMtl = null, usdScene /* TinyUSDZLoader.Scene */ = null, options = {})
   /* => THREE.Object3D */ {

        // Initialize progress tracking on first call (root node)
        if (!options._progressState) {
            const totalNodes = this._countNodes(usdNode);
            const totalMeshes = this._countMeshes(usdNode);
            options._progressState = {
                processedNodes: 0,
                processedMeshes: 0,
                totalNodes: totalNodes,
                totalMeshes: totalMeshes,
                lastYieldTime: 0
            };
            // Report initial progress
            if (options.onProgress) {
                options.onProgress({
                    stage: 'building',
                    percentage: 0,
                    message: `Building scene (0/${totalMeshes} meshes)...`
                });
            }
            // Initial yield to show progress UI
            await this.yieldToUI();
        }

        var node = new THREE.Group();

        if (usdNode.nodeType == 'xform') {

            // intermediate xform node
            // Apply the USD local transform matrix to the Three.js node
            const matrix = this.toMatrix4(usdNode.localMatrix);

            // Decompose the matrix into position, rotation, and scale
            // This is necessary for Three.js to properly handle the transform
            node.applyMatrix4(matrix);

        } else if (usdNode.nodeType == 'skelroot') {

            // UsdSkelRoot: encapsulation prim for skinned subtree.
            // Its world transform (skelLocalToWorld) positions skinned results in world space.
            // Treated as a group node with transform in Three.js.
            if (usdNode.localMatrix) {
                const matrix = this.toMatrix4(usdNode.localMatrix);
                node.applyMatrix4(matrix);
            }

        } else if (usdNode.nodeType == 'skeleton') {

            // UsdSkeleton: joint hierarchy prim.
            // Its transform contributes to skelLocalToWorld.
            // Treated as a group node with transform in Three.js.
            if (usdNode.localMatrix) {
                const matrix = this.toMatrix4(usdNode.localMatrix);
                node.applyMatrix4(matrix);
            }

        } else if (usdNode.nodeType == 'mesh') {

            // contentId is the mesh ID in the USD scene.
            const mesh = usdScene.getMesh(usdNode.contentId);

            // Update progress before building mesh
            if (options._progressState && options.onProgress) {
                const { processedMeshes, totalMeshes } = options._progressState;
                const percentage = (processedMeshes / Math.max(1, totalMeshes)) * 100;
                options.onProgress({
                    stage: 'building',
                    percentage: percentage,
                    message: `Building mesh ${processedMeshes + 1}/${totalMeshes}: ${usdNode.primName}`
                });
            }

            // Yield to browser before heavy mesh setup
            await this.maybeYieldToUI(options._progressState);

            const threeMesh = await this.setupMesh(mesh, defaultMtl, usdScene, options);
            node = threeMesh;

            // Increment mesh counter after building
            if (options._progressState) {
                options._progressState.processedMeshes++;
            }

            // Apply transform to mesh nodes as well
            // Mesh nodes can also have transforms in USD
            if (usdNode.localMatrix) {
                const matrix = this.toMatrix4(usdNode.localMatrix);
                node.applyMatrix4(matrix);
            }

        } else {
            // Unknown node type - still try to apply transform if available
            if (usdNode.localMatrix) {
                const matrix = this.toMatrix4(usdNode.localMatrix);
                node.applyMatrix4(matrix);
            }
        }

        node.name = usdNode.primName;
        node.userData['primMeta.displayName'] = usdNode.displayName;
        node.userData['primMeta.absPath'] = usdNode.absPath;

        // Update progress after processing this node
        if (options._progressState) {
            options._progressState.processedNodes++;

            // Yield periodically to allow UI updates
            await this.maybeYieldToUI(options._progressState);
        }

        if (Object.prototype.hasOwnProperty.call(usdNode, 'children')) {

            // traverse children
            for (const child of usdNode.children) {
                const childNode = await this.buildThreeNode(child, defaultMtl, usdScene, options);
                node.add(childNode);
            }
        }

        return node;
    }

    // ========================================================================
    // DomeLight / Environment Map Utilities
    // ========================================================================

    static MIN_PMREM_SIZE = 64;

    /**
     * Decode half-float (float16) to float32
     */
    static decodeHalfFloat(h) {
        const s = (h & 0x8000) >> 15;
        const e = (h & 0x7C00) >> 10;
        const f = h & 0x03FF;
        if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
        if (e === 0x1F) return f ? NaN : (s ? -Infinity : Infinity);
        return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
    }

    /**
     * Convert RGB [0, 1] to hex color string
     */
    static rgbToHex(r, g, b) {
        const toHex = (c) => {
            const clamped = Math.max(0, Math.min(1, c));
            return Math.round(clamped * 255).toString(16).padStart(2, '0');
        };
        return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
    }

    /**
     * Check if light type is a DomeLight
     */
    static isDomeLight(type) {
        return type === 'dome' || type === 'Dome' || type === 'DomeLight';
    }

    /**
     * Calculate DomeLight intensity from USD light properties
     */
    static calculateDomeLightIntensity(light) {
        let intensity = light.intensity !== undefined ? light.intensity : 1.0;
        const exposure = (light.exposure !== undefined && light.exposure !== 0) ? light.exposure : 1.0;
        return intensity * Math.pow(2, exposure);
    }

    /**
     * Create a fallback environment texture (solid white)
     */
    static createFallbackEnvTexture() {
        const canvas = document.createElement('canvas');
        canvas.width = this.MIN_PMREM_SIZE;
        canvas.height = this.MIN_PMREM_SIZE;
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, this.MIN_PMREM_SIZE, this.MIN_PMREM_SIZE);

        const texture = new THREE.CanvasTexture(canvas);
        texture.mapping = THREE.EquirectangularReflectionMapping;
        return texture;
    }

    /**
     * Create a solid color texture
     */
    static createSolidColorTexture(color, size) {
        const toSRGB = (v) => Math.pow(Math.max(0, Math.min(1, v)), 1 / 2.2);
        const r = Math.round(toSRGB(color.r) * 255);
        const g = Math.round(toSRGB(color.g) * 255);
        const b = Math.round(toSRGB(color.b) * 255);

        const canvas = document.createElement('canvas');
        canvas.width = size;
        canvas.height = size;
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
        ctx.fillRect(0, 0, size, size);

        const texture = new THREE.CanvasTexture(canvas);
        texture.mapping = THREE.EquirectangularReflectionMapping;
        return texture;
    }

    /**
     * Create a constant color environment map
     */
    static createConstantColorEnvironment(color, colorspace, pmremGenerator) {
        const canvas = document.createElement('canvas');
        canvas.width = 256;
        canvas.height = 256;
        const ctx = canvas.getContext('2d');

        let fillColor = color;
        if (colorspace === 'sRGB' && color.startsWith('#')) {
            // Convert sRGB hex to linear for proper rendering
            const hex = color.replace('#', '');
            const r = parseInt(hex.substring(0, 2), 16) / 255;
            const g = parseInt(hex.substring(2, 4), 16) / 255;
            const b = parseInt(hex.substring(4, 6), 16) / 255;
            const sRGBToLinear = (c) => c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
            fillColor = this.rgbToHex(sRGBToLinear(r), sRGBToLinear(g), sRGBToLinear(b));
        }

        ctx.fillStyle = fillColor;
        ctx.fillRect(0, 0, 256, 256);

        const texture = new THREE.CanvasTexture(canvas);
        texture.mapping = THREE.EquirectangularReflectionMapping;
        texture.colorSpace = THREE.LinearSRGBColorSpace;

        return pmremGenerator.fromEquirectangular(texture).texture;
    }

    /**
     * Extract average color from texture data
     */
    static extractAverageColor(texture, width, height) {
        const texData = texture.image?.data;
        if (!texData || width === 0 || height === 0) {
            return { r: 1.0, g: 1.0, b: 1.0 };
        }

        const isHalfFloat = texData instanceof Uint16Array;
        const pixelCount = width * height;
        let sumR = 0, sumG = 0, sumB = 0;

        for (let i = 0; i < pixelCount; i++) {
            if (isHalfFloat) {
                sumR += this.decodeHalfFloat(texData[i * 4 + 0]);
                sumG += this.decodeHalfFloat(texData[i * 4 + 1]);
                sumB += this.decodeHalfFloat(texData[i * 4 + 2]);
            } else {
                sumR += texData[i * 4 + 0];
                sumG += texData[i * 4 + 1];
                sumB += texData[i * 4 + 2];
            }
        }

        return {
            r: sumR / pixelCount,
            g: sumG / pixelCount,
            b: sumB / pixelCount
        };
    }

    /**
     * Ensure texture meets minimum size for PMREM processing
     */
    static ensureMinimumTextureSize(texture) {
        const origWidth = texture.image?.width || 0;
        const origHeight = texture.image?.height || 0;

        if (origWidth >= this.MIN_PMREM_SIZE && origHeight >= this.MIN_PMREM_SIZE) {
            return texture;
        }

        const avgColor = this.extractAverageColor(texture, origWidth, origHeight);
        texture.dispose();

        return this.createSolidColorTexture(avgColor, this.MIN_PMREM_SIZE);
    }

    /**
     * Create a float texture from decoded data
     */
    static createFloatTexture(data, width, height, channels) {
        const floatData = data instanceof Float32Array ? data : new Float32Array(data.buffer);

        let rgbaData;
        if (channels === 4) {
            rgbaData = floatData;
        } else if (channels === 3) {
            rgbaData = new Float32Array(width * height * 4);
            for (let i = 0; i < width * height; i++) {
                rgbaData[i * 4 + 0] = floatData[i * 3 + 0];
                rgbaData[i * 4 + 1] = floatData[i * 3 + 1];
                rgbaData[i * 4 + 2] = floatData[i * 3 + 2];
                rgbaData[i * 4 + 3] = 1.0;
            }
        } else {
            return null;
        }

        return new THREE.DataTexture(rgbaData, width, height, THREE.RGBAFormat, THREE.FloatType);
    }

    /**
     * Create a canvas texture from decoded image data
     */
    static createCanvasTextureFromData(data, width, height, channels) {
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');
        const imageData = ctx.createImageData(width, height);

        for (let i = 0; i < width * height; i++) {
            const srcIdx = i * channels;
            const dstIdx = i * 4;

            if (channels === 1) {
                imageData.data[dstIdx + 0] = data[srcIdx];
                imageData.data[dstIdx + 1] = data[srcIdx];
                imageData.data[dstIdx + 2] = data[srcIdx];
                imageData.data[dstIdx + 3] = 255;
            } else if (channels === 2) {
                imageData.data[dstIdx + 0] = data[srcIdx];
                imageData.data[dstIdx + 1] = data[srcIdx];
                imageData.data[dstIdx + 2] = data[srcIdx];
                imageData.data[dstIdx + 3] = data[srcIdx + 1];
            } else if (channels === 3) {
                imageData.data[dstIdx + 0] = data[srcIdx + 0];
                imageData.data[dstIdx + 1] = data[srcIdx + 1];
                imageData.data[dstIdx + 2] = data[srcIdx + 2];
                imageData.data[dstIdx + 3] = 255;
            } else if (channels === 4) {
                imageData.data[dstIdx + 0] = data[srcIdx + 0];
                imageData.data[dstIdx + 1] = data[srcIdx + 1];
                imageData.data[dstIdx + 2] = data[srcIdx + 2];
                imageData.data[dstIdx + 3] = data[srcIdx + 3];
            }
        }

        ctx.putImageData(imageData, 0, 0);
        return new THREE.CanvasTexture(canvas);
    }

    /**
     * Create texture from decoded USD image data
     */
    static async createTextureFromDecodedData(data, width, height, channels, colorSpace) {
        try {
            if (!data || !width || !height) return null;

            const isFloat = data instanceof Float32Array || (data.buffer && data.BYTES_PER_ELEMENT === 4);
            let texture;

            if (isFloat) {
                texture = this.createFloatTexture(data, width, height, channels);
            } else {
                texture = this.createCanvasTextureFromData(data, width, height, channels);
            }

            if (!texture) return null;

            texture.mapping = THREE.EquirectangularReflectionMapping;
            texture.colorSpace = (colorSpace === 'sRGB' || colorSpace === 'sRGB_Texture')
                ? THREE.SRGBColorSpace
                : THREE.LinearSRGBColorSpace;
            texture.needsUpdate = true;

            return texture;
        } catch (error) {
            console.error('Error creating texture from decoded data:', error);
            return null;
        }
    }

    /**
     * Decode EXR from buffer with Three.js primary + TinyUSDZ fallback
     * @param {ArrayBuffer|Uint8Array} buffer - EXR data
     * @param {string} [outputFormat='float16'] - Output format
     * @returns {THREE.DataTexture|null}
     */
    static decodeEXRFromBuffer(buffer, outputFormat = 'float16') {
        const result = decodeEXRWithFallback(buffer, TinyUSDZLoaderUtils._tinyusdz, {
            outputFormat,
            preferThreeJS: true,
            verbose: false,
        });

        if (!result.success) {
            console.warn('EXR decode failed:', result.error);
            return null;
        }

        // Create Three.js DataTexture from decoded data
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
        texture.needsUpdate = true;

        return texture;
    }

    /**
     * Decode environment map from buffer (supports EXR, HDR, and standard image formats)
     * Uses Three.js EXRLoader with TinyUSDZ fallback for EXR files
     */
    static async decodeEnvmapFromBuffer(buffer, uri) {
        try {
            const lowerUri = uri.toLowerCase();

            let texture = null;

            if (lowerUri.endsWith('.exr')) {
                // Use EXR decoder with TinyUSDZ fallback
                texture = this.decodeEXRFromBuffer(buffer, 'float16');

                if (!texture) {
                    // Last resort: try Three.js EXRLoader with blob URL
                    const blob = new Blob([buffer], { type: 'image/x-exr' });
                    const objectUrl = URL.createObjectURL(blob);
                    try {
                        texture = await new EXRLoader().loadAsync(objectUrl);
                    } finally {
                        URL.revokeObjectURL(objectUrl);
                    }
                }
            } else if (lowerUri.endsWith('.hdr')) {
                // HDR uses TinyUSDZ decoder (faster than Three.js)
                const tinyusdz = TinyUSDZLoaderUtils._tinyusdz;
                if (tinyusdz && typeof tinyusdz.decodeHDR === 'function') {
                    const uint8Array = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
                    const result = tinyusdz.decodeHDR(uint8Array, 'float16');
                    if (result.success) {
                        texture = new THREE.DataTexture(
                            result.data,
                            result.width,
                            result.height,
                            THREE.RGBAFormat,
                            THREE.HalfFloatType
                        );
                        texture.minFilter = THREE.LinearFilter;
                        texture.magFilter = THREE.LinearFilter;
                        texture.generateMipmaps = false;
                        texture.needsUpdate = true;
                    }
                }

                if (!texture) {
                    // Fallback to Three.js HDRLoader
                    const blob = new Blob([buffer], { type: 'image/vnd.radiance' });
                    const objectUrl = URL.createObjectURL(blob);
                    try {
                        texture = await new HDRLoader().loadAsync(objectUrl);
                    } finally {
                        URL.revokeObjectURL(objectUrl);
                    }
                }
            } else {
                // Standard image formats
                const mimeType = this.getMimeTypeFromExtension(this.getFileExtension(uri)) || 'application/octet-stream';
                const blob = new Blob([buffer], { type: mimeType });
                const objectUrl = URL.createObjectURL(blob);
                try {
                    texture = await new THREE.TextureLoader().loadAsync(objectUrl);
                } finally {
                    URL.revokeObjectURL(objectUrl);
                }
            }

            if (texture) {
                texture.mapping = THREE.EquirectangularReflectionMapping;
            }

            return texture;
        } catch (error) {
            console.error('Error decoding envmap from buffer:', error);
            return null;
        }
    }

    /**
     * Load DomeLight environment map from USD texture ID
     * @param {Object} light - USD light data
     * @param {Object} usdLoader - USD loader instance
     * @param {number} envmapTextureId - Texture ID
     * @param {string} textureFile - Optional texture file path
     * @param {THREE.PMREMGenerator} pmremGenerator - PMREM generator
     * @returns {Object|null} - { texture, intensity } or null
     */
    static async loadDomeLightFromTextureId(light, usdLoader, envmapTextureId, textureFile, pmremGenerator) {
        try {
            const imageData = usdLoader.getImage(envmapTextureId);
            if (!imageData || !imageData.data || imageData.data.length === 0) {
                console.warn(`DomeLight: No image data found for texture ID ${envmapTextureId}`);
                return null;
            }

            let texture = imageData.decoded
                ? await this.createTextureFromDecodedData(imageData.data, imageData.width, imageData.height, imageData.channels, imageData.colorSpace)
                : await this.decodeEnvmapFromBuffer(imageData.data, imageData.uri || textureFile || '');

            if (!texture) {
                texture = this.createFallbackEnvTexture();
            }

            // Verify texture has valid image data before PMREM processing
            if (!texture || !texture.image) {
                console.warn(`DomeLight: Failed to create valid texture from image ID ${envmapTextureId}`);
                return null;
            }

            texture = this.ensureMinimumTextureSize(texture);

            const pmremResult = pmremGenerator.fromEquirectangular(texture);
            const envMap = pmremResult.texture;
            texture.dispose();

            const intensity = this.calculateDomeLightIntensity(light);

            return {
                texture: envMap,
                intensity,
                name: light.name,
                textureFile,
                envmapTextureId,
                color: light.color,
                exposure: light.exposure
            };
        } catch (error) {
            console.warn(`DomeLight: Failed to load envmap from image index ${envmapTextureId}:`, error.message);
            return null;
        }
    }

    /**
     * Load DomeLight environment map directly from file
     * Uses TinyUSDZ for HDR (faster) and Three.js + TinyUSDZ fallback for EXR
     * @param {Object} light - USD light data
     * @param {string} textureFile - Texture file path
     * @param {THREE.PMREMGenerator} pmremGenerator - PMREM generator
     * @returns {Object|null} - { texture, intensity } or null
     */
    static async loadDomeLightFromFile(light, textureFile, pmremGenerator) {
        try {
            let texture = null;
            const lowerFile = textureFile.toLowerCase();

            // Fetch the file data
            let response;
            try {
                response = await fetch(textureFile);
                if (!response.ok) {
                    console.warn(`DomeLight: Texture file not accessible '${textureFile}' (HTTP ${response.status})`);
                    return null;
                }
                // Check content type - reject HTML responses (likely 404 pages that return 200)
                const contentType = response.headers.get('content-type') || '';
                if (contentType.includes('text/html')) {
                    console.warn(`DomeLight: Invalid content type for '${textureFile}' (got HTML, expected image)`);
                    return null;
                }
            } catch (fetchError) {
                console.warn(`DomeLight: Cannot access texture file '${textureFile}' - ${fetchError.message}`);
                return null;
            }

            const buffer = await response.arrayBuffer();

            if (lowerFile.endsWith('.exr')) {
                // Use EXR decoder with TinyUSDZ fallback
                texture = this.decodeEXRFromBuffer(buffer, 'float16');

                if (!texture) {
                    // Fallback: try Three.js EXRLoader with blob URL
                    try {
                        const blob = new Blob([buffer], { type: 'image/x-exr' });
                        const objectUrl = URL.createObjectURL(blob);
                        try {
                            texture = await new EXRLoader().loadAsync(objectUrl);
                        } finally {
                            URL.revokeObjectURL(objectUrl);
                        }
                    } catch (exrError) {
                        console.warn(`DomeLight: EXR load failed for '${textureFile}' - ${exrError.message}`);
                        return null;
                    }
                }
            } else if (lowerFile.endsWith('.hdr')) {
                // Use TinyUSDZ HDR decoder (faster than Three.js)
                const tinyusdz = TinyUSDZLoaderUtils._tinyusdz;
                if (tinyusdz && typeof tinyusdz.decodeHDR === 'function') {
                    const uint8Array = new Uint8Array(buffer);
                    const result = tinyusdz.decodeHDR(uint8Array, 'float16');
                    if (result.success) {
                        texture = new THREE.DataTexture(
                            result.data,
                            result.width,
                            result.height,
                            THREE.RGBAFormat,
                            THREE.HalfFloatType
                        );
                        texture.minFilter = THREE.LinearFilter;
                        texture.magFilter = THREE.LinearFilter;
                        texture.generateMipmaps = false;
                        texture.needsUpdate = true;
                    }
                }

                if (!texture) {
                    // Fallback to Three.js HDRLoader
                    try {
                        const blob = new Blob([buffer], { type: 'image/vnd.radiance' });
                        const objectUrl = URL.createObjectURL(blob);
                        try {
                            texture = await new HDRLoader().loadAsync(objectUrl);
                        } finally {
                            URL.revokeObjectURL(objectUrl);
                        }
                    } catch (hdrError) {
                        console.warn(`DomeLight: HDR load failed for '${textureFile}' - ${hdrError.message}`);
                        return null;
                    }
                }
            } else {
                console.warn(`DomeLight: Unsupported texture format for '${textureFile}'`);
                return null;
            }

            // Check if texture was loaded and has valid data
            if (!texture) {
                console.warn(`DomeLight: Failed to decode texture for '${textureFile}'`);
                return null;
            }

            texture.mapping = THREE.EquirectangularReflectionMapping;
            const envMap = pmremGenerator.fromEquirectangular(texture).texture;
            texture.dispose();

            const intensity = this.calculateDomeLightIntensity(light);

            return {
                texture: envMap,
                intensity,
                name: light.name,
                textureFile,
                color: light.color,
                exposure: light.exposure
            };
        } catch (error) {
            console.warn(`DomeLight: Unexpected error loading '${textureFile}' - ${error.message}`);
            return null;
        }
    }

    /**
     * Load DomeLight as constant color environment
     * @param {Object} light - USD light data
     * @param {THREE.PMREMGenerator} pmremGenerator - PMREM generator
     * @returns {Object|null} - { texture, intensity, colorHex } or null
     */
    static loadDomeLightAsConstantColor(light, pmremGenerator) {
        if (!light.color || light.color.length < 3) return null;

        const colorHex = this.rgbToHex(light.color[0], light.color[1], light.color[2]);
        const envMap = this.createConstantColorEnvironment(colorHex, 'linear', pmremGenerator);
        const intensity = this.calculateDomeLightIntensity(light);

        return {
            texture: envMap,
            intensity,
            colorHex,
            name: light.name,
            color: light.color,
            exposure: light.exposure
        };
    }

    /**
     * Process a single DomeLight from USD
     * @param {Object} light - USD light data
     * @param {Object} usdLoader - USD loader instance
     * @param {THREE.PMREMGenerator} pmremGenerator - PMREM generator
     * @returns {Object|null} - DomeLight result or null
     */
    static async processDomeLight(light, usdLoader, pmremGenerator) {
        const envmapTextureId = light.envmapTextureId;
        const textureFile = light.textureFile || light.texture_file;

        // Try loading from texture ID first
        if (envmapTextureId !== undefined && envmapTextureId >= 0) {
            const result = await this.loadDomeLightFromTextureId(light, usdLoader, envmapTextureId, textureFile, pmremGenerator);
            if (result) return result;
        }

        // Fallback: direct file load
        if (textureFile) {
            const result = await this.loadDomeLightFromFile(light, textureFile, pmremGenerator);
            if (result) return result;
        }

        // Final fallback: constant color
        if (!textureFile && (envmapTextureId === undefined || envmapTextureId < 0)) {
            return this.loadDomeLightAsConstantColor(light, pmremGenerator);
        }

        return null;
    }

    /**
     * Load DomeLight from USD scene
     * Iterates through all lights and returns the first DomeLight found
     *
     * @param {Object} usdLoader - USD loader instance with numLights() and getLight() methods
     * @param {THREE.PMREMGenerator} pmremGenerator - PMREM generator for environment map processing
     * @returns {Object|null} - DomeLight data { texture, intensity, name, ... } or null
     *
     * Usage:
     *   const domeLightData = await TinyUSDZLoaderUtils.loadDomeLightFromUSD(usdLoader, pmremGenerator);
     *   if (domeLightData) {
     *       scene.environment = domeLightData.texture;
     *       materials.forEach(m => m.envMapIntensity = domeLightData.intensity);
     *   }
     */
    static async loadDomeLightFromUSD(usdLoader, pmremGenerator) {
        try {
            const numLights = usdLoader.numLights ? usdLoader.numLights() : 0;
            if (numLights === 0) return null;

            for (let i = 0; i < numLights; i++) {
                const light = usdLoader.getLight(i);
                if (light.error) continue;

                if (!this.isDomeLight(light.type)) continue;

                const result = await this.processDomeLight(light, usdLoader, pmremGenerator);
                if (result) return result;
            }

            return null;
        } catch (error) {
            console.warn('Error loading DomeLight from USD:', error);
            return null;
        }
    }

}

export { TinyUSDZLoaderUtils };
