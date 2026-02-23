// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Progressive Scene Loading for Three.js
//
// Usage:
//   const scene = new ProgressiveScene();
//   scene.onPrimReady((prim) => {
//       const mesh = prim.toThreeMesh();
//       threeScene.add(mesh);
//   });
//   await scene.load('model.usdz');

import { WorkerBridge, PrimSkeletonData, PrimGeometryData, AssetRequestData, LoadPriority as WBLoadPriority } from './WorkerBridge';

// ============================================================================
// Types
// ============================================================================

export type PrimState = 'skeleton' | 'queued' | 'loading' | 'ready' | 'error';
export type LoadPriority = 'immediate' | 'high' | 'normal' | 'low' | 'deferred';
export type SceneState = 'idle' | 'loading' | 'ready' | 'error';

const PRIORITY_MAP: Record<LoadPriority, WBLoadPriority> = {
    'immediate': 0,
    'high': 1,
    'normal': 2,
    'low': 3,
    'deferred': 4
};

export interface PrimSkeleton {
    path: string;
    name: string;
    typeName: string;
    parentPath: string;
    childPaths: string[];
    hasGeometry: boolean;
    hasMaterial: boolean;
    hasTransform: boolean;
    estimatedVertices: number;
}

export interface AssetRequest {
    path: string;
    resolvedUrl: string;
    type: 'usd' | 'usdz' | 'texture' | 'other';
    required: boolean;
    requestingPrim: string;
}

export interface LoadOptions {
    /** Path to lightusd_worker.js (for true non-blocking) */
    workerUrl?: string;
    /** Base URL for resolving relative paths */
    baseUrl?: string;
    /** Automatically load all geometry after structure (default: false) */
    autoLoad?: boolean;
    /** Time code for animated data (default: 0) */
    time?: number;
    /** Compute tangents for normal mapping (default: true) */
    computeTangents?: boolean;
    /** Maximum concurrent asset fetches (default: 6) */
    maxConcurrentFetches?: number;
    /** Time budget per frame in ms (default: 16) */
    timeBudgetMs?: number;
}

export interface FrustumInfo {
    cameraPosition: [number, number, number];
    cameraDirection: [number, number, number];
    planes?: Float32Array; // 6 planes x 4 floats
}

// ============================================================================
// PrimProxy - Lightweight handle to a prim
// ============================================================================

export class PrimProxy {
    private _scene: ProgressiveScene;
    private _path: string;
    private _skeleton: PrimSkeleton;
    private _state: PrimState = 'skeleton';
    private _geometry: PrimGeometryData | null = null;
    private _error: string | null = null;
    private _priority: LoadPriority = 'normal';

    /** @internal */
    constructor(scene: ProgressiveScene, skeleton: PrimSkeleton) {
        this._scene = scene;
        this._path = skeleton.path;
        this._skeleton = skeleton;
    }

    // --- Identity ---

    get path(): string { return this._path; }
    get name(): string { return this._skeleton.name; }
    get typeName(): string { return this._skeleton.typeName; }

    // --- Hierarchy ---

    get parent(): PrimProxy | null {
        return this._skeleton.parentPath
            ? this._scene.getPrim(this._skeleton.parentPath)
            : null;
    }

    get children(): PrimProxy[] {
        return this._skeleton.childPaths
            .map(p => this._scene.getPrim(p))
            .filter((p): p is PrimProxy => p !== null);
    }

    // --- State ---

    get state(): PrimState { return this._state; }
    get isRenderable(): boolean { return this._state === 'ready'; }
    get hasGeometry(): boolean { return this._skeleton.hasGeometry; }
    get error(): string | null { return this._error; }

    // --- Geometry (null until ready) ---

    get geometry(): PrimGeometryData | null { return this._geometry; }

    // --- Transform ---

    get transform(): Float32Array | null {
        return this._geometry?.transform ?? null;
    }

    // --- Bounds ---

    get bounds(): { min: [number, number, number]; max: [number, number, number] } | null {
        if (!this._geometry) return null;
        return {
            min: this._geometry.boundsMin,
            max: this._geometry.boundsMax
        };
    }

    // --- Loading Control ---

    get priority(): LoadPriority { return this._priority; }

    set priority(value: LoadPriority) {
        this._priority = value;
        if (this._state === 'queued') {
            this._scene._updatePriority(this._path, value);
        }
    }

