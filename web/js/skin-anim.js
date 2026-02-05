import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
// USD Skeletal Animation Helper - provides simplified Three.js integration
import {
	createThreeSkeletonFromUSD,
	createThreeAnimationClip,
	createSkinnedMeshFromUSD,
	playAnimation as helperPlayAnimation
} from 'tinyusdz/USDSkeletalHelper.js';
// Extended Skinning Support - supports 4, 8, 16, 32, 64+ bones per vertex
import {
	SkinningMode,
	getSkinningMode,
	addExtendedSkinningAttributes,
	applyExtendedSkinningIfNeeded,
	createExtendedWeightVisualizationMaterial,
	// WASM bone texture functions for efficient GPU skinning
	createBoneTextureFromWASM,
	applyWASMBoneTextureToGeometry,
	createMaterialFromWASMBoneTexture
} from 'tinyusdz/ExtendedSkinning.js';

// ===========================================
// Configuration
// ===========================================

/**
 * USD Skeletal Animation - Two Implementation Approaches
 *
 * This demo supports two ways to handle skeletal animation from USD files:
 *
 * 1. SIMPLIFIED HELPER APPROACH (USE_SKELETAL_HELPER = true):
 *    - Uses USDSkeletalHelper.js functions for minimal code
 *    - Best for: Basic skeletal animation playback
 *    - Pros: Simple, clean, easy to understand
 *    - Example usage:
 *      ```javascript
 *      const skeleton = createThreeSkeletonFromUSD(usdSkeleton);
 *      const clip = createThreeAnimationClip(usdAnimation, skeleton);
 *      // or even simpler:
 *      const result = createSkinnedMeshFromUSD(usd, meshId, skelId, animId);
 *      ```
 *
 * 2. CUSTOM ADVANCED APPROACH (USE_SKELETAL_HELPER = false, default):
 *    - Manual skeleton building with custom functions
 *    - Best for: Advanced features and debugging
 *    - Pros: Full control, weight visualization, joint manipulation, transform controls
 *    - Includes: Custom shader for weight visualization, joint sphere gizmos,
 *                transform controls, joint hierarchy display, bone reduction
 *
 * Toggle between approaches by changing USE_SKELETAL_HELPER below.
 */
const USE_SKELETAL_HELPER = false;

// Scene setup
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1a1a1a);

// Module-level variable to store inverse bind matrices from skeleton building
let _cachedBoneInverses = [];

// Camera
const camera = new THREE.PerspectiveCamera(
	75,
	window.innerWidth / window.innerHeight,
	0.1,
	1000
);
camera.position.set(5, 5, 5);
camera.lookAt(0, 0, 0);

// Renderer
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.body.appendChild(renderer.domElement);

// Orbit controls
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.05;

// Transform controls for joint manipulation
const transformControls = new TransformControls(camera, renderer.domElement);
transformControls.setMode('translate');
transformControls.setSpace('local');
transformControls.addEventListener('dragging-changed', (event) => {
	controls.enabled = !event.value; // Disable orbit controls while dragging
});
// In Three.js r150+, TransformControls may need special handling
if (transformControls.isObject3D) {
	scene.add(transformControls);
} else if (transformControls.getHelper) {
	// Some versions use getHelper() for the visual gizmo
	scene.add(transformControls.getHelper());
} else {
	console.warn('TransformControls could not be added to scene - check Three.js version compatibility');
}

// Raycaster for joint selection
const raycaster = new THREE.Raycaster();
const mouse = new THREE.Vector2();

// Lighting - brighter ambient for better visibility
const ambientLight = new THREE.AmbientLight(0x808080, 1.5);
scene.add(ambientLight);

const directionalLight = new THREE.DirectionalLight(0xffffff, 2.5);
directionalLight.position.set(5, 10, 5);
directionalLight.castShadow = true;
// Higher resolution shadow map
directionalLight.shadow.mapSize.width = 2048;
directionalLight.shadow.mapSize.height = 2048;
// Initial shadow camera frustum (will be updated based on scene)
directionalLight.shadow.camera.left = -10;
directionalLight.shadow.camera.right = 10;
directionalLight.shadow.camera.top = 10;
directionalLight.shadow.camera.bottom = -10;
directionalLight.shadow.camera.near = 0.1;
directionalLight.shadow.camera.far = 50;
directionalLight.shadow.bias = -0.001;
scene.add(directionalLight);

// Ground plane
const groundGeometry = new THREE.PlaneGeometry(20, 20);
const groundMaterial = new THREE.MeshStandardMaterial({
	color: 0x333333,
	roughness: 0.8,
	metalness: 0.2
});
const ground = new THREE.Mesh(groundGeometry, groundMaterial);
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);

// Grid helper
const gridHelper = new THREE.GridHelper(20, 20, 0x666666, 0x444444);
scene.add(gridHelper);

// Skeleton visualization helper
let skeletonHelper = null;

// Character root group (virtual USD scene root)
const usdSceneRoot = new THREE.Group();
usdSceneRoot.name = "/";
scene.add(usdSceneRoot);

// Character content group (holds the actual mesh)
const characterGroup = new THREE.Group();
usdSceneRoot.add(characterGroup);

// Animation state
let skinnedMesh = null;
let skeleton = null;
let mixer = null;
let animationAction = null;
let usdAnimations = [];
let boneMap = new Map(); // Map from joint_id to THREE.Bone

// Store the current file's upAxis (Y or Z)
let currentFileUpAxis = "Y";

// Store the current file's timeCodesPerSecond (default 24)
let currentTimeCodesPerSecond = 24;

// Debug visualization
let jointSpheres = [];
let weightVisualizationMaterial = null;
let originalMaterial = null;

// Joint selection state
let selectedJoint = null;
let selectedSphere = null;

// ===========================================
// USD Skeletal Animation Extraction Functions
// ===========================================

/**
 * Convert USD skeletal animation data to Three.js AnimationClip
 * Extracts only SkeletonJoint animations from USD SkelAnimation
 * @param {Object} usdLoader - TinyUSDZ loader instance
 * @param {Map} boneMap - Map from joint_id to THREE.Bone
 * @param {number} [timeCodesPerSecond=24] - USD timeCodesPerSecond for time conversion
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips
 */
