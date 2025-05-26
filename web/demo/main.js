import * as THREE from 'three';
//import * as THREE from 'https://cdn.jsdelivr.net/npm/three/build/three.module.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

//import initTinyUSDZ from 'https://lighttransport.github.io/tinyusdz/tinyusdz.js';
// For developer
//import initTinyUSDZ from './tinyusdz.js';
import { TinyUSDZLoader } from './TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from './TinyUSDZLoaderUtils.js'

//const USDZ_FILEPATH = './UsdCookie.usdz';

//const usd_res = await fetch(USDZ_FILEPATH);
//const usd_data = await usd_res.arrayBuffer();
//const usd_binary = new Uint8Array(usd_data);


/*
initTinyUSDZ().then(async function(TinyUSDZLoaderNative) {

  // Setup the async loader helper before attempting to use it
  //console.log("Setting up async loader...");
  //TinyUSDZLoaderNative.setupAsyncLoader();
  //console.log("Async loader setup complete.");

  const gui = new GUI();

  // FIXME
  let y_rot_value = 0.02;
  let exposure = 3.0;
  let ambient = 0.4
  let ambientLight = new THREE.AmbientLight(0x404040, ambient);
  
  // Create a parameters object
  const params = {
    rotationSpeed: y_rot_value,
    wireframe: false,
    ambient: ambient,
    exposure: exposure,
  };

  // Add controls
  gui.add(params, 'rotationSpeed', 0, 0.1).name('Rotation Speed').onChange((value) => {
    y_rot_value = value;
  });
  gui.add(params, 'ambient', 0, 10).name('Ambient').onChange((value) => {
    ambient = value;
    ambientLight.intensity = ambient;
  });
  gui.add(params, 'exposure', 0, 10).name('Intensity').onChange((value) => {
    exposure = value;
    renderer.toneMappingExposure = exposure;
  });

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera( 75, window.innerWidth / window.innerHeight, 0.1, 1000 );

  const renderer = new THREE.WebGLRenderer();
  renderer.setSize( window.innerWidth, window.innerHeight );
  
  // Set up better rendering for PBR materials
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = exposure;
  renderer.outputEncoding = THREE.sRGBEncoding;
  
  document.body.appendChild( renderer.domElement );

  // Add basic lighting for PBR materials
  scene.add(ambientLight);
  
  const directionalLight = new THREE.DirectionalLight(0xffffff, 1.0);
  directionalLight.position.set(5, 5, 5);
  scene.add(directionalLight);

  // Add loading indicator
  const loadingDiv = document.createElement('div');
  loadingDiv.innerHTML = 'Loading USD file...';
  loadingDiv.style.position = 'absolute';
  loadingDiv.style.top = '50%';
  loadingDiv.style.left = '50%';
  loadingDiv.style.transform = 'translate(-50%, -50%)';
  loadingDiv.style.color = 'white';
  loadingDiv.style.fontSize = '20px';
  loadingDiv.style.backgroundColor = 'rgba(0, 0, 0, 0.7)';
  loadingDiv.style.padding = '20px';
  loadingDiv.style.borderRadius = '10px';
  document.body.appendChild(loadingDiv);

  try {
    // Create loader instance without loading
    const usd = new TinyUSDZLoaderNative.TinyUSDZLoaderNative();
    
    // Load asynchronously with detailed error handling
    console.log('Starting async USD loading...');
    console.log('Module functions:', Object.keys(TinyUSDZLoaderNative).join(', '));
    console.log('usd_binary length:', usd_binary.length);
    
    try {
      await usd.loadAsync(usd_binary);
      console.log('USD loading completed!');
    } catch (error) {
      console.error('Async loading error:', error);
      throw new Error(`Failed to load USD file asynchronously: ${error.message}`);
    }
    console.log('USD loading completed!');
    
    // Remove loading indicator
    document.body.removeChild(loadingDiv);
    
    // First mesh only
    const mesh = usd.getMesh(0);
    console.log("mesh loaded:", mesh);

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute( 'position', new THREE.BufferAttribute( mesh.points, 3 ) );

    if (mesh.hasOwnProperty('texcoords')) {
      geometry.setAttribute( 'uv', new THREE.BufferAttribute( mesh.texcoords, 2 ) );
    }

    const usdMaterial = usd.getMaterial(mesh.materialId);
    console.log("usdMaterial:", usdMaterial);

    var material;

    // Use the proper material conversion function
    material = ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd);
     
    // Assume triangulated indices.
    geometry.setIndex( new THREE.Uint32BufferAttribute(mesh.faceVertexIndices, 1) );

    geometry.computeVertexNormals();

    const mesh0 = new THREE.Mesh( geometry, material );
    scene.add( mesh0 );

    camera.position.z = 1.0;

    function animate() {
      mesh0.rotation.y += y_rot_value;
      renderer.render( scene, camera );
    }

    renderer.setAnimationLoop( animate );
    
  } catch (error) {
    console.error('Failed to load USD file:', error);
    
    // Remove loading indicator
    if (document.body.contains(loadingDiv)) {
      document.body.removeChild(loadingDiv);
    }
    
    // Show error message
    const errorDiv = document.createElement('div');
    errorDiv.innerHTML = `Error loading USD file: ${error.message}`;
    errorDiv.style.position = 'absolute';
    errorDiv.style.top = '50%';
    errorDiv.style.left = '50%';
    errorDiv.style.transform = 'translate(-50%, -50%)';
    errorDiv.style.color = 'red';
    errorDiv.style.fontSize = '16px';
    errorDiv.style.backgroundColor = 'rgba(0, 0, 0, 0.8)';
    errorDiv.style.padding = '20px';
    errorDiv.style.borderRadius = '10px';
    document.body.appendChild(errorDiv);
  }
});
*/

