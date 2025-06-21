// Uncomment if you use zstd compression for wasm
//import { decompress } from 'fzstd';

import { Loader } from 'three'; // or https://cdn.jsdelivr.net/npm/three/build/three.module.js';

// WASM module of TinyUSDZ.
import initTinyUSDZNative from './tinyusdz.js';


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

    constructor(manager) {
        super(manager);

        this.native_ = null;

        this.assetResolver_ = null;

        // texture loader callback
        // null = Use TinyUSDZ's builtin image loader(C++ native module)
        //this.texLoader = null;


        this.imageCache = {};
        this.textureCache = {};

        // Default: do NOT use zstd compressed WASM.
        this.useZstdCompressedWasm_ = false;
        this.wasmPath_ = './tinyusdz.wasm';
        this.compressedWasmPath_ = './tinyusdz.wasm.zst';
    }

    // Decompress zstd compressed WASM
    async decompressZstdWasm(compressedPath) {
        try {
            //console.log(`Loading compressed WASM from: ${compressedPath}`);
            const response = await fetch(compressedPath);
            if (!response.ok) {
                throw new Error(`Failed to fetch compressed WASM: ${response.statusText}`);
            }

            const compressedData = await response.arrayBuffer();
            //console.log(`Compressed WASM size: ${compressedData.byteLength} bytes`);

            // Decompress using zstd
            const decompressedData = decompress(new Uint8Array(compressedData));
            //console.log(`Decompressed WASM size: ${decompressedData.byteLength} bytes`);

            return decompressedData;
        } catch (error) {
            console.error('Error decompressing zstd WASM:', error);
            throw error;
        }
    }

    // Initialize the native WASM module
    // This is async but the load() method handles it internally with promises
    async init() {
        if (!this.native_) {
            //console.log('Initializing native module...');

            let wasmBinary = null;
            
            if (this.useZstdCompressedWasm_) {
                // Load and decompress zstd compressed WASM
                wasmBinary = await this.decompressZstdWasm(this.compressedWasmPath_);

                //console.log('Decompressed WASM binary loaded:', wasmBinary.byteLength, 'bytes');
            }

            // Initialize with custom WASM binary if decompressed
            const initOptions = wasmBinary ? { wasmBinary } : {};

            this.native_ = await initTinyUSDZNative(initOptions);
            if (!this.native_) {
                throw new Error('TinyUSDZLoader: Failed to initialize native module.');
            }
            //console.log('Native module initialized');
        }
        return this;
    }


    // TODO: remove
    // Set AssetResolver callback.
    // This is used to resolve asset paths(e.g. textures, usd files) in the USD.
    // For web app, usually we'll convert asset path to URI
    //setAssetResolver(callback) {
    //    this.assetResolver_ = callback;
    //}

    //
    // Load a USDZ/USDA/USDC file from a URL as USD Stage(Freezed scene graph)
    // NOTE: for loadAsync(), Use base Loader class's loadAsync() method
    //
    load(url, onLoad, onProgress, onError) {
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
                }, onError);

            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error initializing native module:', error);
                if (onError) {
                    onError(error);
                }
            });
    }

    //
    // Parse a USDZ/USDA/USDC binary data
    //
    parse(binary /* ArrayBuffer */, filePath /* optional */, onLoad, onError) {

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

        const ok = usd.loadFromBinary(binary, filePath);
        if (!ok) {
            _onError(new Error('TinyUSDZLoader: Failed to load USD from binary data.', {cause: usd.error()}));
        } else {
            onLoad(usd);
        }
    }

    //
    // Load a USDZ/USDA/USDC file from a URL as USD Layer(for composition)
    //
    loadAsLayer(url, onLoad, onProgress, onError) {
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

    async loadAsLayerAsync(url, onProgress) {
     	const scope = this;

		return new Promise( function ( resolve, reject ) {

			scope.loadAsLayer( url, resolve, onProgress, reject );

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
