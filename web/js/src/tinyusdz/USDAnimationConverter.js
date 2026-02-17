/**
 * USD Animation Converter
 *
 * Converts USD animation data (from TinyUSDZ WASM binding) to Three.js
 * AnimationClip format. Supports both SkeletonJoint channels (skeletal
 * animation) and SceneNode channels (xformOp animation on scene nodes).
 *
 * @module USDAnimationConverter
 */

import * as THREE from 'three';
import { toOwnedFloat32Array } from './TypedArrayOwnership.js';

/**
 * Convert USD interpolation mode to Three.js InterpolateMode
 * @param {string} interpolation - USD interpolation mode (Linear, Step, CubicSpline)
 * @returns {number} Three.js InterpolateMode constant
 */
export function getUSDInterpolationMode(interpolation) {
	switch (interpolation) {
		case 'Step':
		case 'STEP':
			return THREE.InterpolateDiscrete;
		case 'CubicSpline':
		case 'CUBICSPLINE':
			return THREE.InterpolateSmooth;
		case 'Linear':
		case 'LINEAR':
		default:
			return THREE.InterpolateLinear;
	}
}

/**
 * Convert USD skeletal animation data to Three.js AnimationClip
 * Extracts only SkeletonJoint animations from USD SkelAnimation
 * @param {Object} usdLoader - TinyUSDZ loader instance
 * @param {Map} boneMaps - Map from skeleton_id to Map(joint_id -> THREE.Bone)
 * @param {number} [timeCodesPerSecond=24] - USD timeCodesPerSecond for time conversion
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips
 */
