import * as THREE from 'three';

import initTinyUSDZ from 'https://lighttransport.github.io/tinyusdz/tinyusdz.js';

const USDZ_FILEPATH = './UsdCookie.usdz';

const usd_res = await fetch(USDZ_FILEPATH);
const usd_data = await usd_res.arrayBuffer();

const usd_binary = new Uint8Array(usd_data);

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
  console.log(mesh);

  //const geometry = new THREE.BoxGeometry( 1, 1, 1 );
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute( 'position', new THREE.BufferAttribute( mesh.points, 3 ) );
  // TODO: set normal from mesh

  if (mesh.hasOwnProperty('texcoords')) {
    console.log(mesh.texcoords);
    geometry.setAttribute( 'uv', new THREE.BufferAttribute( mesh.texcoords, 2 ) );
  }

  const usdMaterial = usd.getMaterial(mesh.materialId);
  console.log("usdMat", usdMaterial);
  if (usdMaterial.aaa) {
    console.log("aaa");
  }

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

    cube.rotation.x += 0.01;
    cube.rotation.y += 0.01;

    renderer.render( scene, camera );

  }
});
