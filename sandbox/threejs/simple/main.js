import * as THREE from 'three';

import initTinyUSDZ from './tinyusdz.js';

const USDZ_FILEPATH = './suzanne.usdc';
//const USDZ_FILEPATH = './cube.usdz';
//const USDZ_FILEPATH = './texture-cat-plane.usda';
//const USDZ_FILEPATH = './LemonMeringuePie.usdz';
//const USDZ_FILEPATH = './robot.usda';
//const USDZ_FILEPATH = './teapot.usdz';
//const USDZ_FILEPATH = './DamagedHelmet.usdz';

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

  // Assume triangulated indices.
  geometry.setIndex( new THREE.Uint32BufferAttribute(mesh.faceVertexIndices, 1) );

  geometry.computeVertexNormals();

  //const material = new THREE.MeshBasicMaterial( { color: 0x00ff00 } );
  const material = new THREE.MeshNormalMaterial();
  const cube = new THREE.Mesh( geometry, material );
  scene.add( cube );

  camera.position.z = 5;

  function animate() {

    cube.rotation.x += 0.01;
    cube.rotation.y += 0.01;

    renderer.render( scene, camera );

  }
});
