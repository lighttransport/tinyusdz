import { Loader } from 'three'; // or https://cdn.jsdelivr.net/npm/three/build/three.module.js';

// tinyusdz module are dynamically imported at TinyUSDZLoader

// Simple fileLoader both works for nodejs and the browser.
class FileFetcher {
  constructor() {
    this.is_node = typeof process !== "undefined" &&
      process.versions != null &&
      process.versions.node != null;

    this.fs = null;
    this.path = null;
    this.initialized = false;
  }

  async init() {
    if (this.initialized) return;
    
    if (this.is_node) {
      try {
        const { createRequire } = await import("module");
        const require = createRequire(import.meta.url);
        this.fs = require('fs');
        this.path = require('path');
      } catch (error) {
        console.warn('Failed to initialize Node.js modules:', error);
        this.is_node = false;
      }
    }
    this.initialized = true;
  }

  // Return: Object with arrayBuffer() method that returns Promise<ArrayBuffer>
  async fetch(url) {
    await this.init();

    // Check if this is a blob URL - always use fetch for blob URLs
    const isBlobUrl = url.startsWith('blob:');

    if (this.is_node && !isBlobUrl) {
      // Node.js environment - use fs.readFileSync for file paths
      try {
        if (url.startsWith('file://')) {
          url = url.substring(7); // Remove file:// prefix
        }

        const data = this.fs.readFileSync(url);

        // Return an object with arrayBuffer() method for consistency with browser File API
        // Convert Node.js Buffer to ArrayBuffer
        const arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);

        return {
          arrayBuffer: async () => arrayBuffer
        };
      } catch (error) {
        throw new Error(`Failed to read file: ${url} - ${error.message}`);
      }
    } else {
      // Browser environment or blob URL - use XMLHttpRequest for better large file handling
      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.responseType = 'arraybuffer';

        xhr.onload = function() {
          if (xhr.status === 200 || xhr.status === 206) {
            const arrayBuffer = xhr.response;
            // Return object with arrayBuffer method for consistency
            resolve({
              arrayBuffer: async () => arrayBuffer
            });
          } else {
            reject(new Error(`Failed to fetch: ${xhr.statusText}`));
          }
        };

        xhr.onerror = function() {
          reject(new Error(`Network error fetching: ${url}`));
        };

        xhr.send();
      });
    }
  }
}

class FetchAssetResolver {
    constructor() {
        this.assetCache = new Map();

        this.fetcher = new FileFetcher();
    }

    async resolveAsync(uri) {
        try {
            const response = await fetch(uri, {
                cache: 'no-store',
                headers: {
                    'Accept': '*/*',
                }
            });
            if (!response.ok && response.status !== 206) {
                throw new Error(`Failed to fetch asset: ${uri}`);
            }
            const data = await response.arrayBuffer();
            this.assetCache.set(uri, data);
            return Promise.resolve([uri, data]);
        } catch (error) {
            console.error(`Error resolving asset ${uri}:`, error);
            throw error;
        }
    }

    getAsset(uri) {
        if (this.assetCache.has(uri)) {
            return this.assetCache.get(uri);
        } else {
            console.warn(`Asset not found in cache: ${uri}`);
            return null;
        }
    }

    hasAsset(uri) {
        return this.assetCache.has(uri);
    }

    setAsset(uri, data) {
        this.assetCache.set(uri, data);
    }

    clearCache() {
        this.assetCache.clear();
    }

}

// TODO
//
// Polish API
//
class TinyUSDZLoader extends Loader {

    /**
     * Constructor for TinyUSDZLoader
     * @param {*} manager - THREE.js manager
     * @param {Object} options - Configuration options
     * @param {number} options.maxMemoryLimitMB - Maximum memory limit in MB (default: 2048 for WASM32, 8192 for WASM64)
     * @param {boolean} options.useZstdCompressedWasm - Use compressed WASM (default: false)
     * @param {Function} options.onTydraProgress - Callback for Tydra conversion progress ({meshCurrent, meshTotal, stage, meshName, progress}) => void
     * @param {Function} options.onTydraStage - Callback for Tydra stage changes ({stage, message}) => void
     * @param {Function} options.onTydraComplete - Callback for Tydra conversion completion ({meshCount, materialCount, textureCount}) => void
     */
    constructor(manager, options = {}) {
        super(manager);

        this.native_ = null;

        this.assetResolver_ = null;

        // texture loader callback
        // null = Use TinyUSDZ's builtin image loader(C++ native module)
        //this.texLoader = null;


        this.imageCache = {};
        this.textureCache = {};

        this.fetcher = new FileFetcher();

        // Default: do NOT use zstd compressed WASM.
        this.useZstdCompressedWasm_ = options.useZstdCompressedWasm || false;
        this.compressedWasmPath_ = 'tinyusdz.wasm.zst';

        this.useMemory64_ = false;
        this.compressedWasm64Path_ = 'tinyusdz_64.wasm.zst';


        // Memory limit in MB - defaults are set by the native module based on WASM architecture
        // (2GB for WASM32, 8GB for WASM64). If not specified, the native default will be used.
        this.maxMemoryLimitMB_ = options.maxMemoryLimitMB;

        // EM_JS synchronous progress callbacks for Tydra conversion
        // These are called directly from C++ during conversion without ASYNCIFY
        this.onTydraProgress_ = options.onTydraProgress || null;
        this.onTydraStage_ = options.onTydraStage || null;
        this.onTydraComplete_ = options.onTydraComplete || null;
    }

    // Decompress zstd compressed WASM
    async decompressZstdWasm(compressedPath) {
        try {
            const fzstd = await import('fzstd');

            const wasmURL = new URL(compressedPath, import.meta.url).href;

            const response = await fetch(wasmURL);
            if (!response.ok) {
                throw new Error(`Failed to fetch compressed WASM: ${response.statusText}`);
            }

            const compressedData = await response.arrayBuffer();

            if (compressedData.byteLength < 1024*64) {
                throw new Error('Compressed WASM size is unusually small, may not be valid zstd compressed data.');
            }

            // Check zstd magic number (0x28B52FFD in little-endian)
            const magicBytes = new Uint8Array(compressedData, 0, 4);
            const expectedMagic = [0x28, 0xB5, 0x2F, 0xFD]; // Little-endian representation
            
            if (compressedData.byteLength < 4 || 
                magicBytes[0] !== expectedMagic[0] || 
                magicBytes[1] !== expectedMagic[1] || 
                magicBytes[2] !== expectedMagic[2] || 
                magicBytes[3] !== expectedMagic[3]) {
                throw new Error('Invalid zstd file: magic number mismatch');
            }

            // Decompress using zstd
            const decompressedData = fzstd.decompress(new Uint8Array(compressedData));

            return decompressedData;
        } catch (error) {
            console.error('Error decompressing zstd WASM:', error);
            throw error;
        }
    }

