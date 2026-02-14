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
    if (Array.isArray(m) && m.length === 16) {
      matrix.fromArray(m);
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

/**
 * Create a Three.js Skeleton from flattened USD skeleton data (optimized)
 *
 * This is a more efficient version that uses the pre-flattened skeleton data
 * from getSkeletonJointsFlat()
 *
 * @param {Object} flatSkeleton - Flattened skeleton from usd.getSkeletonJointsFlat(id)
 * @param {Object} [options] - Creation options
 * @param {boolean} [options.useRestPose=true] - Use rest pose transforms (local space)
 * @param {boolean} [options.useBindPose] - Deprecated: use useRestPose instead
 * @returns {THREE.Skeleton} Three.js Skeleton object
 */
export function createThreeSkeletonFromFlat(flatSkeleton, options = {}) {
  // Support deprecated useBindPose option (inverted logic)
  // useRestPose=true (default) means use rest_matrices (local space)
  const useRestPose = options.useRestPose !== undefined ? options.useRestPose :
                      options.useBindPose !== undefined ? !options.useBindPose : true;
  const numJoints = flatSkeleton.num_joints;

  // Create all bones first
  const bones = [];
  for (let i = 0; i < numJoints; i++) {
    const bone = new THREE.Bone();
    bone.name = flatSkeleton.joint_names[i] || `joint_${flatSkeleton.joint_ids[i]}`;
    bone.userData.joint_id = flatSkeleton.joint_ids[i];
    bone.userData.joint_path = flatSkeleton.joint_paths[i];
    bones.push(bone);
  }

  // Pre-compute parent bind matrices for fallback computation
  const bindMatrices = [];
  for (let i = 0; i < numJoints; i++) {
    const matrixOffset = i * 16;
    const matrixArray = flatSkeleton.bind_matrices.slice(matrixOffset, matrixOffset + 16);
    const matrix = new THREE.Matrix4();
    matrix.fromArray(Array.from(matrixArray));
    bindMatrices.push(matrix);
  }

  // Build hierarchy using parent indices
  for (let i = 0; i < numJoints; i++) {
    const parentIdx = flatSkeleton.parent_indices[i];

    if (useRestPose && flatSkeleton.rest_matrices) {
      // Use rest_matrices (local space) directly
      const matrixOffset = i * 16;
      const matrixArray = flatSkeleton.rest_matrices.slice(matrixOffset, matrixOffset + 16);
      const matrix = new THREE.Matrix4();
      matrix.fromArray(Array.from(matrixArray));

      bones[i].position.setFromMatrixPosition(matrix);
      bones[i].quaternion.setFromRotationMatrix(matrix);
      bones[i].scale.setFromMatrixScale(matrix);
    } else {
      // Compute local transform from world-space bind_matrices
      // localTransform = inverse(parent_bind) * child_bind
      if (parentIdx >= 0 && parentIdx < numJoints) {
        const parentInverse = bindMatrices[parentIdx].clone().invert();
        const localMatrix = parentInverse.clone().multiply(bindMatrices[i]);
        bones[i].position.setFromMatrixPosition(localMatrix);
        bones[i].quaternion.setFromRotationMatrix(localMatrix);
        bones[i].scale.setFromMatrixScale(localMatrix);
      } else {
        // Root bone: use bind matrix directly
        bones[i].position.setFromMatrixPosition(bindMatrices[i]);
        bones[i].quaternion.setFromRotationMatrix(bindMatrices[i]);
        bones[i].scale.setFromMatrixScale(bindMatrices[i]);
      }
    }

    // Store rest pose in userData for later reset
    bones[i].userData.restPosition = bones[i].position.clone();
    bones[i].userData.restQuaternion = bones[i].quaternion.clone();
    bones[i].userData.restScale = bones[i].scale.clone();

    // Add to parent if not root
    if (parentIdx >= 0 && parentIdx < numJoints) {
      bones[parentIdx].add(bones[i]);
    }
  }

  // Create skeleton
  const skeleton = new THREE.Skeleton(bones);

  return skeleton;
}

/**
 * Create Three.js AnimationClip from USD animation data
 *
 * @param {Object} usdAnimation - Animation data from usd.getAnimation(id)
 * @param {THREE.Skeleton} skeleton - Three.js skeleton to target
 * @param {Object} [options] - Creation options
 * @param {number} [options.fps=24] - Frames per second for time conversion
 * @returns {THREE.AnimationClip} Three.js AnimationClip
 */
export function createThreeAnimationClip(usdAnimation, skeleton, options = {}) {
  const fps = options.fps || 24;
  const tracks = [];

  // Build a map from joint_id to bone index in skeleton
  const jointIdToBoneIndex = new Map();
  skeleton.bones.forEach((bone, index) => {
    if (bone.userData.joint_id !== undefined) {
      jointIdToBoneIndex.set(bone.userData.joint_id, index);
    }
  });

  // Process each animation channel
  for (const channel of usdAnimation.channels) {
    // Only process skeletal animation channels
    if (channel.target_type !== 'SkeletonJoint') {
      continue;
    }

    const jointId = channel.joint_id;
    const boneIndex = jointIdToBoneIndex.get(jointId);

    if (boneIndex === undefined) {
      console.warn(`Joint ID ${jointId} not found in skeleton`);
      continue;
    }

    const bone = skeleton.bones[boneIndex];
    const samplerIdx = channel.sampler;

    if (samplerIdx < 0 || samplerIdx >= usdAnimation.samplers.length) {
      console.warn(`Invalid sampler index ${samplerIdx} for joint ${jointId}`);
      continue;
    }

    const sampler = usdAnimation.samplers[samplerIdx];
    const times = Array.from(sampler.times); // Convert typed array to regular array
    const values = Array.from(sampler.values);

    // Convert times from frames to seconds if needed
    // USD typically uses frame numbers, Three.js expects seconds
    const timesInSeconds = times.map(t => t / fps);

    // Create appropriate track based on path
    let track;
    const boneName = bone.name;

    switch (channel.path) {
      case 'Translation':
        // Create Vector3KeyframeTrack
        track = new THREE.VectorKeyframeTrack(
          `${boneName}.position`,
          timesInSeconds,
          values,
          sampler.interpolation === 'STEP' ? THREE.InterpolateDiscrete : THREE.InterpolateLinear
        );
        break;

      case 'Rotation':
        // Create QuaternionKeyframeTrack
        // USD uses (x, y, z, w) quaternions, same as Three.js
        track = new THREE.QuaternionKeyframeTrack(
          `${boneName}.quaternion`,
          timesInSeconds,
          values,
          sampler.interpolation === 'STEP' ? THREE.InterpolateDiscrete : THREE.InterpolateLinear
        );
        break;

      case 'Scale':
        // Create Vector3KeyframeTrack for scale
        track = new THREE.VectorKeyframeTrack(
          `${boneName}.scale`,
          timesInSeconds,
          values,
          sampler.interpolation === 'STEP' ? THREE.InterpolateDiscrete : THREE.InterpolateLinear
        );
        break;

      case 'Weights':
        // Create NumberKeyframeTrack for morph target weights
        track = new THREE.NumberKeyframeTrack(
          `${boneName}.morphTargetInfluences`,
          timesInSeconds,
          values,
          sampler.interpolation === 'STEP' ? THREE.InterpolateDiscrete : THREE.InterpolateLinear
        );
        break;

      default:
        console.warn(`Unknown animation path: ${channel.path}`);
        continue;
    }

    if (track) {
      tracks.push(track);
    }
  }

  // Create and return animation clip
  const clipName = usdAnimation.name || `Animation_${usdAnimation.prim_name || 'Unnamed'}`;
  const clip = new THREE.AnimationClip(clipName, usdAnimation.duration, tracks);

  clip.userData.usd_anim_id = usdAnimation.id;
  clip.userData.prim_name = usdAnimation.prim_name;
  clip.userData.abs_path = usdAnimation.abs_path;

  return clip;
}

/**
 * Create a Three.js SkinnedMesh from USD mesh and skeleton data
 *
 * @param {THREE.BufferGeometry} geometry - Geometry with skinning attributes
 * @param {THREE.Material} material - Material for the mesh
 * @param {THREE.Skeleton} skeleton - Skeleton for skinning
 * @param {Object} usdMesh - Original USD mesh data for reference
 * @param {Object} [options] - Creation options
 * @param {THREE.Matrix4} [options.geomBindTransform] - Optional geometry bind transform matrix
 * @returns {THREE.SkinnedMesh} Three.js SkinnedMesh
 */
export function createSkinnedMesh(geometry, material, skeleton, usdMesh, options = {}) {
  const mesh = new THREE.SkinnedMesh(geometry, material);
  mesh.add(skeleton.bones[0]); // Add root bone to mesh

  // Bind with optional geomBindTransform
  // geomBindTransform defines the mesh's transform when it was bound to the skeleton
  if (options.geomBindTransform) {
    mesh.bind(skeleton, options.geomBindTransform);
  } else if (usdMesh && usdMesh.geomBindTransform && usdMesh.geomBindTransform.length === 16) {
    // Parse geomBindTransform from USD mesh data if provided as array
    const bindMatrix = new THREE.Matrix4();
    bindMatrix.fromArray(Array.from(usdMesh.geomBindTransform));
    mesh.bind(skeleton, bindMatrix);
  } else {
    mesh.bind(skeleton);
  }

  // Store USD metadata
  if (usdMesh) {
    mesh.userData.usd_prim_name = usdMesh.primName;
    mesh.userData.usd_abs_path = usdMesh.absPath;
    mesh.userData.usd_skel_id = usdMesh.skel_id;
    mesh.userData.hasGeomBindTransform = usdMesh.hasGeomBindTransform || false;
  }

  return mesh;
}

/**
 * Setup skinning attributes on BufferGeometry from USD mesh data
 *
 * @param {THREE.BufferGeometry} geometry - Target geometry
 * @param {Object} usdMesh - USD mesh data with jointIndices and jointWeights
 * @param {number} [influencesPerVertex=4] - Number of bone influences per vertex
 * @returns {THREE.BufferGeometry} Geometry with skinning attributes
 */
export function addSkinningAttributes(geometry, usdMesh, influencesPerVertex = 4) {
  const vertexCount = geometry.attributes.position.count;

  // Create skinning attributes
  const skinIndices = new Uint16Array(vertexCount * 4); // Three.js always uses 4
  const skinWeights = new Float32Array(vertexCount * 4);

  // Copy from USD data
  const usdJointIndices = usdMesh.jointIndices;
  const usdJointWeights = usdMesh.jointWeights;

  for (let i = 0; i < vertexCount; i++) {
    for (let j = 0; j < 4; j++) {
      const srcIdx = i * influencesPerVertex + j;
      const dstIdx = i * 4 + j;

      if (j < influencesPerVertex && srcIdx < usdJointIndices.length) {
        skinIndices[dstIdx] = usdJointIndices[srcIdx];
        skinWeights[dstIdx] = usdJointWeights[srcIdx];
      } else {
        skinIndices[dstIdx] = 0;
        skinWeights[dstIdx] = 0;
      }
    }
  }

  // Add attributes to geometry
  geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(skinIndices, 4));
  geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(skinWeights, 4));

  return geometry;
}