const gui = new GUI();

// FIXME
let y_rot_value = 0.02;
let exposure = 3.0;
let ambient = 0.4
let ambientLight = new THREE.AmbientLight(0x404040, ambient);
let camera_z = 1.4; // TODO: Compute best fit from scene's bbox.
let shader_normal = false;
let material_changed = false;

// Create a parameters object
const params = {
  rotationSpeed: y_rot_value,
  wireframe: false,
  ambient: ambient,
  exposure: exposure,
  camera_z: camera_z,
  shader_normal: shader_normal,
};

// Add controls
gui.add(params, 'camera_z', 0, 10).name('Camera Z').onChange((value) => {
  camera_z = value;
});
gui.add(params, 'rotationSpeed', 0, 0.1).name('Rotation Speed').onChange((value) => {
  y_rot_value = value;
});
gui.add(params, 'ambient', 0, 10).name('Ambient').onChange((value) => {
  ambient = value;
  ambientLight.intensity = ambient;
});
gui.add(params, 'exposure', 0, 10).name('Intensity').onChange((value) => {
  exposure = value;
  renderer.toneMappingExposure = exposure;
});
gui.add(params, 'shader_normal').name('NormalMaterial').onChange((value) => {
  shader_normal = value;
  material_changed = true;
  //jrenderer.toneMappingExposure = exposure;
});

const usdLoader = new TinyUSDZLoader();
const usd = await usdLoader.loadAsync('./UsdCookie.usdz');
console.log('USD file loaded:', usd);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);

const renderer = new THREE.WebGLRenderer();
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setAnimationLoop(animate);
document.body.appendChild(renderer.domElement);

//const geometry = new THREE.BoxGeometry(1, 1, 1);
//const material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
//const cube = new THREE.Mesh(geometry, material);
//scene.add(cube);

camera.position.z = 5;

function usdMeshToThreeMesh( mesh )
{
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute( 'position', new THREE.BufferAttribute( mesh.points, 3 ) );

    // Assume mesh is triangulated.
    // itemsize = 1 since Index expects IntArray for VertexIndices in Three.js?
    geometry.setIndex(new THREE.BufferAttribute( mesh.faceVertexIndices, 1 ));

    if (mesh.hasOwnProperty('texcoords')) {
      geometry.setAttribute( 'uv', new THREE.BufferAttribute( mesh.texcoords, 2 ) );
    }

    //// faceVarying normals
    if (mesh.hasOwnProperty('normals')) {
      geometry.setAttribute( 'normal', new THREE.BufferAttribute( mesh.normals, 3 ) );
    } else {
      geometry.computeVertexNormals();
    }

    return geometry;
}

if (usd.numMeshes() < 1) {
  console.error("No meshes in USD");
}

// First mesh only
const mesh = usd.getMesh(0);
console.log("mesh loaded:", mesh);

const geometry = usdMeshToThreeMesh( mesh );

const usdMaterial = usd.getMaterial(mesh.materialId);
console.log("usdMaterial:", usdMaterial);

//const pbrMaterial = TinyUSDZLoader.ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd);
const pbrMaterial = new THREE.MeshPhysicalMaterial();

const normalMat = new THREE.MeshNormalMaterial();

const usd_util = new TinyUSDZLoaderUtils();

const baseMat = TinyUSDZLoaderUtils.createDefaultMaterial();

const mesh0 = new THREE.Mesh(geometry, baseMat);
//const mesh0 = new THREE.Mesh(geometry, baseMat);
scene.add(mesh0);

function animate() {

  //cube.rotation.x += 0.01;
  mesh0.rotation.y += y_rot_value;
  camera.position.z = camera_z;

  if (material_changed) {
    material_changed = false;

    if (shader_normal) {
      mesh0.material = normalMat;
    } else {
      mesh0.material = pbrMaterial;
    }
  }

    
  
  renderer.render(scene, camera);

}

