// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Worker Bridge for Progressive Loading
//
// Handles communication between main thread and WASM worker

// ============================================================================
// Types
// ============================================================================

export interface PrimSkeletonData {
    path: string;
    name: string;
    typeName: string;
    parentPath: string;
    childPaths: string[];
    hasGeometry: boolean;
    hasMaterial: boolean;
    hasTransform: boolean;
    hasTimesamples: boolean;
    estimatedVertices: number;
    estimatedFaces: number;
}

export interface PrimGeometryData {
    path: string;
    materialIndex: number;
    positions?: Float32Array;
    normals?: Float32Array;
    texcoords?: Float32Array;
    tangents?: Float32Array;
    indices?: Uint32Array;
    boundsMin: [number, number, number];
    boundsMax: [number, number, number];
    transform: Float32Array;
    doubleSided: boolean;
}

export interface AssetRequestData {
    path: string;
    resolvedUrl: string;
    type: 'usd' | 'usdz' | 'texture' | 'other';
    required: boolean;
    requestingPrim: string;
}

export interface ProcessQueueResult {
    processed: number;
    ready: PrimGeometryData[];
    pendingAssets: AssetRequestData[];
    pendingCount: number;
    waitingAssetCount: number;
}

export interface ParseStructureResult {
    ok: boolean;
    error?: string;
    skeletons?: PrimSkeletonData[];
    primCount?: number;
}

export type LoaderState = 'idle' | 'parsing' | 'ready' | 'error';
export type PrimLoadState = 'skeleton' | 'queued' | 'loading' | 'waitingAsset' | 'ready' | 'error';
export type LoadPriority = 0 | 1 | 2 | 3 | 4; // immediate, high, normal, low, deferred

// ============================================================================
// Worker Bridge
// ============================================================================

export interface WorkerBridgeConfig {
    /** Path to lightusd_worker.js */
    workerUrl?: string;
    /** WASM binary path (optional, auto-detected) */
    wasmUrl?: string;
    /** Time budget per processQueue call in ms (default: 16) */
    timeBudgetMs?: number;
    /** Asset cache max size in MB (default: 256) */
    cacheMaxSizeMb?: number;
}

export interface WorkerBridgeEvents {
    onStructureReady?: (skeletons: PrimSkeletonData[]) => void;
    onPrimReady?: (geometry: PrimGeometryData) => void;
    onAssetNeeded?: (request: AssetRequestData) => void;
    onProgress?: (loaded: number, total: number) => void;
    onError?: (error: string) => void;
}

/**
 * Bridge to communicate with LightUSD WASM Worker
 *
 * Usage:
 *   const bridge = new WorkerBridge({ workerUrl: 'lightusd_worker.js' });
 *   await bridge.init();
 *
 *   bridge.onStructureReady = (skeletons) => { ... };
 *   bridge.onPrimReady = (geometry) => { ... };
 *
 *   await bridge.loadFile(arrayBuffer, 'model.usdz');
 *   bridge.requestLoadPrim('/World/Mesh', 2); // normal priority
 *
 *   // In animation loop:
 *   bridge.processQueue();
 */
export class WorkerBridge {
    private _worker: Worker | null = null;
    private _module: any = null;
    private _isInline: boolean = false;
    private _config: WorkerBridgeConfig;
    private _events: WorkerBridgeEvents = {};
    private _state: LoaderState = 'idle';
    private _error: string | null = null;
    private _resolveInit: (() => void) | null = null;
    private _rejectInit: ((err: Error) => void) | null = null;

    // Pending promises for async operations
    private _parsePromise: {
        resolve: (result: ParseStructureResult) => void;
        reject: (err: Error) => void;
    } | null = null;

    // Polling state
    private _isProcessing: boolean = false;
    private _processInterval: number | null = null;

    constructor(config: WorkerBridgeConfig = {}) {
        this._config = {
            timeBudgetMs: 16,
            cacheMaxSizeMb: 256,
            ...config
        };
    }

    // === Initialization ===

    /**
     * Initialize the worker. Must be called before any other methods.
     * Can use either a separate worker file or inline WASM module.
     */
    async init(): Promise<void> {
        if (this._worker || this._module) {
            return; // Already initialized
        }

        // Try to load as separate worker first
        if (this._config.workerUrl) {
            return this._initWorker();
        }

        // Fall back to inline module (synchronous on main thread)
        return this._initInline();
    }

