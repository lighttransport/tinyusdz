/**
 * TinyUSDZ Worker Loader
 *
 * Loads USD files using a Web Worker, keeping the main thread responsive.
 * Progress updates are received via callbacks while the worker processes.
 */

export class TinyUSDZWorkerLoader {
    constructor(options = {}) {
        this.worker = null;
        this.workerReady = false;
        this.pendingCallbacks = new Map();
        this.messageId = 0;

        // Callbacks
        this.onProgress = options.onProgress || null;
        this.onTydraProgress = options.onTydraProgress || null;
        this.onTydraComplete = options.onTydraComplete || null;

        // Worker URL - can be overridden
        this.workerUrl = options.workerUrl || new URL('./TinyUSDZWorker.js', import.meta.url);
    }

    /**
     * Initialize the worker
     */
    async init(options = {}) {
        if (this.worker) return;

        return new Promise((resolve, reject) => {
            try {
                // Create worker with module type
                this.worker = new Worker(this.workerUrl, { type: 'module' });

                const initTimeout = setTimeout(() => {
                    reject(new Error('Worker init timeout'));
                }, 30000);

                this.worker.onmessage = (e) => {
                    const { type } = e.data;

                    if (type === 'ready') {
                        // Worker script loaded, now init WASM
                        this.worker.postMessage({ type: 'init', options });
                    } else if (type === 'init_complete') {
                        clearTimeout(initTimeout);
                        this.workerReady = true;
                        // Set up permanent message handler
                        this.worker.onmessage = this._handleMessage.bind(this);
                        resolve();
                    } else if (type === 'error') {
                        clearTimeout(initTimeout);
                        reject(new Error(e.data.error));
                    }
                };

                this.worker.onerror = (e) => {
                    clearTimeout(initTimeout);
                    reject(new Error(`Worker error: ${e.message}`));
                };
            } catch (e) {
                reject(e);
            }
        });
    }

    /**
     * Handle messages from the worker
     */
    _handleMessage(e) {
        const { type, ...data } = e.data;

        switch (type) {
            case 'progress':
                if (this.onProgress) {
                    this.onProgress({
                        phase: data.phase,
                        progress: data.progress,
                        message: data.message
                    });
                }
                break;

            case 'tydra_progress':
                if (this.onTydraProgress) {
                    this.onTydraProgress({
                        meshCurrent: data.meshCurrent,
                        meshTotal: data.meshTotal,
                        stage: data.stage,
                        meshName: data.meshName,
                        progress: data.progress
                    });
                }
                break;

            case 'tydra_complete':
                if (this.onTydraComplete) {
                    this.onTydraComplete({
                        meshCount: data.meshCount,
                        materialCount: data.materialCount,
                        textureCount: data.textureCount,
                        animationCount: data.animationCount,
                        nodeCount: data.nodeCount,
                        lightCount: data.lightCount,
                        cameraCount: data.cameraCount,
                        pointInstancerCount: data.pointInstancerCount,
                        pointInstanceDrawCount: data.pointInstanceDrawCount,
                        skeletonCount: data.skeletonCount,
                        unsupportedRenderableCount: data.unsupportedRenderableCount
                    });
                }
                break;

            case 'complete':
                this._resolveLoad(data.data);
                break;

            case 'error':
                this._rejectLoad(new Error(data.error));
                break;

            case 'mesh':
                this._resolvePending(`mesh_${data.index}`, data.data);
                break;

            case 'texture':
                this._resolvePending(`texture_${data.index}`, data.data);
                break;
        }
    }

    /**
     * Load USD from URL
     */
    async load(url, options = {}) {
        await this.init();

        // Fetch the file on main thread (for download progress)
        const response = await fetch(url);
        if (!response.ok) {
            throw new Error(`Failed to fetch ${url}: ${response.status}`);
        }

        const contentLength = response.headers.get('content-length');
        const total = contentLength ? parseInt(contentLength, 10) : 0;

        let loaded = 0;
        const reader = response.body.getReader();
        const chunks = [];

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            chunks.push(value);
            loaded += value.length;

            if (this.onProgress) {
                const pct = total > 0 ? loaded / total : 0;
                this.onProgress({
                    phase: 'downloading',
                    progress: pct * 0.3,
                    message: `Downloading: ${Math.round(pct * 100)}%`
                });
            }
        }

        // Combine chunks
        const totalLength = chunks.reduce((acc, c) => acc + c.length, 0);
        const binary = new Uint8Array(totalLength);
        let offset = 0;
        for (const chunk of chunks) {
            binary.set(chunk, offset);
            offset += chunk.length;
        }

