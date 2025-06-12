// WASM module.
//import initTinyUSDZNative from './tinyusdz.js';

class TinyUSDZComposer {
  
    constructor() {
        //this.native_ = null;
    }

    //async init() {
    //    if (!this.native_) {
    //        console.log('Initializing native module...');
    //        this.native_ = await initTinyUSDZNative({});
    //        if (!this.native_) {
    //            throw new Error('TinyUSDZComposer: Failed to initialize native module.');
    //        }
    //        console.log('Native module initialized');
    //    }
    //    return this;
    //}

    // Additional methods for composing USDZ files can be added here

    //addScene(scene) {
    //  console.warn("TODO");
    //}

    //
    // uds_layer : TinyUSDZLoaderNative instance after loadAsLayer/loadAsLayerAsync 
    //
    static extractSublayerAssetPaths(usd_layer) {
        if (!usd_layer || !usd_layer.extractSublayerAssetPaths) {
            console.warn("TinyUSDZComposer: Invalid USD layer or extractSublayerAssetPaths not available.");
            return [];
        }
        return usd_layer.extractSublayerAssetPaths();
    }

    static extractReferencesAssetPaths(usd_layer) {
        if (!usd_layer || !usd_layer.extractReferencesAssetPaths) {
            console.warn("TinyUSDZComposer: Invalid USD layer or extractReferencesAssetPaths not available.");
            return [];
        }
        return usd_layer.extractReferencesAssetPaths();
    }

    static composeSublayer(usd_layer) {
        if (!usd_layer || !usd_layer.composeSublayer) {
            console.warn("TinyUSDZComposer: Invalid USD layer or composeSublayer not available.");
            return [];
        }
        return usd_layer.composeSublayer();
    }

}



export { TinyUSDZComposer };
