import { OrbitControls } from "three/examples/jsm/Addons.js";
import camera from "./camera";
import canvas from "./canvas";

const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;

export default controls;
