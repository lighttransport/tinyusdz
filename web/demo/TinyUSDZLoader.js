// WASM module.
import initTinyUSDZNative from './tinyusdz.js';

// TODO :  use 'from 'three''
import { Loader } from 'three'; // or https://cdn.jsdelivr.net/npm/three/build/three.module.js';


//
// Convert UsdPreviewSureface to MeshPhysicalMaterial
// - [x] diffuseColor -> color
// - [x] ior -> ior
// - [x] clearcoat -> clearcoat
// - [x] clearcoatRoughness -> clearcoatRoughness
// - [x] specularColor -> specular
// - [x] roughness -> roughness 
// - [x] metallic -> metalness
// - [x] emissiveColor -> emissive
// - [x] opacity -> opacity
// - [x] occlusion -> aoMap
// - [x] normal -> normalMap
// - [x] displacement -> displacementMap
function ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd) {
    const material = new THREE.MeshPhysicalMaterial();

    // Helper function to create texture from USD texture ID
    function createTextureFromUSD(textureId) {
        if (textureId === undefined) return null;

        const tex = usd.getTexture(textureId);
        const img = usd.getImage(tex.textureImageId);

        const image8Array = new Uint8ClampedArray(img.data);
        const texture = new THREE.DataTexture(image8Array, img.width, img.height);
        texture.format = THREE.RGBAFormat;
        texture.flipY = true;
        texture.needsUpdate = true;

        return texture;
    }

    // Diffuse color and texture
    material.color = new THREE.Color(0.18, 0.18, 0.18);
    if (usdMaterial.hasOwnProperty('diffuseColor')) {
        const color = usdMaterial.diffuseColor;
        material.color = new THREE.Color(color[0], color[1], color[2]);
    }

    if (usdMaterial.hasOwnProperty('diffuseColorTextureId')) {
        material.map = createTextureFromUSD(usdMaterial.diffuseColorTextureId);
    }

    // IOR
    material.ior = 1.5;
    if (usdMaterial.hasOwnProperty('ior')) {
        material.ior = usdMaterial.ior;
    }

    // Clearcoat
    material.clearcoat = 0.0;
    if (usdMaterial.hasOwnProperty('clearcoat')) {
        material.clearcoat = usdMaterial.clearcoat;
    }

    material.clearcoatRoughness = 0.0;
    if (usdMaterial.hasOwnProperty('clearcoatRoughness')) {
        material.clearcoatRoughness = usdMaterial.clearcoatRoughness;
    }

    // Workflow selection
    material.useSpecularWorkflow = false;
    if (usdMaterial.hasOwnProperty('useSpecularWorkflow')) {
        material.useSpecularWorkflow = usdMaterial.useSpecularWorkflow;
    }

    if (material.useSpecularWorkflow) {
        material.specular = new THREE.Color(0.0, 0.0, 0.0);
        if (usdMaterial.hasOwnProperty('specularColor')) {
            const color = usdMaterial.specularColor;
            material.specular = new THREE.Color(color[0], color[1], color[2]);
        }
        if (usdMaterial.hasOwnProperty('specularColorTextureId')) {
            material.specularMap = createTextureFromUSD(usdMaterial.specularColorTextureId);
        }
    } else {
        material.metalness = 0.0;
        if (usdMaterial.hasOwnProperty('metallic')) {
            material.metalness = usdMaterial.metallic;
        }
        if (usdMaterial.hasOwnProperty('metallicTextureId')) {
            material.metalnessMap = createTextureFromUSD(usdMaterial.metallicTextureId);
        }
    }

    // Roughness
    material.roughness = 0.5;
    if (usdMaterial.hasOwnProperty('roughness')) {
        material.roughness = usdMaterial.roughness;
    }
    if (usdMaterial.hasOwnProperty('roughnessTextureId')) {
        material.roughnessMap = createTextureFromUSD(usdMaterial.roughnessTextureId);
    }

    // Emissive
    if (usdMaterial.hasOwnProperty('emissiveColor')) {
        const color = usdMaterial.emissiveColor;
        material.emissive = new THREE.Color(color[0], color[1], color[2]);
    }
    if (usdMaterial.hasOwnProperty('emissiveColorTextureId')) {
        material.emissiveMap = createTextureFromUSD(usdMaterial.emissiveColorTextureId);
    }

    // Opacity
    material.opacity = 1.0;
    if (usdMaterial.hasOwnProperty('opacity')) {
        material.opacity = usdMaterial.opacity;
        if (material.opacity < 1.0) {
            material.transparent = true;
        }
    }
    if (usdMaterial.hasOwnProperty('opacityTextureId')) {
        material.alphaMap = createTextureFromUSD(usdMaterial.opacityTextureId);
        material.transparent = true;
    }

    // Ambient Occlusion
    if (usdMaterial.hasOwnProperty('occlusionTextureId')) {
        material.aoMap = createTextureFromUSD(usdMaterial.occlusionTextureId);
    }

    // Normal Map
    if (usdMaterial.hasOwnProperty('normalTextureId')) {
        material.normalMap = createTextureFromUSD(usdMaterial.normalTextureId);
    }

    // Displacement Map
    if (usdMaterial.hasOwnProperty('displacementTextureId')) {
        material.displacementMap = createTextureFromUSD(usdMaterial.displacementTextureId);
        material.displacementScale = 1.0;
    }

    return material;
}

/*
 * ```js
 * const loader = new TinyUSDZLoader();
 *
 * const tusd = await loader.loadAsync( 'path/to/file.usdz' );
 * scene.add( tusd.scene );
 * ```
 */

class TinyUSDZParser {
    constructor(options = {}) {

        this.options = {};

    }
}

// TODO
//
// [ ] Material callback(like GLTFLoader)
// [ ] Texture load callback(load texture in Three.js)
// [ ] Composition support
//
class TinyUSDZLoader extends Loader {

    constructor(manager) {
        super(manager);

        this.native_ = null;

        this.usd = null; // USD scene

        // texture loader callback
        // null = Use TinyUSDZ's builtin image loader(C++ native module)
        this.texLoader = null;


        this.imageCache = {};
        this.textureCache = {};

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

    //
    // Load a USDZ/USDA/USDC file from a URL as USD Stage(Freezed scene graph)
    //
    load(url, onLoad, onProgress, onError) {
        console.log('url', url);

        // Create a promise chain to handle initialization and loading
        const initPromise = this.native_ ? Promise.resolve() : this.init();

        initPromise
            .then(() => {
                this.usd_ = new this.native_.TinyUSDZLoaderNative();
                return fetch(url);
            })
            .then((response) => {
                console.log('fetch USDZ file done:', url);
                return response.arrayBuffer();
            })
            .then((usd_data) => {
                const usd_binary = new Uint8Array(usd_data);
                return this.usd_.loadFromBinary(usd_binary);
            })
            .then(() => {
                console.log('# of meshes', this.usd_.numMeshes());
                if (onLoad) {
                    onLoad(this.usd_);
                }
            })
            .catch((error) => {
                console.error('TinyUSDZLoader: Error initializing native module:', error);
                if (onError) {
                    onError(error);
                }
            });
    }

    /**
     * Set texture callback
      */
    setTextureLoader(texLoader) {
        this.texLoader = texLoader;
    }

    // NOTE: Use loadSync() in base Loader class


}

export { TinyUSDZLoader };
