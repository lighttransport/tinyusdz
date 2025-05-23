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


initTinyUSDZ().then(async function(TinyUSDZLoaderNative) {

  // Setup the async loader helper before attempting to use it
  console.log("Setting up async loader...");
  TinyUSDZLoaderNative.setupAsyncLoader();
  console.log("Async loader setup complete.");

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
