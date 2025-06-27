import * as THREE from 'three';

import { LoaderUtils } from "three"

class TinyUSDZLoaderUtils extends LoaderUtils {

    constructor() {
        super();
    }

    static async getDataFromURI(uri) {
        try {
            const response = await fetch(url);
            if (!response.ok) {
                return [null, new Error(`Response status: ${response.status}`)];
            }

            const buf = await response.arrayBuiffer();
            const data = new Uint8Array(buf);

            return [data, null];

        } catch (error) {
            return [null, error];
        }
    }

    // Extract file extension from URI/path
    static getFileExtension(uri) {
        if (!uri || typeof uri !== 'string') return '';

        // Remove query parameters and hash
        const cleanUri = uri.split('?')[0].split('#')[0];

        // Get the last part after the last dot
        const lastDotIndex = cleanUri.lastIndexOf('.');
        if (lastDotIndex === -1 || lastDotIndex === cleanUri.length - 1) {
            return '';
        }

        return cleanUri.substring(lastDotIndex + 1).toLowerCase();
    }

    // Determine MIME type from file extension
    static getMimeTypeFromExtension(extension) {
        const mimeTypes = {
            // Images
            'jpg': 'image/jpeg',
            'jpeg': 'image/jpeg',
            'png': 'image/png',
            'gif': 'image/gif',
            'webp': 'image/webp',
            'bmp': 'image/bmp',
            'tiff': 'image/tiff',
            'tif': 'image/tiff',
            'svg': 'image/svg+xml',
            'ico': 'image/x-icon',

            // HDR/EXR formats
            'hdr': 'image/vnd.radiance',
            'exr': 'image/x-exr',
            'rgbe': 'image/vnd.radiance',

            // 3D/USD formats
            'usd': 'model/vnd.usdz+zip',
            'usda': 'model/vnd.usd+ascii',
            'usdc': 'model/vnd.usd+binary',
            'usdz': 'model/vnd.usdz+zip',

            // Other common formats
            'json': 'application/json',
            'xml': 'application/xml',
            'txt': 'text/plain',
            'bin': 'application/octet-stream'
        };

        return mimeTypes[extension.toLowerCase()] || null;
    }

    // Helper method to determine MIME type
    static getMimeType(texImage) {

        if (texImage.uri) {
            const mime = this.getMimeTypeFromExtension(this.getFileExtension(texImage.uri));
            if (mime != null) {
                return mime;
            }
        }

        // Try to detect from magic bytes if available
        const data = new Uint8Array(texImage.data);
        if (data.length >= 4) {
            // PNG magic bytes: 89 50 4E 47
            if (data[0] === 0x89 && data[1] === 0x50 && data[2] === 0x4E && data[3] === 0x47) {
                return 'image/png';
            }
            // JPEG magic bytes: FF D8 FF
            if (data[0] === 0xFF && data[1] === 0xD8 && data[2] === 0xFF) {
                return 'image/jpeg';
            }
            // WEBP magic bytes: 52 49 46 46 ... 57 45 42 50
            if (data[0] === 0x52 && data[1] === 0x49 && data[2] === 0x46 && data[3] === 0x46) {
                return 'image/webp';
            }
        }

        // Default fallback
        return 'image/png';
    }

