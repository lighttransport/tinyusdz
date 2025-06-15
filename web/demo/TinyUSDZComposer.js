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

    static hasSublayer(usd_layer) {
        if (!usd_layer || !usd_layer.hasSublayer) {
            console.warn("TinyUSDZComposer: Invalid USD layer or hasSublayer not available.");
            return false;
        }
        return usd_layer.hasSublayer();
    }

    static extractSublayerAssetPaths(usd_layer) {
        if (!usd_layer || !usd_layer.extractSublayerAssetPaths) {
            console.warn("TinyUSDZComposer: Invalid USD layer or extractSublayerAssetPaths not available.");
            return [];
        }
        return usd_layer.extractSublayerAssetPaths();
    }

    static hasReferences(usd_layer) {
        if (!usd_layer || !usd_layer.hasReferences) {
            console.warn("TinyUSDZComposer: Invalid USD layer or hasReferences not available.");
            return false;
        }
        return usd_layer.hasReferences();
    }

    static extractReferencesAssetPaths(usd_layer) {
        if (!usd_layer || !usd_layer.extractReferencesAssetPaths) {
            console.warn("TinyUSDZComposer: Invalid USD layer or extractReferencesAssetPaths not available.");
            return [];
        }
        return usd_layer.extractReferencesAssetPaths();
    }

    static hasPayload(usd_layer) {
        if (!usd_layer || !usd_layer.hasPayload) {
            console.warn("TinyUSDZComposer: Invalid USD layer or hasPayload not available.");
            return false;
        }
        return usd_layer.hasPayload();
    }

    static extractPayloadAssetPaths(usd_layer) {
        if (!usd_layer || !usd_layer.extractPayloadAssetPaths) {
            console.warn("TinyUSDZComposer: Invalid USD layer or extractPayloadAssetPaths not available.");
            return [];
        }
        return usd_layer.extractPayloadAssetPaths();
    }

    static composeSublayer(usd_layer) {
        if (!usd_layer || !usd_layer.composeSublayer) {
            console.warn("TinyUSDZComposer: Invalid USD layer or composeSublayer not available.");
            return [];
        }
        return usd_layer.composeSublayer();
    }

    static composeReferences(usd_layer) {
        if (!usd_layer || !usd_layer.composeReferences) {
            console.warn("TinyUSDZComposer: Invalid USD layer or composeReferences not available.");
            return [];
        }
        return usd_layer.composeReferences();
    }

    static composePayload(usd_layer) {
        if (!usd_layer || !usd_layer.composePayload) {
            console.warn("TinyUSDZComposer: Invalid USD layer or composePayload not available.");
            return [];
        }
        return usd_layer.composePayload();
    }

    static hasInherits(usd_layer) {
        if (!usd_layer || !usd_layer.hasInherits) {
            console.warn("TinyUSDZComposer: Invalid USD layer or hasInherits not available.");
            return false;
        }
        return usd_layer.hasInherits();
    }

    static composeInherits(usd_layer) {
        if (!usd_layer || !usd_layer.composeInherits) {
            console.warn("TinyUSDZComposer: Invalid USD layer or composeInherits not available.");
            return [];
        }
        return usd_layer.composeInherits();
    }

    // TOOD: variants


    /*
    static async resolveSublayerAssets(depth, usd_layer, assetResolver) {

        if (depth > 16) {
            console.warn("TinyUSDZComposer: Maximum recursion depth reached while resolving sublayer assets.");
            return;
        }

        if (!usd_layer || !assetResolver) {
            console.warn("TinyUSDZComposer: Invalid USD layer or asset resolver.");
            return;
        }

        const sublayerAssetPaths = this.extractSublayerAssetPaths(usd_layer);
        console.log("extractSublayer", sublayerAssetPaths);

        await Promise.all(sublayerAssetPaths.map(async (sublayerPath) => {
            const [uri, binary] = await assetResolver.resolveAsync(sublayerPath);
            console.log("sublayerPath:", sublayerPath, "binary:", binary.byteLength, "bytes");

            
            

            return usd_layer.setAsset(sublayerPath, binary);
            }));

    }
    */

}



export { TinyUSDZComposer };