    /**
     * Request loading geometry for this prim.
     * No-op if already loading or ready.
     */
    requestLoad(priority?: LoadPriority): void {
        if (this._state === 'skeleton' || this._state === 'error') {
            this._state = 'queued';
            this._priority = priority ?? this._priority;
            this._scene._requestLoad(this._path, this._priority);
        }
    }

    // --- Three.js Integration ---

    /**
     * Create a Three.js BufferGeometry from loaded geometry.
     * Returns null if not ready.
     */
    toThreeGeometry(): any | null {
        if (!this._geometry || typeof window === 'undefined') return null;

        // Dynamic import to avoid hard dependency
        const THREE = (window as any).THREE;
        if (!THREE) {
            console.warn('THREE not found on window. Include Three.js first.');
            return null;
        }

        const geom = new THREE.BufferGeometry();

        // Positions (required)
        if (this._geometry.positions) {
            geom.setAttribute('position', new THREE.BufferAttribute(
                this._geometry.positions, 3
            ));
        }

        // Normals
        if (this._geometry.normals) {
            geom.setAttribute('normal', new THREE.BufferAttribute(
                this._geometry.normals, 3
            ));
        }

        // UVs
        if (this._geometry.texcoords) {
            geom.setAttribute('uv', new THREE.BufferAttribute(
                this._geometry.texcoords, 2
            ));
        }

        // Tangents
        if (this._geometry.tangents) {
            geom.setAttribute('tangent', new THREE.BufferAttribute(
                this._geometry.tangents, 4
            ));
        }

        // Indices
        if (this._geometry.indices) {
            geom.setIndex(new THREE.BufferAttribute(
                this._geometry.indices, 1
            ));
        }

        geom.computeBoundingBox();
        geom.computeBoundingSphere();

        return geom;
    }

    /**
     * Create a complete Three.js Mesh with default material.
     * Returns null if not ready.
     */
    toThreeMesh(): any | null {
        const geom = this.toThreeGeometry();
        if (!geom) return null;

        const THREE = (window as any).THREE;
        const material = new THREE.MeshStandardMaterial({
            color: 0x888888,
            side: this._geometry?.doubleSided ? THREE.DoubleSide : THREE.FrontSide
        });

        const mesh = new THREE.Mesh(geom, material);

        // Apply transform
        if (this._geometry?.transform) {
            const m = new THREE.Matrix4();
            m.fromArray(this._geometry.transform);
            mesh.applyMatrix4(m);
        }

        mesh.name = this.name;
        mesh.userData.usdPath = this.path;

        return mesh;
    }

    // --- Internal ---

    /** @internal */
    _setState(state: PrimState): void {
        this._state = state;
    }

    /** @internal */
    _setGeometry(geometry: PrimGeometryData): void {
        this._geometry = geometry;
        this._state = 'ready';
    }

    /** @internal */
    _setError(error: string): void {
        this._error = error;
        this._state = 'error';
    }
}

// ============================================================================
// ProgressiveScene - Main API
// ============================================================================

export class ProgressiveScene {
    private _bridge: WorkerBridge | null = null;
    private _state: SceneState = 'idle';
    private _error: string | null = null;
    private _prims: Map<string, PrimProxy> = new Map();
    private _rootPaths: string[] = [];
    private _options: LoadOptions = {};
    private _loadedCount: number = 0;
    private _totalGeometryPrims: number = 0;

    // Asset fetching
    private _pendingAssets: Map<string, AssetRequest> = new Map();
    private _activeFetches: number = 0;
    private _fetchQueue: AssetRequest[] = [];

    // Callbacks
    private _onStructureReady: (() => void)[] = [];
    private _onPrimDiscovered: ((prim: PrimProxy) => void)[] = [];
    private _onPrimReady: ((prim: PrimProxy) => void)[] = [];
    private _onProgress: ((progress: number) => void)[] = [];
    private _onError: ((error: string) => void)[] = [];
    private _onAssetNeeded: ((request: AssetRequest) => Promise<ArrayBuffer | null>)[] = [];

    constructor() {}

    // === State ===

    get state(): SceneState { return this._state; }
    get error(): string | null { return this._error; }
    get isReady(): boolean { return this._state === 'ready'; }

    // === Progress ===

    get progress(): number {
        if (this._totalGeometryPrims === 0) return 0;
        return this._loadedCount / this._totalGeometryPrims;
    }

    // === Prim Access ===

    get primCount(): number { return this._prims.size; }

