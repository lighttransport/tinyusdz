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
let rot_scale = 1;
let exposure = 3.0;
let ambient = 0.4
let ambientLight = new THREE.AmbientLight(0x404040, ambient);
let camera_z = 1.4; // TODO: Compute best fit from scene's bbox.
let shader_normal = false;
let material_changed = false;

// Create a parameters object
const params = {
  rotationSpeed: rot_scale,
  camera_z: camera_z,
  shader_normal: shader_normal,
};

// Add controls
gui.add(params, 'camera_z', 0, 10).name('Camera Z').onChange((value) => {
  camera_z = value;
});
gui.add(params, 'rotationSpeed', 0, 10).name('Rotation Speed').onChange((value) => {
  rot_scale = value;
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


function setupMesh(mesh /* TinyUSDZLoaderNative::RenderMesh */, usdScene) {

  // First mesh only
  console.log("mesh loaded:", mesh);

  const geometry = usdMeshToThreeMesh(mesh);

  const usdMaterial = usdScene.getMaterial(mesh.materialId);
  console.log("usdMaterial:", usdMaterial);

  //const pbrMaterial = TinyUSDZLoader.ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd);
  const pbrMaterial = new THREE.MeshPhysicalMaterial();

  const normalMat = new THREE.MeshNormalMaterial();

  const usd_util = new TinyUSDZLoaderUtils();

  const baseMat = TinyUSDZLoaderUtils.createDefaultMaterial();

  // HACK
  const threeMesh = new THREE.Mesh(geometry, /*baseMat*/ normalMat);

  return threeMesh;
}

// arr = float array with 16 elements(row major order)
function toMatrix4(a) {
  const m = new THREE.Matrix4();

  m.set(a[0], a[1], a[2], a[3],
    a[4], a[5], a[6], a[7],
    a[8], a[9], a[10], a[11],
    a[12], a[13], a[14], a[15]);

  return m;
}

function buildThreeNodeRecursively(usdNode /* TinyUSDZLoader.Node */, usdScene /* TinyUSDZLoader.Scene */ = null)
 /* => THREE.Object3D */ {

  var node = new THREE.Group();

  if (usdNode.nodeType == 'xform') {

    // intermediate xform node
    // TODO: create THREE.Group and apply transform.
    node.matrix = toMatrix4(usdNode.localMatrix);

  } else if (usdNode.nodeType == 'mesh') {

    //console.log("usdScene:", usdScene);
    // contentId is the mesh ID in the USD scene.
    const mesh = usdScene.getMesh(usdNode.contentId);
    console.log("mesh:", mesh);

    const threeMesh = setupMesh(mesh, usdScene);
    node = threeMesh;

  } else {
    // ???

  }

  node.name = usdNode.primName;
  node.userData['primMeta.displayName'] = usdNode.displayName;
  node.userData['primMeta.absPath'] = usdNode.absPath;


  // traverse children
  for (const child of usdNode.children) {
    const childNode = buildThreeNodeRecursively(child, usdScene);
    node.add(childNode);
  }

  return node;

}

async function loadScenes() {

  const loader = new TinyUSDZLoader();

  const suzanne_filename = "./suzanne.usdc";
  const cookie_filename = "./UsdCookie.usdz";

  var threeScenes = []

  const usd_scenes = await Promise.all([
    loader.loadAsync(cookie_filename),
    loader.loadAsync(suzanne_filename),
  ]);

  console.log("usd_scenes:", usd_scenes);

  var xoffset = 0;
  for (const usd_scene of usd_scenes) {

    console.log("usd_scene:", usd_scene);

    const usdRootNode = usd_scene.getDefaultRootNode();
    console.log("scene:", usdRootNode);

    const threeNode = buildThreeNodeRecursively(usdRootNode, usd_scene); 

    // HACK
    threeNode.position.x += xoffset;
    xoffset += 2.0

    threeScenes.push(threeNode);
  }

  return threeScenes;

}



const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);
camera.position.z = 5;

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
      rootNode.rotation.y += 0.01 * rot_scale;
      rootNode.rotation.x += 0.02 * rot_scale;
    }

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

  renderer.setAnimationLoop(animate);