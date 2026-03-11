/**
 * TinyUSDZ Web Worker
 *
 * Runs USD parsing in a separate thread, keeping the main thread responsive.
 * Communicates with the main thread via postMessage for progress updates.
 */

import createTinyUSDZ from './tinyusdz.js';

let tinyusdz = null;
let loader = null;

/**
 * Initialize the WASM module
 */
async function init() {
    if (tinyusdz) return true;

    try {
        tinyusdz = await createTinyUSDZ();

        // Set up Tydra progress callback on Module
        tinyusdz.onTydraProgress = (info) => {
            self.postMessage({
                type: 'tydra_progress',
                meshCurrent: info.meshCurrent,
                meshTotal: info.meshTotal,
                stage: info.stage,
                meshName: info.meshName,
                progress: info.progress
            });
        };

        tinyusdz.onTydraComplete = (info) => {
            self.postMessage({
                type: 'tydra_complete',
                meshCount: info.meshCount,
                materialCount: info.materialCount,
                textureCount: info.textureCount
            });
        };

        return true;
    } catch (e) {
        self.postMessage({ type: 'error', error: `Failed to init WASM: ${e.message}` });
        return false;
    }
}

/**
 * Load USD from binary data
 */
async function loadFromBinary(binary, filename, options = {}) {
    if (!tinyusdz) {
        const ok = await init();
        if (!ok) return;
    }

    try {
        loader = new tinyusdz.TinyUSDZLoaderNative();

        // Apply options
        if (options.maxMemoryLimitMB) {
            loader.setMaxMemoryLimitMB(options.maxMemoryLimitMB);
        }

        self.postMessage({ type: 'progress', phase: 'parsing', progress: 0.1 });

        // Load the USD file
        const success = loader.loadFromBinary(binary, filename);

        if (!success) {
            self.postMessage({ type: 'error', error: 'Failed to parse USD file' });
            return;
        }

        self.postMessage({ type: 'progress', phase: 'extracting', progress: 0.8 });

        // Extract scene data to send back to main thread
        const sceneData = extractSceneData(loader);

        self.postMessage({ type: 'progress', phase: 'complete', progress: 1.0 });

        // Send the extracted data back
        // Use transferable arrays for efficiency
        const transferables = collectTransferables(sceneData);

        self.postMessage({
            type: 'complete',
            data: sceneData
        }, transferables);

    } catch (e) {
        self.postMessage({ type: 'error', error: e.message });
    }
}

/**
 * Extract scene data from the loader to send to main thread
 */
function extractSceneData(loader) {
    const numMeshes = loader.numMeshes();
    const numMaterials = loader.numMaterials();
    const numTextures = loader.numTextures();
    const numLights = loader.numLights();
    const numImages = typeof loader.numImages === 'function' ? loader.numImages() : 0;

    const meshes = [];
    const materials = [];
    const textures = [];
    const images = [];
    const lights = [];

    // Extract meshes
    for (let i = 0; i < numMeshes; i++) {
        const mesh = loader.getMesh(i);
        if (mesh) {
            meshes.push(extractMeshData(mesh));
        }

        // Report progress during extraction
        if (i % 10 === 0) {
            self.postMessage({
                type: 'progress',
                phase: 'extracting_meshes',
                progress: 0.8 + (i / numMeshes) * 0.1,
                message: `Extracting mesh ${i + 1}/${numMeshes}`
            });
        }
    }

    // Extract materials using getMaterialWithFormat for full OpenPBR data
    for (let i = 0; i < numMaterials; i++) {
        // Use getMaterialWithFormat to get full material data including OpenPBR
        if (typeof loader.getMaterialWithFormat === 'function') {
            const result = loader.getMaterialWithFormat(i, 'json');
            if (result && !result.error && result.data) {
                try {
                    const parsedMaterial = JSON.parse(result.data);
                    materials.push(parsedMaterial);
                } catch (e) {
                    console.warn(`Failed to parse material ${i}:`, e);
                    // Fallback to getMaterial
                    const material = loader.getMaterial(i);
                    if (material) {
                        materials.push(deepClone(material));
                    }
                }
            }
        } else {
            // Fallback to getMaterial if getMaterialWithFormat not available
            const material = loader.getMaterial(i);
            if (material) {
                materials.push(deepClone(material));
            }
        }
    }

    // Extract textures (metadata with textureImageId reference)
    for (let i = 0; i < numTextures; i++) {
        const texture = loader.getTexture(i);
        if (texture) {
            // Deep clone texture metadata (not the image data)
            textures.push(deepClone(texture));
        }
    }

    // Extract images (actual pixel data)
    for (let i = 0; i < numImages; i++) {
        const image = loader.getImage(i);
        if (image) {
            images.push(extractImageData(image));
        }
    }

    // Extract lights (deep clone to avoid WASM references)
    for (let i = 0; i < numLights; i++) {
        const light = loader.getLight(i);
        if (light) {
            lights.push(deepClone(light));
        }
    }

    // Get root node (deep clone the entire tree)
    const rawRootNode = loader.getDefaultRootNode();
    const rootNode = rawRootNode ? deepCloneNode(rawRootNode) : null;

    // Get metadata (deep clone to avoid WASM references)
    const upAxis = loader.getUpAxis ? loader.getUpAxis() : 'Y';
    const rawMetadata = loader.getSceneMetadata ? loader.getSceneMetadata() : {};
    const metadata = deepClone(rawMetadata);

    return {
        meshes,
        materials,
        textures,
        images,
        lights,
        rootNode,
        upAxis,
        metadata,
        numMeshes,
        numMaterials,
        numTextures,
        numImages,
        numLights
    };
}