    get rootPrims(): PrimProxy[] {
        return this._rootPaths
            .map(p => this._prims.get(p))
            .filter((p): p is PrimProxy => p !== undefined);
    }

    getPrim(path: string): PrimProxy | null {
        return this._prims.get(path) ?? null;
    }

    getAllPrims(): PrimProxy[] {
        return Array.from(this._prims.values());
    }

    getGeometryPrims(): PrimProxy[] {
        return Array.from(this._prims.values()).filter(p => p.hasGeometry);
    }

    // === Event Handlers ===

    /** Called when structure parsing is complete */
    onStructureReady(callback: () => void): () => void {
        this._onStructureReady.push(callback);
        return () => {
            const idx = this._onStructureReady.indexOf(callback);
            if (idx >= 0) this._onStructureReady.splice(idx, 1);
        };
    }

    /** Called when a new prim is discovered (structure only) */
    onPrimDiscovered(callback: (prim: PrimProxy) => void): () => void {
        this._onPrimDiscovered.push(callback);
        return () => {
            const idx = this._onPrimDiscovered.indexOf(callback);
            if (idx >= 0) this._onPrimDiscovered.splice(idx, 1);
        };
    }

    /** Called when a prim's geometry is loaded and ready */
    onPrimReady(callback: (prim: PrimProxy) => void): () => void {
        this._onPrimReady.push(callback);
        return () => {
            const idx = this._onPrimReady.indexOf(callback);
            if (idx >= 0) this._onPrimReady.splice(idx, 1);
        };
    }

    /** Called with loading progress (0-1) */
    onProgress(callback: (progress: number) => void): () => void {
        this._onProgress.push(callback);
        return () => {
            const idx = this._onProgress.indexOf(callback);
            if (idx >= 0) this._onProgress.splice(idx, 1);
        };
    }

    /** Called on error */
    onError(callback: (error: string) => void): () => void {
        this._onError.push(callback);
        return () => {
            const idx = this._onError.indexOf(callback);
            if (idx >= 0) this._onError.splice(idx, 1);
        };
    }

    /**
     * Custom asset fetcher. If provided, called instead of default fetch().
     * Return null to skip the asset.
     */
    onAssetNeeded(callback: (request: AssetRequest) => Promise<ArrayBuffer | null>): () => void {
        this._onAssetNeeded.push(callback);
        return () => {
            const idx = this._onAssetNeeded.indexOf(callback);
            if (idx >= 0) this._onAssetNeeded.splice(idx, 1);
        };
    }

    // === Loading ===

    /**
     * Load USD file from URL.
     * Returns when structure is parsed. Geometry loads progressively after.
     */
    async load(url: string, options?: LoadOptions): Promise<void> {
        this._options = {
            baseUrl: new URL('./', url).href,
            autoLoad: false,
            time: 0,
            computeTangents: true,
            maxConcurrentFetches: 6,
            timeBudgetMs: 16,
            ...options
        };

        this._state = 'loading';
        this._prims.clear();
        this._rootPaths = [];
        this._loadedCount = 0;
        this._totalGeometryPrims = 0;

        try {
            // Initialize bridge
            await this._initBridge();

            // Fetch the file
            const response = await fetch(url);
            if (!response.ok) {
                throw new Error(`Failed to fetch ${url}: ${response.status}`);
            }
            const data = await response.arrayBuffer();

            // Parse structure
            await this._loadFromData(data, this._getFilename(url));
        } catch (e) {
            this._state = 'error';
            this._error = e instanceof Error ? e.message : String(e);
            this._onError.forEach(cb => cb(this._error!));
            throw e;
        }
    }

    /**
     * Load USD from ArrayBuffer.
     */
    async loadFromData(data: ArrayBuffer, filename: string, options?: LoadOptions): Promise<void> {
        this._options = {
            autoLoad: false,
            time: 0,
            computeTangents: true,
            maxConcurrentFetches: 6,
            timeBudgetMs: 16,
            ...options
        };

        this._state = 'loading';
        this._prims.clear();
        this._rootPaths = [];
        this._loadedCount = 0;
        this._totalGeometryPrims = 0;

        try {
            await this._initBridge();
            await this._loadFromData(data, filename);
        } catch (e) {
            this._state = 'error';
            this._error = e instanceof Error ? e.message : String(e);
            this._onError.forEach(cb => cb(this._error!));
            throw e;
        }
    }

    // === Priority Control ===