    private async _initWorker(): Promise<void> {
        return new Promise((resolve, reject) => {
            this._resolveInit = resolve;
            this._rejectInit = reject;

            try {
                this._worker = new Worker(this._config.workerUrl!, { type: 'module' });
                this._worker.onmessage = this._handleWorkerMessage.bind(this);
                this._worker.onerror = (e) => {
                    const err = new Error(`Worker error: ${e.message}`);
                    if (this._rejectInit) {
                        this._rejectInit(err);
                        this._rejectInit = null;
                        this._resolveInit = null;
                    }
                    this._events.onError?.(err.message);
                };

                // Send init message
                this._worker.postMessage({ type: 'INIT', config: this._config });
            } catch (e) {
                reject(e);
            }
        });
    }

    private async _initInline(): Promise<void> {
        this._isInline = true;

        // Dynamically import the module
        try {
            const createModule = (await import('./lightusd')).default;
            this._module = await createModule();
            this._module.workerInit();

            if (this._config.timeBudgetMs) {
                this._module.setTimeBudget(this._config.timeBudgetMs);
            }
            if (this._config.cacheMaxSizeMb) {
                this._module.setCacheMaxSize(this._config.cacheMaxSizeMb);
            }
        } catch (e) {
            throw new Error(`Failed to load WASM module: ${e}`);
        }
    }

    private _handleWorkerMessage(event: MessageEvent): void {
        const { type, data } = event.data;

        switch (type) {
            case 'INIT_COMPLETE':
                if (this._resolveInit) {
                    this._resolveInit();
                    this._resolveInit = null;
                    this._rejectInit = null;
                }
                break;

            case 'STRUCTURE_READY':
                this._state = 'ready';
                if (this._parsePromise) {
                    this._parsePromise.resolve(data);
                    this._parsePromise = null;
                }
                this._events.onStructureReady?.(data.skeletons);
                break;

            case 'PRIM_READY':
                this._events.onPrimReady?.(data);
                break;

            case 'NEED_ASSET':
                this._events.onAssetNeeded?.(data);
                break;

            case 'PROGRESS':
                this._events.onProgress?.(data.loaded, data.total);
                break;

            case 'ERROR':
                this._state = 'error';
                this._error = data.message;
                if (this._parsePromise) {
                    this._parsePromise.reject(new Error(data.message));
                    this._parsePromise = null;
                }
                this._events.onError?.(data.message);
                break;
        }
    }

    // === Event Handlers ===

    set onStructureReady(callback: (skeletons: PrimSkeletonData[]) => void) {
        this._events.onStructureReady = callback;
    }

    set onPrimReady(callback: (geometry: PrimGeometryData) => void) {
        this._events.onPrimReady = callback;
    }

    set onAssetNeeded(callback: (request: AssetRequestData) => void) {
        this._events.onAssetNeeded = callback;
    }

    set onProgress(callback: (loaded: number, total: number) => void) {
        this._events.onProgress = callback;
    }

    set onError(callback: (error: string) => void) {
        this._events.onError = callback;
    }

    // === State ===

    get state(): LoaderState { return this._state; }
    get error(): string | null { return this._error; }
    get isReady(): boolean { return this._state === 'ready'; }

    // === File Loading ===

    /**
     * Load USD file from ArrayBuffer.
     * Returns when structure is parsed.
     */
    async loadFile(data: ArrayBuffer, filename: string): Promise<ParseStructureResult> {
        this._state = 'parsing';

        if (this._isInline) {
            return this._loadFileInline(data, filename);
        }

        return new Promise((resolve, reject) => {
            this._parsePromise = { resolve, reject };

            // Convert to string for Emscripten
            const bytes = new Uint8Array(data);
            let str = '';
            for (let i = 0; i < bytes.length; i++) {
                str += String.fromCharCode(bytes[i]);
            }

            this._worker!.postMessage({
                type: 'LOAD_FILE',
                data: str,
                filename
            });
        });
    }

