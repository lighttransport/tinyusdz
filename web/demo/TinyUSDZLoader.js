// WASM module.
import initTinyUSDZNative from './tinyusdz.js';

/*
 * ```js
 * const loader = new TinyUSDZLoader();
 *
 * const tusd = await loader.loadAsync( 'path/to/file.usdz' );
 * scene.add( tusd.scene );
 * ```
 */

class TinyUSDZLoader extends Loader {

    constructor( manager ) {
       super( manager );
    }

    load( url, onLoad, onProgress, onError) {

    }

}

export { TinyUSDZLoader };
