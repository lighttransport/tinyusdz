import * as THREE from 'three';

import { LoaderUtils } from "three"

class TinyUSDZLoaderUtils extends LoaderUtils {

    constructor() {
        super();
    }

    static getTextureFromUSD(usd, textureId) {
        if (textureId === undefined) return null;

        const tex = usd.getTexture(textureId);
        const img = usd.getImage(tex.textureImageId);

        const image8Array = new Uint8ClampedArray(img.data);
        const texture = new THREE.DataTexture(image8Array, img.width, img.height);
        texture.format = THREE.RGBAFormat;
        texture.flipY = true;
        texture.needsUpdate = true;

        return texture;
    }

    static createDefaultMaterial() {
        // TODO: Use MeshPhysicalMaterial.
        return new THREE.MeshStandardMaterial({
            color: new THREE.Color(0.18, 0.18, 0.18),
            emissive: 0x000000,
            metalness: 0.0,
            roughness: 0.5,
            transparent: false,
            depthTest: true,
            side: THREE.FrontSide
        });
    }
}

export { TinyUSDZLoaderUtils };