    private async _loadFileInline(data: ArrayBuffer, filename: string): Promise<ParseStructureResult> {
        const bytes = new Uint8Array(data);
        let str = '';
        for (let i = 0; i < bytes.length; i++) {
            str += String.fromCharCode(bytes[i]);
        }

        const result = this._module.parseStructure(str, filename);

        if (!result.ok) {
            this._state = 'error';
            this._error = result.error;
            return result;
        }

        this._state = 'ready';

        // Convert JS array to native array for skeletons
        const skeletons: PrimSkeletonData[] = [];
        const jsSkeletons = result.skeletons;
        for (let i = 0; i < jsSkeletons.length; i++) {
            skeletons.push(this._convertSkeleton(jsSkeletons[i]));
        }

        const parseResult: ParseStructureResult = {
            ok: true,
            skeletons,
            primCount: result.primCount
        };

        this._events.onStructureReady?.(skeletons);
        return parseResult;
    }

    private _convertSkeleton(jsSkel: any): PrimSkeletonData {
        const childPaths: string[] = [];
        const jsChildPaths = jsSkel.childPaths;
        for (let i = 0; i < jsChildPaths.length; i++) {
            childPaths.push(jsChildPaths[i]);
        }

        return {
            path: jsSkel.path,
            name: jsSkel.name,
            typeName: jsSkel.typeName,
            parentPath: jsSkel.parentPath,
            childPaths,
            hasGeometry: jsSkel.hasGeometry,
            hasMaterial: jsSkel.hasMaterial,
            hasTransform: jsSkel.hasTransform,
            hasTimesamples: jsSkel.hasTimesamples || false,
            estimatedVertices: jsSkel.estimatedVertices || 0,
            estimatedFaces: jsSkel.estimatedFaces || 0
        };
    }

    // === Load Requests ===

    /**
     * Request geometry loading for a prim.
     * @param path Prim path (e.g., "/World/Mesh")
     * @param priority 0=immediate, 1=high, 2=normal, 3=low, 4=deferred
     * @param time Time code for animated data
     */
    requestLoadPrim(path: string, priority: LoadPriority = 2, time: number = 0): void {
        if (this._isInline) {
            this._module.requestLoadPrim(path, priority, time);
        } else {
            this._worker!.postMessage({
                type: 'LOAD_PRIM',
                path,
                priority,
                time
            });
        }
    }

    /**
     * Request geometry loading for multiple prims.
     */
    requestLoadPrims(paths: string[], priority: LoadPriority = 2, time: number = 0): void {
        if (this._isInline) {
            this._module.requestLoadPrims(paths, priority, time);
        } else {
            this._worker!.postMessage({
                type: 'LOAD_PRIMS',
                paths,
                priority,
                time
            });
        }
    }

    /**
     * Change priority of a pending load request.
     */
    setPriority(path: string, priority: LoadPriority): void {
        if (this._isInline) {
            this._module.setPriority(path, priority);
        } else {
            this._worker!.postMessage({
                type: 'SET_PRIORITY',
                path,
                priority
            });
        }
    }

    /**
     * Cancel pending load request.
     */
    cancelLoad(path: string): void {
        if (this._isInline) {
            this._module.cancelLoad(path);
        } else {
            this._worker!.postMessage({
                type: 'CANCEL',
                path
            });
        }
    }

    /**
     * Cancel all pending requests.
     */
    cancelAll(): void {
        if (this._isInline) {
            this._module.cancelAll();
        } else {
            this._worker!.postMessage({ type: 'CANCEL_ALL' });
        }
    }

    // === Asset Management ===

    /**
     * Provide fetched asset data (for external assets).
     */
    provideAsset(path: string, data: ArrayBuffer): void {
        const bytes = new Uint8Array(data);
        let str = '';
        for (let i = 0; i < bytes.length; i++) {
            str += String.fromCharCode(bytes[i]);
        }

        if (this._isInline) {
            this._module.provideAsset(path, str);
        } else {
            this._worker!.postMessage({
                type: 'PROVIDE_ASSET',
                path,
                data: str
            });
        }
    }

    // === Processing ===

