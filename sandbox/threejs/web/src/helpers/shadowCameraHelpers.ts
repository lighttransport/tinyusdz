import * as THREE from "three";

import { directionalLight } from "../lights";

const directionalLightShadowCameraHelper = new THREE.CameraHelper(
  directionalLight.shadow.camera
);

export { directionalLightShadowCameraHelper };
