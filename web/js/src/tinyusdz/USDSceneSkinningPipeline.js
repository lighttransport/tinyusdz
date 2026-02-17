import * as THREE from 'three';
import { findNodeByUSDPath, replaceWithSkinnedMesh } from './SkinnedMeshUtils.js';
import {
  addExtendedSkinningAttributes,
  applyExtendedSkinningIfNeeded
} from './ExtendedSkinning.js';
import { collectMeshesInHierarchy } from './SceneGraphUtils.js';

function registerMeshVisibility(mesh, showMesh, allSceneMeshes, meshVisibility) {
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  mesh.visible = showMesh;
  allSceneMeshes.push(mesh);
  meshVisibility.set(mesh, true);
}

/**
 * Build skeletons, bind skinned meshes, and collect render meshes for a USD scene node.
 */
export function applyUSDSceneSkinningPipeline(options = {}) {
  const logger = options.logger || console;
  const threeNode = options.threeNode;
  const characterGroup = options.characterGroup;
  const helperScene = options.helperScene;
  const skeletonDataArray = options.skeletonDataArray || [];
  const allSkinnedMeshUSDData = options.allSkinnedMeshUSDData || new Map();
  const skinnedMeshDataByName = options.skinnedMeshDataByName || new Map();
  const usdScene = options.usdScene;
  const showMesh = options.showMesh !== false;
  const showSkeleton = !!options.showSkeleton;
  const useWASMBoneTexture = !!options.useWASMBoneTexture;

  if (!threeNode || !characterGroup) {
    throw new Error('applyUSDSceneSkinningPipeline requires threeNode and characterGroup');
  }

  characterGroup.add(threeNode);

  const allMeshes = collectMeshesInHierarchy(threeNode);
  logger.log(
    `Found ${allMeshes.length} meshes in hierarchy, ${allSkinnedMeshUSDData.size} have USD skinning data`
  );

  const skeletons = new Map();
  const skeletonHelpers = [];
  const allSceneMeshes = [];
  const meshVisibility = new Map();

  let firstSkeleton = null;
  let firstSkeletonHelper = null;
  let firstSkinnedMesh = null;
  let firstRenderableMesh = null;
  let processedSkinnedCount = 0;

  if (allMeshes.length > 0 && skeletonDataArray.length > 0) {
    for (const skelData of skeletonDataArray) {
      const {
        skelId,
        bones: skelBones,
        rootBone: skelRootBone,
        skeletonAbsPath: skelAbsPath
      } = skelData;

      const skelThree = new THREE.Skeleton(skelBones);
      logger.log(`Created skeleton ${skelId} with ${skelBones.length} bones`);
      skeletons.set(skelId, skelThree);

      if (skelId === 0) {
        firstSkeleton = skelThree;
      }

      if (!skelRootBone || !(skelRootBone instanceof THREE.Bone)) {
        logger.error(`Invalid rootBone for skeleton ${skelId}:`, skelRootBone);
        throw new Error(`rootBone for skeleton ${skelId} is not a valid THREE.Bone`);
      }

      let boneParent = threeNode;
      if (skelAbsPath) {
        const skeletonNode = findNodeByUSDPath(threeNode, skelAbsPath);
        if (skeletonNode) {
          boneParent = skeletonNode;
          logger.log(
            `Placing rootBone ${skelId} at skeleton node: ${boneParent.name} (path: ${skelAbsPath})`
          );
        } else {
          logger.warn(
            `Could not find skeleton node "${skelAbsPath}" in hierarchy for skeleton ${skelId}, falling back to threeNode root`
          );
        }
      }
      boneParent.add(skelRootBone);
    }

    characterGroup.updateMatrixWorld(true);

    for (const mesh of allMeshes) {
      const meshName = mesh.name;
      const meshAbsPath = mesh.userData?.['primMeta.absPath'] || '';
      let meshUSDData = allSkinnedMeshUSDData.get(meshAbsPath);
      if (!meshUSDData && skinnedMeshDataByName.has(meshName)) {
        meshUSDData = skinnedMeshDataByName.get(meshName);
        if (meshUSDData === null) {
          logger.warn(
            `Mesh ${meshName}: ambiguous name fallback (duplicate names), skipping skinning without absPath match`
          );
        }
      }

      if (meshUSDData) {
        logger.log(
          `Processing mesh: ${meshName} (path: ${meshAbsPath}, skel_id: ${meshUSDData.skel_id})`
        );

        const meshSkelId =
          meshUSDData.skel_id !== undefined ? meshUSDData.skel_id : 0;
        const meshSkeleton = skeletons.get(meshSkelId);

        if (!meshSkeleton) {
          logger.warn(
            `  Skeleton ${meshSkelId} not found for mesh ${meshName}, skipping`
          );
          continue;
        }

        const geometry = mesh.geometry;
        if (!geometry || !geometry.attributes.position) {
          logger.warn(`  Mesh ${meshName}: missing position attribute, skipping`);
          continue;
        }

        const vertexCount = geometry.attributes.position.count;
        const influencesPerVertex = meshUSDData.elementSize || 4;
        const usdVertexCount = Math.floor(
          meshUSDData.jointIndices.length / influencesPerVertex
        );

        logger.log(
          `  Adding skinning: ${vertexCount} vertices, ${influencesPerVertex} influences/vertex`
        );

        if (vertexCount !== usdVertexCount) {
          logger.warn(
            `  Vertex count mismatch: Three.js=${vertexCount}, USD=${usdVertexCount}`
          );
        }

        const skinningConfig = addExtendedSkinningAttributes(
          geometry,
          meshUSDData.jointIndices,
          meshUSDData.jointWeights,
          influencesPerVertex,
          { normalize: true }
        );

        logger.log(`  Skinning mode: ${skinningConfig.mode}`);

        const newSkinnedMesh = replaceWithSkinnedMesh(mesh);
        registerMeshVisibility(newSkinnedMesh, showMesh, allSceneMeshes, meshVisibility);

        newSkinnedMesh.bind(meshSkeleton);
        logger.log(
          `  Bound to skeleton ${meshSkelId} (bindMatrix = mesh.matrixWorld)`
        );

        let wasmBoneTexture = null;
        if (useWASMBoneTexture && meshUSDData.elementSize > 8 && usdScene) {
          try {
            wasmBoneTexture = usdScene.generateBoneTexture(meshUSDData.meshId, 0);
            if (wasmBoneTexture && wasmBoneTexture.error) {
              logger.warn(
                `WASM bone texture generation failed: ${wasmBoneTexture.error}`
              );
              wasmBoneTexture = null;
            }
          } catch (texErr) {
            logger.warn(`WASM bone texture error: ${texErr.message}`);
            wasmBoneTexture = null;
          }
        }

        if (applyExtendedSkinningIfNeeded(newSkinnedMesh, { wasmBoneTexture })) {
          logger.log('  Extended skinning material applied');
        }

        if (!firstSkinnedMesh) {
          firstSkinnedMesh = newSkinnedMesh;
        }
        if (!firstRenderableMesh) {
          firstRenderableMesh = newSkinnedMesh;
        }

        processedSkinnedCount++;
      } else {
        logger.log(
          `Mesh ${meshName} (${meshAbsPath || 'no absPath'}): no USD skinning data, keeping as regular mesh`
        );
        registerMeshVisibility(mesh, showMesh, allSceneMeshes, meshVisibility);
        if (!firstRenderableMesh) {
          firstRenderableMesh = mesh;
        }
      }
    }

    logger.log(`Processed ${processedSkinnedCount} skinned meshes (hierarchy preserved)`);

    logger.log('=== Mesh-to-Skeleton Binding Summary ===');
    const skelToMeshes = new Map();
    for (const [meshPath, meshData] of allSkinnedMeshUSDData) {
      const skelId = meshData.skel_id;
      if (!skelToMeshes.has(skelId)) {
        skelToMeshes.set(skelId, []);
      }
      skelToMeshes.get(skelId).push(meshPath);
    }
    for (const [skelId, meshNames] of skelToMeshes) {
      logger.log(`Skeleton ${skelId}: meshes = [${meshNames.join(', ')}]`);
    }
    logger.log('=== End Binding Summary ===');

    if (firstSkinnedMesh && helperScene) {
      for (const skelData of skeletonDataArray) {
        const { skelId, rootBone: skelRootBone } = skelData;
        const helper = new THREE.SkeletonHelper(skelRootBone);
        helper.visible = showSkeleton;
        helper.name = `SkeletonHelper_${skelId}`;
        helperScene.add(helper);
        skeletonHelpers.push(helper);
        logger.log(`Created skeleton helper for skeleton ${skelId}`);

        if (skelId === 0) {
          firstSkeletonHelper = helper;
        }
      }
    }
  } else {
    logger.log('No skeleton data or no meshes, scene added as-is');
    for (const child of allMeshes) {
      registerMeshVisibility(child, showMesh, allSceneMeshes, meshVisibility);
      if (!firstRenderableMesh) {
        firstRenderableMesh = child;
      }
    }
  }

  return {
    allMeshes,
    allSceneMeshes,
    meshVisibility,
    skeletons,
    skeletonHelpers,
    firstSkeleton,
    firstSkeletonHelper,
    firstSkinnedMesh,
    primaryMesh: firstSkinnedMesh || firstRenderableMesh,
    processedSkinnedCount,
    hasSkeletonHelpers: skeletonHelpers.length > 0
  };
}