    // Initialize the native WASM module
    // This is async but the load() method handles it internally with promises
    async init( options = {}) {

        if (Object.prototype.hasOwnProperty.call(options, 'useZstdCompressedWasm')) {
          this.useZstdCompressedWasm_ = options.useZstdCompressedWasm;
        }

        if (Object.prototype.hasOwnProperty.call(options, 'useMemory64')) {
          this.useMemory64_ = options.useMemory64;
        }

        if (!this.native_) {
          
            // WASM module of TinyUSDZ.
            const url = new URL(import.meta.url);

            //let initTinyUSDZNative = null;
          

            let use_memory64 = this.useMemory64_;
            if (url.searchParams.get("memory64") == "true") {
              use_memory64 = true;
            }


            let initTinyUSDZNative = null;

            // Use dynamic import based on memory64 parameter
            if (use_memory64) {
                const module = await import('./tinyusdz_64.js');
                initTinyUSDZNative = module.default;
            } else {
                const module = await import('./tinyusdz.js');
                initTinyUSDZNative = module.default;
            }

            let wasmBinary = null;

            if (this.useZstdCompressedWasm_) {
                // Load and decompress zstd compressed WASM
                wasmBinary = await this.decompressZstdWasm(use_memory64 ? this.compressedWasm64Path_ : this.compressedWasmPath_);

            }

            // Initialize with custom WASM binary if decompressed
            const initOptions = {}
            if (wasmBinary) {
              initOptions.wasmBinary = wasmBinary;
            }
            //initOptions.locateFile = function(path, scriptDirectory) {
            //  // Redirect WASM file loading to your custom file
            //  if (path.endsWith('.wasm')) {
            //    return './src/tinyusdz/tinyusdz_64.wasm';
            //  }
            //  return scriptDirectory + path;
            //}

            // Set up EM_JS synchronous progress callbacks
            // These are called from C++ during Tydra conversion without ASYNCIFY
            if (this.onTydraProgress_) {
              initOptions.onTydraProgress = this.onTydraProgress_;
            }
            if (this.onTydraStage_) {
              initOptions.onTydraStage = this.onTydraStage_;
            }
            if (this.onTydraComplete_) {
              initOptions.onTydraComplete = this.onTydraComplete_;
            }

            this.native_ = await initTinyUSDZNative(initOptions);
            if (!this.native_) {
                throw new Error('TinyUSDZLoader: Failed to initialize native module.');
            }
        }
        return this;
    }

    /**
     * Set maximum memory limit for USD loading in MB
     * @param {number} limitMB - Memory limit in megabytes
     */
    setMaxMemoryLimitMB(limitMB) {
        if (typeof limitMB !== 'number' || limitMB <= 0) {
            throw new Error('Memory limit must be a positive number');
        }
        this.maxMemoryLimitMB_ = limitMB;
    }

    /**
     * Set progress callback for load operations
     * Note: Due to WASM synchronous execution, progress updates are limited.
     * For true async progress, use loadWithProgressAsync() or Web Workers.
     * @param {Function} callback - Progress callback (progress: 0-1, stage: string) => void
     */
    setProgressCallback(callback) {
        this.progressCallback_ = callback;
    }

    /**
     * Clear the progress callback
     */
    clearProgressCallback() {
        this.progressCallback_ = null;
    }

    /**
     * Set Tydra progress callback for mesh conversion updates
     * This is called synchronously from C++ via EM_JS during scene conversion
     * @param {Function} callback - ({meshCurrent, meshTotal, stage, meshName, progress}) => void
     */
    setTydraProgressCallback(callback) {
        this.onTydraProgress_ = callback;
        // Update native module if already initialized
        if (this.native_) {
            this.native_.onTydraProgress = callback;
        }
    }

    /**
     * Set Tydra stage callback for conversion stage changes
     * @param {Function} callback - ({stage, message}) => void
     */
    setTydraStageCallback(callback) {
        this.onTydraStage_ = callback;
        if (this.native_) {
            this.native_.onTydraStage = callback;
        }
    }

    /**
     * Set Tydra completion callback
     * @param {Function} callback - ({meshCount, materialCount, textureCount}) => void
     */
    setTydraCompleteCallback(callback) {
        this.onTydraComplete_ = callback;
        if (this.native_) {
            this.native_.onTydraComplete = callback;
        }
    }

    /**
     * Get current maximum memory limit in MB
     * @returns {number|undefined} Memory limit in megabytes, or undefined if using native default
     */
    getMaxMemoryLimitMB() {
        return this.maxMemoryLimitMB_;
    }

    /**
     * Get the native default memory limit in MB
     * @returns {Promise<number>} Native default memory limit in megabytes
     */
    async getNativeDefaultMemoryLimitMB() {
        if (!this.native_) {
            await this.init();
        }
        const tempUsd = new this.native_.TinyUSDZLoaderNative();
        return tempUsd.getMaxMemoryLimitMB();
    }

    /**
     * Enable or disable bone reduction for skeletal meshes
     * @param {boolean} enabled - Enable bone reduction
     */
    setEnableBoneReduction(enabled) {
        this.enableBoneReduction_ = !!enabled;
    }

    /**
     * Get bone reduction enabled status
     * @returns {boolean} True if bone reduction is enabled
     */
    getEnableBoneReduction() {
        return this.enableBoneReduction_ || false;
    }

    /**
     * Set target bone count for bone reduction
     * @param {number} count - Target number of bone influences per vertex (1-128)
     */
    setTargetBoneCount(count) {
        if (typeof count !== 'number' || count < 1 || count > 128) {
            throw new Error('Target bone count must be between 1 and 128');
        }
        this.targetBoneCount_ = count;
    }

    /**
     * Get target bone count for bone reduction
     * @returns {number} Target bone count (default: 4)
     */
    getTargetBoneCount() {
        return this.targetBoneCount_ || 4;
    }

    /**
     * Enable or disable bone count rounding (round up to standard GPU skinning values)
     * When enabled, bone counts are rounded up to: 4, 8, 16, 32, 48, 64, 80, 96, 128
     * This keeps all bone influences but pads to standard sizes for GPU compatibility
     * @param {boolean} enabled - Enable bone count rounding
     */
    setRoundBoneCount(enabled) {
        this.roundBoneCount_ = !!enabled;
    }

    /**
     * Get bone count rounding setting
     * @returns {boolean} Whether bone count rounding is enabled
     */
    getRoundBoneCount() {
        return this.roundBoneCount_ || false;
    }

