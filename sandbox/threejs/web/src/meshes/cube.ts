import * as THREE from "three";
import { cubeFolder } from "../lilgui";
import gsap from "gsap";
import { cubeMaterial } from "../shared/materials";

const cubeProperties = {
  subdivisions: 20,
  animation: () => {},
};

cubeFolder.add(cubeMaterial, "wireframe");

const geometry = new THREE.BoxGeometry(
  1,
  1,
  1,
  cubeProperties.subdivisions,
  cubeProperties.subdivisions,
  cubeProperties.subdivisions
);

const cube = new THREE.Mesh(geometry, cubeMaterial);
cubeFolder.add(cube.position, "y").min(-3).max(3).step(0.1).name("elevation");

cube.castShadow = true;

cubeFolder
  .add(cubeProperties, "subdivisions")
  .min(1)
  .max(20)
  .step(1)
  .onFinishChange(() => {
    cube.geometry.dispose();
    cube.geometry = new THREE.BoxGeometry(
      1,
      1,
      1,
      cubeProperties.subdivisions,
      cubeProperties.subdivisions,
      cubeProperties.subdivisions
    );
  });

cubeProperties.animation = () => {
  gsap.to(cube.rotation, { x: cube.rotation.x + Math.PI });
};
cubeFolder.add(cubeProperties, "animation").name("roll");

cubeFolder.add(cube, "visible");

export default cube;
