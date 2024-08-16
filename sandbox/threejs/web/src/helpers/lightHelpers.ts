import * as THREE from "three";
import { directionalLight, hemisphereLight } from "../lights";

const hemisphereLightHelper = new THREE.HemisphereLightHelper(
  hemisphereLight,
  0.1
);

const directionalLightHelper = new THREE.DirectionalLightHelper(
  directionalLight,
  0.3
);

export { hemisphereLightHelper, directionalLightHelper };
