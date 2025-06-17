// WASM module.
//import initTinyUSDZNative from './tinyusdz.js';

import { FetchAssetResolver } from "./TinyUSDZLoader";  

class TinyUSDZComposer {

    constructor() {

        this.usdLayer_ = null; // This will hold the USD layer after loading.
        this.assetMap_ = new Map();
        this.usdLoader_ = null; // TinyUSDZLoaderNative instance
        this.assetResolver_ = new FetchAssetResolver(); // 'fetch' Asset resolver 
    }

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

    static hasVariants(usd_layer) {
        if (!usd_layer || !usd_layer.hasVariants) {
            console.warn("TinyUSDZComposer: Invalid USD layer or hasVariants not available.");
            return false;
        }
        return usd_layer.hasVariants();
    }

    static composeVariants(usd_layer) {
        if (!usd_layer || !usd_layer.composeVariants) {
            console.warn("TinyUSDZComposer: Invalid USD layer or composeVariants not available.");
            return [];
        }
        return usd_layer.composeVariants();
    }

    clearAssetMap() {
        this.assetMap_.clear();
    }

    getAssetMap() {
        return this.assetMap_;
    }

    setLayer(usdLayer) {
        this.usdLayer_ = usdLayer;
    }

    getLayer() {
        if (!this.usdLayer_) {
            throw new Error("TinyUSDZComposer: Layer is not set. Call setLayer() first.");
        }
        return this.usdLayer_;
    }

    setUSDLoader(usd_loader) {
        this.usdLoader_ = usd_loader;
    }

    getUSDLoader() {
        if (!this.usdloader_) {
            throw new Error("TinyUSDZComposer: USD loader is not set. Call setUSDLoader() first.");
        }
        return this.usdLoader_;
    }

    // Recursively resolve sublayer assets.
    async resolveSublayerAssets(depth, usdLayer) {

        if (depth > 16) {
            console.warn("TinyUSDZComposer: Maximum recursion depth reached while resolving sublayer assets.");
            return;
        }
        const sublayerAssetPaths = TinyUSDZComposer.extractSublayerAssetPaths(usdLayer);
        //console.log("extractSublayer", sublayerAssetPaths);

        await Promise.all(sublayerAssetPaths.map(async (sublayerPath) => {
            const [uri, binary] = await this.assetResolver_.resolveAsync(sublayerPath);
            //console.log("sublayerPath:", sublayerPath, "binary:", binary.byteLength, "bytes");

            //console.log("Loading sublayer:", sublayerPath);
            const sublayer = await this.usdLoader_.loadAsLayerAsync(sublayerPath);

            //console.log("sublayer:", sublayer);
            await this.resolveSublayerAssets(depth + 1, sublayer);

            this.assetMap_.set(sublayerPath, binary);
        }));
    }

    async progressiveComposition() {

        if (!this.usdLayer_) {
            throw new Error("TinyUSDZComposer: setLayer() is not called.");
        }

        if (!this.usdLoader_) {
            throw new Error("TinyUSDZComposer: setUSDLoader() is not called.");
        }


        // LIVRPS
        // [x] local(subLayer)
        // [x] inherits
        // [x] variants
        // [x] references
        // [x ] payload
        // [ ] specializes

        // Resolving subLayer is recursive.
        await this.resolveSublayerAssets(/* depth */0, this.usdLayer_);

        for (const [uri, binary] of this.assetMap_.entries()) {
            //console.log("setAsset:", uri, "binary:", binary.byteLength, "bytes");
            this.usdLayer_.setAsset(uri, binary);
        }

        if (!this.usdLayer_.composeSublayers()) {
            throw new Error("Failed to compose sublayers:", this.usdLayer_.error());
        }


        // others are iterative.
        const kMaxIter = 16;

        for (let i = 0; i < kMaxIter; i++) {

            // In each composition operation, usd_layer may be modified(merged with sublayers, etc).
            // And we iterate until no more composition is needed.

            //console.log("iter", i);
            //console.log("hasReferences:", TinyUSDZComposer.hasReferences(this.usdLayer_));
            //console.log("hasPayload:", TinyUSDZComposer.hasPayload(this.usdLayer_));
            //console.log("hasInherits:", TinyUSDZComposer.hasInherits(this.usdLayer_));
            //console.log("hasVariants:", TinyUSDZComposer.hasVariants(this.usdLayer_));

            if (!TinyUSDZComposer.hasReferences(this.usdLayer_) &&
                !TinyUSDZComposer.hasPayload(this.usdLayer_) &&
                !TinyUSDZComposer.hasInherits(this.usdLayer_) &&
                !TinyUSDZComposer.hasVariants(this.usdLayer_)) {
                break;
            }

            // Inherits and variants does not involve asset loading.
            if (TinyUSDZComposer.hasInherits(this.usdLayer_)) {
                if (!this.usdLayer_.composeInherits()) {
                    throw new Error("Failed to compose inherits:", this.usdLayer_.error());
                }
            }

            if (TinyUSDZComposer.hasVariants(this.usdLayer_)) {
                if (!this.usdLayer_.composeVariants()) {
                    throw new Error("Failed to compose variants:", this.usdLayer_.error());
                }
            }

            if (TinyUSDZComposer.hasReferences(this.usdLayer_)) {
                const referencesAssetPaths = TinyUSDZComposer.extractReferencesAssetPaths(this.usdLayer_);

                await Promise.all(referencesAssetPaths.map(async (assetPath) => {
                    const [uri, binary] = await this.assetResolver_.resolveAsync(assetPath);
                    //console.log("referencesPath:", assetPath, "binary:", binary.byteLength, "bytes");

                    this.assetMap_.set(uri, binary);
                    this.usdLayer_.setAsset(uri, binary);
                }));

                //console.log("do composeReferences");
                if (!this.usdLayer_.composeReferences()) {
                    throw new Error("Failed to compose references:", this.usdLayer_.error());
                }
            }

            if (TinyUSDZComposer.hasPayload(this.usdLayer_)) {
                const payloadAssetPaths = TinyUSDZComposer.extractPayloadAssetPaths(this.usdLayer_);

                await Promise.all(payloadAssetPaths.map(async (assetPath) => {
                    const [uri, binary] = await this.assetResolver_.resolveAsync(assetPath);
                    //console.log("payloadAssetPath:", assetPath, "binary:", binary.byteLength, "bytes");

                    this.assetMap_.set(uri, binary);
                    this.usdLayer_.setAsset(uri, binary);
                }));

                if (!this.usdLayer_.composePayload()) {
                    throw new Error("Failed to compose payload:", usd_layer.error());
                }
            }
        }
    }



}



export { TinyUSDZComposer };
