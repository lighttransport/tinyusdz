//import * as THREE from 'three';
import * as THREE from 'https://cdn.jsdelivr.net/npm/three/build/three.module.js';

import initTinyUSDZ from 'https://lighttransport.github.io/tinyusdz/tinyusdz.js';

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
// - [x] roughness -> specularRoughness 
// - [x] metallic -> metalness
// - [x] emissiveColor -> emissive
// - [x] opacity -> opacity
// - [x] occlusion -> aoMap
// - [x] normal -> normalMap
// - [ ] TODO: displacement -> displacementMap
function ConvertUsdPreviewSurfaceToMeshPhysicalMaterial(usdMaterial) {
  const material = new THREE.MeshPhysicalMaterial();

  material.color = new THREE.Color(0.18, 0.18, 0.18)
  if (usdMaterial.hasOwnProperty('diffuseColor')) {
    const color = usdMaterial.diffuseColor;
    material.color = new THREE.Color(color[0], color[1], color[2]);
  } 

  material.ior = 1.5;
  if (usdMaterial.hasOwnProperty('ior')) {
    material.ior = usdMaterial.ior;
  }

  material.clearcoat = 0.0;
  if (usdMaterial.hasOwnProperty('clearcoat')) {
    material.clearcoat = usdMaterial.clearcoat;
  }   

  material.clearcoatRoughness = 0.0;
  if (usdMaterial.hasOwnProperty('clearcoatRoughness')) {
    material.clearcoatRoughness = usdMaterial.clearcoatRoughness;
  }

  material.useSpecularWorkflow = false;
  if (usdMaterial.hasOwnProperty('useSpecularWorkflow')) 
    material.useSpecularWorkflow = usdMaterial.useSpecularWorkflow;
  }

  if (material.useSpecularWorkflow) {
    material.specular = new THREE.Color(0.0, 0.0, 0.0);
    if (usdMaterial.hasOwnProperty('specularColor')) {
      const color = usdMaterial.specularColor;
      material.specular = new THREE.Color(color[0], color[1], color[2]);
    }
  } else {
    material.metalness = 0.0;
    if (usdMaterial.hasOwnProperty('metallic')) {
      material.metalness = usdMaterial.metallic;
    }
  }

  if (usdMateiral.hasOwnProperty('roughness')) {
    material.specularRoughness = usdMaterial.roughness;
  }

  if (usdMaterial.hasOwnProperty('emissiveColor')) {
    const color = usdMaterial.emissiveColor;
    material.emissive = new THREE.Color(color[0], color[1], color[2]);
  } 

  // TODO: aoMap
  //if (usdMaterial.hasOwnProperty('occlusionTextureId')) {
  //  const occlusionTex = usdMaterial.occlusionTextureId;
  //  const img = usd.getImage(occlusionTex.textureImageId);
  //  console.log(img);

  //  // assume RGBA for now.
  //  let image8Array = new Uint8ClampedArray(img.data);
  //  let imgData = new ImageData(image8Array, img.width, img.height);

  //  const texture = new THREE.DataTexture( imgData, img.width, img.height );
  //  texture.flipY = true;
  //  texture.needsUpdate = true;

  //  material.aoMap = texture;
  //} 


  // TOOD: displacement


  return material;

}


initTinyUSDZ().then(function(TinyUSDZLoader) {

  const usd = new TinyUSDZLoader.TinyUSDZLoader(usd_binary);
  console.log(usd.numMeshes());

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera( 75, window.innerWidth / window.innerHeight, 0.1, 1000 );

  const renderer = new THREE.WebGLRenderer();
  renderer.setSize( window.innerWidth, window.innerHeight );
  renderer.setAnimationLoop( animate );
  document.body.appendChild( renderer.domElement );

  // First mesh only
  const mesh = usd.getMesh(0);
  //console.log("usd", usd)
  //console.log("mesh", mesh);

  //const geometry = new THREE.BoxGeometry( 1, 1, 1 );
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute( 'position', new THREE.BufferAttribute( mesh.points, 3 ) );
  // TODO: set normal from mesh

  if (mesh.hasOwnProperty('texcoords')) {
    console.log(mesh.texcoords);
    geometry.setAttribute( 'uv', new THREE.BufferAttribute( mesh.texcoords, 2 ) );
  }

  const usdMaterial = usd.getMaterial(mesh.materialId);
  //console.log("usdMat", usdMaterial);
  //if (usdMaterial.aaa) {
  //  console.log("aaa");
  //}

  var material;

  if (usdMaterial.hasOwnProperty('diffuseColorTextureId')) {
    const diffTex = usd.getTexture(usdMaterial.diffuseColorTextureId);

    const img = usd.getImage(diffTex.textureImageId);
    console.log(img);

    // assume RGBA for now.
    let image8Array = new Uint8ClampedArray(img.data);
    let imgData = new ImageData(image8Array, img.width, img.height);

    const texture = new THREE.DataTexture( imgData, img.width, img.height );
    texture.flipY = true;
    texture.needsUpdate = true;
    
    material = new THREE.MeshBasicMaterial({
      map: texture
    });
  } else {
    material = new THREE.MeshNormalMaterial();
  }
   

  // Assume triangulated indices.
  geometry.setIndex( new THREE.Uint32BufferAttribute(mesh.faceVertexIndices, 1) );

  geometry.computeVertexNormals();

  //const material = new THREE.MeshBasicMaterial( { color: 0x00ff00 } );
  const cube = new THREE.Mesh( geometry, material );
  scene.add( cube );

  //camera.position.z = 25;
  camera.position.z = 1.0;

  function animate() {

    //cube.rotation.x += 0.01;
    cube.rotation.y += 0.02;

    renderer.render( scene, camera );

  }
});
