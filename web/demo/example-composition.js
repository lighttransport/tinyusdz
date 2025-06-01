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

ui_state['exposure'] = 3.0;
ui_state['ambient'] = 0.4;
let ambientLight = new THREE.AmbientLight(0x404040, ui_state['ambient']);
ui_state['camera_z'] = 4; // TODO: Compute best fit from scene's bbox.
ui_state['shader_normal'] = false;
ui_state['material_changed'] = false;

// Default PBR mateiral params
ui_state['diffuse'] = new THREE.Color(49, 49, 49); // 0.18
ui_state['emissive'] = new THREE.Color(0, 0, 0);
ui_state['roughness'] = 0.5;
ui_state['metalness'] = 0.0;
ui_state['clearcoat'] = 0.0;
ui_state['clearcoatRoughness'] = 0.0;
ui_state['ior'] = 1.5;
ui_state['specularIntensity'] = 1.0;
ui_state['opacity'] = 1.0;

// Create a parameters object
const params = {
  rotationSpeed: ui_state['rot_scale'],
  camera_z: ui_state['camera_z'],
  shader_normal: ui_state['shader_normal'],
  diffuse: ui_state['diffuse'],
  emissive: ui_state['emissive'],
  roughness: ui_state['roughness'],
  metalness: ui_state['metalness'],
  clearcoat: ui_state['clearcoat'],
  clearcoatRoughness: ui_state['clearcoatRoughness'],
  specularIntensity: ui_state['specularIntensity'],
  opacity: ui_state['opacity'],
  ior: ui_state['ior'],
};

// Add controls
gui.add(params, 'camera_z', 0, 20).name('Camera Z').onChange((value) => {
  ui_state['camera_z'] = value;
});
gui.add(params, 'rotationSpeed', 0, 10).name('Rotation Speed').onChange((value) => {
  ui_state['rot_scale'] = value;
});

gui.addColor(params, 'diffuse').name('color').onChange((value) => {
  ui_state['diffuse'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.addColor(params, 'emissive').name('emissive').onChange((value) => {
  ui_state['emissive'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'roughness', 0.0, 1.0, 0.01).name('roughness').onChange((value) => {
  ui_state['roughness'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'metalness', 0.0, 1.0, 0.01).name('metalness').onChange((value) => {
  ui_state['metalness'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'ior', 1.0, 2.4, 0.1).name('ior').onChange((value) => {
  ui_state['ior'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'clearcoat', 0.0, 1.0, 0.01).name('clearcoat').onChange((value) => {
  ui_state['clearcoat'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'clearcoatRoughness', 0.0, 1.0).step(0.01).name('clearcoatRoughness').onChange((value) => {
  ui_state['clearcoatRoughness'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'specularIntensity', 0.0, 1.0, 0.01).name('specular').onChange((value) => {
  ui_state['specularIntensity'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

gui.add(params, 'opacity', 0.0, 1.0, 0.01).name('opacity').onChange((value) => {
  ui_state['opacity'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});

/* TODO
gui.add(params, 'transmission', 0.0, 1.0, 0.01).name('transmission').onChange((value) => {
  ui_state['transmission'] = value;

  ui_state['material_changed'] = true;
  //jrenderer.toneMappingExposure = exposure;
});
*/

gui.add(params, 'shader_normal').name('NormalMaterial').onChange((value) => {
  ui_state['shader_normal'] = value;

  ui_state['material_changed'] = true;
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

  // faceVarying normals
  if (mesh.hasOwnProperty('normals')) {
    geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
  } else {
    geometry.computeVertexNormals();
  }

  if (mesh.hasOwnProperty('vertexColors')) {
    geometry.setAttribute('color', new THREE.BufferAttribute(mesh.vertexColors, 3));

  }

  // Only compute tangents if we have both UV coordinates and normals
  if (mesh.hasOwnProperty('tangents')) {
    geometry.setAttribute('tangent', new THREE.BufferAttribute(mesh.tangents, 3));
  } else if (mesh.hasOwnProperty('texcoords') && (mesh.hasOwnProperty('normals') || geometry.attributes.normal)) {
    // TODO: try MikTSpace tangent algorithm: https://threejs.org/docs/#examples/en/utils/BufferGeometryUtils.computeMikkTSpaceTangents 
    geometry.computeTangents();
  }

  // TODO: vertex opacities(per-vertex alpha)

  return geometry;
}


function setupMesh(mesh /* TinyUSDZLoaderNative::RenderMesh */, defaultMtl, usdScene) {

  // First mesh only
  console.log("mesh loaded:", mesh);

  const geometry = usdMeshToThreeMesh(mesh);

  const usdMaterial = usdScene.getMaterial(mesh.materialId);
  console.log("usdMaterial:", usdMaterial);

  //const pbrMaterial = TinyUSDZLoader.ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd);

  const normalMat = new THREE.MeshNormalMaterial();

  const usd_util = new TinyUSDZLoaderUtils();

  // HACK
  const threeMesh = new THREE.Mesh(geometry, /*baseMat*/ defaultMtl);

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

    // HACK: material
    const threeMesh = setupMesh(mesh, ui_state['defaultMtl'], usdScene);
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
    //loader.loadAsync(cookie_filename),
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

    if (ui_state['material_changed']) {
      ui_state['material_changed'] = false;

      if (ui_state['shader_normal']) {
        //mesh0.material = normalMat;

        for (const rootNode of rootNodes) {
          rootNode.rotation.y += 0.01 * ui_state['rot_scale'];
          rootNode.rotation.x += 0.02 * ui_state['rot_scale'];
        }
      } else {
        //mesh0.material = pbrMaterial;
      }


      // HACK
      ui_state['defaultMtl'].color.r = ui_state['diffuse'].r / 255.0;
      ui_state['defaultMtl'].color.g = ui_state['diffuse'].g / 255.0;
      ui_state['defaultMtl'].color.b = ui_state['diffuse'].b / 255.0;
      console.log("diffuse", ui_state['diffuse']);
      console.log("mat_diffuse", ui_state['defaultMtl'].color);

      ui_state['defaultMtl'].emissive.r = ui_state['emissive'].r / 255.0;
      ui_state['defaultMtl'].emissive.g = ui_state['emissive'].g / 255.0;
      ui_state['defaultMtl'].emissive.b = ui_state['emissive'].b / 255.0;

      ui_state['defaultMtl'].roughness = ui_state['roughness'];
      ui_state['defaultMtl'].metalness = ui_state['metalness'];
      ui_state['defaultMtl'].ior = ui_state['ior'];
      ui_state['defaultMtl'].clearcoat = ui_state['clearcoat'];
      ui_state['defaultMtl'].clearcoatRoughness = ui_state['clearcoatRoughness'];
      ui_state['defaultMtl'].specularIntensity = ui_state['specularIntensity'];
      ui_state['defaultMtl'].opacity = ui_state['opacity'];
      ui_state['defaultMtl'].transparent = (ui_state['opacity'] < 1.0);

      ui_state['defaultMtl'].needsUpdate = true;
    }


    renderer.render(scene, camera);

  }

  renderer.setAnimationLoop(animate);