    /**
     * Update load priorities based on camera frustum.
     * Call this each frame or when camera moves significantly.
     */
    updatePriorities(frustum: FrustumInfo): void {
        const { cameraPosition, cameraDirection } = frustum;

        for (const prim of this._prims.values()) {
            if (prim.state !== 'skeleton') continue;
            if (!prim.hasGeometry) continue;

            // Calculate priority based on estimated position
            // (Use parent bounds or default position for skeleton prims)
            const priority = this._calculatePriority(prim, cameraPosition, cameraDirection);
            prim.priority = priority;
        }
    }

    private _calculatePriority(
        prim: PrimProxy,
        cameraPos: [number, number, number],
        cameraDir: [number, number, number]
    ): LoadPriority {
        // For skeleton prims, use a simple heuristic:
        // - Root prims get higher priority
        // - Prims with more estimated vertices get lower priority (load smaller first)
        const skeleton = (prim as any)._skeleton as PrimSkeleton;

        // Default to normal priority
        if (!skeleton.parentPath) {
            return 'high'; // Root prims
        }

        if (skeleton.estimatedVertices > 100000) {
            return 'low'; // Large meshes
        }

        return 'normal';
    }

    /**
     * Request loading all prims with geometry.
     * Use with autoLoad: false to control when loading starts.
     */
    loadAllPrims(priority: LoadPriority = 'normal'): void {
        for (const prim of this._prims.values()) {
            if (prim.hasGeometry && prim.state === 'skeleton') {
                prim.requestLoad(priority);
            }
        }
    }

    /**
     * Request loading prims by path patterns.
     */
    loadPrims(paths: string[], priority: LoadPriority = 'normal'): void {
        for (const path of paths) {
            const prim = this._prims.get(path);
            if (prim && prim.state === 'skeleton') {
                prim.requestLoad(priority);
            }
        }
    }

    /**
     * Process the load queue. Call this in your animation loop.
     * In worker mode, results are delivered via onPrimReady callback.
     * In inline mode, processes synchronously within time budget.
     */
    tick(): void {
        if (!this._bridge?.isReady) return;

        this._bridge.processQueue();
        this._processFetchQueue();
    }

    // === Cleanup ===

    /**
     * Cancel all pending loads and release resources.
     */
    dispose(): void {
        if (this._bridge) {
            this._bridge.dispose();
            this._bridge = null;
        }
        this._prims.clear();
        this._pendingAssets.clear();
        this._fetchQueue = [];
        this._state = 'idle';
    }

    // === Internal Methods ===

    /** @internal */
    _requestLoad(path: string, priority: LoadPriority): void {
        if (!this._bridge) return;
        this._bridge.requestLoadPrim(path, PRIORITY_MAP[priority], this._options.time ?? 0);
    }

    /** @internal */
    _updatePriority(path: string, priority: LoadPriority): void {
        if (!this._bridge) return;
        this._bridge.setPriority(path, PRIORITY_MAP[priority]);
    }

    private async _initBridge(): Promise<void> {
        if (this._bridge) return;

        this._bridge = new WorkerBridge({
            workerUrl: this._options.workerUrl,
            timeBudgetMs: this._options.timeBudgetMs,
            cacheMaxSizeMb: 256
        });

        // Set up event handlers
        this._bridge.onPrimReady = (geometry) => {
            this._handlePrimReady(geometry);
        };

        this._bridge.onAssetNeeded = (request) => {
            this._handleAssetNeeded(request);
        };

        this._bridge.onError = (error) => {
            this._error = error;
            this._onError.forEach(cb => cb(error));
        };

        await this._bridge.init();
    }

    private async _loadFromData(data: ArrayBuffer, filename: string): Promise<void> {
        if (!this._bridge) {
            throw new Error('Bridge not initialized');
        }

        const result = await this._bridge.loadFile(data, filename);

        if (!result.ok) {
            throw new Error(result.error ?? 'Unknown parse error');
        }

        // Build prim proxies from skeletons
        this._buildPrimProxies(result.skeletons!);

        this._state = 'ready';
        this._onStructureReady.forEach(cb => cb());

        // Auto-load if requested
        if (this._options.autoLoad) {
            this.loadAllPrims();
        }
    }

