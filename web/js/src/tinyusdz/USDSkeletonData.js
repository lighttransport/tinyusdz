import * as THREE from 'three';
import { createThreeSkeletonFromUSD } from './USDSkeletalHelper.js';

/**
 * Build skeleton/bone maps from a TinyUSDZ scene.
 *
 * Includes fallback flat-skeleton creation when skinned meshes exist but no
 * Skeleton prims are present.
 */
export function buildSkeletonDataFromUSD(usdScene, options = {}) {
  const logger = options.logger || console;
  const hasSkinnedMeshData = !!options.hasSkinnedMeshData;
  const onSkeletonInfo = options.onSkeletonInfo || null;

  const numSkeletons = usdScene.numSkeletons ? usdScene.numSkeletons() : 0;
  logger.log(`Found ${numSkeletons} skeletons in USD file`);

  const skeletonDataArray = [];
  const allBoneMaps = new Map();
  let firstBones = [];
  let firstBoneMap = new Map();
  let firstRootBone = null;
  let firstSkeletonAbsPath = null;
  let totalJointCount = 0;
  let fallbackSkeletonCreated = false;

  if (numSkeletons > 0) {
    for (let skelId = 0; skelId < numSkeletons; skelId++) {
      const usdSkeleton = usdScene.getSkeleton(skelId);
      const skelAbsPath = usdSkeleton.abs_path || null;
      logger.log(`USD Skeleton ${skelId}: ${skelAbsPath || '(no path)'}`, usdSkeleton);

      const skeletonData = createThreeSkeletonFromUSD(usdSkeleton, { skelId });
      const skelBones = skeletonData.bones;
      const skelBoneMap = skeletonData.boneMap;
      const skelRootBone = skeletonData.rootBone;
      logger.log(`Built skeleton ${skelId} with ${skelBones.length} bones`);

      skeletonDataArray.push({
        skelId,
        bones: skelBones,
        rootBone: skelRootBone,
        skeletonAbsPath: skelAbsPath,
        boneMap: skelBoneMap
      });

      allBoneMaps.set(skelId, skelBoneMap);
      totalJointCount += skelBones.length;

      if (skelId === 0) {
        firstBones = skelBones;
        firstBoneMap = skelBoneMap;
        firstRootBone = skelRootBone;
        firstSkeletonAbsPath = skelAbsPath;
      }
    }

    if (onSkeletonInfo) {
      onSkeletonInfo(numSkeletons, totalJointCount);
    }
  } else {
    logger.warn('No skeletons found in USD file');

    if (hasSkinnedMeshData) {
      logger.log(
        'Mesh has skinning data but no skeleton hierarchy - attempting fallback skeleton creation'
      );

      const numAnimations = usdScene.numAnimations ? usdScene.numAnimations() : 0;
      if (numAnimations > 0) {
        const anim = usdScene.getAnimation(0);
        if (anim && anim.channels) {
          let maxJointId = -1;
          for (const channel of anim.channels) {
            if (
              channel.target_type === 'SkeletonJoint' &&
              channel.joint_id !== undefined
            ) {
              maxJointId = Math.max(maxJointId, channel.joint_id);
            }
          }

          if (maxJointId >= 0) {
            logger.log(
              `Building fallback skeleton from animation: ${maxJointId + 1} joints (max id: ${maxJointId})`
            );

            const rootBoneContainer = new THREE.Bone();
            rootBoneContainer.name = 'skeleton_root';
            const fallbackBones = [];
            const fallbackBoneMap = new Map();

            for (let i = 0; i <= maxJointId; i++) {
              const bone = new THREE.Bone();
              bone.name = `joint_${i}`;
              bone.userData.joint_id = i;
              fallbackBoneMap.set(i, bone);
              fallbackBones.push(bone);
              rootBoneContainer.add(bone);
            }

            const fallbackSkelId = 0;
            skeletonDataArray.push({
              skelId: fallbackSkelId,
              bones: fallbackBones,
              rootBone: rootBoneContainer,
              skeletonAbsPath: null,
              boneMap: fallbackBoneMap
            });

            allBoneMaps.set(fallbackSkelId, fallbackBoneMap);
            firstBones = fallbackBones;
            firstBoneMap = fallbackBoneMap;
            firstRootBone = rootBoneContainer;
            firstSkeletonAbsPath = null;
            totalJointCount = fallbackBones.length;
            fallbackSkeletonCreated = true;

            logger.log(
              `[Fallback] Built skeleton with ${fallbackBones.length} bones (flat hierarchy)`
            );
          }
        }
      }
    }

    if (onSkeletonInfo) {
      onSkeletonInfo(fallbackSkeletonCreated ? 1 : 0, totalJointCount);
    }
  }

  return {
    numSkeletons,
    totalJointCount,
    skeletonDataArray,
    boneMaps: allBoneMaps,
    firstBones,
    firstBoneMap,
    firstRootBone,
    firstSkeletonAbsPath,
    fallbackSkeletonCreated
  };
}

