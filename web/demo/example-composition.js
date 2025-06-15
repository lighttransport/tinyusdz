import * as THREE from 'three';
import { HDRCubeTextureLoader } from 'three/addons/loaders/HDRCubeTextureLoader.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

import { FetchAssetResolver, TinyUSDZLoader } from './TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from './TinyUSDZLoaderUtils.js'
import { TinyUSDZComposer } from './TinyUSDZComposer.js'
import { reduceEachLeadingCommentRange } from 'typescript';

const manager = new THREE.LoadingManager();

// Initialize loading manager with URL callback.
const objectURLs = [];
manager.setURLModifier((url) => {

  console.log(url);

  url = URL.createObjectURL(blobs[url]);
  objectURLs.push(url);
  return url;

});

const gui = new GUI();

let ui_state = {}
ui_state['rot_scale'] = 1.0;
ui_state['defaultMtl'] = TinyUSDZLoaderUtils.createDefaultMaterial();

ui_state['envMapIntensity'] = 3.14; // pi is good for pisaHDR;
ui_state['ambient'] = 0.4;
let ambientLight = new THREE.AmbientLight(0x404040, ui_state['ambient']);
ui_state['camera_z'] = 4; // TODO: Compute best fit from scene's bbox.


// Create a parameters object
const params = {
  envMapIntensity: ui_state['envMapIntensity'],
  rotationSpeed: ui_state['rot_scale'],
  camera_z: ui_state['camera_z'],
};

// Add controls
gui.add(params, 'envMapIntensity', 0, 20, 0.1).name('envMapIntensity').onChange((value) => {
  ui_state['envMapIntensity'] = value;
});
gui.add(params, 'camera_z', 0, 20).name('Camera Z').onChange((value) => {
  ui_state['camera_z'] = value;
});
gui.add(params, 'rotationSpeed', 0, 10).name('Rotation Speed').onChange((value) => {
  ui_state['rot_scale'] = value;
});


let assetResolver = new FetchAssetResolver();

// Recursively resolve sublayer assets.
async function resolveSublayerAssets(depth, assetMap, usd_layer, loader) {

  if (depth > 16) {
    console.warn("TinyUSDZComposer: Maximum recursion depth reached while resolving sublayer assets.");
    return;
  }
  const sublayerAssetPaths = TinyUSDZComposer.extractSublayerAssetPaths(usd_layer);
  console.log("extractSublayer", sublayerAssetPaths);

  await Promise.all(sublayerAssetPaths.map(async (sublayerPath) => {
      const [uri, binary] = await assetResolver.resolveAsync(sublayerPath);
      console.log("sublayerPath:", sublayerPath, "binary:", binary.byteLength, "bytes");

      console.log("Loading sublayer:", sublayerPath);
      const sublayer = await loader.loadAsLayerAsync(sublayerPath);

      console.log("sublayer:", sublayer);
      await resolveSublayerAssets(depth + 1, assetMap, sublayer, loader);

      assetMap.set(sublayerPath, binary);
    }));
}

async function progressiveComposition(usd_layer, assetMap, loader) {
  // LIVRPS
  // [x] local(subLayer)
  // [ ] inherits
  // [ ] variants
  // [x] references
  // [ ] payload
  // [ ] specializes

  // Resolving subLayer is recursive.
  await resolveSublayerAssets(0, assetMap, usd_layer, loader);

  for (const [uri, binary] of assetMap.entries()) {
    console.log("setAsset:", uri, "binary:", binary.byteLength, "bytes");
    usd_layer.setAsset(uri, binary);
  }

  if (!usd_layer.composeSublayers()) {
    throw new Error("Failed to compose sublayers:", usd_layer.error());
  }


  // others are iterative.
  const kMaxIter = 16;

  for (let i = 0; i < kMaxIter; i++) {
    if (!TinyUSDZComposer.hasReferences(usd_layer) &&
        !TinyUSDZComposer.hasPayload(usd_layer) &&
        !TinyUSDZComposer.hasInheritsd(usd_layer)) {
          break;
        }

    if (TinyUSDZComposer.hasReferences(usd_layer)) {
      const referencesAssetPaths = TinyUSDZComposer.extractReferencesAssetPaths(usd_layer);

      await Promise.all(referencesAssetPaths.map(async (assetPath) => {
          const [uri, binary] = await assetResolver.resolveAsync(assetPath);
          console.log("referencesPath:", assetPath, "binary:", binary.byteLength, "bytes");

          assetMap.set(uri, binary);
          usd_layer.setAsset(uri, binary);
        }));

      if (!usd_layer.composeReferences()) {
        throw new Error("Failed to compose references:", usd_layer.error());
      }
    }

    if (TinyUSDZComposer.hasPayload(usd_layer)) {
      const payloadAssetPaths = TinyUSDZComposer.extractPayloadAssetPaths(usd_layer);

      await Promise.all(payloadAssetPaths.map(async (assetPath) => {
          const [uri, binary] = await assetResolver.resolveAsync(assetPath);
          console.log("payloadAssetPath:", assetPath, "binary:", binary.byteLength, "bytes");

          assetMap.set(uri, binary);
          usd_layer.setAsset(uri, binary);
        }));

      if (!usd_layer.composePayload()) {
        throw new Error("Failed to compose payload:", usd_layer.error());
      }
    }
  }
}





