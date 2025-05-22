//import * as THREE from 'three';
import * as THREE from 'https://cdn.jsdelivr.net/npm/three/build/three.module.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

//import initTinyUSDZ from 'https://lighttransport.github.io/tinyusdz/tinyusdz.js';
// For developer
import initTinyUSDZ from './tinyusdz.js';

const USDZ_FILEPATH = './UsdCookie.usdz';

const usd_res = await fetch(USDZ_FILEPATH);
const usd_data = await usd_res.arrayBuffer();

const usd_binary = new Uint8Array(usd_data);

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


initTinyUSDZ().then(function(TinyUSDZLoader) {

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
    //color: material.color.getHex()
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
  //gui.addColor(params, 'color').onChange((value) => {
  //  material.color.setHex(value);
  //});

  const usd = new TinyUSDZLoader.TinyUSDZLoader(usd_binary);
  //console.log(usd.numMeshes());

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera( 75, window.innerWidth / window.innerHeight, 0.1, 1000 );

  const renderer = new THREE.WebGLRenderer();
  renderer.setSize( window.innerWidth, window.innerHeight );
  renderer.setAnimationLoop( animate );
  
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

  // First mesh only
  const mesh = usd.getMesh(0);
  //console.log("usd", usd)
  //console.log("mesh", mesh);

  //const geometry = new THREE.BoxGeometry( 1, 1, 1 );
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute( 'position', new THREE.BufferAttribute( mesh.points, 3 ) );
  // TODO: set normal from mesh

  if (mesh.hasOwnProperty('texcoords')) {
    //console.log(mesh.texcoords);
    geometry.setAttribute( 'uv', new THREE.BufferAttribute( mesh.texcoords, 2 ) );
  }

  const usdMaterial = usd.getMaterial(mesh.materialId);
  //console.log("usdMat", usdMaterial);
  //if (usdMaterial.aaa) {
  //  console.log("aaa");
  //}

  var material;

  // Use the proper material conversion function
  material = ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial, usd);
   

  // Assume triangulated indices.
  geometry.setIndex( new THREE.Uint32BufferAttribute(mesh.faceVertexIndices, 1) );

  geometry.computeVertexNormals();

  //const material = new THREE.MeshBasicMaterial( { color: 0x00ff00 } );
  const mesh0 = new THREE.Mesh( geometry, material );
  scene.add( mesh0 );

  //camera.position.z = 25;
  camera.position.z = 1.0;

  function animate() {

    //cube.rotation.x += 0.01;
    mesh0.rotation.y += y_rot_value;

    renderer.render( scene, camera );

  }
});
