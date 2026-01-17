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
 * Create a Three.js Skeleton from USD skeleton data
 *
 * @param {Object} usdSkeleton - Skeleton data from usd.getSkeleton(id)
 * @param {Object} [options] - Creation options
 * @param {boolean} [options.useBindPose=true] - Use bind pose transforms for skeleton
 * @returns {THREE.Skeleton} Three.js Skeleton object
 */
export function createThreeSkeletonFromUSD(usdSkeleton, options = {}) {
  const useBindPose = options.useBindPose !== undefined ? options.useBindPose : true;

  // Recursively create bone hierarchy
  function createBoneHierarchy(skelNode, parentBone = null) {
    const bone = new THREE.Bone();
    bone.name = skelNode.joint_name || `joint_${skelNode.joint_id}`;
    bone.userData.joint_id = skelNode.joint_id;
    bone.userData.joint_path = skelNode.joint_path;

    // Get transform matrix (use bind pose or rest pose)
    const matrixArray = useBindPose ? skelNode.bind_transform : skelNode.rest_transform;
    const matrix = new THREE.Matrix4();

    // Convert from USD column-major to Three.js column-major (same order)
    matrix.fromArray(matrixArray);

    // Decompose matrix into position, quaternion, scale
    bone.position.setFromMatrixPosition(matrix);
    bone.quaternion.setFromRotationMatrix(matrix);
    bone.scale.setFromMatrixScale(matrix);

    // If this bone has a parent, make the transform relative
    if (parentBone) {
      parentBone.add(bone);
      // Transform is already in parent space from USD
    }

    // Recursively create children
    if (skelNode.children && skelNode.children.length > 0) {
      for (const childNode of skelNode.children) {
        createBoneHierarchy(childNode, bone);
      }
    }

    return bone;
  }

  // Create root bone and hierarchy
  const rootBone = createBoneHierarchy(usdSkeleton.root_node);

  // Collect all bones in breadth-first order for skeleton
  const bones = [];
  const queue = [rootBone];

  while (queue.length > 0) {
    const bone = queue.shift();
    bones.push(bone);

    for (const child of bone.children) {
      if (child instanceof THREE.Bone) {
        queue.push(child);
      }
    }
  }

  // Create Three.js Skeleton
  const skeleton = new THREE.Skeleton(bones);
  skeleton.userData.usd_skeleton_id = usdSkeleton.id;
  skeleton.userData.prim_name = usdSkeleton.prim_name;
  skeleton.userData.abs_path = usdSkeleton.abs_path;

  return skeleton;
}

/**
 * Create a Three.js Skeleton from flattened USD skeleton data (optimized)
 *
 * This is a more efficient version that uses the pre-flattened skeleton data
 * from getSkeletonJointsFlat()
 *
 * @param {Object} flatSkeleton - Flattened skeleton from usd.getSkeletonJointsFlat(id)
 * @param {Object} [options] - Creation options
 * @param {boolean} [options.useBindPose=true] - Use bind pose transforms
 * @returns {THREE.Skeleton} Three.js Skeleton object
 */
export function createThreeSkeletonFromFlat(flatSkeleton, options = {}) {
  const useBindPose = options.useBindPose !== undefined ? options.useBindPose : true;
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

  // Build hierarchy using parent indices
  for (let i = 0; i < numJoints; i++) {
    const parentIdx = flatSkeleton.parent_indices[i];

    // Get transform matrix
    const matrices = useBindPose ? flatSkeleton.bind_matrices : flatSkeleton.rest_matrices;
    const matrixOffset = i * 16;
    const matrixArray = matrices.slice(matrixOffset, matrixOffset + 16);

    const matrix = new THREE.Matrix4();
    matrix.fromArray(matrixArray);

    // Decompose into position, quaternion, scale
    bones[i].position.setFromMatrixPosition(matrix);
    bones[i].quaternion.setFromRotationMatrix(matrix);
    bones[i].scale.setFromMatrixScale(matrix);

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
 * @returns {THREE.SkinnedMesh} Three.js SkinnedMesh
 */
export function createSkinnedMesh(geometry, material, skeleton, usdMesh) {
  const mesh = new THREE.SkinnedMesh(geometry, material);
  mesh.add(skeleton.bones[0]); // Add root bone to mesh
  mesh.bind(skeleton);

  // Store USD metadata
  if (usdMesh) {
    mesh.userData.usd_prim_name = usdMesh.primName;
    mesh.userData.usd_abs_path = usdMesh.absPath;
    mesh.userData.usd_skel_id = usdMesh.skel_id;
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
  playAnimation
};
