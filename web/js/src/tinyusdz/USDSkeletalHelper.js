/**
 * USD Skeletal Animation Helper for Three.js
 *
 * This module provides utilities to convert USD skeletal animation data
 * (from TinyUSDZ WASM binding) to Three.js SkinnedMesh and AnimationClip format.
 *
 * @module USDSkeletalHelper
 */

import * as THREE from 'three';

/**
 * Create a Three.js Skeleton from USD skeleton data.
 *
 * Uses bind_transform (world space) by default to derive bone local transforms,
 * ensuring bone.matrixWorld = bindTransform at rest. This matches Blender's approach
 * and is correct for animation playback where SkelAnimation replaces the local
 * transform with absolute joint-local values.
 *
 * @param {Object} usdSkeleton - Skeleton data from usd.getSkeleton(id)
 * @param {Object} [options] - Creation options
 * @param {boolean} [options.useBindTransforms=true] - Use bind transforms (world space) for skeleton.
 *   When true (default), bone positions are derived from bind_transform so that
 *   bone.matrixWorld == bindTransform at rest. When false, uses rest_transform (local space).
 * @param {number} [options.skelId] - Skeleton ID for unique bone naming across multiple skeletons
 * @returns {{ bones: Array<THREE.Bone>, boneMap: Map<number, THREE.Bone>, rootBone: THREE.Bone, boneInverses: Array<THREE.Matrix4> }}
 */
export function createThreeSkeletonFromUSD(usdSkeleton, options = {}) {
  const useBindTransforms = options.useBindTransforms !== undefined ? options.useBindTransforms : true;
  const skelId = options.skelId !== undefined ? options.skelId : 0;

  const bones = [];
  const boneMap = new Map();
  const boneBindMatrices = []; // Store bind matrices for computing inverse bind matrices
  let jointId = 0;

  // Helper to parse matrix from USD format
  function parseMatrix(m) {
    const matrix = new THREE.Matrix4();
    if ((Array.isArray(m) || ArrayBuffer.isView(m)) && m.length === 16) {
      matrix.fromArray(Array.from(m));
    } else if (m && m[0] !== undefined && Array.isArray(m[0])) {
      // Legacy 2D array format (4x4) - flatten to column-major
      const flat = [
        m[0][0], m[1][0], m[2][0], m[3][0],
        m[0][1], m[1][1], m[2][1], m[3][1],
        m[0][2], m[1][2], m[2][2], m[3][2],
        m[0][3], m[1][3], m[2][3], m[3][3]
      ];
      matrix.fromArray(flat);
    }
    return matrix;
  }

  /**
   * Recursively build bone hierarchy.
   * Uses bindTransform (world space) to derive bone local transforms, ensuring
   * bone.matrixWorld = bindTransform at rest. restTransform is stored separately
   * for fallback. Animation is expressed as deltas from bind pose.
   */
  function buildBoneHierarchy(skelNode, parentBone, parentBindMatrix) {
    const bone = new THREE.Bone();
    // Extract leaf name from path (e.g., "a/b/c" -> "c") for Three.js compatibility
    let jointName = skelNode.joint_name || skelNode.joint_path || `joint_${jointId}`;
    const lastSlash = jointName.lastIndexOf('/');
    if (lastSlash !== -1) {
      jointName = jointName.substring(lastSlash + 1);
    }
    // Prefix bone name with skeleton ID for uniqueness across multiple skeletons
    bone.name = `skel${skelId}_${jointName}`;

    // Store mapping from joint_id to bone
    const currentJointId = skelNode.joint_id !== undefined ? skelNode.joint_id : jointId;
    boneMap.set(currentJointId, bone);
    bone.userData.joint_id = currentJointId;
    bone.userData.joint_path = skelNode.joint_path;
    jointId++;

    // Parse both transforms if available
    const hasRestTransform = skelNode.rest_transform && skelNode.rest_transform.length === 16;
    const hasBindTransform = skelNode.bind_transform && skelNode.bind_transform.length === 16;

    const restMatrix = hasRestTransform ? parseMatrix(skelNode.rest_transform) : null;
    const bindMatrix = hasBindTransform ? parseMatrix(skelNode.bind_transform) : null;

    // Store bind matrix for computing inverse bind matrix later
    boneBindMatrices.push({ bone, bindMatrix: bindMatrix ? bindMatrix.clone() : null });

    // Store rest transform in userData (for potential fallback when no animation)
    if (restMatrix) {
      const restPos = new THREE.Vector3();
      const restQuat = new THREE.Quaternion();
      const restScale = new THREE.Vector3();
      restMatrix.decompose(restPos, restQuat, restScale);
      bone.userData.restPosition = restPos.clone();
      bone.userData.restQuaternion = restQuat.clone();
      bone.userData.restScale = restScale.clone();
    }

    // Determine bone's local transform
    if (useBindTransforms && hasBindTransform) {
      // Compute local bind transform: localBind = inverse(parent_bind) * child_bind
      if (parentBone && parentBindMatrix) {
        const parentInverse = parentBindMatrix.clone().invert();
        const localBindMatrix = parentInverse.clone().multiply(bindMatrix);
        localBindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
      } else {
        // Root bone: use bind_transform directly (world space = local for root)
        bindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
      }

      // Store bind-derived local transform for animation reset
      bone.userData.bindPosition = bone.position.clone();
      bone.userData.bindQuaternion = bone.quaternion.clone();
      bone.userData.bindScale = bone.scale.clone();
    } else if (hasRestTransform) {
      // Use rest_transform (local space) directly
      restMatrix.decompose(bone.position, bone.quaternion, bone.scale);
      bone.userData.bindPosition = bone.position.clone();
      bone.userData.bindQuaternion = bone.quaternion.clone();
      bone.userData.bindScale = bone.scale.clone();
    } else if (hasBindTransform) {
      // Fallback: useBindTransforms is false but no rest_transform available
      if (parentBone && parentBindMatrix) {
        const parentInverse = parentBindMatrix.clone().invert();
        const localBindMatrix = parentInverse.clone().multiply(bindMatrix);
        localBindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
      } else {
        bindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
      }
      bone.userData.bindPosition = bone.position.clone();
      bone.userData.bindQuaternion = bone.quaternion.clone();
      bone.userData.bindScale = bone.scale.clone();
    } else {
      // Neither transform available - use identity
      bone.userData.bindPosition = new THREE.Vector3(0, 0, 0);
      bone.userData.bindQuaternion = new THREE.Quaternion(0, 0, 0, 1);
      bone.userData.bindScale = new THREE.Vector3(1, 1, 1);
    }

    // If rest transform wasn't stored above, use bind-derived as fallback
    if (!bone.userData.restPosition) {
      bone.userData.restPosition = bone.position.clone();
      bone.userData.restQuaternion = bone.quaternion.clone();
      bone.userData.restScale = bone.scale.clone();
    }

    if (parentBone) {
      parentBone.add(bone);
    }

    // Process children with current bone's bind matrix as parent reference
    if (skelNode.children && skelNode.children.length > 0) {
      for (const childNode of skelNode.children) {
        buildBoneHierarchy(childNode, bone, bindMatrix);
      }
    }

    return bone;
  }

  // Build from root node
  if (!usdSkeleton.root_node) {
    console.warn('No root_node found in skeleton');
    return { bones: [], boneMap: new Map(), rootBone: null, boneInverses: [] };
  }

  const rootBone = buildBoneHierarchy(usdSkeleton.root_node, null, null);

  // Collect all bones in depth-first order
  const allBones = [];
  rootBone.traverse((bone) => {
    if (bone.isBone) {
      allBones.push(bone);
    }
  });

  // Add tip bones for leaf joints (visualization only).
  // SkeletonHelper draws lines from each bone to its parent, so the last
  // bone's rotation is invisible without a child. Tip bones fix this.
  for (const bone of allBones) {
    const hasBoneChild = bone.children.some(c => c.isBone);
    if (!hasBoneChild) {
      const tipBone = new THREE.Bone();
      tipBone.name = bone.name + '_tip';
      tipBone.userData.isTipBone = true;
      // Extend in the same direction as the bone's offset from its parent
      const len = bone.position.length();
      if (len > 0) {
        tipBone.position.copy(bone.position).normalize().multiplyScalar(len);
      } else {
        tipBone.position.set(0, 0.1, 0);
      }
      bone.add(tipBone);
    }
  }

  // Compute inverse bind matrices from USD bind transforms
  const boneInverses = [];
  for (const boneData of boneBindMatrices) {
    if (boneData.bindMatrix) {
      const inverseBindMatrix = boneData.bindMatrix.clone().invert();
      boneInverses.push(inverseBindMatrix);
    } else {
      console.warn(`No bind matrix for bone, using identity`);
      boneInverses.push(new THREE.Matrix4());
    }
  }

  console.log(`Built skeleton with ${allBones.length} bones, ${boneInverses.length} inverse bind matrices`);

  return { bones: allBones, boneMap, rootBone, boneInverses };
}

