import * as THREE from 'three';
import { HDRCubeTextureLoader } from 'three/addons/loaders/HDRCubeTextureLoader.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

import { TinyUSDZLoader } from './TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from './TinyUSDZLoaderUtils.js'
import { TinyUSDZComposer } from './TinyUSDZComposer.js'
import { createTypeReferenceDirectiveResolutionCache } from 'typescript';

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


async function loadScenes() {

  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  await loader.init();

  const suzanne_filename = "./suzanne-pbr.usda";
  const texcat_filename = "./texture-cat-plane.usdz";
  const cookie_filename = "./UsdCookie.usdz";

  var threeScenes = []

  const usd_scenes = await Promise.all([
    loader.loadAsync(texcat_filename),
    loader.loadAsync(cookie_filename),
    loader.loadAsync(suzanne_filename),
  ]);

  console.log("usd_scenes:", usd_scenes);

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  var offset = -(usd_scenes.length-1) * 1.5;
  for (const usd_scene of usd_scenes) {

    //console.log("usd_scene:", usd_scene);

    const usdRootNode = usd_scene.getDefaultRootNode();
    //console.log("scene:", usdRootNode);

    const threeNode = TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options); 

    // HACK
    if (usd_scene.getURI().includes('UsdCookie')) {
      //console.log("UsdCookie");
      // Add exra scaling
      threeNode.scale.x *= 2.5;
      threeNode.scale.y *= 2.5;
      threeNode.scale.z *= 2.5;
    }

    // HACK
    threeNode.position.x += offset;
    offset += 3.0;

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
