import * as THREE from "three";
import {
  directionalLightFolder,
  ambientLightFolder,
  hemisphereLightFolder,
} from "./lilgui";

const ambientLight = new THREE.AmbientLight("white", 1);

ambientLightFolder
  .add(ambientLight, "intensity")
  .min(0)
  .max(5)
  .step(0.01)
  .name("Intensity");

const hemisphereLight = new THREE.HemisphereLight("white", "green", 1);
hemisphereLightFolder
  .add(hemisphereLight, "intensity")
  .min(0)
  .max(10)
  .step(0.01)
  .name("Intensity");

const directionalLight = new THREE.DirectionalLight("white", 2);
directionalLight.position.set(2, 2, 2);
directionalLightFolder
  .add(directionalLight, "intensity")
  .min(0)
  .max(10)
  .step(0.01)
  .name("Intensity");

directionalLight.castShadow = true;
directionalLight.shadow.camera.far = 10;

const directionalLightShadowMapSizeResolution = 2048;
directionalLight.shadow.mapSize.set(
  directionalLightShadowMapSizeResolution,
  directionalLightShadowMapSizeResolution
);

directionalLightFolder
  .add(
    directionalLight.shadow.mapSize,
    "x",
    [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]
  )
  .onChange((v: number) => {
    directionalLight.shadow.mapSize.y = v;
    directionalLight.shadow.map && directionalLight.shadow.map.dispose();
    directionalLight.shadow.map = null;
  })
  .name("Shadow Map Resolution");

export { ambientLight, hemisphereLight, directionalLight };