        // Parse in worker
        return this.parse(binary, url, options);
    }

    /**
     * Parse USD binary data in worker
     */
    async parse(binary, filename = '', options = {}) {
        await this.init(options);

        return new Promise((resolve, reject) => {
            this._loadResolve = resolve;
            this._loadReject = reject;

            // Transfer the binary to the worker (zero-copy)
            this.worker.postMessage({
                type: 'load',
                binary: binary,
                filename: filename,
                options: options
            }, [binary.buffer]);
        });
    }

    _resolveLoad(data) {
        if (this._loadResolve) {
            // Wrap the data in an object with helper methods
            const result = new WorkerLoadResult(data, this);
            this._loadResolve(result);
            this._loadResolve = null;
            this._loadReject = null;
        }
    }

    _rejectLoad(error) {
        if (this._loadReject) {
            this._loadReject(error);
            this._loadResolve = null;
            this._loadReject = null;
        }
    }

    _resolvePending(key, data) {
        const callback = this.pendingCallbacks.get(key);
        if (callback) {
            callback.resolve(data);
            this.pendingCallbacks.delete(key);
        }
    }

    /**
     * Request a specific mesh from the worker
     */
    async getMesh(index) {
        return new Promise((resolve, reject) => {
            this.pendingCallbacks.set(`mesh_${index}`, { resolve, reject });
            this.worker.postMessage({ type: 'getMesh', index });
        });
    }

    /**
     * Request a specific texture from the worker
     */
    async getTexture(index) {
        return new Promise((resolve, reject) => {
            this.pendingCallbacks.set(`texture_${index}`, { resolve, reject });
            this.worker.postMessage({ type: 'getTexture', index });
        });
    }

    /**
     * Dispose worker resources
     */
    dispose() {
        if (this.worker) {
            this.worker.postMessage({ type: 'dispose' });
            this.worker.terminate();
            this.worker = null;
            this.workerReady = false;
        }
    }
}

/**
 * Result wrapper for worker-loaded USD data
 * Provides similar interface to the synchronous loader result
 */
class WorkerLoadResult {
    constructor(data, loader) {
        this._data = data;
        this._loader = loader;
    }

    numMeshes() {
        return this._data.numMeshes;
    }

    numPoints() {
        return this._data.numPoints || 0;
    }

    numCurves() {
        return this._data.numCurves || 0;
    }

    numNodes() {
        return this._data.numNodes || 0;
    }

    numMaterials() {
        return this._data.numMaterials;
    }

    numTextures() {
        return this._data.numTextures;
    }

    numLights() {
        return this._data.numLights;
    }

    numCameras() {
        return this._data.numCameras || 0;
    }

    numPointInstancers() {
        return this._data.numPointInstancers || 0;
    }

    numPointInstanceDraws() {
        return this._data.numPointInstanceDraws || 0;
    }

    numSkeletons() {
        return this._data.numSkeletons || 0;
    }

    numUnsupportedRenderables() {
        return this._data.numUnsupportedRenderables || 0;
    }

    numAnimations() {
        return this._data.numAnimations || 0;
    }

    numImages() {
        return this._data.numImages || 0;
    }

    getMesh(index) {
        return this._data.meshes[index];
    }

    getMeshCopy(index) {
        return this.getMesh(index);
    }

    getPoints(index) {
        return this._data.points ? this._data.points[index] : null;
    }

    getCurves(index) {
        return this._data.curves ? this._data.curves[index] : null;
    }

    getMaterial(index) {
        return this._data.materials[index];
    }

    /**
     * Get material in specified format (for compatibility with native loader)
     * @param {number} index - Material index
     * @param {string} format - Format ('json' supported)
     * @returns {{ data: string, error: string }}
     */
    getMaterialWithFormat(index, format = 'json') {
        const material = this._data.materials[index];
        if (!material) {
            return { data: null, error: `Material ${index} not found` };
        }
        if (format === 'json') {
            // Material is already a plain JS object (extracted with getMaterialWithFormat in worker)
            const jsonStr = JSON.stringify(material);
            return { data: jsonStr, error: null };
        }
        return { data: null, error: `Unsupported format: ${format}` };
    }

    getTexture(index) {
        return this._data.textures[index];
    }

    getImage(index) {
        return this._data.images ? this._data.images[index] : null;
    }

    getImageCopy(index) {
        return this.getImage(index);
    }

    getLight(index) {
        return this._data.lights[index];
    }

    getNode(index) {
        return this._data.nodes ? this._data.nodes[index] : null;
    }

    getCamera(index) {
        return this._data.cameras ? this._data.cameras[index] : null;
    }

    getPointInstancer(index) {
        return this._data.pointInstancers ? this._data.pointInstancers[index] : null;
    }

    getPointInstanceDraw(index) {
        return this._data.pointInstanceDraws ? this._data.pointInstanceDraws[index] : null;
    }

    getSkeleton(index) {
        return this._data.skeletons ? this._data.skeletons[index] : null;
    }

    getAnimation(index) {
        return this._data.animations ? this._data.animations[index] : null;
    }

    getAnimationInfo(index) {
        const infos = this.getAllAnimationInfos();
        if (!Number.isFinite(index) || index < 0 || index >= infos.length) {
            return null;
        }
        return infos[index];
    }

    getAllAnimationInfos() {
        return this._data.animationInfos || [];
    }

    getAllAnimations() {
        return this._data.animations || [];
    }

    getUnsupportedRenderables() {
        return this._data.unsupportedRenderables || [];
    }

    numRootNodes() {
        return this._data.rootNode ? 1 : 0;
    }

    getRootNode(index) {
        return index === 0 ? this.getDefaultRootNode() : null;
    }

    getDefaultRootNode() {
        return this._data.rootNode;
    }

    getUpAxis() {
        return this._data.upAxis;
    }

    getSceneMetadata() {
        return this._data.metadata;
    }

    /**
     * Get all meshes at once
     */
    getAllMeshes() {
        return this._data.meshes;
    }

    /**
     * Get all materials at once
     */
    getAllMaterials() {
        return this._data.materials;
    }

    /**
     * Get all textures at once
     */
    getAllTextures() {
        return this._data.textures;
    }

    /**
     * Get all images at once
     */
    getAllImages() {
        return this._data.images || [];
    }
}

export default TinyUSDZWorkerLoader;