    static async getTextureFromUSD(usdScene, textureId) {
        if (textureId === undefined) return Promise.reject(new Error("textureId undefined"));


        const tex = usdScene.getTexture(textureId);

        const texImage = usdScene.getImage(tex.textureImageId);
        //console.log("Loading texture from URI:", texImage);

        // there are 3 states for texture:
        // 1. URI only. Need to fetch texture(file) from URI in JS layer.
        // 2. Texture is loaded from USDZ file, but not yet decoded(Use Three.js or JS library to decode)
        // 3. Texture is decoded and ready to use in Three.js.

        if (texImage.uri && (texImage.bufferId == -1)) {
            // Case 1: URI only

            const loader = new THREE.TextureLoader();

            //console.log("Loading texture from URI:", texImage.uri);
            // TODO: Use HDR/EXR loader if a uri is HDR/EXR file.
            return loader.loadAsync(texImage.uri);

        } else if (texImage.bufferId >= 0 && texImage.data) {
            //console.log("case 2 or 3");

            if (texImage.decoded) {
                //console.log("case 3");

                const image8Array = new Uint8ClampedArray(texImage.data);
                const texture = new THREE.DataTexture(image8Array, texImage.width, texImage.height);
                if (texImage.channels == 1) {
                    texture.format = THREE.RedFormat;
                } else if (texImage.channels == 2) {
                    texture.format = THREE.RGFormat;
                } else if (texImage.channels == 3) {
                    // Recent three.js does not support RGBFormat.
                    return Promise.reject(new Error("RGB image is not supported"));
                } else if (texImage.channels == 4) {
                    texture.format = THREE.RGBAFormat;
                } else {
                    return Promise.reject(new Error("Unsupported image channels: " + texImage.channels));
                }
                texture.flipY = true;
                texture.needsUpdate = true;

                return Promise.resolve(texture);

            } else {
                //console.log("case 3");
                try {
                    const blob = new Blob([texImage.data], { type: this.getMimeType(texImage) });
                    const blobUrl = URL.createObjectURL(blob);

                    const loader = new THREE.TextureLoader();

                    //console.log("blobUrl", blobUrl);
                    // TODO: Use HDR/EXR loader if a uri is HDR/EXR file.
                    return loader.loadAsync(blobUrl);
                } catch (error) {
                    console.error("Failed to create Blob from texture data:", error);
                    return Promise.reject(new Error("Failed to create Blob from texture data"));
                }
            }

        } else {
            //console.log("case 3");
            return Promise.reject(new Error("Invalid USD texture info"));
        }
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
    static convertUsdMaterialToMeshPhysicalMaterial(usdMaterial, usdScene) {
        const material = new THREE.MeshPhysicalMaterial();
        const loader = new THREE.TextureLoader();

        // Diffuse color and texture
        material.color = new THREE.Color(0.18, 0.18, 0.18);
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'diffuseColor')) {
            const color = usdMaterial.diffuseColor;
            material.color = new THREE.Color(color[0], color[1], color[2]);
            //console.log("diffuseColor:", material.color);
        }

        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'diffuseColorTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.diffuseColorTextureId).then((texture) => {
                //console.log("gettex");
                material.map = texture;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load texture. uri not exists or Cross-Site origin header is not set in the web server?", err);
            });
        }

        // IOR
        material.ior = 1.5;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'ior')) {
            material.ior = usdMaterial.ior;
        }

        // Clearcoat
        material.clearcoat = 0.0;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'clearcoat')) {
            material.clearcoat = usdMaterial.clearcoat;
        }

        material.clearcoatRoughness = 0.0;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'clearcoatRoughness')) {
            material.clearcoatRoughness = usdMaterial.clearcoatRoughness;
        }

        // Workflow selection
        material.useSpecularWorkflow = false;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'useSpecularWorkflow')) {
            material.useSpecularWorkflow = usdMaterial.useSpecularWorkflow;
        }

        if (material.useSpecularWorkflow) {
            material.specularColor = new THREE.Color(0.0, 0.0, 0.0);
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'specularColor')) {
                const color = usdMaterial.specularColor;
                material.specularColor = new THREE.Color(color[0], color[1], color[2]);
            }
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'specularColorTextureId')) {
                this.getTextureFromUSD(usdScene, usdMaterial.specularColorTextureId).then((texture) => {
                    material.specularColorMap = texture;
                    material.needsUpdate = true;
                }).catch((err) => {
                    console.error("failed to load specular color texture", err);
                });
            }
        } else {
            material.metalness = 0.0;
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'metallic')) {
                material.metalness = usdMaterial.metallic;
            }
            if (Object.prototype.hasOwnProperty.call(usdMaterial, 'metallicTextureId')) {
                this.getTextureFromUSD(usdScene, usdMaterial.metallicTextureId).then((texture) => {
                    material.metalnessMap = texture;
                    material.needsUpdate = true;
                }).catch((err) => {
                    console.error("failed to load metallic texture", err);
                });
            }
        }

        // Roughness
        material.roughness = 0.5;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'roughness')) {
            material.roughness = usdMaterial.roughness;
        }
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'roughnessTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.roughnessTextureId).then((texture) => {
                material.roughnessMap = texture;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load roughness texture", err);
            });
        }

        // Emissive
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'emissiveColor')) {
            const color = usdMaterial.emissiveColor;
            material.emissive = new THREE.Color(color[0], color[1], color[2]);
        }
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'emissiveColorTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.emissiveColorTextureId).then((texture) => {
                material.emissiveMap = texture;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load emissive texture", err);
            });
        }

        // Opacity
        material.opacity = 1.0;
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'opacity')) {
            material.opacity = usdMaterial.opacity;
            if (material.opacity < 1.0) {
                material.transparent = true;
            }
        }
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'opacityTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.opacityTextureId).then((texture) => {
                material.alphaMap = texture;
                material.transparent = true;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load opacity texture", err);
            });
        }

        // Ambient Occlusion
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'occlusionTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.occlusionTextureId).then((texture) => {
                material.aoMap = texture;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load occlusion texture", err);
            });
        }

        // Normal Map
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'normalTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.normalTextureId).then((texture) => {
                material.normalMap = texture;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load normal texture", err);
            });
        }

        // Displacement Map
        if (Object.prototype.hasOwnProperty.call(usdMaterial, 'displacementTextureId')) {
            this.getTextureFromUSD(usdScene, usdMaterial.displacementTextureId).then((texture) => {
                material.displacementMap = texture;
                material.displacementScale = 1.0;
                material.needsUpdate = true;
            }).catch((err) => {
                console.error("failed to load displacement texture", err);
            });
        }

        return material;
    }

    static convertUsdMeshToThreeMesh(mesh) {
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(mesh.points, 3));

        // Assume mesh is triangulated.
        // itemsize = 1 since Index expects IntArray for VertexIndices in Three.js?
        geometry.setIndex(new THREE.BufferAttribute(mesh.faceVertexIndices, 1));

        if (Object.prototype.hasOwnProperty.call(mesh, 'texcoords')) {
            geometry.setAttribute('uv', new THREE.BufferAttribute(mesh.texcoords, 2));
        }

        // TODO: uv1

        // faceVarying normals
        if (Object.prototype.hasOwnProperty.call(mesh, 'normals')) {
            geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
        } else {
            geometry.computeVertexNormals();
        }

        if (Object.prototype.hasOwnProperty.call(mesh, 'vertexColors')) {
            geometry.setAttribute('color', new THREE.BufferAttribute(mesh.vertexColors, 3));

        }

        // Only compute tangents if we have both UV coordinates and normals
        if (Object.prototype.hasOwnProperty.call(mesh, 'tangents')) {
            geometry.setAttribute('tangent', new THREE.BufferAttribute(mesh.tangents, 3));
        } else if (Object.prototype.hasOwnProperty.call(mesh, 'texcoords') && (Object.prototype.hasOwnProperty.call(mesh, 'normals') || geometry.attributes.normal)) {
            // TODO: try MikTSpace tangent algorithm: https://threejs.org/docs/#examples/en/utils/BufferGeometryUtils.computeMikkTSpaceTangents 
            geometry.computeTangents();
        }

        // TODO: vertex opacities(per-vertex alpha)

        // Three.js does not have sideness attribute in Mesh.
        // Store doubleSided param to customData
        if (Object.prototype.hasOwnProperty.call(mesh, 'doubleSided')) {
          geometry.userData['doubleSided'] = mesh.doubleSided;
        }

        return geometry;
    }

    static setupMesh(mesh /* TinyUSDZLoaderNative::RenderMesh */, defaultMtl, usdScene, options) {

        const geometry = this.convertUsdMeshToThreeMesh(mesh);

        const normalMtl = new THREE.MeshNormalMaterial();

        let mtl = null;

        //console.log("overrideMaterial:", options.overrideMaterial);
        if (options.overrideMaterial) {
            mtl = defaultMtl || normalMtl
        } else {

            const usdMaterial = usdScene.getMaterial(mesh.materialId);
            //console.log("usdMaterial:", usdMaterial);
         

            const pbrMaterial = this.convertUsdMaterialToMeshPhysicalMaterial(usdMaterial, usdScene);
            //console.log("pbrMaterial:", pbrMaterial);


            // Setting envmap is required for PBR materials to work correctly(e.g. clearcoat)
            pbrMaterial.envMap = options.envMap || null;
            pbrMaterial.envMapIntensity = options.envMapIntensity || 1.0;

            //console.log("envmap:", options.envMap);

            // Sideness is determined by the mesh
            if (Object.prototype.hasOwnProperty.call(geometry.userData, 'doubleSided')) {
              if (geometry.userData.doubleSided) {
                 
                usdMaterial.side = THREE.DoubleSide;
                pbrMaterial.side = THREE.DoubleSide;
              }
            } 

            mtl = pbrMaterial || defaultMtl || normalMtl;
        }

        const threeMesh = new THREE.Mesh(geometry, mtl);

        return threeMesh;
    }


    // arr = float array with 16 elements(row major order)
    static toMatrix4(a) {
        const m = new THREE.Matrix4();

        m.set(a[0], a[1], a[2], a[3],
            a[4], a[5], a[6], a[7],
            a[8], a[9], a[10], a[11],
            a[12], a[13], a[14], a[15]);

        return m;
    }

    // Supported options
    // 'overrideMaterial' : Override usd material with defaultMtl.

    static buildThreeNode(usdNode /* TinyUSDZLoader.Node */, defaultMtl = null, usdScene /* TinyUSDZLoader.Scene */ = null, options = {})
   /* => THREE.Object3D */ {

        var node = new THREE.Group();

        //console.log("usdNode.nodeType:", usdNode.nodeType, "primName:", usdNode.primName, "absPath:", usdNode.absPath);
        if (usdNode.nodeType == 'xform') {

            // intermediate xform node
            // TODO: create THREE.Group and apply transform.
            node.matrix = this.toMatrix4(usdNode.localMatrix);

        } else if (usdNode.nodeType == 'mesh') {

            // contentId is the mesh ID in the USD scene.
            const mesh = usdScene.getMesh(usdNode.contentId);

            const threeMesh = this.setupMesh(mesh, defaultMtl, usdScene, options);
            node = threeMesh;

        } else {
            // ???

        }

        node.name = usdNode.primName;
        node.userData['primMeta.displayName'] = usdNode.displayName;
        node.userData['primMeta.absPath'] = usdNode.absPath;

        if (Object.prototype.hasOwnProperty.call(usdNode, 'children')) {

            // traverse children
            for (const child of usdNode.children) {
                const childNode = this.buildThreeNode(child, defaultMtl, usdScene, options);
                node.add(childNode);
            }
        }

        return node;
    }

}

export { TinyUSDZLoaderUtils };
