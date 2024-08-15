import * as THREE from "three";
import { cobbleDiffTexture, earthTexture, woodTexture } from "./textures";
const cubeMaterial = new THREE.MeshStandardMaterial({
  map: woodTexture,
});

cubeMaterial.side = THREE.DoubleSide;

const floorMaterial = new THREE.MeshStandardMaterial({
  map: cobbleDiffTexture,
  displacementMap: cobbleDiffTexture,
  displacementScale: 1.2,
});

floorMaterial.side = THREE.DoubleSide;

if (floorMaterial.map) {
  floorMaterial.map.repeat.set(8, 8);
  floorMaterial.map.wrapS = THREE.RepeatWrapping;
  floorMaterial.map.wrapT = THREE.RepeatWrapping;
}

const normalMaterial = new THREE.MeshNormalMaterial();

const earthMaterial = new THREE.MeshStandardMaterial({
  map: earthTexture,
});

export { cubeMaterial, floorMaterial, normalMaterial, earthMaterial };