/**
 * Extract mesh data, converting typed arrays to transferable format
 */
function extractMeshData(mesh) {
    // Deep clone the mesh to handle any WASM references
    const data = deepClone(mesh);

    // Ensure typed arrays are proper copies (deepClone handles this, but be explicit)
    if (mesh.points && mesh.points.buffer) {
        data.points = new Float32Array(mesh.points);
    }
    if (mesh.normals && mesh.normals.buffer) {
        data.normals = new Float32Array(mesh.normals);
    }
    if (mesh.uvs && mesh.uvs.buffer) {
        data.uvs = new Float32Array(mesh.uvs);
    }
    if (mesh.indices && mesh.indices.buffer) {
        data.indices = new Uint32Array(mesh.indices);
    }

    return data;
}

/**
 * Extract texture metadata (references image via textureImageId)
 */
function extractTextureData(texture) {
    // Deep clone to handle any WASM references
    return deepClone(texture);
}

/**
 * Extract image data (actual pixel data)
 */
function extractImageData(image) {
    // Deep clone base properties
    const data = deepClone(image);

    // Ensure image pixel data is a proper copy
    if (image.data && image.data.buffer) {
        data.data = new Uint8Array(image.data);
    }

    return data;
}

/**
 * Deep clone an object, handling typed arrays and avoiding WASM references.
 * This creates a plain JavaScript object that can be transferred via postMessage.
 */
function deepClone(obj, seen = new WeakMap()) {
    // Handle primitives and null
    if (obj === null || typeof obj !== 'object') {
        return obj;
    }

    // Handle circular references
    if (seen.has(obj)) {
        return seen.get(obj);
    }

    // Handle typed arrays
    if (ArrayBuffer.isView(obj)) {
        if (obj instanceof Float32Array) return new Float32Array(obj);
        if (obj instanceof Float64Array) return new Float64Array(obj);
        if (obj instanceof Int8Array) return new Int8Array(obj);
        if (obj instanceof Int16Array) return new Int16Array(obj);
        if (obj instanceof Int32Array) return new Int32Array(obj);
        if (obj instanceof Uint8Array) return new Uint8Array(obj);
        if (obj instanceof Uint16Array) return new Uint16Array(obj);
        if (obj instanceof Uint32Array) return new Uint32Array(obj);
        if (obj instanceof Uint8ClampedArray) return new Uint8ClampedArray(obj);
        // Generic fallback
        return new obj.constructor(obj);
    }

    // Handle ArrayBuffer
    if (obj instanceof ArrayBuffer) {
        return obj.slice(0);
    }

    // Handle Date
    if (obj instanceof Date) {
        return new Date(obj.getTime());
    }

    // Handle Array
    if (Array.isArray(obj)) {
        const cloned = [];
        seen.set(obj, cloned);
        for (let i = 0; i < obj.length; i++) {
            cloned[i] = deepClone(obj[i], seen);
        }
        return cloned;
    }

    // Skip functions and WASM-specific objects
    if (typeof obj === 'function') {
        return undefined;
    }

    // Check if it's an Emscripten/WASM object (has $$, ptr, or delete method)
    if (obj.$$ !== undefined || obj.ptr !== undefined || typeof obj.delete === 'function') {
        // This is likely a WASM object - try to extract plain properties
        const cloned = {};
        seen.set(obj, cloned);
        for (const key of Object.keys(obj)) {
            // Skip internal WASM properties
            if (key.startsWith('$') || key === 'ptr' || key === '__proto__') continue;
            const val = obj[key];
            if (typeof val !== 'function') {
                cloned[key] = deepClone(val, seen);
            }
        }
        return cloned;
    }

    // Handle plain objects
    const cloned = {};
    seen.set(obj, cloned);
    for (const key of Object.keys(obj)) {
        const val = obj[key];
        if (typeof val !== 'function') {
            cloned[key] = deepClone(val, seen);
        }
    }
    return cloned;
}

