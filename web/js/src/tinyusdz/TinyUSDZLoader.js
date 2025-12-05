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
      // Browser environment or blob URL - use fetch API and convert to File
      const response = await fetch(url);
      if (!response.ok) {
        throw new Error(`Failed to fetch: ${response.statusText}`);
      }

      const blob = await response.blob();
      const fileName = url.split('/').pop() || 'unknown';
      const file = new File([blob], fileName, {
        type: blob.type || 'application/octet-stream',
        lastModified: Date.now()
      });

      return file;
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
            const response = await fetch(uri);
            if (!response.ok) {
                throw new Error(`Failed to fetch asset: ${uri}`);
            }
            const data = await response.arrayBuffer();
            //console.log(`Fetched asset ${uri} successfully, size: ${data.byteLength} bytes`);
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
    }

    // Decompress zstd compressed WASM
    async decompressZstdWasm(compressedPath) {
        try {
            const fzstd = await import('fzstd');

            const wasmURL = new URL(compressedPath, import.meta.url).href;

            //console.log(`Loading compressed WASM from: ${wasmURL}`);
            const response = await fetch(wasmURL);
            //console.log(response);
            if (!response.ok) {
                throw new Error(`Failed to fetch compressed WASM: ${response.statusText}`);
            }

            const compressedData = await response.arrayBuffer();
            //console.log(`Compressed WASM size: ${compressedData.byteLength} bytes`);

            if (compressedData.byteLength < 1024*64) {
                throw new Error('Compressed WASM size is unusually small, may not be valid zstd compressed data.');
            }

            // Check zstd magic number (0x28B52FFD in little-endian)
            const magicBytes = new Uint8Array(compressedData, 0, 4);
            const expectedMagic = [0x28, 0xB5, 0x2F, 0xFD]; // Little-endian representation
            //console.log(magicBytes);
            //console.log(expectedMagic);
            
            if (compressedData.byteLength < 4 || 
                magicBytes[0] !== expectedMagic[0] || 
                magicBytes[1] !== expectedMagic[1] || 
                magicBytes[2] !== expectedMagic[2] || 
                magicBytes[3] !== expectedMagic[3]) {
                throw new Error('Invalid zstd file: magic number mismatch');
            }

            // Decompress using zstd
            const decompressedData = fzstd.decompress(new Uint8Array(compressedData));
            //console.log(`Decompressed WASM size: ${decompressedData.byteLength} bytes`);

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
            //console.log('Initializing native module...');
          
            // WASM module of TinyUSDZ.
            const url = new URL(import.meta.url);

            //let initTinyUSDZNative = null;
          

            //console.log("arg:", url.searchParams.get("memory64"));
            let use_memory64 = this.useMemory64_;
            if (url.searchParams.get("memory64") == "true") {
              use_memory64 = true;
            }
            //console.log(use_memory64);


            let initTinyUSDZNative = null;

            // Use dynamic import based on memory64 parameter
            if (use_memory64) {
                //console.log("Loading 64bit module");
                const module = await import('./tinyusdz_64.js');
                initTinyUSDZNative = module.default;
            } else {
                //console.log("Loading 32bit module");
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

            this.native_ = await initTinyUSDZNative(initOptions);
            if (!this.native_) {
                throw new Error('TinyUSDZLoader: Failed to initialize native module.');
            }
            //console.log('Native module initialized');
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
     * @param {number} count - Target number of bone influences per vertex (1-64)
     */
    setTargetBoneCount(count) {
        if (typeof count !== 'number' || count < 1 || count > 64) {
            throw new Error('Target bone count must be between 1 and 64');
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


    // TODO: remove
    // Set AssetResolver callback.
    // This is used to resolve asset paths(e.g. textures, usd files) in the USD.
    // For web app, usually we'll convert asset path to URI
    //setAssetResolver(callback) {
    //    this.assetResolver_ = callback;
    //}

    /**
     * Load a USDZ/USDA/USDC file from a URL as USD Stage(Freezed scene graph)
     * NOTE: for loadAsync(), Use base Loader class's loadAsync() method
     * @param {string} url - URL to load from
     * @param {Function} onLoad - Success callback
     * @param {Function} onProgress - Progress callback
     * @param {Function} onError - Error callback
     * @param {Object} options - Loading options
     * @param {number} options.maxMemoryLimitMB - Override memory limit for this load
     */
    load(url, onLoad, onProgress, onError, options = {}) {
        //console.log('url', url);

        const scope = this;

        // Create a promise chain to handle initialization and loading
        const initPromise = this.native_ ? Promise.resolve() : this.init();

        initPromise
            .then(() => {
                return this.fetcher.fetch(url);
            })
            .then(async (response) => {
                // Convert File to ArrayBuffer
                return await response.arrayBuffer();
            })
            .then((usd_data) => {
                const usd_binary = new Uint8Array(usd_data);

                //console.log('Loaded USD binary data:', usd_binary.length, 'bytes');

                scope.parse(usd_binary, url, function (usd) {
                    onLoad(usd);
                }, onError, options);

            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error initializing native module:', error);
                if (onError) {
                    onError(error);
                }
            });
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

        // Set bone reduction configuration
        if (this.enableBoneReduction_) {
            usd.setEnableBoneReduction(true);
            usd.setTargetBoneCount(this.targetBoneCount_ || 4);
        }

        const ok = usd.loadFromBinary(binary, filePath);
        if (!ok) {
            _onError(new Error('TinyUSDZLoader: Failed to load USD from binary data.', {cause: usd.error()}));
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

            // Set bone reduction configuration
            if (this.enableBoneReduction_) {
                usd.setEnableBoneReduction(true);
                usd.setTargetBoneCount(this.targetBoneCount_ || 4);
            }

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
                            reject(new Error('TinyUSDZLoader: Failed to load USD from binary data.', {cause: usd.error()}));
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
        //console.log('url', url);

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
                //usd_ = new this.native_.TinyUSDZLoaderNative();
                return fetch(url);
            })
            .then((response) => {
                console.log('fetch USDZ file done:', url);
                return response.arrayBuffer();
            })
            .then((usd_data) => {
                console.log('usd_data done:', url);
                const usd_binary = new Uint8Array(usd_data);

                //console.log('Loaded USD binary data:', usd_binary.length, 'bytes');
                //return this.parse(usd_binary);

                const usd = new this.native_.TinyUSDZLoaderNative();

                // Set memory limit before loading if specified (otherwise use native default)
                const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
                if (memoryLimit !== undefined) {
                    usd.setMaxMemoryLimitMB(memoryLimit);
                }

                // Set bone reduction configuration
                if (scope.enableBoneReduction_) {
                    usd.setEnableBoneReduction(true);
                    usd.setTargetBoneCount(scope.targetBoneCount_ || 4);
                }

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

                // Set bone reduction configuration
                if (this.enableBoneReduction_) {
                    usd.setEnableBoneReduction(true);
                    usd.setTargetBoneCount(this.targetBoneCount_ || 4);
                }

                const u8data = new Uint8Array(usd_data);
                //console.log(u8data);
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


    ///**
    // * Set texture callback
    //  */
    //setTextureLoader(texLoader) {
    //    this.texLoader = texLoader;
    //}

    

}

export { TinyUSDZLoader, FetchAssetResolver };