    /**
     * Apply configured skinning options to a native USD loader instance.
     * @param {Object} usd - TinyUSDZLoaderNative instance
     * @private
     */
    _applySkinningLoadOptions(usd) {
        if (!usd) return;
        if (this.enableBoneReduction_) {
            usd.setEnableBoneReduction(true);
            usd.setTargetBoneCount(this.targetBoneCount_ || 4);
        } else if (this.roundBoneCount_) {
            usd.setRoundBoneCount(true);
        }
    }

    // TODO: remove
    // Set AssetResolver callback.
    // This is used to resolve asset paths(e.g. textures, usd files) in the USD.
    // For web app, usually we'll convert asset path to URI
    //setAssetResolver(callback) {
    //    this.assetResolver_ = callback;
    //}

    /**
     * Create a progress event object (GLTFLoader compatible + extended)
     * @private
     */
    _createProgressEvent(loaded, total, stage, message) {
        return {
            // GLTFLoader compatible fields
            loaded: loaded,
            total: total,
            // Extended fields
            stage: stage,
            percentage: total > 0 ? (loaded / total) * 100 : 0,
            message: message
        };
    }

    /**
     * Load a USDZ/USDA/USDC file from a URL as USD Stage(Freezed scene graph)
     * NOTE: for loadAsync(), Use base Loader class's loadAsync() method
     * @param {string} url - URL to load from
     * @param {Function} onLoad - Success callback
     * @param {Function} onProgress - Progress callback with GLTFLoader-compatible event {loaded, total, stage, percentage, message}
     * @param {Function} onError - Error callback
     * @param {Object} options - Loading options
     * @param {number} options.maxMemoryLimitMB - Override memory limit for this load
     */
    load(url, onLoad, onProgress, onError, options = {}) {
        const scope = this;

        // Create a promise chain to handle initialization and loading
        const initPromise = this.native_ ? Promise.resolve() : this.init();

        initPromise
            .then(async () => {
                // Use fetch with progress tracking if onProgress is provided
                if (onProgress) {
                    return scope._fetchWithProgress(url, onProgress);
                } else {
                    // Fallback to simple fetch without progress
                    const response = await scope.fetcher.fetch(url);
                    return await response.arrayBuffer();
                }
            })
            .then((usd_data) => {
                const usd_binary = new Uint8Array(usd_data);

                // Report parsing stage
                if (onProgress) {
                    onProgress(scope._createProgressEvent(0, 1, 'parsing', 'Parsing USD...'));
                }

                scope.parse(usd_binary, url, function (usd) {
                    // Report complete
                    if (onProgress) {
                        onProgress(scope._createProgressEvent(1, 1, 'complete', 'Complete'));
                    }
                    onLoad(usd);
                }, onError, options);

            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error loading USD:', error);
                if (onError) {
                    onError(error);
                }
            });
    }