export function convertUSDSkeletalAnimationsToThreeJS(usdLoader, boneMaps, timeCodesPerSecond = 24) {
	const animationClips = [];

	// Get number of animations
	const numAnimations = usdLoader.numAnimations();
	console.log(`Found ${numAnimations} animations in USD file`);

	// Get summary of all animations
	const animationInfos = usdLoader.getAllAnimationInfos();
	console.log('Animation summaries:', animationInfos);

	// Convert each animation to Three.js format
	for (let i = 0; i < numAnimations; i++) {
		const usdAnimation = usdLoader.getAnimation(i);
		console.log(`Processing animation ${i}: ${usdAnimation.name}`);

		if (!usdAnimation.channels || !usdAnimation.samplers) {
			console.warn(`Animation ${i} missing channels or samplers`);
			continue;
		}

		// Filter for skeletal animations only (skip node animations)
		const skeletalChannels = usdAnimation.channels.filter(channel => {
			const targetType = channel.target_type || 'SceneNode';
			return targetType === 'SkeletonJoint';
		});

		if (skeletalChannels.length === 0) {
			console.log(`Animation ${i} has no SkeletonJoint channels (skipping node-only animation)`);
			continue;
		}

		console.log(`Animation ${i}: ${skeletalChannels.length} skeletal channels (${usdAnimation.channels.length - skeletalChannels.length} node channels skipped)`);

		// Create Three.js KeyframeTracks from USD skeletal animation channels
		const keyframeTracks = [];

		// Group channels by (skeleton_id, joint_id) to combine TRS into hierarchical bone animation
		const jointChannels = new Map();
		const skelIdsInAnimation = new Set();
		for (const channel of skeletalChannels) {
			const skelId = channel.skeleton_id !== undefined ? channel.skeleton_id : 0;
			const jointId = channel.joint_id;
			skelIdsInAnimation.add(skelId);
			const key = `${skelId}_${jointId}`;
			if (!jointChannels.has(key)) {
				jointChannels.set(key, { skeleton_id: skelId, joint_id: jointId, channels: {} });
			}
			const joint = jointChannels.get(key);
			joint.channels[channel.path] = channel;
		}
		console.log(`Animation ${i} (${usdAnimation.name}): targets skeleton IDs: ${Array.from(skelIdsInAnimation).sort().join(', ')}`);

		// Process each joint's animation
		// Animation values are absolute joint-local transforms from SkelAnimation.
		// Bones are positioned from bindTransforms (Blender-style), and animation
		// replaces the local transform with the animated value (Normal blend mode).
		// skinningTransform = bone.matrixWorld * inverse(bindTransform) is correct
		// because bone.matrixWorld is computed by composing animLocal up the hierarchy.
		for (const [key, jointData] of jointChannels) {
			const skelId = jointData.skeleton_id;
			const jointId = jointData.joint_id;
			const channels = jointData.channels;

			// Get the correct boneMap for this skeleton
			const boneMap = boneMaps.get(skelId);
			if (!boneMap) {
				console.warn(`Could not find boneMap for skeleton_id: ${skelId}`);
				continue;
			}

			const bone = boneMap.get(jointId);
			if (!bone) {
				console.warn(`Could not find bone for skeleton_id: ${skelId}, joint_id: ${jointId}`);
				continue;
			}

			const boneName = bone.name || `bone_${jointId}`;

			// Process Translation channel
			if (channels.Translation) {
				const channel = channels.Translation;
				const sampler = usdAnimation.samplers[channel.sampler];
				if (sampler && sampler.times && sampler.values) {
					// Copy WASM typed_memory_view arrays into JS-owned buffers.
					// usd_scene.delete() frees C++ data, invalidating views.
					// Keep times in timeCodes (frames), not seconds - mixer handles frame-based playback
					const track = new THREE.VectorKeyframeTrack(
						`${boneName}.position`,
						toOwnedFloat32Array(sampler.times, 'sampler.times'),
						toOwnedFloat32Array(sampler.values, 'sampler.values'),
						getUSDInterpolationMode(sampler.interpolation)
					);
					keyframeTracks.push(track);
				}
			}

			// Process Rotation channel
			if (channels.Rotation) {
				const channel = channels.Rotation;
				const sampler = usdAnimation.samplers[channel.sampler];
				if (sampler && sampler.times && sampler.values) {
					// Copy WASM typed_memory_view arrays into JS-owned buffers.
					// Keep times in timeCodes (frames), not seconds
					const track = new THREE.QuaternionKeyframeTrack(
						`${boneName}.quaternion`,
						toOwnedFloat32Array(sampler.times, 'sampler.times'),
						toOwnedFloat32Array(sampler.values, 'sampler.values'),
						getUSDInterpolationMode(sampler.interpolation)
					);
					keyframeTracks.push(track);
				}
			}

			// Process Scale channel
			if (channels.Scale) {
				const channel = channels.Scale;
				const sampler = usdAnimation.samplers[channel.sampler];
				if (sampler && sampler.times && sampler.values) {
					// Copy WASM typed_memory_view arrays into JS-owned buffers.
					// Keep times in timeCodes (frames), not seconds
					const track = new THREE.VectorKeyframeTrack(
						`${boneName}.scale`,
						toOwnedFloat32Array(sampler.times, 'sampler.times'),
						toOwnedFloat32Array(sampler.values, 'sampler.values'),
						getUSDInterpolationMode(sampler.interpolation)
					);
					keyframeTracks.push(track);
				}
			}
		}

		// Expand single-keyframe tracks to span the full duration.
		// Three.js doesn't extrapolate/hold tracks beyond their keyframe range.
		// A track with times=[0] only applies at exactly t=0; at t>0 the mixer
		// resets the property to its default (0 for position, identity for quat).
		// USD constant attributes (non-time-sampled translations/scales) produce
		// single-keyframe samplers that must span the full clip to stay applied.
		if (keyframeTracks.length > 0) {
			// Find the max time across all tracks to determine the effective duration
			let maxTime = usdAnimation.duration || 0;
			for (const track of keyframeTracks) {
				const lastTime = track.times[track.times.length - 1];
				if (lastTime > maxTime) maxTime = lastTime;
			}

			if (maxTime > 0) {
				for (let t = 0; t < keyframeTracks.length; t++) {
					const track = keyframeTracks[t];
					if (track.times.length === 1 && track.times[0] < maxTime) {
						// Duplicate the single keyframe at the end of the clip
						const stride = track.getValueSize();
						const newTimes = new Float32Array([track.times[0], maxTime]);
						const newValues = new Float32Array(stride * 2);
						for (let s = 0; s < stride; s++) {
							newValues[s] = track.values[s];
							newValues[stride + s] = track.values[s];
						}
						track.times = newTimes;
						track.values = newValues;
					}
				}
			}
		}

		// Create Three.js AnimationClip
		if (keyframeTracks.length > 0) {
			// Let Three.js auto-calculate duration from the actual track keyframes (use -1)
			// This ensures we get the full animation range from the keyframe data
			const clip = new THREE.AnimationClip(
				usdAnimation.name || `SkeletalAnimation_${i}`,
				-1,  // Auto-calculate from tracks to get actual keyframe range
				keyframeTracks
			);

			animationClips.push(clip);
			console.log(`Created skeletal clip: ${clip.name}, duration: ${clip.duration} frames (auto-calc from tracks), USD duration: ${usdAnimation.duration || 'N/A'}`);
		}
	}

	return animationClips;
}

/**
 * Build a node index map by DFS-traversing a Three.js scene node.
 * Must be called BEFORE bones are added to the hierarchy, since adding
 * bones would shift DFS indices and break the mapping to USD node indices.
 * @param {THREE.Object3D} threeNode - Root of the Three.js scene hierarchy
 * @returns {Map<number, THREE.Object3D>} Map from DFS index to Three.js object
 */
