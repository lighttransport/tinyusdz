import * as THREE from 'three';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

import { TinyUSDZLoader } from './TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from './TinyUSDZLoaderUtils.js'
import { TinyUSDZComposer } from './TinyUSDZComposer.js'

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
gui.add(params, 'shader_normal').name('NormalMaterial').onChange((value) => {
  shader_normal = value;
  material_changed = true;
  //jrenderer.toneMappingExposure = exposure;
});

function usdMeshToThreeMesh(mesh) {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(mesh.points, 3));

  // Assume mesh is triangulated.
  // itemsize = 1 since Index expects IntArray for VertexIndices in Three.js?
  geometry.setIndex(new THREE.BufferAttribute(mesh.faceVertexIndices, 1));

  if (mesh.hasOwnProperty('texcoords')) {
    geometry.setAttribute('uv', new THREE.BufferAttribute(mesh.texcoords, 2));
  }

  //// faceVarying normals
  if (mesh.hasOwnProperty('normals')) {
    geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
  } else {
    geometry.computeVertexNormals();
  }

  return geometry;
}


function setupMesh(usd) {

 // First mesh only
  const mesh = usd.getMesh(0);
  console.log("mesh loaded:", mesh);

  const geometry = usdMeshToThreeMesh(mesh);

  const usdMaterial = usd.getMaterial(mesh.materialId);
  console.log("usdMaterial:", usdMaterial);

  //const pbrMaterial = TinyUSDZLoader.ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd);
  const pbrMaterial = new THREE.MeshPhysicalMaterial();

  const normalMat = new THREE.MeshNormalMaterial();

  const usd_util = new TinyUSDZLoaderUtils();

  const baseMat = TinyUSDZLoaderUtils.createDefaultMaterial();

  const threeMesh = new THREE.Mesh(geometry, baseMat);

  return threeMesh;
}

async function loadScenes() {

  const loader = new TinyUSDZLoader();

  const suzanne_filename = "./suzanne.usdc";
  const cookie_filename = "./UsdCookie.usdc";

  const [cookieData, suzanneData] = await Promise.all([
    loader.loadAsync(cookie_filename),
    loader.loadAsync(suzanne_filename),
  ]);

  console.log("loaded!");

}



const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);
camera.position.z = 5;

const renderer = new THREE.WebGLRenderer();
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

