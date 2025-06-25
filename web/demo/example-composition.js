import * as THREE from 'three';
import { HDRCubeTextureLoader } from 'three/addons/loaders/HDRCubeTextureLoader.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js'
import { TinyUSDZComposer } from 'tinyusdz/TinyUSDZComposer.js'

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


async function loadScenes() {

  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  await loader.init();

  const usd_filename = "./assets/usd-composite-sample.usda"; // Read two suzanne model as sublayer.
  //const usd_filename = "./assets/references-001.usda"; // Read Suzanne.usda as reference.
  //const usd_filename = "./assets/references-002.usda"; // read UsdCookie.usdz as reference: No textures(this is expected behavior)
  //const usd_filename = "./assets/references-003.usda"; // read texture-cat-plane.usda as reference: Do texturing(use three.js's TextureLoader)

  //
  // ============================================================
  // Loading USD and do USD composition.
  // 1. First load root USD file as a Layer.
  // 2. Setup USDZComposer
  // 3. Do USD composition(USDZComposer.progressiveComposition)
  // 4. Convert composited USD Layer to RenderScene(Three.js friendly scene graph)
  //
  // ============================================================
  //

  let usd_layer = await loader.loadAsLayerAsync(usd_filename);

  let composer = new TinyUSDZComposer();
  composer.setLayer(usd_layer);
  composer.setUSDLoader(loader);

  // NOTE: baseDir and assetSearchPaths are w.i.p.
  composer.setBaseWorkingPath("./assets");
  composer.setAssetSearchPaths(["./assets"]); // optional

  await composer.progressiveComposition();

  // layer(usd_layer) in the Composer instance now contains composited USD layer
  usd_layer = composer.getLayer();

  // Dump composited USD layer.
  //console.log(usd_layer.layerToString()); 

  // Convert layer(or compositedLayer) to RenderScene(Three.js friendly scene graph)
  usd_layer.layerToRenderScene();

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  var threeScenes = []

  const xoffset = -(usd_layer.numRootNodes() - 1);
  for (let i = 0; i < usd_layer.numRootNodes(); i++) {
    const usdRootNode = usd_layer.getRootNode(i);

    const threeNode = TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_layer, options);

    // HACK
    threeNode.position.x += 2 * i + xoffset;

    threeScenes.push(threeNode);
  }

  return threeScenes;

}



const scene = new THREE.Scene();

async function initScene() {

  const envmap = await new HDRCubeTextureLoader()
    .setPath('assets/textures/cube/pisaHDR/')
    .loadAsync(['px.hdr', 'nx.hdr', 'py.hdr', 'ny.hdr', 'pz.hdr', 'nz.hdr'])
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

}

initScene();
