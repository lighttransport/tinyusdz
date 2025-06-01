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
        return new THREE.MeshPhysicalMaterial({
            color: new THREE.Color(0.18, 0.18, 0.18),
            emissive: 0x000000,
            metalness: 0.0,
            roughness: 0.5,
            transparent: false,
            depthTest: true,
            side: THREE.FrontSide
        });
    }

    //
    // Convert UsdPreviewSureface to MeshPhysicalMaterial
    // - [x] diffuseColor -> color
    // - [x] ior -> ior
    // - [x] clearcoat -> clearcoat
    // - [x] clearcoatRoughness -> clearcoatRoughness
    // - [x] specularColor -> specular
    // - [x] roughness -> roughness 
    // - [x] metallic -> metalness
    // - [x] emissiveColor -> emissive
    // - [x] opacity -> opacity (TODO: map to .transmission?)
    // - [x] occlusion -> aoMap
    // - [x] normal -> normalMap
    // - [x] displacement -> displacementMap
    static convertUsdMaterialToMeshPhysicalMaterial(usdMaterial, usd) {
        const material = new THREE.MeshPhysicalMaterial();

        // Helper function to create texture from USD texture ID
        function createTextureFromUSD(textureId) {
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

        // Diffuse color and texture
        material.color = new THREE.Color(0.18, 0.18, 0.18);
        if (usdMaterial.hasOwnProperty('diffuseColor')) {
            const color = usdMaterial.diffuseColor;
            material.color = new THREE.Color(color[0], color[1], color[2]);
        }

        if (usdMaterial.hasOwnProperty('diffuseColorTextureId')) {
            material.map = createTextureFromUSD(usdMaterial.diffuseColorTextureId);
        }

        // IOR
        material.ior = 1.5;
        if (usdMaterial.hasOwnProperty('ior')) {
            material.ior = usdMaterial.ior;
        }

        // Clearcoat
        material.clearcoat = 0.0;
        if (usdMaterial.hasOwnProperty('clearcoat')) {
            material.clearcoat = usdMaterial.clearcoat;
        }

        material.clearcoatRoughness = 0.0;
        if (usdMaterial.hasOwnProperty('clearcoatRoughness')) {
            material.clearcoatRoughness = usdMaterial.clearcoatRoughness;
        }

        // Workflow selection
        material.useSpecularWorkflow = false;
        if (usdMaterial.hasOwnProperty('useSpecularWorkflow')) {
            material.useSpecularWorkflow = usdMaterial.useSpecularWorkflow;
        }

        if (material.useSpecularWorkflow) {
            material.specular = new THREE.Color(0.0, 0.0, 0.0);
            if (usdMaterial.hasOwnProperty('specularColor')) {
                const color = usdMaterial.specularColor;
                material.specular = new THREE.Color(color[0], color[1], color[2]);
            }
            if (usdMaterial.hasOwnProperty('specularColorTextureId')) {
                material.specularMap = createTextureFromUSD(usdMaterial.specularColorTextureId);
            }
        } else {
            material.metalness = 0.0;
            if (usdMaterial.hasOwnProperty('metallic')) {
                material.metalness = usdMaterial.metallic;
            }
            if (usdMaterial.hasOwnProperty('metallicTextureId')) {
                material.metalnessMap = createTextureFromUSD(usdMaterial.metallicTextureId);
            }
        }

        // Roughness
        material.roughness = 0.5;
        if (usdMaterial.hasOwnProperty('roughness')) {
            material.roughness = usdMaterial.roughness;
        }
        if (usdMaterial.hasOwnProperty('roughnessTextureId')) {
            material.roughnessMap = createTextureFromUSD(usdMaterial.roughnessTextureId);
        }

        // Emissive
        if (usdMaterial.hasOwnProperty('emissiveColor')) {
            const color = usdMaterial.emissiveColor;
            material.emissive = new THREE.Color(color[0], color[1], color[2]);
        }
        if (usdMaterial.hasOwnProperty('emissiveColorTextureId')) {
            material.emissiveMap = createTextureFromUSD(usdMaterial.emissiveColorTextureId);
        }

        // Opacity
        material.opacity = 1.0;
        if (usdMaterial.hasOwnProperty('opacity')) {
            material.opacity = usdMaterial.opacity;
            if (material.opacity < 1.0) {
                material.transparent = true;
            }
        }
        if (usdMaterial.hasOwnProperty('opacityTextureId')) {
            material.alphaMap = createTextureFromUSD(usdMaterial.opacityTextureId);
            material.transparent = true;
        }

        // Ambient Occlusion
        if (usdMaterial.hasOwnProperty('occlusionTextureId')) {
            material.aoMap = createTextureFromUSD(usdMaterial.occlusionTextureId);
        }

        // Normal Map
        if (usdMaterial.hasOwnProperty('normalTextureId')) {
            material.normalMap = createTextureFromUSD(usdMaterial.normalTextureId);
        }

        // Displacement Map
        if (usdMaterial.hasOwnProperty('displacementTextureId')) {
            material.displacementMap = createTextureFromUSD(usdMaterial.displacementTextureId);
            material.displacementScale = 1.0;
        }

        return material;
    }
}

export { TinyUSDZLoaderUtils };