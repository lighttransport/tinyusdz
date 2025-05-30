import * as THREE from 'three';

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

// arr = float array with 16 elements(row major order)
function toMatrix4(a) {
  const m = new THREE.Matrix4();

  m.set(a[0], a[1], a[2], a[3],
    a[4], a[5], a[6], a[7],
    a[8], a[9], a[10], a[11],
    a[12], a[13], a[14], a[15]);
}

// scene: threejs Scene
// parentNode: Parent node(Object3D) in Three.js scene graph.
// node: Current node in USD scene graph.
function buildTheeSceneRecursively(usdScene /* TinyUSDZLoader.Scene */, usdNode /* TinyUSDZLoader.Node */)
 /* => THREE.Object3D */ {

  var node = new THREE.Group();

  node.name = node.primName;
  node.custom['displayName'] = node.displayName;
  node.custom['absPath'] = node.absPath;

  if (usdNode.nodeType == 'xform') {
    // intermediate xform node
    // TODO: create THREE.Group and apply transform.
    node.matrix = toMatrix4(node.localMatrix);

  } else if (usdNode.nodeType == 'mesh') {

    // contentId is the mesh ID in the USD scene.
    const mesh = usdScene.getMesh(node.contentId);

    const threeMesh = setupMesh(mesh);
    //threeScene.add(mesh);

  } else {
    // ???

  }

  // traverse children
  for (const child of usdNode.children) {
    const childNode = buildTheeSceneRecursively(threeScene, usdScene, child);
    node.add(childNode);
  }

  return node;

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

  const usd_scene = suzanneData.getScene();
  console.log("scene:", usd_scene);

  const threeScene = buildTheeSceneRecursively(usd_scene); 
  renderer.scene.add(threeScene);




  // TODO: Provide `traverse` function like glTFLoader?


  //const composer = new TinyUSDZComposer();
  //console.log("composer", composer.loaded())

}



const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);
camera.position.z = 5;

const renderer = new THREE.WebGLRenderer();
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

console.log("loading scenes...");
await loadScenes();
