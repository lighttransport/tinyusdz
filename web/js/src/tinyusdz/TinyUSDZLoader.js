import { Loader } from 'three'; // or https://cdn.jsdelivr.net/npm/three/build/three.module.js';

// tinyusdz module are dynamically imported at TinyUSDZLoader

class FetchAssetResolver {
    constructor() {
        this.assetCache = new Map();
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
            console.log(use_memory64);


            let initTinyUSDZNative = null;

            // Use dynamic import based on memory64 parameter
            if (use_memory64) {
                console.log("Loading 64bit module");
                const module = await import('./tinyusdz_64.js');
                initTinyUSDZNative = module.default;
            } else {
                console.log("Loading 32bit module");
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
                return fetch(url);
            })
            .then((response) => {
                return response.arrayBuffer();
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

        const ok = usd.loadFromBinary(binary, filePath);
        if (!ok) {
            _onError(new Error('TinyUSDZLoader: Failed to load USD from binary data.', {cause: usd.error()}));
        } else {
            onLoad(usd);
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
                //console.log('fetch USDZ file done:', url);
                return response.arrayBuffer();
            })
            .then((usd_data) => {
                const usd_binary = new Uint8Array(usd_data);

                //console.log('Loaded USD binary data:', usd_binary.length, 'bytes');
                //return this.parse(usd_binary);

                const usd = new this.native_.TinyUSDZLoaderNative();

                // Set memory limit before loading if specified (otherwise use native default)
                const memoryLimit = options.maxMemoryLimitMB || this.maxMemoryLimitMB_;
                if (memoryLimit !== undefined) {
                    usd.setMaxMemoryLimitMB(memoryLimit);
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

    ///**
    // * Set texture callback
    //  */
    //setTextureLoader(texLoader) {
    //    this.texLoader = texLoader;
    //}



}

export { TinyUSDZLoader, FetchAssetResolver };