export function buildNodeIndexMap(threeNode) {
	const nodeIndexMap = new Map();
	let nodeIndex = 0;
	threeNode.traverse((obj) => {
		nodeIndexMap.set(nodeIndex, obj);
		nodeIndex++;
	});
	return nodeIndexMap;
}

/**
 * Convert USD SceneNode animation data to Three.js AnimationClips.
 * Extracts xformOp animations (translate/rotate/scale) on scene graph nodes
 * such as SkelRoot or Xform ancestors of Skeletons.
 *
 * With AttachedBindMode, animated ancestor transforms are handled correctly:
 * bindMatrixInverse = inv(mesh.matrixWorld) updates each frame, so the
 * ancestor transform cancels out in the skinning equation.
 *
 * @param {Object} usdLoader - TinyUSDZ loader instance
 * @param {Map<number, THREE.Object3D>} nodeIndexMap - Pre-built DFS index → Object3D map
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips for node animations
 */
export function convertUSDNodeAnimationsToThreeJS(usdLoader, nodeIndexMap) {
	const animationClips = [];

	const numAnimations = usdLoader.numAnimations();

	for (let i = 0; i < numAnimations; i++) {
		const usdAnimation = usdLoader.getAnimation(i);

		if (!usdAnimation.channels || !usdAnimation.samplers) {
			continue;
		}

		// Filter for SceneNode channels only (inverse of skeletal filter)
		const nodeChannels = usdAnimation.channels.filter(channel => {
			const targetType = channel.target_type || 'SceneNode';
			return targetType === 'SceneNode';
		});

		if (nodeChannels.length === 0) {
			continue;
		}

		const keyframeTracks = [];

		for (const channel of nodeChannels) {
			const sampler = usdAnimation.samplers[channel.sampler];
			if (!sampler || !sampler.times || !sampler.values) {
				continue;
			}

			const targetObject = nodeIndexMap.get(channel.target_node);
			if (!targetObject) {
				console.warn(`Node animation: target_node ${channel.target_node} not found in nodeIndexMap`);
				continue;
			}

			// Use UUID for reliable hierarchical animation targeting
			const targetUUID = targetObject.uuid;
			const interpolation = getUSDInterpolationMode(sampler.interpolation);
			// Copy WASM typed_memory_view arrays into JS-owned buffers
			const times = toOwnedFloat32Array(sampler.times, 'sampler.times');
			const values = toOwnedFloat32Array(sampler.values, 'sampler.values');

			let track;
			switch (channel.path) {
				case 'Translation':
					track = new THREE.VectorKeyframeTrack(
						`${targetUUID}.position`, times, values, interpolation
					);
					break;
				case 'Rotation':
					track = new THREE.QuaternionKeyframeTrack(
						`${targetUUID}.quaternion`, times, values, interpolation
					);
					break;
				case 'Scale':
					track = new THREE.VectorKeyframeTrack(
						`${targetUUID}.scale`, times, values, interpolation
					);
					break;
				default:
					continue;
			}

			keyframeTracks.push(track);
		}

		// Expand single-keyframe tracks to span the full duration
		if (keyframeTracks.length > 0) {
			let maxTime = usdAnimation.duration || 0;
			for (const track of keyframeTracks) {
				const lastTime = track.times[track.times.length - 1];
				if (lastTime > maxTime) maxTime = lastTime;
			}

			if (maxTime > 0) {
				for (const track of keyframeTracks) {
					if (track.times.length === 1 && track.times[0] < maxTime) {
						const stride = track.getValueSize();
						const newTimes = new Float32Array([track.times[0], maxTime]);
						const newValues = new Float32Array(stride * 2);
						for (let s = 0; s < stride; s++) {
							newValues[s] = track.values[s];
							newValues[stride + s] = track.values[s];
						}
						track.times = newTimes;
						track.values = newValues;
					}
				}
			}

			const clip = new THREE.AnimationClip(
				usdAnimation.name ? `${usdAnimation.name}_nodes` : `NodeAnimation_${i}`,
				-1,
				keyframeTracks
			);

			animationClips.push(clip);
			console.log(`Created node animation clip: ${clip.name}, duration: ${clip.duration} frames, ${keyframeTracks.length} tracks (targeting ${nodeChannels.length} SceneNode channels)`);
		}
	}

	if (animationClips.length > 0) {
		console.log(`Extracted ${animationClips.length} node animation clip(s)`);
	}

	return animationClips;
}

export default {
	getUSDInterpolationMode,
	convertUSDSkeletalAnimationsToThreeJS,
	buildNodeIndexMap,
	convertUSDNodeAnimationsToThreeJS
};