/**
 * Complete helper to create a skinned mesh with animation from USD data
 *
 * @param {Object} usd - TinyUSDZ loader instance
 * @param {number} meshId - Mesh ID
 * @param {number} skelId - Skeleton ID
 * @param {number} [animId] - Animation ID (optional)
 * @param {Object} [options] - Options
 * @param {THREE.Material} [options.material] - Material to use
 * @param {number} [options.fps=24] - FPS for animation
 * @returns {Object} Object containing {mesh, skeleton, animationClip}
 */
export function createSkinnedMeshFromUSD(usd, meshId, skelId, animId, options = {}) {
  const fps = options.fps || 24;

  // Get USD data
  const usdMesh = usd.getMesh(meshId);
  const usdSkel = usd.getSkeleton(skelId);

  // Create Three.js geometry
  const geometry = new THREE.BufferGeometry();

  // Add position attribute
  if (usdMesh.points && usdMesh.points.length > 0) {
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(usdMesh.points, 3));
  }

  // Add normal attribute
  if (usdMesh.normals && usdMesh.normals.length > 0) {
    geometry.setAttribute('normal', new THREE.Float32BufferAttribute(usdMesh.normals, 3));
  }

  // Add UV attribute
  if (usdMesh.texcoords && usdMesh.texcoords.length > 0) {
    geometry.setAttribute('uv', new THREE.Float32BufferAttribute(usdMesh.texcoords, 2));
  }

  // Add face indices
  if (usdMesh.faceVertexIndices && usdMesh.faceVertexIndices.length > 0) {
    geometry.setIndex(new THREE.Uint32BufferAttribute(usdMesh.faceVertexIndices, 1));
  }

  // Add skinning attributes
  const influencesPerVertex = usdMesh.jointIndices.length / (usdMesh.points.length / 3);
  addSkinningAttributes(geometry, usdMesh, influencesPerVertex);

  // Create skeleton
  const skeleton = createThreeSkeletonFromUSD(usdSkel);

  // Create material if not provided
  const material = options.material || new THREE.MeshStandardMaterial({
    color: 0x888888,
    skinning: true
  });

  // Create skinned mesh
  const mesh = createSkinnedMesh(geometry, material, skeleton, usdMesh);

  // Create animation clip if requested
  let animationClip = null;
  if (animId !== undefined && animId >= 0) {
    const usdAnim = usd.getAnimation(animId);
    animationClip = createThreeAnimationClip(usdAnim, skeleton, { fps });
  }

  return {
    mesh,
    skeleton,
    animationClip,
    geometry,
    material
  };
}

/**
 * Create an AnimationMixer and play an animation
 *
 * @param {THREE.SkinnedMesh} mesh - Skinned mesh
 * @param {THREE.AnimationClip} clip - Animation clip
 * @param {Object} [options] - Playback options
 * @param {boolean} [options.loop=true] - Loop animation
 * @param {number} [options.timeScale=1] - Time scale
 * @returns {THREE.AnimationMixer} Animation mixer
 */
export function playAnimation(mesh, clip, options = {}) {
  const loop = options.loop !== undefined ? options.loop : true;
  const timeScale = options.timeScale !== undefined ? options.timeScale : 1;

  const mixer = new THREE.AnimationMixer(mesh);
  const action = mixer.clipAction(clip);

  action.setLoop(loop ? THREE.LoopRepeat : THREE.LoopOnce, loop ? Infinity : 1);
  action.timeScale = timeScale;
  action.play();

  return mixer;
}

export default {
  createThreeSkeletonFromUSD,
  createThreeSkeletonFromFlat,
  createThreeAnimationClip,
  createSkinnedMesh,
  addSkinningAttributes,
  createSkinnedMeshFromUSD,
  playAnimation,
  resetSkeletonToRestPose
};