    /**
     * Fetch URL with progress reporting via ReadableStream
     * @private
     */
    async _fetchWithProgress(url, onProgress) {
        const response = await fetch(url);
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.statusText}`);
        }

        // Get content length for progress calculation
        const contentLength = response.headers.get('content-length');
        const total = contentLength ? parseInt(contentLength, 10) : 0;

        // If no content-length, fall back to simple fetch
        if (total === 0) {
            onProgress(this._createProgressEvent(0, 0, 'downloading', 'Downloading...'));
            const data = await response.arrayBuffer();
            onProgress(this._createProgressEvent(data.byteLength, data.byteLength, 'downloading', 'Download complete'));
            return data;
        }

        // Read response with progress via ReadableStream
        const reader = response.body.getReader();
        const chunks = [];
        let loaded = 0;

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            chunks.push(value);
            loaded += value.length;

            // Report download progress
            const percentage = Math.round((loaded / total) * 100);
            onProgress(this._createProgressEvent(
                loaded,
                total,
                'downloading',
                `Downloading... ${percentage}%`
            ));
        }

        // Combine chunks into single ArrayBuffer
        const result = new Uint8Array(loaded);
        let offset = 0;
        for (const chunk of chunks) {
            result.set(chunk, offset);
            offset += chunk.length;
        }

        return result.buffer;
    }

    /**
     * Parse a USDZ/USDA/USDC binary data
     * @param {ArrayBuffer} binary - Binary USD data
     * @param {string} filePath - Optional file path
     * @param {Function} onLoad - Success callback
     * @param {Function} onError - Error callback
     * @param {Object} options - Parsing options
     * @param {number} options.maxMemoryLimitMB - Override memory limit for this parse
     */
    parse(binary /* ArrayBuffer */, filePath /* optional */, onLoad, onError, options = {}) {

        const _onError = function (e) {

            if (onError) {

                onError(e);

            } else {

                console.error(e);

            }

            //scope.manager.itemError( url );
            //scope.manager.itemEnd( url );

        };

        if (!this.native_) {
            console.error('TinyUSDZLoader: Native module is not initialized.');
            _onError(new Error('TinyUSDZLoader: Native module is not initialized.'));
        }

        const usd = new this.native_.TinyUSDZLoaderNative();

        // Set memory limit before loading if specified (otherwise use native default)
        const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
        if (memoryLimit !== undefined) {
            usd.setMaxMemoryLimitMB(memoryLimit);
        }

        this._applySkinningLoadOptions(usd);

        let ok;
        try {
            ok = usd.loadFromBinary(binary, filePath);
        } catch (e) {
            // Catch WASM traps (e.g. Emscripten OOM abort, unreachable instruction)
            _onError(e instanceof Error ? e : new Error(String(e)));
            return;
        }
        if (!ok) {
            const fileInfo = filePath ? ` (file: ${filePath})` : '';
            _onError(new Error(`TinyUSDZLoader: Failed to load USD from binary data${fileInfo}.`, {cause: usd.error()}));
        } else {
            onLoad(usd);
        }
    }

    /**
     * Parse USD binary data with progress reporting
     * Uses the native progress callback mechanism for cancellation support.
     *
     * Note: Due to WASM being synchronous, progress updates only occur at
     * checkpoint boundaries in the parser. The onProgress callback is called
     * after parsing completes with final status information.
     *
     * For true async progress reporting in the UI, use Web Workers.
     *
     * @param {ArrayBuffer} binary - Binary USD data
     * @param {string} filePath - Optional file path
     * @param {Object} options - Parsing options
     * @param {number} options.maxMemoryLimitMB - Override memory limit
     * @param {Function} options.onProgress - Progress callback (progressInfo) => void
     * @param {AbortSignal} options.signal - AbortController signal for cancellation
     * @returns {Promise<Object>} Parsed USD object
     */
    async parseWithProgress(binary /* ArrayBuffer */, filePath /* optional */, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        return new Promise((resolve, reject) => {
            const usd = new this.native_.TinyUSDZLoaderNative();

            // Set memory limit before loading if specified
            const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
            if (memoryLimit !== undefined) {
                usd.setMaxMemoryLimitMB(memoryLimit);
            }

            this._applySkinningLoadOptions(usd);

            // Handle AbortController cancellation
            if (options.signal) {
                if (options.signal.aborted) {
                    reject(new DOMException('Parsing aborted', 'AbortError'));
                    return;
                }
                options.signal.addEventListener('abort', () => {
                    usd.cancelParsing();
                });
            }

            // Reset progress state
            usd.resetProgress();

            // Report initial progress
            if (options.onProgress) {
                options.onProgress({
                    progress: 0,
                    stage: 'parsing',
                    percentage: 0,
                    message: 'Starting parse...'
                });
            }

            // Use setTimeout to allow UI to update before blocking parse
            setTimeout(() => {
                try {
                    const ok = usd.loadFromBinaryWithProgress(binary, filePath);

                    // Get final progress state
                    const progressInfo = usd.getProgress();

                    // Report final progress
                    if (options.onProgress) {
                        options.onProgress(progressInfo);
                    }

                    if (!ok) {
                        if (usd.wasCancelled()) {
                            reject(new DOMException('Parsing cancelled', 'AbortError'));
                        } else {
                            const fileInfo = filePath ? ` (file: ${filePath})` : '';
                            reject(new Error(`TinyUSDZLoader: Failed to load USD from binary data${fileInfo}.`, {cause: usd.error()}));
                        }
                    } else {
                        resolve(usd);
                    }
                } catch (e) {
                    reject(e);
                }
            }, 0);
        });
    }

    /**
     * Check if C++20 coroutine-based async loading is available.
     * Returns true if the WASM module was compiled with TINYUSDZ_WASM_COROUTINE=ON.
     *
     * @returns {boolean} True if async support is available
     */
    hasAsyncSupport() {
        if (!this.native_) {
            console.warn('[TinyUSDZLoader] hasAsyncSupport called before init()');
            return false;
        }

        // Check if a temporary instance has loadFromBinaryAsync
        try {
            const usd = new this.native_.TinyUSDZLoaderNative();
            const hasMethod = typeof usd.loadFromBinaryAsync === 'function';
            console.log(`[TinyUSDZLoader] Coroutine async support: ${hasMethod ? 'available' : 'not available'}`);
            return hasMethod;
        } catch (e) {
            console.warn('[TinyUSDZLoader] Error checking async support:', e);
            return false;
        }
    }

    /**
     * Parse USD binary data using C++20 coroutine-based async loading.
     * This method yields to the JavaScript event loop between processing phases,
     * allowing the browser to repaint during loading.
     *
     * Unlike parseWithProgress which uses setTimeout for a single yield,
     * this method uses true C++20 coroutines with co_await to yield multiple times
     * during parsing (detecting -> parsing -> converting -> complete).
     *
     * @param {ArrayBuffer} binary - Binary USD data
     * @param {string} filePath - Optional file path
     * @param {Object} options - Parsing options
     * @param {number} options.maxMemoryLimitMB - Override memory limit
     * @param {Function} options.onPhaseStart - Callback when a phase starts ({phase, progress}) => void
     * @returns {Promise<Object>} Parsed USD object
     */
    async parseAsync(binary /* ArrayBuffer */, filePath /* optional */, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        const usd = new this.native_.TinyUSDZLoaderNative();

        // Set memory limit before loading if specified
        const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
        if (memoryLimit !== undefined) {
            usd.setMaxMemoryLimitMB(memoryLimit);
        }

        this._applySkinningLoadOptions(usd);

        // Set up async phase callback on Module if provided
        if (options.onPhaseStart) {
            this.native_.onAsyncPhaseStart = options.onPhaseStart;
        }

        try {
            // Check if the C++20 coroutine-based async loader is available
            if (typeof usd.loadFromBinaryAsync === 'function') {
                // Call the C++20 coroutine-based async loader
                // This returns a Promise that resolves to { success, error?, meshCount?, materialCount?, textureCount? }
                const result = await usd.loadFromBinaryAsync(binary, filePath || '');

                if (!result.success) {
                    throw new Error(`TinyUSDZLoader: Failed to load USD: ${result.error || 'unknown error'}`);
                }
            } else {
                // Fall back to synchronous loading
                const ok = usd.loadFromBinary(binary, filePath || '');
                if (!ok) {
                    throw new Error(`TinyUSDZLoader: Failed to load USD: ${usd.error()}`);
                }
            }

            return usd;
        } finally {
            // Clean up callback
            if (options.onPhaseStart) {
                this.native_.onAsyncPhaseStart = null;
            }
        }
    }

    /**
     * Load USD file from URL using C++20 coroutine-based async loading.
     * This method yields to the JavaScript event loop during processing,
     * allowing the browser to repaint.
     *
     * @param {string} url - URL to load from
     * @param {Object} options - Loading options
     * @param {number} options.maxMemoryLimitMB - Override memory limit
     * @param {Function} options.onPhaseStart - Callback when a phase starts ({phase, progress}) => void
     * @param {Function} options.onFetchProgress - Fetch progress callback (loaded, total) => void
     * @returns {Promise<Object>} Parsed USD object
     */
    async loadAsync(url, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        // Fetch the file with progress
        const response = await fetch(url);
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.statusText}`);
        }

        // Get content length for progress calculation
        const contentLength = response.headers.get('content-length');
        const total = contentLength ? parseInt(contentLength, 10) : 0;

        // Read response with progress
        const reader = response.body.getReader();
        const chunks = [];
        let loaded = 0;

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            chunks.push(value);
            loaded += value.length;

            if (options.onFetchProgress) {
                options.onFetchProgress(loaded, total);
            }
        }

        // Combine chunks
        const binary = new Uint8Array(loaded);
        let offset = 0;
        for (const chunk of chunks) {
            binary.set(chunk, offset);
            offset += chunk.length;
        }

        // Parse using coroutine-based async method
        return this.parseAsync(binary, url, options);
    }

    /**
     * Load USD file from URL with progress reporting
     *
     * @param {string} url - URL to load from
     * @param {Object} options - Loading options
     * @param {number} options.maxMemoryLimitMB - Override memory limit
     * @param {Function} options.onProgress - Progress callback (progressInfo) => void
     * @param {Function} options.onFetchProgress - Fetch progress callback (loaded, total) => void
     * @param {AbortSignal} options.signal - AbortController signal for cancellation
     * @returns {Promise<Object>} Parsed USD object
     */
    async loadWithProgress(url, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        // Check for cancellation before starting
        if (options.signal?.aborted) {
            throw new DOMException('Loading aborted', 'AbortError');
        }

        // Fetch the file with progress
        const response = await fetch(url, { signal: options.signal });
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.statusText}`);
        }

        // Get content length for progress calculation
        const contentLength = response.headers.get('content-length');
        const total = contentLength ? parseInt(contentLength, 10) : 0;

        // Read response with progress
        const reader = response.body.getReader();
        const chunks = [];
        let loaded = 0;

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            chunks.push(value);
            loaded += value.length;

            if (options.onFetchProgress) {
                options.onFetchProgress(loaded, total);
            }
            if (options.onProgress) {
                options.onProgress({
                    progress: total > 0 ? (loaded / total) * 0.3 : 0, // Fetch is ~30% of total
                    stage: 'fetching',
                    percentage: total > 0 ? (loaded / total) * 30 : 0,
                    message: `Downloading... ${Math.round(loaded / 1024)}KB`
                });
            }
        }

        // Combine chunks
        const totalLength = chunks.reduce((acc, chunk) => acc + chunk.length, 0);
        const binary = new Uint8Array(totalLength);
        let offset = 0;
        for (const chunk of chunks) {
            binary.set(chunk, offset);
            offset += chunk.length;
        }

        // Parse with progress
        const parseOptions = {
            ...options,
            onProgress: options.onProgress ? (info) => {
                // Adjust progress to account for fetch phase
                const adjustedProgress = 0.3 + (info.progress * 0.7);
                options.onProgress({
                    ...info,
                    progress: adjustedProgress,
                    percentage: adjustedProgress * 100,
                    message: info.stage === 'complete' ? 'Complete' : `${info.currentOperation || info.stage}`
                });
            } : undefined
        };

        return this.parseWithProgress(binary, url, parseOptions);
    }

    /**
     * Get the current progress state from a USD loader instance
     * @param {Object} usd - TinyUSDZLoaderNative instance
     * @returns {Object} Progress information
     */
    static getProgress(usd) {
        if (usd && typeof usd.getProgress === 'function') {
            return usd.getProgress();
        }
        return null;
    }

    /**
     * Request cancellation of parsing on a USD loader instance
     * @param {Object} usd - TinyUSDZLoaderNative instance
     */
    static cancelParsing(usd) {
        if (usd && typeof usd.cancelParsing === 'function') {
            usd.cancelParsing();
        }
    }

    // ============================================================
    // Streaming Transfer Methods for Memory-Efficient Loading
    // ============================================================

    /**
     * Check if ReadableStreamBYOBReader is available in this environment
     * @returns {boolean} True if BYOB reader is supported
     */
    static supportsBYOBReader() {
        try {
            return typeof ReadableStreamBYOBReader !== 'undefined';
        } catch {
            return false;
        }
    }

    /**
     * Stream fetch data directly to WASM memory with minimal JS memory footprint.
     * Uses zero-copy transfer where chunks are written directly to pre-allocated WASM buffer.
     * Each JS chunk is freed immediately after transfer to minimize memory usage.
     *
     * @param {string} url - URL to fetch
     * @param {string} assetPath - Asset path/identifier for the cache
     * @param {Object} options - Options
     * @param {AbortSignal} options.signal - AbortController signal for cancellation
     * @param {Function} options.onProgress - Progress callback (bytesLoaded, totalBytes) => void
     * @param {number} options.chunkSize - Preferred chunk size in bytes (default: 64KB)
     * @param {Object} options.usdInstance - Optional existing TinyUSDZLoaderNative instance to use
     * @returns {Promise<{success: boolean, bytesTransferred: number, assetPath: string, usdInstance: Object}>}
     */
    async streamFetchToWasm(url, assetPath, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        const usd = options.usdInstance || new this.native_.TinyUSDZLoaderNative();
        const chunkSize = options.chunkSize || 64 * 1024; // 64KB default

        // Check for cancellation before starting
        if (options.signal?.aborted) {
            throw new DOMException('Stream transfer aborted', 'AbortError');
        }

        // Start the fetch
        const response = await fetch(url, { signal: options.signal });
        if (!response.ok) {
            throw new Error(`Failed to fetch: ${response.statusText}`);
        }

        // Get total size if available
        const contentLength = response.headers.get('content-length');
        const totalSize = contentLength ? parseInt(contentLength, 10) : 0;

        if (totalSize === 0) {
            // If content-length is not available, fall back to buffered approach
            return this._streamFetchBuffered(response, assetPath, usd, options);
        }

        // Allocate WASM buffer upfront
        // Returns UUID for buffer operations, asset_name stored inside for cache key
        const allocResult = usd.allocateZeroCopyBuffer(assetPath, totalSize);
        if (!allocResult.success) {
            throw new Error('Failed to allocate WASM buffer for streaming: ' + (allocResult.error || 'unknown error'));
        }
        const uuid = allocResult.uuid;

        try {
            // Get base pointer to WASM buffer
            const basePtr = allocResult.bufferPtr;
            if (basePtr === 0) {
                throw new Error('Failed to get WASM buffer pointer');
            }

            // Get WASM heap reference
            const HEAPU8 = this.native_.HEAPU8;

            // Use BYOB reader if available for better performance
            if (TinyUSDZLoader.supportsBYOBReader() && response.body.getReader) {
                await this._streamWithBYOBReader(
                    response.body, basePtr, totalSize, HEAPU8, usd, uuid, options
                );
            } else {
                // Fall back to default reader
                await this._streamWithDefaultReader(
                    response.body, basePtr, totalSize, HEAPU8, usd, uuid, options
                );
            }

            // Finalize the buffer (moves to asset cache using stored assetPath)
            const success = usd.finalizeZeroCopyBuffer(uuid);
            if (!success) {
                throw new Error('Failed to finalize streaming buffer');
            }

            return {
                success: true,
                bytesTransferred: totalSize,
                assetPath: assetPath,
                usdInstance: usd
            };

        } catch (error) {
            // Cancel/cleanup the buffer on error
            usd.cancelZeroCopyBuffer(uuid);
            throw error;
        }
    }

    /**
     * Stream with BYOB (Bring Your Own Buffer) reader for efficient reads.
     * @private
     */
    async _streamWithBYOBReader(body, basePtr, totalSize, HEAPU8, usd, uuid, options) {
        const reader = body.getReader({ mode: 'byob' });
        let offset = 0;
        const chunkSize = options.chunkSize || 64 * 1024;

        try {
            while (offset < totalSize) {
                // Check for cancellation
                if (options.signal?.aborted) {
                    throw new DOMException('Stream transfer aborted', 'AbortError');
                }

                // Allocate buffer for this chunk
                const remainingBytes = totalSize - offset;
                const readSize = Math.min(chunkSize, remainingBytes);
                let buffer = new ArrayBuffer(readSize);
                let view = new Uint8Array(buffer);

                // Read into the buffer
                const { done, value } = await reader.read(view);
                if (done) break;

                // Write directly to WASM heap
                const bytesRead = value.byteLength;
                HEAPU8.set(value, basePtr + offset);

                // Mark bytes as written
                usd.markZeroCopyBytesWritten(uuid, bytesRead);
                offset += bytesRead;

                // Report progress
                if (options.onProgress) {
                    options.onProgress(offset, totalSize);
                }

                // Release the buffer (let GC reclaim it)
                buffer = null;
                view = null;
            }
        } finally {
            reader.releaseLock();
        }
    }

    /**
     * Stream with default reader (fallback when BYOB is not available).
     * @private
     */
    async _streamWithDefaultReader(body, basePtr, totalSize, HEAPU8, usd, uuid, options) {
        const reader = body.getReader();
        let offset = 0;

        try {
            while (true) {
                // Check for cancellation
                if (options.signal?.aborted) {
                    throw new DOMException('Stream transfer aborted', 'AbortError');
                }

                const { done, value } = await reader.read();
                if (done) break;

                // Write chunk directly to WASM heap
                const bytesRead = value.byteLength;
                HEAPU8.set(value, basePtr + offset);

                // Mark bytes as written
                usd.markZeroCopyBytesWritten(uuid, bytesRead);
                offset += bytesRead;

                // Report progress
                if (options.onProgress) {
                    options.onProgress(offset, totalSize);
                }

                // The chunk 'value' will be GC'd after this iteration
            }
        } finally {
            reader.releaseLock();
        }
    }

    /**
     * Fallback for when content-length is unknown - buffers in JS then transfers.
     * @private
     */
    async _streamFetchBuffered(response, assetPath, usd, options) {
        const reader = response.body.getReader();
        const chunks = [];
        let totalBytes = 0;

        // Read all chunks (we don't know the size upfront)
        while (true) {
            if (options.signal?.aborted) {
                throw new DOMException('Stream transfer aborted', 'AbortError');
            }

            const { done, value } = await reader.read();
            if (done) break;

            chunks.push(value);
            totalBytes += value.byteLength;

            if (options.onProgress) {
                options.onProgress(totalBytes, 0); // 0 = unknown total
            }
        }

        // Now allocate WASM buffer with known size
        const allocResult = usd.allocateZeroCopyBuffer(assetPath, totalBytes);
        if (!allocResult.success) {
            throw new Error('Failed to allocate WASM buffer: ' + (allocResult.error || 'unknown error'));
        }
        const uuid = allocResult.uuid;

        try {
            const basePtr = allocResult.bufferPtr;
            const HEAPU8 = this.native_.HEAPU8;

            // Transfer chunks to WASM and free them
            let offset = 0;
            for (let i = 0; i < chunks.length; i++) {
                const chunk = chunks[i];
                HEAPU8.set(chunk, basePtr + offset);
                usd.markZeroCopyBytesWritten(uuid, chunk.byteLength);
                offset += chunk.byteLength;

                // Clear reference to allow GC
                chunks[i] = null;
            }

            // Finalize
            const success = usd.finalizeZeroCopyBuffer(uuid);
            if (!success) {
                throw new Error('Failed to finalize streaming buffer');
            }

            return {
                success: true,
                bytesTransferred: totalBytes,
                assetPath: assetPath,
                usdInstance: usd
            };

        } catch (error) {
            usd.cancelZeroCopyBuffer(uuid);
            throw error;
        }
    }

    /**
     * Stream multiple assets to WASM in parallel with memory-efficient transfer.
     * Useful for loading USD files with multiple external references.
     *
     * @param {Array<{url: string, assetPath: string}>} assets - Array of assets to load
     * @param {Object} options - Options
     * @param {AbortSignal} options.signal - AbortController signal for cancellation
     * @param {Function} options.onProgress - Progress callback (completed, total, currentAsset) => void
     * @param {number} options.concurrency - Max concurrent downloads (default: 4)
     * @returns {Promise<Array<{success: boolean, assetPath: string, bytesTransferred: number}>>}
     */
    async streamFetchMultipleToWasm(assets, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        const concurrency = options.concurrency || 4;
        const results = [];
        let completed = 0;

        // Process assets in batches
        for (let i = 0; i < assets.length; i += concurrency) {
            if (options.signal?.aborted) {
                throw new DOMException('Stream transfer aborted', 'AbortError');
            }

            const batch = assets.slice(i, i + concurrency);
            const batchPromises = batch.map(async (asset) => {
                try {
                    const result = await this.streamFetchToWasm(asset.url, asset.assetPath, {
                        signal: options.signal,
                        onProgress: (loaded, total) => {
                            if (options.onAssetProgress) {
                                options.onAssetProgress(asset.assetPath, loaded, total);
                            }
                        }
                    });
                    completed++;
                    if (options.onProgress) {
                        options.onProgress(completed, assets.length, asset.assetPath);
                    }
                    return result;
                } catch (error) {
                    completed++;
                    if (options.onProgress) {
                        options.onProgress(completed, assets.length, asset.assetPath);
                    }
                    return {
                        success: false,
                        assetPath: asset.assetPath,
                        error: error.message
                    };
                }
            });

            const batchResults = await Promise.all(batchPromises);
            results.push(...batchResults);
        }

        return results;
    }

    /**
     * Load USD file with streaming transfer to minimize memory usage.
     * Combines streaming fetch with parsing.
     *
     * @param {string} url - URL to load
     * @param {Object} options - Options
     * @param {AbortSignal} options.signal - AbortController signal
     * @param {Function} options.onProgress - Progress callback
     * @param {number} options.maxMemoryLimitMB - Memory limit for parsing
     * @returns {Promise<Object>} Parsed USD object
     */
    async loadWithStreaming(url, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        // Extract filename for asset path
        const assetPath = url.split('/').pop() || 'main.usd';

        // Report streaming phase
        if (options.onProgress) {
            options.onProgress({
                progress: 0,
                stage: 'streaming',
                percentage: 0,
                message: 'Starting streaming transfer...'
            });
        }

        // Create USD instance for streaming and loading (same instance to share cache)
        const usd = new this.native_.TinyUSDZLoaderNative();

        // Set memory limit before streaming
        const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
        if (memoryLimit !== undefined) {
            usd.setMaxMemoryLimitMB(memoryLimit);
        }

        this._applySkinningLoadOptions(usd);

        // Stream fetch to WASM using the same instance
        const streamResult = await this.streamFetchToWasm(url, assetPath, {
            signal: options.signal,
            usdInstance: usd,
            onProgress: (loaded, total) => {
                if (options.onProgress) {
                    const progress = total > 0 ? (loaded / total) * 0.3 : 0;
                    options.onProgress({
                        progress,
                        stage: 'streaming',
                        percentage: progress * 100,
                        message: `Streaming... ${Math.round(loaded / 1024)}KB`
                    });
                }
            }
        });

        if (!streamResult.success) {
            throw new Error('Streaming transfer failed');
        }

        // Report parsing phase
        if (options.onProgress) {
            options.onProgress({
                progress: 0.3,
                stage: 'parsing',
                percentage: 30,
                message: 'Parsing USD...'
            });
        }

        // Load from the cached asset (same instance, so cache is available)
        const ok = usd.loadFromCachedAsset(assetPath);
        if (!ok) {
            throw new Error('Failed to parse USD from cached asset', { cause: usd.error() });
        }

        if (options.onProgress) {
            options.onProgress({
                progress: 1.0,
                stage: 'complete',
                percentage: 100,
                message: 'Complete'
            });
        }

        return usd;
    }

    /**
     * Stream a Node.js file to WASM memory with chunk-based transfer.
     * Uses fs.createReadStream for memory-efficient reading of large files.
     *
     * @param {string} filePath - Path to the file
     * @param {string} assetPath - Asset path/identifier for the cache
     * @param {Object} options - Options
     * @param {Function} options.onProgress - Progress callback (bytesLoaded, totalBytes) => void
     * @param {number} options.chunkSize - Read chunk size in bytes (default: 64KB)
     * @param {Object} options.usdInstance - Optional existing TinyUSDZLoaderNative instance to use
     * @returns {Promise<{success: boolean, bytesTransferred: number, assetPath: string, usdInstance: Object}>}
     */
    async streamFileToWasm(filePath, assetPath, options = {}) {
        if (!this.native_) {
            await this.init();
        }

        // Check if we're in Node.js
        const isNode = typeof process !== 'undefined' && process.versions?.node;
        if (!isNode) {
            throw new Error('streamFileToWasm is only available in Node.js environment');
        }

        const { createRequire } = await import('module');
        const require = createRequire(import.meta.url);
        const fs = require('fs');
        const { promisify } = require('util');
        const stat = promisify(fs.stat);

        // Get file size
        const stats = await stat(filePath);
        const totalSize = stats.size;

        const usd = options.usdInstance || new this.native_.TinyUSDZLoaderNative();

        // Allocate WASM buffer
        const allocResult = usd.allocateZeroCopyBuffer(assetPath, totalSize);
        if (!allocResult.success) {
            throw new Error('Failed to allocate WASM buffer: ' + (allocResult.error || 'unknown error'));
        }
        const uuid = allocResult.uuid;

        try {
            const basePtr = allocResult.bufferPtr;
            const HEAPU8 = this.native_.HEAPU8;
            const chunkSize = options.chunkSize || 64 * 1024;

            // Create read stream
            const stream = fs.createReadStream(filePath, { highWaterMark: chunkSize });

            let offset = 0;

            await new Promise((resolve, reject) => {
                stream.on('data', (chunk) => {
                    // Convert Buffer to Uint8Array and write to WASM
                    const uint8Chunk = new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
                    HEAPU8.set(uint8Chunk, basePtr + offset);
                    usd.markZeroCopyBytesWritten(uuid, chunk.byteLength);
                    offset += chunk.byteLength;

                    if (options.onProgress) {
                        options.onProgress(offset, totalSize);
                    }
                });

                stream.on('end', resolve);
                stream.on('error', reject);
            });

            // Finalize
            const success = usd.finalizeZeroCopyBuffer(uuid);
            if (!success) {
                throw new Error('Failed to finalize streaming buffer');
            }

            return {
                success: true,
                bytesTransferred: totalSize,
                assetPath: assetPath,
                usdInstance: usd
            };

        } catch (error) {
            usd.cancelZeroCopyBuffer(uuid);
            throw error;
        }
    }

    /**
     * Get information about active streaming buffers (for debugging/monitoring)
     * @returns {Promise<Array>} Array of active buffer info
     */
    async getActiveStreamingBuffers() {
        if (!this.native_) {
            await this.init();
        }

        const usd = new this.native_.TinyUSDZLoaderNative();
        return usd.getActiveZeroCopyBuffers();
    }

    /**
     * Load a USDZ/USDA/USDC file from a URL as USD Layer(for composition)
     * @param {string} url - URL to load from
     * @param {Function} onLoad - Success callback
     * @param {Function} onProgress - Progress callback
     * @param {Function} onError - Error callback
     * @param {Object} options - Loading options
     * @param {number} options.maxMemoryLimitMB - Override memory limit for this load
     */
    loadAsLayer(url, onLoad, onProgress, onError, options = {}) {

        const scope = this;

        const _onError = function (e) {

            if (onError) {

                onError(e);

            } else {

                console.error(e);

            }

            //scope.manager.itemError( url );
            //scope.manager.itemEnd( url );

        };


        // Create a promise chain to handle initialization and loading
        const initPromise = this.native_ ? Promise.resolve() : this.init();

        initPromise
            .then(() => {
                return fetch(url);
            })
            .then((response) => {
                console.log('fetch USDZ file done:', url);
                return response.arrayBuffer();
            })
            .then((usd_data) => {
                console.log('usd_data done:', url);
                const usd_binary = new Uint8Array(usd_data);

                //return this.parse(usd_binary);

                const usd = new this.native_.TinyUSDZLoaderNative();

                // Set memory limit before loading if specified (otherwise use native default)
                const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
                if (memoryLimit !== undefined) {
                    usd.setMaxMemoryLimitMB(memoryLimit);
                }

                scope._applySkinningLoadOptions(usd);

                const ok = usd.loadAsLayerFromBinary(usd_binary, url);
                if (!ok) {
                    _onError(new Error('TinyUSDZLoader: Failed to load USD as Layer from binary data. url: ' + url, {cause: usd.error()}));
                } else {
                    onLoad(usd);
                }

            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error initializing native module:', error);
                if (onError) {
                    onError(error);
                }
            });
    }

    async loadAsLayerAsync(url, onProgress, options = {}) {
     	const scope = this;

		return new Promise( function ( resolve, reject ) {

			scope.loadAsLayer( url, resolve, onProgress, reject, options );

		} );
    }

    loadTest(url, onLoad, onProgress, onError, options = {}) {

        const scope = this;

        const _onError = function (e) {

            if (onError) {

                onError(e);

            } else {

                console.error(e);

            }

            //scope.manager.itemError( url );
            //scope.manager.itemEnd( url );

        };


        // Create a promise chain to handle initialization and loading
        const initPromise = this.native_ ? Promise.resolve() : this.init();

        initPromise
            .then(() => {
                return fetch(url);
            })
            .then((response) => {
                return response.arrayBuffer();
            })
            .then((usd_data) => {

                const usd = new this.native_.TinyUSDZLoaderNative();

                // Set memory limit before loading if specified (otherwise use native default)
                const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
                if (memoryLimit !== undefined) {
                    usd.setMaxMemoryLimitMB(memoryLimit);
                }

                scope._applySkinningLoadOptions(usd);

                const u8data = new Uint8Array(usd_data);
                const ok = usd.loadTest(url, u8data);
                if (!ok) {
                    _onError(new Error('TinyUSDZLoader: Failed to load USD as Layer from binary data. url: ' + url, {cause: usd.error()}));
                } else {
                    onLoad(usd);
                }

            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error initializing native module:', error);
                if (onError) {
                    onError(error);
                }
            });
    }

    async loadTestAsync(url, onProgress, options = {}) {
     	const scope = this;

		return new Promise( function ( resolve, reject ) {

			scope.loadTest( url, resolve, onProgress, reject, options );

		} );
    }

    /**
     * Load USD with full progress reporting (Three.js GLTFLoader compatible)
     * Combines downloading, parsing, and optional scene building with unified progress.
     *
     * @param {string} url - URL to load from
     * @param {Function} onLoad - Success callback (result) => void
     *   - result.usd: The parsed USD object (TinyUSDZLoaderNative)
     *   - result.scene: Three.js scene (if options.buildScene is true and sceneBuilder provided)
     * @param {Function} onProgress - Progress callback ({loaded, total, stage, percentage, message}) => void
     * @param {Function} onError - Error callback (error) => void
     * @param {Object} options - Loading options
     * @param {number} options.maxMemoryLimitMB - Override memory limit for this load
     * @param {boolean} options.buildScene - If true, build Three.js scene (requires sceneBuilder)
     * @param {Function} options.sceneBuilder - Async function (usd, options) => THREE.Object3D
     *   Typically: TinyUSDZLoaderUtils.buildThreeNode(usd.getNode(0), null, usd, options)
     * @param {Object} options.sceneBuilderOptions - Options to pass to sceneBuilder
     */
    loadWithFullProgress(url, onLoad, onProgress, onError, options = {}) {
        const scope = this;

        // Progress phase weights
        const DOWNLOAD_WEIGHT = 0.5;  // 0-50%
        const PARSE_WEIGHT = 0.3;     // 50-80%
        const BUILD_WEIGHT = 0.2;     // 80-100%

        const reportProgress = (phase, phaseProgress, message) => {
            if (!onProgress) return;

            let overallProgress = 0;
            let stage = phase;

            switch (phase) {
                case 'downloading':
                    overallProgress = phaseProgress * DOWNLOAD_WEIGHT;
                    break;
                case 'parsing':
                    overallProgress = DOWNLOAD_WEIGHT + (phaseProgress * PARSE_WEIGHT);
                    break;
                case 'building':
                    overallProgress = DOWNLOAD_WEIGHT + PARSE_WEIGHT + (phaseProgress * BUILD_WEIGHT);
                    break;
                case 'complete':
                    overallProgress = 1.0;
                    break;
            }

            onProgress(scope._createProgressEvent(
                overallProgress,
                1,
                stage,
                message || `${stage}... ${Math.round(overallProgress * 100)}%`
            ));
        };

        // Start loading
        const initPromise = this.native_ ? Promise.resolve() : this.init();

        initPromise
            .then(async () => {
                // Phase 1: Download (0-50%)
                reportProgress('downloading', 0, 'Starting download...');

                const response = await fetch(url);
                if (!response.ok) {
                    throw new Error(`Failed to fetch: ${response.statusText}`);
                }

                const contentLength = response.headers.get('content-length');
                const total = contentLength ? parseInt(contentLength, 10) : 0;

                let usd_data;
                if (total > 0 && response.body) {
                    // Stream with progress
                    const reader = response.body.getReader();
                    const chunks = [];
                    let loaded = 0;

                    while (true) {
                        const { done, value } = await reader.read();
                        if (done) break;
                        chunks.push(value);
                        loaded += value.length;
                        reportProgress('downloading', loaded / total, `Downloading... ${Math.round((loaded / total) * 100)}%`);
                    }

                    const result = new Uint8Array(loaded);
                    let offset = 0;
                    for (const chunk of chunks) {
                        result.set(chunk, offset);
                        offset += chunk.length;
                    }
                    usd_data = result;
                } else {
                    // No content-length, simple fetch
                    const buffer = await response.arrayBuffer();
                    usd_data = new Uint8Array(buffer);
                    reportProgress('downloading', 1, 'Download complete');
                }

                return usd_data;
            })
            .then((usd_binary) => {
                // Phase 2: Parse (50-80%)
                reportProgress('parsing', 0, 'Parsing USD...');

                const usd = new scope.native_.TinyUSDZLoaderNative();

                // Set memory limit
                const memoryLimit = options.maxMemoryLimitMB || scope.maxMemoryLimitMB_;
                if (memoryLimit !== undefined) {
                    usd.setMaxMemoryLimitMB(memoryLimit);
                }

                scope._applySkinningLoadOptions(usd);

                const ok = usd.loadFromBinary(usd_binary, url);
                if (!ok) {
                    throw new Error(`Failed to parse USD: ${usd.error()}`);
                }

                reportProgress('parsing', 1, 'Parse complete');
                return usd;
            })
            .then(async (usd) => {
                // Phase 3: Build scene (80-100%) - optional
                const result = { usd: usd, scene: null };

                if (options.buildScene && options.sceneBuilder) {
                    reportProgress('building', 0, 'Building scene...');

                    try {
                        // Get root node
                        const rootNode = usd.getNode(0);

                        // Build scene with progress callback
                        const builderOptions = {
                            ...(options.sceneBuilderOptions || {}),
                            onProgress: (info) => {
                                // Forward scene building progress (scale to 80-100%)
                                const buildProgress = info.percentage ? info.percentage / 100 : 0.5;
                                reportProgress('building', buildProgress, info.message || 'Building scene...');
                            }
                        };

                        result.scene = await options.sceneBuilder(rootNode, null, usd, builderOptions);
                        reportProgress('building', 1, 'Scene complete');
                    } catch (buildError) {
                        console.warn('Scene building failed:', buildError);
                        // Continue without scene - USD is still valid
                    }
                }

                reportProgress('complete', 1, 'Complete');
                onLoad(result);
            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error in loadWithFullProgress:', error);
                if (onError) {
                    onError(error);
                }
            });
    }

    /**
     * Async version of loadWithFullProgress
     * @returns {Promise<{usd: Object, scene?: THREE.Object3D}>}
     */
    async loadWithFullProgressAsync(url, onProgress, options = {}) {
        const scope = this;
        return new Promise((resolve, reject) => {
            scope.loadWithFullProgress(url, resolve, onProgress, reject, options);
        });
    }

    ///**
    // * Set texture callback
    //  */
    //setTextureLoader(texLoader) {
    //    this.texLoader = texLoader;
    //}



}

export { TinyUSDZLoader, FetchAssetResolver };
