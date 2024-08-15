import * as THREE from "three";

import { floorMaterial } from "../shared/materials";

const geometry = new THREE.PlaneGeometry(20, 20, 20);

const floor = new THREE.Mesh(geometry, floorMaterial);
floor.rotation.x = Math.PI / 2;
floor.position.y = -2;

floor.receiveShadow = true;

export default floor;
