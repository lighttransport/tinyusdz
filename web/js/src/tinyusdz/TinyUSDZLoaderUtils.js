import * as THREE from 'three';

import { LoaderUtils } from "three"
import { convertOpenPBRToMeshPhysicalMaterial } from './TinyUSDZMaterialX.js';

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
                // FIXME. disable opacity texture for a while.
                // transparent = true will create completely transparent material for some reason.
                //material.transparent = true; 
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

    //
    // Material Type Detection
    //
    // Returns an object describing what material types are available:
    // {
    //   hasOpenPBR: boolean,           // Has OpenPBR (MaterialX) data
    //   hasUsdPreviewSurface: boolean, // Has UsdPreviewSurface data
    //   hasBoth: boolean,              // Has both material types
    //   hasNone: boolean,              // Has no material data
    //   recommended: string            // Recommended type: 'openpbr', 'usdpreviewsurface', or 'none'
    // }
    //
    // Usage:
    //   const materialData = usdScene.getMaterial(materialId, 'json');
    //   const typeInfo = TinyUSDZLoaderUtils.getMaterialType(materialData);
    //   console.log(`Material has OpenPBR: ${typeInfo.hasOpenPBR}, UsdPreviewSurface: ${typeInfo.hasUsdPreviewSurface}`);
    //
    static getMaterialType(materialData) {
        // Parse JSON if needed
        let parsedMaterial = materialData;
        if (typeof materialData === 'string') {
            try {
                parsedMaterial = JSON.parse(materialData);
            } catch (e) {
                console.error('Failed to parse material JSON:', e);
                return {
                    hasOpenPBR: false,
                    hasUsdPreviewSurface: false,
                    hasBoth: false,
                    hasNone: true,
                    recommended: 'none'
                };
            }
        }

        if (!parsedMaterial) {
            return {
                hasOpenPBR: false,
                hasUsdPreviewSurface: false,
                hasBoth: false,
                hasNone: true,
                recommended: 'none'
            };
        }

        const hasOpenPBR = !!parsedMaterial.hasOpenPBR;
        const hasUsdPreviewSurface = !!parsedMaterial.hasUsdPreviewSurface;
        const hasBoth = hasOpenPBR && hasUsdPreviewSurface;
        const hasNone = !hasOpenPBR && !hasUsdPreviewSurface;

        // Determine recommended type (prefer OpenPBR when both are available)
        let recommended = 'none';
        if (hasOpenPBR) {
            recommended = 'openpbr';
        } else if (hasUsdPreviewSurface) {
            recommended = 'usdpreviewsurface';
        }

        return {
            hasOpenPBR,
            hasUsdPreviewSurface,
            hasBoth,
            hasNone,
            recommended
        };
    }

    //
    // Get material type as a human-readable string
    //
    // Returns: 'OpenPBR', 'UsdPreviewSurface', 'Both', or 'None'
    //
    static getMaterialTypeString(materialData) {
        const typeInfo = this.getMaterialType(materialData);

        if (typeInfo.hasBoth) return 'Both';
        if (typeInfo.hasOpenPBR) return 'OpenPBR';
        if (typeInfo.hasUsdPreviewSurface) return 'UsdPreviewSurface';
        return 'None';
    }

    //
    // Convert OpenPBR (MaterialX) to MeshPhysicalMaterial
    // Supports all OpenPBR layers: base, specular, transmission, coat, sheen, fuzz, thin_film, emission
    //
    // Usage:
    //   const materialData = usdScene.getMaterial(materialId, 'json');
    //   const material = await TinyUSDZLoaderUtils.convertOpenPBRMaterialToMeshPhysicalMaterial(materialData, usdScene, options);
    //
    static async convertOpenPBRMaterialToMeshPhysicalMaterial(materialData, usdScene, options = {}) {
        // Parse JSON material data if it's a string
        let parsedMaterial = materialData;
        if (typeof materialData === 'string') {
            try {
                parsedMaterial = JSON.parse(materialData);
            } catch (e) {
                console.error('Failed to parse material JSON:', e);
                return this.createDefaultMaterial();
            }
        }

        // Check if material has OpenPBR data
        if (!parsedMaterial || !parsedMaterial.hasOpenPBR) {
            console.warn('Material does not have OpenPBR data, falling back to UsdPreviewSurface');
            // Fall back to UsdPreviewSurface if available
            if (parsedMaterial && parsedMaterial.hasUsdPreviewSurface) {
                return this.convertUsdMaterialToMeshPhysicalMaterial(parsedMaterial, usdScene);
            }
            return this.createDefaultMaterial();
        }

        try {
            // Use the TinyUSDZMaterialX converter
            const material = await convertOpenPBRToMeshPhysicalMaterial(parsedMaterial, usdScene, {
                envMap: options.envMap || null,
                envMapIntensity: options.envMapIntensity || 1.0,
                textureCache: options.textureCache || new Map()
            });

            // Apply sideness based on USD doubleSided attribute
            if (options.doubleSided !== undefined) {
                material.side = options.doubleSided ? THREE.DoubleSide : THREE.FrontSide;
            }

            return material;

        } catch (error) {
            console.error('Failed to convert OpenPBR material:', error);
            return this.createDefaultMaterial();
        }
    }

    //
    // Smart material conversion: automatically selects OpenPBR or UsdPreviewSurface
    //
    // Options:
    //   preferredMaterialType: 'auto' | 'openpbr' | 'usdpreviewsurface'
    //     - 'auto': Prefer OpenPBR when both are available (recommended)
    //     - 'openpbr': Force OpenPBR if available, fallback to UsdPreviewSurface
    //     - 'usdpreviewsurface': Force UsdPreviewSurface if available, fallback to OpenPBR
    //
    // Usage:
    //   const materialData = usdScene.getMaterial(materialId, 'json');
    //   const material = await TinyUSDZLoaderUtils.convertMaterial(materialData, usdScene, options);
    //
    static async convertMaterial(materialData, usdScene, options = {}) {
        // Get material type info
        const typeInfo = this.getMaterialType(materialData);

        // If no material data, return default
        if (typeInfo.hasNone) {
            return this.createDefaultMaterial();
        }

        // Parse material data for conversion
        let parsedMaterial = materialData;
        if (typeof materialData === 'string') {
            try {
                parsedMaterial = JSON.parse(materialData);
            } catch (e) {
                console.error('Failed to parse material JSON:', e);
                return this.createDefaultMaterial();
            }
        }

        // Determine which material type to use based on preference
        const preferredType = options.preferredMaterialType || 'auto';
        let useOpenPBR = false;
        let useUsdPreviewSurface = false;

        switch (preferredType) {
            case 'auto':
                // Auto mode: prefer OpenPBR when available (including when both are present)
                if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                } else if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                }
                break;

            case 'openpbr':
                // Force OpenPBR if available, fallback to UsdPreviewSurface
                if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                } else if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                    console.warn('OpenPBR requested but not available, falling back to UsdPreviewSurface');
                }
                break;

            case 'usdpreviewsurface':
                // Force UsdPreviewSurface if available, fallback to OpenPBR
                if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                } else if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                    console.warn('UsdPreviewSurface requested but not available, falling back to OpenPBR');
                }
                break;

            default:
                // Unknown preference, use auto behavior
                if (typeInfo.hasOpenPBR) {
                    useOpenPBR = true;
                } else if (typeInfo.hasUsdPreviewSurface) {
                    useUsdPreviewSurface = true;
                }
        }

        // Log material type selection for debugging
        if (typeInfo.hasBoth) {
            console.log(`Material has both OpenPBR and UsdPreviewSurface. Using: ${useOpenPBR ? 'OpenPBR' : 'UsdPreviewSurface'} (preferred: ${preferredType})`);
        }

        // Convert using selected material type
        if (useOpenPBR) {
            return this.convertOpenPBRMaterialToMeshPhysicalMaterial(parsedMaterial, usdScene, options);
        } else if (useUsdPreviewSurface) {
            return this.convertUsdMaterialToMeshPhysicalMaterial(parsedMaterial, usdScene);
        }

        return this.createDefaultMaterial();
    }

    static convertUsdMeshToThreeMesh(mesh) {
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(mesh.points, 3));

        if (Object.prototype.hasOwnProperty.call(mesh, 'faceVertexIndices')) {
          if (mesh.faceVertexIndices.length >0 ) {
            //console.log("setIndex", mesh.faceVertexIndices.length);
            // Assume mesh is triangulated.
            // itemsize = 1 since Index expects IntArray for VertexIndices in Three.js?
            geometry.setIndex(new THREE.BufferAttribute(mesh.faceVertexIndices, 1));
          } else {
            //console.log("noindex");
          }
        }

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
          console.log(`USD Mesh doubleSided attribute: ${mesh.doubleSided}`);
        } else {
          console.log('USD Mesh has no doubleSided attribute (will default to FrontSide)');
        }

        // Store submesh data for multi-material support (pre-computed in C++)
        if (Object.prototype.hasOwnProperty.call(mesh, 'submeshes') && mesh.submeshes.length > 0) {
          geometry.userData['submeshes'] = mesh.submeshes;
          console.log(`USD Mesh has ${mesh.submeshes.length} pre-computed submesh group(s)`);
        }

        return geometry;
    }

    static async setupMesh(mesh /* TinyUSDZLoaderNative::RenderMesh */, defaultMtl, usdScene, options) {

        const geometry = this.convertUsdMeshToThreeMesh(mesh);

        const normalMtl = new THREE.MeshNormalMaterial();

        let mtl = null;

        //console.log("overrideMaterial:", options.overrideMaterial);
        if (options.overrideMaterial) {
            mtl = defaultMtl || normalMtl
        } else {

            // Validate materialId before attempting to get material
            // materialId can be undefined, -1 (no material), or out of range
            const hasMaterial = mesh.materialId !== undefined && mesh.materialId >= 0;

            // Get material data in JSON format to access OpenPBR/MaterialX data
            // Using getMaterialWithFormat ensures we get the full material structure including OpenPBR
            let usdMaterialData = null;
            if (hasMaterial) {
                if (typeof usdScene.getMaterialWithFormat === 'function') {
                    const result = usdScene.getMaterialWithFormat(mesh.materialId, 'json');
                    if (!result.error) {
                        usdMaterialData = JSON.parse(result.data);
                    } else {
                        console.warn(`Failed to get material ${mesh.materialId} with format: ${result.error}`);
                    }
                } else {
                    // Fallback to getMaterial if getMaterialWithFormat is not available
                    usdMaterialData = usdScene.getMaterial(mesh.materialId);
                }
            }

            console.log(`Mesh materialId: ${mesh.materialId}, hasMaterial: ${hasMaterial}, usdMaterial: ${usdMaterialData ? 'valid' : 'null'}`);

            let pbrMaterial;
            if (usdMaterialData) {
                // Use smart convertMaterial to handle both OpenPBR and UsdPreviewSurface
                pbrMaterial = await this.convertMaterial(usdMaterialData, usdScene, {
                    preferredMaterialType: options.preferredMaterialType || 'auto',
                    envMap: options.envMap || null,
                    envMapIntensity: options.envMapIntensity || 1.0,
                    textureCache: options.textureCache || new Map(),
                    doubleSided: geometry.userData['doubleSided']
                });
            } else {
                // No valid material - create default material
                pbrMaterial = defaultMtl || new THREE.MeshPhysicalMaterial({
                    color: 0x888888,
                    roughness: 0.5,
                    metalness: 0.0
                });
            }

            // Setting envmap is required for PBR materials to work correctly(e.g. clearcoat)
            pbrMaterial.envMap = options.envMap || null;
            pbrMaterial.envMapIntensity = options.envMapIntensity || 1.0;

            //console.log("envmap:", options.envMap);

            // Sideness is determined by the mesh's USD doubleSided attribute
            if (Object.prototype.hasOwnProperty.call(geometry.userData, 'doubleSided')) {
              if (geometry.userData.doubleSided) {
                console.log(`  Setting material to DoubleSide (from USD doubleSided=true)`);
                pbrMaterial.side = THREE.DoubleSide;
              } else {
                console.log(`  Setting material to FrontSide (from USD doubleSided=false)`);
                pbrMaterial.side = THREE.FrontSide;
              }
            } else {
              // No doubleSided attribute in USD - default to FrontSide
              console.log(`  Setting material to FrontSide (no USD doubleSided attribute)`);
              pbrMaterial.side = THREE.FrontSide;
            }

            mtl = pbrMaterial || defaultMtl || normalMtl;
        }

        // Handle GeomSubsets (per-face materials)
        if (geometry.userData['submeshes'] && geometry.userData['submeshes'].length > 0) {
            const submeshes = geometry.userData['submeshes'];
            console.log(`Setting up multi-material mesh with ${submeshes.length} pre-computed submesh groups`);

            // Build materials array indexed by materialId
            const materials = [];
            const materialIdToIndex = new Map();

            // First pass: collect unique material IDs
            for (const submesh of submeshes) {
                const matId = submesh.materialId;
                if (!materialIdToIndex.has(matId)) {
                    materialIdToIndex.set(matId, materials.length);
                    materials.push(null); // Placeholder
                }
            }

            // Second pass: load materials
            for (const [matId, matIndex] of materialIdToIndex.entries()) {
                if (matId >= 0) {
                    const materialData = usdScene.getMaterialWithFormat ?
                        JSON.parse(usdScene.getMaterialWithFormat(matId, 'json').data) :
                        usdScene.getMaterial(matId);

                    const material = await this.convertMaterial(materialData, usdScene, {
                        preferredMaterialType: options.preferredMaterialType || 'auto',
                        envMap: options.envMap || null,
                        envMapIntensity: options.envMapIntensity || 1.0,
                        textureCache: options.textureCache || new Map(),
                        doubleSided: geometry.userData['doubleSided']
                    });

                    material.envMap = options.envMap || null;
                    material.envMapIntensity = options.envMapIntensity || 1.0;
                    material.side = geometry.userData['doubleSided'] ? THREE.DoubleSide : THREE.FrontSide;

                    materials[matIndex] = material;
                    console.log(`  Loaded material ${matId} -> index ${matIndex}`);
                } else {
                    materials[matIndex] = mtl; // Use default material
                }
            }

            // Third pass: add geometry groups using pre-computed submesh data (from C++)
            for (const submesh of submeshes) {
                const matIndex = materialIdToIndex.get(submesh.materialId);
                geometry.addGroup(submesh.start, submesh.count, matIndex);
            }

            console.log(`  Created ${submeshes.length} geometry groups for ${materials.length} unique materials (pre-computed in WASM)`);

            // Create mesh with multi-material array
            const threeMesh = new THREE.Mesh(geometry, materials);
            return threeMesh;
        } else {
            // Single material mesh
            const threeMesh = new THREE.Mesh(geometry, mtl);
            return threeMesh;
        }
    }


    // arr = float array with 16 elements(row major order)
    static toMatrix4(a) {
        const m = new THREE.Matrix4();

        //m.set(a[0], a[1], a[2], a[3],
        //    a[4], a[5], a[6], a[7],
        //    a[8], a[9], a[10], a[11],
        //    a[12], a[13], a[14], a[15]);
        m.set(a[0], a[4], a[8], a[12],
            a[1], a[5], a[9], a[13],
            a[2], a[6], a[10], a[14],
            a[3], a[7], a[11], a[15]);

        return m;
    }

    // Supported options
    // 'overrideMaterial' : Override usd material with defaultMtl.

    static async buildThreeNode(usdNode /* TinyUSDZLoader.Node */, defaultMtl = null, usdScene /* TinyUSDZLoader.Scene */ = null, options = {})
   /* => THREE.Object3D */ {

        var node = new THREE.Group();

        console.log("usdNode.nodeType:", usdNode.nodeType, "primName:", usdNode.primName, "absPath:", usdNode.absPath);
        if (usdNode.nodeType == 'xform') {

            // intermediate xform node
            // Apply the USD local transform matrix to the Three.js node
            const matrix = this.toMatrix4(usdNode.localMatrix);
            console.log("  Applied localMatrix:", {
                matrix: matrix});

            // Decompose the matrix into position, rotation, and scale
            // This is necessary for Three.js to properly handle the transform
            node.applyMatrix4(matrix);

            // Log transform for debugging
            console.log("  Applied xform matrix:", {
                position: [node.position.x, node.position.y, node.position.z],
                rotation: [node.rotation.x, node.rotation.y, node.rotation.z],
                scale: [node.scale.x, node.scale.y, node.scale.z]
            });

        } else if (usdNode.nodeType == 'mesh') {

            // contentId is the mesh ID in the USD scene.
            const mesh = usdScene.getMesh(usdNode.contentId);

            const threeMesh = await this.setupMesh(mesh, defaultMtl, usdScene, options);
            node = threeMesh;

            // Apply transform to mesh nodes as well
            // Mesh nodes can also have transforms in USD
            if (usdNode.localMatrix) {
                const matrix = this.toMatrix4(usdNode.localMatrix);
                node.applyMatrix4(matrix);

                console.log("  Applied mesh matrix:", {
                    position: [node.position.x, node.position.y, node.position.z],
                    rotation: [node.rotation.x, node.rotation.y, node.rotation.z],
                    scale: [node.scale.x, node.scale.y, node.scale.z]
                });
            }

        } else {
            // Unknown node type - still try to apply transform if available
            if (usdNode.localMatrix) {
                const matrix = this.toMatrix4(usdNode.localMatrix);
                node.applyMatrix4(matrix);
            }
        }

        node.name = usdNode.primName;
        node.userData['primMeta.displayName'] = usdNode.displayName;
        node.userData['primMeta.absPath'] = usdNode.absPath;

        if (Object.prototype.hasOwnProperty.call(usdNode, 'children')) {

            // traverse children
            for (const child of usdNode.children) {
                const childNode = await this.buildThreeNode(child, defaultMtl, usdScene, options);
                node.add(childNode);
            }
        }

        return node;
    }

}

export { TinyUSDZLoaderUtils };