    private _buildPrimProxies(skeletons: PrimSkeletonData[]): void {
        // First pass: create all proxies
        for (const skelData of skeletons) {
            const skeleton: PrimSkeleton = {
                path: skelData.path,
                name: skelData.name,
                typeName: skelData.typeName,
                parentPath: skelData.parentPath,
                childPaths: skelData.childPaths,
                hasGeometry: skelData.hasGeometry,
                hasMaterial: skelData.hasMaterial,
                hasTransform: skelData.hasTransform,
                estimatedVertices: skelData.estimatedVertices
            };

            const proxy = new PrimProxy(this, skeleton);
            this._prims.set(skeleton.path, proxy);

            // Track root prims
            if (!skeleton.parentPath) {
                this._rootPaths.push(skeleton.path);
            }

            // Count geometry prims for progress
            if (skeleton.hasGeometry) {
                this._totalGeometryPrims++;
            }

            // Notify
            this._onPrimDiscovered.forEach(cb => cb(proxy));
        }
    }

    private _handlePrimReady(geometry: PrimGeometryData): void {
        const prim = this._prims.get(geometry.path);
        if (!prim) return;

        prim._setGeometry(geometry);
        this._loadedCount++;

        // Notify callbacks
        this._onPrimReady.forEach(cb => cb(prim));

        // Update progress
        const progress = this.progress;
        this._onProgress.forEach(cb => cb(progress));
    }

    private _handleAssetNeeded(request: AssetRequestData): void {
        const assetRequest: AssetRequest = {
            path: request.path,
            resolvedUrl: request.resolvedUrl || this._resolveAssetUrl(request.path),
            type: request.type,
            required: request.required,
            requestingPrim: request.requestingPrim
        };

        this._pendingAssets.set(request.path, assetRequest);
        this._fetchQueue.push(assetRequest);
        this._processFetchQueue();
    }

    private _resolveAssetUrl(path: string): string {
        if (path.startsWith('http://') || path.startsWith('https://')) {
            return path;
        }
        return (this._options.baseUrl ?? '') + path;
    }

    private async _processFetchQueue(): Promise<void> {
        const maxConcurrent = this._options.maxConcurrentFetches ?? 6;

        while (this._fetchQueue.length > 0 && this._activeFetches < maxConcurrent) {
            const request = this._fetchQueue.shift()!;
            this._activeFetches++;

            this._fetchAsset(request).finally(() => {
                this._activeFetches--;
                this._processFetchQueue();
            });
        }
    }

    private async _fetchAsset(request: AssetRequest): Promise<void> {
        try {
            let data: ArrayBuffer | null = null;

            // Try custom handler first
            for (const handler of this._onAssetNeeded) {
                data = await handler(request);
                if (data) break;
            }

            // Fall back to fetch
            if (!data) {
                const response = await fetch(request.resolvedUrl);
                if (response.ok) {
                    data = await response.arrayBuffer();
                }
            }

            if (data && this._bridge) {
                this._bridge.provideAsset(request.path, data);
            }
        } catch (e) {
            console.warn(`Failed to fetch asset ${request.path}:`, e);
        }

        this._pendingAssets.delete(request.path);
    }

    private _getFilename(url: string): string {
        return url.split('/').pop() || 'unknown.usd';
    }
}

// ============================================================================
// Convenience function
// ============================================================================

/**
 * Load USD file progressively.
 *
 * @example
 * ```typescript
 * const scene = await loadUSDProgressive('model.usdz', {
 *     autoLoad: true,
 *     onPrimReady: (prim) => {
 *         const mesh = prim.toThreeMesh();
 *         threeScene.add(mesh);
 *     }
 * });
 *
 * // In animation loop:
 * function animate() {
 *     scene.tick();
 *     renderer.render(threeScene, camera);
 *     requestAnimationFrame(animate);
 * }
 * ```
 */
export async function loadUSDProgressive(
    url: string,
    options?: LoadOptions & {
        onPrimReady?: (prim: PrimProxy) => void;
        onPrimDiscovered?: (prim: PrimProxy) => void;
        onProgress?: (progress: number) => void;
        onStructureReady?: () => void;
    }
): Promise<ProgressiveScene> {
    const scene = new ProgressiveScene();

    if (options?.onStructureReady) {
        scene.onStructureReady(options.onStructureReady);
    }
    if (options?.onPrimDiscovered) {
        scene.onPrimDiscovered(options.onPrimDiscovered);
    }
    if (options?.onPrimReady) {
        scene.onPrimReady(options.onPrimReady);
    }
    if (options?.onProgress) {
        scene.onProgress(options.onProgress);
    }

    await scene.load(url, options);
    return scene;
}

export default ProgressiveScene;