function convertUSDSkeletalAnimationsToThreeJS(usdLoader, boneMap, timeCodesPerSecond = 24) {
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
					const times = Array.isArray(sampler.times) ? sampler.times : Array.from(sampler.times);
					const values = Array.isArray(sampler.values) ? sampler.values : Array.from(sampler.values);

					const track = new THREE.VectorKeyframeTrack(
						`${boneName}.position`,
						times,
						values,
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
					const times = Array.isArray(sampler.times) ? sampler.times : Array.from(sampler.times);
					const values = Array.isArray(sampler.values) ? sampler.values : Array.from(sampler.values);

					const track = new THREE.QuaternionKeyframeTrack(
						`${boneName}.quaternion`,
						times,
						values,
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
					const times = Array.isArray(sampler.times) ? sampler.times : Array.from(sampler.times);
					const values = Array.isArray(sampler.values) ? sampler.values : Array.from(sampler.values);

					const track = new THREE.VectorKeyframeTrack(
						`${boneName}.scale`,
						times,
						values,
						getUSDInterpolationMode(sampler.interpolation)
					);
					keyframeTracks.push(track);
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

/**
 * Convert USD interpolation mode to Three.js InterpolateMode
 * @param {string} interpolation - USD interpolation mode (Linear, Step, CubicSpline)
 * @returns {number} Three.js InterpolateMode constant
 */
function getUSDInterpolationMode(interpolation) {
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
 * Build Three.js Skeleton from USD skeleton hierarchy
 * Uses rest_transform for local bone transforms (initial/rest pose)
 * Stores both rest_transform and bind_transform for proper skinning and animation reset
 * @param {Object} usdSkeleton - USD skeleton data
 * @param {number} skeletonId - Index of the skeleton
 * @returns {Object} { bones: Array<THREE.Bone>, boneMap: Map, rootBone: THREE.Bone }
 */
function buildSkeletonFromUSD(usdSkeleton, skeletonId) {
	console.log('Building skeleton:', usdSkeleton);

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
	 * Recursively build bone hierarchy
	 * Uses rest_transform (local space) for bone positioning when available
	 * Falls back to computing local transforms from bind_transform (world space) if rest_transform is missing
	 * @param {Object} skelNode - USD SkelNode
	 * @param {THREE.Bone} parentBone - Parent bone (null for root)
	 * @param {THREE.Matrix4} parentBindMatrix - Parent's bind transform (world space) for fallback
	 */
	function buildBoneHierarchy(skelNode, parentBone, parentBindMatrix) {
		const bone = new THREE.Bone();
		// Extract leaf name from path (e.g., "a/b/c" -> "c") for Three.js compatibility
		// Three.js uses "/" as hierarchy separator, so we need just the leaf name
		let jointName = skelNode.joint_name || skelNode.joint_path || `joint_${jointId}`;
		const lastSlash = jointName.lastIndexOf('/');
		if (lastSlash !== -1) {
			jointName = jointName.substring(lastSlash + 1);
		}
		bone.name = jointName;

		// Store mapping from joint_id to bone
		const currentJointId = skelNode.joint_id !== undefined ? skelNode.joint_id : jointId;
		boneMap.set(currentJointId, bone);
		bone.userData.joint_id = currentJointId;
		jointId++;

		// Parse both transforms if available
		const hasRestTransform = skelNode.rest_transform && skelNode.rest_transform.length === 16;
		const hasBindTransform = skelNode.bind_transform && skelNode.bind_transform.length === 16;

		const restMatrix = hasRestTransform ? parseMatrix(skelNode.rest_transform) : null;
		const bindMatrix = hasBindTransform ? parseMatrix(skelNode.bind_transform) : null;

		// Store bind matrix for computing inverse bind matrix later
		// This is crucial for proper skinning deformation
		boneBindMatrices.push({ bone, bindMatrix: bindMatrix ? bindMatrix.clone() : null });

		// Store rest transform in userData for later reset
		// rest_transform is in LOCAL space - can be applied directly to bone
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
		// Priority: rest_transform (local) > computed from bind_transform (world)
		if (hasRestTransform) {
			// rest_transform is in LOCAL space - apply directly
			restMatrix.decompose(bone.position, bone.quaternion, bone.scale);
		} else if (hasBindTransform) {
			// Fallback: compute local transform from world-space bind_transform
			// localTransform = inverse(parent_bind) * child_bind
			if (parentBone && parentBindMatrix) {
				const parentInverse = parentBindMatrix.clone().invert();
				const localMatrix = parentInverse.clone().multiply(bindMatrix);
				localMatrix.decompose(bone.position, bone.quaternion, bone.scale);
			} else {
				// Root bone: use bind_transform directly (world space)
				bindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
			}

			// Store computed local transform as rest pose
			bone.userData.restPosition = bone.position.clone();
			bone.userData.restQuaternion = bone.quaternion.clone();
			bone.userData.restScale = bone.scale.clone();
		} else {
			// Neither transform available - use identity
			bone.userData.restPosition = new THREE.Vector3(0, 0, 0);
			bone.userData.restQuaternion = new THREE.Quaternion(0, 0, 0, 1);
			bone.userData.restScale = new THREE.Vector3(1, 1, 1);
		}

		if (parentBone) {
			parentBone.add(bone);
		} else {
			bones.push(bone); // Root bone
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
	if (usdSkeleton.root_node) {
		const rootBone = buildBoneHierarchy(usdSkeleton.root_node, null, null);

		// Collect all bones in depth-first order
		const allBones = [];
		rootBone.traverse((bone) => {
			if (bone.isBone) {
				allBones.push(bone);
			}
		});

		// Compute inverse bind matrices from USD bind transforms
		// These are crucial for proper skinning - they transform vertices from world space to bone-local space
		const boneInverses = [];
		for (const boneData of boneBindMatrices) {
			if (boneData.bindMatrix) {
				// Compute inverse of bind transform
				// NOTE: We do NOT transform the bind matrix here because the scene root rotation
				// already handles the coordinate system change for the entire hierarchy
				const inverseBindMatrix = boneData.bindMatrix.clone().invert();
				boneInverses.push(inverseBindMatrix);
			} else {
				// No bind matrix available - use identity (may cause issues)
				console.warn(`No bind matrix for bone, using identity`);
				boneInverses.push(new THREE.Matrix4());
			}
		}

		console.log(`[Custom] Built skeleton with ${allBones.length} bones, ${boneInverses.length} inverse bind matrices`);

		return { bones: allBones, boneMap, rootBone, boneInverses };
	}

	console.warn('No root_node found in skeleton');
	return { bones: [], boneMap: new Map(), rootBone: null, boneInverses: [] };
}

/**
 * Reset skeleton bones to their rest pose transforms
 * Uses stored rest transforms from bone.userData
 * @param {THREE.Skeleton} skeleton - The skeleton to reset
 */
function resetSkeletonToRestPose(skeleton) {
	if (!skeleton || !skeleton.bones) return;

	for (const bone of skeleton.bones) {
		if (bone.userData.restPosition) {
			bone.position.copy(bone.userData.restPosition);
		}
		if (bone.userData.restQuaternion) {
			bone.quaternion.copy(bone.userData.restQuaternion);
		}
		if (bone.userData.restScale) {
			bone.scale.copy(bone.userData.restScale);
		}
	}

	// Update matrices
	skeleton.bones[0].updateMatrixWorld(true);
	console.log('Reset skeleton to rest pose');
}

/**
 * Create weight visualization material with pseudo-color shader
 * Includes proper Three.js skinning support for SkinnedMesh
 * @returns {THREE.ShaderMaterial} Weight visualization shader material
 */
function createWeightVisualizationMaterial() {
	// Use Three.js shader chunks for skinning support
	const vertexShader = `
		#include <common>
		#include <skinning_pars_vertex>

		varying vec3 vColor;
		varying vec4 vSkinWeight;
		varying vec4 vSkinIndex;

		// Pseudo-color palette for weight visualization
		vec3 getWeightColor(float weight, float index) {
			// Use HSV color wheel based on bone index
			float hue = mod(index * 0.618033988749895, 1.0); // Golden ratio for good distribution
			float sat = 0.8;
			float val = weight;

			// HSV to RGB conversion
			float h = hue * 6.0;
			float c = val * sat;
			float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
			float m = val - c;

			vec3 rgb;
			if (h < 1.0) rgb = vec3(c, x, 0.0);
			else if (h < 2.0) rgb = vec3(x, c, 0.0);
			else if (h < 3.0) rgb = vec3(0.0, c, x);
			else if (h < 4.0) rgb = vec3(0.0, x, c);
			else if (h < 5.0) rgb = vec3(x, 0.0, c);
			else rgb = vec3(c, 0.0, x);

			return rgb + m;
		}

		void main() {
			#include <skinbase_vertex>

			vSkinWeight = skinWeight;
			vSkinIndex = skinIndex;

			// Blend colors based on weights
			vColor = vec3(0.0);
			vColor += getWeightColor(skinWeight.x, skinIndex.x) * skinWeight.x;
			vColor += getWeightColor(skinWeight.y, skinIndex.y) * skinWeight.y;
			vColor += getWeightColor(skinWeight.z, skinIndex.z) * skinWeight.z;
			vColor += getWeightColor(skinWeight.w, skinIndex.w) * skinWeight.w;

			// Apply skinning transformation
			vec3 transformed = vec3(position);
			#include <skinning_vertex>

			vec4 mvPosition = modelViewMatrix * vec4(transformed, 1.0);
			gl_Position = projectionMatrix * mvPosition;
		}
	`;

	const fragmentShader = `
		varying vec3 vColor;
		varying vec4 vSkinWeight;
		varying vec4 vSkinIndex;

		uniform int visualizationMode;

		void main() {
			if (visualizationMode == 0) {
				// Blended weight colors
				gl_FragColor = vec4(vColor, 1.0);
			} else if (visualizationMode == 1) {
				// Primary influence only
				float maxWeight = max(max(vSkinWeight.x, vSkinWeight.y), max(vSkinWeight.z, vSkinWeight.w));
				if (vSkinWeight.x == maxWeight) {
					gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
				} else if (vSkinWeight.y == maxWeight) {
					gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
				} else if (vSkinWeight.z == maxWeight) {
					gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);
				} else {
					gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);
				}
			} else {
				// Weight intensity (grayscale based on total weight)
				float totalWeight = vSkinWeight.x + vSkinWeight.y + vSkinWeight.z + vSkinWeight.w;
				gl_FragColor = vec4(vec3(totalWeight), 1.0);
			}
		}
	`;

	// Merge Three.js skinning uniforms with our custom uniforms
	const uniforms = THREE.UniformsUtils.merge([
		THREE.UniformsLib.skinning,
		{
			visualizationMode: { value: 0 }
		}
	]);

	return new THREE.ShaderMaterial({
		vertexShader: vertexShader,
		fragmentShader: fragmentShader,
		uniforms: uniforms,
		skinning: true,  // Enable skinning for this material
		side: THREE.DoubleSide
	});
}

/**
 * Create joint visualization spheres
 * @param {Array<THREE.Bone>} bones - Array of bones
 * @returns {Array<THREE.Mesh>} Array of sphere meshes
 */
function createJointSpheres(bones) {
	const spheres = [];
	// Use larger sphere radius (0.1) for better visibility
	const geometry = new THREE.SphereGeometry(0.1, 16, 16);

	bones.forEach((bone, index) => {
		const material = new THREE.MeshStandardMaterial({
			color: 0xff0000,
			emissive: 0x000000,
			metalness: 0.3,
			roughness: 0.7
		});

		const sphere = new THREE.Mesh(geometry, material);
		sphere.material.color.setHSL(index / bones.length, 1.0, 0.5);
		sphere.userData.bone = bone;
		sphere.userData.boneName = bone.name;
		spheres.push(sphere);
		scene.add(sphere);
	});

	return spheres;
}

/**
 * Update joint sphere positions
 */
function updateJointSpheres() {
	jointSpheres.forEach(sphere => {
		const bone = sphere.userData.bone;
		const worldPos = new THREE.Vector3();
		bone.getWorldPosition(worldPos);
		sphere.position.copy(worldPos);
	});
}

/**
 * Compute scene bounding box from meshes (static state, ignores skinning deformation)
 * @param {THREE.Object3D} root - Root object to traverse
 * @returns {THREE.Box3} Bounding box
 */
function computeSceneBoundingBox(root) {
	const box = new THREE.Box3();

	root.traverse((child) => {
		if (child.isMesh && child.geometry) {
			child.geometry.computeBoundingBox();
			if (child.geometry.boundingBox) {
				const meshBox = child.geometry.boundingBox.clone();
				meshBox.applyMatrix4(child.matrixWorld);
				box.union(meshBox);
			}
		}
	});

	// If no valid box found, return default
	if (box.isEmpty()) {
		box.set(new THREE.Vector3(-5, -5, -5), new THREE.Vector3(5, 5, 5));
	}

	return box;
}

/**
 * Update shadow camera frustum based on scene bounding box
 * @param {THREE.DirectionalLight} light - Directional light with shadow
 * @param {THREE.Box3} sceneBounds - Scene bounding box
 */
function updateShadowCameraFromBounds(light, sceneBounds) {
	const center = new THREE.Vector3();
	const size = new THREE.Vector3();
	sceneBounds.getCenter(center);
	sceneBounds.getSize(size);

	// Calculate the maximum extent with some padding
	const maxDim = Math.max(size.x, size.y, size.z);
	const padding = maxDim * 0.5;
	const frustumSize = maxDim + padding;

	// Update shadow camera frustum
	light.shadow.camera.left = -frustumSize;
	light.shadow.camera.right = frustumSize;
	light.shadow.camera.top = frustumSize;
	light.shadow.camera.bottom = -frustumSize;

	// Update near/far based on light position and scene bounds
	const lightPos = light.position.clone();
	const lightToCenter = center.clone().sub(lightPos);
	const dist = lightToCenter.length();
	light.shadow.camera.near = Math.max(0.1, dist - maxDim);
	light.shadow.camera.far = dist + maxDim * 2;

	// Move light target to scene center
	light.target.position.copy(center);
	light.target.updateMatrixWorld();

	// Update shadow camera
	light.shadow.camera.updateProjectionMatrix();

	console.log(`Shadow camera updated: frustum=${frustumSize.toFixed(2)}, near=${light.shadow.camera.near.toFixed(2)}, far=${light.shadow.camera.far.toFixed(2)}`);
}

/**
 * Select a joint and attach transform controls
 * @param {THREE.Bone} bone - The bone to select
 * @param {THREE.Mesh} sphere - The corresponding sphere mesh
 */
function selectJoint(bone, sphere) {
	// Deselect previous joint
	if (selectedSphere && selectedSphere !== sphere) {
		selectedSphere.material.emissive.setHex(0x000000);
		selectedSphere.scale.set(1, 1, 1);
	}

	selectedJoint = bone;
	selectedSphere = sphere;

	if (sphere) {
		// Highlight selected sphere
		sphere.material.emissive.setHex(0xffffff);
		sphere.scale.set(1.5, 1.5, 1.5);
	}

	// Attach transform controls to bone
	transformControls.attach(bone);
	transformControls.visible = animationParams.showGizmo;

	// Update UI to show selected joint name
	if (window.updateSelectedJoint) {
		window.updateSelectedJoint(bone.name);
	}
	// Also update GUI if available
	if (typeof updateSelectedJointGUI === 'function') {
		updateSelectedJointGUI(bone.name);
	}

	console.log(`Selected joint: ${bone.name}`);
}

/**
 * Deselect current joint
 */
function deselectJoint() {
	if (selectedSphere) {
		selectedSphere.material.emissive.setHex(0x000000);
		selectedSphere.scale.set(1, 1, 1);
	}

	selectedJoint = null;
	selectedSphere = null;
	transformControls.detach();

	if (window.updateSelectedJoint) {
		window.updateSelectedJoint(null);
	}
	// Also update GUI if available
	if (typeof updateSelectedJointGUI === 'function') {
		updateSelectedJointGUI(null);
	}
}

/**
 * Handle mouse click for joint selection via raycasting
 * @param {MouseEvent} event - Mouse event
 */
function onMouseClick(event) {
	// Calculate mouse position in normalized device coordinates
	mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
	mouse.y = -(event.clientY / window.innerHeight) * 2 + 1;

	// Update raycaster
	raycaster.setFromCamera(mouse, camera);

	// Check for intersections with joint spheres
	const intersects = raycaster.intersectObjects(jointSpheres);

	if (intersects.length > 0) {
		const sphere = intersects[0].object;
		const bone = sphere.userData.bone;
		selectJoint(bone, sphere);
	}
}

/**
 * Calculate bounding box of the scene and fit camera to view entire scene
 * @param {THREE.Object3D} targetObject - Object to fit (usually usdSceneRoot)
 * @param {number} paddingFactor - Extra padding around the object (1.5 = 50% padding)
 */
/**
 * Compute bounding box from skeleton bones
 * This is more accurate for skinned meshes than using geometry bounds
 * @param {THREE.Skeleton} skeleton - The skeleton to compute bounds from
 * @param {THREE.Box3} box - Box to expand with bone positions
 */
function expandBoxBySkeletonBones(skeleton, box) {
	if (!skeleton || !skeleton.bones) return;

	const boneWorldPos = new THREE.Vector3();
	for (const bone of skeleton.bones) {
		bone.getWorldPosition(boneWorldPos);
		box.expandByPoint(boneWorldPos);
	}
}

function fitCameraToScene(targetObject = null, paddingFactor = 2.0) {
	// Use usdSceneRoot to include the Z-up to Y-up transformation
	const target = targetObject || usdSceneRoot;

	if (!target || target.children.length === 0) {
		console.warn('fitCameraToScene: No object to fit');
		return;
	}

	// Update world matrices to ensure transformations are applied
	target.updateMatrixWorld(true);

	// Compute bounding box - for animated scenes, sample across all frames
	const box = new THREE.Box3();

	// Check if we have animations and a skeleton to sample
	const hasAnimation = mixer && usdAnimations.length > 0 && animationAction;
	const hasSkeleton = skinnedMesh && skinnedMesh.skeleton && skinnedMesh.skeleton.bones.length > 0;

	if (hasAnimation && hasSkeleton) {
		// Sample animation at regular intervals to get bounding box for entire animation
		// Use skeleton bone positions for accurate bounds (geometry bounds don't update with skinning)
		const clip = usdAnimations[animationParams.currentAnimation] || usdAnimations[0];
		const duration = clip.duration;
		const numSamples = 20; // Sample 20 frames across the animation for better coverage
		const savedTime = animationAction.time;

		console.log(`fitCameraToScene: Sampling ${numSamples} frames over ${duration.toFixed(2)}s animation (using skeleton bones)`);

		for (let i = 0; i <= numSamples; i++) {
			// Set animation time
			const sampleTime = (i / numSamples) * duration;
			animationAction.time = sampleTime;
			mixer.setTime(sampleTime);

			// Update skeleton
			skinnedMesh.skeleton.update();
			target.updateMatrixWorld(true);

			// Expand bounding box using skeleton bone world positions
			expandBoxBySkeletonBones(skinnedMesh.skeleton, box);
		}

		// Restore original animation time
		animationAction.time = savedTime;
		mixer.setTime(savedTime);
		skinnedMesh.skeleton.update();
		target.updateMatrixWorld(true);
	} else if (hasSkeleton) {
		// Has skeleton but no animation - use bone positions at current pose
		expandBoxBySkeletonBones(skinnedMesh.skeleton, box);
	} else {
		// No skeleton - use geometry bounds (works for non-skinned meshes)
		box.expandByObject(target);
	}

	if (box.isEmpty()) {
		console.warn('fitCameraToScene: Could not compute bounding box');
		return;
	}

	// Add padding to the bounding box to account for mesh volume around bones
	// Bones are at the center of limbs, so we need extra space for the actual mesh
	const boneMargin = 0.3; // 30% margin for mesh around bones
	const size = box.getSize(new THREE.Vector3());
	const marginVec = size.clone().multiplyScalar(boneMargin * 0.5);
	box.min.sub(marginVec);
	box.max.add(marginVec);

	const center = box.getCenter(new THREE.Vector3());
	const finalSize = box.getSize(new THREE.Vector3());
	const maxDim = Math.max(finalSize.x, finalSize.y, finalSize.z);

	// Calculate camera distance based on FOV and bounding box size
	const fov = camera.fov * (Math.PI / 180);
	const cameraDistance = (maxDim / 2) / Math.tan(fov / 2) * paddingFactor;

	// Position camera at an angle (front-right, slightly above)
	const cameraOffset = new THREE.Vector3(
		cameraDistance * 0.7,
		cameraDistance * 0.5,
		cameraDistance * 0.7
	);

	camera.position.copy(center).add(cameraOffset);
	camera.lookAt(center);

	// Update orbit controls target
	controls.target.copy(center);
	controls.update();

	// Update camera near/far planes based on scene size
	camera.near = Math.max(0.01, maxDim * 0.001);
	camera.far = maxDim * 100;
	camera.updateProjectionMatrix();

	console.log(`fitCameraToScene: center=(${center.x.toFixed(2)}, ${center.y.toFixed(2)}, ${center.z.toFixed(2)}), size=(${finalSize.x.toFixed(2)}, ${finalSize.y.toFixed(2)}, ${finalSize.z.toFixed(2)}), distance=${cameraDistance.toFixed(2)}${hasAnimation ? ' (sampled animation)' : ''}`);
}

/**
 * Generate joint hierarchy text with clickable elements
 * @param {Array<THREE.Bone>} bones - Array of bones
 * @returns {string} HTML formatted hierarchy
 */
function generateJointHierarchy(bones) {
	if (bones.length === 0) return '<p>No skeleton loaded</p>';

	let html = '<div style="font-family: monospace; font-size: 12px; line-height: 1.4;">';

	function traverseBone(bone, depth = 0) {
		const indent = '&nbsp;&nbsp;'.repeat(depth);
		const bullet = depth > 0 ? '└─ ' : '● ';
		const color = depth === 0 ? '#ff6b6b' : '#4ecdc4';

		// Make bone name clickable
		html += `<div style="color: ${color}; cursor: pointer; padding: 2px; border-radius: 3px;"
		              class="joint-item"
		              data-bone-name="${bone.name}"
		              onmouseover="this.style.backgroundColor='rgba(255,255,255,0.1)'"
		              onmouseout="this.style.backgroundColor='transparent'">${indent}${bullet}${bone.name}</div>`;

		bone.children.forEach(child => {
			if (child.isBone) {
				traverseBone(child, depth + 1);
			}
		});
	}

	// Find root bones
	bones.forEach(bone => {
		if (!bone.parent || !bone.parent.isBone) {
			traverseBone(bone);
		}
	});

	html += '</div>';
	return html;
}

/**
 * Handle joint selection from hierarchy UI
 * @param {string} boneName - Name of the bone to select
 */
function selectJointByName(boneName) {
	// Find the bone and sphere with matching name
	const sphere = jointSpheres.find(s => s.userData.boneName === boneName);
	if (sphere) {
		const bone = sphere.userData.bone;
		selectJoint(bone, sphere);
	}
}

/**
 * Load USD model from a URL path
 */
async function loadUSDModel() {
	const loader = new TinyUSDZLoader();

	// Initialize the loader (wait for WASM module to load)
	await loader.init({ useZstdCompressedWasm: false, useMemory64: false });

	// Default USD file to load
	const usd_filename = "./assets/AnimFinal_LowRes.usdz";

	console.log(`Loading USD file: ${usd_filename}`);

	// Load USD scene using Promise-based API
	const usd_scene = await new Promise((resolve, reject) => {
		loader.load(
			usd_filename,
			resolve,  // onLoad
			null,     // onProgress
			reject    // onError
		);
	});

	console.log('USD scene loaded:', usd_scene);

	// Process the loaded scene
	await processUSDScene(usd_scene, loader, usd_filename);
}

/**
 * Load USD file and extract skeletal mesh and animations
 * @param {ArrayBuffer} arrayBuffer - USD file data
 * @param {string} filename - File name
 */
async function loadUSDFromArrayBuffer(arrayBuffer, filename) {
	const loader = new TinyUSDZLoader();
	await loader.init({ useZstdCompressedWasm: false, useMemory64: false });

	console.log(`Loading USD from file: ${filename} (${(arrayBuffer.byteLength / 1024).toFixed(2)} KB)`);

	// Parse USD directly from array buffer
	const usd_scene = await new Promise((resolve, reject) => {
		loader.parse(
			new Uint8Array(arrayBuffer),
			filename,
			resolve,  // onLoad
			reject    // onError
		);
	});

	console.log('USD scene loaded:', usd_scene);

	// Process the loaded scene
	await processUSDScene(usd_scene, loader, filename);
}

/**
 * Process loaded USD scene and extract skeletal mesh and animations
 * @param {Object} usd_scene - Loaded USD scene
 * @param {Object} loader - TinyUSDZ loader instance
 * @param {string} filename - File name
 */
async function processUSDScene(usd_scene, loader, filename) {
	// Update current file display in UI
	const currentFileElement = document.getElementById('currentFile');
	if (currentFileElement) {
		// Extract just the filename from the path
		const displayName = filename.split('/').pop();
		currentFileElement.textContent = displayName;
	}

	// Clear existing model
	if (skinnedMesh) {
		characterGroup.remove(skinnedMesh);
		skinnedMesh = null;
	}
	if (skeletonHelper) {
		scene.remove(skeletonHelper);
		skeletonHelper = null;
	}

	// Clear joint spheres
	jointSpheres.forEach(sphere => scene.remove(sphere));
	jointSpheres = [];

	// Deselect any selected joint
	deselectJoint();

	// Reset materials
	originalMaterial = null;
	weightVisualizationMaterial = null;

	// Reset animations
	usdAnimations = [];
	boneMap.clear();
	animationParams.hasUSDAnimations = false;
	animationParams.usdAnimationCount = 0;

	// Store loader globally for debugging
	window.usd_scene = usd_scene;

	// Get scene metadata from the USD file
	const sceneMetadata = usd_scene.getSceneMetadata ? usd_scene.getSceneMetadata() : {};
	let fileUpAxis = sceneMetadata.upAxis || "Y";
	const timeCodesPerSecond = sceneMetadata.timeCodesPerSecond || 24;

	// Update global timeCodesPerSecond and speed parameter
	currentTimeCodesPerSecond = timeCodesPerSecond;
	animationParams.speed = timeCodesPerSecond;

	console.log('=== USD Scene Metadata ===');
	console.log(`upAxis (from metadata): "${fileUpAxis}"`);
	console.log(`metersPerUnit: ${sceneMetadata.metersPerUnit || 1.0}`);
	console.log(`timeCodesPerSecond: ${timeCodesPerSecond}`);

	// Debug: Log mesh skinning data
	const numMeshes = usd_scene.numMeshes ? usd_scene.numMeshes() : 0;
	console.log(`=== Mesh Skinning Data (${numMeshes} meshes) ===`);
	let hasSkinnedMeshData = false;
	// Store USD mesh skinning data for ALL meshes, keyed by mesh name (last part of path)
	const allSkinnedMeshUSDData = new Map();
	let firstGeomBindTransform = null;
	let detectedZUpFromPath = false;
	for (let i = 0; i < numMeshes; i++) {
		const mesh = usd_scene.getMesh(i);
		const hasJointIndices = mesh.jointIndices && mesh.jointIndices.length > 0;
		const hasJointWeights = mesh.jointWeights && mesh.jointWeights.length > 0;
		if (hasJointIndices || hasJointWeights) {
			hasSkinnedMeshData = true;

			// Parse geomBindTransform if available
			let geomBindTransformMatrix = null;
			if (mesh.geomBindTransform && mesh.geomBindTransform.length === 16) {
				geomBindTransformMatrix = new THREE.Matrix4();
				geomBindTransformMatrix.fromArray(Array.from(mesh.geomBindTransform));
			}

			// Extract mesh name from path (e.g., "/root/Armature/Body/Body" -> "Body")
			const meshName = mesh.absPath.split('/').pop();

			const meshData = {
				meshId: i,
				jointIndices: mesh.jointIndices,
				jointWeights: mesh.jointWeights,
				elementSize: mesh.elementSize || 4,
				absPath: mesh.absPath,
				geomBindTransform: geomBindTransformMatrix,
				hasGeomBindTransform: mesh.hasGeomBindTransform || false
			};

			allSkinnedMeshUSDData.set(meshName, meshData);

			// Store first geomBindTransform for UI display
			if (!firstGeomBindTransform && geomBindTransformMatrix) {
				firstGeomBindTransform = geomBindTransformMatrix;
			}

			console.log(`Mesh ${i}: ${mesh.absPath} (name: ${meshName})`);
			console.log(`  - skel_id: ${mesh.skel_id}`);
			console.log(`  - jointIndices: ${mesh.jointIndices ? mesh.jointIndices.length : 0} elements`);
			console.log(`  - jointWeights: ${mesh.jointWeights ? mesh.jointWeights.length : 0} elements`);
			console.log(`  - elementSize (influences per vertex): ${mesh.elementSize}`);
			console.log(`  - hasGeomBindTransform: ${mesh.hasGeomBindTransform || false}`);
		}
		// Check path for Z_UP hint (some models have this in their hierarchy)
		if (mesh.absPath && (mesh.absPath.includes('Z_UP') || mesh.absPath.includes('z_up') || mesh.absPath.includes('Z_up'))) {
			detectedZUpFromPath = true;
		}
	}

	// Update geomBindTransform display in UI
	if (window.updateGeomBindTransform) {
		if (firstGeomBindTransform) {
			window.updateGeomBindTransform(firstGeomBindTransform.elements);
		} else if (hasSkinnedMeshData) {
			window.updateGeomBindTransform(null); // Show "Identity" message
		}
	}

	// For backward compatibility, also set skinnedMeshUSDData to first entry
	const skinnedMeshUSDData = allSkinnedMeshUSDData.size > 0 ? allSkinnedMeshUSDData.values().next().value : null;

	// Hide geomBindTransform display if no skinned mesh data
	if (!hasSkinnedMeshData && window.updateGeomBindTransform) {
		window.updateGeomBindTransform(undefined); // Hide the section
	}

	// Override upAxis if detected from path but metadata says Y
	if (detectedZUpFromPath && fileUpAxis === "Y") {
		console.log(`WARNING: Path contains 'Z_UP' but metadata says upAxis="Y". Overriding to "Z".`);
		fileUpAxis = "Z";
	}

	currentFileUpAxis = fileUpAxis; // Store globally for toggle function
	console.log(`upAxis (final): "${fileUpAxis}"`);
	console.log('========================');

	// Note: geomBindTransform is NOT pre-transformed here.
	// The scene root rotation handles the Z-up to Y-up conversion for all scene elements.
	// The boneInverses ARE pre-transformed (in buildSkeletonFromUSD) because they need to
	// account for the rotated bone world matrices while still computing correct skin deformation.

	// Configure bone reduction (if enabled in UI)
	if (animationParams.enableBoneReduction) {
		loader.setEnableBoneReduction(true);
		loader.setTargetBoneCount(animationParams.targetBoneCount);
		console.log(`Bone reduction enabled: ${animationParams.targetBoneCount} bones per vertex`);
	}

	// Get skeletons
	const numSkeletons = usd_scene.numSkeletons ? usd_scene.numSkeletons() : 0;
	console.log(`Found ${numSkeletons} skeletons in USD file`);

	let bones = [];
	let rootBone = null;

	if (numSkeletons > 0) {
		// Get first skeleton (for simplicity, only support one skeleton for now)
		const usdSkeleton = usd_scene.getSkeleton(0);
		console.log('USD Skeleton:', usdSkeleton);

		if (USE_SKELETAL_HELPER) {
			// ===== SIMPLIFIED APPROACH: Use USDSkeletalHelper =====
			// This creates the skeleton using the helper function
			const threeSkeleton = createThreeSkeletonFromUSD(usdSkeleton);
			bones = threeSkeleton.bones;
			rootBone = bones[0];

			// Build bone map from skeleton
			boneMap = new Map();
			threeSkeleton.bones.forEach(bone => {
				if (bone.userData.joint_id !== undefined) {
					boneMap.set(bone.userData.joint_id, bone);
				}
			});
			console.log(`[Helper] Built skeleton with ${bones.length} bones`);
		} else {
			// ===== CUSTOM APPROACH: Manual skeleton building with advanced features =====
			// Build Three.js skeleton using custom function
			const skeletonData = buildSkeletonFromUSD(usdSkeleton, 0);
			bones = skeletonData.bones;
			boneMap = skeletonData.boneMap;
			rootBone = skeletonData.rootBone;
			// Store inverse bind matrices for proper skinning
			_cachedBoneInverses = skeletonData.boneInverses;
			console.log(`[Custom] Built skeleton with ${bones.length} bones`);
		}

		// Update skeleton info display
		if (window.updateSkeletonInfo) {
			window.updateSkeletonInfo(numSkeletons, bones.length);
		}
	} else {
		console.warn('No skeletons found in USD file');

		// WORKAROUND: If mesh has skinning data but no skeleton, try to build from animation
		if (hasSkinnedMeshData) {
			console.log('Mesh has skinning data but no skeleton hierarchy - attempting fallback skeleton creation');

			// Try to build skeleton from animation channels
			const numAnimations = usd_scene.numAnimations ? usd_scene.numAnimations() : 0;
			if (numAnimations > 0) {
				const anim = usd_scene.getAnimation(0);
				if (anim && anim.channels) {
					// Find max joint_id from skeleton joint channels
					let maxJointId = -1;
					const jointIds = new Set();
					for (const channel of anim.channels) {
						if (channel.target_type === 'SkeletonJoint' && channel.joint_id !== undefined) {
							jointIds.add(channel.joint_id);
							maxJointId = Math.max(maxJointId, channel.joint_id);
						}
					}

					if (maxJointId >= 0) {
						console.log(`Building fallback skeleton from animation: ${jointIds.size} joints (max id: ${maxJointId})`);

						// Create flat bone hierarchy (all bones at root level)
						// This won't have correct rest poses but allows animation to play
						const rootBoneContainer = new THREE.Bone();
						rootBoneContainer.name = 'skeleton_root';
						boneMap.set(-1, rootBoneContainer);

						for (let i = 0; i <= maxJointId; i++) {
							const bone = new THREE.Bone();
							bone.name = `joint_${i}`;
							bone.userData.joint_id = i;
							boneMap.set(i, bone);
							bones.push(bone);

							// Add all bones to root for now (flat hierarchy)
							rootBoneContainer.add(bone);
						}

						// Add root container as first bone
						bones.unshift(rootBoneContainer);
						rootBone = rootBoneContainer;

						console.log(`[Fallback] Built skeleton with ${bones.length} bones (flat hierarchy)`);
					}
				}
			}
		}

		// Update skeleton info display
		if (window.updateSkeletonInfo) {
			window.updateSkeletonInfo(0, bones.length);
		}
	}

	// ===========================================
	// ULTRA-SIMPLIFIED ALTERNATIVE (commented out):
	// If you just want to load and display a skinned mesh with animation,
	// you could replace most of the code below with this single helper call:
	//
	// const result = createSkinnedMeshFromUSD(
	//     usd_scene,
	//     0,  // meshId
	//     0,  // skelId
	//     0,  // animId (optional)
	//     { material: new THREE.MeshStandardMaterial({ color: 0x3399ff, skinning: true }), fps: 24 }
	// );
	// characterGroup.add(result.mesh);
	// if (result.animationClip) {
	//     mixer = helperPlayAnimation(result.mesh, result.animationClip);
	// }
	//
	// However, this demo uses the manual approach to showcase advanced features.
	// ===========================================

	// Get the default root node from USD
	const usdRootNode = usd_scene.getDefaultRootNode();

	// Create default material
	const defaultMtl = TinyUSDZLoaderUtils.createDefaultMaterial();

	const options = {
		overrideMaterial: false,
		envMap: null,
		envMapIntensity: 1.0,
	};

	// Build Three.js node from USD
	const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options);

	// Find skinned meshes in the loaded geometry
	let foundSkinnedMesh = false;
	threeNode.traverse((child) => {
		if (child.isMesh && child.geometry.attributes.skinIndex && child.geometry.attributes.skinWeight) {
			console.log('Found skinned mesh:', child.name);

			// Only create skeleton if we have bones
			if (bones.length > 0 && rootBone) {
				// Create Three.js skeleton with inverse bind matrices from USD
				// The inverse bind matrices are crucial for proper skinning deformation
				const boneInverses = _cachedBoneInverses || [];
				if (boneInverses.length === bones.length) {
					skeleton = new THREE.Skeleton(bones, boneInverses);
					console.log('Created skeleton with USD inverse bind matrices');
				} else {
					skeleton = new THREE.Skeleton(bones);
					console.warn(`Skeleton created without inverse bind matrices (expected ${bones.length}, got ${boneInverses.length})`);
				}

				// Convert to SkinnedMesh if not already
				if (!child.isSkinnedMesh) {
					const skinnedMeshChild = new THREE.SkinnedMesh(child.geometry, child.material);
					skinnedMeshChild.name = child.name;
					skinnedMeshChild.position.copy(child.position);
					skinnedMeshChild.quaternion.copy(child.quaternion);
					skinnedMeshChild.scale.copy(child.scale);
					child = skinnedMeshChild;
				}

				child.add(rootBone); // Add skeleton root to the mesh

				// Bind skeleton with optional geomBindTransform
				// geomBindTransform defines the mesh's transform when it was bound to the skeleton
				if (skinnedMeshUSDData && skinnedMeshUSDData.geomBindTransform) {
					child.bind(skeleton, skinnedMeshUSDData.geomBindTransform);
					console.log('Applied geomBindTransform to skinned mesh');
				} else {
					child.bind(skeleton);
				}

				// Apply extended skinning material if needed (for 8+ bones per vertex)
				// Optionally use WASM bone texture for high bone counts
				let wasmBoneTexture = null;
				if (animationParams.useWASMBoneTexture && skinnedMeshUSDData &&
					skinnedMeshUSDData.elementSize > 8) {
					try {
						wasmBoneTexture = usd_scene.generateBoneTexture(skinnedMeshUSDData.meshId, 0);
						if (wasmBoneTexture.error) {
							console.warn(`WASM bone texture generation failed: ${wasmBoneTexture.error}`);
							wasmBoneTexture = null;
						} else {
							console.log(`Using WASM bone texture: ${wasmBoneTexture.textureWidth}x${wasmBoneTexture.textureHeight}`);
						}
					} catch (texErr) {
						console.warn(`WASM bone texture error: ${texErr.message}`);
						wasmBoneTexture = null;
					}
				}

				if (applyExtendedSkinningIfNeeded(child, { wasmBoneTexture })) {
					console.log('Extended skinning material applied for high bone count');
				}

				child.castShadow = true;
				child.receiveShadow = true;

				skinnedMesh = child;
				skinnedMesh.visible = animationParams.showMesh;
				characterGroup.add(skinnedMesh);
				foundSkinnedMesh = true;

				// Save original material
				originalMaterial = skinnedMesh.material;

				// Create skeleton helper for visualization
				// Add to scene (not usdSceneRoot) since SkeletonHelper uses bone world positions
				// which already include the Z-up to Y-up rotation from usdSceneRoot
				skeletonHelper = new THREE.SkeletonHelper(skinnedMesh);
				skeletonHelper.visible = animationParams.showSkeleton;
				scene.add(skeletonHelper);

				// Create joint spheres
				jointSpheres = createJointSpheres(bones);
				jointSpheres.forEach(sphere => sphere.visible = animationParams.showJoints);

				// Update joint hierarchy display
				if (window.updateJointHierarchy) {
					window.updateJointHierarchy(generateJointHierarchy(bones));
				}
				if (typeof updateJointHierarchyGUI === 'function') {
					updateJointHierarchyGUI(bones);
				}
			} else {
				// No skeleton data - just add as regular mesh
				console.log('No skeleton data available, adding mesh without skeleton');
				child.castShadow = true;
				child.receiveShadow = true;
				child.visible = animationParams.showMesh;
				characterGroup.add(child);

				// Save as skinnedMesh reference even though it's not actually skinned
				skinnedMesh = child;
				originalMaterial = child.material;
				foundSkinnedMesh = true;
			}
		}
	});

	if (!foundSkinnedMesh) {
		// If no skinned mesh found, try to find meshes and add skinning
		console.log('No pre-skinned mesh found, processing all meshes with USD skinning data');

		// Collect all meshes from the loaded scene
		const allMeshes = [];
		threeNode.traverse((child) => {
			if (child.isMesh) {
				allMeshes.push(child);
			}
		});

		console.log(`Found ${allMeshes.length} meshes, ${allSkinnedMeshUSDData.size} have USD skinning data`);

		if (allMeshes.length > 0 && bones.length > 0 && rootBone) {
			// Create skeleton with inverse bind matrices from USD (shared by all meshes)
			const boneInverses = _cachedBoneInverses || [];
			if (boneInverses.length === bones.length) {
				skeleton = new THREE.Skeleton(bones, boneInverses);
				console.log('Created skeleton with USD inverse bind matrices');
			} else {
				skeleton = new THREE.Skeleton(bones);
				console.warn(`Skeleton created without inverse bind matrices (expected ${bones.length}, got ${boneInverses.length})`);
			}

			// Validate rootBone
			if (!rootBone || !(rootBone instanceof THREE.Bone)) {
				console.error('Invalid rootBone:', rootBone);
				throw new Error('rootBone is not a valid THREE.Bone');
			}

			// Process each mesh
			let firstSkinnedMesh = null;
			let processedCount = 0;

			for (const mesh of allMeshes) {
				const meshName = mesh.name;

				// Look up USD skinning data for this mesh
				const meshUSDData = allSkinnedMeshUSDData.get(meshName);

				if (meshUSDData) {
					console.log(`Processing mesh: ${meshName}`);

					// Add skinning attributes to geometry
					const geometry = mesh.geometry;
					if (geometry.attributes.position) {
						const vertexCount = geometry.attributes.position.count;
						const influencesPerVertex = meshUSDData.elementSize || 4;
						const usdVertexCount = Math.floor(meshUSDData.jointIndices.length / influencesPerVertex);

						console.log(`  Adding skinning: ${vertexCount} vertices, ${influencesPerVertex} influences/vertex`);

						if (vertexCount !== usdVertexCount) {
							console.warn(`  Vertex count mismatch: Three.js=${vertexCount}, USD=${usdVertexCount}`);
						}

						// Add skinning attributes
						const skinningConfig = addExtendedSkinningAttributes(
							geometry,
							meshUSDData.jointIndices,
							meshUSDData.jointWeights,
							influencesPerVertex,
							{ normalize: true }
						);

						console.log(`  Skinning mode: ${skinningConfig.mode}`);

						// Create SkinnedMesh
						const newSkinnedMesh = new THREE.SkinnedMesh(geometry, mesh.material);
						newSkinnedMesh.name = meshName;
						newSkinnedMesh.castShadow = true;
						newSkinnedMesh.receiveShadow = true;
						newSkinnedMesh.visible = animationParams.showMesh;

						// For first mesh, add rootBone and bind skeleton
						if (!firstSkinnedMesh) {
							newSkinnedMesh.add(rootBone);
							newSkinnedMesh.updateMatrixWorld(true);

							// Bind with geomBindTransform if available
							if (meshUSDData.geomBindTransform) {
								newSkinnedMesh.bind(skeleton, meshUSDData.geomBindTransform);
								console.log(`  Applied geomBindTransform`);
							} else {
								newSkinnedMesh.bind(skeleton);
							}

							firstSkinnedMesh = newSkinnedMesh;
							skinnedMesh = newSkinnedMesh;
							originalMaterial = newSkinnedMesh.material;
						} else {
							// Subsequent meshes share the skeleton
							if (meshUSDData.geomBindTransform) {
								newSkinnedMesh.bind(skeleton, meshUSDData.geomBindTransform);
							} else {
								newSkinnedMesh.bind(skeleton);
							}
						}

						// Apply extended skinning material if needed
						if (applyExtendedSkinningIfNeeded(newSkinnedMesh)) {
							console.log(`  Extended skinning material applied`);
						}

						characterGroup.add(newSkinnedMesh);
						processedCount++;
					}
				} else {
					// Mesh without skinning data - add as regular mesh
					console.log(`Mesh ${meshName}: no USD skinning data, adding as regular mesh`);
					mesh.castShadow = true;
					mesh.receiveShadow = true;
					mesh.visible = animationParams.showMesh;
					characterGroup.add(mesh);

					if (!skinnedMesh) {
						skinnedMesh = mesh;
						originalMaterial = mesh.material;
					}
				}
			}

			console.log(`Processed ${processedCount} skinned meshes`);

			if (firstSkinnedMesh) {
				// Create skeleton helper for visualization
				skeletonHelper = new THREE.SkeletonHelper(firstSkinnedMesh);
				skeletonHelper.visible = animationParams.showSkeleton;
				scene.add(skeletonHelper);

				// Create joint spheres
				jointSpheres = createJointSpheres(bones);
				jointSpheres.forEach(sphere => sphere.visible = animationParams.showJoints);

				// Update joint hierarchy display
				if (window.updateJointHierarchy) {
					window.updateJointHierarchy(generateJointHierarchy(bones));
				}
				if (typeof updateJointHierarchyGUI === 'function') {
					updateJointHierarchyGUI(bones);
				}
			}
		} else {
			// No skeleton or no mesh - just add the scene as-is
			console.log('Adding scene without skeleton');
			for (const mesh of allMeshes) {
				mesh.castShadow = true;
				mesh.receiveShadow = true;
				mesh.visible = animationParams.showMesh;
				characterGroup.add(mesh);
				if (!skinnedMesh) {
					skinnedMesh = mesh;
					originalMaterial = mesh.material;
				}
			}

			// Update joint hierarchy display (empty)
			if (window.updateJointHierarchy) {
				window.updateJointHierarchy(generateJointHierarchy([]));
			}
			if (typeof updateJointHierarchyGUI === 'function') {
				updateJointHierarchyGUI([]);
			}
		}
	}

	// Extract skeletal animations if available
	try {
		const animationInfos = usd_scene.getAllAnimationInfos ? usd_scene.getAllAnimationInfos() : [];

		// Only try to extract animations if we have bones
		if (bones.length > 0 && boneMap.size > 0) {
			if (USE_SKELETAL_HELPER) {
				// ===== SIMPLIFIED APPROACH: Use USDSkeletalHelper =====
				const numAnimations = usd_scene.numAnimations();
				usdAnimations = [];
				for (let i = 0; i < numAnimations; i++) {
					const usdAnimation = usd_scene.getAnimation(i);
					// Use helper to create animation clip with timeCodesPerSecond from metadata
					const clip = createThreeAnimationClip(usdAnimation, skeleton, { fps: timeCodesPerSecond });
					if (clip.tracks.length > 0) {
						usdAnimations.push(clip);
					}
				}
				console.log(`[Helper] Converted ${usdAnimations.length} animations (fps: ${timeCodesPerSecond})`);
			} else {
				// ===== CUSTOM APPROACH: Custom animation conversion with filtering =====
				usdAnimations = convertUSDSkeletalAnimationsToThreeJS(usd_scene, boneMap, timeCodesPerSecond);
				console.log(`[Custom] Converted ${usdAnimations.length} animations (fps: ${timeCodesPerSecond})`);
			}
		} else {
			console.log('No skeleton data available - skipping animation extraction');
			usdAnimations = [];
		}

		if (usdAnimations.length > 0) {
			console.log(`Extracted ${usdAnimations.length} skeletal animations from USD file`);

			// Update animation parameters
			animationParams.hasUSDAnimations = true;
			animationParams.usdAnimationCount = usdAnimations.length;
			animationParams.currentAnimation = Math.min(0, usdAnimations.length - 1);

			// Update animation list in UI with type information
			if (window.updateAnimationList) {
				window.updateAnimationList(usdAnimations, animationInfos);
			}

			// Log animation details
			usdAnimations.forEach((clip, index) => {
				const info = animationInfos[index];
				let typeStr = '';
				if (info && info.has_skeletal_animation) {
					typeStr = ' [skeletal]';
				}
				console.log(`Animation ${index}: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}${typeStr}`);
			});

			// Create mixer and play first animation
			if (skinnedMesh && usdAnimations.length > 0 && skeleton) {
				mixer = new THREE.AnimationMixer(skinnedMesh);
				playAnimation(0);

				// Update timeline range to match animation duration
				const firstClipDuration = usdAnimations[0].duration;
				if (window.updateTimelineRange) {
					window.updateTimelineRange(firstClipDuration);
				}
			}
		} else {
			if (window.updateAnimationList) {
				window.updateAnimationList([], []);
			}
			console.log('No skeletal animations found in USD file (loading as static mesh)');
		}
	} catch (error) {
		console.error('Error extracting skeletal animations:', error);
		console.log('Continuing without animations...');
		if (window.updateAnimationList) {
			window.updateAnimationList([], []);
		}
	}

	// Z-up to Y-up conversion
	// NOTE: For skinned meshes, rotating the scene root breaks skinning because the
	// inverse bind matrices were computed in the original coordinate system.
	// So we skip the conversion for skinned meshes and keep them in original coordinates.
	const hasSkinnedMeshes = skinnedMesh && skinnedMesh.isSkinnedMesh;

	if (hasSkinnedMeshes) {
		// Don't apply rotation to skinned meshes - it breaks skinning
		usdSceneRoot.rotation.x = 0;
		if (fileUpAxis === "Z") {
			console.log(`[processUSDScene] Skipping Z-up to Y-up rotation (has skinned meshes - rotation breaks skinning)`);
			console.log(`[processUSDScene] Model is in original Z-up coordinate system. Use camera orbit to view from different angles.`);
		}
	} else if (animationParams.applyUpAxisConversion && fileUpAxis === "Z") {
		// Non-skinned meshes can be rotated safely
		usdSceneRoot.rotation.x = -Math.PI / 2;
		console.log(`[processUSDScene] Applied Z-up to Y-up conversion (file upAxis="${fileUpAxis}"): rotation.x =`, usdSceneRoot.rotation.x);
	} else if (fileUpAxis !== "Y" && fileUpAxis !== "Z") {
		usdSceneRoot.rotation.x = 0;
		console.warn(`[processUSDScene] File upAxis is "${fileUpAxis}" (not Y or Z), no conversion applied`);
	} else {
		usdSceneRoot.rotation.x = 0;
		console.log(`[processUSDScene] No upAxis conversion needed (file upAxis="${fileUpAxis}", conversion ${animationParams.applyUpAxisConversion ? 'enabled' : 'disabled'})`);
	}

	// Update shadow camera frustum based on scene bounds
	usdSceneRoot.updateMatrixWorld(true);
	const sceneBounds = computeSceneBoundingBox(usdSceneRoot);
	updateShadowCameraFromBounds(directionalLight, sceneBounds);

	// Fit camera to scene after loading
	fitCameraToScene();
}

/**
 * Play animation by index
 * @param {number} index - Animation index
 */
function playAnimation(index) {
	if (!mixer || index < 0 || index >= usdAnimations.length) {
		return;
	}

	// Stop current animation
	if (animationAction) {
		animationAction.stop();
	}

	// Play new animation
	const clip = usdAnimations[index];
	animationAction = mixer.clipAction(clip);
	animationAction.loop = THREE.LoopRepeat;
	animationAction.clampWhenFinished = false;
	animationAction.play();

	animationParams.currentAnimation = index;
	console.log(`Playing animation: ${clip.name}`);
}

// Listen for file upload events
window.addEventListener('loadUSDFile', async (event) => {
	const file = event.detail.file;
	if (!file) return;

	try {
		const arrayBuffer = await file.arrayBuffer();
		await loadUSDFromArrayBuffer(arrayBuffer, file.name);
		console.log('USD file loaded successfully:', file.name);
	} catch (error) {
		console.error('Failed to load USD file:', error);
	}
});

// Listen for default model reload
window.addEventListener('loadDefaultModel', async () => {
	try {
		await loadUSDModel();
		console.log('Default model reloaded');
	} catch (error) {
		console.error('Failed to reload default model:', error);
	}
});

// Load USD model on startup
loadUSDModel().catch((error) => {
	console.error('Failed to load default USD file:', error);
	console.error('Error details:', error.message || error);
	console.log('Please upload a USD file (with or without skeletal animation).');
	// Update UI to show error
	const currentFileElement = document.getElementById('currentFile');
	if (currentFileElement) {
		currentFileElement.textContent = 'Failed to load (see console)';
		currentFileElement.style.color = '#ff6b6b';
	}
});

// Listen for mouse clicks for joint selection
window.addEventListener('click', onMouseClick);

// Keyboard shortcuts for transform modes
window.addEventListener('keydown', (event) => {
	// Don't trigger if user is typing in an input field
	if (event.target.tagName === 'INPUT' || event.target.tagName === 'TEXTAREA') {
		return;
	}

	switch(event.key.toLowerCase()) {
		case 'g': // Translate/Grab
			animationParams.transformMode = 'translate';
			animationParams.setTransformMode();
			console.log('Transform mode: Translate');
			break;
		case 'r': // Rotate
			animationParams.transformMode = 'rotate';
			animationParams.setTransformMode();
			console.log('Transform mode: Rotate');
			break;
		case 's': // Scale
			if (!event.ctrlKey && !event.metaKey) { // Avoid conflicts with Save
				animationParams.transformMode = 'scale';
				animationParams.setTransformMode();
				console.log('Transform mode: Scale');
			}
			break;
		case 'escape': // Deselect
			deselectJoint();
			console.log('Joint deselected');
			break;
		case 'x': // Toggle local/world space
			animationParams.transformSpace = animationParams.transformSpace === 'local' ? 'world' : 'local';
			animationParams.setTransformSpace();
			console.log(`Transform space: ${animationParams.transformSpace}`);
			break;
	}
});

// Expose selectJointByName for UI hierarchy clicks
window.selectJointByName = selectJointByName;

// Animation parameters
const animationParams = {
	isPlaying: true,
	playPause: function() {
		this.isPlaying = !this.isPlaying;
		if (animationAction) {
			animationAction.paused = !this.isPlaying;
		}
	},
	reset: function() {
		if (animationAction) {
			animationAction.reset();
			animationAction.play();
		}
	},
	resetToRestPose: function() {
		// Stop animation if playing
		if (animationAction) {
			animationAction.stop();
			this.isPlaying = false;
		}
		// Reset skeleton to rest pose
		if (skeleton) {
			resetSkeletonToRestPose(skeleton);
		}
	},
	speed: 24,  // Default to timeCodesPerSecond (updated on file load)
	time: 0,

	// Skeletal animation properties
	hasUSDAnimations: false,
	usdAnimationCount: 0,
	currentAnimation: 0,
	selectAnimation: function() {
		if (this.hasUSDAnimations && this.currentAnimation < usdAnimations.length) {
			playAnimation(this.currentAnimation);
		}
	},

	// Visualization
	showMesh: true,
	toggleMesh: function() {
		if (skinnedMesh) {
			skinnedMesh.visible = this.showMesh;
		}
	},

	showSkeleton: true,
	toggleSkeleton: function() {
		if (skeletonHelper) {
			skeletonHelper.visible = this.showSkeleton;
		}
	},

	// Camera controls
	fitToScene: function() {
		fitCameraToScene();
	},

	// Debug visualization
	showJoints: false,
	toggleJoints: function() {
		jointSpheres.forEach(sphere => sphere.visible = this.showJoints);
	},

	showWeights: false,
	weightVisualizationMode: 0, // 0: blended, 1: primary only, 2: intensity
	toggleWeights: function() {
		if (!skinnedMesh || !originalMaterial) return;

		if (this.showWeights) {
			// Check if mesh has extended skinning and use appropriate visualization
			const skinningConfig = skinnedMesh.geometry?.userData?.extendedSkinning;
			if (skinningConfig && skinningConfig.mode > SkinningMode.STANDARD) {
				// Use extended weight visualization for 8+ bone meshes
				if (!weightVisualizationMaterial || !weightVisualizationMaterial.userData?.isExtended) {
					weightVisualizationMaterial = createExtendedWeightVisualizationMaterial({
						maxInfluences: skinningConfig.maxInfluences
					});
					weightVisualizationMaterial.userData = { isExtended: true };
				}
			} else {
				// Use standard weight visualization for 4-bone meshes
				if (!weightVisualizationMaterial || weightVisualizationMaterial.userData?.isExtended) {
					weightVisualizationMaterial = createWeightVisualizationMaterial();
					weightVisualizationMaterial.userData = { isExtended: false };
				}
			}
			weightVisualizationMaterial.uniforms.visualizationMode.value = this.weightVisualizationMode;
			skinnedMesh.material = weightVisualizationMaterial;
		} else {
			skinnedMesh.material = originalMaterial;
		}
	},

	updateWeightMode: function() {
		if (this.showWeights && weightVisualizationMaterial) {
			weightVisualizationMaterial.uniforms.visualizationMode.value = this.weightVisualizationMode;
		}
	},

	// Gizmo controls
	showGizmo: true,
	transformMode: 'translate',
	transformSpace: 'local',
	toggleGizmo: function() {
		transformControls.visible = this.showGizmo && selectedJoint !== null;
	},
	setTransformMode: function() {
		transformControls.setMode(this.transformMode);
	},
	setTransformSpace: function() {
		transformControls.setSpace(this.transformSpace);
	},
	deselectJoint: function() {
		deselectJoint();
	},

	// Bone reduction settings (applied on next file load)
	enableBoneReduction: false,
	targetBoneCount: 4,

	// WASM bone texture for GPU skinning (applied on next file load)
	// When enabled, generates bone texture in WASM for efficient GPU skinning
	// with high bone counts (16+ bones per vertex)
	useWASMBoneTexture: false,

	// Shadows
	enableShadows: true,
	toggleShadows: function() {
		renderer.shadowMap.enabled = this.enableShadows;
		directionalLight.castShadow = this.enableShadows;
		// Need to update materials and re-render
		scene.traverse((child) => {
			if (child.isMesh) {
				child.material.needsUpdate = true;
			}
		});
		console.log(`Shadows ${this.enableShadows ? 'enabled' : 'disabled'}`);
	},

	// Up axis conversion (Z-up to Y-up)
	// Note: For skinned meshes, rotation is not applied because it breaks skinning.
	// For non-skinned meshes, the rotation can be toggled.
	applyUpAxisConversion: true,
	toggleUpAxisConversion: function() {
		// Check if we have skinned meshes
		const hasSkinnedMeshes = skinnedMesh && skinnedMesh.isSkinnedMesh;

		if (hasSkinnedMeshes) {
			// For skinned meshes, don't apply rotation - it breaks skinning
			console.log(`[toggleUpAxisConversion] Skinned mesh detected - Z-up to Y-up rotation disabled to preserve skinning`);
			console.log(`[toggleUpAxisConversion] Use camera orbit to view the model from different angles`);
			usdSceneRoot.rotation.x = 0;
		} else if (this.applyUpAxisConversion && currentFileUpAxis === "Z") {
			// Non-skinned meshes can be rotated
			usdSceneRoot.rotation.x = -Math.PI / 2;
			console.log(`[toggleUpAxisConversion] Applied Z-up to Y-up rotation: usdSceneRoot.rotation.x =`, usdSceneRoot.rotation.x);
		} else {
			usdSceneRoot.rotation.x = 0;
			console.log(`[toggleUpAxisConversion] Reset rotation: usdSceneRoot.rotation.x =`, usdSceneRoot.rotation.x);
		}
	}
};

// GUI setup
const gui = new GUI();
gui.title('Skeletal Animation Controls');

// Playback controls
const playbackFolder = gui.addFolder('Playback');
playbackFolder.add(animationParams, 'playPause').name('Play / Pause');
playbackFolder.add(animationParams, 'reset').name('Reset Animation');
playbackFolder.add(animationParams, 'resetToRestPose').name('Reset to Rest Pose');
playbackFolder.add(animationParams, 'speed', 1, 120, 1).name('Speed').onChange(() => {
	if (animationAction) {
		// Speed represents playback FPS (timeCodes per second)
		animationAction.setEffectiveTimeScale(animationParams.speed);
	}
});
const timelineController = playbackFolder.add(animationParams, 'time', 0, 30, 0.01)
	.name('Timeline').listen().onChange(() => {
		if (animationAction) {
			animationAction.time = animationParams.time;
		}
	});
playbackFolder.open();

// Function to update timeline range when animation loads
window.updateTimelineRange = function(duration) {
	console.log('updateTimelineRange called with duration:', duration);
	if (timelineController && duration > 0) {
		// lil-gui: need to destroy and recreate the controller to change range
		// Or use the _max property directly
		timelineController._max = duration;
		timelineController.$slider.max = duration;
		timelineController.updateDisplay();
		console.log('Timeline range updated to:', duration);
	}
};

// Animation controls
const animationFolder = gui.addFolder('Skeletal Animations');
animationFolder.add(animationParams, 'hasUSDAnimations').name('Has Animations').listen().disable();
animationFolder.add(animationParams, 'usdAnimationCount').name('Animation Count').listen().disable();
animationFolder.add(animationParams, 'currentAnimation', 0, 10, 1)
	.name('Select Animation')
	.listen()
	.onChange(() => animationParams.selectAnimation());
animationFolder.open();

// Visualization controls
const visualFolder = gui.addFolder('Visualization');
visualFolder.add(animationParams, 'showMesh')
	.name('Show Mesh')
	.onChange(() => animationParams.toggleMesh());
visualFolder.add(animationParams, 'showSkeleton')
	.name('Show Skeleton')
	.onChange(() => animationParams.toggleSkeleton());
visualFolder.add(animationParams, 'enableShadows')
	.name('Enable Shadows')
	.onChange(() => animationParams.toggleShadows());
visualFolder.add(animationParams, 'applyUpAxisConversion')
	.name('Z-up to Y-up')
	.onChange(() => animationParams.toggleUpAxisConversion());
visualFolder.add(animationParams, 'fitToScene').name('Fit to Scene');
visualFolder.open();

// Debug visualization controls
const debugFolder = gui.addFolder('Debug Visualization');
debugFolder.add(animationParams, 'showJoints')
	.name('Show Joints')
	.onChange(() => animationParams.toggleJoints());
debugFolder.add(animationParams, 'showWeights')
	.name('Show Weights')
	.onChange(() => animationParams.toggleWeights());
debugFolder.add(animationParams, 'weightVisualizationMode', {
	'Blended Colors': 0,
	'Primary Influence': 1,
	'Weight Intensity': 2
})
	.name('Weight Mode')
	.onChange(() => animationParams.updateWeightMode());
debugFolder.open();

// Gizmo controls
const gizmoFolder = gui.addFolder('Joint Manipulation');
gizmoFolder.add(animationParams, 'showGizmo')
	.name('Show Gizmo')
	.onChange(() => animationParams.toggleGizmo());
gizmoFolder.add(animationParams, 'transformMode', {
	'Translate': 'translate',
	'Rotate': 'rotate',
	'Scale': 'scale'
})
	.name('Transform Mode')
	.onChange(() => animationParams.setTransformMode());
gizmoFolder.add(animationParams, 'transformSpace', {
	'Local': 'local',
	'World': 'world'
})
	.name('Space')
	.onChange(() => animationParams.setTransformSpace());
gizmoFolder.add(animationParams, 'deselectJoint')
	.name('Deselect Joint');
gizmoFolder.open();

// Bone reduction settings
const boneReductionFolder = gui.addFolder('Bone Settings (Next Load)');
boneReductionFolder.add(animationParams, 'enableBoneReduction')
	.name('Enable Reduction')
	.onChange(() => {
		console.log(`Bone reduction ${animationParams.enableBoneReduction ? 'enabled' : 'disabled'} (applies on next file load)`);
	});
boneReductionFolder.add(animationParams, 'targetBoneCount', 1, 8, 1)
	.name('Target Bone Count')
	.onChange(() => {
		console.log(`Target bone count set to ${animationParams.targetBoneCount} (applies on next file load)`);
	});
boneReductionFolder.add(animationParams, 'useWASMBoneTexture')
	.name('WASM Bone Texture')
	.onChange(() => {
		console.log(`WASM bone texture ${animationParams.useWASMBoneTexture ? 'enabled' : 'disabled'} (applies on next file load)`);
	});
boneReductionFolder.close(); // Closed by default as advanced feature

// Joint Hierarchy folder (with custom HTML content)
const jointHierarchyFolder = gui.addFolder('Joint Hierarchy');

// Selected joint indicator (add this first so we can append custom content after)
const selectedJointInfo = {
	selected: 'None'
};
jointHierarchyFolder.add(selectedJointInfo, 'selected').name('Selected').listen().disable();

// Create custom container for joint hierarchy
const jointHierarchyContainer = document.createElement('div');
jointHierarchyContainer.id = 'gui-joint-hierarchy';
jointHierarchyContainer.style.cssText = `
	max-height: 200px;
	overflow-y: auto;
	padding: 4px 8px;
	font-family: monospace;
	font-size: 11px;
	line-height: 1.4;
	background: rgba(0, 0, 0, 0.2);
	border-radius: 3px;
	margin: 4px;
`;
jointHierarchyContainer.innerHTML = '<span style="color: #888;">No skeleton loaded</span>';

// Safely append custom container to folder
try {
	// Try $children first (lil-gui internal)
	if (jointHierarchyFolder.$children) {
		jointHierarchyFolder.$children.appendChild(jointHierarchyContainer);
	} else if (jointHierarchyFolder.domElement) {
		// Fallback: find children container in domElement
		const childrenContainer = jointHierarchyFolder.domElement.querySelector('.children');
		if (childrenContainer) {
			childrenContainer.appendChild(jointHierarchyContainer);
		} else {
			jointHierarchyFolder.domElement.appendChild(jointHierarchyContainer);
		}
	}
} catch (e) {
	console.warn('Could not append joint hierarchy container to GUI:', e);
}

jointHierarchyFolder.open();

// Function to update joint hierarchy in GUI
function updateJointHierarchyGUI(bones) {
	if (!jointHierarchyContainer) return;

	if (bones.length === 0) {
		jointHierarchyContainer.innerHTML = '<span style="color: #888;">No skeleton loaded</span>';
		return;
	}

	let html = '';

	function traverseBone(bone, depth = 0) {
		const indent = '&nbsp;&nbsp;'.repeat(depth);
		const bullet = depth > 0 ? '└─ ' : '● ';
		const color = depth === 0 ? '#ff6b6b' : '#4ecdc4';

		html += `<div style="color: ${color}; cursor: pointer; padding: 2px; border-radius: 3px;"
		              class="gui-joint-item"
		              data-bone-name="${bone.name}"
		              onmouseover="this.style.backgroundColor='rgba(255,255,255,0.15)'"
		              onmouseout="this.style.backgroundColor='transparent'">${indent}${bullet}${bone.name}</div>`;

		bone.children.forEach(child => {
			if (child.isBone) {
				traverseBone(child, depth + 1);
			}
		});
	}

	// Find root bones
	bones.forEach(bone => {
		if (!bone.parent || !bone.parent.isBone) {
			traverseBone(bone);
		}
	});

	jointHierarchyContainer.innerHTML = html;

	// Add click handlers
	const jointItems = jointHierarchyContainer.querySelectorAll('.gui-joint-item');
	jointItems.forEach(item => {
		item.addEventListener('click', function() {
			const boneName = this.getAttribute('data-bone-name');
			selectJointByName(boneName);
		});
	});
}

// Function to update selected joint display in GUI
function updateSelectedJointGUI(jointName) {
	selectedJointInfo.selected = jointName || 'None';
}

// Info folder
const infoFolder = gui.addFolder('Info');
const info = {
	fps: 0,
	objects: scene.children.length
};
infoFolder.add(info, 'fps').name('FPS').listen().disable();
infoFolder.add(info, 'objects').name('Objects').listen().disable();
infoFolder.open();

// Window resize handler
window.addEventListener('resize', onWindowResize, false);

function onWindowResize() {
	camera.aspect = window.innerWidth / window.innerHeight;
	camera.updateProjectionMatrix();
	renderer.setSize(window.innerWidth, window.innerHeight);
}

// FPS calculation
let lastTime = performance.now();
let frames = 0;
let fpsUpdateTime = 0;

// Animation loop
function animate() {
	requestAnimationFrame(animate);

	const currentTime = performance.now();
	const deltaTime = (currentTime - lastTime) / 1000; // Convert to seconds
	lastTime = currentTime;

	// Update FPS
	frames++;
	fpsUpdateTime += deltaTime;
	if (fpsUpdateTime >= 0.5) {
		info.fps = Math.round(frames / fpsUpdateTime);
		frames = 0;
		fpsUpdateTime = 0;
	}

	// Update animation mixer
	// Speed represents playback FPS (timeCodes per second)
	// Animation times are in timeCodes, so speed directly controls how many timeCodes advance per second
	if (mixer && animationParams.isPlaying) {
		mixer.update(deltaTime * animationParams.speed);

		// Update time display
		if (animationAction) {
			animationParams.time = animationAction.time;
		}
	}

	// Update skeleton helper
	if (skeletonHelper && typeof skeletonHelper.update === 'function') {
		skeletonHelper.update();
	}

	// Update joint spheres
	if (animationParams.showJoints && jointSpheres.length > 0) {
		updateJointSpheres();
	}

	// Update transform controls position if a joint is selected
	if (selectedJoint && transformControls && transformControls.visible) {
		if (typeof transformControls.updateMatrixWorld === 'function') {
			transformControls.updateMatrixWorld();
		}
	}

	// Update controls
	controls.update();

	// Update object count
	info.objects = scene.children.length;

	// Render
	renderer.render(scene, camera);
}

// Start animation loop
animate();
