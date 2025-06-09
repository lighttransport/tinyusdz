// WASM module.
import initTinyUSDZNative from './tinyusdz.js';

// TODO :  use 'from 'three''
import { Loader } from 'three'; // or https://cdn.jsdelivr.net/npm/three/build/three.module.js';



// TODO
//
// [ ] Material callback(like GLTFLoader)
// [ ] Texture load callback(load texture in Three.js)
// [ ] USD Composition support
//
class TinyUSDZLoader extends Loader {

    constructor(manager) {
        super(manager);

        this.native_ = null;

        this.assetResolver_ = null;

        // texture loader callback
        // null = Use TinyUSDZ's builtin image loader(C++ native module)
        this.texLoader = null;


        this.imageCache = {};
        this.textureCache = {};

        this.enableComposition_ = false;
    }

    // Initialize the native WASM module
    // This is async but the load() method handles it internally with promises
    // Initialize the native WASM module
    // This is async but the load() method handles it internally with promises
    async init() {
        if (!this.native_) {
            console.log('Initializing native module...');
            this.native_ = await initTinyUSDZNative({});
            if (!this.native_) {
                throw new Error('TinyUSDZLoader: Failed to initialize native module.');
            }
            console.log('Native module initialized');
        }
        return this;
    }

    setEnableComposition(enabled) {

        this.enableComposition_ = enabled;
    }


    // Set AssetResolver callback.
    // This is used to resolve asset paths(e.g. textures, usd files) in the USD.
    // For web app, usually we'll convert asset path to URI
    setAssetResolver(callback) {
        this.assetResolver_ = callback;
    }

    //
    // Load a USDZ/USDA/USDC file from a URL as USD Stage(Freezed scene graph)
    //
    load(url, onLoad, onProgress, onError) {
        //console.log('url', url);

        const scope = this;

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
                const usd_binary = new Uint8Array(usd_data);

                console.log('Loaded USD binary data:', usd_binary.length, 'bytes');
                //return this.parse(usd_binary);

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

        usd.setEnableComposition(this.enableComposition_);
        const ok = usd.loadFromBinary(binary, filePath);
        if (!ok) {
            _onError(new Error('TinyUSDZLoader: Failed to load USD from binary data.', {cause: usd.error()}));
        } else {
            onLoad(usd);
        }
    }

    /**
     * Set texture callback
      */
    setTextureLoader(texLoader) {
        this.texLoader = texLoader;
    }

    // NOTE: Use loadSync() in base Loader class


    //addLayer() {
    //    console.warn('TinyUSDZLoader: addLayer() is not implemented.');
    //}

}

export { TinyUSDZLoader };