async function loadScenes() {

  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  await loader.init();

  //const usd_filename = "./usd-composite-sample.usda";
  const usd_filename = "./references-001.usda";

  // TODO: Expose asset resolver callabck
  loader.setEnableComposition(true);

  var threeScenes = []

  const usd_layer = await loader.loadAsLayerAsync(usd_filename);
  //console.log("warn", usd_scene.warn());
  console.log("usd_layer:", usd_layer);

  //
  // Extract sublayer asset paths(file URLs) from the USD subLayer(in recursively).
  // Read asset with fetch() in JS layer.
  // Set asset binary to usd_layer so that the content of asset is found in C++ layer.
  //
  let assetMap = new Map();
  //await resolveSublayerAssets(0, assetMap, usd_layer, loader);
  await progressiveComposition(usd_layer, assetMap, loader);
  //console.log("resolveSublayerAssets done");
  //console.log("assetMap:", assetMap);

  //const sublayerAssetPaths = TinyUSDZComposer.extractSublayerAssetPaths(usd_layer);
  //console.log("extractSublayer", sublayerAssetPaths);

  //await Promise.all(sublayerAssetPaths.map(async (sublayerPath) => {
  //    const [uri, binary] = await assetResolver.resolveAsync(sublayerPath);
  //    console.log("sublayerPath:", sublayerPath, "binary:", binary.byteLength, "bytes");
  //    return usd_layer.setAsset(sublayerPath, binary);
  //  }));


  usd_layer.composedLayerToRenderScene();

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  console.log("numRootNodes:", usd_layer.numRootNodes());
  for (let i = 0; i < usd_layer.numRootNodes(); i++) {
    const usdRootNode = usd_layer.getRootNode(i);
    console.log("usdRootNode:", usdRootNode);

    const threeNode = TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_layer, options); 

    // HACK
    threeNode.position.x += 2 * i;

    threeScenes.push(threeNode);
  }

  return threeScenes;

}



const scene = new THREE.Scene();

const envmap = await new HDRCubeTextureLoader()
  .setPath( 'textures/cube/pisaHDR/' )
  .loadAsync( [ 'px.hdr', 'nx.hdr', 'py.hdr', 'ny.hdr', 'pz.hdr', 'nz.hdr' ] )
scene.background = envmap;
scene.environment = envmap;

// Assign envmap to material
// Otherwise some material parameters like clarcoat will not work properly.
ui_state['defaultMtl'].envMap = envmap;

const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);
camera.position.z = ui_state['camera_z'];

const renderer = new THREE.WebGLRenderer();
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

console.log("loading scenes...");
const rootNodes = await loadScenes();

for (const rootNode of rootNodes) {
  scene.add(rootNode);
}

  function animate() {

    for (const rootNode of rootNodes) {
      rootNode.rotation.y += 0.01 * ui_state['rot_scale'];
      rootNode.rotation.x += 0.02 * ui_state['rot_scale'];
    }

    camera.position.z = ui_state['camera_z'];


    renderer.render(scene, camera);

  }

  renderer.setAnimationLoop(animate);
