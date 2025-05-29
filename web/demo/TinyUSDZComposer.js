// WASM module.
import initTinyUSDZNative from './tinyusdz.js';

class TinyUSDZComposer {
  
    constructor() {
        this.native_ = null;
    }

    async init() {
        if (!this.native_) {
            console.log('Initializing native module...');
            this.native_ = await initTinyUSDZNative({});
            if (!this.native_) {
                throw new Error('TinyUSDZComposer: Failed to initialize native module.');
            }
            console.log('Native module initialized');
        }
        return this;
    }

    // Additional methods for composing USDZ files can be added here

    addScene(scene) {
      console.warn("TODO");
    }

}



export { TinyUSDZComposer };