    /**
     * Process load queue and return results.
     * Call this periodically (e.g., in requestAnimationFrame).
     * @param maxCount Maximum prims to process (0 = time-budget based)
     */
    processQueue(maxCount: number = 0): ProcessQueueResult | null {
        if (!this.isReady) return null;

        if (this._isInline) {
            const result = this._module.processQueue(maxCount);
            return this._convertProcessResult(result);
        }

        // For worker mode, send process request
        // Results come back via onPrimReady callbacks
        this._worker!.postMessage({
            type: 'PROCESS_QUEUE',
            maxCount
        });

        return null;
    }

    private _convertProcessResult(jsResult: any): ProcessQueueResult {
        const ready: PrimGeometryData[] = [];
        const jsReady = jsResult.ready;
        for (let i = 0; i < jsReady.length; i++) {
            ready.push(this._convertGeometry(jsReady[i]));
        }

        const pendingAssets: AssetRequestData[] = [];
        const jsPending = jsResult.pendingAssets;
        for (let i = 0; i < jsPending.length; i++) {
            pendingAssets.push(jsPending[i]);
        }

        // Notify ready prims
        for (const geom of ready) {
            this._events.onPrimReady?.(geom);
        }

        // Notify pending assets
        for (const req of pendingAssets) {
            this._events.onAssetNeeded?.(req);
        }

        return {
            processed: jsResult.processed,
            ready,
            pendingAssets,
            pendingCount: jsResult.pendingCount,
            waitingAssetCount: jsResult.waitingAssetCount
        };
    }

    private _convertGeometry(jsGeom: any): PrimGeometryData {
        // Copy typed arrays (they reference WASM memory which may be invalidated)
        const positions = jsGeom.positions ? new Float32Array(jsGeom.positions) : undefined;
        const normals = jsGeom.normals ? new Float32Array(jsGeom.normals) : undefined;
        const texcoords = jsGeom.texcoords ? new Float32Array(jsGeom.texcoords) : undefined;
        const tangents = jsGeom.tangents ? new Float32Array(jsGeom.tangents) : undefined;
        const indices = jsGeom.indices ? new Uint32Array(jsGeom.indices) : undefined;
        const transform = new Float32Array(jsGeom.transform);

        return {
            path: jsGeom.path,
            materialIndex: jsGeom.materialIndex,
            positions,
            normals,
            texcoords,
            tangents,
            indices,
            boundsMin: [jsGeom.boundsMin[0], jsGeom.boundsMin[1], jsGeom.boundsMin[2]],
            boundsMax: [jsGeom.boundsMax[0], jsGeom.boundsMax[1], jsGeom.boundsMax[2]],
            transform,
            doubleSided: jsGeom.doubleSided
        };
    }

    /**
     * Start automatic processing at 60fps.
     * Results are delivered via onPrimReady callback.
     */
    startAutoProcess(): void {
        if (this._processInterval !== null) return;

        const tick = () => {
            if (!this._isProcessing && this.isReady) {
                this._isProcessing = true;
                this.processQueue();
                this._isProcessing = false;
            }
            this._processInterval = requestAnimationFrame(tick);
        };

        this._processInterval = requestAnimationFrame(tick);
    }

    /**
     * Stop automatic processing.
     */
    stopAutoProcess(): void {
        if (this._processInterval !== null) {
            cancelAnimationFrame(this._processInterval);
            this._processInterval = null;
        }
    }

    // === State Queries ===

    /**
     * Get prim load state.
     */
    getPrimState(path: string): PrimLoadState {
        if (this._isInline) {
            return this._module.getPrimState(path) as PrimLoadState;
        }
        return 'skeleton'; // Worker mode needs async query
    }

    /**
     * Get pending load count.
     */
    getPendingCount(): number {
        if (this._isInline) {
            return this._module.getPendingCount();
        }
        return 0; // Worker mode needs async query
    }

    /**
     * Check if there are ready prims to retrieve.
     */
    hasReadyPrims(): boolean {
        if (this._isInline) {
            return this._module.hasReadyPrims();
        }
        return false; // Worker mode delivers via callback
    }

    // === Cleanup ===

    /**
     * Terminate worker and release resources.
     */
    dispose(): void {
        this.stopAutoProcess();

        if (this._worker) {
            this._worker.terminate();
            this._worker = null;
        }

        if (this._module) {
            this._module.clearCache();
            this._module = null;
        }

        this._state = 'idle';
        this._parsePromise = null;
    }
}

export default WorkerBridge;
