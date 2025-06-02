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
    static convertUsdMaterialToMeshPhysicalMaterial(usdMaterial, usdScene) {
        const material = new THREE.MeshPhysicalMaterial();

        // Helper function to create texture from USD texture ID
        function createTextureFromUSD(textureId) {
            if (textureId === undefined) return null;

            const tex = usdScene.getTexture(textureId);
            const img = usdScene.getImage(tex.textureImageId);

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
            console.log("has diffuse tex");

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
            material.specularColor = new THREE.Color(0.0, 0.0, 0.0);
            if (usdMaterial.hasOwnProperty('specularColor')) {
                const color = usdMaterial.specularColor;
                material.specularColor = new THREE.Color(color[0], color[1], color[2]);
            }
            if (usdMaterial.hasOwnProperty('specularColorTextureId')) {
                material.specularColorMap = createTextureFromUSD(usdMaterial.specularColorTextureId);
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

    static convertUsdMeshToThreeMesh(mesh) {
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.BufferAttribute(mesh.points, 3));

      // Assume mesh is triangulated.
      // itemsize = 1 since Index expects IntArray for VertexIndices in Three.js?
      geometry.setIndex(new THREE.BufferAttribute(mesh.faceVertexIndices, 1));

      if (mesh.hasOwnProperty('texcoords')) {
        geometry.setAttribute('uv', new THREE.BufferAttribute(mesh.texcoords, 2));
      }

      // TODO: uv1

      // faceVarying normals
      if (mesh.hasOwnProperty('normals')) {
        geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
      } else {
        geometry.computeVertexNormals();
      }

      if (mesh.hasOwnProperty('vertexColors')) {
        geometry.setAttribute('color', new THREE.BufferAttribute(mesh.vertexColors, 3));

      }

      // Only compute tangents if we have both UV coordinates and normals
      if (mesh.hasOwnProperty('tangents')) {
        geometry.setAttribute('tangent', new THREE.BufferAttribute(mesh.tangents, 3));
      } else if (mesh.hasOwnProperty('texcoords') && (mesh.hasOwnProperty('normals') || geometry.attributes.normal)) {
        // TODO: try MikTSpace tangent algorithm: https://threejs.org/docs/#examples/en/utils/BufferGeometryUtils.computeMikkTSpaceTangents 
        geometry.computeTangents();
      }

      // TODO: vertex opacities(per-vertex alpha)

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

            const pbrMaterial = this.convertUsdMaterialToMeshPhysicalMaterial(usdMaterial, usdScene);
            //console.log("pbrMaterial:", pbrMaterial);


            // Setting envmap is required for PBR materials to work correctly(e.g. clearcoat)
            pbrMaterial.envMap = options.envMap || null;
            pbrMaterial.envMapIntensity = options.envMapIntensity || 1.0;

            console.log("envmap:", options.envMap);

            mtl = pbrMaterial || defaultMtl || normalMtl;
        }

        const threeMesh = new THREE.Mesh(geometry, mtl );

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


    // traverse children
    for (const child of usdNode.children) {
      const childNode = this.buildThreeNode(child, defaultMtl, usdScene, options);
      node.add(childNode);
    }

    return node;
  }

}

export { TinyUSDZLoaderUtils };
