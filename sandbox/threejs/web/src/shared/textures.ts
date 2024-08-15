import * as THREE from "three";
import { textureLoader } from "../loadingManager";
import woodImage from "../../static/textures/wood/wood_floor_worn_diff_1k.jpg";

import cobbleDiff from "../../static/textures/cobble/grassy_cobblestone_diff_1k.jpg";
import cobbleDisp from "../../static/textures/cobble/grassy_cobblestone_disp_1k.png";
import cobbleMask from "../../static/textures/cobble/grassy_cobblestone_mask_1k.png";
import earth from "../../static/textures/earth/earth.jpg";

const woodTexture = textureLoader.load(woodImage);
woodTexture.colorSpace = THREE.SRGBColorSpace;

const cobbleDiffTexture = textureLoader.load(cobbleDiff);
cobbleDiffTexture.colorSpace = THREE.SRGBColorSpace;

const cobbleDispTexture = textureLoader.load(cobbleDisp);

const cobbleMaskTexture = textureLoader.load(cobbleMask);

const earthTexture = textureLoader.load(earth);
earthTexture.colorSpace = THREE.SRGBColorSpace;

export {
  woodTexture,
  cobbleDiffTexture,
  cobbleDispTexture,
  cobbleMaskTexture,
  earthTexture,
};