/**
 * Deep clone a scene node tree, handling children recursively.
 */
function deepCloneNode(node, seen = new WeakMap()) {
    if (!node) return null;

    // Check for circular reference
    if (seen.has(node)) {
        return seen.get(node);
    }

    // Create cloned node
    const cloned = {};
    seen.set(node, cloned);

    // Copy all properties except children (which we handle specially)
    for (const key of Object.keys(node)) {
        if (key === 'children') continue;
        const val = node[key];
        if (typeof val !== 'function') {
            cloned[key] = deepClone(val, seen);
        }
    }

    // Handle children array recursively
    if (node.children && Array.isArray(node.children)) {
        cloned.children = node.children.map(child => deepCloneNode(child, seen));
    } else {
        cloned.children = [];
    }

    return cloned;
}

/**
 * Collect transferable objects from scene data
 */
function collectTransferables(sceneData) {
    const transferables = [];

    for (const mesh of sceneData.meshes) {
        if (mesh.points && mesh.points.buffer) {
            transferables.push(mesh.points.buffer);
        }
        if (mesh.normals && mesh.normals.buffer) {
            transferables.push(mesh.normals.buffer);
        }
        if (mesh.uvs && mesh.uvs.buffer) {
            transferables.push(mesh.uvs.buffer);
        }
        if (mesh.indices && mesh.indices.buffer) {
            transferables.push(mesh.indices.buffer);
        }
    }

    // Images contain the actual pixel data
    for (const image of sceneData.images) {
        if (image.data && image.data.buffer) {
            transferables.push(image.data.buffer);
        }
    }

    return transferables;
}

/**
 * Get a specific mesh by index (for streaming mode)
 */
function getMesh(index) {
    if (!loader) {
        self.postMessage({ type: 'error', error: 'No USD loaded' });
        return;
    }

    const mesh = loader.getMesh(index);
    if (mesh) {
        const data = extractMeshData(mesh);
        const transferables = [];
        if (data.points && data.points.buffer) transferables.push(data.points.buffer);
        if (data.normals && data.normals.buffer) transferables.push(data.normals.buffer);
        if (data.uvs && data.uvs.buffer) transferables.push(data.uvs.buffer);
        if (data.indices && data.indices.buffer) transferables.push(data.indices.buffer);

        self.postMessage({ type: 'mesh', index, data }, transferables);
    } else {
        self.postMessage({ type: 'error', error: `Mesh ${index} not found` });
    }
}

/**
 * Get a specific texture by index
 */
function getTexture(index) {
    if (!loader) {
        self.postMessage({ type: 'error', error: 'No USD loaded' });
        return;
    }

    const texture = loader.getTexture(index);
    if (texture) {
        const data = extractTextureData(texture);
        const transferables = data.data && data.data.buffer ? [data.data.buffer] : [];
        self.postMessage({ type: 'texture', index, data }, transferables);
    } else {
        self.postMessage({ type: 'error', error: `Texture ${index} not found` });
    }
}

/**
 * Message handler
 */
self.onmessage = async function(e) {
    const { type, ...params } = e.data;

    switch (type) {
        case 'init':
            const ok = await init();
            self.postMessage({ type: 'init_complete', success: ok });
            break;

        case 'load':
            await loadFromBinary(params.binary, params.filename, params.options);
            break;

        case 'getMesh':
            getMesh(params.index);
            break;

        case 'getTexture':
            getTexture(params.index);
            break;

        case 'dispose':
            if (loader) {
                loader = null;
            }
            self.postMessage({ type: 'disposed' });
            break;

        default:
            self.postMessage({ type: 'error', error: `Unknown message type: ${type}` });
    }
};

// Signal that worker is ready
self.postMessage({ type: 'ready' });
