/**
 * USD Skeletal Animation Converter
 *
 * Converts USD skeletal animation data (from TinyUSDZ WASM binding)
 * to Three.js AnimationClip format. Filters for SkeletonJoint channels
 * only and groups channels by joint for proper TRS track creation.
 *
 * @module USDAnimationConverter
 */

import * as THREE from 'three';

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
 * @param {Map} boneMap - Map from joint_id to THREE.Bone
 * @param {number} [timeCodesPerSecond=24] - USD timeCodesPerSecond for time conversion
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips
 */
export function convertUSDSkeletalAnimationsToThreeJS(usdLoader, boneMap, timeCodesPerSecond = 24) {
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

		// Group channels by joint_id to combine TRS into hierarchical bone animation
		const jointChannels = new Map();
		for (const channel of skeletalChannels) {
			const jointId = channel.joint_id;
			if (!jointChannels.has(jointId)) {
				jointChannels.set(jointId, {});
			}
			const joint = jointChannels.get(jointId);
			joint[channel.path] = channel;
		}

		// Process each joint's animation
		// Animation values are absolute joint-local transforms from SkelAnimation.
		// Bones are positioned from bindTransforms (Blender-style), and animation
		// replaces the local transform with the animated value (Normal blend mode).
		// skinningTransform = bone.matrixWorld * inverse(bindTransform) is correct
		// because bone.matrixWorld is computed by composing animLocal up the hierarchy.
		for (const [jointId, channels] of jointChannels) {
			const bone = boneMap.get(jointId);
			if (!bone) {
				console.warn(`Could not find bone for joint_id: ${jointId}`);
				continue;
			}

			const boneName = bone.name || `bone_${jointId}`;

			// Process Translation channel
			if (channels.Translation) {
				const channel = channels.Translation;
				const sampler = usdAnimation.samplers[channel.sampler];
				if (sampler && sampler.times && sampler.values) {
					const track = new THREE.VectorKeyframeTrack(
						`${boneName}.position`,
						sampler.times,
						sampler.values,
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
					const track = new THREE.QuaternionKeyframeTrack(
						`${boneName}.quaternion`,
						sampler.times,
						sampler.values,
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
					const track = new THREE.VectorKeyframeTrack(
						`${boneName}.scale`,
						sampler.times,
						sampler.values,
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
			const clip = new THREE.AnimationClip(
				usdAnimation.name || `SkeletalAnimation_${i}`,
				usdAnimation.duration || -1, // -1 will auto-calculate from tracks
				keyframeTracks
			);

			animationClips.push(clip);
			console.log(`Created skeletal clip: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}`);
		}
	}

	return animationClips;
}

export default {
	getUSDInterpolationMode,
	convertUSDSkeletalAnimationsToThreeJS
};
