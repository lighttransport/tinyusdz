import GUI from "lil-gui";

const gui = new GUI({
  title: "ThreeJS Starter Bun",
});

const cubeFolder = gui.addFolder("Cube");
const cameraFolder = gui.addFolder("Camera");
const lightsFolder = gui.addFolder("Lights");

const directionalLightFolder = lightsFolder.addFolder("Directional Light");
const ambientLightFolder = lightsFolder.addFolder("Ambient Light");
const hemisphereLightFolder = lightsFolder.addFolder("Hemisphere Light");

export {
  cubeFolder,
  cameraFolder,
  lightsFolder,
  directionalLightFolder,
  hemisphereLightFolder,
  ambientLightFolder,
};
