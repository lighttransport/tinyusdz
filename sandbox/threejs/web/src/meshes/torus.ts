import * as THREE from "three";
import { normalMaterial } from "../shared/materials";

const geometry = new THREE.TorusGeometry(0.3, 0.1, 20);

const torus = new THREE.Mesh(geometry, normalMaterial);
torus.position.x = 2;
torus.castShadow = true;

export default torus;