/**
 * Reset skeleton bones to their bind pose transforms.
 * Prefers bind-derived local transforms (bindPosition/bindQuaternion/bindScale)
 * stored by createThreeSkeletonFromUSD, with fallback to restPosition for
 * backward compatibility with older skeleton data.
 * @param {THREE.Skeleton} skeleton - The skeleton to reset
 */
export function resetSkeletonToRestPose(skeleton) {
  if (!skeleton || !skeleton.bones) return;

  for (const bone of skeleton.bones) {
    // Prefer bind-derived transforms (set by createThreeSkeletonFromUSD)
    if (bone.userData.bindPosition) {
      bone.position.copy(bone.userData.bindPosition);
    } else if (bone.userData.restPosition) {
      bone.position.copy(bone.userData.restPosition);
    }
    if (bone.userData.bindQuaternion) {
      bone.quaternion.copy(bone.userData.bindQuaternion);
    } else if (bone.userData.restQuaternion) {
      bone.quaternion.copy(bone.userData.restQuaternion);
    }
    if (bone.userData.bindScale) {
      bone.scale.copy(bone.userData.bindScale);
    } else if (bone.userData.restScale) {
      bone.scale.copy(bone.userData.restScale);
    }
  }

  // Update matrices
  if (skeleton.bones.length > 0) {
    skeleton.bones[0].updateMatrixWorld(true);
  }
  console.log('Reset skeleton to bind pose');
}

