import * as THREE from 'three';
import { HDRCubeTextureLoader } from 'three/addons/loaders/HDRCubeTextureLoader.js';

import { GUI } from 'lil-gui';

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js'

function checkMemory64Support() {
  try {
    // Try creating a 64-bit memory
    const memory = new WebAssembly.Memory({
      initial: 1,
      maximum: 65536,
      index: 'i64'  // This specifies 64-bit indexing
    });
    return true;
  } catch (e) {
    return false;
  }
}


// Loading bar elements
const loadingContainer = document.createElement('div');
loadingContainer.id = 'loading-container';
loadingContainer.style.cssText = `
  position: fixed;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  background: rgba(0, 0, 0, 0.8);
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  z-index: 1000;
  font-family: Arial, sans-serif;
  color: white;
`;

const loadingText = document.createElement('div');
loadingText.id = 'loading-text';
loadingText.textContent = 'Loading...';
loadingText.style.cssText = `
  font-size: 24px;
  margin-bottom: 20px;
`;

const progressBarContainer = document.createElement('div');
progressBarContainer.style.cssText = `
  width: 300px;
  height: 20px;
  background: #333;
  border-radius: 10px;
  overflow: hidden;
  margin-bottom: 10px;
`;

const progressBar = document.createElement('div');
progressBar.id = 'progress-bar';
progressBar.style.cssText = `
  width: 0%;
  height: 100%;
  background: linear-gradient(90deg, #4CAF50, #8BC34A);
  border-radius: 10px;
  transition: width 0.3s ease;
`;

const progressText = document.createElement('div');
progressText.id = 'progress-text';
progressText.textContent = '0%';
progressText.style.cssText = `
  font-size: 14px;
  color: #ccc;
`;

progressBarContainer.appendChild(progressBar);
loadingContainer.appendChild(loadingText);
loadingContainer.appendChild(progressBarContainer);
loadingContainer.appendChild(progressText);
document.body.appendChild(loadingContainer);

// Function to update loading progress
function updateLoadingProgress(progress, message = 'Loading...') {
  loadingText.textContent = message;
  progressBar.style.width = `${progress}%`;
  progressText.textContent = `${Math.round(progress)}%`;
}

// Function to hide loading screen
function hideLoadingScreen() {
  loadingContainer.style.display = 'none';
}



const gui = new GUI();

let ui_state = {}
ui_state['rot_scale'] = 1.0;
ui_state['defaultMtl'] = TinyUSDZLoaderUtils.createDefaultMaterial();

ui_state['envMapIntensity'] = 3.14; // pi is good for pisaHDR;
ui_state['ambient'] = 0.4;
let ambientLight = new THREE.AmbientLight(0x404040, ui_state['ambient']);
ui_state['camera_z'] = 3.14; // TODO: Compute best fit from scene's bbox.
ui_state['needsMtlUpdate'] = false;
ui_state['renderer'] = null;


// Create a parameters object
const params = {
  envMapIntensity: ui_state['envMapIntensity'],
  rotationSpeed: ui_state['rot_scale'],
  camera_z: ui_state['camera_z'],
  take_screenshot: takeScreenshot
};

// Add controls
gui.add(params, 'envMapIntensity', 0, 20, 0.1).name('envMapIntensity').onChange((value) => {
  ui_state['envMapIntensity'] = value;
  ui_state['needsMtlUpdate'] = true;

});
gui.add(params, 'camera_z', 0, 20).name('Camera Z').onChange((value) => {
  ui_state['camera_z'] = value;
});
gui.add(params, 'rotationSpeed', 0, 10).name('Rotation Speed').onChange((value) => {
  ui_state['rot_scale'] = value;
});
gui.add(params, 'take_screenshot').name('Take Screenshot');


function takeScreenshot() {

  const renderer = ui_state['renderer'];
  const quality = 0.92; // JPEG quality, if you want to use JPEG format

  const img = renderer.domElement.toDataURL('image/jpeg', quality)
  console.log('Screenshot taken:', img);

  return img;
}

async function loadScenes() {

  updateLoadingProgress(20, 'Initializing TinyUSDZLoader...');

  // Create loader with optional memory limit
  // Default: 2GB for WASM32, 8GB for WASM64
  // const loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 }); // Set 512MB limit
  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  //await loader.init();

  const useMemory64 = checkMemory64Support();
  console.log('64-bit memory support:', useMemory64);
  await loader.init({useMemory64});
  TinyUSDZLoaderUtils.setTinyUSDZ(loader.native_);

  // You can set memory limit for USD loading.
  // The limit is only effective to USD loading.
  // No limit for asset data(e.g. textures) and Three.js data, etc.
  loader.setMaxMemoryLimitMB(250);


  // Use zstd compressed tinyusdz.wasm to save the bandwidth.
  //await loader.init({useZstdCompressedWasm: true});

  const suzanne_filename = "./assets/suzanne-pbr.usda";
  const texcat_filename = "./assets/texture-cat-plane.usdz";
  const cookie_filename = "./assets/UsdCookie.usdz";
  //const usd_filename = "./assets/suzanne-pbr.usda";
  const usd_filename = "./assets/suzanne-subd-lv6.usdc";

  var threeScenes = []

  const usd_scenes = await Promise.all([
    //loader.loadAsync(texcat_filename),
    loader.loadAsync(usd_filename),
    //loader.loadAsync(suzanne_filename),
  ]);

  hideLoadingScreen();

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  var offset = -(usd_scenes.length - 1) * 1.5;
  for (const usd_scene of usd_scenes) {

    const usdRootNode = usd_scene.getDefaultRootNode();

    const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options);

    if (usd_scene.getURI().includes('UsdCookie')) {
      // Add exra scaling
      threeNode.scale.x *= 2.5;
      threeNode.scale.y *= 2.5;
      threeNode.scale.z *= 2.5;
    }

    threeNode.position.x += offset;
    offset += 3.0;

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

  const renderer = new THREE.WebGLRenderer({
    preserveDrawingBuffer: true, // for screenshot
    alpha: true, // Enable transparency
    antialias: true
  });
  renderer.setSize(window.innerWidth, window.innerHeight);
  ui_state['renderer'] = renderer; // Store renderer in ui_state
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

    if (ui_state['needsMtlUpdate']) {

      // TODO: Cache materials in the scene.
      scene.traverse((object) => {
        if (object.material) {
          if (Object.prototype.hasOwnProperty.call(object.material, 'envMapIntensity')) {
            object.material.envMapIntensity = ui_state['envMapIntensity'];
            object.material.needsUpdate = true;
          }
        }
      });

      ui_state['needsMtlUpdate'] = false;
    }

    renderer.render(scene, camera);

  }

  renderer.setAnimationLoop(animate);
}

initScene();
