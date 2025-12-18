// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Web Worker wrapper
//
// This file is loaded as a Web Worker and wraps the Emscripten WASM module.
// It handles the postMessage protocol between the main thread and the WASM loader.
//
// Build: This file should be bundled with lightusd_worker.js/.wasm
//
// Usage from main thread:
//   const worker = new Worker('lightusd-worker.js', { type: 'module' });
//   worker.postMessage({ type: 'INIT', config: { timeBudgetMs: 16 } });

// Import the WASM module
import createLightUSDWorker from './lightusd_worker.js';

let Module = null;
let isInitialized = false;

// Initialize the WASM module
async function init(config) {
    try {
        Module = await createLightUSDWorker();
        Module.workerInit();

        if (config.timeBudgetMs) {
            Module.setTimeBudget(config.timeBudgetMs);
        }
        if (config.cacheMaxSizeMb) {
            Module.setCacheMaxSize(config.cacheMaxSizeMb);
        }

        isInitialized = true;
        self.postMessage({ type: 'INIT_COMPLETE' });
    } catch (e) {
        self.postMessage({
            type: 'ERROR',
            data: { message: `Failed to initialize WASM: ${e.message}` }
        });
    }
}

// Parse structure from file data
function parseStructure(data, filename) {
    if (!isInitialized) {
        self.postMessage({
            type: 'ERROR',
            data: { message: 'Worker not initialized' }
        });
        return;
    }

    try {
        const result = Module.parseStructure(data, filename);

        if (!result.ok) {
            self.postMessage({
                type: 'ERROR',
                data: { message: result.error }
            });
            return;
        }

        // Convert skeletons to plain objects
        const skeletons = [];
        const jsSkeletons = result.skeletons;
        for (let i = 0; i < jsSkeletons.length; i++) {
            const skel = jsSkeletons[i];
            const childPaths = [];
            for (let j = 0; j < skel.childPaths.length; j++) {
                childPaths.push(skel.childPaths[j]);
            }
            skeletons.push({
                path: skel.path,
                name: skel.name,
                typeName: skel.typeName,
                parentPath: skel.parentPath,
                childPaths,
                hasGeometry: skel.hasGeometry,
                hasMaterial: skel.hasMaterial,
                hasTransform: skel.hasTransform,
                hasTimesamples: skel.hasTimesamples || false,
                estimatedVertices: skel.estimatedVertices || 0,
                estimatedFaces: skel.estimatedFaces || 0
            });
        }

        self.postMessage({
            type: 'STRUCTURE_READY',
            data: {
                ok: true,
                skeletons,
                primCount: result.primCount
            }
        });
    } catch (e) {
        self.postMessage({
            type: 'ERROR',
            data: { message: `Parse error: ${e.message}` }
        });
    }
}

// Request loading geometry for a prim
function requestLoadPrim(path, priority, time) {
    if (!isInitialized) return;
    Module.requestLoadPrim(path, priority, time);
}

// Request loading multiple prims
function requestLoadPrims(paths, priority, time) {
    if (!isInitialized) return;
    Module.requestLoadPrims(paths, priority, time);
}

// Set priority for a pending request
function setPriority(path, priority) {
    if (!isInitialized) return;
    Module.setPriority(path, priority);
}

// Cancel load request
function cancelLoad(path) {
    if (!isInitialized) return;
    Module.cancelLoad(path);
}

// Cancel all requests
function cancelAll() {
    if (!isInitialized) return;
    Module.cancelAll();
}

// Provide fetched asset
function provideAsset(path, data) {
    if (!isInitialized) return;
    Module.provideAsset(path, data);
}

// Process queue and send results
function processQueue(maxCount) {
    if (!isInitialized) return;

    const result = Module.processQueue(maxCount);

    // Send ready prims to main thread
    const jsReady = result.ready;
    for (let i = 0; i < jsReady.length; i++) {
        const geom = jsReady[i];

        // Copy typed arrays since they reference WASM memory
        const geometry = {
            path: geom.path,
            materialIndex: geom.materialIndex,
            positions: geom.positions ? new Float32Array(geom.positions) : null,
            normals: geom.normals ? new Float32Array(geom.normals) : null,
            texcoords: geom.texcoords ? new Float32Array(geom.texcoords) : null,
            tangents: geom.tangents ? new Float32Array(geom.tangents) : null,
            indices: geom.indices ? new Uint32Array(geom.indices) : null,
            boundsMin: [geom.boundsMin[0], geom.boundsMin[1], geom.boundsMin[2]],
            boundsMax: [geom.boundsMax[0], geom.boundsMax[1], geom.boundsMax[2]],
            transform: new Float32Array(geom.transform),
            doubleSided: geom.doubleSided
        };

        // Build transfer list for zero-copy transfer
        const transfers = [];
        if (geometry.positions) transfers.push(geometry.positions.buffer);
        if (geometry.normals) transfers.push(geometry.normals.buffer);
        if (geometry.texcoords) transfers.push(geometry.texcoords.buffer);
        if (geometry.tangents) transfers.push(geometry.tangents.buffer);
        if (geometry.indices) transfers.push(geometry.indices.buffer);
        transfers.push(geometry.transform.buffer);

        self.postMessage({ type: 'PRIM_READY', data: geometry }, transfers);
    }

    // Send asset requests
    const jsPending = result.pendingAssets;
    for (let i = 0; i < jsPending.length; i++) {
        self.postMessage({
            type: 'NEED_ASSET',
            data: jsPending[i]
        });
    }

    // Send progress if there are pending items
    if (result.pendingCount > 0 || result.processed > 0) {
        self.postMessage({
            type: 'PROGRESS',
            data: {
                processed: result.processed,
                pending: result.pendingCount,
                waitingAsset: result.waitingAssetCount
            }
        });
    }
}

// Handle messages from main thread
self.onmessage = function(event) {
    const { type, ...data } = event.data;

    switch (type) {
        case 'INIT':
            init(data.config || {});
            break;

        case 'LOAD_FILE':
            parseStructure(data.data, data.filename);
            break;

        case 'LOAD_PRIM':
            requestLoadPrim(data.path, data.priority, data.time);
            break;

        case 'LOAD_PRIMS':
            requestLoadPrims(data.paths, data.priority, data.time);
            break;

        case 'SET_PRIORITY':
            setPriority(data.path, data.priority);
            break;

        case 'CANCEL':
            cancelLoad(data.path);
            break;

        case 'CANCEL_ALL':
            cancelAll();
            break;

        case 'PROVIDE_ASSET':
            provideAsset(data.path, data.data);
            break;

        case 'PROCESS_QUEUE':
            processQueue(data.maxCount || 0);
            break;

        default:
            console.warn('Unknown message type:', type);
    }
};

// Report any errors
self.onerror = function(e) {
    self.postMessage({
        type: 'ERROR',
        data: { message: `Worker error: ${e.message}` }
    });
};
